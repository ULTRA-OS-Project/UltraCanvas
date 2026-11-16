#!/usr/bin/env python3
"""A small Mastodon in memory, for MastodonProviderTests: the part of the REST
API the UltraIM Mastodon provider uses - app registration and OAuth (the
consent page approves at once, as --oauth-user), the account, lookup and
search, direct statuses (posted, read, edited, deleted, their context),
conversations (read, removed), and media (an upload that is processed: a
picture at once, a video or sound after one look).

Standard library only. Prints {"url": "http://127.0.0.1:<port>"} once it
listens; runs until it is killed.

    mastodon_fake_server.py --users alice,bob,carol [--oauth-user carol]

Each user's access token is "<name>-token".
"""

import argparse
import base64
import hashlib
import html
import itertools
import json
import re
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOCK = threading.Lock()
IDS = itertools.count(100000000000000000)
ACCOUNTS = {}          # name -> account
TOKENS = {}            # token -> name
STATUSES = {}          # id -> status (with "_recipients", "_media")
MEDIA = {}             # id -> {"type", "name", "bytes", "looks"}
APPS = {}              # client_id -> app
CODES = {}             # code -> {"user", "challenge", "client_id", "redirect_uri"}
# A conversation is a thread, as on Mastodon: a status that answers another
# is in its thread, one that answers nothing starts one ("_thread").
UNREAD = {}            # (user, thread) -> bool
HIDDEN = set()         # (user, thread): removed from the list
CONV_IDS = {}          # (user, thread) -> conversation id
IDEMPOTENT = {}        # (user, key) -> status id
OAUTH_USER = "carol"
BASE = ""


def next_id():
    return str(next(IDS))


def now_iso():
    t = time.time()
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(t)) + ".%03dZ" % int((t % 1) * 1000)


def account_json(name):
    a = ACCOUNTS[name]
    return {"id": a["id"], "username": name, "acct": name, "display_name": name.capitalize(),
            "avatar_static": BASE + "/avatars/" + name + ".png", "url": BASE + "/@" + name}


def media_json(m):
    ready = m["type"] == "image" or m["looks"] > 0
    url = BASE + "/media/" + m["id"] + "/" + m["name"] if ready else None
    meta = {"original": {"width": 64, "height": 48}} if m["type"] == "image" else {"original": {"duration": 2.5}}
    return {"id": m["id"], "type": m["type"], "url": url, "remote_url": None, "meta": meta}


def status_json(s):
    out = {k: v for k, v in s.items() if not k.startswith("_")}
    out["account"] = account_json(s["_author"])
    out["media_attachments"] = [media_json(MEDIA[i]) for i in s["_media"]]
    return out


def content_html(text):
    # Mentions as Mastodon writes them: an h-card link around "@<span>name</span>".
    def mention(m):
        name = m.group(1).split("@")[0]
        return ('<span class="h-card"><a href="%s/@%s" class="u-url mention">@<span>%s</span></a></span>'
                % (BASE, name, name))
    paragraphs = []
    for para in text.split("\n\n"):
        escaped = html.escape(para).replace("\n", "<br />")
        paragraphs.append("<p>" + re.sub(r"@([A-Za-z0-9_]+(?:@[A-Za-z0-9_.:-]+)?)", mention, escaped) + "</p>")
    return "".join(paragraphs)


def visible(s, user):
    return user in s["_recipients"]


def people_of(s, user):
    return frozenset(r for r in s["_recipients"] if r != user)


def conversations_of(user):
    latest = {}
    for s in STATUSES.values():
        if not visible(s, user) or s["visibility"] != "direct":
            continue
        if not people_of(s, user):
            continue
        key = s["_thread"]
        if key not in latest or int(s["id"]) > int(latest[key]["id"]):
            latest[key] = s
    out = []
    for key, s in latest.items():
        if (user, key) in HIDDEN:
            continue
        cid = CONV_IDS.setdefault((user, key), next_id())
        out.append({"id": cid, "unread": UNREAD.get((user, key), False),
                    "accounts": [account_json(n) for n in sorted(people_of(s, user))], "last_status": status_json(s)})
    out.sort(key=lambda c: -int(c["last_status"]["id"]))
    return out


def conversation_key(user, cid):
    for (u, key), value in CONV_IDS.items():
        if u == user and value == cid:
            return key
    return None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    # ---- plumbing -----------------------------------------------------------
    def reply(self, status, body, headers=None):
        data = json.dumps(body).encode() if not isinstance(body, bytes) else body
        self.send_response(status)
        self.send_header("Content-Type", "application/json" if not isinstance(body, bytes) else "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def error(self, status, text):
        self.reply(status, {"error": text})

    def body_bytes(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def body_fields(self, raw):
        kind = self.headers.get("Content-Type", "")
        if kind.startswith("application/json"):
            return json.loads(raw.decode() or "{}")
        if kind.startswith("application/x-www-form-urlencoded"):
            return {k: v[0] for k, v in urllib.parse.parse_qs(raw.decode()).items()}
        return {}

    def user(self):
        auth = self.headers.get("Authorization", "")
        return TOKENS.get(auth[7:]) if auth.startswith("Bearer ") else None

    def route(self, method):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = {k: v[0] for k, v in urllib.parse.parse_qs(parsed.query).items()}
        raw = self.body_bytes() if method in ("POST", "PUT") else b""
        with LOCK:
            try:
                self.dispatch(method, path, query, raw)
            except BrokenPipeError:
                pass

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PUT(self):
        self.route("PUT")

    def do_DELETE(self):
        self.route("DELETE")

    # ---- the API ------------------------------------------------------------
    def dispatch(self, method, path, query, raw):
        m = re.match(r"^/media/(\d+)/(.+)$", path)
        if method == "GET" and m:
            media = MEDIA.get(m.group(1))
            return self.reply(200, media["bytes"]) if media else self.error(404, "Record not found")
        if method == "POST" and path == "/api/v1/apps":
            fields = self.body_fields(raw)
            cid, secret = "client-" + next_id(), "secret-" + next_id()
            APPS[cid] = {"secret": secret, "redirect_uri": fields.get("redirect_uris", "")}
            return self.reply(200, {"client_id": cid, "client_secret": secret, "name": fields.get("client_name")})
        if method == "GET" and path == "/oauth/authorize":
            app = APPS.get(query.get("client_id", ""))
            if not app or app["redirect_uri"] != query.get("redirect_uri"):
                return self.error(400, "invalid_client")
            code = "code-" + next_id()
            CODES[code] = {"user": OAUTH_USER, "challenge": query.get("code_challenge"), "client_id": query["client_id"],
                           "redirect_uri": query["redirect_uri"]}
            target = query["redirect_uri"] + "?" + urllib.parse.urlencode({"code": code, "state": query.get("state", "")})
            self.send_response(302)
            self.send_header("Location", target)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if method == "POST" and path == "/oauth/token":
            fields = self.body_fields(raw)
            grant = CODES.pop(fields.get("code", ""), None)
            app = APPS.get(fields.get("client_id", ""))
            if not grant or not app or app["secret"] != fields.get("client_secret") or \
                    grant["client_id"] != fields.get("client_id") or grant["redirect_uri"] != fields.get("redirect_uri"):
                return self.reply(400, {"error": "invalid_grant", "error_description": "The code is not valid"})
            if grant["challenge"]:
                verifier = fields.get("code_verifier", "")
                digest = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
                if digest != grant["challenge"]:
                    return self.reply(400, {"error": "invalid_grant", "error_description": "PKCE"})
            return self.reply(200, {"access_token": grant["user"] + "-token", "token_type": "Bearer",
                                    "scope": "read write", "created_at": int(time.time())})

        user = self.user()
        if not user:
            return self.error(401, "The access token is invalid")

        if method == "GET" and path == "/api/v1/accounts/verify_credentials":
            return self.reply(200, account_json(user))
        if method == "GET" and path == "/api/v1/accounts/lookup":
            acct = query.get("acct", "").lstrip("@")
            host = urllib.parse.urlparse(BASE).netloc
            if acct.endswith("@" + host):
                acct = acct[:-len(host) - 1]
            return self.reply(200, account_json(acct)) if acct in ACCOUNTS else self.error(404, "Record not found")
        if method == "GET" and path == "/api/v2/search":
            q = query.get("q", "").lstrip("@").split("@")[0]
            return self.reply(200, {"accounts": [account_json(q)] if q in ACCOUNTS else [], "statuses": [], "hashtags": []})

        if method == "GET" and path == "/api/v1/conversations":
            return self.reply(200, conversations_of(user))
        m = re.match(r"^/api/v1/conversations/(\d+)(/read)?$", path)
        if m:
            key = conversation_key(user, m.group(1))
            if key is None:
                return self.error(404, "Record not found")
            if method == "POST" and m.group(2):
                UNREAD[(user, key)] = False
                return self.reply(200, {"id": m.group(1), "unread": False})
            if method == "DELETE" and not m.group(2):
                HIDDEN.add((user, key))
                return self.reply(200, {})

        if method == "POST" and path == "/api/v2/media":
            boundary = self.headers.get("Content-Type", "").split("boundary=")[-1].encode()
            part = raw.split(b"--" + boundary)[1]
            head, _, data = part.partition(b"\r\n\r\n")
            data = data[:-2] if data.endswith(b"\r\n") else data
            name = re.search(rb'filename="([^"]*)"', head).group(1).decode()
            kind = re.search(rb"Content-Type: ([^\r\n]+)", head).group(1).decode()
            mtype = "image" if kind.startswith("image/") else "video" if kind.startswith("video/") else \
                "audio" if kind.startswith("audio/") else "unknown"
            mid = next_id()
            MEDIA[mid] = {"id": mid, "type": mtype, "name": name, "bytes": data, "looks": 0, "owner": user}
            return self.reply(200 if mtype == "image" else 202, media_json(MEDIA[mid]))
        m = re.match(r"^/api/v1/media/(\d+)$", path)
        if method == "GET" and m:
            media = MEDIA.get(m.group(1))
            if not media:
                return self.error(404, "Record not found")
            if media["type"] != "image" and media["looks"] == 0:
                media["looks"] += 1
                return self.reply(206, media_json(media))
            media["looks"] += 1
            return self.reply(200, media_json(media))

        if method == "POST" and path == "/api/v1/statuses":
            key = self.headers.get("Idempotency-Key")
            if key and (user, key) in IDEMPOTENT:
                return self.reply(200, status_json(STATUSES[IDEMPOTENT[(user, key)]]))
            fields = self.body_fields(raw)
            text = fields.get("status", "")
            mentioned = {n.split("@")[0] for n in re.findall(r"@([A-Za-z0-9_]+(?:@[A-Za-z0-9_.:-]+)?)", text)}
            mentioned = {n for n in mentioned if n in ACCOUNTS}
            sid = next_id()
            parent = STATUSES.get(fields.get("in_reply_to_id") or "")
            s = {"_thread": parent["_thread"] if parent else sid,
                 "id": sid, "created_at": now_iso(), "in_reply_to_id": fields.get("in_reply_to_id"),
                 "visibility": fields.get("visibility", "public"), "content": content_html(text),
                 "spoiler_text": fields.get("spoiler_text", ""), "edited_at": None,
                 "_author": user, "_recipients": mentioned | {user}, "_media": list(fields.get("media_ids", []))}
            STATUSES[sid] = s
            if key:
                IDEMPOTENT[(user, key)] = sid
            for r in s["_recipients"]:
                HIDDEN.discard((r, s["_thread"]))
                UNREAD[(r, s["_thread"])] = r != user
            return self.reply(200, status_json(s))
        m = re.match(r"^/api/v1/statuses/(\d+)(/context)?$", path)
        if m:
            s = STATUSES.get(m.group(1))
            if not s or not visible(s, user):
                return self.error(404, "Record not found")
            if method == "GET" and m.group(2):
                ancestors, at = [], s
                while at["in_reply_to_id"] and at["in_reply_to_id"] in STATUSES:
                    at = STATUSES[at["in_reply_to_id"]]
                    if visible(at, user):
                        ancestors.insert(0, status_json(at))
                descendants, frontier = [], [s["id"]]
                while frontier:
                    parent = frontier.pop(0)
                    for child in sorted(STATUSES.values(), key=lambda x: int(x["id"])):
                        if child["in_reply_to_id"] == parent:
                            frontier.append(child["id"])
                            if visible(child, user):
                                descendants.append(status_json(child))
                return self.reply(200, {"ancestors": ancestors, "descendants": descendants})
            if method == "GET":
                return self.reply(200, status_json(s))
            if s["_author"] != user:
                return self.error(403, "This action is not allowed")
            if method == "PUT":
                fields = self.body_fields(raw)
                s["content"] = content_html(fields.get("status", ""))
                s["_media"] = list(fields.get("media_ids", []))
                s["edited_at"] = now_iso()
                return self.reply(200, status_json(s))
            if method == "DELETE":
                del STATUSES[s["id"]]
                for child in STATUSES.values():
                    if child["in_reply_to_id"] == s["id"]:
                        child["in_reply_to_id"] = s["in_reply_to_id"]
                return self.reply(200, status_json(s))

        # The test's own: what the server holds, for checks.
        if method == "GET" and path == "/_test/statuses":
            return self.reply(200, [status_json(s) for s in sorted(STATUSES.values(), key=lambda x: int(x["id"]))])
        return self.error(404, "No route " + method + " " + path)


def main():
    global OAUTH_USER, BASE
    parser = argparse.ArgumentParser()
    parser.add_argument("--users", default="alice,bob,carol")
    parser.add_argument("--oauth-user", default="carol")
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    OAUTH_USER = args.oauth_user
    for name in args.users.split(","):
        ACCOUNTS[name] = {"id": next_id()}
        TOKENS[name + "-token"] = name
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    BASE = "http://127.0.0.1:%d" % server.server_address[1]
    print(json.dumps({"url": BASE}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
