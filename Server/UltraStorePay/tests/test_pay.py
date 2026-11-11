"""The payment service end to end, through FastAPI's test client: a signed
catalog served from memory, a checkout with a code, the Quick Checkout
page, Skrill's status report (good and forged), the licence, the receipt,
the signed download URL, and the sales the admin backend fetches."""

from __future__ import annotations

import hashlib
import json
import os

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from fastapi.testclient import TestClient

from ultrastore_pay.app import add_cycle, add_days, create_app
from ultrastore_pay.catalog import cents, decimal, discounted, parse_catalog
from ultrastore_pay.config import Settings
from ultrastore_pay.licence import verify
from ultrastore_pay.skrill import SkrillApi, SkrillApiError, parse_response, status_signature
from ultrastore_pay.store import Store

CATALOG = {
    "schema": 2, "sequence": 7,
    "codes": [
        {"code": "ANNA-20", "kind": "affiliate", "discount_percent": 20, "apps": ["org.ultraos.video"], "valid_until": "2099-12-31", "affiliate": "aff-anna"},
        {"code": "OLD", "kind": "discount", "discount_percent": 50, "valid_until": "2020-01-01"},
        {"code": "FIVE", "kind": "discount", "discount_amount": "5", "currency": "EUR"},
    ],
    "apps": [
        {"id": "org.ultraos.video", "name": "UltraVideo", "pricing": {"model": "one-time", "price": "29.90", "currency": "EUR", "trial_days": 14},
         "updates": {"period": "until", "until": "2027-12-31"},
         "releases": [{"version": "1.0.0", "channel": "stable", "packages": [
             {"os": "linux", "arch": "x86_64", "format": "zip", "url": "https://store.example/v1/restricted/" + "a" * 64 + ".zip", "sha256": "a" * 64, "size": 10}]}]},
        {"id": "org.ultraos.hello", "name": "Hello", "pricing": {"model": "free"}, "releases": []},
        {"id": "org.ultraos.cloud", "name": "UltraCloud Plus", "pricing": {"model": "monthly", "price": "4.90", "currency": "EUR"},
         "updates": {"period": "always"}, "releases": []},
    ],
}


class FakeSkrillApi:
    """Skrill's Automated Payments Interface as the tests see it: every
    call recorded, the refund prepared and executed, the recurring payment
    cancelled, or a refusal when told to refuse."""

    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []
        self.refuse = ""
        self.next_transaction = 900

    def __call__(self, url: str, fields: dict) -> str:
        self.calls.append((url, dict(fields)))
        if self.refuse:
            return f"<response><error><error_msg>{self.refuse}</error_msg></error></response>"
        if url.endswith("/refund.pl") and fields["action"] == "prepare":
            return "<response><sid>sid-" + fields["mb_transaction_id"] + "</sid></response>"
        if url.endswith("/refund.pl") and fields["action"] == "refund":
            self.next_transaction += 1
            return f"<response><status>2</status><mb_transaction_id>{self.next_transaction}</mb_transaction_id></response>"
        if url.endswith("/query.pl") and fields["action"] == "cancel_rec_payment":
            return "<response><status>OK</status></response>"
        return "<response><error><error_msg>unknown action</error_msg></error></response>"


def database_urls() -> list[str]:
    """SQLite always (tests and development), and PostgreSQL and MySQL when
    the environment names one: ULTRASTORE_PAY_TEST_PG_URL=postgresql://...,
    ULTRASTORE_PAY_TEST_MYSQL_URL=mysql://... - the production engines, so
    the whole purchase runs on them too."""
    urls = ["sqlite"]
    if os.environ.get("ULTRASTORE_PAY_TEST_PG_URL"):
        urls.append("postgresql")
    if os.environ.get("ULTRASTORE_PAY_TEST_MYSQL_URL"):
        urls.append("mysql")
    return urls


def database_url(kind: str, tmp_path) -> str:
    if kind in ("postgresql", "mysql"):
        url = os.environ["ULTRASTORE_PAY_TEST_PG_URL" if kind == "postgresql" else "ULTRASTORE_PAY_TEST_MYSQL_URL"]
        # A fresh schema per test: the tables are dropped first.
        from ultrastore_db import Database
        db = Database(url)
        db.script(["DROP TABLE IF EXISTS payments", "DROP TABLE IF EXISTS orders", "DROP TABLE IF EXISTS meta"])
        db.close()
        return url
    return "sqlite:///" + str(tmp_path / "pay.sqlite3")


@pytest.fixture(params=database_urls())
def service(request, tmp_path):
    signing = Ed25519PrivateKey.generate()
    body = json.dumps(CATALOG).encode()
    files = {"https://store.example/v1/catalog.json": body,
             "https://store.example/v1/catalog.json.sig": signing.sign(body).hex().encode() + b"\n"}
    settings = Settings(database=database_url(request.param, tmp_path), public_base_url="https://pay.example", skrill_pay_to_email="shop@example.org",
                        skrill_secret_word="word", skrill_merchant_id="12345", catalog_url="https://store.example/v1",
                        catalog_keys=[signing.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()], admin_token="admin-token",
                        restricted_secret="nginx-secret", restricted_base_url="https://store.example/v1", skrill_api_password="api-pass")
    api = FakeSkrillApi()
    app = create_app(settings, fetcher=lambda url: files[url], api_call=api)
    app.state.fake_api = api
    with TestClient(app) as client:
        yield client, settings, files, signing


def sig(settings: Settings, order_id: str, amount: str, currency: str, status: str) -> str:
    return status_signature(settings.skrill_merchant_id, order_id, settings.skrill_secret_word, amount, currency, status)


def test_prices_and_codes():
    assert cents("29.90") == 2990 and cents("5") == 500 and cents("3.5") == 350 and cents("x") is None and cents("") is None
    assert decimal(2392) == "23.92"
    cat = parse_catalog(json.dumps(CATALOG).encode())
    video = cat.apps["org.ultraos.video"]
    assert video.restricted_files == ["restricted/" + "a" * 64 + ".zip"]
    assert discounted(video.pricing, cat.find_code(" anna-20 ")) == (2392, True)
    assert discounted(video.pricing, cat.find_code("five")) == (2490, True)
    assert discounted(video.pricing, None) == (2990, False)
    assert not cat.find_code("OLD").valid_on("2026-10-27")


def test_checkout_status_licence_receipt_and_sales(service):
    client, settings, files, signing = service
    # A free app and an unknown app are not for sale.
    assert client.post("/v1/checkout", json={"app": "org.ultraos.hello"}).status_code == 400
    assert client.post("/v1/checkout", json={"app": "org.ultraos.nope"}).status_code == 404
    # The checkout with the partner's code: the discounted price, the page to Skrill.
    r = client.post("/v1/checkout", json={"app": "org.ultraos.video", "code": "anna-20"})
    assert r.status_code == 200, r.text
    order = r.json()
    assert order["amount"] == "23.92" and order["currency"] == "EUR" and order["code"] == "ANNA-20" and order["code_applied"]
    assert order["checkout_url"] == f"https://pay.example/pay/{order['order_id']}"
    page = client.get(f"/pay/{order['order_id']}")
    assert page.status_code == 200
    # The shared frame: the stylesheet and the mark served by the service, the button for a browser without scripts.
    assert '<link rel="stylesheet" href="/pay/static/style.css">' in page.text and "/pay/static/logo-light.png" in page.text
    assert "Continue to Skrill" in page.text and "<h1>UltraVideo</h1>" in page.text
    css = client.get("/pay/static/style.css")
    assert css.status_code == 200 and css.headers["content-type"].startswith("text/css") and "--accent" in css.text
    logo = client.get("/pay/static/logo-dark.png")
    assert logo.status_code == 200 and logo.headers["content-type"] == "image/png" and logo.content[:4] == b"\x89PNG"
    assert client.get("/pay/static/other.png").status_code == 404
    assert 'name="pay_to_email" value="shop@example.org"' in page.text
    assert 'name="amount" value="23.92"' in page.text
    assert 'name="status_url" value="https://pay.example/v1/skrill/status"' in page.text
    assert f'name="transaction_id" value="{order["order_id"]}"' in page.text
    # An expired code does not apply; the order still carries it for the ledger.
    stale = client.post("/v1/checkout", json={"app": "org.ultraos.video", "code": "old"}).json()
    assert stale["amount"] == "29.90" and stale["code"] == "OLD" and not stale["code_applied"]
    # Before the status report: open, no licence; the wrong secret is a 404.
    assert client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()["status"] == "open"
    assert client.get(f"/v1/orders/{order['order_id']}", params={"secret": "nope"}).status_code == 404
    assert client.get(f"/v1/orders/{order['order_id']}/receipt", params={"secret": order["secret"]}).status_code == 409
    # A forged status report changes nothing.
    report = {"transaction_id": order["order_id"], "mb_transaction_id": "SK-1", "mb_amount": "23.92", "mb_currency": "EUR", "status": "2",
              "pay_from_email": "buyer@example.org", "md5sig": "0" * 32}
    assert client.post("/v1/skrill/status", data=report).status_code == 403
    assert client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()["status"] == "open"
    # A real one for another amount is refused and noted.
    wrong = {**report, "mb_amount": "1.00", "md5sig": sig(settings, order["order_id"], "1.00", "EUR", "2")}
    assert client.post("/v1/skrill/status", data=wrong).status_code == 400
    # The real one: paid, the licence issued and verifiable with the public key, the receipt numbered.
    report["md5sig"] = sig(settings, order["order_id"], "23.92", "EUR", "2")
    assert client.post("/v1/skrill/status", data=report).text == "OK"
    paid = client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()
    assert paid["status"] == "paid" and paid["receipt_no"] == 1 and paid["licence"].startswith("ULIC1.")
    public = client.get("/v1/keys").json()["licence_public_key"]
    payload = verify(paid["licence"], public)
    assert payload["updates"] == "until" and payload["updates_until"] == "2027-12-31"
    assert payload and payload["app"] == "org.ultraos.video" and payload["order"] == order["order_id"] and payload["licence"] == paid["licence_id"]
    assert verify(paid["licence"][:-4] + "AAAA", public) is None
    # Skrill repeats a report it got no answer to: still OK, nothing issued twice.
    assert client.post("/v1/skrill/status", data=report).text == "OK"
    assert client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()["receipt_no"] == 1
    receipt = client.get(f"/v1/orders/{order['order_id']}/receipt", params={"secret": order["secret"]})
    assert receipt.status_code == 200 and "receipt no. 1" in receipt.text and "Code: ANNA-20 applied" in receipt.text and "23.92 EUR" in receipt.text
    assert receipt.headers["content-type"].startswith("text/plain")
    # A browser asks for text/html first and gets the receipt as a page.
    shown = client.get(f"/v1/orders/{order['order_id']}/receipt", params={"secret": order["secret"]},
                       headers={"Accept": "text/html,application/xhtml+xml,*/*;q=0.8"})
    assert shown.headers["content-type"].startswith("text/html") and "<h1>Receipt no. 1</h1>" in shown.text
    assert "ANNA-20 applied" in shown.text and "23.92 EUR" in shown.text and 'class="receipt"' in shown.text
    # The return page says so; the pay page no longer sends to Skrill.
    assert "is yours" in client.get(f"/pay/{order['order_id']}/return").text
    assert "Already paid" in client.get(f"/pay/{order['order_id']}").text
    # A signed download URL for the app's restricted package, nginx secure_link_md5 style.
    bad = client.post(f"/v1/orders/{order['order_id']}/download", params={"secret": order["secret"], "file": "restricted/nope.zip"})
    assert bad.status_code == 404
    good = client.post(f"/v1/orders/{order['order_id']}/download", params={"secret": order["secret"], "file": "restricted/" + "a" * 64 + ".zip"}).json()
    assert good["url"].startswith("https://store.example/v1/restricted/" + "a" * 64 + ".zip?md5=") and "&expires=" in good["url"]
    # The stale order fails, the sales list has the paid one only, after a sequence the admin remembers.
    failed = {"transaction_id": stale["order_id"], "mb_transaction_id": "SK-2", "mb_amount": "29.90", "mb_currency": "EUR", "status": "-2",
              "failed_reason_code": "02", "md5sig": sig(settings, stale["order_id"], "29.90", "EUR", "-2")}
    assert client.post("/v1/skrill/status", data=failed).text == "OK"
    assert client.get(f"/v1/orders/{stale['order_id']}", params={"secret": stale["secret"]}).json()["status"] == "failed"
    assert client.get("/v1/sales").status_code == 401
    sales = client.get("/v1/sales", headers={"Authorization": "Bearer admin-token"}).json()
    assert len(sales["sales"]) == 1 and sales["sales"][0]["code"] == "ANNA-20" and sales["sales"][0]["amount"] == "23.92" and sales["next"] == 1
    assert sales["sales"][0]["kind"] == "sale" and sales["sales"][0]["reference"] == order["order_id"]
    # The licence stands, and the order carries its one payment.
    assert client.get(f"/v1/licences/{paid['licence_id']}").json()["status"] == "valid"
    assert client.get("/v1/licences/nope").status_code == 404
    assert [p["kind"] for p in paid["payments"]] == ["sale"] and paid["payments"][0]["receipt_no"] == 1
    assert client.get("/v1/sales", params={"since": 1}, headers={"Authorization": "Bearer admin-token"}).json()["sales"] == []
    # Cancel from Skrill's cancel page.
    other = client.post("/v1/checkout", json={"app": "org.ultraos.video"}).json()
    assert "Nothing was charged" in client.get(f"/pay/{other['order_id']}/cancel").text
    assert client.get(f"/v1/orders/{other['order_id']}", params={"secret": other["secret"]}).json()["status"] == "cancelled"


def test_the_catalog_must_verify_and_the_service_must_be_configured(tmp_path):
    signing = Ed25519PrivateKey.generate()
    other = Ed25519PrivateKey.generate()
    body = json.dumps(CATALOG).encode()
    files = {"https://store.example/v1/catalog.json": body, "https://store.example/v1/catalog.json.sig": other.sign(body).hex().encode()}
    settings = Settings(database=str(tmp_path / "pay.sqlite3"), skrill_pay_to_email="s@e.org", skrill_secret_word="w", skrill_merchant_id="1",
                        catalog_url="https://store.example/v1", catalog_keys=[signing.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()])
    with TestClient(create_app(settings, fetcher=lambda url: files[url])) as client:
        r = client.post("/v1/checkout", json={"app": "org.ultraos.video"})
        assert r.status_code == 503 and r.json()["error"] == "catalog_unavailable"
    unconfigured = Settings(database=str(tmp_path / "pay2.sqlite3"))
    with TestClient(create_app(unconfigured, fetcher=lambda url: files[url])) as client:
        assert client.get("/healthz").text.startswith("not configured")
        r = client.post("/v1/checkout", json={"app": "org.ultraos.video"})
        assert r.status_code == 503 and "ULTRASTORE_SKRILL_PAY_TO_EMAIL" in r.json()["error_description"]
        # The licence key made on the first start is kept for the next.
        first = client.get("/v1/keys").json()["licence_public_key"]
    with TestClient(create_app(unconfigured, fetcher=lambda url: files[url])) as client:
        assert client.get("/v1/keys").json()["licence_public_key"] == first


def test_status_signature_is_skrills_scheme():
    inner = hashlib.md5(b"word").hexdigest().upper()
    expected = hashlib.md5(("12345" + "ord" + inner + "23.92" + "EUR" + "2").encode()).hexdigest().upper()
    assert status_signature("12345", "ord", "word", "23.92", "EUR", "2") == expected


ADMIN = {"Authorization": "Bearer admin-token"}


def pay(client, settings, order, transaction, amount=None):
    report = {"transaction_id": order["order_id"], "mb_transaction_id": transaction, "mb_amount": amount or order["amount"], "mb_currency": "EUR",
              "status": "2", "pay_from_email": "buyer@example.org", "md5sig": sig(settings, order["order_id"], amount or order["amount"], "EUR", "2")}
    assert client.post("/v1/skrill/status", data=report).text == "OK"
    return client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()


def recurring(client, settings, order, transaction, status="2", amount="4.90", **more):
    report = {"transaction_id": order["order_id"], "mb_transaction_id": transaction, "rec_payment_id": "REC-1", "mb_amount": amount,
              "mb_currency": "EUR", "status": status, "md5sig": sig(settings, order["order_id"], amount, "EUR", status), **more}
    return client.post("/v1/skrill/recurring", data=report)


def test_dates():
    assert add_cycle("2026-01-31", "month") == "2026-02-28" and add_cycle("2026-12-15", "month") == "2027-01-15"
    assert add_cycle("2028-02-29", "year") == "2029-02-28" and add_cycle("2026-11-11", "year") == "2027-11-11"
    assert add_days("2026-12-28", 7) == "2027-01-04"


def test_a_subscription_is_paid_renewed_and_cancelled(service):
    client, settings, files, signing = service
    api = client.app.state.fake_api
    # The checkout names the recurring billing: the first payment, then the list price every month.
    r = client.post("/v1/checkout", json={"app": "org.ultraos.cloud", "code": "five"})
    assert r.status_code == 200, r.text
    order = r.json()
    assert order["cycle"] == "month" and order["amount"] == "0.00" and order["list_price"] == "4.90"
    page = client.get(f"/pay/{order['order_id']}").text
    assert 'name="rec_amount" value="4.90"' in page and 'name="rec_cycle" value="month"' in page and 'name="rec_period" value="1"' in page
    assert 'name="rec_status_url" value="https://pay.example/v1/skrill/recurring"' in page and "every month" in page
    # A renewal report before the first payment is refused.
    assert recurring(client, settings, order, "SK-R0").status_code == 409
    # Paid: the licence valid until the first period plus the grace days.
    paid = pay(client, settings, order, "SK-10")
    assert paid["status"] == "paid" and paid["subscription_status"] == "active"
    assert paid["paid_until"] == add_cycle(paid["paid_at"][:10], "month") and paid["valid_until"] == add_days(paid["paid_until"], 7)
    public = client.get("/v1/keys").json()["licence_public_key"]
    assert verify(paid["licence"], public)["valid_until"] == paid["valid_until"]
    assert "subscription renews every month" in client.get(f"/pay/{order['order_id']}/return").text
    # A renewal: the period extended from where the paid one ends, the licence re-issued with the same id, a second receipt.
    assert recurring(client, settings, order, "SK-11").text == "OK"
    renewed = client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()
    assert renewed["paid_until"] == add_cycle(paid["paid_until"], "month") and renewed["valid_until"] == add_days(renewed["paid_until"], 7)
    assert renewed["licence"] != paid["licence"] and verify(renewed["licence"], public)["licence"] == paid["licence_id"]
    assert verify(renewed["licence"], public)["valid_until"] == renewed["valid_until"]
    assert [p["kind"] for p in renewed["payments"]] == ["sale", "renewal"] and renewed["payments"][1]["receipt_no"] == 2
    # The repeat of the report changes nothing; a renewal for another amount is refused.
    assert recurring(client, settings, order, "SK-11").text == "OK"
    assert len(client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()["payments"]) == 2
    assert recurring(client, settings, order, "SK-12", amount="1.00").status_code == 400
    # The receipt of the renewal, by its number.
    text = client.get(f"/v1/orders/{order['order_id']}/receipt", params={"secret": order["secret"], "no": 2}).text
    assert "receipt no. 2" in text and "Kind: Renewal" in text and f"Paid until: {renewed['paid_until']}" in text and "Paid: 4.90 EUR" in text
    assert client.get(f"/v1/orders/{order['order_id']}/receipt", params={"secret": order["secret"], "no": 9}).status_code == 404
    # Both payments are in the sales feed, the renewal under its own reference.
    sales = client.get("/v1/sales", headers=ADMIN).json()["sales"]
    assert [s["kind"] for s in sales] == ["sale", "renewal"] and sales[1]["reference"] != sales[0]["reference"]
    assert sales[1]["reference"].startswith(order["order_id"] + "/") and sales[1]["amount"] == "4.90" and sales[1]["code"] == ""
    # A failed renewal is noted and the subscription is failing; the licence lasts through the grace days.
    assert recurring(client, settings, order, "SK-13", status="-2", failed_reason_code="07").text == "OK"
    failing = client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()
    assert failing["subscription_status"] == "failing" and failing["status"] == "paid" and failing["valid_until"] == renewed["valid_until"]
    # The buyer cancels from the store: Skrill is asked to end the recurring payment, the licence stands until the period ends.
    r = client.post(f"/v1/orders/{order['order_id']}/subscription/cancel", params={"secret": order["secret"]})
    assert r.status_code == 200, r.text
    assert r.json()["subscription_status"] == "cancelled" and r.json()["status"] == "paid"
    assert api.calls[-1][0].endswith("/query.pl") and api.calls[-1][1]["rec_payment_id"] == "REC-1" and api.calls[-1][1]["action"] == "cancel_rec_payment"
    assert client.get(f"/v1/licences/{paid['licence_id']}").json()["status"] == "valid"
    # Cancelling again asks Skrill nothing more.
    calls = len(api.calls)
    assert client.post(f"/v1/orders/{order['order_id']}/subscription/cancel", params={"secret": order["secret"]}).status_code == 200
    assert len(api.calls) == calls
    # A one-time purchase is no subscription to cancel.
    one = client.post("/v1/checkout", json={"app": "org.ultraos.video"}).json()
    pay(client, settings, one, "SK-20")
    assert client.post(f"/v1/orders/{one['order_id']}/subscription/cancel", params={"secret": one["secret"]}).status_code == 409


def test_a_refund_takes_the_licence_back_when_it_is_whole(service):
    client, settings, files, signing = service
    api = client.app.state.fake_api
    order = client.post("/v1/checkout", json={"app": "org.ultraos.video"}).json()
    assert client.post(f"/v1/orders/{order['order_id']}/refund", json={}).status_code == 401
    assert client.post(f"/v1/orders/{order['order_id']}/refund", json={}, headers=ADMIN).status_code == 409   # not paid
    paid = pay(client, settings, order, "SK-30")
    # Part of it: Skrill refunds the amount, the licence stands, the order remembers what went back.
    r = client.post(f"/v1/orders/{order['order_id']}/refund", json={"amount": "10.00", "note": "goodwill"}, headers=ADMIN)
    assert r.status_code == 200, r.text
    part = r.json()
    assert part["status"] == "paid" and part["refunded"] == "10.00" and "revoked" not in part and part["licence"] == paid["licence"]
    assert api.calls[0][1] == {"action": "prepare", "email": "shop@example.org", "password": hashlib.md5(b"api-pass").hexdigest(),
                               "mb_transaction_id": "SK-30", "amount": "10.00", "refund_note": "goodwill"}
    assert api.calls[1][1] == {"action": "refund", "sid": "sid-SK-30"}
    assert [p["kind"] for p in part["payments"]] == ["sale", "refund"] and part["payments"][1]["amount"] == "10.00" and part["payments"][1]["receipt_no"] == 2
    # More than is left is refused; a bad amount too.
    assert client.post(f"/v1/orders/{order['order_id']}/refund", json={"amount": "25.00"}, headers=ADMIN).status_code == 400
    assert client.post(f"/v1/orders/{order['order_id']}/refund", json={"amount": "x"}, headers=ADMIN).status_code == 400
    # Skrill refusing leaves everything as it was.
    api.refuse = "Transaction already refunded"
    r = client.post(f"/v1/orders/{order['order_id']}/refund", json={"amount": "1.00"}, headers=ADMIN)
    assert r.status_code == 502 and "already refunded" in r.json()["error_description"]
    api.refuse = ""
    # The rest: the licence is taken back, the downloads stop, the store sees it.
    rest = client.post(f"/v1/orders/{order['order_id']}/refund", json={"amount": "19.90"}, headers=ADMIN).json()
    assert rest["status"] == "refunded" and rest["revoked"] and rest["revoked_reason"] == "refund" and rest["refunded"] == "29.90" and "licence" not in rest
    assert client.get(f"/v1/licences/{paid['licence_id']}").json()["status"] == "revoked"
    assert client.post(f"/v1/orders/{order['order_id']}/download", params={"secret": order["secret"], "file": "restricted/" + "a" * 64 + ".zip"}).status_code == 409
    assert client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()["revoked"]
    # The receipt of a refund.
    text = client.get(f"/v1/orders/{order['order_id']}/receipt", params={"secret": order["secret"], "no": 3}).text
    assert "Kind: Refund" in text and "Refunded: 19.90 EUR" in text
    # The reversals feed names what each refund takes back; the sales feed still has the sale.
    reversals = client.get("/v1/reversals", headers=ADMIN).json()
    assert [r["kind"] for r in reversals["reversals"]] == ["refund", "refund"] and reversals["next"] == 2
    assert reversals["reversals"][0]["reverses"] == order["order_id"] and reversals["reversals"][0]["amount"] == "10.00"
    assert not reversals["reversals"][0]["revoked"] and reversals["reversals"][1]["revoked"]
    assert client.get("/v1/reversals", params={"since": 2}, headers=ADMIN).json()["reversals"] == []
    assert len(client.get("/v1/sales", headers=ADMIN).json()["sales"]) == 1
    # Refund of a renewal: its own transaction, and the licence kept when asked.
    sub = client.post("/v1/checkout", json={"app": "org.ultraos.cloud"}).json()
    pay(client, settings, sub, "SK-40")
    assert recurring(client, settings, sub, "SK-41").text == "OK"
    r = client.post(f"/v1/orders/{sub['order_id']}/refund", json={"transaction": "SK-41", "revoke": False}, headers=ADMIN)
    assert r.status_code == 200, r.text
    assert r.json()["status"] == "paid" and r.json()["refunded"] == "4.90" and api.calls[-2][1]["mb_transaction_id"] == "SK-41" and "amount" not in api.calls[-2][1]
    renewal_ref = client.get("/v1/sales", headers=ADMIN).json()["sales"][-1]["reference"]
    assert client.get("/v1/reversals", params={"since": 2}, headers=ADMIN).json()["reversals"][0]["reverses"] == renewal_ref
    # Without the API password there are no refunds from here.
    settings.skrill_api_password = ""
    client.app.state.fake_api.refuse = ""


def test_a_chargeback_takes_the_licence_back(service):
    client, settings, files, signing = service
    order = client.post("/v1/checkout", json={"app": "org.ultraos.video"}).json()
    paid = pay(client, settings, order, "SK-50")
    report = {"transaction_id": order["order_id"], "mb_transaction_id": "SK-50", "mb_amount": "29.90", "mb_currency": "EUR", "status": "-3",
              "md5sig": sig(settings, order["order_id"], "29.90", "EUR", "-3")}
    assert client.post("/v1/skrill/status", data=report).text == "OK"
    back = client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()
    assert back["status"] == "chargeback" and back["revoked"] and back["revoked_reason"] == "chargeback" and "licence" not in back
    assert [p["kind"] for p in back["payments"]] == ["sale", "chargeback"] and back["refunded"] == "29.90"
    assert client.get(f"/v1/licences/{paid['licence_id']}").json() == {"licence_id": paid["licence_id"], "app": "org.ultraos.video", "status": "revoked",
                                                                        "cycle": "", "valid_until": "", "revoked_reason": "chargeback"}
    # Skrill repeats it: nothing twice. A refund after a chargeback is refused by the amount left.
    assert client.post("/v1/skrill/status", data=report).text == "OK"
    assert len(client.get(f"/v1/orders/{order['order_id']}", params={"secret": order["secret"]}).json()["payments"]) == 2
    assert client.post(f"/v1/orders/{order['order_id']}/refund", json={}, headers=ADMIN).status_code == 400
    reversals = client.get("/v1/reversals", headers=ADMIN).json()["reversals"]
    assert len(reversals) == 1 and reversals[0]["kind"] == "chargeback" and reversals[0]["reverses"] == order["order_id"]
    # A chargeback of a subscription's renewal, reported to the recurring URL, ends it the same way.
    sub = client.post("/v1/checkout", json={"app": "org.ultraos.cloud"}).json()
    pay(client, settings, sub, "SK-60")
    assert recurring(client, settings, sub, "SK-61").text == "OK"
    assert recurring(client, settings, sub, "SK-61", status="-3").text == "OK"
    gone = client.get(f"/v1/orders/{sub['order_id']}", params={"secret": sub["secret"]}).json()
    assert gone["status"] == "chargeback" and gone["subscription_status"] == "cancelled" and gone["refunded"] == "4.90"
    # A renewal after the licence was taken back is not applied.
    assert recurring(client, settings, sub, "SK-62").text == "OK"
    assert client.get(f"/v1/orders/{sub['order_id']}", params={"secret": sub["secret"]}).json()["status"] == "chargeback"


def test_the_api_answers_are_parsed_and_the_refund_needs_both_steps():
    assert parse_response("<response><sid>abc</sid></response>") == {"sid": "abc"}
    with pytest.raises(SkrillApiError, match="Transaction not found"):
        parse_response("<response><error><error_msg>Transaction not found</error_msg></error></response>")
    with pytest.raises(SkrillApiError, match="not XML"):
        parse_response("nope")
    calls = []

    def call(url, fields):
        calls.append(fields["action"])
        return "<response><sid>s</sid></response>" if fields["action"] == "prepare" else "<response><status>0</status></response>"
    api = SkrillApi("https://www.skrill.com/app", "shop@example.org", "pw", call)
    assert api.configured and not SkrillApi("https://www.skrill.com/app", "shop@example.org", "", call).configured
    with pytest.raises(SkrillApiError, match="status 0"):
        api.refund("SK-1")
    assert calls == ["prepare", "refund"]


def test_a_version_1_database_is_brought_to_schema_2(tmp_path):
    import sqlite3
    path = str(tmp_path / "old.sqlite3")
    db = sqlite3.connect(path)
    db.executescript("""
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE orders (id TEXT PRIMARY KEY, secret TEXT NOT NULL, created_at TEXT NOT NULL, app TEXT NOT NULL, app_name TEXT NOT NULL,
          amount TEXT NOT NULL, currency TEXT NOT NULL, list_price TEXT NOT NULL, code TEXT NOT NULL DEFAULT '', code_applied INTEGER NOT NULL DEFAULT 0,
          status TEXT NOT NULL, skrill_transaction TEXT NOT NULL DEFAULT '', payer_email TEXT NOT NULL DEFAULT '', paid_at TEXT NOT NULL DEFAULT '',
          licence_id TEXT NOT NULL DEFAULT '', licence TEXT NOT NULL DEFAULT '', receipt_no INTEGER NOT NULL DEFAULT 0, notes TEXT NOT NULL DEFAULT '',
          seq INTEGER NOT NULL DEFAULT 0);
        INSERT INTO meta VALUES ('receipt_no', '2'), ('sales_seq', '2');
        INSERT INTO orders (id, secret, created_at, app, app_name, amount, currency, list_price, status, skrill_transaction, paid_at, licence_id, licence, receipt_no, seq)
          VALUES ('o1', 's1', '2026-11-01T00:00:00Z', 'org.ultraos.video', 'UltraVideo', '29.90', 'EUR', '29.90', 'paid', 'SK-1', '2026-11-01T00:01:00Z', 'lic1', 'ULIC1.x.y', 1, 1),
                 ('o2', 's2', '2026-11-02T00:00:00Z', 'org.ultraos.video', 'UltraVideo', '29.90', 'EUR', '29.90', 'paid', 'SK-2', '2026-11-02T00:01:00Z', 'lic2', 'ULIC1.x.z', 2, 2),
                 ('o3', 's3', '2026-11-03T00:00:00Z', 'org.ultraos.video', 'UltraVideo', '29.90', 'EUR', '29.90', 'open', '', '', '', '', 0, 0);
    """)
    db.commit()
    db.close()
    store = Store("sqlite:///" + path)
    assert store.meta("schema") == "2"
    sales = store.sales_since(0)
    assert [(p.order_id, p.kind, p.receipt_no, p.seq) for p, _ in sales] == [("o1", "sale", 1, 1), ("o2", "sale", 2, 2)]
    assert store.get_order("o1").payments[0].skrill_transaction == "SK-1" and store.get_order("o3").payments == []
    assert store.sales_since(2) == []
    # The next sale continues the counters; opening again upgrades nothing twice.
    store.close()
    again = Store("sqlite:///" + path)
    assert len(again.get_order("o1").payments) == 1
    paid = again.mark_paid("o3", skrill_transaction="SK-3", payer_email="", licence_id="lic3", licence="ULIC1.a.b")
    assert paid.receipt_no == 3 and paid.payments[0].seq == 3
