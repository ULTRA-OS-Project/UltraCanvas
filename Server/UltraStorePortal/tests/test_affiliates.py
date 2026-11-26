"""UltraAffiliates, phase 1: becoming an affiliate, a link to a product,
what a sale earns from the store's fee and from the publisher's boost, the
superaffiliate's share, what a refund takes back, a block and a ban."""

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
from ultrastore_portal.affiliates.models import AffiliatePayout, AffiliateProfile, Attribution, Boost, Click, ClickDay, Commission, Sanction
from ultrastore_portal.billing import payouts
from ultrastore_portal.billing.models import Order, Payment
from ultrastore_portal.store.models import AuditEntry, PublisherMember

from .test_billing import SKRILL, BillingCase
from .test_portal import PASSWORD

D = Decimal
BORN = dt.date(1990, 5, 17)


@override_settings(AFFILIATES=True)
class AffiliateCase(BillingCase):
    def setUp(self):
        super().setUp()
        texts.outbox.clear()
        self.anna = self.affiliate("anna")

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
        self.assertEqual(services.sums(self.anna), [{"currency": "EUR", "pending": D("1.00"), "balance": D("0.00"), "paid": D("0.00")}])
        with self.assertRaises(services.AffiliateError):
            services.pay(self.anna, "EUR", self.operator, "ref")                                   # pending is not owed yet
        today = timezone.localdate()
        self.assertEqual(services.approve_due(today + dt.timedelta(days=29)), 0)
        self.assertEqual(services.approve_due(today + dt.timedelta(days=30)), 1)
        self.assertEqual(services.sums(self.anna)[0]["balance"], D("1.00"))
        with self.assertRaises(services.AffiliateError):
            services.pay(self.anna, "EUR", self.operator, "  ")                                    # no reference, no payout
        payout = services.pay(self.anna, "EUR", self.operator, "SEPA 17", "November")
        self.assertEqual((payout.amount, payout.details, payout.reference), (D("1.00"), "IBAN of anna", "SEPA 17"))
        self.assertEqual(services.sums(self.anna), [{"currency": "EUR", "pending": D("0.00"), "balance": D("0.00"), "paid": D("1.00")}])
        with self.assertRaises(services.AffiliateError):
            services.pay(self.anna, "EUR", self.operator, "again")                                 # nothing is owed twice
        self.assertEqual(AuditEntry.objects.filter(action="affiliate.paid").count(), 1)

    def test_the_command(self):
        self.follow()
        self.buy()
        Commission.objects.update(approve_after=timezone.localdate())
        Click.objects.update(at=timezone.now() - dt.timedelta(days=120))
        out = io.StringIO()
        call_command("affiliates", "maintain", stdout=out)
        self.assertIn("1 commission(s) approved, 0 block(s) ended, 1 click(s) counted and removed", out.getvalue())
        self.assertEqual((Click.objects.count(), ClickDay.objects.get().clicks), (0, 1))
        out = io.StringIO()
        call_command("affiliates", "owed", stdout=out)
        self.assertIn("anna\t1.00 EUR", out.getvalue())
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
        services.approve_due(timezone.localdate() + dt.timedelta(days=30))
        services.pay(self.anna, "EUR", self.operator, "SEPA 1")
        self.report(order["order_id"], status="-3")
        self.assertEqual(services.sums(self.anna), [{"currency": "EUR", "pending": D("0.00"), "balance": D("-1.00"), "paid": D("1.00")}])
        with self.assertRaises(services.AffiliateError):
            services.pay(self.anna, "EUR", self.operator, "SEPA 2")   # nothing is owed; the next sale makes it up


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
        with self.assertRaises(services.AffiliateError):
            services.pay(self.anna, "EUR", self.operator, "ref")
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
        services.approve_due(timezone.localdate() + dt.timedelta(days=30))
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/affiliates/?state=active"), "1.00 EUR")
        self.client.post(f"/operator/affiliates/{self.anna.pk}/", {"action": "pay", "currency": "EUR", "reference": "SEPA 99"})
        self.assertEqual(AffiliatePayout.objects.get().reference, "SEPA 99")
        self.sign_in("anna@example.org")
        self.assertContains(self.client.get("/affiliate/"), "SEPA 99")


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
