#!/usr/bin/env python3
"""A small Mattermost in memory, for MattermostProviderTests: the part of the
REST API (v4) the UltraIM Mattermost provider uses - signing in (a password,
with a multi-factor code for --mfa-user; a personal access token), users,
one team ("Team A") with its channels "town-square" and "off-topic" that
everyone is in, direct and group messages, posts (created, read by pages and
"since", edited, deleted - with their replies), files (uploaded, their info,
their bytes), unread counts, viewing a channel, leaving one, and the
preferences that hide a direct or group message.

Standard library only. Prints {"url": "http://127.0.0.1:<port>"} once it
listens; runs until it is killed.

    mattermost_fake_server.py --users alice,bob,carol [--mfa-user dave]

Each user's password is "<name>-pass", their personal access token
"<name>-pat", their e-mail address "<name>@example.org"; the --mfa-user's
code is 123456.
"""

import argparse
import json
import re
import secrets
import string
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOCK = threading.Lock()
USERS = {}             # id -> user (with "_password", "_mfa")
TOKENS = {}            # token -> user id (sessions and personal access tokens)
SESSIONS = set()       # the tokens that are sessions (revocable by /_test/revoke)
TEAM = {}
CHANNELS = {}          # id -> channel (with "_members": set of user ids)
MEMBERS = {}           # (channel id, user id) -> {"msg_count", "last_viewed_at"}
POSTS = {}             # id -> post
FILES = {}             # id -> {"info", "bytes"}
PREFS = {}             # (user id, category, name) -> value
PENDING = {}           # (user id, pending_post_id) -> post id
MFA_CODE = "123456"
_LAST_MS = [0]


def new_id():
    alphabet = string.ascii_lowercase + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(26))


def now_ms():
    # Strictly growing, so that every change has a time of its own.
    t = int(time.time() * 1000)
    _LAST_MS[0] = max(t, _LAST_MS[0] + 1)
    return _LAST_MS[0]


def user_json(u):
    return {k: v for k, v in u.items() if not k.startswith("_")}


def channel_json(c):
    return {k: v for k, v in c.items() if not k.startswith("_")}


def by_name(name):
    for u in USERS.values():
        if u["username"] == name:
            return u
    return None


def by_login(login):
    login = login.lower()
    for u in USERS.values():
        if u["username"] == login or u["email"] == login:
            return u
    return None


def make_channel(kind, name, display, creator, members, team_id=""):
    t = now_ms()
    cid = new_id()
    CHANNELS[cid] = {"id": cid, "create_at": t, "update_at": t, "delete_at": 0, "team_id": team_id, "type": kind,
                     "display_name": display, "name": name, "header": "", "purpose": "", "last_post_at": 0,
                     "last_root_post_at": 0, "total_msg_count": 0, "creator_id": creator, "_members": set()}
    for uid in members:
        join(cid, uid)
    return CHANNELS[cid]


def join(cid, uid):
    c = CHANNELS[cid]
    c["_members"].add(uid)
    MEMBERS[(cid, uid)] = {"msg_count": c["total_msg_count"], "last_viewed_at": now_ms()}


def add_post(channel, uid, message, root_id="", file_ids=None, pending="", kind=""):
    t = now_ms()
    pid = new_id()
    files = [FILES[f]["info"] for f in (file_ids or [])]
    for f in file_ids or []:
        FILES[f]["info"]["post_id"] = pid
    post = {"id": pid, "create_at": t, "update_at": t, "edit_at": 0, "delete_at": 0, "is_pinned": False,
            "user_id": uid, "channel_id": channel["id"], "root_id": root_id, "original_id": "", "message": message,
            "type": kind, "props": {}, "hashtags": "", "pending_post_id": pending, "file_ids": list(file_ids or []),
            "metadata": {"files": files} if files else {}}
    POSTS[pid] = post
    channel["last_post_at"] = t
    if not root_id:
        channel["last_root_post_at"] = t
    channel["total_msg_count"] += 1
    if uid in channel["_members"]:
        # One's own post is read by oneself.
        MEMBERS[(channel["id"], uid)] = {"msg_count": channel["total_msg_count"], "last_viewed_at": t}
    return post


def posts_list(posts):
    posts = sorted(posts, key=lambda p: (-p["create_at"], p["id"]))
    return {"order": [p["id"] for p in posts], "posts": {p["id"]: p for p in posts}, "next_post_id": "",
            "prev_post_id": ""}


def display_of(u):
    name = (u["first_name"] + " " + u["last_name"]).strip()
    return name or u["nickname"] or u["username"]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    # ---- plumbing -----------------------------------------------------------
    def reply(self, status, body, headers=None, kind=None):
        data = json.dumps(body).encode() if not isinstance(body, bytes) else body
        self.send_response(status)
        self.send_header("Content-Type", kind or ("application/json" if not isinstance(body, bytes)
                                                  else "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def error(self, status, ident, text):
        self.reply(status, {"id": ident, "message": text, "detailed_error": "", "request_id": new_id(),
                            "status_code": status})

    def body_bytes(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def user(self):
        auth = self.headers.get("Authorization", "")
        for prefix in ("Bearer ", "Token ", "bearer ", "token "):
            if auth.startswith(prefix):
                return TOKENS.get(auth[len(prefix):])
        return None

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

    def multipart(self, raw):
        """The parts of a multipart/form-data body: [(name, filename, content type, bytes)]."""
        boundary = self.headers.get("Content-Type", "").split("boundary=")[-1].strip('"').encode()
        parts = []
        for chunk in raw.split(b"--" + boundary)[1:]:
            if chunk.startswith(b"--"):
                break
            head, _, data = chunk.partition(b"\r\n\r\n")
            if data.endswith(b"\r\n"):
                data = data[:-2]
            name = re.search(rb'name="([^"]*)"', head)
            filename = re.search(rb'filename="([^"]*)"', head)
            kind = re.search(rb"Content-Type: ([^\r\n]+)", head)
            parts.append((name.group(1).decode() if name else "", filename.group(1).decode() if filename else None,
                          kind.group(1).decode() if kind else "", data))
        return parts

    # ---- the API ------------------------------------------------------------
    def dispatch(self, method, path, query, raw):
        if method == "POST" and path == "/api/v4/users/login":
            fields = json.loads(raw.decode() or "{}")
            u = by_login(fields.get("login_id", ""))
            if not u or u["_password"] != fields.get("password"):
                return self.error(401, "api.user.login.invalid_credentials_email_username",
                                  "Enter a valid email or username and/or password.")
            if u["_mfa"]:
                code = fields.get("token", "")
                if not code:
                    return self.error(401, "mfa.validate_token.authenticate.app_error",
                                      "Error trying to authenticate MFA token.")
                if code != MFA_CODE:
                    return self.error(401, "api.user.check_user_mfa.bad_code.app_error", "Invalid MFA token.")
            token = "sess-" + new_id()
            TOKENS[token] = u["id"]
            SESSIONS.add(token)
            return self.reply(200, user_json(u), headers={"Token": token})
        if method == "POST" and path == "/api/v4/users/mfa":
            fields = json.loads(raw.decode() or "{}")
            u = by_login(fields.get("login_id", ""))
            return self.reply(200, {"mfa_required": bool(u and u["_mfa"])})
        if method == "POST" and path == "/_test/revoke":
            # The test's own: the user's sessions end (personal access tokens stay).
            name = json.loads(raw.decode() or "{}").get("user", "")
            for token in [t for t, uid in TOKENS.items() if t in SESSIONS and USERS[uid]["username"] == name]:
                del TOKENS[token]
            return self.reply(200, {"status": "OK"})

        me = self.user()
        if not me:
            return self.error(401, "api.context.session_expired.app_error", "Invalid or expired session, please login again.")

        # ---- users ----
        if method == "GET" and path == "/api/v4/users/me":
            return self.reply(200, user_json(USERS[me]))
        if method == "POST" and path == "/api/v4/users/ids":
            ids = json.loads(raw.decode() or "[]")
            return self.reply(200, [user_json(USERS[i]) for i in ids if i in USERS])
        m = re.match(r"^/api/v4/users/username/([^/]+)$", path)
        if method == "GET" and m:
            u = by_name(urllib.parse.unquote(m.group(1)).lower())
            return self.reply(200, user_json(u)) if u else self.error(404, "app.user.missing_account.const",
                                                                        "Unable to find the user.")
        m = re.match(r"^/api/v4/users/email/([^/]+)$", path)
        if method == "GET" and m:
            email = urllib.parse.unquote(m.group(1)).lower()
            u = next((x for x in USERS.values() if x["email"] == email), None)
            return self.reply(200, user_json(u)) if u else self.error(404, "app.user.missing_account.const",
                                                                        "Unable to find the user.")
        if method == "GET" and path == "/api/v4/users/me/teams":
            return self.reply(200, [TEAM])
        if path == "/api/v4/users/me/preferences" or path == "/api/v4/users/%s/preferences" % me:
            if method == "GET":
                return self.reply(200, [{"user_id": u, "category": c, "name": n, "value": v}
                                        for (u, c, n), v in PREFS.items() if u == me])
            if method == "PUT":
                for p in json.loads(raw.decode() or "[]"):
                    PREFS[(me, p["category"], p["name"])] = p["value"]
                return self.reply(200, {"status": "OK"})
        m = re.match(r"^/api/v4/users/me/teams/([^/]+)/channels(/members)?$", path)
        if method == "GET" and m:
            if m.group(1) != TEAM["id"]:
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            # The team's channels the user is in, and every direct and group message of theirs.
            mine = [c for c in CHANNELS.values()
                    if me in c["_members"] and c["delete_at"] == 0 and c["team_id"] in ("", TEAM["id"])]
            if m.group(2):
                return self.reply(200, [dict(MEMBERS[(c["id"], me)], channel_id=c["id"], user_id=me,
                                             roles="channel_user", notify_props={}) for c in mine])
            return self.reply(200, [channel_json(c) for c in mine])
        m = re.match(r"^/api/v4/users/([a-z0-9]{26})$", path)
        if method == "GET" and m:
            u = USERS.get(m.group(1))
            return self.reply(200, user_json(u)) if u else self.error(404, "app.user.missing_account.const",
                                                                        "Unable to find the user.")

        # ---- channels ----
        if method == "POST" and path == "/api/v4/channels/direct":
            ids = json.loads(raw.decode() or "[]")
            if len(ids) != 2 or any(i not in USERS for i in ids) or me not in ids:
                return self.error(400, "api.channel.create_direct_channel.invalid", "Invalid users.")
            name = "__".join(sorted(ids))
            existing = next((c for c in CHANNELS.values() if c["type"] == "D" and c["name"] == name), None)
            c = existing or make_channel("D", name, "", me, set(ids))
            return self.reply(200 if existing else 201, channel_json(c))
        if method == "POST" and path == "/api/v4/channels/group":
            ids = set(json.loads(raw.decode() or "[]")) | {me}
            if any(i not in USERS for i in ids) or not 3 <= len(ids) <= 8:
                return self.error(400, "api.channel.create_group.bad_size.app_error",
                                  "Group message channels must contain at least 3 and no more than 8 users.")
            existing = next((c for c in CHANNELS.values() if c["type"] == "G" and c["_members"] == ids), None)
            if existing:
                return self.reply(200, channel_json(existing))
            names = sorted(USERS[i]["username"] for i in ids)
            c = make_channel("G", new_id(), ", ".join(names), me, ids)
            return self.reply(201, channel_json(c))
        if method == "POST" and path == "/api/v4/channels/members/me/view":
            cid = json.loads(raw.decode() or "{}").get("channel_id", "")
            c = CHANNELS.get(cid)
            if not c or me not in c["_members"]:
                return self.error(404, "app.channel.get.existing.app_error", "Unable to find the existing channel.")
            MEMBERS[(cid, me)] = {"msg_count": c["total_msg_count"], "last_viewed_at": now_ms()}
            return self.reply(200, {"status": "OK", "last_viewed_at_times": {cid: MEMBERS[(cid, me)]["last_viewed_at"]}})
        m = re.match(r"^/api/v4/channels/([a-z0-9]{26})(/members|/stats|/posts)?(?:/([a-z0-9]{26}))?$", path)
        if m:
            c = CHANNELS.get(m.group(1))
            if not c or me not in c["_members"]:
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            part = m.group(2)
            if method == "GET" and part == "/members":
                page, per_page = int(query.get("page", 0)), int(query.get("per_page", 60))
                ids = sorted(c["_members"])[page * per_page:(page + 1) * per_page]
                return self.reply(200, [dict(MEMBERS[(c["id"], i)], channel_id=c["id"], user_id=i) for i in ids])
            if method == "GET" and part == "/stats":
                return self.reply(200, {"channel_id": c["id"], "member_count": len(c["_members"])})
            if method == "DELETE" and part == "/members" and m.group(3):
                if c["type"] not in ("O", "P"):
                    return self.error(400, "api.channel.remove_user_from_channel.direct", "Not a channel.")
                if c["name"] == "town-square":
                    return self.error(400, "api.channel.remove.default.app_error",
                                      "Unable to remove user from the default channel town-square.")
                if m.group(3) != me:
                    return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
                c["_members"].discard(me)
                MEMBERS.pop((c["id"], me), None)
                add_post(c, me, "%s left the channel." % USERS[me]["username"], kind="system_leave_channel")
                return self.reply(200, {"status": "OK"})
            if method == "GET" and part == "/posts":
                mine = [p for p in POSTS.values() if p["channel_id"] == c["id"]]
                if "since" in query and int(query["since"]) > 0:
                    # Changed since: new, edited and deleted alike.
                    since = int(query["since"])
                    return self.reply(200, posts_list([p for p in mine if p["update_at"] > since]))
                alive = sorted([p for p in mine if p["delete_at"] == 0], key=lambda p: (-p["create_at"], p["id"]))
                if query.get("before"):
                    pivot = POSTS.get(query["before"])
                    if not pivot:
                        return self.reply(200, posts_list([]))
                    alive = [p for p in alive if (p["create_at"], p["id"]) < (pivot["create_at"], pivot["id"])]
                page, per_page = int(query.get("page", 0)), int(query.get("per_page", 60))
                return self.reply(200, posts_list(alive[page * per_page:(page + 1) * per_page]))

        # ---- posts ----
        if method == "POST" and path == "/api/v4/posts":
            fields = json.loads(raw.decode() or "{}")
            c = CHANNELS.get(fields.get("channel_id", ""))
            if not c or me not in c["_members"]:
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            pending = fields.get("pending_post_id", "")
            if pending and (me, pending) in PENDING:
                # The same post sent again (a retry): once.
                return self.reply(201, POSTS[PENDING[(me, pending)]])
            root = fields.get("root_id", "")
            if root and (root not in POSTS or POSTS[root]["channel_id"] != c["id"] or POSTS[root]["root_id"]):
                return self.error(400, "api.post.create_post.root_id.app_error", "Invalid RootId parameter.")
            file_ids = fields.get("file_ids") or []
            if any(f not in FILES or FILES[f]["info"]["user_id"] != me for f in file_ids):
                return self.error(400, "api.post.create_post.file_ids.app_error", "Invalid file ids.")
            if not fields.get("message") and not file_ids:
                return self.error(400, "model.post.is_valid.msg.app_error", "Invalid message.")
            post = add_post(c, me, fields.get("message", ""), root, file_ids, pending)
            if pending:
                PENDING[(me, pending)] = post["id"]
            # (Hidden direct messages stay hidden on the server: the clients show them again.)
            return self.reply(201, post)
        m = re.match(r"^/api/v4/posts/([a-z0-9]{26})(/patch)?$", path)
        if m:
            p = POSTS.get(m.group(1))
            if not p or p["delete_at"] or me not in CHANNELS[p["channel_id"]]["_members"]:
                return self.error(404, "app.post.get.app_error", "Unable to get the post.")
            if method == "GET" and not m.group(2):
                return self.reply(200, p)
            if p["user_id"] != me:
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            if method == "PUT" and m.group(2):
                fields = json.loads(raw.decode() or "{}")
                t = now_ms()
                if "message" in fields:
                    p["message"] = fields["message"]
                p["edit_at"] = t
                p["update_at"] = t
                return self.reply(200, p)
            if method == "DELETE" and not m.group(2):
                t = now_ms()
                for x in POSTS.values():
                    if x["id"] == p["id"] or x["root_id"] == p["id"]:
                        x["delete_at"] = t
                        x["update_at"] = t
                return self.reply(200, {"status": "OK"})

        # ---- files ----
        if method == "POST" and path == "/api/v4/files":
            parts = self.multipart(raw)
            cid = query.get("channel_id", "")
            for name, filename, kind, data in parts:
                if name == "channel_id" and filename is None:
                    cid = data.decode()
            c = CHANNELS.get(cid)
            if not c or me not in c["_members"]:
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            infos = []
            for name, filename, kind, data in parts:
                if name != "files" or filename is None:
                    continue
                fid = new_id()
                ext = filename.rsplit(".", 1)[-1].lower() if "." in filename else ""
                info = {"id": fid, "user_id": me, "post_id": "", "channel_id": cid, "create_at": now_ms(),
                        "update_at": 0, "delete_at": 0, "name": filename, "extension": ext, "size": len(data),
                        "mime_type": kind or "application/octet-stream", "width": 0, "height": 0,
                        "has_preview_image": False}
                if kind.startswith("image/"):
                    info["width"], info["height"] = 64, 48
                FILES[fid] = {"info": info, "bytes": data}
                infos.append(info)
            if not infos:
                return self.error(400, "api.file.upload_file.incorrect_number_of_files.app_error", "No file.")
            return self.reply(201, {"file_infos": infos, "client_ids": []})
        m = re.match(r"^/api/v4/files/([a-z0-9]{26})(/info)?$", path)
        if method == "GET" and m:
            f = FILES.get(m.group(1))
            if not f or me not in CHANNELS[f["info"]["channel_id"]]["_members"]:
                return self.error(404, "app.file_info.get.app_error", "Unable to get the file info.")
            if m.group(2):
                return self.reply(200, f["info"])
            return self.reply(200, f["bytes"], kind=f["info"]["mime_type"])

        return self.error(404, "api.context.404.app_error", "No route " + method + " " + path)


def main():
    global TEAM
    parser = argparse.ArgumentParser()
    parser.add_argument("--users", default="alice,bob,carol")
    parser.add_argument("--mfa-user", default="")
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    for name in args.users.split(","):
        uid = new_id()
        USERS[uid] = {"id": uid, "username": name, "email": name + "@example.org", "first_name": name.capitalize(),
                      "last_name": "", "nickname": "", "locale": "en", "roles": "system_user", "create_at": now_ms(),
                      "_password": name + "-pass", "_mfa": name == args.mfa_user}
        TOKENS[name + "-pat"] = uid
    TEAM = {"id": new_id(), "name": "team-a", "display_name": "Team A", "type": "O"}
    first = next(iter(USERS))
    for name, display in (("town-square", "Town Square"), ("off-topic", "Off-Topic")):
        c = make_channel("O", name, display, first, set(), TEAM["id"])
        for uid in USERS:
            join(c["id"], uid)
            add_post(c, uid, "%s joined the channel." % USERS[uid]["username"], kind="system_join_channel")
        for uid in USERS:
            MEMBERS[(c["id"], uid)] = {"msg_count": c["total_msg_count"], "last_viewed_at": now_ms()}
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    print(json.dumps({"url": "http://127.0.0.1:%d" % server.server_address[1]}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
