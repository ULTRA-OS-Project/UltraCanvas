#!/usr/bin/env python3
"""A small IRC server with IRCv3, for IrcProviderTests: three listeners on
127.0.0.1 -

  plain  IRC over TCP with the IRCv3 capabilities below,
  tls    the same over TLS (--cert / --key: the certificate the test made),
  basic  IRC over TCP without IRCv3 at all (CAP is an unknown command; no
         tags, no echo, no history) - an old server.

What it speaks: CAP LS 302 / REQ / END (sasl=PLAIN, server-time,
message-tags, echo-message, batch, away-notify, account-tag, multi-prefix,
labeled-response, draft/chathistory, draft/event-playback), SASL PLAIN
(--accounts alice:alice-pass,bob:bob-pass), NICK (433 when in use) / USER,
001-005 (CASEMAPPING=rfc1459, PREFIX, NETWORK, MONITOR, CHATHISTORY), PING /
PONG, JOIN / PART / NAMES / TOPIC / KICK / INVITE / MODE (+i, +k), PRIVMSG /
NOTICE / TAGMSG with tags (time, msgid, account, client tags such as
+draft/reply and +typing), echo-message with labeled-response, WHOIS (311 /
318, 401), MONITOR (730 / 731), AWAY with away-notify, CHATHISTORY LATEST /
BEFORE (msgid= or timestamp=) in a chathistory batch, QUIT. Lines longer than
512 bytes (without tags) are cut, as real servers cut them.

For the tests: FAKEDROP <nick> (from any connection, even one that has not
registered) drops that nick's connection at once, as a network failure would.
--time-offset <seconds> moves the server's clock (server-time) away from the
real one, so a test can tell the server's time from the moment it read a line.

Standard library only. Prints {"plain": port, "tls": port, "basic": port}
once it listens; runs until it is killed.
"""

import argparse
import asyncio
import base64
import calendar
import itertools
import json
import ssl
import sys
import time

SERVER = "irc.fake.test"
NETWORK = "FakeNet"
CAPS = ["sasl=PLAIN", "server-time", "message-tags", "echo-message", "batch", "away-notify", "account-tag",
        "multi-prefix", "labeled-response", "draft/chathistory", "draft/event-playback"]
ACCOUNTS = {}
CLIENTS = {}        # folded nick -> Client (registered or not, once it has a nick)
CHANNELS = {}       # folded name -> Channel
PRIVATE = {}        # frozenset of two folded nicks -> [entry]
IDS = itertools.count(1)
TIME_OFFSET = 0.0


def fold(name):
    out = []
    for ch in name:
        if "A" <= ch <= "Z":
            out.append(ch.lower())
        elif ch == "[":
            out.append("{")
        elif ch == "]":
            out.append("}")
        elif ch == "\\":
            out.append("|")
        elif ch == "~":
            out.append("^")
        else:
            out.append(ch)
    return "".join(out)


def now_ms():
    return int((time.time() + TIME_OFFSET) * 1000)


def server_time(ms):
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(ms / 1000)) + ".%03dZ" % (ms % 1000)


def parse_time(text):
    try:
        base, _, frac = text.rstrip("Z").partition(".")
        seconds = calendar.timegm(time.strptime(base, "%Y-%m-%dT%H:%M:%S"))
        return int(seconds * 1000) + int((frac + "000")[:3] or 0)
    except ValueError:
        return 0


def escape(value):
    return (value.replace("\\", "\\\\").replace(";", "\\:").replace(" ", "\\s")
            .replace("\r", "\\r").replace("\n", "\\n"))


def unescape(value):
    out, i = [], 0
    while i < len(value):
        ch = value[i]
        if ch == "\\" and i + 1 < len(value):
            nxt = value[i + 1]
            out.append({":": ";", "s": " ", "r": "\r", "n": "\n"}.get(nxt, nxt))
            i += 2
            continue
        if ch != "\\":
            out.append(ch)
        i += 1
    return "".join(out)


def parse(line):
    tags = {}
    if line.startswith("@"):
        raw, _, line = line[1:].partition(" ")
        for item in raw.split(";"):
            if not item:
                continue
            key, _, value = item.partition("=")
            tags[key] = unescape(value)
        line = line.lstrip(" ")
    if line.startswith(":"):
        _, _, line = line.partition(" ")
        line = line.lstrip(" ")
    params = []
    while line:
        if line.startswith(":"):
            params.append(line[1:])
            break
        part, _, line = line.partition(" ")
        line = line.lstrip(" ")
        if part:
            params.append(part)
    if not params:
        return None, None, None
    return tags, params[0].upper(), params[1:]


class Channel:
    def __init__(self, name):
        self.name = name
        self.members = {}       # Client -> modes ("@", "+", "@+")
        self.topic = ""
        self.invite_only = False
        self.key = ""
        self.invited = set()
        self.history = []


class Client:
    def __init__(self, reader, writer, basic):
        self.reader = reader
        self.writer = writer
        self.basic = basic
        self.nick = None
        self.user = None
        self.realname = ""
        self.host = "fake/user"
        self.caps = set()
        self.negotiating = False
        self.registered = False
        self.account = None
        self.sasl = False
        self.monitor = set()
        self.away = None
        self.gone = False

    def prefix(self):
        return "%s!%s@%s" % (self.nick, self.user or "user", self.host)

    def target(self):
        return self.nick or "*"

    def send(self, line, tags=None):
        if self.gone:
            return
        out = {}
        tags = dict(tags or {})
        if "time" not in tags and "batch" not in tags:
            tags["time"] = server_time(now_ms())       # every line, as IRCv3 servers do
        for key, value in tags.items():
            if value is None:
                continue
            if key.startswith("+"):
                if "message-tags" in self.caps:
                    out[key] = value
            elif key == "time":
                if "server-time" in self.caps:
                    out[key] = value
            elif key == "msgid":
                if "message-tags" in self.caps:
                    out[key] = value
            elif key == "account":
                if "account-tag" in self.caps:
                    out[key] = value
            elif key == "label":
                if "labeled-response" in self.caps:
                    out[key] = value
            elif key == "batch":
                if "batch" in self.caps:
                    out[key] = value
        data = line.encode("utf-8", "surrogateescape")[:510]
        if out:
            data = ("@" + ";".join(k if v == "" else "%s=%s" % (k, escape(v)) for k, v in out.items()) +
                    " ").encode("utf-8", "surrogateescape") + data
        try:
            self.writer.write(data + b"\r\n")
        except Exception:
            pass

    def numeric(self, code, *params):
        parts = [":%s %s %s" % (SERVER, code, self.target())]
        for i, p in enumerate(params):
            parts.append((":" + p) if i == len(params) - 1 and (" " in p or not p or p.startswith(":")) else p)
        self.send(" ".join(parts))


def history_line(entry, batch):
    tags = dict(entry["tags"])
    tags["time"] = server_time(entry["time"])
    tags["msgid"] = entry["msgid"]
    tags["batch"] = batch
    return ":%s %s %s :%s" % (entry["prefix"], entry["command"], entry["target"], entry["text"]), tags


def names_of(channel, client):
    names = []
    for member, modes in channel.members.items():
        shown = modes if "multi-prefix" in client.caps else modes[:1]
        names.append(shown + member.nick)
    return " ".join(names)


def channels_of(client):
    return [ch for ch in CHANNELS.values() if client in ch.members]


def peers_of(client):
    peers = set()
    for ch in channels_of(client):
        peers.update(ch.members)
    peers.discard(client)
    return peers


def notify_monitors(client, online):
    if not client.nick:
        return
    folded = fold(client.nick)
    for other in list(CLIENTS.values()):
        if other is not client and folded in other.monitor and other.registered:
            if online:
                other.numeric("730", client.prefix())
            else:
                other.numeric("731", client.nick)


def welcome(c):
    c.registered = True
    c.numeric("001", "Welcome to %s %s" % (NETWORK, c.prefix()))
    c.numeric("002", "Your host is %s" % SERVER)
    c.numeric("003", "This server was created today")
    c.numeric("004", SERVER, "fake-1.0", "o", "ikov")
    tokens = ["CASEMAPPING=rfc1459", "CHANTYPES=#", "PREFIX=(ov)@+", "NETWORK=" + NETWORK]
    if not c.basic:
        tokens += ["MONITOR=100", "CHATHISTORY=100"]
    c.numeric("005", *(tokens + ["are supported by this server"]))
    c.numeric("422", "MOTD File is missing")
    notify_monitors(c, True)


def try_register(c):
    if not c.registered and c.nick and c.user and not c.negotiating:
        welcome(c)


def leave_everything(c, reason):
    if c.gone:
        return
    c.gone = True
    for peer in peers_of(c):
        peer.send(":%s QUIT :%s" % (c.prefix(), reason))
    for ch in channels_of(c):
        del ch.members[c]
    if c.nick and CLIENTS.get(fold(c.nick)) is c:
        del CLIENTS[fold(c.nick)]
        if c.registered:
            notify_monitors(c, False)


def deliver(c, command, params, tags):
    if not params or (command != "TAGMSG" and len(params) < 2):
        c.numeric("411", "No recipient given (%s)" % command)
        return
    target, text = params[0], (params[1] if len(params) > 1 else "")
    client_tags = {k: v for k, v in tags.items() if k.startswith("+")} if "message-tags" in c.caps else {}
    label = tags.get("label") if "labeled-response" in c.caps else None
    ms = now_ms()
    msgid = "fake%06d" % next(IDS)
    stags = dict(client_tags)
    stags.update({"time": server_time(ms), "msgid": msgid, "account": c.account})
    line = ":%s %s %s" % (c.prefix(), command, target)
    if command != "TAGMSG":
        line += " :" + text
    entry = {"time": ms, "msgid": msgid, "prefix": c.prefix(), "command": command, "target": target, "text": text,
             "tags": client_tags}
    if target.startswith("#"):
        ch = CHANNELS.get(fold(target))
        if ch is None:
            c.numeric("403", target, "No such channel")
            return
        if c not in ch.members:
            c.numeric("404", target, "Cannot send to channel")
            return
        for member in ch.members:
            if member is c:
                continue
            if command == "TAGMSG" and "message-tags" not in member.caps:
                continue
            member.send(line, stags)
        if command != "TAGMSG":
            ch.history.append(entry)
    else:
        dest = CLIENTS.get(fold(target))
        if dest is None or not dest.registered:
            c.numeric("401", target, "No such nick/channel")
            return
        if not (command == "TAGMSG" and "message-tags" not in dest.caps):
            dest.send(line, stags)
        if command != "TAGMSG":
            PRIVATE.setdefault(frozenset((fold(c.nick), fold(target))), []).append(entry)
        if dest.away and command == "PRIVMSG":
            c.numeric("301", dest.nick, dest.away)
    if "echo-message" in c.caps and not (command == "TAGMSG" and "message-tags" not in c.caps):
        echo = dict(stags)
        if label:
            echo["label"] = label
        c.send(line, echo)
    elif label:
        c.send(":%s ACK" % SERVER, {"label": label})


def chathistory(c, params, tags):
    label = tags.get("label") if "labeled-response" in c.caps else None
    if "draft/chathistory" not in c.caps or len(params) < 4:
        c.send(":%s FAIL CHATHISTORY INVALID_PARAMS %s :Not now" % (SERVER, params[0] if params else "*"))
        return
    sub, target, ref, limit = params[0].upper(), params[1], params[2], params[3]
    try:
        limit = max(0, min(100, int(limit)))
    except ValueError:
        limit = 50
    if target.startswith("#"):
        ch = CHANNELS.get(fold(target))
        if ch is None or c not in ch.members:
            c.send(":%s FAIL CHATHISTORY INVALID_TARGET %s %s :No such channel" % (SERVER, sub, target))
            return
        entries = ch.history
    else:
        entries = PRIVATE.get(frozenset((fold(c.nick), fold(target))), [])

    def index_of(reference):
        kind, _, value = reference.partition("=")
        if kind == "msgid":
            for i, e in enumerate(entries):
                if e["msgid"] == value:
                    return i
            return None
        if kind == "timestamp":
            ms = parse_time(value)
            for i, e in enumerate(entries):
                if e["time"] >= ms:
                    return i
            return len(entries)
        return None

    if sub == "LATEST":
        start = 0
        if ref != "*":
            at = index_of(ref)
            start = (at + 1) if at is not None else 0
        chosen = entries[start:][-limit:] if limit else []
    elif sub == "BEFORE":
        at = index_of(ref)
        if at is None:
            at = 0
        chosen = entries[max(0, at - limit):at]
    else:
        c.send(":%s FAIL CHATHISTORY INVALID_PARAMS %s :Unknown subcommand" % (SERVER, sub))
        return
    ref_id = "h%d" % next(IDS)
    c.send(":%s BATCH +%s chathistory %s" % (SERVER, ref_id, target), {"label": label} if label else None)
    for entry in chosen:
        line, etags = history_line(entry, ref_id)
        c.send(line, etags)
    c.send(":%s BATCH -%s" % (SERVER, ref_id))


def join(c, name, key):
    if not name.startswith("#") or len(name) < 2 or "," in name:
        c.numeric("403", name, "No such channel")
        return
    folded = fold(name)
    ch = CHANNELS.get(folded)
    if ch is None:
        ch = CHANNELS[folded] = Channel(name)
    if c in ch.members:
        return
    if ch.invite_only and fold(c.nick) not in ch.invited:
        c.numeric("473", ch.name, "Cannot join channel (+i)")
        return
    if ch.key and key != ch.key:
        c.numeric("475", ch.name, "Cannot join channel (+k)")
        return
    ch.invited.discard(fold(c.nick))
    ch.members[c] = "@" if not ch.members else ""
    for member in ch.members:
        member.send(":%s JOIN %s" % (c.prefix(), ch.name))
    if ch.topic:
        c.numeric("332", ch.name, ch.topic)
    c.numeric("353", "=", ch.name, names_of(ch, c))
    c.numeric("366", ch.name, "End of /NAMES list")


def handle(c, raw):
    tags, command, params = parse(raw)
    if command is None:
        return
    p = lambda i: params[i] if i < len(params) else ""
    if command == "FAKEDROP":
        victim = CLIENTS.get(fold(p(0)))
        if victim:
            leave_everything(victim, "Connection reset by peer")
            victim.writer.transport.abort()
        return
    if command == "CAP":
        if c.basic:
            c.numeric("421", "CAP", "Unknown command")
            return
        sub = p(0).upper()
        if sub == "LS":
            if not c.registered:
                c.negotiating = True
            c.send(":%s CAP %s LS :%s" % (SERVER, c.target(), " ".join(CAPS)))
        elif sub == "REQ":
            wanted = p(1).split()
            known = {cap.split("=")[0] for cap in CAPS}
            if not c.registered:
                c.negotiating = True
            if all(w.lstrip("-") in known for w in wanted):
                for w in wanted:
                    if w.startswith("-"):
                        c.caps.discard(w[1:])
                    else:
                        c.caps.add(w)
                c.send(":%s CAP %s ACK :%s" % (SERVER, c.target(), " ".join(wanted)))
            else:
                c.send(":%s CAP %s NAK :%s" % (SERVER, c.target(), " ".join(wanted)))
        elif sub == "END":
            c.negotiating = False
            try_register(c)
        return
    if command == "AUTHENTICATE":
        if "sasl" not in c.caps:
            return
        if p(0).upper() == "PLAIN":
            c.sasl = True
            c.send("AUTHENTICATE +")
        elif p(0) == "*":
            c.sasl = False
            c.numeric("906", "SASL authentication aborted")
        elif c.sasl:
            c.sasl = False
            try:
                _authz, authc, password = base64.b64decode(p(0)).decode("utf-8").split("\0")
            except Exception:
                c.numeric("904", "SASL authentication failed")
                return
            if ACCOUNTS.get(authc) == password:
                c.account = authc
                c.numeric("900", c.prefix() if c.nick else "*", authc, "You are now logged in as " + authc)
                c.numeric("903", "SASL authentication successful")
            else:
                c.numeric("904", "SASL authentication failed")
        return
    if command == "NICK":
        new = p(0)
        if not new:
            c.numeric("431", "No nickname given")
            return
        holder = CLIENTS.get(fold(new))
        if holder is not None and holder is not c:
            c.numeric("433", new, "Nickname is already in use")
            return
        old = c.nick
        if old and CLIENTS.get(fold(old)) is c:
            del CLIENTS[fold(old)]
        CLIENTS[fold(new)] = c
        if c.registered:
            line = ":%s!%s@%s NICK %s" % (old, c.user, c.host, new)
            c.send(line)
            for peer in peers_of(c):
                peer.send(line)
        c.nick = new
        try_register(c)
        return
    if command == "USER":
        c.user = p(0)[:10] or "user"
        c.realname = p(3)
        try_register(c)
        return
    if command == "PING":
        c.send(":%s PONG %s :%s" % (SERVER, SERVER, p(0)))
        return
    if command == "PONG":
        return
    if command == "QUIT":
        leave_everything(c, "Quit: " + p(0))
        c.writer.close()
        return
    if not c.registered:
        c.numeric("451", "You have not registered")
        return

    if command == "JOIN":
        keys = p(1).split(",") if p(1) else []
        for i, name in enumerate(p(0).split(",")):
            join(c, name, keys[i] if i < len(keys) else "")
    elif command == "PART":
        for name in p(0).split(","):
            ch = CHANNELS.get(fold(name))
            if ch is None or c not in ch.members:
                c.numeric("442", name, "You're not on that channel")
                continue
            for member in ch.members:
                member.send(":%s PART %s :%s" % (c.prefix(), ch.name, p(1)))
            del ch.members[c]
    elif command == "NAMES":
        ch = CHANNELS.get(fold(p(0)))
        if ch:
            c.numeric("353", "=", ch.name, names_of(ch, c))
        c.numeric("366", p(0), "End of /NAMES list")
    elif command == "TOPIC":
        ch = CHANNELS.get(fold(p(0)))
        if ch is None or c not in ch.members:
            c.numeric("442", p(0), "You're not on that channel")
        elif len(params) >= 2:
            ch.topic = p(1)
            for member in ch.members:
                member.send(":%s TOPIC %s :%s" % (c.prefix(), ch.name, ch.topic))
        elif ch.topic:
            c.numeric("332", ch.name, ch.topic)
        else:
            c.numeric("331", ch.name, "No topic is set")
    elif command == "KICK":
        ch = CHANNELS.get(fold(p(0)))
        victim = CLIENTS.get(fold(p(1)))
        if ch is None or c not in ch.members:
            c.numeric("442", p(0), "You're not on that channel")
        elif "@" not in ch.members[c]:
            c.numeric("482", ch.name, "You're not channel operator")
        elif victim is None or victim not in ch.members:
            c.numeric("441", p(1), ch.name, "They aren't on that channel")
        else:
            for member in ch.members:
                member.send(":%s KICK %s %s :%s" % (c.prefix(), ch.name, victim.nick, p(2) or c.nick))
            del ch.members[victim]
    elif command == "INVITE":
        victim = CLIENTS.get(fold(p(0)))
        ch = CHANNELS.get(fold(p(1)))
        if victim is None:
            c.numeric("401", p(0), "No such nick/channel")
        elif ch is None or c not in ch.members:
            c.numeric("442", p(1), "You're not on that channel")
        else:
            ch.invited.add(fold(victim.nick))
            c.numeric("341", victim.nick, ch.name)
            victim.send(":%s INVITE %s %s" % (c.prefix(), victim.nick, ch.name))
    elif command == "MODE":
        ch = CHANNELS.get(fold(p(0)))
        if ch is None:
            return
        if len(params) < 2:
            c.numeric("324", ch.name, "+" + ("i" if ch.invite_only else "") + ("k" if ch.key else ""))
            return
        if "@" not in ch.members.get(c, ""):
            c.numeric("482", ch.name, "You're not channel operator")
            return
        modes = p(1)
        sign = "+"
        for m in modes:
            if m in "+-":
                sign = m
            elif m == "i":
                ch.invite_only = sign == "+"
            elif m == "k":
                ch.key = p(2) if sign == "+" else ""
        for member in ch.members:
            member.send(":%s MODE %s %s" % (c.prefix(), ch.name, " ".join(params[1:])))
    elif command in ("PRIVMSG", "NOTICE", "TAGMSG"):
        deliver(c, command, params, tags)
    elif command == "WHOIS":
        nick = p(len(params) - 1) if params else ""
        victim = CLIENTS.get(fold(nick))
        if victim is None or not victim.registered:
            c.numeric("401", nick, "No such nick/channel")
        else:
            c.numeric("311", victim.nick, victim.user, victim.host, "*", victim.realname)
        c.numeric("318", nick, "End of /WHOIS list")
    elif command == "MONITOR":
        sub = p(0)
        if sub == "+":
            online, offline = [], []
            for nick in p(1).split(","):
                if not nick:
                    continue
                c.monitor.add(fold(nick))
                other = CLIENTS.get(fold(nick))
                (online if other and other.registered else offline).append(other.prefix() if other and other.registered
                                                                            else nick)
            if online:
                c.numeric("730", ",".join(online))
            if offline:
                c.numeric("731", ",".join(offline))
        elif sub == "-":
            for nick in p(1).split(","):
                c.monitor.discard(fold(nick))
        elif sub == "C":
            c.monitor.clear()
    elif command == "AWAY":
        c.away = p(0) or None
        c.numeric("306" if c.away else "305", "You are away" if c.away else "You are back")
        line = ":%s AWAY" % c.prefix() + (" :" + c.away if c.away else "")
        for peer in peers_of(c):
            if "away-notify" in peer.caps:
                peer.send(line)
    elif command == "CHATHISTORY":
        chathistory(c, params, tags)
    else:
        c.numeric("421", command, "Unknown command")


async def serve(reader, writer, basic):
    c = Client(reader, writer, basic)
    c.send(":%s NOTICE * :*** Looking up your hostname..." % SERVER)
    try:
        while not c.gone:
            raw = await reader.readline()
            if not raw:
                break
            line = raw.rstrip(b"\r\n").decode("utf-8", "surrogateescape")
            if line:
                handle(c, line)
                try:
                    await writer.drain()
                except Exception:
                    pass
            for other in list(CLIENTS.values()):
                if other is not c and not other.gone:
                    try:
                        await other.writer.drain()
                    except Exception:
                        pass
    except (ConnectionError, ssl.SSLError, asyncio.IncompleteReadError):
        pass
    finally:
        leave_everything(c, "Connection closed")
        try:
            writer.close()
        except Exception:
            pass


async def main():
    global TIME_OFFSET
    parser = argparse.ArgumentParser()
    parser.add_argument("--accounts", default="alice:alice-pass,bob:bob-pass")
    parser.add_argument("--cert", default="")
    parser.add_argument("--key", default="")
    parser.add_argument("--time-offset", type=float, default=0.0)
    args = parser.parse_args()
    TIME_OFFSET = args.time_offset
    for pair in args.accounts.split(","):
        name, _, password = pair.partition(":")
        if name:
            ACCOUNTS[name] = password
    plain = await asyncio.start_server(lambda r, w: serve(r, w, False), "127.0.0.1", 0)
    basic = await asyncio.start_server(lambda r, w: serve(r, w, True), "127.0.0.1", 0)
    ports = {"plain": plain.sockets[0].getsockname()[1], "basic": basic.sockets[0].getsockname()[1]}
    servers = [plain, basic]
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.key)
        tls = await asyncio.start_server(lambda r, w: serve(r, w, False), "127.0.0.1", 0, ssl=context)
        ports["tls"] = tls.sockets[0].getsockname()[1]
        servers.append(tls)
    print(json.dumps(ports), flush=True)
    await asyncio.gather(*(s.serve_forever() for s in servers))


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
