"""The relay end to end, through FastAPI's test client."""

from __future__ import annotations

import base64

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from fastapi.testclient import TestClient

from chatcube_relay.app import create_app
from chatcube_relay.config import Settings
from chatcube_relay.identity import login_message, register_message, user_id_for


class Person:
    def __init__(self, client: TestClient, name: str) -> None:
        self.client = client
        self.name = name
        self.key = Ed25519PrivateKey.generate()
        self.public = self.key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        self.public_b64 = base64.b64encode(self.public).decode()
        self.user_id = user_id_for(self.public)
        self.token = ""

    def sign(self, data: bytes) -> str:
        return base64.b64encode(self.key.sign(data)).decode()

    def register(self):
        return self.client.post("/v1/register", json={
            "public_key": self.public_b64, "display_name": self.name,
            "signature": self.sign(register_message(self.public_b64, self.name))})

    def login(self) -> None:
        nonce = self.client.post("/v1/login/challenge", json={"user_id": self.user_id}).json()["nonce"]
        r = self.client.post("/v1/login", json={"user_id": self.user_id, "nonce": nonce,
                                                "signature": self.sign(login_message(nonce))})
        assert r.status_code == 200, r.text
        self.token = r.json()["token"]

    @property
    def auth(self) -> dict[str, str]:
        return {"Authorization": "Bearer " + self.token}

    def join(self) -> "Person":
        assert self.register().status_code == 200
        self.login()
        return self


@pytest.fixture
def client(tmp_path):
    settings = Settings(database=str(tmp_path / "relay.sqlite3"),
                        stun_urls=["stun:stun.example.org:3478"],
                        turn_urls=["turn:turn.example.org:3478"], turn_secret="s3cret",
                        telegram_api_id=1017053, telegram_api_hash="0123456789abcdef0123456789abcdef")
    with TestClient(create_app(settings)) as c:
        yield c


def test_register_derives_the_user_id_and_needs_the_key(client):
    alice = Person(client, "Alice")
    r = alice.register()
    assert r.status_code == 200 and r.json()["user_id"] == alice.user_id
    assert alice.register().status_code == 200          # idempotent for the same key

    mallory = Person(client, "Mallory")
    bad = client.post("/v1/register", json={
        "public_key": alice.public_b64, "display_name": "Alice",
        "signature": mallory.sign(register_message(alice.public_b64, "Alice"))})
    assert bad.status_code == 400 and bad.json()["error"] == "invalid_signature"


def test_login_is_challenge_response_and_single_use(client):
    alice = Person(client, "Alice")
    alice.register()
    nonce = client.post("/v1/login/challenge", json={"user_id": alice.user_id}).json()["nonce"]
    other = Person(client, "Other")
    wrong = client.post("/v1/login", json={"user_id": alice.user_id, "nonce": nonce,
                                           "signature": other.sign(login_message(nonce))})
    assert wrong.status_code == 401
    # The nonce was spent by the failed attempt.
    replay = client.post("/v1/login", json={"user_id": alice.user_id, "nonce": nonce,
                                            "signature": alice.sign(login_message(nonce))})
    assert replay.status_code == 401
    alice.login()
    assert client.get("/v1/contacts", headers=alice.auth).status_code == 200
    assert client.get("/v1/contacts").status_code == 401
    assert client.post("/v1/logout", headers=alice.auth).status_code == 200
    assert client.get("/v1/contacts", headers=alice.auth).status_code == 401


def test_contacts_and_presence(client):
    alice = Person(client, "Alice").join()
    bob = Person(client, "Bob").join()
    r = client.put("/v1/contacts/" + bob.user_id, headers=alice.auth)
    assert r.status_code == 200 and r.json()["display_name"] == "Bob"
    assert r.json()["presence"] == "offline"
    assert client.put("/v1/contacts/" + alice.user_id, headers=alice.auth).status_code == 400
    assert client.put("/v1/contacts/" + "0" * 32, headers=alice.auth).status_code == 404

    with client.websocket_connect("/v1/ws", headers=alice.auth) as a:
        assert a.receive_json()["type"] == "hello"
        with client.websocket_connect("/v1/ws", headers=bob.auth) as b:
            assert b.receive_json()["type"] == "hello"
            presence = a.receive_json()
            assert presence == {"type": "presence", "from": bob.user_id, "status": "online"}
            contacts = client.get("/v1/contacts", headers=alice.auth).json()["contacts"]
            assert contacts[0]["presence"] == "online"
        assert a.receive_json() == {"type": "presence", "from": bob.user_id, "status": "offline"}


def test_recovery_email_and_phone_are_the_owners_alone(client):
    alice = Person(client, "Alice").join()
    bob = Person(client, "Bob").join()
    r = client.put("/v1/profile", headers=alice.auth,
                   json={"email": "alice@example.org", "phone": "+49 151 2345 6789"})
    assert r.status_code == 200, r.text
    assert r.json()["email"] == "alice@example.org" and r.json()["phone"] == "+49 151 2345 6789"
    assert r.json()["display_name"] == "Alice"          # left out: unchanged
    mine = client.get("/v1/profile", headers=alice.auth).json()
    assert mine["email"] == "alice@example.org" and mine["user_id"] == alice.user_id
    # Nobody else is told: not by id, not as a contact.
    seen = client.get("/v1/users/" + alice.user_id, headers=bob.auth).json()
    assert "email" not in seen and "phone" not in seen
    client.put("/v1/contacts/" + alice.user_id, headers=bob.auth)
    contact = client.get("/v1/contacts", headers=bob.auth).json()["contacts"][0]
    assert "email" not in contact and "phone" not in contact
    # A name alone leaves them; "" removes one.
    r = client.put("/v1/profile", headers=alice.auth, json={"display_name": "Alice B."})
    assert r.json()["email"] == "alice@example.org" and r.json()["display_name"] == "Alice B."
    r = client.put("/v1/profile", headers=alice.auth, json={"phone": ""})
    assert r.json()["phone"] == "" and r.json()["email"] == "alice@example.org"
    # What is plainly not one is refused.
    assert client.put("/v1/profile", headers=alice.auth, json={"email": "alice"}).status_code == 422
    assert client.put("/v1/profile", headers=alice.auth, json={"phone": "0151 2345"}).status_code == 422
    assert client.put("/v1/profile", headers=alice.auth, json={"phone": "+1 2"}).status_code == 422
    assert client.get("/v1/profile").status_code == 401


def test_offline_queue_is_store_and_forward(client):
    alice = Person(client, "Alice").join()
    bob = Person(client, "Bob").join()
    copy = {"to": bob.user_id, "message_id": "m-1", "conversation": alice.user_id, "envelope": "CIPHERTEXT"}
    r = client.post("/v1/messages", headers=alice.auth, json={"messages": [copy]})
    assert r.status_code == 200
    first_id = r.json()["accepted"][0]["id"]
    # A resend after a lost answer is not queued twice.
    again = client.post("/v1/messages", headers=alice.auth, json={"messages": [copy]})
    assert again.json()["accepted"][0]["id"] is None

    pending = client.get("/v1/messages", headers=bob.auth).json()["messages"]
    assert len(pending) == 1
    assert pending[0]["from"] == alice.user_id and pending[0]["envelope"] == "CIPHERTEXT"
    assert client.get("/v1/messages", headers=alice.auth).json()["messages"] == []
    assert client.post("/v1/messages/ack", headers=bob.auth, json={"ids": [first_id]}).json()["deleted"] == 1
    assert client.get("/v1/messages", headers=bob.auth).json()["messages"] == []

    big = dict(copy, message_id="m-2", envelope="x" * (64 * 1024 + 1))
    assert client.post("/v1/messages", headers=alice.auth, json={"messages": [big]}).status_code == 413


def test_a_message_to_an_online_device_is_pushed(client):
    alice = Person(client, "Alice").join()
    bob = Person(client, "Bob").join()
    with client.websocket_connect("/v1/ws", headers=bob.auth) as b:
        b.receive_json()
        client.post("/v1/messages", headers=alice.auth, json={"messages": [
            {"to": bob.user_id, "message_id": "m-9", "conversation": alice.user_id, "envelope": "E"}]})
        frame = b.receive_json()
        assert frame["type"] == "message.new"
        assert frame["message"]["message_id"] == "m-9" and frame["message"]["from"] == alice.user_id


def test_groups_fan_out_only_between_members(client):
    alice = Person(client, "Alice").join()
    bob = Person(client, "Bob").join()
    carol = Person(client, "Carol").join()
    eve = Person(client, "Eve").join()
    g = client.post("/v1/groups", headers=alice.auth,
                    json={"title": "Team", "members": [bob.user_id, carol.user_id]}).json()
    assert g["owner"] == alice.user_id and set(g["members"]) == {alice.user_id, bob.user_id, carol.user_id}
    conversation = g["conversation"]
    assert conversation == "group:" + g["group_id"]

    copies = [{"to": m, "message_id": "g-1", "conversation": conversation, "envelope": "E-" + m}
              for m in (bob.user_id, carol.user_id)]
    assert client.post("/v1/messages", headers=alice.auth, json={"messages": copies}).status_code == 200
    assert client.get("/v1/messages", headers=carol.auth).json()["messages"][0]["envelope"] == "E-" + carol.user_id

    # Not a member: neither sending into the group nor being sent to.
    assert client.post("/v1/messages", headers=eve.auth, json={"messages": [
        {"to": bob.user_id, "message_id": "x", "conversation": conversation, "envelope": "E"}]}).status_code == 403
    assert client.post("/v1/messages", headers=alice.auth, json={"messages": [
        {"to": eve.user_id, "message_id": "y", "conversation": conversation, "envelope": "E"}]}).status_code == 403
    assert client.get("/v1/groups/" + g["group_id"], headers=eve.auth).status_code == 404

    # Only the owner removes someone else; anyone may leave.
    assert client.delete("/v1/groups/%s/members/%s" % (g["group_id"], carol.user_id),
                         headers=bob.auth).status_code == 403
    assert client.delete("/v1/groups/%s/members/%s" % (g["group_id"], bob.user_id),
                         headers=bob.auth).status_code == 200
    assert bob.user_id not in client.get("/v1/groups/" + g["group_id"], headers=alice.auth).json()["members"]
    assert [x["group_id"] for x in client.get("/v1/groups", headers=carol.auth).json()["groups"]] == [g["group_id"]]


def test_signalling_is_forwarded_untouched(client):
    alice = Person(client, "Alice").join()
    bob = Person(client, "Bob").join()
    with client.websocket_connect("/v1/ws", headers=alice.auth) as a, \
            client.websocket_connect("/v1/ws", headers=bob.auth) as b:
        a.receive_json()
        b.receive_json()
        a.send_json({"type": "signal", "to": bob.user_id, "kind": "offer", "session": "s1",
                     "payload": {"sdp": "v=0...", "sig": "..."}})
        frame = b.receive_json()
        while frame["type"] == "presence":
            frame = b.receive_json()
        assert frame == {"type": "signal", "from": alice.user_id, "kind": "offer", "session": "s1",
                         "payload": {"sdp": "v=0...", "sig": "..."}}
        b.send_json({"type": "typing", "to": alice.user_id, "conversation": bob.user_id, "typing": True})
        frame = a.receive_json()
        while frame["type"] == "presence":
            frame = a.receive_json()
        assert frame["type"] == "typing" and frame["typing"] is True
        a.send_json({"type": "signal", "to": "0" * 32, "kind": "offer", "session": "s2", "payload": ""})
        assert a.receive_json() == {"type": "signal.undeliverable", "to": "0" * 32, "session": "s2"}
        a.send_json({"type": "signal", "to": bob.user_id, "kind": "rm -rf", "payload": ""})
        assert a.receive_json()["error"] == "unknown_frame"


def test_a_websocket_without_a_session_is_refused(client):
    from starlette.websockets import WebSocketDisconnect
    with pytest.raises(WebSocketDisconnect):
        with client.websocket_connect("/v1/ws") as ws:
            ws.receive_json()


def test_turn_credentials_are_short_lived_hmac(client):
    import hashlib
    import hmac
    alice = Person(client, "Alice").join()
    r = client.get("/v1/turn-credentials", headers=alice.auth).json()
    stun, turn = r["ice_servers"]
    assert stun == {"url": "stun:stun.example.org:3478"}
    assert turn["username"].endswith(":" + alice.user_id)
    expected = base64.b64encode(hmac.new(b"s3cret", turn["username"].encode(), hashlib.sha1).digest()).decode()
    assert turn["credential"] == expected and r["ttl"] > 0


def test_telegram_credentials_go_to_signed_in_users_only(client, tmp_path):
    alice = Person(client, "Alice").join()
    assert client.get("/v1/telegram-credentials").status_code == 401
    r = client.get("/v1/telegram-credentials", headers=alice.auth)
    assert r.status_code == 200
    assert r.json() == {"api_id": 1017053, "api_hash": "0123456789abcdef0123456789abcdef"}

    # A relay without them says so, and does not hand out half a pair.
    bare = Settings(database=str(tmp_path / "bare.sqlite3"), telegram_api_id=1017053)
    with TestClient(create_app(bare)) as c:
        bob = Person(c, "Bob").join()
        r = c.get("/v1/telegram-credentials", headers=bob.auth)
        assert r.status_code == 404 and r.json()["error"] == "not_configured"


def test_telegram_credentials_come_from_the_environment(monkeypatch):
    monkeypatch.setenv("CHATCUBE_RELAY_TELEGRAM_API_ID", "1017053")
    monkeypatch.setenv("CHATCUBE_RELAY_TELEGRAM_API_HASH", " 0123456789abcdef0123456789abcdef ")
    s = Settings.from_environment()
    assert s.telegram_api_id == 1017053 and s.telegram_api_hash == "0123456789abcdef0123456789abcdef"
    monkeypatch.setenv("CHATCUBE_RELAY_TELEGRAM_API_ID", "")
    assert Settings.from_environment().telegram_api_id == 0


# ---- confirming the e-mail and phone; account recovery ------------------------

import json
import os
import time as _time

from cryptography.hazmat.primitives.serialization import NoEncryption, PrivateFormat


@pytest.fixture
def recovering(tmp_path):
    """A relay that offers recovery, its e-mail and SMS going to an outbox file."""
    outbox = tmp_path / "outbox.jsonl"
    settings = Settings(database=str(tmp_path / "relay.sqlite3"), outbox=str(outbox),
                        recovery_key=base64.b64encode(os.urandom(32)).decode())
    with TestClient(create_app(settings)) as c:
        c.outbox = outbox
        yield c


def sent(client, channel: str, to: str, count: int = 1, timeout: float = 5.0) -> list[dict]:
    """The messages to `to` on `channel`, once at least `count` are there
    (they are sent on a worker thread after the answer)."""
    deadline = _time.time() + timeout
    while True:
        found = []
        if client.outbox.exists():
            for line in client.outbox.read_text().splitlines():
                entry = json.loads(line)
                if entry["channel"] == channel and entry["to"] == to:
                    found.append(entry)
        if len(found) >= count or _time.time() > deadline:
            return found
        _time.sleep(0.05)


def code_in(entry: dict) -> str:
    import re as _re
    return _re.search(r"\b(\d{6})\b", entry.get("subject", "") + " " + entry["text"]).group(1)


def secret_of(person: Person) -> str:
    seed = person.key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
    return base64.b64encode(seed + person.public).decode()


def verified(client, person: Person, email: str, phone: str) -> None:
    r = client.put("/v1/profile", headers=person.auth, json={"email": email, "phone": phone})
    assert r.status_code == 200 and sorted(r.json()["codes_sent"]) == ["email", "phone"], r.text
    assert not r.json()["email_verified"] and not r.json()["phone_verified"]
    for channel, to in (("email", email), ("phone", "+" + "".join(c for c in phone if c.isdigit()))):
        code = code_in(sent(client, channel, to)[-1])
        r = client.post("/v1/profile/verify", headers=person.auth, json={"channel": channel, "code": code})
        assert r.status_code == 200, r.text
    assert client.get("/v1/profile", headers=person.auth).json()["phone_verified"]


def test_an_address_is_verified_by_the_code_sent_to_it(recovering):
    erin = Person(recovering, "Erin").join()
    r = recovering.put("/v1/profile", headers=erin.auth, json={"email": "erin@example.org"})
    assert r.json()["codes_sent"] == ["email"] and not r.json()["email_verified"]
    assert r.json()["recovery_available"] and not r.json()["recovery_enabled"]
    code = code_in(sent(recovering, "email", "erin@example.org")[-1])
    wrong = "000000" if code != "000000" else "111111"
    assert recovering.post("/v1/profile/verify", headers=erin.auth,
                           json={"channel": "email", "code": wrong}).status_code == 400
    r = recovering.post("/v1/profile/verify", headers=erin.auth, json={"channel": "email", "code": code})
    assert r.status_code == 200 and r.json()["email_verified"]
    # A code is good once.
    assert recovering.post("/v1/profile/verify", headers=erin.auth,
                           json={"channel": "email", "code": code}).status_code == 400
    # Changing the address takes the verification away and sends a new code.
    r = recovering.put("/v1/profile", headers=erin.auth, json={"email": "erin@example.net"})
    assert not r.json()["email_verified"] and r.json()["codes_sent"] == ["email"]
    # The name alone sends nothing.
    assert recovering.put("/v1/profile", headers=erin.auth, json={"display_name": "Erin K."}).json()["codes_sent"] == []
    # Five wrong codes spend it.
    for _ in range(5):
        recovering.post("/v1/profile/verify", headers=erin.auth, json={"channel": "email", "code": wrong})
    right = code_in(sent(recovering, "email", "erin@example.net")[-1])
    assert recovering.post("/v1/profile/verify", headers=erin.auth,
                           json={"channel": "email", "code": right}).status_code == 400
    # A new one comes on asking; only so many an hour.
    assert recovering.post("/v1/profile/verify/resend", headers=erin.auth,
                           json={"channel": "email"}).json()["codes_sent"] == ["email"]
    for _ in range(5):
        r = recovering.post("/v1/profile/verify/resend", headers=erin.auth, json={"channel": "email"})
    assert r.status_code == 429


def test_recovery_needs_both_codes_and_hands_the_key_back(recovering):
    erin = Person(recovering, "Erin").join()
    # Not before both are confirmed, and only Erin's own key.
    assert recovering.put("/v1/recovery/escrow", headers=erin.auth,
                          json={"secret_key": secret_of(erin)}).status_code == 409
    verified(recovering, erin, "erin@example.org", "+49 151 2345 6789")
    mallory = Person(recovering, "Mallory").join()
    assert recovering.put("/v1/recovery/escrow", headers=erin.auth,
                          json={"secret_key": secret_of(mallory)}).status_code == 400
    r = recovering.put("/v1/recovery/escrow", headers=erin.auth, json={"secret_key": secret_of(erin)})
    assert r.status_code == 200 and r.json()["recovery_enabled"]
    # The copy at rest is not the key.
    raw = recovering.app.state.store._db.execute("SELECT sealed FROM escrow").fetchone()[0]
    assert base64.b64decode(secret_of(erin)) not in bytes(raw)

    # On another computer: by the phone number, written another way.
    start = recovering.post("/v1/recovery/start", json={"address": "+49 (151) 23456789"})
    assert start.status_code == 200, start.text
    rid = start.json()["recovery_id"]
    email_code = code_in(sent(recovering, "email", "erin@example.org", 2)[-1])
    phone_code = code_in(sent(recovering, "phone", "+4915123456789", 2)[-1])
    # One code is not enough.
    bad = "000000" if phone_code != "000000" else "111111"
    assert recovering.post("/v1/recovery/finish", json={"recovery_id": rid, "email_code": email_code,
                                                         "phone_code": bad}).status_code == 401
    r = recovering.post("/v1/recovery/finish", json={"recovery_id": rid, "email_code": email_code,
                                                      "phone_code": phone_code})
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["user_id"] == erin.user_id and body["secret_key"] == secret_of(erin)
    assert body["email"] == "erin@example.org" and body["display_name"] == "Erin"
    # The owner is told, on both.
    assert any("recovered" in e["subject"] for e in sent(recovering, "email", "erin@example.org", 3))
    assert any("recovered" in e["text"] for e in sent(recovering, "phone", "+4915123456789", 3))
    # And the recovery is spent.
    assert recovering.post("/v1/recovery/finish", json={"recovery_id": rid, "email_code": email_code,
                                                         "phone_code": phone_code}).status_code == 401


def test_recovery_does_not_tell_whether_an_address_is_known(recovering):
    erin = Person(recovering, "Erin").join()
    verified(recovering, erin, "erin@example.org", "+4915123456789")
    recovering.put("/v1/recovery/escrow", headers=erin.auth, json={"secret_key": secret_of(erin)})
    known = recovering.post("/v1/recovery/start", json={"address": "Erin@Example.org"})
    unknown = recovering.post("/v1/recovery/start", json={"address": "nobody@example.org"})
    assert known.status_code == unknown.status_code == 200
    assert set(known.json()) == set(unknown.json()) and known.json()["channels"] == unknown.json()["channels"]
    # Nothing goes to an address nobody has, and no codes finish it.
    assert sent(recovering, "email", "nobody@example.org", timeout=0.5) == []
    for _ in range(6):
        r = recovering.post("/v1/recovery/finish", json={"recovery_id": unknown.json()["recovery_id"],
                                                          "email_code": "123456", "phone_code": "123456"})
        assert r.status_code == 401
    # Five wrong pairs spend a real one too.
    rid = known.json()["recovery_id"]
    email_code = code_in(sent(recovering, "email", "erin@example.org", 2)[-1])
    phone_code = code_in(sent(recovering, "phone", "+4915123456789", 2)[-1])
    for _ in range(5):
        recovering.post("/v1/recovery/finish", json={"recovery_id": rid, "email_code": "999999",
                                                      "phone_code": "999999"})
    assert recovering.post("/v1/recovery/finish", json={"recovery_id": rid, "email_code": email_code,
                                                         "phone_code": phone_code}).status_code == 401


def test_recovery_turned_off_or_not_offered(recovering, client):
    erin = Person(recovering, "Erin").join()
    verified(recovering, erin, "erin@example.org", "+4915123456789")
    recovering.put("/v1/recovery/escrow", headers=erin.auth, json={"secret_key": secret_of(erin)})
    r = recovering.delete("/v1/recovery/escrow", headers=erin.auth)
    assert r.status_code == 200 and not r.json()["recovery_enabled"]
    rid = recovering.post("/v1/recovery/start", json={"address": "erin@example.org"}).json()["recovery_id"]
    # Recovery is off: no codes go out - the one e-mail is still the confirmation.
    assert len(sent(recovering, "email", "erin@example.org", 2, timeout=0.5)) == 1
    assert recovering.post("/v1/recovery/finish", json={"recovery_id": rid, "email_code": "1",
                                                         "phone_code": "1"}).status_code == 401
    # A relay without a recovery key and senders offers none.
    bob = Person(client, "Bob").join()
    assert client.get("/v1/profile", headers=bob.auth).json()["recovery_available"] is False
    assert client.post("/v1/recovery/start", json={"address": "bob@example.org"}).status_code == 503
