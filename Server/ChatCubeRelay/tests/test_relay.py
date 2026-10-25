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
