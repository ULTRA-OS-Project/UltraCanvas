"""The payment service end to end, through FastAPI's test client: a signed
catalog served from memory, a checkout with a code, the Quick Checkout
page, Skrill's status report (good and forged), the licence, the receipt,
the signed download URL, and the sales the admin backend fetches."""

from __future__ import annotations

import hashlib
import json

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from fastapi.testclient import TestClient

from ultrastore_pay.app import create_app
from ultrastore_pay.catalog import cents, decimal, discounted, parse_catalog
from ultrastore_pay.config import Settings
from ultrastore_pay.licence import verify
from ultrastore_pay.skrill import status_signature

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
    ],
}


@pytest.fixture
def service(tmp_path):
    signing = Ed25519PrivateKey.generate()
    body = json.dumps(CATALOG).encode()
    files = {"https://store.example/v1/catalog.json": body,
             "https://store.example/v1/catalog.json.sig": signing.sign(body).hex().encode() + b"\n"}
    settings = Settings(database=str(tmp_path / "pay.sqlite3"), public_base_url="https://pay.example", skrill_pay_to_email="shop@example.org",
                        skrill_secret_word="word", skrill_merchant_id="12345", catalog_url="https://store.example/v1",
                        catalog_keys=[signing.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()], admin_token="admin-token",
                        restricted_secret="nginx-secret", restricted_base_url="https://store.example/v1")
    app = create_app(settings, fetcher=lambda url: files[url])
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
    assert receipt.status_code == 200 and "receipt no. 1" in receipt.text and "Code ANNA-20 applied" in receipt.text and "23.92 EUR" in receipt.text
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
