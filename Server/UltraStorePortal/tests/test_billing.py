"""Buying, end to end: an order from the store's client or the app's page,
the provider's signed report, the licence, a subscription's renewals, a
refund, a chargeback, and the signed link to a paid app's package."""

from __future__ import annotations

import datetime as dt
import importlib
import json
from decimal import Decimal

from django.core import mail
from django.test import RequestFactory, override_settings

from ultrastore_portal.billing import licence, skrill, split
from ultrastore_portal.billing import services as billing
from ultrastore_portal.billing.models import Order, Payment
from ultrastore_portal.store import catalog
from ultrastore_portal.web import pay

from .test_portal import ROOT, PortalCase

SKRILL = dict(PAYMENT_PROVIDER="skrill", SKRILL_PAY_TO_EMAIL="shop@ultraos.eu", SKRILL_SECRET_WORD="the secret word",
              SKRILL_MERCHANT_ID="4711", SKRILL_API_PASSWORD="api-password", RESTRICTED_SECRET="link-secret")


class BillingCase(PortalCase):
    provider = dict(PAYMENT_PROVIDER="test", RESTRICTED_SECRET="link-secret")

    def setUp(self):
        super().setUp()
        override = override_settings(**self.provider)
        override.enable()
        self.addCleanup(override.disable)
        self.publisher = self.make_publisher()
        self.app = self.make_app(self.publisher, "org.acme.paid", name="Paid", pricing_model="one-time", price="19.90", currency="EUR")
        self.package = self.make_release(self.app).packages.get()
        self.calls: list[tuple[str, dict]] = []
        self.answers: list[str] = []

        def fake_api(url, fields):
            self.calls.append((url, fields))
            return self.answers.pop(0)

        self.addCleanup(setattr, billing, "api_call", billing.api_call)
        billing.api_call = fake_api

    def checkout(self, app_id="org.acme.paid", **extra) -> dict:
        response = self.client.post("/v1/checkout", json.dumps({"app": app_id, **extra}), content_type="application/json")
        self.assertEqual(response.status_code, 200, response.content)
        return response.json()

    def order_json(self, order: dict) -> dict:
        return self.client.get(f"/v1/orders/{order['order_id']}?secret={order['secret']}").json()

    def report(self, order_id: str, status: str = "2", amount: str = "19.90", currency: str = "EUR", url: str = "/v1/skrill/status", **extra):
        """A status report as Skrill posts it, signed with the secret word."""
        fields = {"pay_to_email": "shop@ultraos.eu", "pay_from_email": "buyer@example.org", "merchant_id": "4711", "transaction_id": order_id,
                  "mb_transaction_id": extra.pop("mb_transaction_id", "900100"), "mb_amount": amount, "mb_currency": currency,
                  "amount": amount, "currency": currency, "status": status, **extra}
        fields["md5sig"] = extra.get("md5sig") or skrill.status_signature("4711", order_id, "the secret word", amount, currency, status)
        return self.client.post(url, fields)


class CheckoutTests(BillingCase):
    def test_the_catalog_names_the_store_as_the_payment_service_only_when_it_sells(self):
        body = self.catalog()
        self.assertEqual(body["payment_url"], ROOT)
        self.assertEqual(body["licence_key"], self.client.get("/v1/keys").json()["licence_public_key"])
        self.assertRegex(body["licence_key"], "^[0-9a-f]{64}$")
        with override_settings(PAYMENT_PROVIDER=""):
            snapshot, made = catalog.publish_catalog()
            body = json.loads(bytes(snapshot.body))
            self.assertTrue(made)
            self.assertNotIn("payment_url", body)
            self.assertNotIn("licence_key", body)
            refused = self.client.post("/v1/checkout", json.dumps({"app": "org.acme.paid"}), content_type="application/json")
            self.assertEqual((refused.status_code, refused.json()["error"]), (503, "not_configured"))
            self.assertNotContains(self.client.get("/apps/org.acme.paid/"), "Buy on the web")

    def test_a_purchase_from_the_store_app(self):
        order = self.checkout(code="FRIEND10")
        self.assertEqual((order["amount"], order["currency"], order["list_price"], order["status"], order["code_applied"]),
                         ("19.90", "EUR", "19.90", "open", False))
        self.assertEqual(order["checkout_url"], f"{ROOT}/pay/{order['order_id']}/")
        self.assertEqual(self.client.get(f"/v1/orders/{order['order_id']}?secret=wrong").status_code, 404)
        self.assertEqual(self.client.get(f"/v1/orders/{order['order_id']}").status_code, 404)
        self.assertNotIn("licence", self.order_json(order))
        # Nothing to fetch before it is paid.
        file = f"restricted/{self.package.file_name}"
        refused = self.client.post(f"/v1/orders/{order['order_id']}/download?secret={order['secret']}&file={file}")
        self.assertEqual((refused.status_code, refused.json()["error"]), (409, "not_paid"))

        self.assertContains(self.client.get(f"/pay/{order['order_id']}/"), "test payment provider")
        paid = self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        self.assertRedirects(paid, f"/pay/{order['order_id']}/return/", fetch_redirect_response=False)
        self.assertContains(self.client.get(f"/pay/{order['order_id']}/return/"), "Paid is yours")
        state = self.order_json(order)
        self.assertEqual((state["status"], state["receipt_no"], state["payments"][0]["kind"]), ("paid", 1, "sale"))
        payload = licence.verify(state["licence"], self.catalog()["licence_key"])
        self.assertEqual((payload["app"], payload["order"], payload["licence"], payload["updates"]),
                         ("org.acme.paid", order["order_id"], state["licence_id"], "always"))
        self.assertIsNone(licence.verify(state["licence"], "00" * 32))
        self.assertIsNone(licence.verify(state["licence"][:-4] + "AAAA", self.catalog()["licence_key"]))
        self.assertEqual(self.client.get(f"/v1/licences/{state['licence_id']}").json()["status"], "valid")

        # The package: a link the web server accepts for ten minutes, for this file alone.
        link = self.client.post(f"/v1/orders/{order['order_id']}/download?secret={order['secret']}&file={file}").json()
        path, _, query = link["url"].removeprefix(ROOT).partition("?")
        self.assertEqual(path, f"/v1/restricted/{self.package.file_name}")
        args = dict(pair.split("=") for pair in query.split("&"))
        self.assertTrue(billing.link_is_valid(path, args["md5"], args["expires"]))
        self.assertFalse(billing.link_is_valid(path + "x", args["md5"], args["expires"]))
        self.assertFalse(billing.link_is_valid(path, args["md5"], str(int(args["expires"]) + 60)))
        self.assertFalse(billing.link_is_valid(path, args["md5"], args["expires"], now=int(args["expires"]) + 1))
        other = self.client.post(f"/v1/orders/{order['order_id']}/download?secret={order['secret']}&file=restricted/{'0' * 64}.zip")
        self.assertEqual((other.status_code, other.json()["error"]), (404, "no_such_file"))
        # What nginx's secure link does, on a developer's machine.
        factory = RequestFactory()
        served = pay.restricted_file(factory.get(path, args), self.package.file_name)
        self.assertEqual((served.status_code, len(b"".join(served.streaming_content))), (200, self.package.size))
        self.assertEqual(pay.restricted_file(factory.get(path), self.package.file_name).status_code, 403)
        self.assertEqual(pay.restricted_file(factory.get(path, {**args, "md5": "forged"}), self.package.file_name).status_code, 403)

        receipt = self.client.get(f"/v1/orders/{order['order_id']}/receipt?secret={order['secret']}")
        self.assertIn("Receipt no. 1", receipt.content.decode())
        self.assertEqual(receipt["Content-Type"], "text/plain; charset=utf-8")
        page = self.client.get(f"/v1/orders/{order['order_id']}/receipt?secret={order['secret']}", headers={"Accept": "text/html"})
        self.assertContains(page, "19.90 EUR")

    def test_what_cannot_be_bought(self):
        free = self.make_app(self.publisher, "org.acme.free", name="Free")
        self.make_release(free)
        self.make_app(self.publisher, "org.acme.draft", name="Draft", pricing_model="one-time", price="5.00", currency="EUR")   # never published
        for app_id, status, code in (("org.acme.free", 400, "free_app"), ("org.acme.draft", 404, "no_such_app"), ("org.nobody.app", 404, "no_such_app")):
            response = self.client.post("/v1/checkout", json.dumps({"app": app_id}), content_type="application/json")
            self.assertEqual((response.status_code, response.json()["error"]), (status, code))
        self.assertEqual(self.client.post("/v1/checkout", "nonsense", content_type="application/json").status_code, 400)
        self.assertEqual(self.client.get("/v1/checkout").status_code, 405)

    def test_a_failed_and_a_cancelled_payment_charge_nothing(self):
        order = self.checkout()
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "failed"})
        self.assertEqual(self.order_json(order)["status"], "failed")
        other = self.checkout()
        self.assertContains(self.client.get(f"/pay/{other['order_id']}/cancel/"), "Nothing was charged")
        self.assertEqual(self.order_json(other)["status"], "cancelled")
        self.assertEqual(Payment.objects.count(), 0)

    def test_buying_from_the_apps_page(self):
        self.assertContains(self.client.get("/apps/org.acme.paid/"), "Buy on the web")
        response = self.client.post("/apps/org.acme.paid/buy/", {"email": "buyer@example.org"})
        order = Order.objects.get()
        self.assertRedirects(response, f"/pay/{order.public_id}/", fetch_redirect_response=False)
        self.assertEqual((order.source, order.payer_email), ("web", "buyer@example.org"))
        mail.outbox.clear()
        with self.captureOnCommitCallbacks(execute=True):
            self.client.post(f"/pay/{order.public_id}/test/", {"outcome": "paid"})
        # The buyer's own browser is taken to the purchase; the mail carries the way back.
        own = f"/orders/{order.public_id}/?secret={order.secret}"
        self.assertRedirects(self.client.get(f"/pay/{order.public_id}/return/"), own, fetch_redirect_response=False)
        self.assertEqual(mail.outbox[0].to, ["buyer@example.org"])
        self.assertIn(ROOT + own, mail.outbox[0].body)
        page = self.client.get(own)
        self.assertContains(page, "Paid is yours")
        self.assertContains(page, "ULIC1.")
        self.assertEqual((page["Referrer-Policy"], page["Cache-Control"]), ("same-origin", "no-store"))   # the link carries the secret
        self.assertNotContains(page, 'name="referrer"')   # a stricter policy would blank the Origin the page's own forms need
        self.assertEqual(self.client.get(f"/orders/{order.public_id}/").status_code, 404)
        self.assertEqual(self.client.get(f"/orders/{order.public_id}/?secret=guess").status_code, 404)
        file = self.client.get(f"/orders/{order.public_id}/licence.ulic?secret={order.secret}")
        self.assertTrue(file.content.startswith(b"ULIC1."))
        self.assertIn("licence.ulic", file["Content-Disposition"])
        link = self.client.get(f"/orders/{order.public_id}/download/{self.package.pk}/?secret={order.secret}")
        self.assertEqual(link.status_code, 302)
        self.assertTrue(link["Location"].startswith(f"{ROOT}/v1/restricted/{self.package.file_name}?md5="))
        # Somebody else's browser, sent back by the provider, learns nothing.
        self.client.cookies.clear()
        self.assertNotContains(self.client.get(f"/pay/{order.public_id}/return/"), "ULIC1.")

    def test_the_publisher_sees_its_sales_and_nobody_elses(self):
        order = self.checkout()
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        self.sign_in("dev@example.org")
        page = self.client.get("/console/sales/")
        self.assertContains(page, "19.90 EUR")
        self.assertNotContains(page, "buyer@example.org")
        self.make_publisher("other@example.org", "other")
        self.sign_in("other@example.org")
        self.assertContains(self.client.get("/console/sales/"), "Nothing sold yet")


@override_settings(**SKRILL)
class SkrillTests(BillingCase):
    provider = SKRILL

    def test_only_skrills_signed_report_pays_an_order(self):
        order = self.checkout()
        page = self.client.get(f"/pay/{order['order_id']}/")
        for expected in ('action="https://pay.skrill.com"', 'name="pay_to_email" value="shop@ultraos.eu"', f'name="transaction_id" value="{order["order_id"]}"',
                         'name="amount" value="19.90"', f'name="status_url" value="{ROOT}/v1/skrill/status"',
                         f'name="return_url" value="{ROOT}/pay/{order["order_id"]}/return/"'):
            self.assertContains(page, expected)
        self.assertEqual(self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"}).status_code, 404)
        # The browser coming back proves nothing.
        self.assertContains(self.client.get(f"/pay/{order['order_id']}/return/"), "being confirmed")
        self.assertEqual(self.order_json(order)["status"], "open")

        forged = self.report(order["order_id"], md5sig="0" * 32)
        self.assertEqual((forged.status_code, forged.json()["error"]), (403, "bad_signature"))
        self.assertEqual(self.report("no-such-order").status_code, 404)
        short = self.report(order["order_id"], amount="1.00")
        self.assertEqual((short.status_code, short.json()["error"]), (400, "amount_mismatch"))
        found = Order.objects.get(public_id=order["order_id"])
        self.assertEqual(found.status, "pending")
        self.assertIn("amount mismatch", found.notes)
        self.assertEqual(self.report(order["order_id"], currency="USD").status_code, 400)
        self.assertEqual(self.order_json(order)["status"], "pending")

        with self.captureOnCommitCallbacks(execute=True):
            paid = self.report(order["order_id"], amount="19.9")   # the same amount, written Skrill's way
        self.assertEqual((paid.status_code, paid.content), (200, b"OK"))
        state = self.order_json(order)
        self.assertEqual((state["status"], state["receipt_no"]), ("paid", 1))
        self.assertEqual(mail.outbox[-1].to, ["buyer@example.org"])   # the address Skrill reports
        # Skrill repeats a report it got no answer to: answered again, nothing changes.
        self.assertEqual(self.report(order["order_id"]).content, b"OK")
        self.assertEqual((Payment.objects.count(), self.order_json(order)["licence"]), (1, state["licence"]))

    def test_a_chargeback_takes_the_licence_back(self):
        order = self.checkout()
        self.report(order["order_id"])
        licence_id = self.order_json(order)["licence_id"]
        self.assertEqual(self.report(order["order_id"], status="-3").content, b"OK")
        self.report(order["order_id"], status="-3")   # and its repeat
        state = self.order_json(order)
        self.assertEqual((state["status"], state["revoked"], state["revoked_reason"]), ("chargeback", True, "chargeback"))
        self.assertNotIn("licence", state)
        self.assertEqual(Payment.objects.filter(kind="chargeback").count(), 1)
        self.assertEqual(self.client.get(f"/v1/licences/{licence_id}").json()["status"], "revoked")
        refused = self.client.post(f"/v1/orders/{order['order_id']}/download?secret={order['secret']}&file=restricted/{self.package.file_name}")
        self.assertEqual(refused.status_code, 409)

    def test_the_operator_refunds_through_skrill(self):
        order = self.checkout()
        self.report(order["order_id"])
        found = Order.objects.get(public_id=order["order_id"])
        self.sign_in("dev@example.org")
        self.assertEqual(self.client.get(f"/operator/orders/{found.pk}/").status_code, 403)
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/orders/"), "Paid")
        self.assertContains(self.client.get(f"/operator/orders/{found.pk}/"), "19.90 EUR is left")
        # Too much is refused before Skrill is asked.
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "50.00"})
        self.assertEqual(self.calls, [])
        # A part of it: the licence stays.
        self.answers += ["<response><sid>s1</sid></response>", "<response><mb_transaction_id>900200</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "5.00", "note": "late delivery"})
        self.assertEqual(self.calls[0][0], "https://www.skrill.com/app/refund.pl")
        self.assertEqual((self.calls[0][1]["mb_transaction_id"], self.calls[0][1]["amount"], self.calls[1][1]), ("900100", "5.00", {"action": "refund", "sid": "s1"}))
        state = self.order_json(order)
        self.assertEqual((state["status"], state["refunded"], "licence" in state), ("paid", "5.00", True))
        # Skrill refusing changes nothing.
        self.answers += ["<response><error><error_msg>ALREADY_REFUNDED</error_msg></error></response>"]
        self.assertContains(self.client.post(f"/operator/orders/{found.pk}/", {"amount": "1.00"}, follow=True), "ALREADY_REFUNDED")
        self.assertEqual(self.order_json(order)["refunded"], "5.00")
        # The rest: everything paid is back, so is the licence.
        self.answers += ["<response><sid>s2</sid></response>", "<response><mb_transaction_id>900201</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "14.90"})
        state = self.order_json(order)
        self.assertEqual((state["status"], state["refunded"], state["revoked_reason"], "licence" in state), ("refunded", "19.90", "refund", False))
        self.assertEqual([p["kind"] for p in state["payments"]], ["sale", "refund", "refund"])
        self.assertEqual(sorted(p["receipt_no"] for p in state["payments"]), [1, 2, 3])
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/console/sales/"), "0.00")   # the net


@override_settings(**SKRILL)
class SubscriptionTests(BillingCase):
    provider = SKRILL

    def setUp(self):
        super().setUp()
        self.monthly = self.make_app(self.publisher, "org.acme.monthly", name="Monthly", pricing_model="monthly", price="4.00", currency="EUR",
                                     updates_period="until", updates_until=dt.date(2030, 1, 1))
        self.make_release(self.monthly)

    def test_a_subscription_is_paid_renewed_and_cancelled(self):
        order = self.checkout("org.acme.monthly")
        self.assertEqual(order["cycle"], "month")
        page = self.client.get(f"/pay/{order['order_id']}/")
        for expected in ('name="rec_amount" value="4.00"', 'name="rec_cycle" value="month"', f'name="rec_status_url" value="{ROOT}/v1/skrill/recurring"'):
            self.assertContains(page, expected)
        early = self.report(order["order_id"], amount="4.00", url="/v1/skrill/recurring")
        self.assertEqual((early.status_code, early.json()["error"]), (409, "not_a_subscription"))

        self.report(order["order_id"], amount="4.00", rec_payment_id="REC1")
        state = self.order_json(order)
        today = dt.date.today()
        paid_until = billing.add_cycle(today, "month")
        self.assertEqual((state["paid_until"], state["valid_until"], state["subscription_status"]),
                         (paid_until.isoformat(), (paid_until + dt.timedelta(days=7)).isoformat(), "active"))
        first = licence.verify(state["licence"], self.catalog()["licence_key"])
        self.assertEqual((first["valid_until"], first["updates"], first["updates_until"]), (state["valid_until"], "until", "2030-01-01"))

        # A renewal starts the next period where the paid one ends and re-issues the licence under the same id.
        self.assertEqual(self.report(order["order_id"], amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900101").content, b"OK")
        self.report(order["order_id"], amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900101")   # its repeat
        renewed = self.order_json(order)
        self.assertEqual(renewed["paid_until"], billing.add_cycle(paid_until, "month").isoformat())
        self.assertEqual([p["kind"] for p in renewed["payments"]], ["sale", "renewal"])
        second = licence.verify(renewed["licence"], self.catalog()["licence_key"])
        self.assertEqual((second["licence"], second["valid_until"] > first["valid_until"]), (first["licence"], True))
        self.assertEqual(self.report(order["order_id"], amount="1.00", url="/v1/skrill/recurring", mb_transaction_id="900102").status_code, 400)

        self.report(order["order_id"], status="-2", amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900103")
        self.assertEqual(self.order_json(order)["subscription_status"], "failing")

        self.answers += ["<response><result>OK</result></response>"]
        cancelled = self.client.post(f"/v1/orders/{order['order_id']}/subscription/cancel?secret={order['secret']}").json()
        self.assertEqual((cancelled["subscription_status"], cancelled["status"]), ("cancelled", "paid"))   # it lasts until the paid period ends
        self.assertEqual((self.calls[0][0], self.calls[0][1]["action"], self.calls[0][1]["rec_payment_id"]),
                         ("https://www.skrill.com/app/query.pl", "cancel_rec_payment", "REC1"))
        self.client.post(f"/v1/orders/{order['order_id']}/subscription/cancel?secret={order['secret']}")   # again: Skrill is not asked twice
        self.assertEqual(len(self.calls), 1)

    def test_an_ended_subscription_downloads_nothing_and_its_licence_reads_expired(self):
        order = self.checkout("org.acme.monthly")
        self.report(order["order_id"], amount="4.00")
        found = Order.objects.get(public_id=order["order_id"])
        Order.objects.filter(pk=found.pk).update(paid_until=dt.date(2020, 1, 1), valid_until=dt.date(2020, 1, 8))
        self.assertEqual(self.client.get(f"/v1/licences/{found.licence_id}").json()["status"], "expired")
        self.assertEqual(billing.add_cycle(dt.date(2026, 1, 31), "month"), dt.date(2026, 2, 28))
        self.assertEqual(billing.add_cycle(dt.date(2024, 2, 29), "year"), dt.date(2025, 2, 28))
        self.assertEqual(billing.add_cycle(dt.date(2026, 12, 15), "month"), dt.date(2027, 1, 15))
        self.assertEqual(billing.paid_total(found), Decimal("4.00"))


class ConfigurationTests(PortalCase):
    def test_what_is_missing_is_named(self):
        with override_settings(PAYMENT_PROVIDER=""):
            self.assertEqual(billing.missing_settings(), ["ULTRASTORE_PORTAL_PAYMENT_PROVIDER"])
        with override_settings(PAYMENT_PROVIDER="skrill", SKRILL_PAY_TO_EMAIL="", SKRILL_SECRET_WORD="x", SKRILL_MERCHANT_ID="", RESTRICTED_SECRET=""):
            self.assertEqual(billing.missing_settings(), ["ULTRASTORE_PORTAL_SKRILL_PAY_TO_EMAIL", "ULTRASTORE_PORTAL_SKRILL_MERCHANT_ID",
                                                          "ULTRASTORE_PORTAL_RESTRICTED_SECRET"])
            self.assertEqual(self.client.get("/v1/keys").status_code, 503)
        with override_settings(PAYMENT_PROVIDER="test", RESTRICTED_SECRET="s"):
            self.assertTrue(billing.selling())
            # A report signed with the test secret is no good to a store that sells through Skrill.
            self.assertEqual(self.client.post("/v1/skrill/status", {"transaction_id": "x"}).status_code, 404)
        self.sign_in("operator@example.org")
        with override_settings(PAYMENT_PROVIDER=""):
            self.assertContains(self.client.get("/operator/"), "The store is not selling")



class SplitTests(BillingCase):
    """The store's fee and the publisher's share of every payment."""

    def test_a_sale_is_split_at_the_fee_of_the_day_and_keeps_it(self):
        order = self.checkout()
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        first = Payment.objects.get()
        # 15 % of 19.90 is 2.985: to the cent, half up.
        self.assertEqual((first.fee_percent, first.store_fee, first.publisher_share), (Decimal("15"), Decimal("2.99"), Decimal("16.91")))
        with override_settings(STORE_FEE_PERCENT=Decimal("20")):
            later = self.checkout()
            self.client.post(f"/pay/{later['order_id']}/test/", {"outcome": "paid"})
        second = Payment.objects.exclude(pk=first.pk).get()
        self.assertEqual((second.fee_percent, second.store_fee, second.publisher_share), (Decimal("20"), Decimal("3.98"), Decimal("15.92")))
        first.refresh_from_db()
        self.assertEqual(first.store_fee, Decimal("2.99"))   # a fee changed later leaves what was sold as it was

        self.sign_in("dev@example.org")
        page = self.client.get("/console/sales/")
        self.assertContains(page, "Your share, EUR")
        self.assertContains(page, "<b>32.83</b>", html=True)   # 16.91 + 15.92
        self.assertContains(page, "6.97 the store")            # 2.99 + 3.98
        self.assertContains(page, "15 % today")
        self.assertNotContains(page, "buyer@example.org")
        self.sign_in("operator@example.org")
        page = self.client.get(f"/operator/orders/{Order.objects.get(public_id=order['order_id']).pk}/")
        self.assertContains(page, "2.99")
        self.assertContains(page, "(15 %)")
        self.assertContains(page, "16.91")

    def test_the_fee_is_reckoned_to_the_cent_half_up(self):
        self.assertEqual(split.fee_of(Decimal("0.10"), Decimal("15")), Decimal("0.02"))     # 0.015
        self.assertEqual(split.fee_of(Decimal("0.03"), Decimal("15")), Decimal("0.00"))     # 0.0045
        self.assertEqual(split.fee_of(Decimal("100.00"), Decimal("12.5")), Decimal("12.50"))
        self.assertEqual(split.fee_of(Decimal("19.90"), Decimal("0")), Decimal("0.00"))
        self.assertEqual((split.percent_text(Decimal("15.00")), split.percent_text(Decimal("10")), split.percent_text(Decimal("12.50"))), ("15", "10", "12.5"))

    def test_a_reversal_gives_the_fee_back_in_proportion_and_never_more_than_it_took(self):
        fee = lambda amount, before=("0", "0"): split.reversal_fee(Decimal("19.90"), Decimal("2.99"), Decimal(amount), Decimal(before[0]), Decimal(before[1]))
        self.assertEqual(fee("5.00"), Decimal("0.75"))                       # 2.99 * 5.00 / 19.90 = 0.7512
        self.assertEqual(fee("14.90", ("5.00", "0.75")), Decimal("2.24"))    # the rest of the payment: the rest of its fee
        self.assertEqual(fee("19.90"), Decimal("2.99"))
        self.assertEqual(fee("25.00"), Decimal("2.99"))                      # a chargeback above the payment returns no more than the fee
        self.assertEqual(fee("1.00", ("18.00", "2.99")), Decimal("0.00"))    # nothing left to return

    def test_payments_from_before_the_split_get_it_when_the_portal_is_upgraded(self):
        order = self.checkout()
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        found = Order.objects.get(public_id=order["order_id"])
        sale = found.payments.get()
        Payment.objects.create(order=found, kind=Payment.REFUND, amount=Decimal("5.00"), currency="EUR", reverses=sale.provider_transaction)
        Payment.objects.create(order=found, kind=Payment.CHARGEBACK, amount=Decimal("14.90"), currency="EUR", reverses=sale.provider_transaction)
        Payment.objects.update(fee_percent=0, store_fee=0)   # as 0.6.0 left them
        from django.apps import apps
        importlib.import_module("ultrastore_portal.billing.migrations.0002_payment_split").split_earlier_payments(apps, None)
        self.assertEqual([(p.kind, p.fee_percent, p.store_fee) for p in found.payments.all()],
                         [("sale", Decimal("15"), Decimal("2.99")), ("refund", Decimal("15"), Decimal("0.75")), ("chargeback", Decimal("15"), Decimal("2.24"))])


@override_settings(**SKRILL)
class SkrillSplitTests(BillingCase):
    provider = SKRILL

    def totals(self) -> tuple[Decimal, Decimal]:
        payments = Payment.objects.all()
        return sum(p.signed_store_fee for p in payments), sum(p.signed_publisher_share for p in payments)

    def test_a_refund_in_parts_returns_the_whole_fee_and_no_more(self):
        order = self.checkout()
        self.report(order["order_id"])
        found = Order.objects.get(public_id=order["order_id"])
        self.sign_in("operator@example.org")
        self.answers += ["<response><sid>s1</sid></response>", "<response><mb_transaction_id>900200</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "5.00"})
        part = Payment.objects.get(kind="refund")
        self.assertEqual((part.fee_percent, part.store_fee, part.publisher_share), (Decimal("15"), Decimal("0.75"), Decimal("4.25")))
        self.assertEqual(self.totals(), (Decimal("2.24"), Decimal("12.66")))
        self.answers += ["<response><sid>s2</sid></response>", "<response><mb_transaction_id>900201</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "14.90"})
        self.assertEqual(Payment.objects.filter(kind="refund").order_by("id").last().store_fee, Decimal("2.24"))
        self.assertEqual(self.totals(), (Decimal("0.00"), Decimal("0.00")))
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/console/sales/"), "<b>0.00</b>", html=True)

    def test_a_chargeback_returns_the_fee(self):
        order = self.checkout()
        self.report(order["order_id"])
        self.report(order["order_id"], status="-3")
        back = Payment.objects.get(kind="chargeback")
        self.assertEqual((back.store_fee, back.signed_store_fee, back.signed_publisher_share), (Decimal("2.99"), Decimal("-2.99"), Decimal("-16.91")))
        self.assertEqual(self.totals(), (Decimal("0.00"), Decimal("0.00")))

    def test_a_renewal_is_split_like_a_sale(self):
        monthly = self.make_app(self.publisher, "org.acme.monthly", name="Monthly", pricing_model="monthly", price="4.00", currency="EUR")
        self.make_release(monthly)
        order = self.checkout("org.acme.monthly")
        self.report(order["order_id"], amount="4.00", rec_payment_id="REC1")
        self.report(order["order_id"], amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900101")
        self.assertEqual([(p.kind, p.store_fee, p.publisher_share) for p in Payment.objects.order_by("id")],
                         [("sale", Decimal("0.60"), Decimal("3.40")), ("renewal", Decimal("0.60"), Decimal("3.40"))])
