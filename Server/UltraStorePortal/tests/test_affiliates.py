"""UltraAffiliates: becoming an affiliate, a link to a product, what a sale
earns from the store's fee and from the publisher's boost, the
superaffiliate's share, what a refund takes back, a block and a ban; the
customer's binding; the holdback, the monthly payout and its statement."""

from __future__ import annotations

import datetime as dt
import io
from decimal import Decimal

from django.core.management import call_command
from django.test import override_settings
from django.utils import timezone

from ultrastore_portal.accounts import closure
from ultrastore_portal.accounts.models import User
from ultrastore_portal.affiliates import services, texts
from ultrastore_portal.affiliates.models import (AffiliatePayout, AffiliateProfile, Attribution, Binding, Boost, Click, ClickDay, Commission,
                                                Holdback, OrderVisit, Sanction)
from ultrastore_portal.billing import payouts
from ultrastore_portal.billing import services as billing
from ultrastore_portal.identity import provider
from ultrastore_portal.identity.models import Client, Grant
from ultrastore_portal.billing.models import Order, Payment
from ultrastore_portal.store.models import AuditEntry, PublisherMember

from .test_billing import SKRILL, BillingCase
from .test_portal import PASSWORD

D = Decimal
BORN = dt.date(1990, 5, 17)


# The cases below run with nothing held back and a payout from one cent, so
# that each says what it is about; MoneyRulesTests runs with the programme's
# own numbers - 5 % for six months, and 25.00.
@override_settings(AFFILIATES=True, AFFILIATE_HOLDBACK_PERCENT=Decimal("0"), AFFILIATE_PAYOUT_MINIMUM=Decimal("0.01"))
class AffiliateCase(BillingCase):
    def setUp(self):
        super().setUp()
        texts.outbox.clear()
        self.anna = self.affiliate("anna")

    def approve(self) -> int:
        """The approval period is over for everything pending."""
        Commission.objects.filter(state="pending").update(approve_after=timezone.localdate())
        return services.approve_due()

    def money(self, profile: AffiliateProfile | None = None, currency: str = "EUR") -> tuple:
        """Pending, held back, the balance, in a payout to pay, paid."""
        row = next((r for r in services.sums(profile or self.anna) if r["currency"] == currency), None)
        return tuple(row[name] for name in ("pending", "held", "balance", "to_pay", "paid")) if row else ()

    def pay(self, profile: AffiliateProfile | None = None, reference: str = "SEPA 1") -> list[AffiliatePayout]:
        """The payouts for what is due today, made and marked paid."""
        return [services.mark_paid(payout, self.operator, reference) for payout in services.build(timezone.localdate(), self.operator, profile or self.anna)]

    def affiliate(self, handle: str, *, active: bool = True, recruiter: str = "") -> AffiliateProfile:
        user = User.objects.create_user(f"{handle}@example.org", PASSWORD, name=handle.title(), email_verified=True)
        profile = services.apply(user, handle=handle, phone="+491701234567", payout_details=f"IBAN of {handle}", recruiter=recruiter)
        if active:
            services.activate(profile, self.operator, id_name=f"{handle.title()} Example", id_birth_date=BORN, id_document="passport",
                              id_country="DE", id_expires=None, phone_confirmed=True)
        return profile

    def follow(self, handle: str = "anna", app_id: str = "org.acme.paid", **headers):
        return self.client.get(f"/r/{handle}/{app_id}/" if app_id else f"/r/{handle}/", headers=headers)

    def buy(self, email: str = "buyer@example.org") -> Payment:
        self.client.post("/apps/org.acme.paid/buy/", {"email": email})
        order = Order.objects.order_by("-id").first()
        self.client.post(f"/pay/{order.public_id}/test/", {"outcome": "paid"})
        return Payment.objects.get(order=order, kind="sale")

    def boost(self, percent: str, **fields) -> Boost:
        owner = PublisherMember.objects.get(publisher=self.publisher, role="owner").user
        return services.save_boost(self.publisher, owner, scope=fields.pop("scope", Boost.APP), percent=D(percent),
                                   app=fields.pop("app", self.app), **fields)


class LinkTests(AffiliateCase):
    def test_a_link_to_a_product_earns_the_base_from_the_stores_fee(self):
        self.assertRedirects(self.follow(), "/apps/org.acme.paid/", fetch_redirect_response=False)
        payment = self.buy()
        line = Commission.objects.get()
        # 19.90, no tax configured: 5 % is 0.995, half up. The publisher pays nothing of it.
        self.assertEqual((line.affiliate, line.basis, line.net, line.base_percent, line.boost_percent, line.operator_part, line.merchant_part, line.amount, line.state),
                         (self.anna, "product_link", D("19.90"), D("5"), D("0"), D("1.00"), D("0.00"), D("1.00"), "pending"))
        self.assertEqual(line.approve_after, timezone.localdate() + dt.timedelta(days=30))
        payment.refresh_from_db()
        self.assertEqual((payment.boost_amount, payment.store_fee, payment.publisher_share), (D("0"), D("2.99"), D("16.91")))
        self.assertEqual(Attribution.objects.get().affiliate, self.anna)

    def test_no_link_no_commission_and_a_link_to_the_shop_is_no_product_link(self):
        self.buy()
        self.follow(app_id="")                     # the shop, not the product: counted, and earns nothing in phase 1
        self.buy()
        self.assertEqual((Commission.objects.count(), Click.objects.filter(app__isnull=True).count()), (0, 1))

    def test_the_last_link_to_the_product_wins(self):
        self.affiliate("bert")
        self.follow("anna")
        self.follow("bert")
        self.buy()
        self.assertEqual(Commission.objects.get().affiliate.handle, "bert")

    def test_only_a_person_following_a_link_counts(self):
        self.follow(**{"sec-fetch-dest": "image"})
        self.follow(**{"sec-fetch-dest": "iframe"})
        self.assertEqual(Click.objects.count(), 0)
        self.follow(**{"sec-fetch-dest": "document"})
        self.assertEqual(Click.objects.count(), 1)
        self.assertEqual(self.client.get("/r/anna/org.acme.nothere/").status_code, 404)
        self.assertRedirects(self.follow("nobody"), "/apps/org.acme.paid/", fetch_redirect_response=False)   # an unknown name: no click, no error
        self.assertEqual(Click.objects.count(), 1)
        self.client.get("/r/anna/org.acme.paid/?s=newsletter-may")
        self.assertEqual(Click.objects.first().label, "newsletter-may")

    def test_a_link_counts_for_ninety_days(self):
        self.follow()
        Click.objects.update(at=timezone.now() - dt.timedelta(days=91))
        self.buy()
        self.assertEqual(Commission.objects.count(), 0)
        self.follow()
        Click.objects.filter(pk=Click.objects.first().pk).update(at=timezone.now() - dt.timedelta(days=89))
        self.buy()
        self.assertEqual(Commission.objects.count(), 1)

    def test_who_does_not_earn(self):
        # The buyer themselves.
        self.follow()
        self.buy(email="Anna@Example.org")
        # One of the seller's own people.
        helper = self.affiliate("helper")
        PublisherMember.objects.create(publisher=self.publisher, user=helper.user, role=PublisherMember.ANALYST)
        self.follow("helper")
        self.buy()
        # Someone not active yet, and someone paused.
        self.affiliate("newbie", active=False)
        self.follow("newbie")
        self.buy()
        self.assertEqual((Commission.objects.count(), Click.objects.filter(affiliate__handle="newbie").count()), (0, 0))

    def test_a_client_sends_the_links_token_with_the_checkout(self):
        token = services.record_click(self.anna, self.app).token
        order = self.checkout(ref=token)
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        self.assertEqual(Commission.objects.get().affiliate, self.anna)
        self.checkout(ref="not-a-token")
        self.assertEqual(Attribution.objects.count(), 1)

    @override_settings(AFFILIATES=False)
    def test_switched_off_there_is_nothing(self):
        self.assertEqual(self.follow().status_code, 404)
        self.sign_in("anna@example.org")
        self.assertEqual(self.client.get("/affiliate/").status_code, 404)
        token = Click.objects.create(affiliate=self.anna, app=self.app).token
        order = self.checkout(ref=token)
        self.client.post(f"/pay/{order['order_id']}/test/", {"outcome": "paid"})
        self.assertEqual((Attribution.objects.count(), Commission.objects.count()), (0, 0))


class BoostTests(AffiliateCase):
    def test_the_boost_comes_out_of_the_publishers_share_and_nothing_else_does(self):
        self.boost("10")
        self.follow()
        payment = self.buy()
        line = Commission.objects.get()
        self.assertEqual((line.base_percent, line.boost_percent, line.operator_part, line.merchant_part, line.amount),
                         (D("5"), D("10"), D("1.00"), D("1.99"), D("2.99")))
        payment.refresh_from_db()
        self.assertEqual((payment.boost_amount, payment.publisher_share), (D("1.99"), D("14.92")))   # 16.91 less the boost
        self.assertEqual(payouts.owed(self.publisher), [{"currency": "EUR", "amount": D("14.92"), "payments": 1}])
        # A sale no link brought costs the publisher nothing, boost or no boost.
        self.client.session.flush()
        plain = self.buy()
        self.assertEqual((plain.boost_amount, plain.publisher_share), (D("0"), D("16.91")))

    def test_a_boost_is_one_to_forty_five_percent_and_the_owners_to_set(self):
        for percent in ("0.5", "46", "0", "-3"):
            with self.assertRaises(services.AffiliateError):
                self.boost(percent)
        self.boost("1")
        self.boost("45")
        helper = User.objects.create_user("helper@example.org", PASSWORD, email_verified=True)
        PublisherMember.objects.create(publisher=self.publisher, user=helper, role=PublisherMember.DEVELOPER)
        with self.assertRaises(services.AffiliateError):
            services.save_boost(self.publisher, helper, scope=Boost.ALL, percent=D("5"))
        other = self.make_publisher("other@example.org", "other")
        with self.assertRaises(services.AffiliateError):   # another publisher's app
            services.save_boost(other, PublisherMember.objects.get(publisher=other).user, scope=Boost.APP, percent=D("5"), app=self.app)

    def test_the_most_specific_boost_in_force_counts(self):
        today = timezone.localdate()
        self.boost("3", scope=Boost.ALL, app=None)
        self.assertEqual(services.percent_for(self.app), (D("5"), D("3")))
        self.app.category = "office"
        self.app.save()
        self.boost("7", scope=Boost.CATEGORY, app=None, category="office")
        self.assertEqual(services.percent_for(self.app)[1], D("7"))
        self.boost("20", valid_from=today + dt.timedelta(days=1))          # tomorrow's is tomorrow's
        self.assertEqual(services.percent_for(self.app)[1], D("7"))
        self.assertEqual(services.percent_for(self.app, today + dt.timedelta(days=1))[1], D("20"))
        self.boost("12", valid_until=today - dt.timedelta(days=1))         # over
        self.assertEqual(services.percent_for(self.app)[1], D("7"))
        self.boost("9")
        self.assertEqual(services.percent_for(self.app)[1], D("9"))

    def test_a_boost_changed_after_a_sale_does_not_change_that_sale(self):
        row = self.boost("10")
        self.follow()
        self.buy()
        row.delete()
        self.boost("40")
        self.assertEqual(Commission.objects.get().amount, D("2.99"))

    def test_the_publishers_page(self):
        self.sign_in("dev@example.org")
        page = self.client.get("/console/affiliates/")
        self.assertContains(page, "nothing to switch on")
        self.assertContains(page, "Add a boost")
        self.client.post("/console/affiliates/", {"scope": "app", "app": self.app.pk, "category": "", "percent": "10", "valid_from": "", "valid_until": ""})
        row = Boost.objects.get()
        self.assertEqual((row.percent, row.app), (D("10"), self.app))
        self.assertContains(self.client.post("/console/affiliates/", {"scope": "all", "percent": "60"}), "from 1 to 45 percent")
        self.follow()
        self.buy()
        page = self.client.get("/console/affiliates/")
        self.assertContains(page, "anna")
        self.assertContains(page, "1.99 EUR")
        self.assertNotContains(page, "buyer@example.org")
        sales = self.client.get("/console/sales/")
        self.assertContains(sales, "<b>14.92</b>", html=True)
        helper = User.objects.create_user("helper@example.org", PASSWORD, email_verified=True)
        PublisherMember.objects.create(publisher=self.publisher, user=helper, role=PublisherMember.ANALYST)
        self.sign_in("helper@example.org")
        page = self.client.get("/console/affiliates/")           # another member reads it, and changes nothing
        self.assertNotContains(page, "Add a boost")
        self.client.post("/console/affiliates/", {"delete": row.pk})
        self.assertTrue(Boost.objects.filter(pk=row.pk).exists())
        self.sign_in("dev@example.org")
        self.client.post("/console/affiliates/", {"delete": row.pk})
        self.assertFalse(Boost.objects.filter(pk=row.pk).exists())


class SuperaffiliateTests(AffiliateCase):
    def test_the_superaffiliate_earns_a_share_on_top_one_level_up(self):
        rita = self.affiliate("rita", recruiter="anna")
        tom = self.affiliate("tom", recruiter="rita")
        self.assertEqual((rita.superaffiliate, tom.superaffiliate), (self.anna, rita))
        self.boost("10")
        self.follow("tom")
        self.buy()
        lines = {(line.affiliate.handle, line.basis): line for line in Commission.objects.all()}
        self.assertEqual(set(lines), {("tom", "product_link"), ("rita", "super")})          # anna, two levels up: nothing
        self.assertEqual((lines["tom", "product_link"].amount, lines["rita", "super"].amount), (D("2.99"), D("0.30")))   # 10 % of 2.99, on top
        share = lines["rita", "super"]
        self.assertEqual((share.share_of, share.operator_part, share.merchant_part), (lines["tom", "product_link"], D("0.30"), D("0")))
        self.assertEqual(Payment.objects.get().boost_amount, D("1.99"))                      # the publisher pays the boost and no share
        self.assertEqual(services.cost(), [{"currency": "EUR", "operator": D("1.30"), "merchants": D("1.99"), "total": D("3.29")}])

    def test_a_share_set_for_one_affiliate_and_none(self):
        rita = self.affiliate("rita", recruiter="anna")
        rita.super_percent = D("25")
        rita.save()
        self.follow("rita")
        self.buy()
        self.assertEqual(Commission.objects.get(basis="super").amount, D("0.25"))
        rita.super_percent = D("0")
        rita.save()
        self.follow("rita")
        self.buy()
        self.assertEqual(Commission.objects.filter(basis="super").count(), 1)

    def test_a_recruiting_link_sets_the_superaffiliate_once(self):
        self.client.get("/affiliate/join/anna/")
        user = User.objects.create_user("rita@example.org", PASSWORD, email_verified=True)
        self.sign_in("rita@example.org")
        page = self.client.get("/affiliate/")
        self.assertContains(page, "Become an affiliate")
        session = self.client.session
        session["affiliate_recruiter"] = "anna"   # the link was followed in this browser
        session.save()
        self.client.post("/affiliate/", {"handle": "rita", "phone": "+49 170 7654321", "website": "", "channels": "a blog", "legal_name": "",
                                         "vat_id": "", "payout_details": "", "accept_terms": "on"})
        rita = AffiliateProfile.objects.get(user=user)
        self.assertEqual((rita.superaffiliate, rita.state, rita.phone), (self.anna, "applied", "+491707654321"))
        with self.assertRaises(services.AffiliateError):
            services.apply(user, handle="rita2", phone="+491700000000")              # one profile per account
        self.assertIsNone(self.affiliate("solo", recruiter="nobody").superaffiliate)
        self.assertIsNone(self.affiliate("late", recruiter="blocked.one").superaffiliate)


class MoneyTests(AffiliateCase):
    def test_pending_then_approved_then_paid(self):
        self.follow()
        self.buy()
        today = timezone.localdate()
        self.assertEqual(self.money(), (D("1.00"), D("0.00"), D("0.00"), D("0.00"), D("0.00")))
        self.assertEqual(services.build(today, self.operator), [])                                # pending is not owed yet
        self.assertEqual(services.approve_due(today + dt.timedelta(days=29)), 0)
        self.assertEqual(self.approve(), 1)
        self.assertEqual((Commission.objects.get().approved_on, self.money()), (today, (D("0.00"), D("0.00"), D("1.00"), D("0.00"), D("0.00"))))
        with self.assertRaises(services.AffiliateError):
            services.build(today + dt.timedelta(days=1), self.operator)                           # not for days that have not come
        payout, = services.build(today, self.operator)
        self.assertEqual((payout.amount, payout.details, payout.state, payout.until, payout.regardless), (D("1.00"), "IBAN of anna", "open", today, False))
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("0.00"), D("1.00"), D("0.00")))
        self.assertEqual(services.build(today, self.operator), [])                                # nothing is in two payouts
        with self.assertRaises(services.AffiliateError):
            services.mark_paid(payout, self.operator, "  ")                                       # no reference, not paid
        paid = services.mark_paid(payout, self.operator, "SEPA 17", "November")
        self.assertEqual((paid.state, paid.reference, paid.note, paid.paid_by), ("paid", "SEPA 17", "November", self.operator))
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("0.00"), D("0.00"), D("1.00")))
        with self.assertRaises(services.AffiliateError):
            services.mark_paid(payout, self.operator, "again")                                    # nothing is paid twice
        self.assertEqual((AuditEntry.objects.filter(action="affiliate.payout_made").count(), AuditEntry.objects.filter(action="affiliate.paid").count()), (1, 1))

    def test_the_command(self):
        self.follow()
        self.buy()
        Commission.objects.update(approve_after=timezone.localdate())
        Click.objects.update(at=timezone.now() - dt.timedelta(days=120))
        out = io.StringIO()
        call_command("affiliates", "maintain", stdout=out)
        self.assertIn("1 commission(s) approved, 0 holdback(s) released, 0 block(s) ended, 1 click(s) counted and removed", out.getvalue())
        self.assertEqual((Click.objects.count(), ClickDay.objects.get().clicks), (0, 1))
        out = io.StringIO()
        call_command("affiliates", "owed", stdout=out)
        self.assertIn("anna\t1.00 EUR\theld back 0.00", out.getvalue())
        out = io.StringIO()
        call_command("affiliates", "payouts", "--until", timezone.localdate().isoformat(), stdout=out)
        payout = AffiliatePayout.objects.get()
        self.assertIn(f"{payout.pk}\tanna\t1.00 EUR\tuntil {timezone.localdate()}\n1 payout(s) made", out.getvalue())
        out = io.StringIO()
        call_command("affiliates", "payouts", stdout=out)                       # by default up to the end of last month
        self.assertIn("0 payout(s) made", out.getvalue())
        self.assertEqual(services.default_until(dt.date(2027, 3, 15)), dt.date(2027, 2, 28))
        out = io.StringIO()
        call_command("affiliates", "cost", stdout=out)
        self.assertIn("EUR\t1.00\tfrom the store's fee 1.00\tfrom publishers' boosts 0.00", out.getvalue())


@override_settings(**SKRILL, AFFILIATES=True)
class ReversalTests(AffiliateCase):
    provider = SKRILL

    def test_a_refund_in_parts_takes_everything_back_in_proportion(self):
        rita = self.affiliate("rita", recruiter="anna")
        self.boost("10")
        token = services.record_click(rita, self.app).token
        order = self.checkout(ref=token)
        self.report(order["order_id"])
        found = Order.objects.get(public_id=order["order_id"])
        self.sign_in("operator@example.org")
        self.answers += ["<response><sid>s1</sid></response>", "<response><mb_transaction_id>900200</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "5.00"})
        part = Payment.objects.get(kind="refund")
        back = {line.basis: line for line in Commission.objects.filter(payment=part)}
        # 5.00 of 19.90: 2.99 * 5 / 19.90 = 0.751 of the commission, 1.99 * 5 / 19.90 = 0.50 of the boost.
        self.assertEqual((back["product_link"].amount, back["product_link"].merchant_part, back["product_link"].state, back["super"].amount),
                         (D("-0.75"), D("-0.50"), "reversal", D("-0.08")))
        self.assertEqual((part.boost_amount, part.signed_boost_amount), (D("0.50"), D("-0.50")))
        self.answers += ["<response><sid>s2</sid></response>", "<response><mb_transaction_id>900201</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "14.90"})
        for profile in (rita, self.anna):
            self.assertEqual(sum(line.amount for line in Commission.objects.filter(affiliate=profile)), D("0.00"))
        self.assertEqual(sum(line.merchant_part for line in Commission.objects.all()), D("0.00"))
        self.assertEqual(sum(p.signed_publisher_share for p in Payment.objects.all()), D("0.00"))
        self.assertEqual(sum(p.signed_boost_amount for p in Payment.objects.all()), D("0.00"))
        self.assertEqual(sum(line.net for line in Commission.objects.filter(basis="product_link")), D("0.00"))   # and what it was reckoned on

    def test_a_chargeback_after_the_payout_leaves_the_affiliate_owing(self):
        token = services.record_click(self.anna, self.app).token
        order = self.checkout(ref=token)
        self.report(order["order_id"])
        self.approve()
        self.pay()
        self.report(order["order_id"], status="-3")
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("-1.00"), D("0.00"), D("1.00")))
        self.assertEqual(self.pay(), [])                                         # nothing is owed; the next sale makes it up
        again = self.checkout(ref=services.record_click(self.anna, self.app).token)
        self.report(again["order_id"], mb_transaction_id="900300")
        self.approve()
        self.assertEqual((self.money()[2], self.pay()), (D("0.00"), []))


class IdentityTests(AffiliateCase):
    def test_a_profile_is_active_once_the_phone_and_the_identity_are_proven(self):
        ben = self.affiliate("ben", active=False)
        self.assertEqual((ben.state, ben.identified), ("applied", False))
        services.send_phone_code(ben)
        number, text = texts.outbox[-1]
        self.assertEqual(number, "+491701234567")
        code = text.split()[0]
        self.assertFalse(services.confirm_phone(ben, "000000" if code != "000000" else "111111"))
        self.assertTrue(services.confirm_phone(ben, code))
        self.assertFalse(services.confirm_phone(ben, code))                       # a code is good once
        ben.refresh_from_db()
        self.assertIsNotNone(ben.phone_verified_at)
        facts = dict(id_name="Ben Example", id_birth_date=BORN, id_document="id_card", id_country="at", id_expires=None)
        with self.assertRaises(services.AffiliateError):                          # under eighteen
            services.activate(ben, self.operator, **{**facts, "id_birth_date": timezone.localdate() - dt.timedelta(days=17 * 365)})
        with self.assertRaises(services.AffiliateError):                          # an expired document
            services.activate(ben, self.operator, **{**facts, "id_expires": timezone.localdate() - dt.timedelta(days=1)})
        services.activate(ben, self.operator, **facts)
        ben.refresh_from_db()
        self.assertEqual((ben.state, ben.identified, ben.id_country, ben.id_checked_by), ("active", True, "AT", self.operator))

    def test_one_person_one_profile(self):
        twin = self.affiliate("twin", active=False)
        with self.assertRaises(services.AffiliateError) as refused:
            services.activate(twin, self.operator, id_name="anna example", id_birth_date=BORN, id_document="passport", id_country="de",
                              id_expires=None, phone_confirmed=True)
        self.assertIn("anna", str(refused.exception))

    def test_the_code_runs_out_and_five_tries_are_all(self):
        ben = self.affiliate("ben", active=False)
        services.send_phone_code(ben)
        code = texts.outbox[-1][1].split()[0]
        for _ in range(5):
            services.confirm_phone(ben, "999999" if code != "999999" else "888888")
        self.assertFalse(services.confirm_phone(ben, code))                       # the right code, too late
        services.send_phone_code(ben)
        code = texts.outbox[-1][1].split()[0]
        AffiliateProfile.objects.filter(pk=ben.pk).update(phone_code_expires=timezone.now() - dt.timedelta(minutes=1))
        ben.refresh_from_db()
        self.assertFalse(services.confirm_phone(ben, code))
        with self.assertRaises(services.AffiliateError):                          # not proven, and the operator did not confirm it
            services.activate(ben, self.operator, id_name="Ben", id_birth_date=BORN, id_document="passport", id_country="DE", id_expires=None)

    def test_the_pages_of_becoming_an_affiliate(self):
        User.objects.create_user("carl@example.org", PASSWORD, email_verified=True)
        self.sign_in("carl@example.org")
        taken = self.client.post("/affiliate/", {"handle": "Anna", "phone": "+491700000001", "accept_terms": "on"})
        self.assertContains(taken, "That name is taken.")
        bad = self.client.post("/affiliate/", {"handle": "carl", "phone": "0170 1234", "accept_terms": "on"})
        self.assertContains(bad, "With the country&#x27;s code")
        self.client.post("/affiliate/", {"handle": "carl", "phone": "+491700000001", "accept_terms": "on"})
        carl = AffiliateProfile.objects.get(handle="carl")
        page = self.client.get("/affiliate/")
        self.assertContains(page, "Prove your phone number")
        self.assertNotContains(page, "/r/carl/")                                  # no links before the review
        self.client.post("/affiliate/phone/")
        code = texts.outbox[-1][1].split()[0]
        self.client.post("/affiliate/phone/", {"code": code})
        self.assertContains(self.client.get("/affiliate/"), "Your phone number is proven.")
        self.assertEqual(self.follow("carl").status_code, 302)
        self.assertEqual(Click.objects.filter(affiliate=carl).count(), 0)         # not active: the link counts nothing
        # The operator's review.
        self.sign_in("dev@example.org")
        self.assertEqual(self.client.get("/operator/affiliates/").status_code, 403)
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/affiliates/"), "carl")
        self.assertContains(self.client.get(f"/operator/affiliates/{carl.pk}/"), "No picture of it is kept here")
        self.client.post(f"/operator/affiliates/{carl.pk}/", {"action": "activate", "review-id_name": "Carl Example", "review-id_birth_date": "1985-01-02",
                                                               "review-id_document": "passport", "review-id_country": "de", "review-id_expires": ""})
        carl.refresh_from_db()
        self.assertEqual((carl.state, carl.id_name, carl.id_country), ("active", "Carl Example", "DE"))
        self.sign_in("carl@example.org")
        page = self.client.get("/affiliate/")
        self.assertContains(page, "/r/carl/org.acme.paid/")
        self.assertContains(page, "/affiliate/join/carl/")
        products = self.client.get("/affiliate/products/?q=paid")
        self.assertContains(products, "<b>5 %</b>", html=True)
        self.assertContains(self.client.get("/affiliate/products/?q=nothing-like-it"), "Nothing found.")


class SanctionTests(AffiliateCase):
    def test_a_blocked_affiliate_earns_nothing_and_has_the_reasons(self):
        self.follow()
        self.buy()
        with self.assertRaises(services.AffiliateError):
            services.sanction(self.anna, self.operator, Sanction.BLOCK, rule="", facts="something")     # no rule named
        until = timezone.localdate() + dt.timedelta(days=14)
        row = services.sanction(self.anna, self.operator, Sanction.BLOCK, rule="Terms 4.2: no advertising on a merchant's name",
                                facts="Search ads on 'Acme' on 3 and 4 November.", evidence="Screenshots, kept by support.", until=until,
                                money="The two sales of those days are taken back.")
        self.anna.refresh_from_db()
        self.assertEqual((self.anna.state, row.decided_by, row.until), ("blocked", self.operator, until))
        self.follow()
        self.buy()
        self.assertEqual((Click.objects.count(), Commission.objects.count()), (1, 1))                    # no new click, no new commission
        self.assertEqual(services.approve_due(timezone.localdate() + dt.timedelta(days=60)), 0)         # what it earned is held
        self.assertEqual(services.build(timezone.localdate(), self.operator), [])
        self.sign_in("anna@example.org")
        page = self.client.get("/affiliate/")
        self.assertContains(page, "Your affiliate profile is blocked until")
        self.assertContains(page, "Terms 4.2")
        self.assertContains(page, "Search ads on")
        self.assertNotContains(page, "Screenshots, kept by support.")                                   # the evidence is the operator's to give out
        self.client.post(f"/affiliate/statement/{row.pk}/", {"statement": "That campaign was not mine."})
        row.refresh_from_db()
        self.assertIn("That campaign was not mine.", row.statement)
        self.assertEqual(services.end_blocks(until), 0)
        self.assertEqual(services.end_blocks(until + dt.timedelta(days=1)), 1)                          # its time is over
        self.anna.refresh_from_db()
        self.assertEqual((self.anna.state, [s.kind for s in self.anna.sanctions.all()]), ("active", ["lift", "block"]))

    def test_a_ban_ends_the_relations_and_stays(self):
        rita = self.affiliate("rita", recruiter="anna")
        tom = self.affiliate("tom", recruiter="rita")
        self.sign_in("operator@example.org")
        self.client.post(f"/operator/affiliates/{rita.pk}/", {"action": "sanction", "sanction-kind": "ban", "sanction-rule": "Terms 6: fraud",
                                                               "sanction-facts": "Orders with stolen cards.", "sanction-evidence": "", "sanction-money": ""})
        for profile in (rita, tom):
            profile.refresh_from_db()
        self.assertEqual((rita.state, rita.superaffiliate, tom.superaffiliate), ("banned", None, None))
        with self.assertRaises(services.AffiliateError):
            services.lift(rita, self.operator)                                                         # a ban is not a block
        with self.assertRaises(services.AffiliateError):
            services.sanction(rita, self.operator, Sanction.BLOCK, rule="x", facts="y")
        page = self.client.get(f"/operator/affiliates/{rita.pk}/")
        self.assertContains(page, "Orders with stolen cards.")
        self.assertNotContains(page, "Block or ban</h3>")
        self.assertEqual(AuditEntry.objects.filter(action="affiliate.ban").count(), 1)
        # The same person does not come back as somebody else.
        again = self.affiliate("rita2", active=False)
        with self.assertRaises(services.AffiliateError):
            services.activate(again, self.operator, id_name="Rita Example", id_birth_date=BORN, id_document="passport", id_country="DE",
                              id_expires=None, phone_confirmed=True)

    def test_the_operator_pays_from_the_page(self):
        self.follow()
        self.buy()
        self.approve()
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/affiliates/?state=active"), "1.00 EUR")
        self.client.post(f"/operator/affiliates/{self.anna.pk}/", {"action": "payout"})
        payout = AffiliatePayout.objects.get()
        self.assertEqual((payout.state, payout.amount, payout.created_by), ("open", D("1.00"), self.operator))
        self.assertContains(self.client.get("/operator/affiliates/?state=active"), "Payouts - 1 to pay")
        self.assertContains(self.client.get("/operator/affiliates/payouts/"), "IBAN of anna", count=0)
        page = self.client.get(f"/operator/affiliates/payouts/{payout.pk}/")
        self.assertContains(page, "IBAN of anna")
        self.assertContains(page, "Mark as paid")
        self.client.post(f"/operator/affiliates/payouts/{payout.pk}/", {"reference": "SEPA 99"})
        self.assertEqual(AffiliatePayout.objects.get().reference, "SEPA 99")
        self.assertContains(self.client.get("/operator/affiliates/payouts/?state=paid"), "SEPA 99")
        self.sign_in("anna@example.org")
        self.assertContains(self.client.get("/affiliate/"), "SEPA 99")
        statement = self.client.get(f"/affiliate/payouts/{payout.pk}/")
        self.assertContains(statement, "not a tax document")
        self.assertContains(statement, "SEPA 99")
        self.sign_in("dev@example.org")                                          # nobody else's
        self.assertEqual(self.client.get(f"/affiliate/payouts/{payout.pk}/").status_code, 404)
        self.assertEqual(self.client.get(f"/operator/affiliates/payouts/{payout.pk}/").status_code, 403)


class AccountTests(AffiliateCase):
    def test_the_profile_follows_the_account(self):
        rita = self.affiliate("rita", recruiter="anna")
        closure.close(self.anna.user)
        self.anna.refresh_from_db()
        self.assertEqual(self.anna.state, "closed")
        self.follow()
        self.assertEqual(Click.objects.count(), 0)                    # a closed account's links count nothing
        closure.reactivate(User.objects.get(pk=self.anna.user_id))
        self.anna.refresh_from_db()
        self.assertEqual(self.anna.state, "active")
        closure.request_deletion(self.anna.user)
        closure.erase(User.objects.get(pk=self.anna.user_id))
        self.anna.refresh_from_db()
        rita.refresh_from_db()
        self.assertEqual((self.anna.state, self.anna.handle, self.anna.phone, self.anna.id_name, self.anna.payout_details, rita.superaffiliate),
                         ("closed", f"erased.{self.anna.pk}", "", "", "", None))
        # The name is free again, and the person is nobody the store knows.
        self.assertIsNotNone(self.affiliate("anna"))


class BindingCase(AffiliateCase):
    """Phase 2: orders know their buyer, and a customer is bound to the
    affiliate who brought them."""

    def setUp(self):
        super().setUp()
        self.other = self.make_app(self.publisher, "org.acme.notes", name="Notes", pricing_model="one-time", price="10.00", currency="EUR")
        self.make_release(self.other)
        self.bert = self.affiliate("bert")
        self.carl = User.objects.create_user("carl@example.org", PASSWORD, name="Carl", email_verified=True)

    def purchase(self, buyer, app=None, *, ref: AffiliateProfile | None = None, via: AffiliateProfile | None = None, clicked_days_ago: int = 0) -> Payment:
        """An order by this account, with the link (to the product) or the
        last link (to anything) it followed, paid."""
        app = app or self.app
        def click(profile, target):
            row = services.record_click(profile, target)
            if clicked_days_ago:
                Click.objects.filter(pk=row.pk).update(at=timezone.now() - dt.timedelta(days=clicked_days_ago))
            return row.token
        order = billing.create_order(app.app_id, email=buyer.email if buyer else "guest@example.org", buyer=buyer,
                                     ref=click(ref, app) if ref else "", via=click(via, None) if via else "")
        self.client.post(f"/pay/{order.public_id}/test/", {"outcome": "paid"})
        return Payment.objects.get(order=order, kind="sale")

    def bound_to(self, user) -> str:
        row = services.binding_of(user)
        return f"{row.affiliate.handle}:{row.state}" if row else ""

    def line_of(self, payment: Payment) -> tuple:
        row = Commission.objects.filter(payment=payment).exclude(basis="super").first()
        return (row.affiliate.handle, row.basis, row.amount) if row else ()


class BindingTests(BindingCase):
    def test_somebody_new_who_signs_up_through_a_link_is_that_affiliates_customer(self):
        self.follow("anna", app_id="")                                           # a link to the shop is enough
        self.client.post("/account/signup/", {"name": "Dora", "email": "dora@example.org", "password": PASSWORD, "password_again": PASSWORD})
        dora = User.objects.get(email="dora@example.org")
        row = Binding.objects.get()
        self.assertEqual((row.customer, row.affiliate, row.through, row.state), (dora, self.anna, "signup", "active"))
        self.boost("10")
        first, second = self.purchase(dora), self.purchase(dora, self.other)
        # Everything she buys earns anna the base - 5 % - and no boost: nobody promoted these.
        self.assertEqual((self.line_of(first), self.line_of(second)), (("anna", "bound_customer", D("1.00")), ("anna", "bound_customer", D("0.50"))))
        first.refresh_from_db()
        self.assertEqual((first.boost_amount, first.publisher_share), (D("0"), D("16.91")))   # the publisher pays nothing for it
        self.assertEqual(Commission.objects.get(payment=first).binding, row)
        User.objects.filter(pk=dora.pk).update(email_verified=True)
        self.sign_in("dora@example.org")
        page = self.client.get("/account/")
        self.assertContains(page, "Who referred you")
        self.assertContains(page, "<b>anna</b>", html=True)
        self.sign_in("anna@example.org")
        page = self.client.get("/affiliate/")
        self.assertContains(page, "Your customers")
        self.assertContains(page, f"your customer no. {row.pk}")
        self.assertNotContains(page, "dora")                                     # a number, never who

    def test_somebody_new_is_bound_by_the_first_order_and_the_link_earns_its_product(self):
        self.boost("10")
        first = self.purchase(self.carl, ref=self.anna)                           # a link to the product
        self.assertEqual((self.line_of(first), self.bound_to(self.carl)), (("anna", "product_link", D("2.99")), "anna:active"))
        self.assertEqual(Binding.objects.get().through, "first_order")
        later = self.purchase(self.carl, self.other)                             # no link: her customer
        self.assertEqual(self.line_of(later), ("anna", "bound_customer", D("0.50")))

    def test_a_link_to_a_product_wins_that_product_and_the_binding_stays(self):
        self.purchase(self.carl, via=self.anna)                                   # carl is anna's
        self.boost("10")
        won = self.purchase(self.carl, ref=self.bert)                             # bert's link to this very product
        self.assertEqual((self.line_of(won), self.bound_to(self.carl)), (("bert", "product_link", D("2.99")), "anna:active"))
        self.assertEqual(Commission.objects.filter(payment=won).exclude(basis="super").count(), 1)   # one commission, never two
        rest = self.purchase(self.carl, self.other, via=self.bert)                # bert's link to the shop moves nothing
        self.assertEqual((self.line_of(rest), self.bound_to(self.carl)), (("anna", "bound_customer", D("0.50")), "anna:active"))

    def test_a_customer_who_is_buying_cannot_be_claimed(self):
        earlier = self.purchase(self.carl)                                        # no affiliate: carl is nobody's
        self.assertEqual((self.line_of(earlier), self.bound_to(self.carl)), ((), ""))
        again = self.purchase(self.carl, self.other, via=self.bert)
        self.assertEqual((self.line_of(again), self.bound_to(self.carl)), ((), ""))
        linked = self.purchase(self.carl, ref=self.bert)                          # a product link still earns its product
        self.assertEqual((self.line_of(linked), self.bound_to(self.carl)), (("bert", "product_link", D("1.00")), ""))
        # Somebody who bought as a guest under the same address is no new customer either.
        self.purchase(None)
        guest = User.objects.create_user("guest@example.org", PASSWORD, email_verified=True)
        self.assertIsNone(services.signed_up(guest, services.record_click(self.anna, None).token))
        self.purchase(guest, via=self.anna)
        self.assertEqual(self.bound_to(guest), "")

    def test_a_link_counts_for_ninety_days_towards_a_new_customer(self):
        self.purchase(self.carl, via=self.anna, clicked_days_ago=91)
        self.assertEqual(self.bound_to(self.carl), "")
        dora = User.objects.create_user("dora@example.org", PASSWORD, email_verified=True)
        self.purchase(dora, via=self.anna, clicked_days_ago=89)
        self.assertEqual(self.bound_to(dora), "anna:active")
        old = services.record_click(self.anna, None)
        Click.objects.filter(pk=old.pk).update(at=timezone.now() - dt.timedelta(days=91))
        self.assertIsNone(services.signed_up(User.objects.create_user("eve@example.org", PASSWORD), old.token))

    def test_nobody_is_their_own_customer(self):
        self.purchase(self.anna.user, via=self.anna)
        self.purchase(self.anna.user, ref=self.anna)
        self.assertEqual((Binding.objects.count(), Commission.objects.count()), (0, 0))
        self.assertIsNone(services.signed_up(self.anna.user, services.record_click(self.anna, None).token))

    def test_a_customer_who_stopped_buying_is_won_back(self):
        first = self.purchase(self.carl, via=self.anna)
        seven_months = timezone.now() - dt.timedelta(days=215)
        # Five months without a purchase: still anna's, whatever link he follows.
        Payment.objects.filter(pk=first.pk).update(at=timezone.now() - dt.timedelta(days=150))
        kept = self.purchase(self.carl, self.other, via=self.bert)
        self.assertEqual((self.line_of(kept), self.bound_to(self.carl)), (("anna", "bound_customer", D("0.50")), "anna:active"))
        # Seven months, and a link followed fifteen days before the purchase: too long ago.
        Payment.objects.update(at=seven_months)
        late = self.purchase(self.carl, self.other, via=self.bert, clicked_days_ago=15)
        self.assertEqual((self.line_of(late), self.bound_to(self.carl)), (("anna", "bound_customer", D("0.50")), "anna:active"))
        # Seven months, and bought within the two weeks: bert's from this order on.
        Payment.objects.update(at=seven_months)
        won = self.purchase(self.carl, self.other, via=self.bert, clicked_days_ago=13)
        self.assertEqual((self.line_of(won), self.bound_to(self.carl)), (("bert", "bound_customer", D("0.50")), "bert:active"))
        history = [(b.affiliate.handle, b.state, b.through, b.ended_because) for b in Binding.objects.order_by("id")]
        self.assertEqual(history, [("anna", "ended", "first_order", "won_back"), ("bert", "active", "won_back", "")])
        self.assertEqual(AuditEntry.objects.filter(action="affiliate.bound").count(), 2)

    def test_a_lapsed_customer_with_nobody_is_bound_the_same_way(self):
        first = self.purchase(self.carl)
        Payment.objects.filter(pk=first.pk).update(at=timezone.now() - dt.timedelta(days=215))
        self.purchase(self.carl, self.other, via=self.bert)
        self.assertEqual((self.bound_to(self.carl), Binding.objects.get().through), ("bert:active", "won_back"))

    def test_six_months_are_months(self):
        moment = timezone.now().replace(year=2026, month=8, day=31, hour=12)
        self.assertEqual((services.months_before(moment, 6).month, services.months_before(moment, 6).day), (2, 28))
        self.assertEqual(services.months_before(moment.replace(month=3, day=15), 6).strftime("%Y-%m-%d"), "2025-09-15")

    def test_the_binding_follows_the_customers_account(self):
        first = self.purchase(self.carl, via=self.anna)
        closure.request_deletion(self.carl)                                       # suspended from the request on
        self.assertEqual(self.bound_to(self.carl), "anna:suspended")
        during = self.purchase(self.carl, self.other)
        self.assertEqual(self.line_of(during), ())
        linked = self.purchase(self.carl, ref=self.bert)                          # a product link still earns its product
        self.assertEqual(self.line_of(linked)[:2], ("bert", "product_link"))
        closure.cancel_deletion(self.carl)                                        # and back, to the same affiliate
        self.assertEqual(self.bound_to(self.carl), "anna:active")
        closure.close(self.carl)
        self.assertEqual(self.bound_to(self.carl), "anna:suspended")
        closure.reactivate(User.objects.get(pk=self.carl.pk))
        self.assertEqual(self.bound_to(self.carl), "anna:active")
        closure.request_deletion(self.carl)
        closure.erase(User.objects.get(pk=self.carl.pk))
        self.assertEqual(Binding.objects.count(), 0)                              # gone with the account
        kept = Commission.objects.get(payment=first)
        self.assertEqual((kept.amount, kept.binding), (D("1.00"), None))          # what was earned stays, with nobody behind it

    def test_a_ban_and_an_erased_affiliate_end_their_bindings(self):
        dora = User.objects.create_user("dora@example.org", PASSWORD, email_verified=True)
        self.purchase(self.carl, via=self.anna)
        self.purchase(dora, via=self.bert)
        services.sanction(self.anna, self.operator, Sanction.BAN, rule="Terms 6", facts="Fraud.")
        self.assertEqual((self.bound_to(self.carl), Binding.objects.get(customer=self.carl).ended_because), ("", "affiliate_banned"))
        self.assertEqual(self.line_of(self.purchase(self.carl, self.other)), ())
        closure.request_deletion(self.bert.user)
        closure.erase(User.objects.get(pk=self.bert.user_id))
        self.assertEqual((self.bound_to(dora), Binding.objects.get(customer=dora).ended_because), ("", "affiliate_erased"))
        # A blocked affiliate's customers stay theirs, and earn them nothing meanwhile.
        eve = User.objects.create_user("eve@example.org", PASSWORD, email_verified=True)
        fred = self.affiliate("fred")
        self.purchase(eve, via=fred)
        services.sanction(fred, self.operator, Sanction.BLOCK, rule="Terms 4", facts="Under review.")
        self.assertEqual((self.bound_to(eve), self.line_of(self.purchase(eve, self.other))), ("fred:active", ()))

    def test_a_referred_customer_who_becomes_an_affiliate_has_their_affiliate_above(self):
        self.purchase(self.carl, via=self.anna)
        profile = services.apply(self.carl, handle="carl", phone="+491700000002", recruiter="bert")   # bert's recruiting link, too
        self.assertEqual(profile.superaffiliate, self.anna)                        # whoever brought the customer brought the affiliate

    def test_the_superaffiliate_shares_in_a_bound_customers_purchase(self):
        rita = self.affiliate("rita", recruiter="anna")
        payment = self.purchase(self.carl, via=rita)
        lines = {(l.affiliate.handle, l.basis): l.amount for l in Commission.objects.filter(payment=payment)}
        self.assertEqual(lines, {("rita", "bound_customer"): D("1.00"), ("anna", "super"): D("0.10")})


class BuyerTests(BindingCase):
    def test_an_order_knows_the_account_that_bought(self):
        guest = self.checkout()
        self.assertIsNone(Order.objects.get(public_id=guest["order_id"]).buyer)   # nobody said who: nobody's, reached by its secret
        client = Client.objects.create(client_id="ultrastore-app", name="UltraStore", redirect_uris="https://store.example/cb")
        grant = Grant.objects.create(user=self.carl, client=client, scope="openid", auth_time=timezone.now())
        token = provider.issue_tokens(grant)["access_token"]
        mine = self.client.post("/v1/checkout", '{"app": "org.acme.paid"}', content_type="application/json", headers={"authorization": f"Bearer {token}"})
        self.assertEqual(Order.objects.get(public_id=mine.json()["order_id"]).buyer, self.carl)
        wrong = self.client.post("/v1/checkout", '{"app": "org.acme.paid"}', content_type="application/json", headers={"authorization": "Bearer nonsense"})
        self.assertEqual((wrong.status_code, Order.objects.get(public_id=wrong.json()["order_id"]).buyer), (200, None))
        self.assertNotIn("buyer", mine.json())                                    # the order's JSON says nothing of who
        self.assertIsNone(provider.person_of(""))
        # The buy page: whoever is signed in.
        self.sign_in("carl@example.org")
        self.client.post("/apps/org.acme.paid/buy/", {"email": "carl@example.org"})
        self.assertEqual(Order.objects.order_by("-id").first().buyer, self.carl)

    def test_a_guests_purchase_becomes_the_accounts_when_its_owner_opens_it_signed_in(self):
        self.client.post("/apps/org.acme.paid/buy/", {"email": "carl@example.org"})
        order = Order.objects.get()
        self.client.post(f"/pay/{order.public_id}/test/", {"outcome": "paid"})
        self.assertIsNone(order.buyer)
        self.sign_in("dev@example.org")                                            # somebody else with the link: not theirs
        self.client.get(f"/orders/{order.public_id}/?secret={order.secret}")
        self.assertIsNone(Order.objects.get(pk=order.pk).buyer)
        self.sign_in("carl@example.org")
        self.client.get(f"/orders/{order.public_id}/?secret={order.secret}")
        self.assertEqual(Order.objects.get(pk=order.pk).buyer, self.carl)

    def test_the_web_buy_page_carries_the_last_link_followed(self):
        self.sign_in("carl@example.org")
        self.follow("anna", app_id="")                                            # to the shop
        self.client.post("/apps/org.acme.paid/buy/", {"email": "carl@example.org"})
        order = Order.objects.get()
        self.assertEqual(OrderVisit.objects.get(order=order).affiliate, self.anna)
        self.client.post(f"/pay/{order.public_id}/test/", {"outcome": "paid"})
        self.assertEqual((self.bound_to(self.carl), self.line_of(Payment.objects.get())), ("anna:active", ("anna", "bound_customer", D("1.00"))))


@override_settings(**SKRILL, AFFILIATES=True)
class BoundRenewalTests(BindingCase):
    provider = SKRILL

    def test_a_bound_customers_subscription_earns_the_base_at_every_renewal(self):
        monthly = self.make_app(self.publisher, "org.acme.monthly", name="Monthly", pricing_model="monthly", price="4.00", currency="EUR")
        self.make_release(monthly)
        self.boost("10", app=monthly, renewals=True)
        order = billing.create_order("org.acme.monthly", email="carl@example.org", buyer=self.carl, via=services.record_click(self.anna, None).token)
        self.report(order.public_id, amount="4.00", rec_payment_id="REC1")
        self.report(order.public_id, amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900101")
        lines = [(l.payment.kind, l.basis, l.amount, l.merchant_part) for l in Commission.objects.order_by("id")]
        self.assertEqual(lines, [("sale", "bound_customer", D("0.20"), D("0.00")), ("renewal", "bound_customer", D("0.20"), D("0.00"))])
        self.report(order.public_id, status="-3", mb_transaction_id="900100")      # the first payment charged back
        self.assertEqual(sum(l.amount for l in Commission.objects.filter(affiliate=self.anna)), D("0.20"))

    def test_a_renewal_follows_the_customer(self):
        monthly = self.make_app(self.publisher, "org.acme.monthly", name="Monthly", pricing_model="monthly", price="4.00", currency="EUR")
        self.make_release(monthly)
        order = billing.create_order("org.acme.monthly", email="carl@example.org", buyer=self.carl, via=services.record_click(self.anna, None).token)
        self.report(order.public_id, amount="4.00", rec_payment_id="REC1")
        closure.request_deletion(self.carl)                                        # suspended: this renewal is nobody's
        self.report(order.public_id, amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900101")
        self.assertEqual(Commission.objects.count(), 1)
        # On the way out he is nobody's new customer either.
        Binding.objects.all().delete()
        other = billing.create_order("org.acme.notes", email="carl@example.org", buyer=self.carl, via=services.record_click(self.bert, None).token)
        self.report(other.public_id, amount="10.00", mb_transaction_id="900200")
        self.assertEqual((self.bound_to(self.carl), Commission.objects.count()), ("", 1))
        closure.cancel_deletion(self.carl)
        services._bind(self.carl, self.bert, Binding.WON_BACK)                     # won back by bert since
        self.report(order.public_id, amount="4.00", url="/v1/skrill/recurring", mb_transaction_id="900102")
        last = Commission.objects.order_by("-id").first()
        self.assertEqual((last.affiliate, last.basis, last.amount, last.payment.kind), (self.bert, "bound_customer", D("0.20"), "renewal"))


@override_settings(**SKRILL, AFFILIATES=True, AFFILIATE_HOLDBACK_PERCENT=Decimal("5"), AFFILIATE_PAYOUT_MINIMUM=Decimal("25.00"))
class MoneyRulesTests(AffiliateCase):
    """Phase 3 with the programme's own numbers: 5 % of an approved
    commission held back for six months from the sale, and a payout a month
    from 25.00 per currency."""
    provider = SKRILL

    def setUp(self):
        super().setUp()
        for app_id, price in (("org.acme.suite", "600.00"), ("org.acme.tool", "200.00")):
            self.make_release(self.make_app(self.publisher, app_id, name=app_id.rsplit(".", 1)[1].title(), pricing_model="one-time", price=price,
                                            currency="EUR"))
        self.number = 910000

    def sale(self, app_id: str = "org.acme.suite", amount: str = "600.00", *, affiliate: AffiliateProfile | None = None, days_ago: int = 0) -> Order:
        """A sale an affiliate's link brought, paid so many days ago."""
        app = self.app.__class__.objects.get(app_id=app_id)
        order = billing.create_order(app_id, email="buyer@example.org", ref=services.record_click(affiliate or self.anna, app).token)
        self.number += 1
        self.report(order.public_id, amount=amount, mb_transaction_id=str(self.number))
        if days_ago:
            Payment.objects.filter(order=order).update(at=timezone.now() - dt.timedelta(days=days_ago))
        return order

    def test_five_percent_are_held_for_six_months_from_the_sale(self):
        order = self.sale("org.acme.tool", "200.00")                             # a commission of 10.00
        self.assertEqual(self.money(), (D("10.00"), D("0.00"), D("0.00"), D("0.00"), D("0.00")))
        self.assertEqual((self.approve(), Holdback.objects.count()), (1, 1))
        held = Holdback.objects.get()
        paid_on = timezone.localtime(Payment.objects.get(order=order).at).date()
        self.assertEqual((held.amount, held.percent, held.release_on, held.released_on), (D("0.50"), D("5"), services.months_after(paid_on, 6), None))
        # 9.50 in the balance, 0.50 held - and the statistics say when it comes.
        row = services.sums(self.anna)[0]
        self.assertEqual((row["balance"], row["held"], row["next_release"], row["next_release_amount"], row["payable"]),
                         (D("9.50"), D("0.50"), held.release_on, D("0.50"), False))
        self.assertEqual(services.release_due(held.release_on - dt.timedelta(days=1)), 0)
        self.assertEqual(services.release_due(held.release_on), 1)
        self.assertEqual(services.release_due(held.release_on), 0)
        self.sign_in("anna@example.org")
        page = self.client.get("/affiliate/")
        self.assertContains(page, "Held back, EUR")
        self.assertContains(page, "5 % are held back for 6 months")
        self.assertContains(page, "Paid from 25.00; carried until then.")

    def test_six_months_are_months(self):
        self.assertEqual(services.months_after(dt.date(2026, 8, 31), 6), dt.date(2027, 2, 28))
        self.assertEqual(services.months_after(dt.date(2026, 11, 26), 6), dt.date(2027, 5, 26))
        self.assertEqual(services.months_before(dt.date(2027, 1, 15), 6), dt.date(2026, 7, 15))

    def test_a_late_chargeback_comes_out_of_the_holdback_first(self):
        order = self.sale("org.acme.tool", "200.00", days_ago=100)                # the fourth month
        self.approve()
        self.report(order.public_id, status="-3", amount="200.00", mb_transaction_id=str(self.number))
        back = Commission.objects.get(state="reversal")
        self.assertEqual((back.amount, back.from_holdback, back.payable, back.reverses, Holdback.objects.get().left),
                         (D("-10.00"), D("0.50"), D("-9.50"), Commission.objects.get(state="approved"), D("0.00")))
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("0.00"), D("0.00"), D("0.00")))
        Holdback.objects.update(release_on=timezone.localdate())
        services.release_due()
        self.assertEqual((self.money(), self.pay()), ((D("0.00"), D("0.00"), D("0.00"), D("0.00"), D("0.00")), []))   # nothing is left to release

    def test_a_refund_in_part_takes_its_part_of_the_holdback(self):
        order = self.sale(days_ago=40)                                            # 30.00: 28.50 and 1.50
        self.approve()
        found = Order.objects.get(pk=order.pk)
        self.sign_in("operator@example.org")
        self.answers += ["<response><sid>s1</sid></response>", "<response><mb_transaction_id>900200</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "20.00"})    # a thirtieth: 1.00 of the commission
        back = Commission.objects.get(state="reversal")
        self.assertEqual((back.amount, back.from_holdback, Holdback.objects.get().left), (D("-1.00"), D("1.00"), D("0.50")))
        self.assertEqual(self.money(), (D("0.00"), D("0.50"), D("28.50"), D("0.00"), D("0.00")))
        self.answers += ["<response><sid>s2</sid></response>", "<response><mb_transaction_id>900201</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{found.pk}/", {"amount": "60.00"})    # 3.00 more: 0.50 from what is held, 2.50 off the balance
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("26.00"), D("0.00"), D("0.00")))

    def test_a_refund_before_the_approval_leaves_less_to_hold(self):
        order = self.sale()
        self.sign_in("operator@example.org")
        self.answers += ["<response><sid>s1</sid></response>", "<response><mb_transaction_id>900200</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{order.pk}/", {"amount": "300.00"})   # half
        self.assertEqual(self.money(), (D("15.00"), D("0.00"), D("0.00"), D("0.00"), D("0.00")))    # pending together, not owed apart
        self.approve()
        self.assertEqual((Holdback.objects.get().amount, self.money()), (D("0.75"), (D("0.00"), D("0.75"), D("14.25"), D("0.00"), D("0.00"))))
        self.answers += ["<response><sid>s2</sid></response>", "<response><mb_transaction_id>900201</mb_transaction_id><status>2</status></response>"]
        self.client.post(f"/operator/orders/{order.pk}/", {"amount": "300.00"})   # the rest
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("0.00"), D("0.00"), D("0.00")))
        self.assertEqual(sum(line.amount for line in Commission.objects.all()), D("0.00"))

    def test_a_balance_under_the_minimum_is_carried_to_the_next_month(self):
        self.sale("org.acme.tool", "200.00", days_ago=200)                        # 9.50 and 0.50
        self.approve()
        today = timezone.localdate()
        self.assertEqual((services.build(today, self.operator), self.money()[2]), ([], D("9.50")))
        self.assertEqual(services.owed()[self.anna.pk], [{"currency": "EUR", "sum": D("9.50"), "payable": False}])
        self.sale("org.acme.tool", "200.00", days_ago=40)
        self.approve()
        self.assertEqual(self.money()[2], D("19.00"))
        self.assertEqual(services.build(today, self.operator), [])               # two months' worth, still under it
        self.assertEqual(services.release_due(), 1)                              # the first sale's 0.50, six months on
        self.sale("org.acme.tool", "200.00", days_ago=35)
        self.approve()
        row = services.sums(self.anna)[0]
        self.assertEqual((row["balance"], row["held"], row["payable"]), (D("29.00"), D("1.00"), True))
        payout, = services.build(today, self.operator)
        self.assertEqual((payout.amount, payout.lines.count(), payout.releases.count()), (D("29.00"), 3, 1))
        self.assertEqual(self.money(), (D("0.00"), D("1.00"), D("0.00"), D("29.00"), D("0.00")))
        said = services.statement(payout)
        self.assertEqual((said["commissions"], said["held"], said["taken_back"], said["released"]), (D("30.00"), D("1.50"), D("0.00"), D("0.50")))
        self.sign_in("anna@example.org")
        page = self.client.get(f"/affiliate/payouts/{payout.pk}/")
        self.assertContains(page, "Held back of them")
        self.assertContains(page, "released")
        self.assertContains(self.client.get("/affiliate/"), f"In payout {payout.pk}")

    def test_a_payout_holds_what_was_due_on_its_day_and_no_more(self):
        self.sale(days_ago=70)
        self.approve()
        Commission.objects.update(approved_on=timezone.localdate() - dt.timedelta(days=40))
        self.sale(days_ago=35)
        self.approve()                                                           # approved today
        month_end = timezone.localdate() - dt.timedelta(days=10)
        payout, = services.build(month_end, self.operator)
        self.assertEqual((payout.amount, payout.until, payout.lines.count()), (D("28.50"), month_end, 1))
        self.assertEqual(self.money(), (D("0.00"), D("3.00"), D("28.50"), D("28.50"), D("0.00")))

    def test_the_superaffiliates_share_is_held_like_its_commission(self):
        rita = self.affiliate("rita", recruiter="anna")
        self.sale(affiliate=rita)
        self.approve()
        held = {row.affiliate.handle: row.amount for row in Holdback.objects.all()}
        self.assertEqual(held, {"rita": D("1.50"), "anna": D("0.15")})
        self.assertEqual((self.money(rita), self.money()), ((D("0.00"), D("1.50"), D("28.50"), D("0.00"), D("0.00")),
                                                           (D("0.00"), D("0.15"), D("2.85"), D("0.00"), D("0.00"))))

    def test_an_affiliate_who_leaves_is_paid_whatever_is_owed(self):
        self.sale("org.acme.tool", "200.00", days_ago=40)
        self.approve()
        today = timezone.localdate()
        self.assertEqual(services.build(today, self.operator), [])
        closure.request_deletion(self.anna.user)                                  # on the way out: the minimum does not hold
        self.anna.refresh_from_db()
        self.assertTrue(services.sums(self.anna)[0]["payable"])
        payout, = services.build(today, self.operator)
        self.assertEqual((payout.amount, payout.regardless), (D("9.50"), True))
        # The holdback is paid as it is released, the same way.
        Holdback.objects.update(release_on=today)
        services.release_due()
        late, = services.build(today, self.operator)
        self.assertEqual((late.amount, late.regardless, late.releases.count()), (D("0.50"), True, 1))

    def test_a_blocked_affiliates_money_waits(self):
        self.sale(days_ago=200)
        self.approve()
        payout, = services.build(timezone.localdate(), self.operator)
        self.sale(days_ago=200)
        services.sanction(self.anna, self.operator, Sanction.BLOCK, rule="Terms 4", facts="Under review.")
        with self.assertRaises(services.AffiliateError):
            services.mark_paid(payout, self.operator, "SEPA 5")                   # made before the block, not paid in it
        self.assertEqual((self.approve(), services.release_due(), services.build(timezone.localdate(), self.operator)), (0, 0, []))
        self.assertNotIn(self.anna.pk, services.owed())
        services.lift(self.anna, self.operator)
        self.assertEqual((self.approve(), services.release_due()), (1, 2))
        services.mark_paid(payout, self.operator, "SEPA 5")
        self.assertEqual(self.money(), (D("0.00"), D("0.00"), D("31.50"), D("0.00"), D("28.50")))

    def test_the_operators_payout_page_makes_the_months_payouts(self):
        self.sale(days_ago=40)
        self.approve()
        self.sign_in("operator@example.org")
        page = self.client.get("/operator/affiliates/payouts/")
        self.assertContains(page, "A balance under 25.00 is carried")
        self.client.post("/operator/affiliates/payouts/", {"until": "not a day"})
        self.assertEqual(AffiliatePayout.objects.count(), 0)
        self.client.post("/operator/affiliates/payouts/", {"until": timezone.localdate().isoformat()})
        payout = AffiliatePayout.objects.get()
        self.assertEqual((payout.amount, payout.state), (D("28.50"), "open"))
        page = self.client.get(f"/operator/affiliates/{self.anna.pk}/")
        self.assertContains(page, "In a payout to pay")
        self.assertContains(page, f"In payout {payout.pk}")
