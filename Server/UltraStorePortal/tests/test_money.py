"""Tax and payouts: the VAT in a price by the buyer's country, the invoice
and the credit note, a quarter's figures, and what a publisher is owed and
paid."""

from __future__ import annotations

import datetime as dt
import io
from decimal import Decimal

from django.core.management import call_command
from django.test import override_settings
from django.utils import timezone

from ultrastore_portal.accounts.models import User
from ultrastore_portal.billing import payouts, tax
from ultrastore_portal.billing.models import Invoice, Order, Payment, Payout, VatRate
from ultrastore_portal.store.models import AuditEntry, PublisherMember

from .test_billing import SKRILL, BillingCase
from .test_portal import PASSWORD

SELLER = dict(SELLER_NAME="ULTRA OS Project GmbH", SELLER_ADDRESS="Example Street 1\n12345 Berlin", SELLER_VAT_ID="DE123456789",
              SELLER_COUNTRY="DE", INVOICE_PREFIX="UO-")
D = Decimal


class TaxCase(BillingCase):
    seller = SELLER

    def setUp(self):
        super().setUp()
        override = override_settings(**self.seller)
        override.enable()
        self.addCleanup(override.disable)
        VatRate.objects.create(country="DE", percent=D("19"), valid_from=dt.date(2020, 1, 1))
        VatRate.objects.create(country="FR", percent=D("20"), valid_from=dt.date(2020, 1, 1))
        self.year = timezone.localdate().year

    def buy(self, **extra) -> Payment:
        order = self.checkout(**extra)
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        return Payment.objects.filter(order__public_id=order["order_id"]).get()


class TaxTests(TaxCase):
    def test_the_tax_comes_out_of_the_price_and_the_fee_is_on_the_net(self):
        home = self.buy()   # no country came with the order: the seller's
        # 19.90 includes 19 %: 19.90 * 19 / 119 = 3.177 tax; 15 % of the 16.72 left is 2.508.
        self.assertEqual((home.country, home.vat_percent, home.vat_amount, home.net_amount, home.store_fee, home.publisher_share, home.vat_note),
                         ("DE", D("19"), D("3.18"), D("16.72"), D("2.51"), D("14.21"), ""))
        abroad = self.buy(country="fr")
        self.assertEqual((abroad.country, abroad.vat_percent, abroad.vat_amount, abroad.store_fee, abroad.publisher_share),
                         ("FR", D("20"), D("3.32"), D("2.49"), D("14.09")))
        self.assertEqual(abroad.amount, D("19.90"))   # the buyer pays the listed price wherever they are
        beyond = self.buy(country="US")
        self.assertEqual((beyond.vat_amount, beyond.store_fee, beyond.publisher_share, beyond.vat_note),
                         (D("0.00"), D("2.99"), D("16.91"), "outside the EU: no EU VAT"))
        greek = self.buy(country="EL")   # the VAT prefix of Greece is its country all the same
        self.assertEqual((greek.country, greek.vat_percent, greek.vat_note), ("GR", D("19"), "no rate for GR: the seller's (DE)"))
        self.assertEqual(Order.objects.get(payments=greek).country, "GR")
        self.assertIn("No VAT rate for:", " ".join(tax.problems()))
        self.assertIn("1 payment(s) were taxed without their own country's rate", " ".join(tax.problems()))

    def test_a_rate_counts_from_its_day(self):
        VatRate.objects.create(country="FR", percent=D("22"), valid_from=timezone.localdate() + dt.timedelta(days=1))
        self.assertEqual(self.buy(country="FR").vat_percent, D("20"))                       # tomorrow's rate is tomorrow's
        self.assertEqual(tax.rate_for("FR", timezone.localdate() + dt.timedelta(days=1)), (D("22"), ""))
        self.assertEqual(tax.vat_in(D("119.00"), D("19")), D("19.00"))
        self.assertEqual(tax.vat_in(D("0.05"), D("19")), D("0.01"))                         # 0.00798: half up at the cent
        self.assertEqual(tax.vat_in(D("10.00"), D("0")), D("0.00"))

    def test_every_payment_has_its_invoice_in_one_series_without_gaps(self):
        first = self.buy()
        second = self.buy(country="FR")
        numbers = [i.number for i in Invoice.objects.order_by("id")]
        self.assertEqual(numbers, [f"UO-{self.year}-000001", f"UO-{self.year}-000002"])
        invoice = Invoice.objects.get(payment=first)
        self.assertEqual((invoice.kind, invoice.net, invoice.vat_percent, invoice.vat, invoice.gross, invoice.currency, invoice.buyer_country,
                          invoice.seller_name, invoice.seller_vat_id, invoice.corrects),
                         ("invoice", D("16.72"), D("19"), D("3.18"), D("19.90"), "EUR", "DE", "ULTRA OS Project GmbH", "DE123456789", ""))
        self.assertIn("Paid (org.acme.paid)", invoice.description)
        self.assertIsNone(tax.issue_invoice(first))                                         # never a second one for a payment
        # What the seller is called later does not rewrite what was written.
        with override_settings(SELLER_NAME="Somebody Else Ltd"):
            self.assertEqual(Invoice.objects.get(payment=second).seller_name, "ULTRA OS Project GmbH")
        order = Order.objects.get(payments=first)
        page = self.client.get(f"/v1/orders/{order.public_id}/receipt?secret={order.secret}", headers={"accept": "text/html"})
        self.assertContains(page, f"Invoice UO-{self.year}-000001")
        self.assertContains(page, "VAT 19 %")
        self.assertContains(page, "ULTRA OS Project GmbH")
        self.assertNotContains(page, "not a tax invoice")
        text = self.client.get(f"/v1/orders/{order.public_id}/receipt?secret={order.secret}").content.decode()
        self.assertIn("VAT:       3.18 EUR (19 %)", text)
        self.assertEqual(self.order_json({"order_id": order.public_id, "secret": order.secret})["payments"][0]["invoice_no"], f"UO-{self.year}-000001")

    def test_the_buy_page_asks_for_the_country(self):
        page = self.client.get("/apps/org.acme.paid/buy/")
        self.assertContains(page, "Your country")
        self.assertContains(page, "Outside the European Union")
        self.client.post("/apps/org.acme.paid/buy/", {"email": "buyer@example.org", "country": "FR"})
        self.assertEqual(Order.objects.get().country, "FR")
        refused = self.client.post("/apps/org.acme.paid/buy/", {"email": "buyer@example.org", "country": "Mars"})
        self.assertEqual((refused.status_code, Order.objects.count()), (200, 1))

    def test_a_quarter_is_reckoned_by_country_and_rate(self):
        self.buy()
        self.buy()
        self.buy(country="FR")
        self.buy(country="US")
        year, quarter = tax.quarter_of(timezone.localdate())
        report = tax.quarter_report(year, quarter)
        self.assertEqual([(r["country"], r["percent"], r["invoices"], r["net"], r["vat"], r["gross"]) for r in report["rows"]],
                         [("DE", "19", 2, D("33.44"), D("6.36"), D("39.80")), ("FR", "20", 1, D("16.58"), D("3.32"), D("19.90")),
                          ("US", "0", 1, D("19.90"), D("0.00"), D("19.90"))])
        self.assertEqual([(t["net"], t["vat"], t["gross"]) for t in report["totals"]], [(D("69.92"), D("9.68"), D("79.60"))])
        self.assertEqual(tax.quarter_report(year - 1, quarter)["rows"], [])
        self.sign_in("dev@example.org")
        self.assertEqual(self.client.get("/operator/tax/").status_code, 403)
        self.sign_in("operator@example.org")
        page = self.client.get("/operator/tax/")
        self.assertContains(page, "6.36 EUR")
        self.assertContains(page, "No VAT rate for:")
        self.assertEqual(self.client.get("/operator/tax/?year=nonsense&quarter=9").status_code, 200)
        out = io.StringIO()
        call_command("money", "quarter", str(year), str(quarter), stdout=out)
        self.assertIn("DE\t19 %\t2\t33.44\t6.36\t39.80\tEUR", out.getvalue())

    def test_the_operator_keeps_the_rates(self):
        self.sign_in("operator@example.org")
        day = (timezone.localdate() + dt.timedelta(days=40)).isoformat()
        self.client.post("/operator/tax/", {"country": "el", "percent": "24", "valid_from": "2020-01-01", "note": ""})
        self.assertEqual(VatRate.objects.get(country="GR").percent, D("24"))
        self.client.post("/operator/tax/", {"country": "GR", "percent": "24.5", "valid_from": "2020-01-01", "note": "corrected"})   # the same day: replaced
        self.assertEqual((VatRate.objects.filter(country="GR").count(), VatRate.objects.get(country="GR").percent), (1, D("24.5")))
        self.client.post("/operator/tax/", {"country": "GR", "percent": "25", "valid_from": day, "note": ""})                     # a change to come
        self.assertContains(self.client.get("/operator/tax/"), "25.00 % from")
        self.assertEqual(self.buy(country="GR").vat_percent, D("24.5"))
        bad = self.client.post("/operator/tax/", {"country": "Greece", "percent": "120", "valid_from": "2020-01-01"})
        self.assertContains(bad, "Two letters, such as DE.")
        self.assertEqual(AuditEntry.objects.filter(action="tax.rate").count(), 3)

    def test_the_starting_table_fills_what_is_empty_and_is_marked(self):
        out = io.StringIO()
        call_command("money", "seed-rates", stdout=out)
        self.assertIn("25 rate(s) added", out.getvalue())                    # 27 member states, two had a rate
        self.assertEqual((VatRate.objects.get(country="DE").note, VatRate.objects.get(country="FI").percent, VatRate.objects.get(country="FI").note),
                         ("", D("25.5"), "starting table, to be checked"))
        self.assertEqual(tax.seed(), 0)
        self.assertEqual(set(tax.STARTING_RATES), set(tax.EU))
        self.assertNotIn("No VAT rate for:", " ".join(tax.problems()))


class UntaxedTests(BillingCase):
    def test_without_the_sellers_details_nothing_is_taxed_and_no_invoice_written(self):
        order = self.checkout(country="FR")
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        payment = Payment.objects.get()
        self.assertEqual((payment.vat_amount, payment.country, payment.store_fee, payment.publisher_share), (D("0"), "", D("2.99"), D("16.91")))
        self.assertFalse(Invoice.objects.exists())
        self.assertNotContains(self.client.get("/apps/org.acme.paid/buy/"), "Your country")
        page = self.client.get(f"/v1/orders/{order['order_id']}/receipt?secret={order['secret']}", headers={"accept": "text/html"})
        self.assertContains(page, "not a tax invoice")
        self.assertEqual(len(tax.missing_settings()), 4)
        self.assertNotIn("invoice_no", self.order_json(order)["payments"][0])
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/tax/"), "No tax is reckoned and no invoice written")


@override_settings(**SKRILL)
class CreditNoteTests(TaxCase):
    provider = SKRILL

    def test_a_refund_in_parts_gives_the_tax_back_with_a_credit_note_each(self):
        order = self.checkout()
        self.report(order["order_id"])
        found = Order.objects.get(public_id=order["order_id"])
        self.sign_in("operator@example.org")
        self.answers += ["<response><sid>s1</sid></response>", "<response><mb_transaction_id>900200</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "5.00"})
        part = Payment.objects.get(kind="refund")
        # 5.00 of 19.90: 3.18 * 5 / 19.90 = 0.799 tax, 2.51 * 5 / 19.90 = 0.631 fee.
        self.assertEqual((part.vat_amount, part.store_fee, part.publisher_share, part.country, part.vat_percent), (D("0.80"), D("0.63"), D("3.57"), "DE", D("19")))
        note = Invoice.objects.get(payment=part)
        self.assertEqual((note.kind, note.number, note.corrects, note.net, note.vat, note.gross),
                         ("credit_note", f"UO-{self.year}-000002", f"UO-{self.year}-000001", D("-4.20"), D("-0.80"), D("-5.00")))
        self.answers += ["<response><sid>s2</sid></response>", "<response><mb_transaction_id>900201</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "14.90"})
        payments = list(Payment.objects.all())
        self.assertEqual((sum(p.signed_vat_amount for p in payments), sum(p.signed_store_fee for p in payments), sum(p.signed_publisher_share for p in payments)),
                         (D("0.00"), D("0.00"), D("0.00")))
        self.assertEqual(sum(i.vat for i in Invoice.objects.all()), D("0.00"))
        page = self.client.get(f"/operator/orders/{found.pk}/")
        self.assertContains(page, f"UO-{self.year}-000003")
        self.assertContains(page, "(DE 19 %)")
        credit = self.client.get(f"/v1/orders/{found.public_id}/receipt?secret={found.secret}&no={part.receipt_no}", headers={"accept": "text/html"})
        self.assertContains(credit, f"Credit note UO-{self.year}-000002")
        self.assertContains(credit, f"invoice UO-{self.year}-000001")

    def test_a_chargeback_has_its_credit_note(self):
        order = self.checkout(country="FR")
        self.report(order["order_id"])
        self.report(order["order_id"], status="-3")
        back = Payment.objects.get(kind="chargeback")
        self.assertEqual((back.vat_amount, back.country), (D("3.32"), "FR"))
        note = Invoice.objects.get(payment=back)
        self.assertEqual((note.kind, note.gross, note.buyer_country, "chargeback" in note.description), ("credit_note", D("-19.90"), "FR", True))


class PayoutTests(BillingCase):
    def sale(self) -> Payment:
        order = self.checkout()
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        return Payment.objects.filter(order__public_id=order["order_id"]).get()

    def age(self, payment: Payment, days: int) -> None:
        Payment.objects.filter(pk=payment.pk).update(at=timezone.now() - dt.timedelta(days=days))

    def test_a_sale_waits_before_its_share_is_paid_out(self):
        today = timezone.localdate()
        young, old = self.sale(), self.sale()
        self.age(old, 31)
        self.assertEqual(payouts.owed(self.publisher), [{"currency": "EUR", "amount": D("33.82"), "payments": 2}])
        made = payouts.build(today, self.operator)
        self.assertEqual([(p.publisher, p.currency, p.amount, p.until, p.state) for p in made], [(self.publisher, "EUR", D("16.91"), today, "open")])
        old.refresh_from_db()
        young.refresh_from_db()
        self.assertEqual((old.payout, young.payout), (made[0], None))                       # the young sale waits its thirty days
        self.assertEqual(payouts.build(today, self.operator), [])                           # and what is taken is taken
        self.assertEqual(payouts.owed(self.publisher), [{"currency": "EUR", "amount": D("16.91"), "payments": 1}])
        self.assertEqual(AuditEntry.objects.filter(action="payout.made").count(), 1)
        with self.assertRaises(payouts.PayoutError):
            payouts.build(today + dt.timedelta(days=1))
        self.assertEqual(payouts.default_until(dt.date(2026, 3, 15)), dt.date(2026, 2, 28))

    @override_settings(PAYOUT_HOLD_DAYS=0)
    def test_what_is_given_back_after_a_payout_comes_off_the_next(self):
        today = timezone.localdate()
        first = self.sale()
        paid = payouts.build(today)[0]
        order = first.order
        Payment.objects.create(order=order, kind=Payment.REFUND, amount=D("5.00"), currency="EUR", reverses=first.provider_transaction,
                               fee_percent=D("15"), store_fee=D("0.75"))
        self.assertEqual(payouts.build(today), [])                                          # minus 4.25: no payout, and nobody is asked for money
        self.assertEqual(payouts.owed(self.publisher), [{"currency": "EUR", "amount": D("-4.25"), "payments": 1}])
        self.sale()
        second = payouts.build(today)[0]
        self.assertEqual((second.amount, second.payments.count()), (D("12.66"), 2))         # 16.91 less the 4.25 given back
        self.assertEqual(paid.payments.count(), 1)
        self.assertEqual(payouts.lines(second)["share"], D("12.66"))

    @override_settings(PAYOUT_HOLD_DAYS=0, PAYOUT_MINIMUM=Decimal("25.00"))
    def test_a_small_sum_is_carried(self):
        today = timezone.localdate()
        self.sale()
        self.assertEqual(payouts.build(today), [])
        self.sale()
        self.assertEqual([p.amount for p in payouts.build(today)], [D("33.82")])

    @override_settings(PAYOUT_HOLD_DAYS=0)
    def test_the_operator_pays_and_says_so_and_the_publisher_sees_it(self):
        self.publisher.payout_details = "Acme Ltd\nDE00 1234 5678 9000 0000 00"
        self.publisher.save()
        self.sale()
        self.sign_in("dev@example.org")
        self.assertEqual(self.client.get("/operator/payouts/").status_code, 403)
        page = self.client.get("/console/sales/")
        self.assertContains(page, "Not paid out yet: <b>16.91 EUR</b>", html=False)
        self.assertContains(page, "No payout yet.")
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/payouts/"), "No payout here.")
        self.client.post("/operator/payouts/", {"until": timezone.localdate().isoformat()})
        payout = Payout.objects.get()
        self.assertEqual((payout.amount, payout.details, payout.created_by), (D("16.91"), "Acme Ltd\nDE00 1234 5678 9000 0000 00", self.operator))
        self.assertContains(self.client.post("/operator/payouts/", {"until": "soon"}, follow=True), "Give the day as YYYY-MM-DD.")
        page = self.client.get(f"/operator/payouts/{payout.pk}/")
        self.assertContains(page, "DE00 1234 5678 9000 0000 00")
        self.assertContains(page, "Mark as paid")
        self.assertContains(self.client.post(f"/operator/payouts/{payout.pk}/", {"reference": " "}, follow=True), "Give the transfer")
        self.client.post(f"/operator/payouts/{payout.pk}/", {"reference": "SEPA 2026-11-26/17", "note": "November"})
        payout.refresh_from_db()
        self.assertEqual((payout.state, payout.reference, payout.paid_by, payout.note), ("paid", "SEPA 2026-11-26/17", self.operator, "November"))
        self.assertContains(self.client.post(f"/operator/payouts/{payout.pk}/", {"reference": "again"}, follow=True), "marked paid already")
        self.assertNotContains(self.client.get(f"/operator/payouts/{payout.pk}/"), "Mark as paid")
        self.assertContains(self.client.get("/operator/payouts/?state=paid"), "SEPA 2026-11-26/17")
        self.assertEqual(AuditEntry.objects.filter(action="payout.paid").count(), 1)
        # The publisher's side: the payout and its statement - and nobody else's.
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/console/sales/"), "SEPA 2026-11-26/17")
        statement = self.client.get(f"/console/payouts/{payout.pk}/")
        self.assertContains(statement, "16.91")
        self.assertContains(statement, "not a tax document")
        self.make_publisher("other@example.org", "other")
        self.sign_in("other@example.org")
        self.assertEqual(self.client.get(f"/console/payouts/{payout.pk}/").status_code, 404)
        out = io.StringIO()
        call_command("money", "owed", stdout=out)
        self.assertEqual(out.getvalue(), "")   # everything is in a payout: nothing is owed

    def test_where_the_money_goes_is_the_owners_to_say(self):
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/console/publisher/"), "Payout details")
        self.client.post("/console/publisher/", {"name": "Acme", "website": "", "support_email": "dev@example.org", "description": "",
                                                 "legal_name": "Acme Ltd", "vat_id": "DE1", "payout_details": "IBAN DE00"})
        self.publisher.refresh_from_db()
        self.assertEqual((self.publisher.legal_name, self.publisher.payout_details), ("Acme Ltd", "IBAN DE00"))
        helper = User.objects.create_user("helper@example.org", PASSWORD, name="Hal", email_verified=True)
        PublisherMember.objects.create(publisher=self.publisher, user=helper, role=PublisherMember.DEVELOPER)
        self.sign_in("helper@example.org")
        page = self.client.get("/console/publisher/")
        self.assertNotContains(page, "Payout details")
        self.assertNotContains(page, "IBAN DE00")
        self.client.post("/console/publisher/", {"name": "Acme", "website": "", "support_email": "dev@example.org", "description": "",
                                                 "payout_details": "IBAN XX99 somebody else"})
        self.publisher.refresh_from_db()
        self.assertEqual(self.publisher.payout_details, "IBAN DE00")   # a developer cannot send the money elsewhere
