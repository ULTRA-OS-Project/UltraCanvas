#!/usr/bin/env python3
"""A stand-in for signal-cli's HTTP JSON-RPC daemon, for SignalProviderTests:
the part of signal-cli (multi-account mode, --receive-mode=manual) the UltraIM
Signal provider uses, with signal-cli's method, parameter and result names and
its envelope JSON - linking a device (startLink, finishLink), listAccounts,
receive, send (texts, attachments from local paths, quotes, edits),
remoteDelete, sendReceipt, sendTyping, listContacts, listGroups, updateGroup,
quitGroup, getUserStatus, getAttachment.

A small Signal in memory: the --accounts are the phone numbers registered on
it, each with a phone of its own; an account is "linked" here (listed by
listAccounts, its messages kept for receive) once a startLink URI was scanned
for it. Messages between the accounts reach the recipients' receive queues;
a recipient's device answers each with a delivery receipt when it receives it.

Standard library only. Two ways to run:

    signal_fake_daemon.py --accounts +4915100000001,+4915100000002

listens on 127.0.0.1, a free port, and prints {"url": "http://127.0.0.1:<port>"};

    signal_fake_daemon.py --accounts ... --config DIR daemon --http 127.0.0.1:PORT \
        --receive-mode=manual

is what a wrapper standing in for signal-cli runs (the arguments signal-cli
gets): it listens there, keeps its linked accounts in DIR (a new start knows
them), stores attachments in DIR/attachments/<id> as signal-cli does, and
writes DIR/fake-daemon.json with {"url", "pid"}.

Test-only endpoints (POST, JSON):

    /_test/scan        {uri, number}       the phone scans the QR code: the
                                           finishLink waiting for uri returns
                                           number (linked)
    /_test/expire      {uri}               the link times out, as signal-cli's
                                           does after a while ("" = every one)
    /_test/phone_send  {from, to | groupId, message, attachment?}
                                           a message the user typed on the
                                           phone: delivered to the recipients,
                                           and a sync copy (syncMessage.
                                           sentMessage) to the sender's linked
                                           device here
    /_test/state       {}                  the accounts, groups and links

Runs until it is killed.
"""

import argparse
import base64
import json
import mimetypes
import os
import secrets
import sys
import threading
import time
import uuid as uuidlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOCK = threading.Lock()
CHANGED = threading.Condition(LOCK)
ACCOUNTS = {}          # number -> {"uuid", "name", "username", "linked", "queue", "contacts"}
GROUPS = {}            # group id (base64) -> {"name", "members": [numbers], "admins": set, "revision"}
LINKS = {}             # device link uri -> {"number": None | str, "expired": bool}
ATTACHMENTS = {}       # attachment id -> path on disk
CONFIG = {"dir": None, "attachments": None}
_LAST_MS = [0]


class RpcError(Exception):
    def __init__(self, message, code=-1):
        super().__init__(message)
        self.code = code


def now_ms():
    # Strictly growing: a message is known by its author and its timestamp.
    t = int(time.time() * 1000)
    _LAST_MS[0] = max(t, _LAST_MS[0] + 1)
    return _LAST_MS[0]


def as_list(params, name):
    """signal-cli takes "recipient" or "recipients", one value or a list."""
    for key in (name, name + "s"):
        value = params.get(key)
        if value is None:
            continue
        return value if isinstance(value, list) else [value]
    return []


def save_state():
    if not CONFIG["dir"]:
        return
    linked = [n for n, a in ACCOUNTS.items() if a["linked"]]
    with open(os.path.join(CONFIG["dir"], "fake-accounts.json"), "w") as f:
        json.dump({"linked": linked}, f)


def account_of(params):
    number = params.get("account")
    if not number:
        raise RpcError("Method requires valid account parameter", -32602)
    a = ACCOUNTS.get(number)
    if not a or not a["linked"]:
        raise RpcError("Specified account does not exist", -32602)
    return number, a


def resolve(identifier):
    """A number, an ACI or a "u:username" -> the account's number, or None."""
    if identifier in ACCOUNTS:
        return identifier
    for number, a in ACCOUNTS.items():
        if a["uuid"] == identifier or a["username"] == identifier:
            return number
    return None


def contact_json(owner, number):
    a = ACCOUNTS[number]
    given, _, family = a["name"].partition(" ")
    return {
        "number": number, "uuid": a["uuid"], "username": a["username"],
        "name": "", "givenName": None, "familyName": None,
        "nickName": None, "nickGivenName": None, "nickFamilyName": None, "note": None,
        "color": None, "isArchived": False, "isBlocked": False, "isHidden": False,
        "messageExpirationTime": 0, "profileSharing": True, "unregistered": False,
        "profile": {"lastUpdateTimestamp": 0, "givenName": given, "familyName": family or None,
                    "about": None, "aboutEmoji": None, "hasAvatar": False,
                    "mobileCoinAddress": None, "badges": []},
    }


def group_json(gid):
    g = GROUPS[gid]
    members = [{"number": n, "uuid": ACCOUNTS[n]["uuid"], "isAdmin": n in g["admins"]} for n in g["members"]]
    return {
        "id": gid, "name": g["name"], "description": "", "isMember": True, "isBlocked": False,
        "messageExpirationTime": 0, "members": members, "pendingMembers": [], "requestingMembers": [],
        "admins": [{"number": n, "uuid": ACCOUNTS[n]["uuid"]} for n in g["admins"]], "banned": [],
        "permissionAddMember": "EVERY_MEMBER", "permissionEditDetails": "EVERY_MEMBER",
        "permissionSendMessage": "EVERY_MEMBER", "groupInviteLink": None, "isTerminated": False,
    }


def group_info(gid, kind="DELIVER"):
    g = GROUPS[gid]
    return {"groupId": gid, "groupName": g["name"], "revision": g["revision"], "type": kind}


def envelope(source, recipient, ts, **content):
    a = ACCOUNTS[source]
    env = {
        "source": source, "sourceNumber": source, "sourceUuid": a["uuid"], "sourceName": a["name"],
        "sourceDevice": 1, "timestamp": ts, "serverReceivedTimestamp": ts, "serverDeliveredTimestamp": ts,
        "isUnidentifiedSender": False,
    }
    env.update(content)
    return {"envelope": env, "account": recipient}


def deliver(recipient, item, receipt_to=None):
    """Into recipient's queue (when linked here); receipt_to: the sender, who
    gets a delivery receipt once the recipient's device has the message."""
    a = ACCOUNTS[recipient]
    if a["linked"]:
        a["queue"].append((item, receipt_to))
        CHANGED.notify_all()
    elif receipt_to:
        # The recipient's phone has it at once.
        send_receipt(recipient, receipt_to, [item["envelope"]["timestamp"]], delivery=True)


def send_receipt(source, to, timestamps, delivery):
    ts = now_ms()
    receipt = {"when": ts, "isDelivery": delivery, "isRead": not delivery, "isViewed": False,
               "timestamps": timestamps}
    deliver(to, envelope(source, to, ts, receiptMessage=receipt))


def know(owner, other):
    if owner != other:
        ACCOUNTS[owner]["contacts"].add(other)


def targets(sender, params, recipients=None):
    """The members a message goes to, and the group (or None)."""
    gid = params.get("groupId")
    if isinstance(gid, list):
        gid = gid[0] if gid else None
    if gid:
        g = GROUPS.get(gid)
        if not g or sender not in g["members"]:
            raise RpcError("Group not found: " + gid)
        return [m for m in g["members"] if m != sender], gid
    out = []
    for r in (recipients if recipients is not None else as_list(params, "recipient")):
        number = resolve(r)
        if number is None:
            raise RpcError("The user " + r + " is not registered.")
        out.append(number)
    for u in as_list(params, "username"):
        number = resolve(u)
        if number is None:
            raise RpcError("The user " + u + " is not registered.")
        out.append(number)
    if not out:
        raise RpcError("No recipients given")
    return out, None


def store_attachment(path, voice_note=False):
    if not os.path.isfile(path):
        raise RpcError("Failed to send message: " + path + " (AttachmentInvalidException)")
    with open(path, "rb") as f:
        data = f.read()
    name = os.path.basename(path)
    ext = os.path.splitext(name)[1].lower()
    aid = secrets.token_urlsafe(15).replace("-", "x").replace("_", "y") + ext
    stored = os.path.join(CONFIG["attachments"], aid)
    with open(stored, "wb") as f:
        f.write(data)
    ATTACHMENTS[aid] = stored
    mime = mimetypes.guess_type(name)[0] or "application/octet-stream"
    return {"contentType": mime, "filename": name, "id": aid, "size": len(data),
            "width": None, "height": None, "caption": None, "uploadTimestamp": now_ms(),
            "isVoiceNote": voice_note}


def data_message(sender, params, ts, gid):
    msg = {"timestamp": ts, "message": params.get("message"), "expiresInSeconds": 0, "viewOnce": False}
    files = as_list(params, "attachment")
    if files:
        msg["attachments"] = [store_attachment(p, bool(params.get("voiceNote"))) for p in files]
    if params.get("quoteTimestamp"):
        author = resolve(params.get("quoteAuthor") or "") or params.get("quoteAuthor")
        msg["quote"] = {"id": int(params["quoteTimestamp"]), "author": author, "authorNumber": author,
                        "authorUuid": ACCOUNTS[author]["uuid"] if author in ACCOUNTS else None,
                        "text": params.get("quoteMessage"), "attachments": []}
    if gid:
        msg["groupInfo"] = group_info(gid)
    return msg


def send_from(sender, params, sync_copy=False):
    """A data message from sender (its linked device, or its phone when
    sync_copy: then the device here gets the sync copy)."""
    members, gid = targets(sender, params)
    ts = now_ms()
    msg = data_message(sender, params, ts, gid)
    edit = params.get("editTimestamp")
    for m in members:
        know(m, sender)
        know(sender, m)
        if edit:
            item = envelope(sender, m, ts, editMessage={"targetSentTimestamp": int(edit), "dataMessage": msg})
        else:
            item = envelope(sender, m, ts, dataMessage=msg)
        deliver(m, item, receipt_to=sender)
    if sync_copy and ACCOUNTS[sender]["linked"]:
        sent = dict(msg)
        if gid is None:
            dest = members[0]
            sent.update({"destination": dest, "destinationNumber": dest, "destinationUuid": ACCOUNTS[dest]["uuid"]})
        else:
            sent.update({"destination": None, "destinationNumber": None, "destinationUuid": None})
        deliver(sender, envelope(sender, sender, ts, syncMessage={"sentMessage": sent}))
    results = [{"recipientAddress": {"uuid": ACCOUNTS[m]["uuid"], "number": m}, "type": "SUCCESS"} for m in members]
    for r in results:
        if gid:
            r["groupId"] = gid
    return {"timestamp": ts, "results": results}


# ---------------------------------------------------------------------------
# The JSON-RPC methods
# ---------------------------------------------------------------------------

def m_listAccounts(params):
    return [{"number": n, "aci": a["uuid"]} for n, a in ACCOUNTS.items() if a["linked"]]


def m_startLink(params):
    uri = "sgnl://linkdevice?uuid=%s&pub_key=%s" % (secrets.token_urlsafe(16), secrets.token_urlsafe(24))
    LINKS[uri] = {"number": None, "expired": False}
    return {"deviceLinkUri": uri}


def m_finishLink(params):
    uri = params.get("deviceLinkUri")
    if not uri:
        raise RpcError("Missing deviceLinkUri.")
    link = LINKS.get(uri)
    if link is None:
        raise RpcError("Unknown device link uri.")
    # Blocks until the phone scans the code (or the link times out).
    deadline = time.time() + 600
    while link["number"] is None and not link["expired"]:
        left = deadline - time.time()
        if left <= 0:
            link["expired"] = True
            break
        CHANGED.wait(min(left, 1.0))
    del LINKS[uri]
    if link["number"] is None:
        raise RpcError("Link request timed out, please try again.")
    number = link["number"]
    a = ACCOUNTS[number]
    a["linked"] = True
    a["queue"] = []
    save_state()
    return {"number": number, "aci": a["uuid"]}


def m_receive(params):
    number, a = account_of(params)
    timeout = float(params.get("timeout") if params.get("timeout") is not None else 3.0)
    limit = int(params.get("maxMessages") if params.get("maxMessages") is not None else -1)
    deadline = time.time() + max(0.0, timeout)
    while not a["queue"]:
        left = deadline - time.time()
        if left <= 0 or not a["linked"]:
            return []
        CHANGED.wait(left)
    take = a["queue"] if limit < 0 else a["queue"][:limit]
    a["queue"] = a["queue"][len(take):]
    out = []
    for item, receipt_to in take:
        out.append(item)
        if receipt_to:
            # This device has it: the sender learns so.
            send_receipt(number, receipt_to, [item["envelope"]["timestamp"]], delivery=True)
    return out


def m_send(params):
    number, _ = account_of(params)
    return send_from(number, params)


def m_remoteDelete(params):
    number, _ = account_of(params)
    members, gid = targets(number, params)
    ts = now_ms()
    target = int(params.get("targetTimestamp") or 0)
    msg = {"timestamp": ts, "message": None, "expiresInSeconds": 0, "viewOnce": False,
           "remoteDelete": {"targetTimestamp": target, "timestamp": target}}
    if gid:
        msg["groupInfo"] = group_info(gid)
    for m in members:
        deliver(m, envelope(number, m, ts, dataMessage=msg))
    return {"timestamp": ts, "results": [{"recipientAddress": {"uuid": ACCOUNTS[m]["uuid"], "number": m},
                                          "type": "SUCCESS"} for m in members]}


def m_sendReceipt(params):
    number, _ = account_of(params)
    to = resolve(as_list(params, "recipient")[0] if as_list(params, "recipient") else "")
    if to is None:
        raise RpcError("Invalid recipient")
    stamps = [int(t) for t in as_list(params, "targetTimestamp")]
    kind = params.get("type") or "read"
    if kind not in ("read", "viewed"):
        raise RpcError("Unknown receipt type: " + kind)
    send_receipt(number, to, stamps, delivery=False)
    return {"timestamp": now_ms(), "results": [{"recipientAddress": {"uuid": ACCOUNTS[to]["uuid"], "number": to},
                                                "type": "SUCCESS"}]}


def m_sendTyping(params):
    number, _ = account_of(params)
    members, gid = targets(number, params)
    ts = now_ms()
    typing = {"action": "STOPPED" if params.get("stop") else "STARTED", "timestamp": ts}
    if gid:
        typing["groupId"] = gid
    for m in members:
        deliver(m, envelope(number, m, ts, typingMessage=typing))
    return {}


def m_listContacts(params):
    number, a = account_of(params)
    return [contact_json(number, n) for n in sorted(a["contacts"])]


def m_listGroups(params):
    number, _ = account_of(params)
    wanted = set(as_list(params, "groupId"))
    return [group_json(gid) for gid, g in GROUPS.items()
            if number in g["members"] and (not wanted or gid in wanted)]


def m_updateGroup(params):
    number, _ = account_of(params)
    gid = params.get("groupId")
    added = []
    for m in as_list(params, "member"):
        n = resolve(m)
        if n is None:
            raise RpcError("The user " + m + " is not registered.")
        added.append(n)
    created = not gid
    if created:
        gid = base64.b64encode(secrets.token_bytes(32)).decode()
        GROUPS[gid] = {"name": params.get("name") or "", "members": [number], "admins": {number}, "revision": 0}
    g = GROUPS.get(gid)
    if not g or number not in g["members"]:
        raise RpcError("Group not found: " + str(gid))
    if params.get("name") is not None:
        g["name"] = params["name"]
    for n in added:
        if n not in g["members"]:
            g["members"].append(n)
    g["revision"] += 1
    ts = now_ms()
    for m in g["members"]:
        if m == number:
            continue
        know(m, number)
        deliver(m, envelope(number, m, ts, dataMessage={"timestamp": ts, "message": None, "expiresInSeconds": 0,
                                                       "viewOnce": False, "groupInfo": group_info(gid, "UPDATE")}))
    out = {"timestamp": ts, "results": []}
    if created:
        out["groupId"] = gid
    return out


def m_quitGroup(params):
    number, _ = account_of(params)
    gid = params.get("groupId")
    if isinstance(gid, list):
        gid = gid[0] if gid else None
    g = GROUPS.get(gid)
    if not g or number not in g["members"]:
        raise RpcError("Group not found: " + str(gid))
    g["members"].remove(number)
    g["admins"].discard(number)
    g["revision"] += 1
    ts = now_ms()
    for m in g["members"]:
        deliver(m, envelope(number, m, ts, dataMessage={"timestamp": ts, "message": None, "expiresInSeconds": 0,
                                                       "viewOnce": False, "groupInfo": group_info(gid, "QUIT")}))
    return {"timestamp": ts, "results": []}


def m_getUserStatus(params):
    account_of(params)
    out = []
    for r in as_list(params, "recipient"):
        n = resolve(r)
        out.append({"recipient": r, "number": n or r, "uuid": ACCOUNTS[n]["uuid"] if n else None,
                    "isRegistered": n is not None})
    for u in as_list(params, "username"):
        n = resolve(u)
        out.append({"recipient": u, "username": u, "uuid": ACCOUNTS[n]["uuid"] if n else None,
                    "isRegistered": n is not None})
    return out


def m_getAttachment(params):
    account_of(params)
    aid = params.get("id")
    path = ATTACHMENTS.get(aid)
    if not path or not os.path.isfile(path):
        raise RpcError("Could not find attachment with ID: " + str(aid))
    with open(path, "rb") as f:
        return {"data": base64.b64encode(f.read()).decode()}


def m_sendSyncRequest(params):
    account_of(params)
    return {}


METHODS = {name[2:]: fn for name, fn in globals().items() if name.startswith("m_")}


def rpc(request):
    rid = request.get("id")
    method = request.get("method")
    if not method:
        return {"jsonrpc": "2.0", "error": {"code": -32600, "message": "method field must be set", "data": None},
                "id": None}
    fn = METHODS.get(method)
    if fn is None:
        return {"jsonrpc": "2.0", "error": {"code": -32601, "message": "Method not implemented", "data": None},
                "id": rid}
    try:
        with LOCK:
            result = fn(request.get("params") or {})
        return {"jsonrpc": "2.0", "result": result, "id": rid}
    except RpcError as e:
        return {"jsonrpc": "2.0", "error": {"code": e.code, "message": str(e), "data": None}, "id": rid}


# ---------------------------------------------------------------------------
# Test-only endpoints
# ---------------------------------------------------------------------------

def t_scan(body):
    link = LINKS.get(body.get("uri"))
    number = body.get("number")
    if link is None or number not in ACCOUNTS:
        return 404, {"error": "no such link or account"}
    link["number"] = number
    CHANGED.notify_all()
    return 200, {}


def t_expire(body):
    uri = body.get("uri") or ""
    n = 0
    for u, link in LINKS.items():
        if (not uri or u == uri) and link["number"] is None:
            link["expired"] = True
            n += 1
    CHANGED.notify_all()
    return 200, {"expired": n}


def t_phone_send(body):
    sender = resolve(body.get("from") or "")
    if sender is None:
        return 404, {"error": "no such account"}
    params = {"message": body.get("message")}
    if body.get("groupId"):
        params["groupId"] = body["groupId"]
    else:
        params["recipient"] = [body.get("to")]
    if body.get("attachment"):
        params["attachment"] = body["attachment"]
    for key in ("quoteTimestamp", "quoteAuthor", "quoteMessage", "editTimestamp"):
        if body.get(key) is not None:
            params[key] = body[key]
    try:
        return 200, send_from(sender, params, sync_copy=True)
    except RpcError as e:
        return 400, {"error": str(e)}


def t_state(body):
    return 200, {
        "accounts": {n: {"uuid": a["uuid"], "linked": a["linked"], "queued": len(a["queue"])}
                     for n, a in ACCOUNTS.items()},
        "groups": {g: {"name": v["name"], "members": v["members"]} for g, v in GROUPS.items()},
        "links": list(LINKS.keys()),
    }


TEST = {"/_test/scan": t_scan, "/_test/expire": t_expire, "/_test/phone_send": t_phone_send,
        "/_test/state": t_state}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def answer(self, status, payload):
        data = json.dumps(payload).encode() if payload is not None else b""
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass    # the client gave up on a waiting receive (signed out, shut down)

    def do_GET(self):
        if self.path == "/api/v1/check":
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.answer(404, {"error": "not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            body = json.loads(raw.decode() or "{}")
        except ValueError:
            self.answer(200, {"jsonrpc": "2.0", "error": {"code": -32700, "message": "Parse error", "data": None},
                              "id": None})
            return
        if self.path == "/api/v1/rpc":
            if isinstance(body, list):
                self.answer(200, [rpc(r) for r in body])
            else:
                self.answer(200, rpc(body))
            return
        fn = TEST.get(self.path)
        if fn is None:
            self.answer(404, {"error": "not found"})
            return
        with LOCK:
            status, payload = fn(body)
        self.answer(status, payload)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--accounts", default="+4915100000001,+4915100000002,+4915100000003")
    parser.add_argument("--names", default="")
    parser.add_argument("--config", "-c", "--data-dir", "-d", dest="config", default=None)
    parser.add_argument("command", nargs="?", default=None)
    parser.add_argument("--http", default=None)
    parser.add_argument("--receive-mode", default=None)
    args, _ = parser.parse_known_args()

    numbers = [n.strip() for n in args.accounts.split(",") if n.strip()]
    names = [n.strip() for n in args.names.split(",") if n.strip()]
    for i, number in enumerate(numbers):
        name = names[i] if i < len(names) else "User %d" % (i + 1)
        ACCOUNTS[number] = {
            "uuid": str(uuidlib.uuid5(uuidlib.NAMESPACE_OID, "signal" + number)),
            "name": name, "username": "%s.%02d" % (name.split(" ")[0].lower(), i + 1),
            "linked": False, "queue": [], "contacts": set(),
        }

    if args.config:
        CONFIG["dir"] = args.config
        os.makedirs(args.config, exist_ok=True)
        try:
            with open(os.path.join(args.config, "fake-accounts.json")) as f:
                for number in json.load(f).get("linked", []):
                    if number in ACCOUNTS:
                        ACCOUNTS[number]["linked"] = True
        except (OSError, ValueError):
            pass
        CONFIG["attachments"] = os.path.join(args.config, "attachments")
    else:
        import tempfile
        CONFIG["attachments"] = os.path.join(tempfile.mkdtemp(prefix="signal-fake-"), "attachments")
    os.makedirs(CONFIG["attachments"], exist_ok=True)

    host, port = "127.0.0.1", 0
    if args.http:
        h, _, p = args.http.rpartition(":")
        host, port = (h or "127.0.0.1"), int(p)
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    url = "http://127.0.0.1:%d" % server.server_address[1]
    if args.config:
        with open(os.path.join(args.config, "fake-daemon.json"), "w") as f:
            json.dump({"url": url, "pid": os.getpid()}, f)
    print(json.dumps({"url": url}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
