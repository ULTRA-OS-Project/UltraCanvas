#!/usr/bin/env python3
"""A small XMPP server in memory, for XmppProviderTests: XMPP over WebSocket
(RFC 7395) on an HTTP server that also answers XEP-0156 discovery
(/.well-known/host-meta.json and host-meta) and the HTTP File Upload slots
(XEP-0363 PUT and GET).

What it speaks: the WebSocket itself (RFC 6455: the upgrade, text frames,
the client's masking, close, ping and pong); SASL SCRAM-SHA-256,
SCRAM-SHA-1 (salted, iterated, with the server's signature) and PLAIN;
resource binding; the roster and presence subscriptions (RFC 6121); chats
routed between the users' connections, stored while they are offline;
carbons (XEP-0280); receipts, markers, chat states, corrections and replies
passed along; retractions (XEP-0424, which take the message out of the
archives too); the message archive (XEP-0313) paged with RSM; service
discovery; an upload service; a group chat service (XEP-0045: created,
configured, joined, members, invitations, messages, its own archive);
bookmarks in PEP (XEP-0402) and private storage (XEP-0048).

Standard library only. Prints {"url": "http://127.0.0.1:<port>"} once it
listens; runs until it is killed.

    xmpp_fake_server.py --users alice,bob,carol

Each user is <name>@example.org with the password "<name>-pass". For the
tests: POST /_test/drop {"user": "alice"} cuts alice's connections without
a goodbye; POST /_test/mechanisms {"mechanisms": [...]} sets the SASL
mechanisms offered; POST /_test/password {"user": ..., "password": ...}
changes a password (a new salt).
"""

import argparse
import base64
import copy
import hashlib
import hmac
import json
import secrets
import socket
import struct
import sys
import threading
import time
import urllib.parse
import xml.etree.ElementTree as ET
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DOMAIN = "example.org"
MUC = "conference.example.org"
UPLOAD = "upload.example.org"

NS_FRAMING = "urn:ietf:params:xml:ns:xmpp-framing"
NS_STREAMS = "http://etherx.jabber.org/streams"
NS_CLIENT = "jabber:client"
NS_SASL = "urn:ietf:params:xml:ns:xmpp-sasl"
NS_BIND = "urn:ietf:params:xml:ns:xmpp-bind"
NS_SESSION = "urn:ietf:params:xml:ns:xmpp-session"
NS_STANZAS = "urn:ietf:params:xml:ns:xmpp-stanzas"
NS_ROSTER = "jabber:iq:roster"
NS_DISCO_INFO = "http://jabber.org/protocol/disco#info"
NS_DISCO_ITEMS = "http://jabber.org/protocol/disco#items"
NS_CARBONS = "urn:xmpp:carbons:2"
NS_FORWARD = "urn:xmpp:forward:0"
NS_DELAY = "urn:xmpp:delay"
NS_SID = "urn:xmpp:sid:0"
NS_MAM = "urn:xmpp:mam:2"
NS_RSM = "http://jabber.org/protocol/rsm"
NS_DATA = "jabber:x:data"
NS_UPLOAD = "urn:xmpp:http:upload:0"
NS_MUC = "http://jabber.org/protocol/muc"
NS_MUC_USER = "http://jabber.org/protocol/muc#user"
NS_MUC_OWNER = "http://jabber.org/protocol/muc#owner"
NS_MUC_ADMIN = "http://jabber.org/protocol/muc#admin"
NS_PUBSUB = "http://jabber.org/protocol/pubsub"
NS_PUBSUB_EVENT = "http://jabber.org/protocol/pubsub#event"
NS_BOOKMARKS = "urn:xmpp:bookmarks:1"
NS_PRIVATE = "jabber:iq:private"
NS_RETRACT = "urn:xmpp:message-retract:1"
NS_HINTS = "urn:xmpp:hints"
NS_PING = "urn:xmpp:ping"
NS_RECEIPTS = "urn:xmpp:receipts"
NS_MARKERS = "urn:xmpp:chat-markers:0"
NS_XML = "http://www.w3.org/XML/1998/namespace"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

LOCK = threading.RLock()
USERS = {}          # bare -> {"password", "scram": {mechanism: (salt, iterations, stored_key, server_key)}}
ROSTERS = {}        # bare -> {jid: {"name", "subscription", "ask"}}
SESSIONS = {}       # bare -> {resource: Conn}
OFFLINE = {}        # bare -> [message]
PENDING_SUBS = {}   # bare -> set of bare JIDs asking to see its presence
ARCHIVES = {}       # bare -> [entry]; entry = {"id", "stamp", "with", "message"}
PRIVATE = {}        # bare -> storage element (XEP-0048)
PEP = {}            # bare -> {item id: item element} (XEP-0402)
ROOMS = {}          # room bare JID -> Room
UPLOADS = {}        # path -> {"size", "type", "auth", "data"}
MECHANISMS = ["SCRAM-SHA-256", "SCRAM-SHA-1", "PLAIN"]
BASE = {"url": ""}
HASHES = {"SCRAM-SHA-256": "sha256", "SCRAM-SHA-1": "sha1"}


# ---- XML ----------------------------------------------------------------------

def q(ns, name):
    return "{%s}%s" % (ns, name)


def split(tag):
    if tag.startswith("{"):
        ns, name = tag[1:].split("}", 1)
        return ns, name
    return "", tag


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def xml(el, parent_ns=None):
    """The element as XML, every namespace a default one (no prefixes)."""
    ns, name = split(el.tag)
    out = "<" + name
    if ns != parent_ns:
        out += ' xmlns="%s"' % esc(ns)
    for k, v in el.attrib.items():
        if k.startswith("{" + NS_XML + "}"):
            k = "xml:" + k.split("}", 1)[1]
        elif k.startswith("{"):
            continue
        out += ' %s="%s"' % (k, esc(v))
    children = list(el)
    if not children and not el.text:
        return out + "/>"
    out += ">" + esc(el.text or "")
    for c in children:
        out += xml(c, ns)
    return out + "</%s>" % name


def E(ns, name, attrs=None, text=None, children=()):
    el = ET.Element(q(ns, name), {k: v for k, v in (attrs or {}).items() if v is not None})
    if text:
        el.text = text
    for c in children:
        el.append(c)
    return el


def child(el, ns, name):
    return el.find(q(ns, name))


def bare(jid):
    return (jid or "").split("/", 1)[0].lower()


def resource(jid):
    parts = (jid or "").split("/", 1)
    return parts[1] if len(parts) > 1 else ""


def domain(jid):
    b = bare(jid)
    return b.split("@", 1)[1] if "@" in b else b


def stamp(t=None):
    t = time.time() if t is None else t
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(t)) + ".%03dZ" % int((t % 1) * 1000)


def parse_stamp(s):
    s = s.rstrip("Z")
    frac = 0.0
    if "." in s:
        s, f = s.split(".", 1)
        frac = float("0." + f)
    return time.mktime(time.strptime(s, "%Y-%m-%dT%H:%M:%S")) - time.timezone + frac


def new_id():
    return secrets.token_hex(8)


# ---- users and SCRAM ---------------------------------------------------------------

def set_password(jid, password):
    scram = {}
    for mech, h in HASHES.items():
        salt = secrets.token_bytes(16)
        iterations = 4096
        salted = hashlib.pbkdf2_hmac(h, password.encode(), salt, iterations)
        client_key = hmac.new(salted, b"Client Key", h).digest()
        stored_key = hashlib.new(h, client_key).digest()
        server_key = hmac.new(salted, b"Server Key", h).digest()
        scram[mech] = (salt, iterations, stored_key, server_key)
    USERS[jid] = {"password": password, "scram": scram}


def roster(jid):
    return ROSTERS.setdefault(jid, {})


def roster_item(owner, jid, create=False):
    r = roster(owner)
    if jid not in r and create:
        r[jid] = {"name": None, "subscription": "none", "ask": None}
    return r.get(jid)


def add_sub(item, direction):
    s = item["subscription"]
    have = {"none": set(), "to": {"to"}, "from": {"from"}, "both": {"to", "from"}}[s] | {direction}
    item["subscription"] = "both" if have == {"to", "from"} else (next(iter(have)) if have else "none")


def item_element(jid, item):
    return E(NS_ROSTER, "item", {"jid": jid, "name": item["name"], "subscription": item["subscription"],
                                 "ask": item["ask"]})


def roster_push(owner, jid):
    item = roster_item(owner, jid)
    el = item_element(jid, item) if item else E(NS_ROSTER, "item", {"jid": jid, "subscription": "remove"})
    for c in all_conns(owner):
        c.send(E(NS_CLIENT, "iq", {"type": "set", "id": new_id(), "to": c.full}, children=[E(NS_ROSTER, "query", children=[copy.deepcopy(el)])]))


def all_conns(jid):
    return [c for c in SESSIONS.get(jid, {}).values() if c.full]


def available_conns(jid):
    return [c for c in all_conns(jid) if c.available]


def store_offline(jid, message):
    m = copy.deepcopy(message)
    m.append(E(NS_DELAY, "delay", {"stamp": stamp(), "from": DOMAIN}))
    OFFLINE.setdefault(jid, []).append(m)


def archive(owner, with_jid, message, entry_id=None):
    entry = {"id": entry_id or new_id(), "stamp": time.time(), "with": with_jid, "message": copy.deepcopy(message)}
    ARCHIVES.setdefault(owner, []).append(entry)
    return entry["id"]


def ids_of(message):
    ids = {message.get("id")}
    o = child(message, NS_SID, "origin-id")
    if o is not None:
        ids.add(o.get("id"))
    for s in message.findall(q(NS_SID, "stanza-id")):
        ids.add(s.get("id"))
    ids.discard(None)
    return ids


def retract_from(entries, target, sender_bare, sender_of):
    entries[:] = [e for e in entries
                  if not ((target in ids_of(e["message"]) or e["id"] == target) and sender_of(e) == sender_bare)]


# ---- rooms --------------------------------------------------------------------------

class Room:
    def __init__(self, jid, owner):
        self.jid = jid
        self.name = jid.split("@", 1)[0]
        self.members_only = False
        self.persistent = False
        self.locked = True
        self.affiliations = {owner: "owner"}
        self.occupants = {}     # nick -> Conn
        self.archive = []       # entries as the users' archives, "with" unused

    def nick_of(self, conn):
        for n, c in self.occupants.items():
            if c is conn:
                return n
        return None

    def item(self, conn):
        aff = self.affiliations.get(conn.bare, "none")
        role = "moderator" if aff in ("owner", "admin") else "participant"
        return E(NS_MUC_USER, "item", {"affiliation": aff, "role": role, "jid": conn.full})


# ---- a connection ---------------------------------------------------------------------

class Conn:
    def __init__(self, sock, rfile):
        self.sock = sock
        self.rfile = rfile
        self.send_lock = threading.Lock()
        self.alive = True
        self.user = None            # bare JID once signed in
        self.bare = None
        self.full = None
        self.available = False
        self.presence = None
        self.carbons = False
        self.scram = None

    # -- RFC 6455 --
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

    def send(self, el):
        self.send_frame(1, xml(el).encode())

    def send_raw(self, text):
        self.send_frame(1, text.encode())

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
            if n == 126:
                n = struct.unpack(">H", self.read_exact(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self.read_exact(8))[0]
            mask = self.read_exact(4) if masked else b"\0\0\0\0"
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
                    el = ET.fromstring(payload.decode())
                except ET.ParseError:
                    continue
                with LOCK:
                    self.handle(el)
        except (EOFError, OSError, ValueError):
            pass
        finally:
            with LOCK:
                self.gone()

    def drop(self):
        """Gone without a goodbye: the socket shut."""
        with self.send_lock:
            self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def gone(self):
        self.alive = False
        if self.bare and SESSIONS.get(self.bare, {}).get(resource(self.full)) is self:
            del SESSIONS[self.bare][resource(self.full)]
        for room in list(ROOMS.values()):
            nick = room.nick_of(self)
            if nick:
                leave_room(self, room, nick)
        if self.available:
            self.available = False
            broadcast_presence(self, E(NS_CLIENT, "presence", {"type": "unavailable"}))

    # -- the stream --
    def handle(self, el):
        ns, name = split(el.tag)
        if ns == NS_FRAMING and name == "open":
            self.open_stream()
        elif ns == NS_FRAMING and name == "close":
            self.send(E(NS_FRAMING, "close"))
            self.drop()
        elif ns == NS_SASL:
            self.sasl(name, el)
        elif ns == NS_CLIENT and self.user:
            if name == "iq":
                handle_iq(self, el)
            elif name == "message" and self.full:
                handle_message(self, el)
            elif name == "presence" and self.full:
                handle_presence(self, el)

    def open_stream(self):
        self.send(E(NS_FRAMING, "open", {"from": DOMAIN, "id": new_id(), "version": "1.0",
                                         q(NS_XML, "lang"): "en"}))
        if self.user:
            inner = xml(E(NS_BIND, "bind")) + xml(E(NS_SESSION, "session", children=[E(NS_SESSION, "optional")]))
        else:
            inner = xml(E(NS_SASL, "mechanisms", children=[E(NS_SASL, "mechanism", text=m) for m in MECHANISMS]))
        # The prefixed form RFC 7395 shows, to be read as such.
        self.send_raw('<stream:features xmlns:stream="%s">%s</stream:features>' % (NS_STREAMS, inner))

    def sasl_fail(self, condition="not-authorized"):
        self.scram = None
        self.send(E(NS_SASL, "failure", children=[E(NS_SASL, condition)]))

    def sasl(self, name, el):
        if name == "abort":
            return self.sasl_fail("aborted")
        if name == "auth":
            mech = el.get("mechanism")
            if mech not in MECHANISMS:
                return self.sasl_fail("invalid-mechanism")
            data = base64.b64decode(el.text or "")
            if mech == "PLAIN":
                _, user, password = data.decode().split("\0", 2)
                jid = user.lower() + "@" + DOMAIN
                if jid in USERS and USERS[jid]["password"] == password:
                    self.user = jid
                    return self.send(E(NS_SASL, "success"))
                return self.sasl_fail()
            first = data.decode()
            if not first.startswith("n,,"):
                return self.sasl_fail("malformed-request")
            bare_first = first[3:]
            attrs = dict(p.split("=", 1) for p in bare_first.split(","))
            jid = attrs["n"].replace("=2C", ",").replace("=3D", "=").lower() + "@" + DOMAIN
            known = jid in USERS
            salt, iterations, stored, server_key = (USERS[jid]["scram"][mech] if known
                                                    else (secrets.token_bytes(16), 4096, b"", b""))
            nonce = attrs["r"] + secrets.token_urlsafe(18)
            server_first = "r=%s,s=%s,i=%d" % (nonce, base64.b64encode(salt).decode(), iterations)
            self.scram = {"mech": mech, "jid": jid, "known": known, "bare_first": bare_first,
                          "server_first": server_first, "nonce": nonce, "stored": stored, "server_key": server_key}
            return self.send(E(NS_SASL, "challenge", text=base64.b64encode(server_first.encode()).decode()))
        if name == "response" and self.scram:
            s = self.scram
            final = base64.b64decode(el.text or "").decode()
            without_proof, _, proof = final.rpartition(",p=")
            attrs = dict(p.split("=", 1) for p in without_proof.split(","))
            h = HASHES[s["mech"]]
            if attrs.get("r") != s["nonce"] or attrs.get("c") != "biws" or not s["known"]:
                return self.sasl_fail()
            auth_message = ",".join([s["bare_first"], s["server_first"], without_proof])
            signature = hmac.new(s["stored"], auth_message.encode(), h).digest()
            client_key = bytes(a ^ b for a, b in zip(base64.b64decode(proof), signature))
            if hashlib.new(h, client_key).digest() != s["stored"]:
                return self.sasl_fail()
            verifier = hmac.new(s["server_key"], auth_message.encode(), h).digest()
            self.user = s["jid"]
            self.scram = None
            return self.send(E(NS_SASL, "success", text=base64.b64encode(b"v=" + base64.b64encode(verifier)).decode()))
        self.sasl_fail("malformed-request")

    def bind(self, iq):
        b = child(iq, NS_BIND, "bind")
        res = (b.findtext(q(NS_BIND, "resource")) or "").strip() or new_id()
        sessions = SESSIONS.setdefault(self.user, {})
        if res in sessions:
            res += "-" + new_id()[:4]
        self.bare = self.user
        self.full = self.user + "/" + res
        sessions[res] = self
        self.send(result(iq, E(NS_BIND, "bind", children=[E(NS_BIND, "jid", text=self.full)])))


# ---- iq -----------------------------------------------------------------------------------

def result(iq, *children):
    r = E(NS_CLIENT, "iq", {"type": "result", "id": iq.get("id")}, children=children)
    if iq.get("to"):
        r.set("from", iq.get("to"))
    return r


def error(stanza, condition, kind="cancel"):
    ns, name = split(stanza.tag)
    r = E(NS_CLIENT, name, {"type": "error", "id": stanza.get("id")},
          children=[E(NS_CLIENT, "error", {"type": kind}, children=[E(NS_STANZAS, condition)])])
    if stanza.get("to"):
        r.set("from", stanza.get("to"))
    return r


def disco_info(identities, features, extra=()):
    return E(NS_DISCO_INFO, "query", children=[E(NS_DISCO_INFO, "identity", i) for i in identities] +
             [E(NS_DISCO_INFO, "feature", {"var": f}) for f in features] + list(extra))


def form_fields(x):
    out = {}
    if x is None:
        return out
    for f in x.findall(q(NS_DATA, "field")):
        out[f.get("var")] = [v.text or "" for v in f.findall(q(NS_DATA, "value"))]
    return out


def handle_iq(conn, iq):
    typ = iq.get("type")
    to = iq.get("to")
    payload = list(iq)
    first = payload[0] if payload else None
    ns, name = split(first.tag) if first is not None else ("", "")
    if typ in ("result", "error"):
        if to and "/" in to and bare(to) in SESSIONS:
            iq.set("from", conn.full)
            for c in all_conns(bare(to)):
                if c.full == to:
                    c.send(iq)
        return
    if ns == NS_BIND and not conn.full:
        return conn.bind(iq)
    if not conn.full:
        return conn.send(error(iq, "not-authorized", "auth"))
    if to is None or bare(to) == conn.bare or to == DOMAIN:
        return own_iq(conn, iq, typ, first, ns, name)
    if to == UPLOAD:
        return upload_iq(conn, iq, typ, first, ns, name)
    if domain(to) == MUC:
        return muc_iq(conn, iq, typ, first, ns, name)
    target = bare(to)
    if target in USERS:
        if "/" in to:
            for c in all_conns(target):
                if c.full == to:
                    iq.set("from", conn.full)
                    return c.send(iq)
            return conn.send(error(iq, "service-unavailable"))
        if ns == NS_DISCO_INFO:
            return conn.send(result(iq, disco_info([{"category": "account", "type": "registered"}], [NS_DISCO_INFO])))
        return conn.send(error(iq, "service-unavailable"))
    conn.send(error(iq, "item-not-found"))


def own_iq(conn, iq, typ, first, ns, name):
    if ns == NS_SESSION:
        return conn.send(result(iq))
    if ns == NS_PING:
        return conn.send(result(iq))
    if ns == NS_ROSTER:
        if typ == "get":
            return conn.send(result(iq, E(NS_ROSTER, "query", children=[item_element(j, i) for j, i in roster(conn.bare).items()])))
        for item in first.findall(q(NS_ROSTER, "item")):
            jid = bare(item.get("jid"))
            if item.get("subscription") == "remove":
                roster(conn.bare).pop(jid, None)
            else:
                entry = roster_item(conn.bare, jid, create=True)
                entry["name"] = item.get("name")
            conn.send(result(iq))
            roster_push(conn.bare, jid)
        return
    if ns == NS_CARBONS:
        conn.carbons = name == "enable"
        return conn.send(result(iq))
    if ns == NS_DISCO_INFO:
        if iq.get("to") == DOMAIN:
            return conn.send(result(iq, disco_info([{"category": "server", "type": "im", "name": "fake"}],
                                                   [NS_DISCO_INFO, NS_DISCO_ITEMS, NS_ROSTER, NS_CARBONS, NS_MAM])))
        return conn.send(result(iq, disco_info([{"category": "account", "type": "registered"}],
                                               [NS_DISCO_INFO, NS_MAM, NS_PUBSUB])))
    if ns == NS_DISCO_ITEMS:
        return conn.send(result(iq, E(NS_DISCO_ITEMS, "query", children=[
            E(NS_DISCO_ITEMS, "item", {"jid": UPLOAD, "name": "Upload"}),
            E(NS_DISCO_ITEMS, "item", {"jid": MUC, "name": "Chatrooms"})])))
    if ns == NS_PRIVATE:
        storage = list(first)[0] if list(first) else None
        if typ == "get":
            have = PRIVATE.get(conn.bare)
            return conn.send(result(iq, E(NS_PRIVATE, "query", children=[copy.deepcopy(have) if have is not None else storage])))
        PRIVATE[conn.bare] = copy.deepcopy(storage)
        return conn.send(result(iq))
    if ns == NS_PUBSUB:
        return pep_iq(conn, iq, typ, first)
    if ns == NS_MAM and typ == "set":
        return mam_query(conn, iq, first, ARCHIVES.get(conn.bare, []), with_filter=True)
    conn.send(error(iq, "service-unavailable"))


def pep_iq(conn, iq, typ, pubsub):
    node_items = PEP.setdefault(conn.bare, {})
    items = child(pubsub, NS_PUBSUB, "items")
    publish = child(pubsub, NS_PUBSUB, "publish")
    retract = child(pubsub, NS_PUBSUB, "retract")
    if typ == "get" and items is not None and items.get("node") == NS_BOOKMARKS:
        if conn.bare not in PEP or not node_items:
            return conn.send(error(iq, "item-not-found"))
        return conn.send(result(iq, E(NS_PUBSUB, "pubsub", children=[
            E(NS_PUBSUB, "items", {"node": NS_BOOKMARKS}, children=[copy.deepcopy(i) for i in node_items.values()])])))
    if typ == "set" and publish is not None and publish.get("node") == NS_BOOKMARKS:
        item = child(publish, NS_PUBSUB, "item")
        node_items[item.get("id")] = copy.deepcopy(item)
        conn.send(result(iq))
        return pep_notify(conn.bare, E(NS_PUBSUB_EVENT, "item", {"id": item.get("id")}, children=[copy.deepcopy(c) for c in item]))
    if typ == "set" and retract is not None and retract.get("node") == NS_BOOKMARKS:
        for item in retract.findall(q(NS_PUBSUB, "item")):
            node_items.pop(item.get("id"), None)
            pep_notify(conn.bare, E(NS_PUBSUB_EVENT, "retract", {"id": item.get("id")}))
        return conn.send(result(iq))
    conn.send(error(iq, "feature-not-implemented"))


def pep_notify(owner, event_item):
    for c in all_conns(owner):
        c.send(E(NS_CLIENT, "message", {"from": owner, "to": c.full, "type": "headline", "id": new_id()}, children=[
            E(NS_PUBSUB_EVENT, "event", children=[E(NS_PUBSUB_EVENT, "items", {"node": NS_BOOKMARKS},
                                                     children=[copy.deepcopy(event_item)])])]))


def mam_query(conn, iq, query, entries, with_filter, room=None):
    fields = form_fields(child(query, NS_DATA, "x"))
    with_jid = (fields.get("with") or [None])[0]
    if with_filter and with_jid:
        entries = [e for e in entries if e["with"] == bare(with_jid)]
    rsm = child(query, NS_RSM, "set")
    limit = int(rsm.findtext(q(NS_RSM, "max")) or "50") if rsm is not None else 50
    before = child(rsm, NS_RSM, "before") if rsm is not None else None
    after = rsm.findtext(q(NS_RSM, "after")) if rsm is not None else None
    complete = True
    if before is not None:
        if before.text:
            ids = [e["id"] for e in entries]
            entries = entries[:ids.index(before.text)] if before.text in ids else []
        page = entries[-limit:] if limit > 0 else []
        complete = len(entries) <= limit
    elif after:
        ids = [e["id"] for e in entries]
        rest = entries[ids.index(after) + 1:] if after in ids else []
        page = rest[:limit]
        complete = len(rest) <= limit
    else:
        page = entries[:limit]
        complete = len(entries) <= limit
    queryid = query.get("queryid")
    for e in page:
        m = E(NS_CLIENT, "message", {"to": conn.full, "id": new_id()}, children=[
            E(NS_MAM, "result", {"queryid": queryid, "id": e["id"]}, children=[
                E(NS_FORWARD, "forwarded", children=[E(NS_DELAY, "delay", {"stamp": stamp(e["stamp"])}),
                                                     copy.deepcopy(e["message"])])])])
        if room:
            m.set("from", room)
        conn.send(m)
    rsm_out = E(NS_RSM, "set")
    if page:
        rsm_out.append(E(NS_RSM, "first", text=page[0]["id"]))
        rsm_out.append(E(NS_RSM, "last", text=page[-1]["id"]))
    conn.send(result(iq, E(NS_MAM, "fin", {"complete": "true" if complete else None}, children=[rsm_out])))


def upload_iq(conn, iq, typ, first, ns, name):
    if ns == NS_DISCO_INFO:
        form = E(NS_DATA, "x", {"type": "result"}, children=[
            E(NS_DATA, "field", {"var": "FORM_TYPE", "type": "hidden"}, children=[E(NS_DATA, "value", text=NS_UPLOAD)]),
            E(NS_DATA, "field", {"var": "max-file-size"}, children=[E(NS_DATA, "value", text=str(50 * 1024 * 1024))])])
        return conn.send(result(iq, disco_info([{"category": "store", "type": "file", "name": "Upload"}],
                                               [NS_DISCO_INFO, NS_UPLOAD], [form])))
    if ns == NS_UPLOAD and name == "request" and typ == "get":
        size = int(first.get("size") or "0")
        if size > 50 * 1024 * 1024:
            return conn.send(error(iq, "not-acceptable", "modify"))
        path = "/upload/%s/%s" % (new_id(), urllib.parse.quote(first.get("filename") or "file"))
        secret = secrets.token_hex(12)
        UPLOADS[path] = {"size": size, "type": first.get("content-type") or "application/octet-stream",
                         "auth": "Bearer " + secret, "data": None}
        url = BASE["url"] + path
        return conn.send(result(iq, E(NS_UPLOAD, "slot", children=[
            E(NS_UPLOAD, "put", {"url": url}, children=[E(NS_UPLOAD, "header", {"name": "Authorization"},
                                                          text="Bearer " + secret)]),
            E(NS_UPLOAD, "get", {"url": url})])))
    conn.send(error(iq, "service-unavailable"))


# ---- presence --------------------------------------------------------------------------------

def broadcast_presence(conn, el):
    out = copy.deepcopy(el)
    out.set("from", conn.full)
    for jid, item in roster(conn.bare).items():
        if item["subscription"] in ("from", "both"):
            for c in available_conns(jid):
                p = copy.deepcopy(out)
                p.set("to", c.full)
                c.send(p)
    for c in available_conns(conn.bare):
        if c is not conn:
            p = copy.deepcopy(out)
            p.set("to", c.full)
            c.send(p)


def send_presence_of(owner, to_conns):
    for c in available_conns(owner):
        for t in to_conns:
            p = copy.deepcopy(c.presence)
            p.set("from", c.full)
            p.set("to", t.full)
            t.send(p)


def handle_presence(conn, el):
    to = el.get("to")
    typ = el.get("type")
    if to and domain(to) == MUC:
        return muc_presence(conn, el)
    if to is None:
        if typ not in (None, "unavailable"):
            return
        first = typ is None and not conn.available
        conn.available = typ is None
        conn.presence = copy.deepcopy(el)
        broadcast_presence(conn, el)
        if first:
            for jid, item in roster(conn.bare).items():
                if item["subscription"] in ("to", "both"):
                    send_presence_of(jid, [conn])
            for asker in sorted(PENDING_SUBS.pop(conn.bare, set())):
                conn.send(E(NS_CLIENT, "presence", {"from": asker, "to": conn.bare, "type": "subscribe"}))
            for m in OFFLINE.pop(conn.bare, []):
                conn.send(m)
        return
    target = bare(to)
    if target not in USERS:
        return
    if typ == "subscribe":
        item = roster_item(conn.bare, target, create=True)
        if item["subscription"] not in ("to", "both"):
            item["ask"] = "subscribe"
            roster_push(conn.bare, target)
        targets = available_conns(target)
        if targets:
            for c in targets:
                c.send(E(NS_CLIENT, "presence", {"from": conn.bare, "to": c.full, "type": "subscribe"}))
        else:
            PENDING_SUBS.setdefault(target, set()).add(conn.bare)
    elif typ == "subscribed":
        mine = roster_item(conn.bare, target, create=True)
        add_sub(mine, "from")
        roster_push(conn.bare, target)
        theirs = roster_item(target, conn.bare, create=True)
        add_sub(theirs, "to")
        theirs["ask"] = None
        roster_push(target, conn.bare)
        for c in all_conns(target):
            c.send(E(NS_CLIENT, "presence", {"from": conn.bare, "to": c.full, "type": "subscribed"}))
        send_presence_of(conn.bare, available_conns(target))
        PENDING_SUBS.get(conn.bare, set()).discard(target)
    elif typ in ("unsubscribe", "unsubscribed"):
        mine = roster_item(conn.bare, target)
        theirs = roster_item(target, conn.bare)
        lose_mine, lose_theirs = ("to", "from") if typ == "unsubscribe" else ("from", "to")
        for item, lose, owner, jid in ((mine, lose_mine, conn.bare, target), (theirs, lose_theirs, target, conn.bare)):
            if item:
                s = {"none": set(), "to": {"to"}, "from": {"from"}, "both": {"to", "from"}}[item["subscription"]] - {lose}
                item["subscription"] = "both" if len(s) == 2 else (next(iter(s)) if s else "none")
                roster_push(owner, jid)
    else:
        out = copy.deepcopy(el)
        out.set("from", conn.full)
        for c in available_conns(target):
            if "/" not in to or c.full == to:
                c.send(out)


# ---- messages ------------------------------------------------------------------------------------

def is_retract(m):
    return child(m, NS_RETRACT, "retract") is not None


def handle_message(conn, el):
    to = el.get("to")
    typ = el.get("type") or "normal"
    if not to:
        return
    el.set("from", conn.full)
    target = bare(to)
    if domain(target) == MUC:
        return muc_message(conn, el)
    if target not in USERS:
        return conn.send(error(el, "service-unavailable"))
    body = child(el, NS_CLIENT, "body")
    retract = child(el, NS_RETRACT, "retract")
    if retract is not None:
        # Out of both archives: only its sender's.
        for owner in (conn.bare, target):
            retract_from(ARCHIVES.setdefault(owner, []), retract.get("id"), conn.bare,
                         lambda e: bare(e["message"].get("from")))
    delivered = copy.deepcopy(el)
    mine = copy.deepcopy(el)
    if typ in ("chat", "normal") and body is not None and retract is None:
        theirs_id = archive(target, conn.bare, el)
        my_id = archive(conn.bare, target, el)
        delivered.append(E(NS_SID, "stanza-id", {"id": theirs_id, "by": target}))
        mine.append(E(NS_SID, "stanza-id", {"id": my_id, "by": conn.bare}))
        ARCHIVES[target][-1]["message"] = copy.deepcopy(delivered)
        ARCHIVES[conn.bare][-1]["message"] = copy.deepcopy(mine)
    if "/" in to:
        exact = [c for c in all_conns(target) if c.full == to]
        for c in exact:
            c.send(delivered)
        # The recipient's other clients get a copy (received carbons).
        for c in available_conns(target):
            if c.carbons and c.full != to:
                c.send(carbon("received", target, c, delivered))
        if not exact and body is not None:
            store_offline(target, delivered)
    else:
        conns = available_conns(target)
        for c in conns:
            c.send(delivered)
        if not conns and (body is not None or child(el, NS_MARKERS, "displayed") is not None or
                          child(el, NS_RECEIPTS, "received") is not None):
            store_offline(target, delivered)
    if child(el, NS_CARBONS, "private") is None:
        for c in available_conns(conn.bare):
            if c is not conn and c.carbons:
                c.send(carbon("sent", conn.bare, c, mine))


def carbon(kind, owner, to_conn, message):
    return E(NS_CLIENT, "message", {"from": owner, "to": to_conn.full, "type": "chat", "id": new_id()}, children=[
        E(NS_CARBONS, kind, children=[E(NS_FORWARD, "forwarded", children=[copy.deepcopy(message)])])])


# ---- rooms ---------------------------------------------------------------------------------------

def room_presence(room, nick, c, typ=None, codes=()):
    p = E(NS_CLIENT, "presence", {"from": room.jid + "/" + nick, "type": typ})
    x = E(NS_MUC_USER, "x", children=[room.item(c)])
    for code in codes:
        x.append(E(NS_MUC_USER, "status", {"code": code}))
    if c.presence is not None and typ is None:
        show = c.presence.findtext(q(NS_CLIENT, "show"))
        if show:
            p.append(E(NS_CLIENT, "show", text=show))
    p.append(x)
    return p


def to_conn(el, c):
    out = copy.deepcopy(el)
    out.set("to", c.full)
    return out


def leave_room(conn, room, nick):
    room.occupants.pop(nick, None)
    for c in list(room.occupants.values()) + [conn]:
        c.send(to_conn(room_presence(room, nick, conn, "unavailable", ["110"] if c is conn else []), c))
    if not room.occupants and not room.persistent:
        ROOMS.pop(room.jid, None)


def muc_presence(conn, el):
    room_jid = bare(el.get("to"))
    nick = resource(el.get("to"))
    typ = el.get("type")
    room = ROOMS.get(room_jid)
    if typ == "unavailable":
        if room and room.nick_of(conn):
            leave_room(conn, room, room.nick_of(conn))
        return
    if typ is not None or not nick:
        return
    def refuse(condition, kind="cancel"):
        e = error(el, condition, kind)
        e.set("from", el.get("to"))
        e.set("to", conn.full)
        conn.send(e)
    created = False
    if room is None:
        room = ROOMS[room_jid] = Room(room_jid, conn.bare)
        created = True
    if room.locked and room.affiliations.get(conn.bare) != "owner":
        return refuse("item-not-found")
    if room.members_only and room.affiliations.get(conn.bare) not in ("owner", "admin", "member"):
        return refuse("registration-required", "auth")
    holder = room.occupants.get(nick)
    if holder is not None and holder.bare != conn.bare:
        return refuse("conflict")
    if holder is conn:
        return
    old = room.nick_of(conn)
    if old:
        room.occupants.pop(old)
    room.occupants[nick] = conn
    for n, c in room.occupants.items():
        if c is not conn:
            conn.send(to_conn(room_presence(room, n, c), conn))
    for n, c in room.occupants.items():
        if c is not conn:
            c.send(to_conn(room_presence(room, nick, conn), c))
    conn.send(to_conn(room_presence(room, nick, conn, None, ["110"] + (["201"] if created else [])), conn))
    # The history asked for.
    hist = None
    x = child(el, NS_MUC, "x")
    if x is not None:
        hist = child(x, NS_MUC, "history")
    entries = room.archive
    if hist is not None and hist.get("maxstanzas") is not None:
        n = int(hist.get("maxstanzas"))
        entries = entries[-n:] if n > 0 else []
    elif hist is not None and hist.get("since"):
        since = parse_stamp(hist.get("since"))
        entries = [e for e in entries if e["stamp"] >= since]
    else:
        entries = entries[-20:]
    for e in entries:
        m = to_conn(e["message"], conn)
        m.append(E(NS_DELAY, "delay", {"stamp": stamp(e["stamp"]), "from": room.jid}))
        conn.send(m)
    conn.send(E(NS_CLIENT, "message", {"from": room.jid, "to": conn.full, "type": "groupchat", "id": new_id()},
                children=[E(NS_CLIENT, "subject")]))


def muc_message(conn, el):
    room = ROOMS.get(bare(el.get("to")))
    if room is None:
        return conn.send(error(el, "item-not-found"))
    x = child(el, NS_MUC_USER, "x")
    if (el.get("type") or "normal") != "groupchat":
        if x is not None:
            for invite in x.findall(q(NS_MUC_USER, "invite")):
                invitee = bare(invite.get("to"))
                if room.affiliations.get(conn.bare) in ("owner", "admin") and invitee not in room.affiliations:
                    room.affiliations[invitee] = "member"
                out = E(NS_CLIENT, "message", {"from": room.jid, "id": new_id()}, children=[
                    E(NS_MUC_USER, "x", children=[E(NS_MUC_USER, "invite", {"from": conn.bare}, children=[
                        copy.deepcopy(c) for c in invite])])])
                conns = available_conns(invitee)
                for c in conns:
                    c.send(to_conn(out, c))
                if not conns:
                    store_offline(invitee, out)
        return
    nick = room.nick_of(conn)
    if not nick:
        return conn.send(error(el, "not-acceptable", "modify"))
    out = copy.deepcopy(el)
    out.set("from", room.jid + "/" + nick)
    out.attrib.pop("to", None)
    retract = child(el, NS_RETRACT, "retract")
    if retract is not None:
        retract_from(room.archive, retract.get("id"), conn.bare,
                     lambda e: bare(e["message"].find(q(NS_MUC_USER, "x")).find(q(NS_MUC_USER, "item")).get("jid")))
    elif child(el, NS_CLIENT, "body") is not None:
        entry_id = new_id()
        out.append(E(NS_SID, "stanza-id", {"id": entry_id, "by": room.jid}))
        archived = copy.deepcopy(out)
        archived.append(E(NS_MUC_USER, "x", children=[E(NS_MUC_USER, "item", {"jid": conn.full})]))
        room.archive.append({"id": entry_id, "stamp": time.time(), "with": None, "message": archived})
    for c in room.occupants.values():
        c.send(to_conn(out, c))


def room_config_form(room):
    def field(var, value, kind="text-single"):
        return E(NS_DATA, "field", {"var": var, "type": kind}, children=[E(NS_DATA, "value", text=value)])
    return E(NS_DATA, "x", {"type": "form"}, children=[
        E(NS_DATA, "field", {"var": "FORM_TYPE", "type": "hidden"},
          children=[E(NS_DATA, "value", text="http://jabber.org/protocol/muc#roomconfig")]),
        field("muc#roomconfig_roomname", room.name),
        field("muc#roomconfig_membersonly", "1" if room.members_only else "0", "boolean"),
        field("muc#roomconfig_persistentroom", "1" if room.persistent else "0", "boolean")])


def muc_iq(conn, iq, typ, first, ns, name):
    to = iq.get("to")
    if to == MUC:
        if ns == NS_DISCO_INFO:
            return conn.send(result(iq, disco_info([{"category": "conference", "type": "text", "name": "Chatrooms"}],
                                                   [NS_DISCO_INFO, NS_MUC])))
        if ns == NS_DISCO_ITEMS:
            return conn.send(result(iq, E(NS_DISCO_ITEMS, "query")))
        return conn.send(error(iq, "service-unavailable"))
    room = ROOMS.get(bare(to))
    if room is None or (room.locked and room.affiliations.get(conn.bare) != "owner"):
        return conn.send(error(iq, "item-not-found"))
    aff = room.affiliations.get(conn.bare)
    if ns == NS_DISCO_INFO:
        features = [NS_DISCO_INFO, NS_MUC, NS_MAM] + (["muc_membersonly"] if room.members_only else [])
        return conn.send(result(iq, disco_info([{"category": "conference", "type": "text", "name": room.name}], features)))
    if ns == NS_MUC_OWNER:
        if aff != "owner":
            return conn.send(error(iq, "forbidden", "auth"))
        if typ == "get":
            return conn.send(result(iq, E(NS_MUC_OWNER, "query", children=[room_config_form(room)])))
        fields = form_fields(child(first, NS_DATA, "x"))
        def flag(var, current):
            v = fields.get(var)
            return current if not v else v[0] in ("1", "true")
        room.name = (fields.get("muc#roomconfig_roomname") or [room.name])[0] or room.name
        room.members_only = flag("muc#roomconfig_membersonly", room.members_only)
        room.persistent = flag("muc#roomconfig_persistentroom", room.persistent)
        room.locked = False
        return conn.send(result(iq))
    if ns == NS_MUC_ADMIN:
        if typ == "set":
            if aff not in ("owner", "admin"):
                return conn.send(error(iq, "forbidden", "auth"))
            for item in first.findall(q(NS_MUC_ADMIN, "item")):
                jid, new = bare(item.get("jid")), item.get("affiliation")
                if new == "none":
                    room.affiliations.pop(jid, None)
                elif jid and new:
                    room.affiliations[jid] = new
            return conn.send(result(iq))
        if aff not in ("owner", "admin", "member"):
            return conn.send(error(iq, "forbidden", "auth"))
        want = first.find(q(NS_MUC_ADMIN, "item")).get("affiliation")
        return conn.send(result(iq, E(NS_MUC_ADMIN, "query", children=[
            E(NS_MUC_ADMIN, "item", {"jid": j, "affiliation": a}) for j, a in room.affiliations.items() if a == want])))
    if ns == NS_MAM and typ == "set":
        if room.members_only and aff not in ("owner", "admin", "member"):
            return conn.send(error(iq, "forbidden", "auth"))
        return mam_query(conn, iq, first, room.archive, with_filter=False, room=room.jid)
    conn.send(error(iq, "service-unavailable"))


# ---- HTTP -------------------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, status, body=b"", content_type="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def ws_url(self):
        return BASE["url"].replace("http://", "ws://", 1) + "/xmpp-websocket"

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/xmpp-websocket" and self.headers.get("Upgrade", "").lower() == "websocket":
            return self.websocket()
        if path == "/.well-known/host-meta.json":
            body = json.dumps({"links": [{"rel": "urn:xmpp:alt-connections:websocket", "href": self.ws_url()}]})
            return self.reply(200, body.encode())
        if path == "/.well-known/host-meta":
            body = ('<?xml version="1.0"?><XRD xmlns="http://docs.oasis-open.org/ns/xri/xrd-1.0">'
                    '<Link rel="urn:xmpp:alt-connections:websocket" href="%s"/></XRD>' % esc(self.ws_url()))
            return self.reply(200, body.encode(), "application/xrd+xml")
        with LOCK:
            upload = UPLOADS.get(path)
        if upload and upload["data"] is not None:
            return self.reply(200, upload["data"], upload["type"])
        self.reply(404, b"{}")

    def do_PUT(self):
        path = urllib.parse.urlparse(self.path).path
        length = int(self.headers.get("Content-Length") or "0")
        data = self.rfile.read(length)
        with LOCK:
            upload = UPLOADS.get(path)
            if not upload or upload["data"] is not None:
                return self.reply(404, b"{}")
            if self.headers.get("Authorization") != upload["auth"] or len(data) != upload["size"] or \
                    self.headers.get("Content-Type") != upload["type"]:
                return self.reply(403, b"{}")
            upload["data"] = data
        self.reply(201, b"{}")

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        length = int(self.headers.get("Content-Length") or "0")
        body = json.loads(self.rfile.read(length) or b"{}")
        with LOCK:
            if path == "/_test/drop":
                jid = body["user"] + "@" + DOMAIN
                for c in list(SESSIONS.get(jid, {}).values()):
                    c.drop()
            elif path == "/_test/mechanisms":
                MECHANISMS[:] = body["mechanisms"]
            elif path == "/_test/password":
                set_password(body["user"] + "@" + DOMAIN, body["password"])
            else:
                return self.reply(404, b"{}")
        self.reply(200, b"{}")

    def websocket(self):
        key = self.headers.get("Sec-WebSocket-Key", "")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        protocols = [p.strip() for p in self.headers.get("Sec-WebSocket-Protocol", "").split(",")]
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        if "xmpp" in protocols:
            self.send_header("Sec-WebSocket-Protocol", "xmpp")
        self.end_headers()
        self.wfile.flush()
        Conn(self.connection, self.rfile).loop()
        self.close_connection = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--users", default="alice,bob,carol")
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    for name in args.users.split(","):
        name = name.strip().lower()
        if name:
            set_password(name + "@" + DOMAIN, name + "-pass")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    BASE["url"] = "http://127.0.0.1:%d" % server.server_address[1]
    print(json.dumps({"url": BASE["url"]}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
