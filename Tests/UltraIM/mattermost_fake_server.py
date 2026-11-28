#!/usr/bin/env python3
"""A small Mattermost in memory, for MattermostProviderTests: the part of the
REST API (v4) the UltraIM Mattermost provider uses - signing in (a password,
with a multi-factor code for --mfa-user; a personal access token), users,
one team ("Team A") with its channels "town-square" and "off-topic" that
everyone is in, direct and group messages, posts (created, read by pages and
"since", edited, deleted - with their replies), files (uploaded, their info,
their bytes), unread counts, viewing a channel, leaving one, the
preferences that hide a direct or group message, and typing: the WebSocket
(/api/v4/websocket - a minimal RFC 6455 server: the upgrade, text frames
from masked client frames, pings, close; signed in by the Authorization
header or the authentication_challenge action, then "hello") and
POST /api/v4/users/me/typing, which sends a "typing" event to the sockets of
everyone in the channel - the typist's own too (Mattermost leaves them out),
so that a client is seen to leave its own out.

Standard library only. Prints {"url": "http://127.0.0.1:<port>"} once it
listens; runs until it is killed.

    mattermost_fake_server.py --users alice,bob,carol [--mfa-user dave]

Each user's password is "<name>-pass", their personal access token
"<name>-pat", their e-mail address "<name>@example.org"; the --mfa-user's
code is 123456.

The test's own: GET /_test/sockets answers {"<username>": {"open": n,
"opened": n}} (the sockets signed in now, and ever); POST /_test/drop_sockets
{"user": "<username>"} cuts that user's sockets without a close frame (a
network that dropped); GET /_test/typing_counts answers how many typing
notifications each username sent.
"""

import argparse
import base64
import hashlib
import json
import re
import secrets
import socket
import string
import struct
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler

from local_http_server import LocalHTTPServer    # no name lookup at start (see there)

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
SOCKETS = []           # the signed-in WebSockets (WsConn)
SOCKETS_OPENED = {}    # user id -> sockets signed in, ever
TYPING_SENT = {}       # user id -> typing notifications sent
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
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


# ---- the WebSocket (RFC 6455) ---------------------------------------------------------

class WsConn:
    def __init__(self, sock, rfile):
        self.sock = sock
        self.rfile = rfile
        self.send_lock = threading.Lock()
        self.alive = True
        self.user = None
        self.seq = 0

    def send_frame(self, opcode, payload):
        header = bytes([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header += bytes([n])
        elif n < 65536:
            header += bytes([126]) + struct.pack(">H", n)
        else:
            header += bytes([127]) + struct.pack(">Q", n)
        with self.send_lock:
            if not self.alive:
                return
            try:
                self.sock.sendall(header + payload)
            except OSError:
                self.alive = False

    def send_event(self, event, data, broadcast):
        # (Called with LOCK held.)
        self.seq += 1
        self.send_frame(1, json.dumps({"event": event, "data": data, "broadcast": broadcast,
                                       "seq": self.seq}).encode())

    def read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.rfile.read(n - len(data))
            if not chunk:
                raise EOFError
            data += chunk
        return data

    def read_message(self):
        """One whole message: (opcode, payload); pings are answered here."""
        parts, first_op = [], None
        while True:
            b0, b1 = self.read_exact(2)
            fin, opcode = b0 & 0x80, b0 & 0x0F
            masked, n = b1 & 0x80, b1 & 0x7F
            if not masked:
                raise ValueError("a client frame must be masked")
            if n == 126:
                n = struct.unpack(">H", self.read_exact(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self.read_exact(8))[0]
            mask = self.read_exact(4)
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(self.read_exact(n)))
            if opcode == 9:
                self.send_frame(10, payload)
                continue
            if opcode == 10:
                continue
            if opcode == 8:
                return 8, payload
            if opcode != 0:
                first_op = opcode
            parts.append(payload)
            if fin:
                return first_op, b"".join(parts)

    def signed_in(self, uid):
        # (Called with LOCK held.)
        self.user = uid
        SOCKETS.append(self)
        SOCKETS_OPENED[uid] = SOCKETS_OPENED.get(uid, 0) + 1
        self.send_event("hello", {"server_version": "9.11.0.fake"}, {"user_id": uid, "channel_id": "", "team_id": ""})

    def loop(self):
        try:
            while self.alive:
                opcode, payload = self.read_message()
                if opcode == 8:
                    self.send_frame(8, payload[:2])
                    break
                if opcode != 1:
                    continue
                try:
                    action = json.loads(payload.decode())
                except ValueError:
                    continue
                with LOCK:
                    if action.get("action") == "authentication_challenge":
                        uid = TOKENS.get((action.get("data") or {}).get("token", ""))
                        if self.user is None and uid:
                            self.signed_in(uid)
                        self.seq += 1
                        self.send_frame(1, json.dumps({"status": "OK" if uid else "FAIL",
                                                       "seq_reply": action.get("seq", 0)}).encode())
        except (EOFError, OSError, ValueError):
            pass
        finally:
            with LOCK:
                self.alive = False
                if self in SOCKETS:
                    SOCKETS.remove(self)

    def drop(self):
        """Gone without a goodbye: the socket shut."""
        with self.send_lock:
            self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


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
        if urllib.parse.urlparse(self.path).path == "/api/v4/websocket" and \
                self.headers.get("Upgrade", "").lower() == "websocket":
            return self.websocket()
        self.route("GET")

    def websocket(self):
        key = self.headers.get("Sec-WebSocket-Key", "")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()
        conn = WsConn(self.connection, self.rfile)
        with LOCK:
            # Signed in by the header at once; otherwise by the challenge.
            uid = self.user()
            if uid:
                conn.signed_in(uid)
        conn.loop()
        self.close_connection = True

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
        if method == "GET" and path == "/_test/sockets":
            out = {}
            for uid, u in USERS.items():
                out[u["username"]] = {"open": sum(1 for c in SOCKETS if c.user == uid and c.alive),
                                      "opened": SOCKETS_OPENED.get(uid, 0)}
            return self.reply(200, out)
        if method == "POST" and path == "/_test/drop_sockets":
            name = json.loads(raw.decode() or "{}").get("user", "")
            for c in [c for c in SOCKETS if USERS[c.user]["username"] == name]:
                c.drop()
                SOCKETS.remove(c)
            return self.reply(200, {"status": "OK"})
        if method == "GET" and path == "/_test/typing_counts":
            return self.reply(200, {USERS[uid]["username"]: n for uid, n in TYPING_SENT.items()})
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
        m = re.match(r"^/api/v4/users/(me|[a-z0-9]{26})/typing$", path)
        if method == "POST" and m:
            if m.group(1) not in ("me", me):
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            fields = json.loads(raw.decode() or "{}")
            c = CHANNELS.get(fields.get("channel_id", ""))
            if not c or me not in c["_members"]:
                return self.error(403, "api.context.permissions.app_error", "You do not have the appropriate permissions.")
            TYPING_SENT[me] = TYPING_SENT.get(me, 0) + 1
            for conn in list(SOCKETS):
                if conn.user in c["_members"] and conn.alive:
                    conn.send_event("typing", {"parent_id": fields.get("parent_id", ""), "user_id": me},
                                    {"omit_users": None, "user_id": "", "channel_id": c["id"], "team_id": ""})
            return self.reply(200, {"status": "OK"})
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
    server = LocalHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    print(json.dumps({"url": "http://127.0.0.1:%d" % server.server_address[1]}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
