#!/usr/bin/env python3
"""A small Zulip in memory, for ZulipProviderTests: the part of the REST API
v1 the UltraIM Zulip provider uses - the API key fetched with a password, the
account and the people, one stream everyone is subscribed to ("general", id
1) and its topics, direct and group direct messages, messages sent, read
(with a narrow and an anchor), edited, deleted and flagged read, topics
muted, uploads, and the event queue with a real long-poll.

Standard library only. Prints {"url": "http://127.0.0.1:<port>"} once it
listens; runs until it is killed.

    zulip_fake_server.py --users alice,bob,carol [--poll-seconds 10]

Each user's e-mail is "<name>@example.org", the password "<name>-pass", the
API key "<name>-key"; requests sign with HTTP Basic "<e-mail>:<API key>".
A long-poll (GET /api/v1/events) waits up to --poll-seconds for an event and
then answers a heartbeat. POST /_test/forget_queues drops every event queue,
as a server restart does: the next poll of each answers BAD_EVENT_QUEUE_ID.
"""

import argparse
import base64
import html
import itertools
import json
import re
import secrets
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOCK = threading.Lock()
WAKE = threading.Condition(LOCK)     # a long-poll waits on it; a new event wakes it
MESSAGE_IDS = itertools.count(1000)
USERS = {}             # user id -> {"id", "name", "email", "full_name", "key", "password", "is_bot"}
STREAMS = {1: {"stream_id": 1, "name": "general", "description": "Everyone", "subscribers": set()}}
MESSAGES = {}          # id -> message (with "_recipients": the user ids of a direct message)
FLAGS = {}             # (user id, message id) -> set of flags
MUTED = set()          # (user id, stream id, topic)
UPLOADS = {}           # "/user_uploads/..." -> bytes
QUEUES = {}            # queue id -> {"user", "events": [...], "next": int, "types": set}
POLL_SECONDS = 10.0


def user_by_email(email):
    for u in USERS.values():
        if u["email"].lower() == email.lower():
            return u
    return None


def user_json(u):
    return {"user_id": u["id"], "email": u["email"], "full_name": u["full_name"], "is_bot": u["is_bot"],
            "is_active": True, "avatar_url": None, "role": 400}


def can_see(m, uid):
    if m["type"] == "private":
        return uid in m["_recipients"]
    return uid in STREAMS[m["stream_id"]]["subscribers"]


def readers_of(m):
    if m["type"] == "private":
        return set(m["_recipients"])
    return set(STREAMS[m["stream_id"]]["subscribers"])


def message_json(m, uid, apply_markdown=False):
    out = {"id": m["id"], "sender_id": m["sender_id"], "sender_email": USERS[m["sender_id"]]["email"],
           "sender_full_name": USERS[m["sender_id"]]["full_name"], "timestamp": m["timestamp"],
           "type": m["type"], "subject": m["subject"], "client": "test",
           "content": "<p>" + html.escape(m["content"]) + "</p>" if apply_markdown else m["content"],
           "flags": sorted(FLAGS.get((uid, m["id"]), set()))}
    if m["type"] == "private":
        out["display_recipient"] = [{"id": r, "email": USERS[r]["email"], "full_name": USERS[r]["full_name"]}
                                    for r in sorted(m["_recipients"])]
    else:
        out["display_recipient"] = STREAMS[m["stream_id"]]["name"]
        out["stream_id"] = m["stream_id"]
    if m.get("last_edit_timestamp"):
        out["last_edit_timestamp"] = m["last_edit_timestamp"]
    return out


def push(uids, event, only_queue=None, extra_for_queue=None):
    """Adds the event to every queue of these users that asked for its type."""
    for qid, q in QUEUES.items():
        if q["user"] not in uids or event["type"] not in q["types"]:
            continue
        e = dict(event)
        if callable(e.get("message")):
            e["message"] = e["message"](q["user"])
        if "_flags" in e:
            e["flags"] = sorted(e.pop("_flags")(q["user"]))
        if extra_for_queue and qid == only_queue:
            e.update(extra_for_queue)
        e["id"] = q["next"]
        q["next"] += 1
        q["events"].append(e)
    WAKE.notify_all()


def operand_ids(operand):
    """A "dm" operand: a list of user ids, or e-mails joined by commas."""
    if isinstance(operand, list):
        return {int(x) for x in operand}
    ids = set()
    for email in str(operand).split(","):
        u = user_by_email(email.strip())
        ids.add(u["id"] if u else -1)
    return ids


def stream_of(operand):
    if isinstance(operand, int) or str(operand).isdigit():
        return int(operand)
    for sid, s in STREAMS.items():
        if s["name"] == operand:
            return sid
    return -1


def matches(m, uid, narrow):
    for term in narrow:
        op, operand = term.get("operator"), term.get("operand")
        negated = term.get("negated", False)
        if op == "is" and operand in ("dm", "private"):
            ok = m["type"] == "private"
        elif op in ("dm", "pm-with"):
            ok = m["type"] == "private" and m["_recipients"] == (operand_ids(operand) | {uid})
        elif op == "dm-including":
            ok = m["type"] == "private" and int(operand) in m["_recipients"]
        elif op in ("stream", "channel"):
            ok = m["type"] == "stream" and m["stream_id"] == stream_of(operand)
        elif op == "topic":
            ok = m["type"] == "stream" and m["subject"].lower() == str(operand).lower()
        elif op == "search":
            ok = str(operand).lower() in m["content"].lower()
        else:
            raise ValueError("Invalid narrow operator: " + str(op))
        if ok == negated:
            return False
    return True


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    # ---- plumbing -----------------------------------------------------------
    def reply(self, status, body, kind="application/json", headers=None):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def success(self, body=None):
        out = {"result": "success", "msg": ""}
        out.update(body or {})
        self.reply(200, out)

    def error(self, status, text, code="BAD_REQUEST", extra=None):
        out = {"result": "error", "msg": text, "code": code}
        out.update(extra or {})
        self.reply(status, out)

    def fields(self, raw, query):
        out = dict(query)
        kind = self.headers.get("Content-Type", "")
        if kind.startswith("application/x-www-form-urlencoded"):
            out.update({k: v[0] for k, v in urllib.parse.parse_qs(raw.decode(), keep_blank_values=True).items()})
        return out

    def user(self):
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Basic "):
            return None
        try:
            email, _, key = base64.b64decode(auth[6:]).decode().partition(":")
        except Exception:
            return None
        u = user_by_email(email)
        return u if u and u["key"] == key else None

    def route(self, method):
        parsed = urllib.parse.urlparse(self.path)
        query = {k: v[0] for k, v in urllib.parse.parse_qs(parsed.query, keep_blank_values=True).items()}
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        with LOCK:
            try:
                self.dispatch(method, parsed.path, query, raw)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except ValueError as e:
                self.error(400, str(e))

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PATCH(self):
        self.route("PATCH")

    def do_DELETE(self):
        self.route("DELETE")

    # ---- the API ------------------------------------------------------------
    def dispatch(self, method, path, query, raw):
        f = self.fields(raw, query)
        if method == "POST" and path == "/api/v1/fetch_api_key":
            u = user_by_email(f.get("username", ""))
            if not u or u["password"] != f.get("password"):
                return self.error(401, "Your username or password is incorrect", "AUTHENTICATION_FAILED")
            return self.success({"api_key": u["key"], "email": u["email"], "user_id": u["id"]})
        if method == "POST" and path == "/_test/forget_queues":
            QUEUES.clear()
            WAKE.notify_all()
            return self.success()
        if method == "GET" and path == "/_test/messages":
            return self.success({"messages": [dict((k, v if not isinstance(v, set) else sorted(v))
                                                   for k, v in m.items()) for m in MESSAGES.values()]})
        if method == "GET" and path == "/_test/queues":
            return self.success({"queues": len(QUEUES)})

        user = self.user()
        if not user:
            return self.error(401, "Invalid API key", "INVALID_API_KEY")
        uid = user["id"]

        # The event queue.
        if method == "POST" and path == "/api/v1/register":
            qid = "q-" + secrets.token_hex(6)
            types = set(json.loads(f.get("event_types", "[]")) or ["message", "update_message", "delete_message"])
            QUEUES[qid] = {"user": uid, "events": [], "next": 0, "types": types}
            return self.success({"queue_id": qid, "last_event_id": -1, "zulip_feature_level": 300,
                                 "zulip_version": "fake"})
        if method == "GET" and path == "/api/v1/events":
            qid = f.get("queue_id", "")
            last = int(f.get("last_event_id", "-1"))
            q = QUEUES.get(qid)
            if not q or q["user"] != uid:
                return self.error(400, "Bad event queue ID: " + qid, "BAD_EVENT_QUEUE_ID", {"queue_id": qid})
            q["events"] = [e for e in q["events"] if e["id"] > last]
            deadline = time.time() + POLL_SECONDS
            while not q["events"] and QUEUES.get(qid) is q and time.time() < deadline:
                WAKE.wait(deadline - time.time())
            if QUEUES.get(qid) is not q:
                return self.error(400, "Bad event queue ID: " + qid, "BAD_EVENT_QUEUE_ID", {"queue_id": qid})
            if not q["events"]:
                q["events"].append({"type": "heartbeat", "id": q["next"]})
                q["next"] += 1
            return self.success({"events": list(q["events"]), "queue_id": qid})
        if method == "DELETE" and path == "/api/v1/events":
            QUEUES.pop(f.get("queue_id", ""), None)
            return self.success()

        # People.
        if method == "GET" and path == "/api/v1/users/me":
            return self.success(user_json(user))
        if method == "GET" and path == "/api/v1/users":
            return self.success({"members": [user_json(u) for u in USERS.values()]})
        if method == "GET" and path == "/api/v1/users/me/subscriptions":
            subs = [{"stream_id": s["stream_id"], "name": s["name"], "description": s["description"]}
                    for s in STREAMS.values() if uid in s["subscribers"]]
            return self.success({"subscriptions": subs})
        m = re.match(r"^/api/v1/users/me/(\d+)/topics$", path)
        if method == "GET" and m:
            sid = int(m.group(1))
            latest = {}
            for msg in MESSAGES.values():
                if msg["type"] == "stream" and msg["stream_id"] == sid:
                    latest[msg["subject"]] = max(latest.get(msg["subject"], 0), msg["id"])
            topics = [{"name": t, "max_id": i} for t, i in sorted(latest.items(), key=lambda x: -x[1])]
            return self.success({"topics": topics})
        m = re.match(r"^/api/v1/users/([^/]+)$", path)
        if method == "GET" and m:
            key = urllib.parse.unquote(m.group(1))
            u = USERS.get(int(key)) if key.isdigit() else user_by_email(key)
            return self.success({"user": user_json(u)}) if u else self.error(400, "No such user", "BAD_REQUEST")
        m = re.match(r"^/api/v1/streams/(\d+)/members$", path)
        if method == "GET" and m:
            s = STREAMS.get(int(m.group(1)))
            if not s:
                return self.error(400, "Invalid channel ID", "BAD_REQUEST")
            return self.success({"subscribers": sorted(s["subscribers"])})
        if method == "POST" and path == "/api/v1/user_topics":
            entry = (uid, int(f.get("stream_id", "0")), f.get("topic", ""))
            if f.get("visibility_policy") == "1":
                MUTED.add(entry)
            else:
                MUTED.discard(entry)
            return self.success()

        # Messages.
        if method == "GET" and path == "/api/v1/messages":
            narrow = json.loads(f.get("narrow", "[]"))
            apply_markdown = f.get("apply_markdown", "true") == "true"
            anchor_text = f.get("anchor", "newest")
            seen = sorted(i for i, msg in MESSAGES.items() if can_see(msg, uid) and matches(msg, uid, narrow))
            anchor = 10 ** 18 if anchor_text in ("newest", "first_unread") else 0 if anchor_text == "oldest" \
                else int(anchor_text)
            include = f.get("include_anchor", "true") == "true"
            before = [i for i in seen if i < anchor][-int(f.get("num_before", "0")):] if int(f.get("num_before", "0")) else []
            after = [i for i in seen if i > anchor][:int(f.get("num_after", "0"))]
            middle = [anchor] if include and anchor in seen else []
            ids = before + middle + after
            return self.success({"messages": [message_json(MESSAGES[i], uid, apply_markdown) for i in ids],
                                 "anchor": anchor, "found_anchor": anchor in seen,
                                 "found_oldest": bool(not seen or (ids and ids[0] == seen[0])),
                                 "found_newest": bool(not seen or (ids and ids[-1] == seen[-1]))})
        if method == "POST" and path == "/api/v1/messages":
            kind = f.get("type", "")
            content = f.get("content", "")
            if not content.strip():
                return self.error(400, "Message must not be empty")
            mid = next(MESSAGE_IDS)
            msg = {"id": mid, "sender_id": uid, "content": content, "timestamp": int(time.time()), "subject": ""}
            if kind in ("direct", "private"):
                to = json.loads(f.get("to", "[]"))
                ids = operand_ids(to)
                if -1 in ids or any(i not in USERS for i in ids):
                    return self.error(400, "Invalid user ID")
                msg.update({"type": "private", "_recipients": ids | {uid}})
            elif kind in ("stream", "channel"):
                sid = stream_of(json.loads(f.get("to", "0")) if f.get("to", "").isdigit() else f.get("to", ""))
                if sid not in STREAMS:
                    return self.error(400, "Invalid channel ID")
                msg.update({"type": "stream", "stream_id": sid, "subject": f.get("topic", "") or "(no topic)"})
            else:
                return self.error(400, "Invalid message type")
            MESSAGES[mid] = msg
            for r in readers_of(msg):
                FLAGS[(r, mid)] = {"read"} if r == uid else set()
            extra = {"local_message_id": f["local_id"]} if f.get("local_id") else None
            push(readers_of(msg), {"type": "message", "message": lambda viewer: message_json(msg, viewer),
                                   "_flags": lambda viewer: FLAGS[(viewer, mid)]},
                 only_queue=f.get("queue_id"), extra_for_queue=extra)
            return self.success({"id": mid})
        if method == "POST" and path == "/api/v1/messages/flags":
            ids = [int(i) for i in json.loads(f.get("messages", "[]"))]
            flag, op = f.get("flag", ""), f.get("op", "")
            changed = []
            for i in ids:
                if i in MESSAGES and can_see(MESSAGES[i], uid):
                    flags = FLAGS.setdefault((uid, i), set())
                    (flags.add if op == "add" else flags.discard)(flag)
                    changed.append(i)
            return self.success({"messages": changed})
        m = re.match(r"^/api/v1/messages/(\d+)$", path)
        if m:
            msg = MESSAGES.get(int(m.group(1)))
            if not msg or not can_see(msg, uid):
                return self.error(400, "Invalid message(s)", "BAD_REQUEST")
            if method == "GET":
                return self.success({"message": message_json(msg, uid, f.get("apply_markdown", "true") == "true"),
                                     "raw_content": msg["content"]})
            if msg["sender_id"] != uid:
                return self.error(400, "You don't have permission to edit this message")
            if method == "PATCH":
                if "content" in f:
                    msg["content"] = f["content"]
                    msg["last_edit_timestamp"] = int(time.time())
                event = {"type": "update_message", "message_id": msg["id"], "message_ids": [msg["id"]],
                         "user_id": uid, "edit_timestamp": msg.get("last_edit_timestamp", 0),
                         "content": msg["content"], "rendered_content": "<p>" + html.escape(msg["content"]) + "</p>",
                         "_flags": lambda viewer: FLAGS.get((viewer, msg["id"]), set())}
                if msg["type"] == "stream":
                    event.update({"stream_id": msg["stream_id"], "orig_subject": msg["subject"]})
                push(readers_of(msg), event)
                return self.success()
            if method == "DELETE":
                del MESSAGES[msg["id"]]
                event = {"type": "delete_message", "message_id": msg["id"], "message_type": msg["type"]}
                if msg["type"] == "stream":
                    event.update({"stream_id": msg["stream_id"], "topic": msg["subject"]})
                push(readers_of(msg), event)
                return self.success()

        # Uploads.
        if method == "POST" and path == "/api/v1/user_uploads":
            boundary = self.headers.get("Content-Type", "").split("boundary=")[-1].encode()
            part = raw.split(b"--" + boundary)[1]
            head, _, data = part.partition(b"\r\n\r\n")
            data = data[:-2] if data.endswith(b"\r\n") else data
            name = re.search(rb'filename="([^"]*)"', head).group(1).decode()
            uri = "/user_uploads/2/%s/%s" % (secrets.token_hex(4), urllib.parse.quote(name))
            UPLOADS[uri] = data
            return self.success({"uri": uri, "url": uri, "filename": name})
        if method == "GET" and path.startswith("/user_uploads/"):
            data = UPLOADS.get(path)
            return self.reply(200, data, "application/octet-stream") if data is not None \
                else self.error(404, "File not found", "NOT_FOUND")

        return self.error(404, "No route " + method + " " + path, "NOT_FOUND")


def main():
    global POLL_SECONDS
    parser = argparse.ArgumentParser()
    parser.add_argument("--users", default="alice,bob,carol")
    parser.add_argument("--poll-seconds", type=float, default=10.0)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    POLL_SECONDS = args.poll_seconds
    for i, name in enumerate(args.users.split(",")):
        uid = 10 + i
        USERS[uid] = {"id": uid, "name": name, "email": name + "@example.org", "full_name": name.capitalize(),
                      "key": name + "-key", "password": name + "-pass", "is_bot": False}
        STREAMS[1]["subscribers"].add(uid)
    bot = 9
    USERS[bot] = {"id": bot, "name": "welcome-bot", "email": "welcome-bot@example.org", "full_name": "Welcome Bot",
                  "key": "bot-key", "password": None, "is_bot": True}
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    print(json.dumps({"url": "http://127.0.0.1:%d" % server.server_address[1]}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
