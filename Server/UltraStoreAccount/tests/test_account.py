"""The account service end to end, through FastAPI's test client, on SQLite
and on whatever ULTRASTORE_ACCOUNT_TEST_PG_URL and
ULTRASTORE_ACCOUNT_TEST_MYSQL_URL name: an account created and verified by
the mailed code, signed in on two machines, two-factor turned on and asked
for, a licence attached and its seats counted, the notices, the data
archive released by the operator and downloaded once, the deletion with
its grace, the operator's list, file and actions, and the audit chain."""

from __future__ import annotations

import base64
import json
import os

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from fastapi.testclient import TestClient

from ultrastore_account.app import create_app
from ultrastore_account.auth import hash_password, password_problems, totp_code, verify_password, verify_totp
from ultrastore_account.config import Settings
from ultrastore_account.store import AccountStore, add_days, today

OP = {"Authorization": "Bearer op-token"}
TABLES = ["audit", "archives", "notices", "installs", "licences", "signins", "sessions", "codes", "users", "meta"]
PASSWORD = "correct horse battery staple 7"


def database_urls() -> list[str]:
    urls = ["sqlite"]
    if os.environ.get("ULTRASTORE_ACCOUNT_TEST_PG_URL"):
        urls.append("postgresql")
    if os.environ.get("ULTRASTORE_ACCOUNT_TEST_MYSQL_URL"):
        urls.append("mysql")
    return urls


def database_url(kind: str, tmp_path) -> str:
    if kind == "sqlite":
        return "sqlite:///" + str(tmp_path / "account.sqlite3")
    url = os.environ["ULTRASTORE_ACCOUNT_TEST_PG_URL" if kind == "postgresql" else "ULTRASTORE_ACCOUNT_TEST_MYSQL_URL"]
    from ultrastore_db import Database
    db = Database(url)
    db.script([f"DROP TABLE IF EXISTS {t}" for t in TABLES])
    db.close()
    return url


class Outbox:
    """The mails the service sent, and the last code in each."""

    def __init__(self) -> None:
        self.mails: list[tuple[str, str, str]] = []

    def __call__(self, to: str, subject: str, text: str) -> None:
        self.mails.append((to, subject, text))

    def code(self, to: str) -> str:
        for mail_to, _subject, text in reversed(self.mails):
            if mail_to == to and "Your code:" in text:
                return text.split("Your code:")[1].split()[0]
        raise AssertionError(f"no code mailed to {to}")


class LicenceIssuer:
    """The payment service's licence key, as far as a test needs it."""

    def __init__(self) -> None:
        self.key = Ed25519PrivateKey.generate()
        self.public_hex = self.key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()

    def issue(self, licence_id: str, app: str, **more) -> str:
        payload = {"v": 1, "licence": licence_id, "app": app, "order": "o-1", "issued": today(), "updates": "always", **more}
        body = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode()
        b64 = lambda b: base64.urlsafe_b64encode(b).decode().rstrip("=")   # noqa: E731
        return "ULIC1." + b64(body) + "." + b64(self.key.sign(body))


@pytest.fixture(params=database_urls())
def service(request, tmp_path):
    issuer = LicenceIssuer()
    outbox = Outbox()
    settings = Settings(database=database_url(request.param, tmp_path), operator_token="op-token", licence_key=issuer.public_hex, default_seats=2)
    with TestClient(create_app(settings, mailer=outbox)) as client:
        client.outbox = outbox          # type: ignore[attr-defined]
        client.issuer = issuer          # type: ignore[attr-defined]
        yield client


def signup(client: TestClient, email: str = "anna@example.org", device: str = "Anna's laptop") -> dict:
    r = client.post("/v1/signup", json={"email": email, "password": PASSWORD, "country": "de", "year_of_birth": 1990, "language": "de"})
    assert r.status_code == 200, r.text
    assert r.json()["verify"] == "email_code"
    code = client.outbox.code(email)   # type: ignore[attr-defined]
    r = client.post("/v1/signup/verify", json={"email": email, "code": code, "device": {"name": device, "os": "ULTRA OS 0.10", "client": "UltraStore 0.2"}})
    assert r.status_code == 200, r.text
    return r.json()


def bearer(token: str) -> dict:
    return {"Authorization": "Bearer " + token}


def signin(client: TestClient, email: str = "anna@example.org", device: str = "Anna's phone", **more) -> dict:
    r = client.post("/v1/signin", json={"email": email, "password": PASSWORD, "device": {"name": device, "os": "Android 15"}, **more})
    assert r.status_code == 200, r.text
    return r.json()


# ----- the primitives -----
def test_passwords_and_codes():
    stored = hash_password(PASSWORD)
    assert stored.startswith("scrypt$") and verify_password(PASSWORD, stored) and not verify_password("wrong", stored)
    assert password_problems("short") and password_problems("abcdefghijklmnop") and not password_problems(PASSWORD)
    secret = "JBSWY3DPEHPK3PXP"
    assert verify_totp(secret, totp_code(secret, 1_700_000_000), at=1_700_000_010)
    assert not verify_totp(secret, totp_code(secret, 1_700_000_000), at=1_700_000_200)


# ----- sign up, sign in -----
def test_signup_verify_and_signin(service):
    first = signup(service)
    assert first["token"].startswith("us_") and first["user"]["email"] == "anna@example.org" and first["user"]["email_verified"]
    assert first["user"]["country"] == "DE" and first["device"]["device_name"] == "Anna's laptop"
    # The same address again: the same answer, a mail to the owner, no second account.
    r = service.post("/v1/signup", json={"email": "Anna@example.org", "password": PASSWORD})
    assert r.status_code == 200 and r.json()["user"]["id"] == ""
    assert any("exists already" in m[2] for m in service.outbox.mails)
    # Sign in on a second machine: a security notice and a mail.
    second = signin(service)
    me = service.get("/v1/me", headers=bearer(second["token"])).json()
    assert [d["device_name"] for d in me["devices"]] == ["Anna's phone", "Anna's laptop"]
    assert me["device"]["current"] and me["unread_notices"] == 1
    notices = service.get("/v1/me/notices", headers=bearer(second["token"])).json()["notices"]
    assert notices[0]["kind"] == "security" and "Anna's phone" in notices[0]["subject"]
    # The wrong password, and an unknown address, are the same error.
    r = service.post("/v1/signin", json={"email": "anna@example.org", "password": "wrong password here"})
    assert r.status_code == 401 and r.json()["error"] == "bad_credentials"
    assert service.post("/v1/signin", json={"email": "nobody@example.org", "password": PASSWORD}).json()["error"] == "bad_credentials"
    history = service.get("/v1/me/sign-ins", headers=bearer(first["token"])).json()["sign_ins"]
    assert [h["ok"] for h in history] == [False, True, True]
    # A weak password and a bad address are refused at sign-up.
    assert service.post("/v1/signup", json={"email": "b@example.org", "password": "short"}).json()["error"] == "weak_password"
    assert service.post("/v1/signup", json={"email": "not-an-address", "password": PASSWORD}).json()["error"] == "bad_email"
    # Signing out ends the session.
    assert service.post("/v1/me/signout", headers=bearer(second["token"])).status_code == 200
    assert service.get("/v1/me", headers=bearer(second["token"])).json()["error"] == "signed_out"


def test_unverified_cannot_sign_in_and_can_resend(service):
    service.post("/v1/signup", json={"email": "b@example.org", "password": PASSWORD})
    r = service.post("/v1/signin", json={"email": "b@example.org", "password": PASSWORD})
    assert r.status_code == 403 and r.json()["error"] == "email_unverified"
    assert service.post("/v1/signup/verify", json={"email": "b@example.org", "code": "000000"}).json()["error"] == "bad_code"
    assert service.post("/v1/signup/resend", json={"email": "b@example.org"}).json()["error"] == "code_sent"   # within the minute


def test_profile_settings_email_and_password(service):
    s = signup(service)
    h = bearer(s["token"])
    r = service.put("/v1/me", json={"display_name": "Anna K.", "handle": "@AnnaK", "about": "Edits video."}, headers=h)
    assert r.status_code == 200 and r.json()["handle"] == "annak" and r.json()["display_name"] == "Anna K."
    other = signup(service, "b@example.org", "B's machine")
    assert service.put("/v1/me", json={"handle": "annak"}, headers=bearer(other["token"])).json()["error"] == "handle_taken"
    assert service.put("/v1/me", json={"handle": "a!"}, headers=h).json()["error"] == "bad_handle"
    r = service.put("/v1/me/settings", json={"notifications": {"security": "off", "deals": "off"}, "history_on_account": True}, headers=h)
    assert r.json()["settings"]["notifications"]["security"] == "email" and r.json()["settings"]["notifications"]["deals"] == "off"
    assert r.json()["settings"]["history_on_account"] is True
    # The email moves after a code to the new address.
    assert service.post("/v1/me/email", json={"email": "b@example.org", "password": PASSWORD}, headers=h).json()["error"] == "email_taken"
    r = service.post("/v1/me/email", json={"email": "anna.k@example.org", "password": PASSWORD}, headers=h)
    assert r.status_code == 200
    code = service.outbox.code("anna.k@example.org")
    r = service.post("/v1/me/email/verify", json={"code": code}, headers=h)
    assert r.status_code == 200 and r.json()["email"] == "anna.k@example.org"
    # A password change signs the other machines out, not this one.
    second = signin(service, "anna.k@example.org")
    r = service.post("/v1/me/password", json={"current": "wrong wrong wrong", "new": PASSWORD + "x"}, headers=h)
    assert r.status_code == 401
    r = service.post("/v1/me/password", json={"current": PASSWORD, "new": PASSWORD + "x"}, headers=h)
    assert r.json() == {"changed": True, "signed_out": 1}
    assert service.get("/v1/me", headers=bearer(second["token"])).json()["error"] == "signed_out"
    assert service.get("/v1/me", headers=h).status_code == 200


def test_two_factor_and_recovery(service):
    s = signup(service)
    h = bearer(s["token"])
    setup = service.post("/v1/me/two-factor/setup", headers=h).json()
    assert setup["otpauth_url"].startswith("otpauth://totp/UltraStore:anna%40example.org?secret=" + setup["secret"])
    assert service.post("/v1/me/two-factor/enable", json={"code": "000000"}, headers=h).json()["error"] == "bad_code"
    r = service.post("/v1/me/two-factor/enable", json={"code": totp_code(setup["secret"])}, headers=h)
    codes = r.json()["recovery_codes"]
    assert r.json()["enabled"] and len(codes) == 8
    assert service.get("/v1/me", headers=h).json()["two_factor"] and service.get("/v1/me", headers=h).json()["recovery_codes_left"] == 8
    # A sign-in now needs the code.
    r = service.post("/v1/signin", json={"email": "anna@example.org", "password": PASSWORD})
    assert r.status_code == 401 and r.json()["error"] == "two_factor_required"
    signin(service, totp=totp_code(setup["secret"]))
    # A recovery code works once and leaves a notice.
    signin(service, recovery_code=codes[0])
    r = service.post("/v1/signin", json={"email": "anna@example.org", "password": PASSWORD, "recovery_code": codes[0]})
    assert r.json()["error"] == "two_factor_required"
    assert service.get("/v1/me", headers=h).json()["recovery_codes_left"] == 7
    # A new set, and turning it off.
    r = service.post("/v1/me/two-factor/recovery-codes", json={"code": totp_code(setup["secret"])}, headers=h)
    assert len(r.json()["recovery_codes"]) == 8 and codes[1] not in r.json()["recovery_codes"]
    assert service.post("/v1/me/two-factor/disable", json={"code": totp_code(setup["secret"])}, headers=h).json() == {"enabled": False}
    signin(service)
    # The password reset asks for the code by mail (and the second factor when on).
    assert service.post("/v1/password/forgot", json={"email": "anna@example.org"}).json() == {"sent": True}
    code = service.outbox.code("anna@example.org")
    r = service.post("/v1/password/reset", json={"email": "anna@example.org", "code": code, "new_password": "another good long password"})
    assert r.json() == {"reset": True}
    assert service.get("/v1/me", headers=h).json()["error"] == "signed_out"
    r = service.post("/v1/signin", json={"email": "anna@example.org", "password": "another good long password"})
    assert r.status_code == 200


def test_devices_licences_and_seats(service):
    s = signup(service)
    h = bearer(s["token"])
    phone = signin(service)
    hp = bearer(phone["token"])
    token = service.issuer.issue("lic-1", "org.ultraos.ultravideo", valid_until="")
    r = service.post("/v1/me/licences", json={"licence": token}, headers=h)
    assert r.status_code == 200, r.text
    lic = r.json()["licence"]
    assert lic["id"] == "lic-1" and lic["app"] == "org.ultraos.ultravideo" and lic["seats"] == 2 and lic["seats_used"] == 0
    assert service.post("/v1/me/licences", json={"licence": "ULIC1.bogus.bogus"}, headers=h).json()["error"] == "bad_licence"
    # Attached again (a renewal): the same licence, not a second row.
    assert service.post("/v1/me/licences", json={"licence": token}, headers=h).json()["licence"]["id"] == "lic-1"
    # Another account cannot take it.
    other = signup(service, "b@example.org", "B")
    assert service.post("/v1/me/licences", json={"licence": token}, headers=bearer(other["token"])).json()["error"] == "licence_taken"
    # Two seats: the laptop and the phone; a third machine is refused until one signs out.
    assert service.post("/v1/me/licences/lic-1/install", headers=h).json()["licence"]["seats_used"] == 1
    assert service.post("/v1/me/licences/lic-1/install", headers=hp).json()["licence"]["seats_used"] == 2
    pi = signin(service, device="Kitchen Pi")
    r = service.post("/v1/me/licences/lic-1/install", headers=bearer(pi["token"]))
    assert r.status_code == 409 and r.json()["error"] == "no_seat"
    devices = service.get("/v1/me/devices", headers=h).json()["devices"]
    phone_id = next(d["id"] for d in devices if d["device_name"] == "Anna's phone")
    assert service.put(f"/v1/me/devices/{phone_id}", json={"name": "Anna's new phone"}, headers=h).json()["device"]["device_name"] == "Anna's new phone"
    assert service.delete(f"/v1/me/devices/{phone_id}", headers=h).json() == {"signed_out": True}
    assert service.get("/v1/me", headers=hp).json()["error"] == "signed_out"
    assert service.post("/v1/me/licences/lic-1/install", headers=bearer(pi["token"])).json()["licence"]["seats_used"] == 2
    assert service.post("/v1/me/licences/lic-1/uninstall", headers=bearer(pi["token"])).json()["licence"]["seats_used"] == 1
    assert service.post("/v1/me/signout-all", headers=h).json() == {"signed_out": 1}
    assert service.get("/v1/me/licences", headers=h).json()["licences"][0]["seats_used"] == 1
    assert service.delete("/v1/me/licences/lic-1", headers=h).json() == {"released": True}
    assert service.get("/v1/me/licences", headers=h).json()["licences"] == []


def test_archive_deletion_and_the_operator(service):
    s = signup(service)
    h = bearer(s["token"])
    service.put("/v1/me", json={"display_name": "Anna K.", "handle": "annak"}, headers=h)
    # The operator's list and the counts.
    r = service.get("/v1/users", headers=OP)
    assert r.status_code == 200
    users, stats = r.json()["users"], r.json()["stats"]
    assert users[0]["email"] == "an***@example.org" and "password_hash" not in users[0] and "year_of_birth" not in users[0]
    assert stats["accounts"] == 1 and stats["new_in_30_days"] == 1 and stats["two_factor_percent"] == 0
    assert service.get("/v1/users", headers={"Authorization": "Bearer wrong"}).status_code == 401
    uid = users[0]["id"]
    # A notice from the operator reaches the account and the mailbox.
    r = service.post(f"/v1/users/{uid}/notice", json={"kind": "notice", "subject": "About your review", "text": "Thank you."}, headers=OP)
    assert r.status_code == 200
    notices = service.get("/v1/me/notices", headers=h).json()["notices"]
    assert notices[0]["subject"] == "About your review" and notices[0]["created_by"] == "operator"
    assert service.outbox.mails[-1][1] == "UltraStore: About your review"
    assert service.post(f"/v1/me/notices/{notices[0]['id']}/read", headers=h).json() == {"read": True}
    assert service.get("/v1/me", headers=h).json()["unread_notices"] == 0
    # The data archive: built at once, released by the operator, downloaded once.
    r = service.post("/v1/me/archive", headers=h)
    archive = r.json()["archive"]
    assert archive["state"] == "built" and archive["due"] == add_days(today(), 1)
    assert service.post("/v1/me/archive", headers=h).json()["archive"]["id"] == archive["id"]
    assert service.get("/v1/users?state=requests", headers=OP).json()["users"][0]["request"] == "archive"
    assert service.get("/v1/users", headers=OP).json()["stats"]["archive_requests"] == 1
    assert service.get(f"/v1/me/archive/{archive['id']}/download", headers=h).status_code == 404
    assert service.post(f"/v1/archives/{archive['id']}/release", headers=OP).json()["archive"]["state"] == "released"
    r = service.get(f"/v1/me/archive/{archive['id']}/download", headers=h)
    assert r.status_code == 200 and r.json()["profile"]["handle"] == "annak" and r.json()["notices"][0]["subject"] == "About your review"
    assert service.get(f"/v1/me/archive/{archive['id']}/download", headers=h).status_code == 404
    assert service.get("/v1/me/archive", headers=h).json()["archives"][0]["state"] == "downloaded"
    # Suspended: signed out, refused, reinstated.
    r = service.post(f"/v1/users/{uid}/suspend", json={"reason": "review spam", "days": 30}, headers=OP)
    assert r.json()["user"]["state"] == "suspended" and r.json()["user"]["suspended_until"] == add_days(today(), 30)
    assert service.get("/v1/me", headers=h).status_code == 401
    assert service.post("/v1/signin", json={"email": "anna@example.org", "password": PASSWORD}).json()["error"] == "suspended"
    assert service.get("/v1/users?state=suspended", headers=OP).json()["users"][0]["id"] == uid
    assert service.post(f"/v1/users/{uid}/reinstate", json={"reason": "appeal upheld"}, headers=OP).json()["user"]["state"] == "active"
    s = signin(service)
    h = bearer(s["token"])
    # Sign out everywhere and reset two-factor, from the operator.
    assert service.post(f"/v1/users/{uid}/signout-all", json={"reason": "stolen laptop"}, headers=OP).json() == {"signed_out": 1}
    s = signin(service)
    h = bearer(s["token"])
    assert service.post(f"/v1/users/{uid}/two-factor/reset", json={"reason": "identity checked"}, headers=OP).json() == {"reset": True}
    s = signin(service)
    h = bearer(s["token"])
    # The file: devices, licences, notices, history; nothing private.
    f = service.get(f"/v1/users/{uid}", headers=OP).json()
    assert f["display_name"] == "Anna K." and len(f["devices"]) == 1 and [n["kind"] for n in f["notices"]][-1] == "notice"
    assert {h["action"] for h in f["history"]} >= {"signup", "notify", "suspend", "reinstate", "signout_all", "two_factor_reset", "release_archive"}
    assert "password_hash" not in f and "year_of_birth" not in f and "licence" not in json.dumps(f["licences"])
    # Deletion: asked by the person, carried out by the operator or after the grace.
    assert service.post(f"/v1/users/{uid}/delete", json={}, headers=OP).json()["error"] == "not_requested"
    assert service.post("/v1/me/delete", json={"password": "wrong wrong wrong"}, headers=h).status_code == 401
    r = service.post("/v1/me/delete", json={"password": PASSWORD}, headers=h)
    assert r.json()["state"] == "deleting" and r.json()["delete_after"] == add_days(today(), 30)
    assert service.get("/v1/users?state=deleting", headers=OP).json()["users"][0]["request"] == "deletion"
    assert service.post("/v1/me/delete/cancel", headers=h).json()["state"] == "active"
    service.post("/v1/me/delete", json={"password": PASSWORD}, headers=h)
    assert service.post(f"/v1/users/{uid}/delete", json={"reason": "requested"}, headers=OP).json() == {"deleted": True}
    assert service.get("/v1/me", headers=h).status_code == 401
    assert service.get("/v1/users", headers=OP).json()["users"] == []
    assert service.get(f"/v1/users/{uid}", headers=OP).status_code == 404
    # The address is free again, and the audit chain is whole.
    signup(service)
    assert service.get("/v1/audit/verify", headers=OP).json()["ok"] is True
    assert len(service.get("/v1/audit", headers=OP).json()["audit"]) >= 10


def test_maintenance_carries_out_due_deletions(service, tmp_path):
    s = signup(service)
    service.post("/v1/me/delete", json={"password": PASSWORD}, headers=bearer(s["token"]))
    store: AccountStore = service.app.state.store
    uid = s["user"]["id"]
    store.update_user(uid, delete_after=add_days(today(), -1))
    store.suspend_user(uid, reason="old", days=0, actor="test")
    store.update_user(uid, state="deleting", suspended_until=add_days(today(), -2))
    assert service.post("/v1/maintenance", headers=OP).json()["deleted"] == 1
    assert store.get_user(uid).state == "deleted" and store.get_user(uid).email == f"deleted-{uid}"


def test_audit_chain_shows_an_edit(service):
    signup(service)
    store: AccountStore = service.app.state.store
    assert store.verify_audit()["ok"]
    store._db.execute("UPDATE audit SET actor = 'someone else' WHERE id = 1")
    assert store.verify_audit() == {"ok": False, "rows": 0, "broken_at": 1}


def test_healthz_names_what_is_missing(tmp_path):
    with TestClient(create_app(Settings(database="sqlite:///" + str(tmp_path / "a.sqlite3")), mailer=Outbox())) as client:
        assert client.get("/healthz").text == "not configured: ULTRASTORE_ACCOUNT_OPERATOR_TOKEN, ULTRASTORE_ACCOUNT_LICENCE_KEY"
