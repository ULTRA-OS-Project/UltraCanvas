"""The marketplace service end to end, through FastAPI's test client, on
SQLite and on whatever ULTRASTORE_MARKET_TEST_PG_URL and
ULTRASTORE_MARKET_TEST_MYSQL_URL name: a merchant registered and signing
its requests, the terms accepted and a merchant paused without them, a
submission checked, reviewed, released, resubmitted and withdrawn, the
reports through the three doors with the reporter kept from the merchant,
the notices and the strikes that suspend by the policy, the suspensions
and reinstatements, and the audit chain that shows an edit."""

from __future__ import annotations

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

OP = {"Authorization": "Bearer op-token"}
TABLES = ["audit", "suspensions", "notice_acks", "notices", "reports", "decisions", "submissions", "terms_acceptances", "terms_versions", "merchants", "meta"]


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


@pytest.fixture(params=database_urls())
def service(request, tmp_path):
    settings = Settings(database=database_url(request.param, tmp_path), operator_token="op-token", publish_dir=str(tmp_path / "publish"),
                        payment_url="https://pay.example", licence_key="ab" * 32, public_base_url="https://market.example",
                        trusted_keys=["cd" * 32])
    with TestClient(create_app(settings)) as client:
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
    assert good["checks"] == {"signature": "ok", "merchant_standing": "ok", "packages": "ok", "scans": "not run", "entry": "not run",
                              "smoke": "not run", "listing": "not run"}
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
    assert rel["sequence"] == 1 and [r["app"] for r in rel["released"]] == ["org.example.hello"] and rel["released"][0]["submission"] == sid
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
    assert [h["action"] for h in history] == ["register", "submit", "submit", "start_review", "release", "submit", "request_changes", "withdraw"]
    assert history[4]["after"] == {"state": "released", "message": "fine", "sequence": 1}
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
