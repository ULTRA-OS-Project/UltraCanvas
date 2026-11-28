"""The marketplace service end to end, through FastAPI's test client, on
SQLite and on whatever ULTRASTORE_MARKET_TEST_PG_URL and
ULTRASTORE_MARKET_TEST_MYSQL_URL name: a merchant registered and signing
its requests, the terms accepted and a merchant paused without them, a
submission checked, reviewed, released, resubmitted and withdrawn, the
reports through the three doors with the reporter kept from the merchant,
the notices and the strikes that suspend by the policy, the suspensions
and reinstatements, and the audit chain that shows an edit."""

from __future__ import annotations

import hashlib
import io
import json
import os

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, NoEncryption, PrivateFormat
from fastapi.testclient import TestClient

from ultrastore_market.app import create_app
from ultrastore_market.auth import public_hex, sign_request, verify_request
from ultrastore_market.config import Settings
from ultrastore_market.publish import SigningKey
from ultrastore_market.store import MarketStore, add_days, canonical, today
from ultrastore_db.totp import totp_code

OP = {"Authorization": "Bearer op-token"}
TABLES = ["operator_codes", "operators", "applications", "audit", "suspensions", "notice_acks", "notices", "reports", "decisions", "submissions", "terms_acceptances", "terms_versions", "merchants", "meta"]


def database_urls() -> list[str]:
    urls = ["sqlite"]
    if os.environ.get("ULTRASTORE_MARKET_TEST_PG_URL"):
        urls.append("postgresql")
    if os.environ.get("ULTRASTORE_MARKET_TEST_MYSQL_URL"):
        urls.append("mysql")
    return urls


def database_url(kind: str, tmp_path) -> str:
    if kind == "sqlite":
        return "sqlite:///" + str(tmp_path / "market.sqlite3")
    url = os.environ["ULTRASTORE_MARKET_TEST_PG_URL" if kind == "postgresql" else "ULTRASTORE_MARKET_TEST_MYSQL_URL"]
    from ultrastore_db import Database
    db = Database(url)
    db.script([f"DROP TABLE IF EXISTS {t}" for t in TABLES])
    db.close()
    return url


class MerchantClient:
    """A merchant's backend: every request signed with the merchant's key."""

    def __init__(self, client: TestClient, merchant_id: str) -> None:
        self.client = client
        self.id = merchant_id
        self.key = Ed25519PrivateKey.generate()
        self.seed = self.key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
        self.public = public_hex(self.seed)

    def sign_component(self, component: dict) -> str:
        return self.key.sign(canonical(component).encode()).hex()

    def request(self, method: str, path: str, body: dict | None = None, timestamp: str | None = None):
        raw = json.dumps(body).encode() if body is not None else b""
        headers = {"Authorization": sign_request(self.seed, self.id, method, path, raw, timestamp)}
        if body is not None:
            headers["Content-Type"] = "application/json"
        return self.client.request(method, path, content=raw, headers=headers)

    def upload(self, path: str, raw: bytes):
        headers = {"Authorization": sign_request(self.seed, self.id, "POST", path, raw), "Content-Type": "application/octet-stream"}
        return self.client.post(path, content=raw, headers=headers)


@pytest.fixture(params=database_urls())
def service(request, tmp_path):
    settings = Settings(database=database_url(request.param, tmp_path), operator_token="op-token", operator_session_secret="shared-secret",
                        publish_dir=str(tmp_path / "publish"), checks_inline=True,
                        payment_url="https://pay.example", licence_key="ab" * 32, public_base_url="https://market.example",
                        trusted_keys=["cd" * 32])
    mails: list[tuple[str, str, str]] = []
    with TestClient(create_app(settings, mailer=lambda to, subject, text: mails.append((to, subject, text)))) as client:
        client.mails = mails   # type: ignore[attr-defined]
        yield client


def component(app_id: str, version: str) -> dict:
    return {"id": app_id, "name": "Hello", "publisher": "anna-co", "pricing": {"model": "free"},
            "releases": [{"version": version, "channel": "stable", "packages": [{"os": "linux", "arch": "x86_64", "format": "zip", "sha256": "a" * 64, "size": 10}]}]}


def register(client, name="Anna & Co") -> MerchantClient:
    m = MerchantClient(client, "")
    r = client.post("/v1/merchants", json={"name": name, "contact": "anna@example.org", "key": m.public}, headers=OP)
    assert r.status_code == 200, r.text
    m.id = r.json()["id"]
    return m


def test_request_signing():
    seed = Ed25519PrivateKey.generate().private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
    header = sign_request(seed, "m-1", "POST", "/v1/submissions", b'{"a":1}', "1700000000")
    assert header.startswith("Merchant m-1:1700000000:")
    sig = header.split(":")[2]
    assert verify_request(public_hex(seed), "POST", "/v1/submissions", "1700000000", sig, b'{"a":1}', now=1700000100)
    assert not verify_request(public_hex(seed), "POST", "/v1/submissions", "1700000000", sig, b'{"a":2}', now=1700000100)   # another body
    assert not verify_request(public_hex(seed), "GET", "/v1/submissions", "1700000000", sig, b'{"a":1}', now=1700000100)    # another method
    assert not verify_request(public_hex(seed), "POST", "/v1/submissions", "1700000000", sig, b'{"a":1}', now=1700001000)   # too late
    assert not verify_request("00" * 32, "POST", "/v1/submissions", "1700000000", sig, b'{"a":1}', now=1700000100)
    assert add_days("2026-12-31", 1) == "2027-01-01"


def test_merchants_terms_and_standing(service):
    client = service
    assert client.get("/healthz").text == "ok"
    assert client.post("/v1/merchants", json={"name": "x", "key": "0" * 64}).status_code == 401
    assert client.post("/v1/merchants", json={"name": "x", "key": "zz" * 32}, headers=OP).status_code == 400
    anna = register(client)
    assert client.post("/v1/merchants", json={"id": anna.id, "name": "x", "key": "0" * 64}, headers=OP).status_code == 409
    # Without terms in force the merchant is active; a signed request works, a forged or stale one does not.
    me = anna.request("GET", "/v1/merchants/me")
    assert me.status_code == 200, me.text
    assert me.json()["standing"] == "active" and me.json()["strikes"] == 0 and me.json()["terms"] == {"in_force": 0, "accepted": 0}
    assert anna.request("GET", "/v1/merchants/me", timestamp="1700000000").status_code == 401
    other = MerchantClient(client, anna.id)
    assert other.request("GET", "/v1/merchants/me").status_code == 401
    assert client.get("/v1/merchants/me").status_code == 401
    # Terms come into force: the merchant is paused until it accepts the version in force.
    assert client.get("/v1/terms/current").status_code == 404
    t1 = client.post("/v1/terms", json={"text": "Be nice.", "changes": "first", "in_force_from": "2026-01-01"}, headers=OP).json()
    assert t1["version"] == 1
    later = client.post("/v1/terms", json={"text": "Be nicer.", "changes": "nicer", "in_force_from": add_days(today(), 30)}, headers=OP).json()
    assert later["version"] == 2 and client.get("/v1/terms").json()["in_force"] == 1
    assert client.get("/v1/terms/current").json()["version"] == 1
    assert client.post("/v1/terms", json={"text": "x", "in_force_from": "2026-01-01", "version": 1}, headers=OP).status_code == 409
    assert client.get(f"/v1/merchants/{anna.id}", headers=OP).json()["standing"] == "paused"
    assert anna.request("POST", "/v1/terms/accept", {"version": 9}).status_code == 404
    accepted = anna.request("POST", "/v1/terms/accept", {"version": 1, "accepted_by": "Anna"}).json()
    assert accepted["standing"] == "active" and accepted["accepted_by"] == "Anna"
    assert client.get(f"/v1/merchants/{anna.id}", headers=OP).json()["acceptances"][0]["version"] == 1
    # The key rotated: the old one signs no more.
    new = MerchantClient(client, anna.id)
    assert client.put(f"/v1/merchants/{anna.id}", json={"key": new.public, "contact": "new@example.org"}, headers=OP).json()["contact"] == "new@example.org"
    assert anna.request("GET", "/v1/merchants/me").status_code == 401
    assert new.request("GET", "/v1/merchants/me").status_code == 200
    assert [m["id"] for m in client.get("/v1/merchants", headers=OP).json()["merchants"]] == [anna.id]
    # Every step is in the audit log, chained.
    audit = client.get("/v1/audit", headers=OP).json()
    assert [a["action"] for a in audit["audit"]] == ["register", "terms", "terms", "accept_terms", "update"]
    assert audit["audit"][0]["prev_hash"] == "" and audit["audit"][1]["prev_hash"] == audit["audit"][0]["hash"]
    assert client.get("/v1/audit/verify", headers=OP).json() == {"ok": True, "rows": 5, "last_hash": audit["audit"][-1]["hash"]}
    assert client.get("/v1/audit", params={"since": 4}, headers=OP).json()["audit"][0]["action"] == "update"


def test_submissions_are_checked_reviewed_released_and_withdrawn(service):
    client = service
    anna = register(client)
    comp = component("org.example.hello", "1.0.0")
    packages = [{"file": "restricted/" + "a" * 64 + ".zip", "sha256": "a" * 64, "size": 10}]
    # Unsigned: refused. Signed by another key: received but the check fails.
    assert client.post("/v1/submissions", json={"component": comp, "signature": "0" * 128, "packages": packages}).status_code == 401
    r = anna.request("POST", "/v1/submissions", {"component": comp, "signature": "0" * 128, "packages": packages})
    assert r.status_code == 200, r.text
    bad = r.json()
    assert bad["state"] == "failed_checks" and bad["checks"]["signature"] == "failed" and bad["checks"]["packages"] == "ok"
    # The real one: checked, in review, released with the next catalog sequence.
    good = anna.request("POST", "/v1/submissions", {"component": comp, "signature": anna.sign_component(comp), "packages": packages}).json()
    assert good["state"] == "checked" and good["version"] == "1.0.0" and good["app"] == "org.example.hello"
    # Without a package store the pipeline checks the listing alone; the rest says why it did not run.
    assert good["checks"] == {"signature": "ok", "merchant_standing": "ok", "packages": "ok", "scans": "not run", "entry": "not run",
                              "smoke": "not run", "listing": "failed"}
    assert good["findings"]["scans"] == ["no package store: the packages stay on the merchant's server, unscanned"]
    assert "no summary: the store's card shows one line under the name" in good["findings"]["listing"] and good["checked_at"]
    sid = good["id"]
    assert client.post(f"/v1/submissions/{sid}/decide", json={"action": "fly"}, headers=OP).status_code == 400
    assert client.post(f"/v1/submissions/{bad['id']}/decide", json={"action": "release"}, headers=OP).status_code == 409   # failed the checks
    assert client.post(f"/v1/submissions/{sid}/decide", json={"action": "start_review", "message": "looking"}, headers=OP).json()["state"] == "in_review"
    released = client.post(f"/v1/submissions/{sid}/decide", json={"action": "release", "message": "fine"}, headers=OP).json()
    assert released["state"] == "released" and released["released_sequence"] == 1
    assert [d["action"] for d in released["decisions"]] == ["start_review", "release"] and released["decisions"][1]["sequence"] == 1
    assert client.post(f"/v1/submissions/{sid}/decide", json={"action": "reject"}, headers=OP).status_code == 409   # released stays released
    assert [s["state"] for s in client.get("/v1/submissions", params={"state": "released"}, headers=OP).json()["submissions"]] == ["released"]
    assert client.get("/v1/submissions", params={"app": "org.example.hello"}, headers=OP).json()["submissions"][0]["id"] == bad["id"]
    assert client.get(f"/v1/submissions/{sid}", headers=OP).json()["decisions"][1]["actor"] == "operator"
    assert client.get("/v1/submissions/nope", headers=OP).status_code == 404
    # What the catalog is generated from.
    rel = client.get("/v1/released", headers=OP).json()
    assert rel["sequence"] == 1 and rel["published"] == 0 and [r["app"] for r in rel["released"]] == ["org.example.hello"] and rel["released"][0]["submission"] == sid
    # A resubmission answers the released one; changes are requested; it is withdrawn by the merchant.
    comp2 = component("org.example.hello", "1.1.0")
    assert anna.request("POST", "/v1/submissions", {"component": comp2, "signature": anna.sign_component(comp2), "packages": packages, "answers": "s-00000099"}).status_code == 400
    again = anna.request("POST", "/v1/submissions", {"component": comp2, "signature": anna.sign_component(comp2), "packages": packages, "answers": sid}).json()
    assert again["answers"] == sid and again["state"] == "checked"
    changes = client.post(f"/v1/submissions/{again['id']}/decide", json={"action": "request_changes", "message": "the icon is missing"}, headers=OP).json()
    assert changes["state"] == "changes_requested"
    assert client.post(f"/v1/submissions/{again['id']}/decide", json={"action": "release"}, headers=OP).status_code == 409
    mine = anna.request("GET", "/v1/merchants/me/submissions").json()["submissions"]
    assert [s["state"] for s in mine] == ["failed_checks", "released", "changes_requested"]
    assert mine[2]["decisions"][0]["message"] == "the icon is missing"
    withdrawn = anna.request("POST", f"/v1/submissions/{again['id']}/withdraw").json()
    assert withdrawn["state"] == "withdrawn"
    assert anna.request("POST", f"/v1/submissions/{again['id']}/withdraw").status_code == 409
    bob = register(client, "Bob")
    assert bob.request("POST", f"/v1/submissions/{sid}/withdraw").status_code == 404   # not his
    # The released list still names 1.0.0; the history shows the merchant every step.
    assert client.get("/v1/released", headers=OP).json()["released"][0]["component"]["releases"][0]["version"] == "1.0.0"
    history = anna.request("GET", "/v1/merchants/me/history").json()["history"]
    assert [h["action"] for h in history] == ["register", "submit", "run_checks", "submit", "run_checks", "start_review", "release", "submit", "run_checks",
                                              "request_changes", "withdraw"]
    assert history[6]["after"] == {"state": "released", "message": "fine", "sequence": 1}
    # A suspended app leaves the released list; reinstated, it is back. A paused merchant too.
    assert client.post("/v1/apps/org.example.hello/suspend", json={"clause": "3.2", "reason": "malware report", "days": 0}, headers=OP).status_code == 200
    assert client.get("/v1/released", headers=OP).json()["released"] == []
    assert client.post("/v1/apps/org.example.hello/reinstate", json={"reason": "cleared"}, headers=OP).json()["lifted"] == 1
    assert client.post("/v1/apps/org.example.hello/reinstate", json={}, headers=OP).json()["lifted"] == 0
    assert len(client.get("/v1/released", headers=OP).json()["released"]) == 1
    client.post("/v1/terms", json={"text": "Terms.", "in_force_from": "2026-01-01"}, headers=OP)
    assert client.get("/v1/released", headers=OP).json()["released"] == []   # Anna has not accepted
    anna.request("POST", "/v1/terms/accept", {"version": 1})
    assert len(client.get("/v1/released", headers=OP).json()["released"]) == 1
    # A paused merchant's submission checks fail on the standing; a release of one is refused while not active.
    client.post("/v1/terms", json={"text": "Terms 2.", "in_force_from": "2026-01-02"}, headers=OP)
    comp3 = component("org.example.hello", "1.2.0")
    paused = anna.request("POST", "/v1/submissions", {"component": comp3, "signature": anna.sign_component(comp3), "packages": packages}).json()
    assert paused["checks"]["merchant_standing"] == "paused" and paused["state"] == "failed_checks"
    assert client.post(f"/v1/submissions/{paused['id']}/decide", json={"action": "start_review"}, headers=OP).json()["state"] == "in_review"
    assert client.post(f"/v1/submissions/{paused['id']}/decide", json={"action": "release"}, headers=OP).status_code == 409
    assert client.get("/v1/audit/verify", headers=OP).json()["ok"]


def test_reports_through_three_doors(service):
    client = service
    anna = register(client)
    comp = component("org.example.hello", "1.0.0")
    anna.request("POST", "/v1/submissions", {"component": comp, "signature": anna.sign_component(comp), "packages": [{"file": "x.zip", "sha256": "b" * 64, "size": 1}]})
    # The store's door: anonymous or with the licence id; the reporter gets a secret to learn the outcome.
    r = client.post("/v1/reports", json={"target_kind": "app", "target": "org.example.hello", "kind": "crash", "text": "It crashes on start.", "reporter": "lic-77"})
    assert r.status_code == 200, r.text
    store_report = r.json()
    assert store_report["state"] == "open" and store_report["secret"]
    assert client.post("/v1/reports", json={"target_kind": "thing", "target": "x", "kind": "k", "text": "t"}).status_code == 422
    # The merchant's door: Bob reports Anna's app; the operator's door, from mail.
    bob = register(client, "Bob")
    merchant_report = bob.request("POST", "/v1/reports", {"target_kind": "app", "target": "org.example.hello", "kind": "copy", "text": "That is my code."}).json()
    operator_report = client.post("/v1/reports", json={"target_kind": "merchant", "target": anna.id, "kind": "complaint", "text": "From mail.", "reporter": "user@example.org"}, headers=OP).json()
    # The operator sees the reporters; Anna sees the reports about her and her app, never who filed them.
    listed = client.get("/v1/reports", headers=OP).json()["reports"]
    assert [(x["source"], x["reporter"]) for x in listed] == [("store", "lic-77"), ("merchant", "Bob"), ("operator", "user@example.org")]
    assert listed[1]["reporter_merchant"] == bob.id
    mine = anna.request("GET", "/v1/merchants/me/reports").json()["reports"]
    assert [x["kind"] for x in mine] == ["crash", "copy", "complaint"]
    assert all("reporter" not in x and "reporter_merchant" not in x and "findings" not in x for x in mine)
    assert bob.request("GET", "/v1/merchants/me/reports").json()["reports"] == []
    # Assigned, resolved: the findings for the operator, the one line for the reporter and the merchant.
    assert client.post(f"/v1/reports/{store_report['id']}/assign", json={"assignee": "kim"}, headers=OP).json()["state"] == "assigned"
    assert client.get("/v1/reports", params={"state": "assigned"}, headers=OP).json()["reports"][0]["assignee"] == "kim"
    done = client.post(f"/v1/reports/{store_report['id']}/resolve", json={"findings": "reproduced on 1.0.0, fixed in 1.0.1", "outcome": "Fixed in 1.0.1."}, headers=OP).json()
    assert done["state"] == "resolved" and done["findings"].startswith("reproduced") and done["resolved_at"]
    outcome = client.get(f"/v1/reports/{store_report['id']}/outcome", params={"secret": store_report["secret"]}).json()
    assert outcome == {"id": store_report["id"], "state": "resolved", "outcome": "Fixed in 1.0.1.", "resolved_at": done["resolved_at"]}
    assert client.get(f"/v1/reports/{store_report['id']}/outcome", params={"secret": "nope"}).status_code == 404
    assert anna.request("GET", "/v1/merchants/me/reports").json()["reports"][0]["outcome"] == "Fixed in 1.0.1."
    dismissed = client.post(f"/v1/reports/{merchant_report['id']}/dismiss", json={"outcome": "No copy found."}, headers=OP).json()
    assert dismissed["state"] == "dismissed"
    assert client.get(f"/v1/reports/{operator_report['id']}", headers=OP).json()["state"] == "open"
    assert client.get("/v1/reports/nope", headers=OP).status_code == 404
    assert client.post("/v1/reports/nope/assign", json={"assignee": "x"}, headers=OP).status_code == 404
    assert [a["action"] for a in client.get("/v1/audit", headers=OP).json()["audit"] if a["target_kind"] == "report"] == ["assign", "resolve", "dismiss"]


def test_notices_strikes_and_the_policy(service):
    client = service
    anna = register(client)
    bob = register(client, "Bob")
    assert client.get("/v1/policy", headers=OP).json()["strikes_before_suspension"] == 3
    policy = client.put("/v1/policy", json={"strikes_before_suspension": 2, "suspension_days": 10, "clauses_that_suspend": ["9.1"], "nonsense": 1}, headers=OP).json()
    assert policy["strikes_before_suspension"] == 2 and "nonsense" not in policy and policy["strike_expiry_days"] == 365
    # An announcement to all; a reminder to Anna; the merchant sees them, unacknowledged first, and acknowledges.
    assert client.post("/v1/notices", json={"kind": "announcement", "subject": "Maintenance", "text": "Sunday.", "recipients": ["*"]}, headers=OP).status_code == 200
    assert client.post("/v1/notices", json={"kind": "warning", "subject": "x", "recipients": [anna.id]}, headers=OP).status_code == 400   # no clause
    assert client.post("/v1/notices", json={"kind": "shout", "subject": "x", "recipients": ["*"]}, headers=OP).status_code == 400
    assert client.post("/v1/notices", json={"kind": "reminder", "subject": "x", "recipients": ["m-nope"]}, headers=OP).status_code == 404
    ask = client.post("/v1/notices", json={"kind": "information_request", "subject": "Your address", "text": "Please.", "recipients": [anna.id]}, headers=OP).json()
    assert ask["notice"]["deadline"] == add_days(today(), 14) and ask["suspended"] == []
    mine = anna.request("GET", "/v1/merchants/me/notices").json()["notices"]
    assert [n["kind"] for n in mine] == ["announcement", "information_request"] and all(not n["acknowledged_at"] for n in mine)
    assert bob.request("GET", "/v1/merchants/me/notices").json()["notices"][0]["kind"] == "announcement"
    acked = anna.request("POST", f"/v1/notices/{mine[0]['id']}/ack").json()
    assert acked["acknowledged_at"]
    assert anna.request("POST", f"/v1/notices/{mine[0]['id']}/ack").json()["acknowledged_at"] == acked["acknowledged_at"]
    assert bob.request("POST", f"/v1/notices/{ask['notice']['id']}/ack").status_code == 404   # not his
    assert [n["kind"] for n in anna.request("GET", "/v1/merchants/me/notices").json()["notices"]] == ["information_request", "announcement"]
    # A warning is a strike; the second suspends Anna by the policy, for its days, by the actor "policy".
    first = client.post("/v1/notices", json={"kind": "warning", "subject": "Late answer", "clause": "4.1", "recipients": [anna.id]}, headers=OP).json()
    assert first["notice"]["expires_at"] == add_days(today(), 365) and first["suspended"] == []
    assert anna.request("GET", "/v1/merchants/me").json()["strikes"] == 1
    second = client.post("/v1/notices", json={"kind": "warning", "subject": "Again", "clause": "4.1", "recipients": [anna.id, bob.id]}, headers=OP).json()
    assert len(second["suspended"]) == 1 and second["suspended"][0]["target"] == anna.id and second["suspended"][0]["end"] == add_days(today(), 10)
    assert second["suspended"][0]["reason"] == "2 strikes within 365 days" and second["suspended"][0]["created_by"] == "policy"
    me = anna.request("GET", "/v1/merchants/me").json()
    assert me["standing"] == "suspended" and me["strikes"] == 2 and me["suspensions"][0]["clause"] == "4.1"
    assert bob.request("GET", "/v1/merchants/me").json()["strikes"] == 1 and bob.request("GET", "/v1/merchants/me").json()["standing"] == "active"
    # A clause that suspends at once: Bob, on his first warning for it. Reinstated by the operator.
    third = client.post("/v1/notices", json={"kind": "warning", "subject": "Malware", "clause": "9.1", "recipients": [bob.id]}, headers=OP).json()
    assert third["suspended"][0]["reason"] == "clause 9.1 suspends at once"
    assert client.get(f"/v1/merchants/{bob.id}", headers=OP).json()["standing"] == "suspended"
    assert client.post(f"/v1/merchants/{bob.id}/reinstate", json={"reason": "false alarm"}, headers=OP).json()["lifted"] == 1
    assert client.get(f"/v1/merchants/{bob.id}", headers=OP).json()["standing"] == "active"
    # The operator suspends by hand, open-ended; a warning while suspended adds no second suspension.
    hand = client.post(f"/v1/merchants/{bob.id}/suspend", json={"clause": "2.0", "reason": "by hand"}, headers=OP).json()
    assert hand["suspension"]["end"] == "" and hand["merchant"]["standing"] == "suspended"
    more = client.post("/v1/notices", json={"kind": "warning", "subject": "More", "clause": "9.1", "recipients": [bob.id]}, headers=OP).json()
    assert more["suspended"] == []
    assert client.post("/v1/merchants/m-nope/suspend", json={}, headers=OP).status_code == 404
    assert [n["kind"] for n in client.get("/v1/notices", headers=OP).json()["notices"]].count("warning") == 4
    actions = [a["action"] for a in client.get("/v1/audit", headers=OP).json()["audit"]]
    assert actions.count("suspend") == 3 and actions.count("warn") == 4 and "policy" in actions and "reinstate" in actions and "acknowledge" in actions


def test_the_audit_chain_shows_an_edit(tmp_path):
    store = MarketStore("sqlite:///" + str(tmp_path / "audit.sqlite3"))
    store.audit("operator", "register", "merchant", "m-1", after={"name": "A"})
    store.audit("operator", "warn", "notice", "n-1", clause="4.1")
    store.audit("m-1", "acknowledge", "notice", "n-1")
    assert store.verify_audit() == {"ok": True, "rows": 3, "last_hash": store.audit_since(0)[-1]["hash"]}
    store._db.execute("UPDATE audit SET clause = '1.1' WHERE id = 2")   # an edit after the fact
    assert store.verify_audit() == {"ok": False, "rows": 1, "broken_at": 2}
    store._db.execute("UPDATE audit SET clause = '4.1' WHERE id = 2")
    store._db.execute("DELETE FROM audit WHERE id = 2")   # a gap
    assert store.verify_audit()["broken_at"] == 3
    store.close()


def test_the_catalog_is_published_from_the_released_submissions_and_signed(service, tmp_path):
    client = service
    anna = register(client)
    bob = register(client, "Bob")
    key = client.get("/v1/publish/key").json()
    assert len(key["public_key"]) == 64 and key["trusted_keys"] == [key["public_key"], "cd" * 32]
    packages = [{"file": "packages/" + "a" * 64 + ".zip", "sha256": "a" * 64, "size": 10}]
    # Two of Anna's apps released, one at a later version; a runtime; Bob's app released and then Bob suspended.
    hello = component("org.example.hello", "1.0.0")
    hello2 = component("org.example.hello", "1.1.0")
    runtime = {**component("org.example.runtime", "2.0.0"), "kind": "runtime", "name": "Runtime"}
    bobs = {**component("org.example.bob", "0.1.0"), "publisher": "bob-co"}
    ids = []
    for who, comp in ((anna, hello), (anna, hello2), (anna, runtime), (bob, bobs)):
        r = who.request("POST", "/v1/submissions", {"component": comp, "signature": who.sign_component(comp), "packages": packages})
        assert r.status_code == 200, r.text
        ids.append(r.json()["id"])
        assert client.post(f"/v1/submissions/{r.json()['id']}/decide", json={"action": "release"}, headers=OP).status_code == 200
    client.post(f"/v1/merchants/{bob.id}/suspend", json={"clause": "9.1", "reason": "x"}, headers=OP)
    # Publish: the files, the sequence past the releases, the signature over the exact bytes.
    assert client.post("/v1/publish").status_code == 401
    r = client.post("/v1/publish", headers=OP)
    assert r.status_code == 200, r.text
    report = r.json()
    assert report["sequence"] == 5 and report["apps"] == 1 and report["components"] == 1 and report["publishers"] == 1 and report["left_out"] == []
    folder = tmp_path / "publish"
    body = (folder / "catalog.json").read_bytes()
    signature = bytes.fromhex((folder / "catalog.json.sig").read_text().strip())
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    Ed25519PublicKey.from_public_bytes(bytes.fromhex(key["public_key"])).verify(signature, body)
    catalog = json.loads(body)
    assert catalog["schema"] == 2 and catalog["sequence"] == 5 and catalog["trusted_keys"] == key["trusted_keys"]
    assert catalog["payment_url"] == "https://pay.example" and catalog["licence_key"] == "ab" * 32 and catalog["market_url"] == "https://market.example"
    assert catalog["codes"] == [] and [a["id"] for a in catalog["apps"]] == ["org.example.hello"]
    assert catalog["apps"][0]["releases"][0]["version"] == "1.1.0"   # the latest release of the app
    assert catalog["apps"][0]["publisher"] == "anna-co" and catalog["publishers"] == [{"id": "anna-co", "name": "Anna & Co"}]
    assert [c["id"] for c in catalog["components"]] == ["org.example.runtime"]   # Bob's app is out: suspended
    # Again: the next sequence; the audit log has both publishes.
    assert client.post("/v1/publish", headers=OP).json()["sequence"] == 6
    assert client.get("/v1/released", headers=OP).json()["published"] == 6
    assert [a["action"] for a in client.get("/v1/audit", headers=OP).json()["audit"]].count("publish") == 2
    # A merchant whose contact is a URL becomes the publisher's URL; Bob reinstated is back in.
    client.post(f"/v1/merchants/{bob.id}/reinstate", json={}, headers=OP)
    client.put(f"/v1/merchants/{bob.id}", json={"contact": "https://bob.example"}, headers=OP)
    client.post("/v1/publish", headers=OP)
    catalog = json.loads((folder / "catalog.json").read_bytes())
    assert [a["id"] for a in catalog["apps"]] == ["org.example.bob", "org.example.hello"]
    assert {"id": "bob-co", "name": "Bob", "url": "https://bob.example"} in catalog["publishers"]
    # The signing key is kept across restarts when none is configured.
    seed = SigningKey.generate()
    assert len(seed.seed_hex) == 64 and SigningKey(bytes.fromhex(seed.seed_hex)).public_hex == seed.public_hex


def test_publish_needs_a_folder(tmp_path):
    settings = Settings(database="sqlite:///" + str(tmp_path / "m.sqlite3"), operator_token="op-token")
    with TestClient(create_app(settings)) as client:
        r = client.post("/v1/publish", headers=OP)
        assert r.status_code == 503 and "ULTRASTORE_MARKET_PUBLISH_DIR" in r.json()["error_description"]
        first = client.get("/v1/publish/key").json()["public_key"]
    with TestClient(create_app(settings)) as client:
        assert client.get("/v1/publish/key").json()["public_key"] == first


def test_a_merchant_applies_is_approved_and_edits_its_profile(service):
    m = MerchantClient(service, "")
    body = {"name": "Pixelwerk Studio", "contact": "mail@pixelwerk.example", "email": "jo@pixelwerk.example", "website": "https://pixelwerk.example",
            "description": "Games and tools for ULTRA OS.", "key": m.public}
    # A bad signature, or a key with a signature by another key, is refused; the right one is an application.
    r = service.post("/v1/merchants/apply", json={**body, "signature": "00" * 64})
    assert r.status_code == 400 and r.json()["error"] == "bad_signature"
    signature = m.key.sign(canonical(body).encode()).hex()
    r = service.post("/v1/merchants/apply", json={**body, "signature": signature})
    assert r.status_code == 200, r.text
    aid, secret = r.json()["id"], r.json()["secret"]
    assert aid.startswith("ap-") and r.json()["state"] == "applied"
    # Applied twice with the same key: refused while one waits.
    assert service.post("/v1/merchants/apply", json={**body, "signature": signature}).json()["error"] == "applied_already"
    # The applicant polls with the secret; a stranger learns nothing.
    assert service.get(f"/v1/applications/{aid}").status_code == 404
    assert service.get(f"/v1/applications/{aid}?secret=wrong").status_code == 404
    state = service.get(f"/v1/applications/{aid}?secret={secret}").json()
    assert state["state"] == "applied" and "email" not in state
    # The operator sees the queue with the email, and approves with an id of its own.
    queue = service.get("/v1/applications?state=applied", headers=OP).json()["applications"]
    assert len(queue) == 1 and queue[0]["email"] == "jo@pixelwerk.example"
    r = service.post(f"/v1/applications/{aid}/approve", json={"id": "pixelwerk", "message": "Welcome"}, headers=OP)
    assert r.status_code == 200, r.text
    assert r.json()["application"]["state"] == "approved" and r.json()["merchant"]["id"] == "pixelwerk"
    assert r.json()["merchant"]["website"] == "https://pixelwerk.example" and r.json()["merchant"]["key"] == m.public
    assert service.post(f"/v1/applications/{aid}/approve", json={}, headers=OP).json()["error"] == "decided"
    state = service.get(f"/v1/applications/{aid}?secret={secret}").json()
    assert state["state"] == "approved" and state["merchant"] == "pixelwerk" and state["message"] == "Welcome"
    # The key now signs for a merchant: a new application with it is refused.
    assert service.post("/v1/merchants/apply", json={**body, "signature": signature}).json()["error"] == "key_registered"
    # The merchant edits its own profile, signed with its key; the key rotates signed with the old one.
    m.id = "pixelwerk"
    r = m.request("PUT", "/v1/merchants/me", {"description": "Games for ULTRA OS.", "website": "ftp://no"})
    assert r.status_code == 400 and r.json()["error"] == "bad_website"
    r = m.request("PUT", "/v1/merchants/me", {"description": "Games for ULTRA OS.", "contact": "https://pixelwerk.example/contact"})
    assert r.status_code == 200, r.text
    assert r.json()["description"] == "Games for ULTRA OS." and r.json()["name"] == "Pixelwerk Studio"
    other = MerchantClient(service, "pixelwerk")
    r = m.request("PUT", "/v1/merchants/me", {"key": other.public})
    assert r.status_code == 200 and r.json()["key"] == other.public
    assert m.request("GET", "/v1/merchants/me").status_code == 401   # the old key signs no more
    assert other.request("GET", "/v1/merchants/me").status_code == 200
    # Another merchant cannot take that key.
    anna = register(service)
    assert anna.request("PUT", "/v1/merchants/me", {"key": other.public}).json()["error"] == "key_taken"
    # A second application, rejected with a message the applicant learns.
    third = MerchantClient(service, "")
    body2 = {**body, "name": "Copycat", "key": third.public}
    r = service.post("/v1/merchants/apply", json={**body2, "signature": third.key.sign(canonical(body2).encode()).hex()})
    aid2, secret2 = r.json()["id"], r.json()["secret"]
    assert service.post(f"/v1/applications/{aid2}/reject", json={"message": "The name is another publisher's"}, headers=OP).json()["application"]["state"] == "rejected"
    assert service.get(f"/v1/applications/{aid2}?secret={secret2}").json()["message"] == "The name is another publisher's"
    assert service.get("/v1/audit/verify", headers=OP).json()["ok"] is True
    # The publish names the publisher's page from the website.
    assert service.get("/v1/merchants/pixelwerk", headers=OP).json()["website"] == "https://pixelwerk.example"


def test_the_operators_sign_in_and_the_roles_gate_the_calls(service):
    # The static token is the bootstrap admin; it makes the first accounts.
    me = service.get("/v1/operators/me", headers=OP).json()
    assert me["role"] == "admin" and me["bootstrap"] is True and "operators.manage" in me["permissions"]
    assert service.get("/v1/operators/me", headers={"Authorization": "Bearer op1.forged.00"}).status_code == 401
    r = service.post("/v1/operators", json={"email": "alex@ultra-os.example", "name": "Alex", "role": "admin", "password": "a long enough password"}, headers=OP)
    assert r.status_code == 200, r.text
    alex = r.json()
    assert alex["id"] == "op-0001" and alex["active"] is True and "password" not in alex and "password_hash" not in alex
    assert service.post("/v1/operators", json={"email": "alex@ultra-os.example", "role": "admin", "password": "a long enough password"}, headers=OP).json()["error"] == "operator_exists"
    assert service.post("/v1/operators", json={"email": "x@y", "role": "king", "password": "a long enough password"}, headers=OP).json()["error"] == "bad_role"
    assert service.post("/v1/operators", json={"email": "x@y", "role": "support", "password": "short"}, headers=OP).status_code == 422
    r = service.post("/v1/operators", json={"email": "Sam@ultra-os.example", "name": "Sam", "role": "support", "password": "another long password"}, headers=OP)
    sam = r.json()
    assert sam["email"] == "sam@ultra-os.example" and sam["role"] == "support"
    # Sign in: a wrong password, a wrong email, then the session.
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "nope"}).json()["error"] == "bad_credentials"
    assert service.post("/v1/operators/signin", json={"email": "nobody@ultra-os.example", "password": "a long enough password"}).status_code == 401
    r = service.post("/v1/operators/signin", json={"email": "ALEX@ultra-os.example", "password": "a long enough password"})
    assert r.status_code == 200, r.text
    assert r.json()["token"].startswith("op1.") and r.json()["operator"]["name"] == "Alex" and r.json()["hours"] == 12
    alex_h = {"Authorization": "Bearer " + r.json()["token"]}
    sam_h = {"Authorization": "Bearer " + service.post("/v1/operators/signin", json={"email": "sam@ultra-os.example", "password": "another long password"}).json()["token"]}
    me = service.get("/v1/operators/me", headers=alex_h).json()
    assert me == {"id": "op-0001", "name": "Alex", "role": "admin", "bootstrap": False, "permissions": me["permissions"]}
    assert service.get("/v1/operators/me", headers=sam_h).json()["permissions"] == ["notices.send", "reports.handle", "users.support"]
    assert service.get("/v1/operators", headers=OP).json()["operators"][1]["last_signin"] != ""
    # The roles gate the calls: support reads the merchants but registers none, sends a notice, cannot decide a review.
    anna = register(service)
    assert service.get("/v1/merchants", headers=sam_h).status_code == 200
    r = service.post("/v1/merchants", json={"name": "No", "contact": "no@example.org", "key": "ab" * 32}, headers=sam_h)
    assert r.status_code == 403 and r.json()["error"] == "forbidden" and "support" in r.json()["error_description"]
    assert service.post("/v1/notices", json={"kind": "announcement", "subject": "Hi", "text": "Welcome", "recipients": [anna.id]}, headers=sam_h).status_code == 200
    assert service.get("/v1/submissions", headers=sam_h).status_code == 200
    assert service.post("/v1/submissions/s-1/decide", json={"action": "approve"}, headers=sam_h).status_code == 403
    assert service.get("/v1/operators", headers=sam_h).status_code == 200
    assert service.post("/v1/operators", json={"email": "x@y", "role": "admin", "password": "a long enough password"}, headers=sam_h).status_code == 403
    # The audit names the person, not the token.
    rows = service.get("/v1/audit", headers=alex_h).json()["audit"]
    assert any(e["actor"] == "Sam" and e["action"] == "notify" for e in rows), rows
    # Sam changes their own name and password, not their role; Alex changes the role and disables.
    assert service.put("/v1/operators/op-0002", json={"name": "Samira"}, headers=sam_h).json()["name"] == "Samira"
    assert service.put("/v1/operators/op-0002", json={"role": "admin"}, headers=sam_h).status_code == 403
    assert service.put("/v1/operators/op-0001", json={"name": "A"}, headers=sam_h).status_code == 403
    assert service.put("/v1/operators/op-0002", json={"password": "a third long password"}, headers=sam_h).status_code == 200
    assert service.post("/v1/operators/signin", json={"email": "sam@ultra-os.example", "password": "another long password"}).status_code == 401
    assert service.put("/v1/operators/op-0001", json={"disabled": True}, headers=alex_h).json()["error"] == "own_account"
    assert service.put("/v1/operators/op-0001", json={"role": "auditor"}, headers=alex_h).json()["error"] == "own_account"
    assert service.put("/v1/operators/op-0002", json={"role": "auditor"}, headers=alex_h).json()["role"] == "auditor"
    assert service.put("/v1/operators/op-0002", json={"disabled": True}, headers=alex_h).json()["active"] is False
    assert service.put("/v1/operators/op-9999", json={"name": "x"}, headers=alex_h).status_code == 404
    # A disabled operator's session is refused on the next call, and cannot sign in.
    assert service.get("/v1/merchants", headers=sam_h).status_code == 401
    assert service.post("/v1/operators/signin", json={"email": "sam@ultra-os.example", "password": "a third long password"}).status_code == 401
    assert service.put("/v1/operators/op-0002", json={"disabled": False}, headers=alex_h).json()["active"] is True
    assert service.get("/v1/audit/verify", headers=OP).json()["ok"] is True


def test_signin_needs_the_session_secret(tmp_path):
    settings = Settings(database="sqlite:///" + str(tmp_path / "m.sqlite3"), operator_token="op-token", publish_dir=str(tmp_path / "publish"),
                        payment_url="https://pay.example", licence_key="ab" * 32)
    with TestClient(create_app(settings)) as client:
        client.post("/v1/operators", json={"email": "a@b.example", "role": "admin", "password": "a long enough password"}, headers=OP)
        r = client.post("/v1/operators/signin", json={"email": "a@b.example", "password": "a long enough password"})
        assert r.status_code == 503 and r.json()["error"] == "not_configured"


def test_a_package_is_uploaded_with_the_submission_downloaded_by_the_operator_and_laid_out_by_the_publish(tmp_path):
    settings = Settings(database="sqlite:///" + str(tmp_path / "m.sqlite3"), operator_token="op-token", publish_dir=str(tmp_path / "publish"),
                        package_dir=str(tmp_path / "store"), catalog_url="https://store.example/v1", max_package_mb=1, payment_url="https://pay.example",
                        licence_key="ab" * 32, public_base_url="https://market.example")
    with TestClient(create_app(settings)) as client:
        anna = register(client)
        free = b"PK\x03\x04 the free one"
        paid = b"PK\x03\x04 the paid one, no trial"
        comp = component("org.example.hello", "1.0.0")
        comp["releases"][0]["packages"] = [{"os": "linux", "arch": "x86_64", "format": "zip", "sha256": hashlib.sha256(free).hexdigest(), "size": len(free),
                                           "url": "https://anna.example/packages/" + hashlib.sha256(free).hexdigest() + ".zip"},
                                          {"os": "windows", "arch": "x86_64", "format": "zip", "sha256": hashlib.sha256(paid).hexdigest(), "size": len(paid),
                                           "url": "https://anna.example/restricted/" + hashlib.sha256(paid).hexdigest() + ".zip"}]
        packages = [{"file": "packages/" + hashlib.sha256(free).hexdigest() + ".zip", "sha256": hashlib.sha256(free).hexdigest(), "size": len(free), "format": "zip"},
                    {"file": "restricted/" + hashlib.sha256(paid).hexdigest() + ".zip", "sha256": hashlib.sha256(paid).hexdigest(), "size": len(paid),
                     "format": "zip", "access": "restricted"}]
        # Submitted: the packages wait for their bytes; a release is refused until they are there.
        sub = anna.request("POST", "/v1/submissions", {"component": comp, "signature": anna.sign_component(comp), "packages": packages}).json()
        assert sub["state"] == "checked" and sub["checks"]["packages"] == "waiting" and not any(p.get("uploaded") for p in sub["packages"])
        sid = sub["id"]
        r = client.post(f"/v1/submissions/{sid}/decide", json={"action": "release"}, headers=OP)
        assert r.status_code == 409 and r.json()["error"] == "packages_missing"
        # The wrong bytes, the wrong size, a hash the submission does not name, another merchant: refused.
        sha_free, sha_paid = hashlib.sha256(free).hexdigest(), hashlib.sha256(paid).hexdigest()
        assert anna.upload(f"/v1/submissions/{sid}/packages/{sha_free}", paid).json()["error"] == "hash_mismatch"
        assert anna.upload(f"/v1/submissions/{sid}/packages/{'0' * 64}", free).json()["error"] == "no_such_package"
        assert anna.upload(f"/v1/submissions/{sid}/packages/{sha_free}", b"x" * (1 << 20) + b"y").status_code == 413
        bob = register(client, "Bob")
        assert bob.upload(f"/v1/submissions/{sid}/packages/{sha_free}", free).status_code == 404
        assert client.post(f"/v1/submissions/{sid}/packages/{sha_free}", content=free).status_code == 401
        # The right bytes: one waiting still, then ok; the files under their hash.
        after = anna.upload(f"/v1/submissions/{sid}/packages/{sha_free.upper()}", free).json()
        assert after["checks"]["packages"] == "waiting" and [p.get("uploaded", False) for p in after["packages"]] == [True, False]
        assert (tmp_path / "store" / (sha_free + ".zip")).read_bytes() == free
        after = anna.upload(f"/v1/submissions/{sid}/packages/{sha_paid}", paid).json()
        assert after["checks"]["packages"] == "ok" and after["state"] == "checked"
        assert (tmp_path / "store" / (sha_paid + ".zip")).read_bytes() == paid
        assert anna.upload(f"/v1/submissions/{sid}/packages/{sha_free}", free).json()["checks"]["packages"] == "ok"   # again: harmless
        # The operator downloads the build; a stranger does not; a package not uploaded is 404.
        r = client.get(f"/v1/submissions/{sid}/packages/{sha_free}", headers=OP)
        assert r.status_code == 200 and r.content == free and r.headers["content-type"].startswith("application/octet-stream")
        assert sha_free + ".zip" in r.headers.get("content-disposition", "")
        assert client.get(f"/v1/submissions/{sid}/packages/{sha_free}").status_code == 401
        assert client.get(f"/v1/submissions/{sid}/packages/{'0' * 64}", headers=OP).status_code == 404
        # Released and published: the packages beside the catalog, the catalog naming them; the restricted one under restricted/.
        assert client.post(f"/v1/submissions/{sid}/decide", json={"action": "release"}, headers=OP).json()["state"] == "released"
        report = client.post("/v1/publish", headers=OP).json()
        assert report["packages"] == 2 and any(f.endswith("packages/" + sha_free + ".zip") for f in report["files"])
        catalog = json.loads((tmp_path / "publish" / "catalog.json").read_bytes())
        urls = [p["url"] for p in catalog["apps"][0]["releases"][0]["packages"]]
        assert urls == ["https://store.example/v1/packages/" + sha_free + ".zip", "https://store.example/v1/restricted/" + sha_paid + ".zip"]
        assert (tmp_path / "publish" / "packages" / (sha_free + ".zip")).read_bytes() == free
        assert (tmp_path / "publish" / "restricted" / (sha_paid + ".zip")).read_bytes() == paid
        # The stored submission still carries the merchant's URLs; a released submission takes no more packages.
        assert client.get(f"/v1/submissions/{sid}", headers=OP).json()["component"]["releases"][0]["packages"][0]["url"].startswith("https://anna.example/")
        assert anna.upload(f"/v1/submissions/{sid}/packages/{sha_free}", free).status_code == 409
        # A new submission naming bytes already in the store needs no upload.
        comp2 = component("org.example.hello", "1.1.0")
        comp2["releases"][0]["packages"] = comp["releases"][0]["packages"][:1]
        sub2 = anna.request("POST", "/v1/submissions", {"component": comp2, "signature": anna.sign_component(comp2), "packages": packages[:1]}).json()
        assert sub2["checks"]["packages"] == "ok" and sub2["packages"][0]["uploaded"] is True
        # The audit names the upload.
        assert [a["action"] for a in client.get("/v1/audit", headers=OP).json()["audit"]].count("upload_package") == 3
        assert client.get("/v1/audit/verify", headers=OP).json()["ok"] is True


def test_no_package_store_takes_no_upload(service):
    anna = register(service)
    comp = component("org.example.hello", "1.0.0")
    packages = [{"file": "packages/" + "a" * 64 + ".zip", "sha256": "a" * 64, "size": 10}]
    sub = anna.request("POST", "/v1/submissions", {"component": comp, "signature": anna.sign_component(comp), "packages": packages}).json()
    assert sub["checks"]["packages"] == "ok"
    assert anna.upload(f"/v1/submissions/{sub['id']}/packages/{'a' * 64}", b"0123456789").status_code == 503


def test_an_operator_turns_on_two_factor_and_resets_a_forgotten_password(service):
    service.post("/v1/operators", json={"email": "alex@ultra-os.example", "name": "Alex", "role": "admin", "password": "a long enough password"}, headers=OP)
    service.post("/v1/operators", json={"email": "sam@ultra-os.example", "name": "Sam", "role": "support", "password": "another long password"}, headers=OP)
    token = service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password"}).json()["token"]
    alex = {"Authorization": "Bearer " + token}
    # The static token has no account to set a second factor on.
    assert service.post("/v1/operators/me/two-factor/setup", headers=OP).json()["error"] == "bootstrap"
    # Setup, a wrong code, the right one: on, with the recovery codes shown once.
    setup = service.post("/v1/operators/me/two-factor/setup", headers=alex).json()
    assert len(setup["secret"]) == 32 and setup["otpauth_url"].startswith("otpauth://totp/UltraStore%20operations:alex%40ultra-os.example?")
    assert service.post("/v1/operators/me/two-factor/enable", json={"code": "000000"}, headers=alex).json()["error"] == "bad_code"
    enabled = service.post("/v1/operators/me/two-factor/enable", json={"code": totp_code(setup["secret"])}, headers=alex).json()
    assert enabled["two_factor"] is True and len(enabled["recovery_codes"]) == 8
    me = service.get("/v1/operators", headers=alex).json()["operators"][0]
    assert me["two_factor"] is True and me["recovery_codes_left"] == 8
    assert service.post("/v1/operators/me/two-factor/setup", headers=alex).json()["error"] == "two_factor_on"
    # The sign-in now wants the code; a recovery code works once.
    r = service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password"})
    assert r.status_code == 401 and r.json()["error"] == "two_factor_required"
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password", "totp": "000000"}).status_code == 401
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password", "totp": totp_code(setup["secret"])}).status_code == 200
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password", "recovery_code": enabled["recovery_codes"][0]}).status_code == 200
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password", "recovery_code": enabled["recovery_codes"][0]}).status_code == 401
    assert service.get("/v1/operators", headers=alex).json()["operators"][0]["recovery_codes_left"] == 7
    fresh = service.post("/v1/operators/me/two-factor/recovery-codes", json={"code": totp_code(setup["secret"])}, headers=alex).json()["recovery_codes"]
    assert len(fresh) == 8 and fresh[0] not in enabled["recovery_codes"]
    # Forgotten password: a code by mail (the answer gives nothing away), the reset with the second factor, the old password gone.
    assert service.post("/v1/operators/password/forgot", json={"email": "nobody@ultra-os.example"}).json()["sent"] is True
    assert len(service.mails) == 0
    assert service.post("/v1/operators/password/forgot", json={"email": "ALEX@ultra-os.example"}).json()["sent"] is True
    to, subject, text = service.mails[-1]
    assert to == "alex@ultra-os.example" and "reset code" in subject
    code = text.split("Your code is ")[1][:6]
    assert service.post("/v1/operators/password/reset", json={"email": "alex@ultra-os.example", "code": "000000", "new_password": "a brand new password"}).json()["error"] == "bad_code"
    r = service.post("/v1/operators/password/reset", json={"email": "alex@ultra-os.example", "code": code, "new_password": "a brand new password"})
    assert r.status_code == 401 and r.json()["error"] == "two_factor_required"
    # The code was used up by the attempt that reached the second factor: ask for a new one.
    service.post("/v1/operators/password/forgot", json={"email": "alex@ultra-os.example"})
    code = service.mails[-1][2].split("Your code is ")[1][:6]
    r = service.post("/v1/operators/password/reset", json={"email": "alex@ultra-os.example", "code": code, "new_password": "a brand new password", "totp": totp_code(setup["secret"])})
    assert r.status_code == 200, r.text
    assert service.mails[-1][1].endswith("your password was changed")
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a long enough password", "totp": totp_code(setup["secret"])}).status_code == 401
    assert service.post("/v1/operators/signin", json={"email": "alex@ultra-os.example", "password": "a brand new password", "totp": totp_code(setup["secret"])}).status_code == 200
    assert service.post("/v1/operators/password/reset", json={"email": "alex@ultra-os.example", "code": code, "new_password": "a brand new password"}).json()["error"] == "bad_code"   # used
    # An admin resets another's second factor, never their own; Sam's own two-factor request from the static token is refused.
    sam = {"Authorization": "Bearer " + service.post("/v1/operators/signin", json={"email": "sam@ultra-os.example", "password": "another long password"}).json()["token"]}
    sam_setup = service.post("/v1/operators/me/two-factor/setup", headers=sam).json()
    service.post("/v1/operators/me/two-factor/enable", json={"code": totp_code(sam_setup["secret"])}, headers=sam)
    assert service.put("/v1/operators/op-0001", json={"two_factor": False}, headers=sam).status_code == 403
    assert service.put("/v1/operators/op-0001", json={"two_factor": False}, headers=alex).status_code == 400   # one's own
    assert service.put("/v1/operators/op-0002", json={"two_factor": False}, headers=alex).json()["two_factor"] is False
    assert service.post("/v1/operators/signin", json={"email": "sam@ultra-os.example", "password": "another long password"}).status_code == 200
    # Off again with the code.
    assert service.post("/v1/operators/me/two-factor/disable", json={"code": totp_code(setup["secret"])}, headers=alex).json()["two_factor"] is False
    actions = [a["action"] for a in service.get("/v1/audit", headers=OP).json()["audit"]]
    assert {"enable_two_factor", "new_recovery_codes", "reset_password", "reset_two_factor", "disable_two_factor"} <= set(actions)
    assert service.get("/v1/audit/verify", headers=OP).json()["ok"] is True


def _zip(path, members: dict, *, symlink: dict | None = None, executable: tuple = ()):
    """A zip with the members given; a symlink member points where the dict says."""
    import zipfile
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in members.items():
            info = zipfile.ZipInfo(name)
            info.external_attr = (0o100755 if name in executable else 0o100644) << 16
            z.writestr(info, data)
        for name, target in (symlink or {}).items():
            info = zipfile.ZipInfo(name)
            info.external_attr = 0o120777 << 16
            z.writestr(info, target)
    return path.read_bytes()


def _listed(client, anna, comp, raw, *, access=""):
    sha = hashlib.sha256(raw).hexdigest()
    comp = json.loads(json.dumps(comp))
    comp["releases"][0]["packages"][0].update({"sha256": sha, "size": len(raw), "url": f"https://anna.example/packages/{sha}.zip"})
    entry = {"file": f"packages/{sha}.zip", "sha256": sha, "size": len(raw), "format": "zip"}
    if access:
        entry["access"] = access
    sub = anna.request("POST", "/v1/submissions", {"component": comp, "signature": anna.sign_component(comp), "packages": [entry]}).json()
    assert sub["checks"]["packages"] in ("waiting", "ok"), sub
    if sub["checks"]["packages"] == "waiting":
        sub = anna.upload(f"/v1/submissions/{sub['id']}/packages/{sha}", raw).json()
    return sub


def test_the_review_pipeline_scans_the_archives_finds_the_entry_and_checks_the_listing(tmp_path):
    scanner = tmp_path / "scan.sh"
    scanner.write_text("#!/bin/sh\nif grep -q EICAR \"$1\"; then echo \"$1: Eicar-Test-Signature FOUND\"; exit 1; fi\nexit 0\n")
    runner = tmp_path / "smoke.sh"
    runner.write_text("#!/bin/sh\ncase \"$2\" in bin/hello|hello|Hello.exe|Hello.app) echo \"window shown after 2 s\"; exit 0;; esac\necho \"no window\" >&2; exit 3\n")
    scanner.chmod(0o755); runner.chmod(0o755)
    settings = Settings(database="sqlite:///" + str(tmp_path / "m.sqlite3"), operator_token="op-token", publish_dir=str(tmp_path / "publish"),
                        package_dir=str(tmp_path / "store"), catalog_url="https://store.example/v1", checks_inline=True,
                        scan_command=f"{scanner} {{file}}", smoke_command=f"{runner} {{file}} {{entry}}", payment_url="https://pay.example",
                        licence_key="ab" * 32, public_base_url="https://market.example")
    with TestClient(create_app(settings)) as client:
        anna = register(client)
        good = {"id": "org.example.hello", "name": "Hello", "publisher": "anna-co", "summary": {"en": "Says hello", "de": "Sagt hallo"},
                "description": {"en": "A longer text."}, "categories": ["utilities"], "icon": "media/org.example.hello/icon.png",
                "screenshots": ["media/org.example.hello/1.png"], "pricing": {"model": "free"}, "age_rating": "3", "homepage": "https://anna.example",
                "releases": [{"version": "1.0.0", "channel": "stable", "packages": [{"os": "linux", "arch": "x86_64", "format": "zip", "entry": "bin/hello"}]}]}
        # A clean archive with the named entry: every check passes, the findings say what was seen.
        raw = _zip(tmp_path / "good.zip", {"README": "hi", "bin/hello": "#!/bin/sh\necho hello\n", "share/data.txt": "x" * 1000}, executable=("bin/hello",))
        sub = _listed(client, anna, good, raw)
        assert sub["state"] == "checked", sub
        assert {k: sub["checks"][k] for k in ("scans", "entry", "smoke", "listing")} == {"scans": "ok", "entry": "ok", "smoke": "ok", "listing": "ok"}
        assert any("virus scanner found nothing" in f for f in sub["findings"]["scans"])
        assert sub["findings"]["entry"] == [f"linux/x86_64 {hashlib.sha256(raw).hexdigest()[:12]}: the entry bin/hello is in the archive"]
        assert any("window shown" in f for f in sub["findings"]["smoke"])
        assert "summary in de, en" in sub["findings"]["listing"] and "1 screenshot(s)" in sub["findings"]["listing"]
        # A release is allowed; the operator may run the pipeline again, and it is audited.
        assert client.post(f"/v1/submissions/{sub['id']}/checks", json={}, headers=OP).json()["checks"]["scans"] == "ok"
        # No entry named: found by the rules (the launcher beside bin/); a Windows exe likewise.
        unnamed = json.loads(json.dumps(good)); del unnamed["releases"][0]["packages"][0]["entry"]
        raw2 = _zip(tmp_path / "unnamed.zip", {"hello": "#!/bin/sh\n", "bin/hello": "x"}, executable=("hello", "bin/hello"))
        sub2 = _listed(client, anna, unnamed, raw2)
        assert sub2["checks"]["entry"] == "ok" and "hello found by the store's rules" in sub2["findings"]["entry"][0]
        windows = json.loads(json.dumps(unnamed)); windows["releases"][0]["packages"][0]["os"] = "windows"
        raw3 = _zip(tmp_path / "win.zip", {"Hello/Uninstall.exe": "MZ", "Hello/Hello.exe": "MZ", "Hello/data/x.dll": "MZ"})
        sub3 = _listed(client, anna, windows, raw3)
        assert sub3["checks"]["entry"] == "ok" and "Hello/Hello.exe found" in sub3["findings"]["entry"][0]
        # The named entry missing: failed_checks, which cannot be released.
        wrong = json.loads(json.dumps(good)); wrong["releases"][0]["packages"][0]["entry"] = "bin/nope"
        sub4 = _listed(client, anna, wrong, _zip(tmp_path / "wrong.zip", {"bin/hello": "x"}, executable=("bin/hello",)))
        assert sub4["state"] == "failed_checks" and sub4["checks"]["entry"] == "failed"
        assert "the entry bin/nope is not in the archive (found bin/hello instead)" in sub4["findings"]["entry"][0]
        assert client.post(f"/v1/submissions/{sub4['id']}/decide", json={"action": "release"}, headers=OP).status_code == 409
        # The scans: a path that escapes, a link outside, an installer, and the scanner's find.
        sub5 = _listed(client, anna, good, _zip(tmp_path / "slip.zip", {"../etc/cron.d/x": "bad", "bin/hello": "x"}))
        assert sub5["checks"]["scans"] == "failed" and any("would escape the install folder" in f for f in sub5["findings"]["scans"])
        sub6 = _listed(client, anna, good, _zip(tmp_path / "link.zip", {"bin/hello": "x"}, symlink={"etc": "/etc"}))
        assert sub6["checks"]["scans"] == "failed" and any("points outside the archive" in f for f in sub6["findings"]["scans"])
        sub7 = _listed(client, anna, good, _zip(tmp_path / "installer.zip", {"setup.exe": "MZ", "bin/hello": "x"}))
        assert sub7["checks"]["scans"] == "failed" and any("is an installer" in f for f in sub7["findings"]["scans"])
        sub8 = _listed(client, anna, good, _zip(tmp_path / "virus.zip", {"bin/hello": "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*"}))
        assert sub8["checks"]["scans"] == "failed" and any("Eicar-Test-Signature FOUND" in f for f in sub8["findings"]["scans"])
        assert sub8["state"] == "failed_checks"
        # The smoke run failing is a finding the reviewer weighs, not a block.
        other = json.loads(json.dumps(good)); other["releases"][0]["packages"][0]["entry"] = "bin/other"
        sub9 = _listed(client, anna, other, _zip(tmp_path / "other.zip", {"bin/other": "x"}, executable=("bin/other",)))
        assert sub9["checks"]["smoke"] == "failed" and "no window" in sub9["findings"]["smoke"][0] and sub9["state"] == "checked"
        # The listing: a paid app without a price, a bad age rating, a URL that is not one, a restricted package on a trial.
        bad = json.loads(json.dumps(good))
        bad.update({"summary": {}, "pricing": {"model": "one-time", "trial_days": 14}, "age_rating": "21", "support_url": "mailto:x", "categories": []})
        sub10 = _listed(client, anna, bad, _zip(tmp_path / "bad.zip", {"bin/hello": "x"}, executable=("bin/hello",)), access="restricted")
        assert sub10["checks"]["listing"] == "failed" and sub10["state"] == "checked"   # the listing does not block
        lines = "\n".join(sub10["findings"]["listing"])
        for expected in ("no summary", "no category", "without a price and a currency", "age rating '21'", "support_url is not an http(s) URL",
                         "a restricted package on an app with a trial"):
            assert expected in lines, lines
        # A tarball with a link that stays inside is fine; a 7z without py7zr is not run, said so.
        import tarfile
        tgz = tmp_path / "ok.tar.gz"
        with tarfile.open(tgz, "w:gz") as t:
            data = b"#!/bin/sh\n"
            info = tarfile.TarInfo("app/bin/hello"); info.size = len(data); info.mode = 0o755
            t.addfile(info, io.BytesIO(data))
            link = tarfile.TarInfo("app/hello"); link.type = tarfile.SYMTYPE; link.linkname = "bin/hello"
            t.addfile(link)
        tar_comp = json.loads(json.dumps(good)); tar_comp["releases"][0]["packages"][0].update({"format": "tar.gz", "entry": "app/bin/hello"})
        sha = hashlib.sha256(tgz.read_bytes()).hexdigest()
        tar_comp["releases"][0]["packages"][0].update({"sha256": sha, "size": tgz.stat().st_size, "url": f"https://anna.example/packages/{sha}.tar.gz"})
        sub11 = anna.request("POST", "/v1/submissions", {"component": tar_comp, "signature": anna.sign_component(tar_comp),
                                                        "packages": [{"file": f"packages/{sha}.tar.gz", "sha256": sha, "size": tgz.stat().st_size, "format": "tar.gz"}]}).json()
        sub11 = anna.upload(f"/v1/submissions/{sub11['id']}/packages/{sha}", tgz.read_bytes()).json()
        assert sub11["checks"]["scans"] == "ok" and sub11["checks"]["entry"] == "ok", sub11["findings"]
        assert client.get("/v1/audit/verify", headers=OP).json()["ok"] is True


def test_the_pipeline_helpers():
    from ultrastore_market.checks import Entry, find_entry, link_escapes, unsafe_path
    assert unsafe_path("/etc/passwd") == "an absolute path" and unsafe_path("C:\\x") == "an absolute path"
    assert unsafe_path("a/../../b") == "a path with .." and unsafe_path("a/b") == ""
    assert link_escapes("a/b/link", "../../../x") and not link_escapes("a/b/link", "../c") and link_escapes("x", "/etc")
    mac = [Entry("Hello.app/Contents/MacOS/Hello"), Entry("Hello.app/Contents/Info.plist")]
    assert find_entry(mac, "macos") == "Hello.app"
    assert find_entry([Entry("Hello-1.0.AppImage", executable=True)], "linux") == "Hello-1.0.AppImage"
    assert find_entry([Entry("readme.txt")], "linux") == ""
