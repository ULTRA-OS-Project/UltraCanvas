"""UltraAds, phase 1, end to end through the portal's pages and its JSON
service: a publisher promotes an app, its card appears marked where the
page calls for it, a click is charged once, an order is counted back to its
click, and the operator sets the prices and stops what must stop."""

from __future__ import annotations

import datetime as dt
import io
from decimal import Decimal
from pathlib import Path

from django.core import mail
from django.core.management import call_command
from django.test import Client as Browser
from django.test import override_settings
from django.utils import timezone

from ultrastore_portal.billing.models import Order
from ultrastore_portal.promote import decide, services
from ultrastore_portal.promote.models import Attribution, Balance, Campaign, Click, DayCount, LedgerEntry, Placement, TopUp
from ultrastore_portal.store.models import AuditEntry, Report, Setting

from .test_portal import PASSWORD, PortalCase, User

def noon() -> dt.datetime:
    """Today at twelve - half the day gone: the pacing allows 60 % of a
    day's budget. Today's, whenever the tests run: a campaign starts on the
    day it is made, and a noon of another day would be before its start."""
    return timezone.make_aware(dt.datetime.combine(timezone.localdate(), dt.time(12, 0)))


class PromoteCase(PortalCase):
    def setUp(self):
        super().setUp()
        override = override_settings(PROMOTE=True, PROMOTE_TOPUPS=True, PROMOTE_CURRENCY="EUR", PAYMENT_PROVIDER="test", RESTRICTED_SECRET="link-secret")
        override.enable()
        self.addCleanup(override.disable)
        Setting.objects.update_or_create(key="promote.pacing", defaults={"value": "0"})   # the tests of pacing switch it on
        self.acme = self.make_publisher()
        self.editor = self.make_app(self.acme, "org.acme.editor", name="Photo Editor", category="graphics", tags="photo, picture, raw")
        self.make_release(self.editor)
        self.other = self.make_publisher("other@example.org", "zeta")
        self.rival = self.make_app(self.other, "org.zeta.darkroom", name="Darkroom", category="graphics", tags="photo, develop")
        self.make_release(self.rival)
        self.notes = self.make_app(self.other, "org.zeta.notes", name="Notes", category="office")
        self.make_release(self.notes)

    # -- helpers ---------------------------------------------------------
    def fund(self, publisher, amount="20.00"):
        return services.credit(publisher, LedgerEntry.VOUCHER, Decimal(amount), "test", self.operator)

    def campaign(self, app, places=("store-search", "store-category", "store-home", "store-detail"), daily="5.00", max_click=None, **fields) -> Campaign:
        made = Campaign(publisher=app.publisher, app=app, name=app.name, daily_budget=Decimal(daily), max_click=Decimal(max_click) if max_click else None, **fields)
        return services.save_campaign(made, list(Placement.objects.filter(key__in=places)), None)

    def ads(self, placement="store-search", **context):
        return decide.decide(placement, **context)

    def balance(self, publisher) -> Decimal:
        return Balance.objects.get(publisher=publisher).amount


class DecisionTests(PromoteCase):
    def test_an_advertisement_is_chosen_from_the_page(self):
        self.fund(self.acme)
        self.campaign(self.editor)
        # A search it answers by the store's own rule: every word found in it.
        self.assertEqual([ad.app for ad in self.ads(q="photo")], [self.editor])
        self.assertEqual([ad.app for ad in self.ads(q="photo editor")], [self.editor])
        self.assertEqual(self.ads(q="spreadsheet"), [])          # not at any price
        self.assertEqual(self.ads(q="photo spreadsheet"), [])
        self.assertEqual(self.ads(q=""), [])                     # a search placement without a search
        # Its category, and beside an app of its category - never beside itself.
        self.assertEqual(len(self.ads("store-category", category="graphics")), 1)
        self.assertEqual(self.ads("store-category", category="office"), [])
        self.assertEqual(self.ads("store-category", category="nonsense"), [])
        self.assertEqual(len(self.ads("store-detail", listing="org.zeta.darkroom")), 1)
        self.assertEqual(self.ads("store-detail", listing="org.acme.editor"), [])
        self.assertEqual(self.ads("store-detail", listing="org.zeta.notes"), [])
        # The front page's row takes it as it is; a system it has no build for does not.
        self.assertEqual(len(self.ads("store-home")), 1)
        self.assertEqual(len(self.ads("store-home", os="linux")), 1)
        self.assertEqual(self.ads("store-home", os="macos"), [])
        # A page with no results carries no advertisement; a short one carries one.
        self.assertEqual(self.ads(q="photo", results=0), [])
        self.assertEqual(len(self.ads(q="photo", results=3)), 1)
        # What the device says has had its share is left out.
        self.assertEqual(self.ads(q="photo", seen=(Campaign.objects.get().pk,)), [])

    def test_the_function_that_chooses_is_never_told_who_asks(self):
        import inspect
        names = set(inspect.signature(decide.decide).parameters)
        self.assertFalse(names & {"request", "user", "session", "account"})

    def test_who_may_be_shown(self):
        campaign = self.campaign(self.editor)
        self.assertEqual(self.ads(q="photo"), [])                # no balance, no showing
        self.fund(self.acme)
        self.assertEqual(len(self.ads(q="photo")), 1)
        for change, back in ((dict(state=Campaign.PAUSED), dict(state=Campaign.ACTIVE)), (dict(stopped=True), dict(stopped=False)),
                             (dict(starts=timezone.localdate() + dt.timedelta(days=2)), dict(starts=timezone.localdate())),
                             (dict(ends=timezone.localdate() - dt.timedelta(days=1)), dict(ends=None)),
                             (dict(total_budget=Decimal("3.00"), spent=Decimal("3.00")), dict(total_budget=None))):
            Campaign.objects.filter(pk=campaign.pk).update(**change)
            self.assertEqual(self.ads(q="photo"), [], change)
            Campaign.objects.filter(pk=campaign.pk).update(**back)
        self.assertEqual(len(self.ads(q="photo")), 1)
        # The listing's own standing is the campaign's: suspended, it is not advertised.
        self.editor.suspended = True
        self.editor.save()
        self.assertEqual(self.ads(q="photo"), [])
        self.assertEqual(Campaign.objects.get().standing()[0], "waiting")
        self.editor.suspended = False
        self.editor.save()
        # A placement the operator switched off holds nothing; advertising switched off, nothing anywhere.
        Placement.objects.filter(key="store-search").update(state=Placement.CLOSED)
        self.assertEqual(self.ads(q="photo"), [])
        Placement.objects.filter(key="store-search").update(state=Placement.OPEN)
        with self.settings(PROMOTE=False):
            self.assertEqual(self.ads(q="photo"), [])

    def test_the_price_is_the_floor_alone_and_an_auction_together(self):
        floor = Placement.objects.get(key="store-search").floor
        self.fund(self.acme)
        self.fund(self.other)
        mine = self.campaign(self.editor, max_click="0.50")
        alone = self.ads(q="photo")
        self.assertEqual([(ad.app, ad.price) for ad in alone], [(self.editor, floor)])   # one advertiser: the price list
        theirs = self.campaign(self.rival, max_click="0.30")
        both = self.ads(q="photo")
        self.assertEqual([ad.app for ad in both], [self.editor, self.rival])
        # The winner pays what it took to stay ahead of the next one - a cent over its offer - not what it offered.
        self.assertEqual(both[0].price, Decimal("0.31"))
        self.assertEqual(both[1].price, floor)
        # Never more than it offered, never less than the floor.
        Campaign.objects.filter(pk=theirs.pk).update(max_click=Decimal("0.50"))
        self.assertTrue(all(floor <= ad.price <= Decimal("0.50") for ad in self.ads(q="photo")))
        # A listing that is clicked more often wins against a somewhat higher offer.
        Campaign.objects.filter(pk=theirs.pk).update(max_click=Decimal("0.30"))
        place = Placement.objects.get(key="store-search")
        DayCount.objects.create(campaign=theirs, placement=place, day=timezone.localdate(), shown=1000, clicks=100)
        DayCount.objects.create(campaign=mine, placement=place, day=timezone.localdate(), shown=1000, clicks=10)
        self.assertEqual([ad.app for ad in self.ads(q="photo")], [self.rival, self.editor])
        # One advertisement per listing, and no more than the placement holds.
        self.campaign(self.editor, max_click="0.40")
        self.assertEqual(len(self.ads(q="photo")), 2)
        self.assertEqual(len(self.ads(q="photo", count=1)), 1)

    def test_a_publishers_own_campaigns_do_not_bid_against_each_other(self):
        self.fund(self.acme)
        second = self.make_app(self.acme, "org.acme.viewer", name="Photo Viewer", category="graphics", tags="photo")
        self.make_release(second)
        self.campaign(self.editor, max_click="0.50")
        self.campaign(second, max_click="0.40")
        floor = Placement.objects.get(key="store-search").floor
        self.assertEqual([ad.price for ad in self.ads(q="photo")], [floor, floor])   # both shown, neither raising the other

    def test_automatic_bidding_and_a_budget_spread_over_the_day(self):
        self.fund(self.acme)
        campaign = self.campaign(self.editor, daily="2.00")
        self.assertEqual(self.ads(q="photo")[0].price, Decimal("0.10"))
        place = Placement.objects.get(key="store-search")
        NOON = noon()
        today = timezone.localdate(NOON)
        Setting.objects.filter(key="promote.pacing").update(value="1")
        DayCount.objects.create(campaign=campaign, placement=place, day=today, spent=Decimal("1.00"))
        self.assertEqual(len(decide.decide("store-search", q="photo", now=NOON)), 1)       # 1.00 of the 1.20 noon allows
        DayCount.objects.filter(campaign=campaign).update(spent=Decimal("1.15"))
        self.assertEqual(decide.decide("store-search", q="photo", now=NOON), [])           # the next click would pass it
        evening = NOON + dt.timedelta(hours=10)
        self.assertEqual(len(decide.decide("store-search", q="photo", now=evening)), 1)    # later in the day there is room again
        DayCount.objects.filter(campaign=campaign).update(spent=Decimal("1.95"))
        self.assertEqual(decide.decide("store-search", q="photo", now=evening), [])        # the day's budget is the day's budget


class CountingTests(PromoteCase):
    def setUp(self):
        super().setUp()
        self.fund(self.acme, "1.00")
        self.made = self.campaign(self.editor)

    def test_a_click_is_charged_once(self):
        ad = self.ads(q="photo")[0]
        self.assertTrue(services.shown(ad.token))
        self.assertFalse(services.shown(ad.token))                # the same showing, told twice
        click = services.clicked(ad.token, address="10.0.0.1")
        self.assertTrue(click.valid)
        self.assertEqual(self.balance(self.acme), Decimal("0.90"))
        again = services.clicked(ad.token, address="10.0.0.1")
        self.assertEqual((again.pk, Click.objects.count(), self.balance(self.acme)), (click.pk, 1, Decimal("0.90")))
        count = DayCount.objects.get()
        self.assertEqual((count.shown, count.clicks, count.spent, count.invalid), (1, 1, Decimal("0.10"), 0))
        self.assertEqual(Campaign.objects.get().spent, Decimal("0.10"))
        # Nothing about who clicked is kept.
        self.assertEqual({field.name for field in Click._meta.fields}, {"id", "jti", "campaign", "placement", "country", "price", "valid", "reason", "installed", "at"})
        # A token that is not ours, or one tampered with, is nothing.
        self.assertIsNone(services.clicked("nonsense"))
        self.assertIsNone(services.clicked(ad.token[:-3] + "abc"))

    def test_what_does_not_count_is_not_charged(self):
        # The publisher's own click.
        own = services.clicked(self.ads(q="photo")[0].token, own=True)
        self.assertEqual((own.valid, own.reason, self.balance(self.acme)), (False, "own", Decimal("1.00")))
        # A burst from one address: the sixth in ten minutes and after.
        for _ in range(5):
            self.assertTrue(services.clicked(self.ads(q="photo")[0].token, address="10.0.0.9").valid)
        burst = services.clicked(self.ads(q="photo")[0].token, address="10.0.0.9")
        self.assertEqual((burst.valid, burst.reason, self.balance(self.acme)), (False, "burst", Decimal("0.50")))
        self.assertTrue(services.clicked(self.ads(q="photo")[0].token, address="10.0.0.10").valid)   # another address is another story
        # A click on a campaign that was paused since the page was drawn.
        token = self.ads(q="photo")[0].token
        services.set_state(self.made, Campaign.PAUSED, None)
        late = services.clicked(token, address="10.0.0.11")
        self.assertEqual((late.valid, late.reason), (False, "ended"))
        services.set_state(self.made, Campaign.ACTIVE, None)
        # A balance that does not cover the click any more.
        token = self.ads(q="photo")[0].token
        Balance.objects.update(amount=Decimal("0.05"))
        short = services.clicked(token, address="10.0.0.12")
        self.assertEqual((short.valid, short.reason, self.balance(self.acme)), (False, "funds", Decimal("0.05")))
        self.assertEqual(self.ads(q="photo"), [])                 # and it waits
        self.assertEqual(DayCount.objects.get().invalid, 4)

    def test_the_operator_credits_a_click_back(self):
        click = services.clicked(self.ads(q="photo")[0].token, address="10.0.0.1")
        services.invalidate(click, self.operator)
        self.assertEqual(self.balance(self.acme), Decimal("1.00"))
        count = DayCount.objects.get()
        self.assertEqual((count.clicks, count.invalid, count.spent, Campaign.objects.get().spent), (0, 1, Decimal("0.00"), Decimal("0.00")))
        self.assertEqual(LedgerEntry.objects.filter(kind=LedgerEntry.CREDIT).get().amount, Decimal("0.10"))
        with self.assertRaises(services.Refused):
            services.invalidate(Click.objects.get(), self.operator)   # once

    def test_a_low_balance_is_told_once(self):
        Balance.objects.update(amount=Decimal("5.05"))
        mail.outbox.clear()
        for address in ("10.1.0.1", "10.1.0.2", "10.1.0.3"):
            services.clicked(self.ads(q="photo")[0].token, address=address)
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn("running low", mail.outbox[0].subject)
        self.fund(self.acme, "10.00")
        self.assertFalse(Balance.objects.get(publisher=self.acme).low_notified)   # the next time it is low, it is told again

    def test_an_order_is_counted_back_to_its_click(self):
        paid = self.make_app(self.acme, "org.acme.paid", name="Photo Pro", category="graphics", tags="photo", pricing_model="one_time", price=Decimal("19.90"))
        self.make_release(paid)
        self.campaign(paid, places=("store-search",))
        token = [ad for ad in self.ads(q="photo") if ad.app == paid][0].token
        services.clicked(token, address="10.0.0.1")
        order = self.client.post("/v1/checkout", {"app": "org.acme.paid", "promote": token}, content_type="application/json").json()
        row = Attribution.objects.get()
        self.assertEqual((row.order.public_id, row.counted), (order["order_id"], False))
        self.assertEqual(DayCount.objects.filter(orders__gt=0).count(), 0)    # an order that is not paid is no order
        from ultrastore_portal.billing import services as billing
        found = Order.objects.get(public_id=order["order_id"])
        billing.report_status(billing.test_report(found, "2"))
        billing.report_status(billing.test_report(found, "2"))                # the provider repeats itself
        count = DayCount.objects.get(campaign__app=paid)
        self.assertEqual((count.orders, count.revenue), (1, Decimal("19.90")))
        total = services.report(Campaign.objects.filter(app=paid))["total"]
        self.assertEqual((total["orders"], total["per_order"], total["per_click"]), (1, Decimal("0.10"), Decimal("0.10")))
        # A click on another listing's advertisement counts for nothing here, and a made-up token for nothing at all.
        other = services.clicked(self.ads(q="editor")[0].token, address="10.0.0.2")
        self.assertEqual(other.campaign.app, self.editor)
        self.client.post("/v1/checkout", {"app": "org.acme.paid", "promote": self.ads(q="editor")[0].token}, content_type="application/json")
        self.client.post("/v1/checkout", {"app": "org.acme.paid", "promote": "nonsense"}, content_type="application/json")
        self.assertEqual(Attribution.objects.count(), 1)

    def test_a_report_is_about_an_audience(self):
        place = Placement.objects.get(key="store-search")
        today = timezone.localdate()
        for country, shown, clicks in (("DE", 400, 12), ("FR", 90, 3), ("LU", 4, 1), ("MT", 3, 0)):
            DayCount.objects.create(campaign=self.made, placement=place, day=today, country=country, shown=shown, clicks=clicks, spent=Decimal(clicks) / 10)
        report = services.report([self.made])
        self.assertEqual([row["label"] for row in report["by_country"]], ["DE", "FR", "other"])   # the two that rest on a handful, folded
        self.assertEqual(report["by_country"][2]["shown"], 7)
        self.assertEqual((report["total"]["shown"], report["total"]["clicks"], report["total"]["rate"]), (497, 16, Decimal("3.2")))
        DayCount.objects.create(campaign=self.made, placement=place, day=today - dt.timedelta(days=45), shown=999)
        self.assertEqual(services.report([self.made])["total"]["shown"], 497)   # the last thirty days

    def test_click_rows_go_after_a_month(self):
        click = services.clicked(self.ads(q="photo")[0].token, address="10.0.0.1")
        Click.objects.filter(pk=click.pk).update(at=timezone.now() - dt.timedelta(days=31))
        out = io.StringIO()
        call_command("promote", "cleanup", stdout=out)
        self.assertEqual((Click.objects.count(), DayCount.objects.get().clicks), (0, 1))   # the row goes, the total stays


class PagesTests(PromoteCase):
    def setUp(self):
        super().setUp()
        # The test store is two shelves wide: a list of two is not "short" here.
        Setting.objects.update_or_create(key="promote.results_per_ad", defaults={"value": "2"})

    def test_a_short_list_does_not_repeat_what_it_shows(self):
        self.fund(self.acme)
        self.campaign(self.editor)
        Setting.objects.filter(key="promote.results_per_ad").update(value="5")
        visitor = Browser()
        self.assertEqual(visitor.get("/apps/?q=photo").context["ads"], [])          # two results, both in view: no card twice
        self.assertEqual(visitor.get("/category/graphics/").context["ads"], [])
        self.assertEqual(DayCount.objects.count(), 0)                                # and nothing counted as shown
        self.assertEqual(len(visitor.get("/apps/org.zeta.darkroom/").context["ads"]), 1)   # beside another app it is news

    def test_promote_lists_every_place_with_this_shop_ticked(self):
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/console/apps/org.acme.editor/"), "/console/apps/org.acme.editor/promote/")
        page = self.client.get("/console/apps/org.acme.editor/promote/")
        groups = page.context["groups"]
        self.assertEqual([group["name"] for group in groups],
                         ["App store", "Games shop", "Marketplace", "B2B trade", "Video", "Music and podcasts", "Books and audiobooks"])
        store = groups[0]
        self.assertTrue(store["home"] and all(row["ticked"] for row in store["rows"]))             # this shop: ticked
        self.assertEqual([row["placement"].key for row in store["rows"]], ["store-search", "store-category", "store-home", "store-detail"])
        elsewhere = [row for group in groups[1:] for row in group["rows"]]
        self.assertTrue(elsewhere and not any(row["ticked"] for row in elsewhere))                 # every other one: listed, not ticked
        self.assertTrue(all(row["reason"] == "This shop is not open yet." for row in elsewhere))  # and said why it cannot be yet
        self.assertContains(page, "In search results")
        self.assertContains(page, "Marketplace")
        self.assertContains(page, "from 0.10 EUR a click")
        self.assertContains(page, "Your advertising balance is empty")

        # Started with what is ticked, and nothing else.
        posted = self.client.post("/console/apps/org.acme.editor/promote/", {"places": ["store-search", "store-detail"], "daily_budget": "5.00"})
        campaign = Campaign.objects.get()
        self.assertRedirects(posted, f"/promote/campaigns/{campaign.pk}/", fetch_redirect_response=False)
        self.assertEqual(set(campaign.placements.values_list("key", flat=True)), {"store-search", "store-detail"})
        self.assertEqual((campaign.publisher, campaign.app, campaign.max_click), (self.acme, self.editor, None))
        self.assertTrue(AuditEntry.objects.filter(action="promote.campaign.add").exists())
        self.assertContains(self.client.get(f"/promote/campaigns/{campaign.pk}/"), "The balance is empty")
        # Changing it shows what it has, not the default.
        edit = self.client.get(f"/promote/campaigns/{campaign.pk}/edit/")
        ticked = {row["placement"].key for group in edit.context["groups"] for row in group["rows"] if row["ticked"]}
        self.assertEqual(ticked, {"store-search", "store-detail"})

    def test_what_a_campaign_may_not_be(self):
        self.sign_in("dev@example.org")
        url = "/console/apps/org.acme.editor/promote/"
        self.assertContains(self.client.post(url, {"daily_budget": "5.00"}), "Tick at least one place")
        self.assertContains(self.client.post(url, {"places": ["market-search"], "daily_budget": "5.00"}), "is not open for advertisements")
        self.assertContains(self.client.post(url, {"places": ["store-search"], "daily_budget": "5.00", "max_click": "0.05"}), "is under the least a click costs")
        self.assertContains(self.client.post(url, {"places": ["store-search"], "daily_budget": "500.00"}), "spends at most 25.00 EUR a day")
        self.assertContains(self.client.post(url, {"places": ["store-search"], "daily_budget": "5.00", "ends": "2020-01-01"}), "A day that is still to come")
        self.assertEqual(Campaign.objects.count(), 0)
        # Another publisher's listing is not one's own to promote, and their campaign not one's own to see.
        self.assertEqual(self.client.get("/console/apps/org.zeta.darkroom/promote/").status_code, 404)
        theirs = self.campaign(self.rival)
        self.assertEqual(self.client.get(f"/promote/campaigns/{theirs.pk}/").status_code, 404)
        self.assertEqual(self.client.post(f"/promote/campaigns/{theirs.pk}/pause/").status_code, 404)
        # Switched off, none of it exists.
        with self.settings(PROMOTE=False):
            self.assertEqual(self.client.get(url).status_code, 404)
            self.assertEqual(self.client.get("/promote/").status_code, 404)
            self.assertNotContains(self.client.get("/console/apps/org.acme.editor/"), "Promote")

    def test_the_store_pages_carry_marked_advertisements(self):
        self.fund(self.acme)
        self.campaign(self.editor)
        visitor = Browser()
        # A search: the advertisement first, marked, then the results as the store gives them to everyone.
        page = visitor.get("/apps/?q=photo")
        self.assertEqual([ad.app for ad in page.context["ads"]], [self.editor])
        self.assertContains(page, '<span class="mark">Sponsored</span>', html=True)
        self.assertContains(page, "Why this ad?")
        self.assertEqual({app.app_id for app in page.context["page"]}, {"org.acme.editor", "org.zeta.darkroom"})   # the results, untouched
        self.assertNotContains(visitor.get("/apps/?q=notes"), "Sponsored")
        self.assertNotContains(visitor.get("/apps/"), "Sponsored")                 # all apps, no search and no category: none
        self.assertContains(visitor.get("/category/graphics/"), "Sponsored")
        self.assertNotContains(visitor.get("/category/office/"), "Sponsored")
        self.assertContains(visitor.get("/apps/org.zeta.darkroom/"), "Sponsored - similar apps")
        self.assertNotContains(visitor.get("/apps/org.acme.editor/"), "Sponsored")
        home = visitor.get("/")
        self.assertContains(home, "<h2>Sponsored</h2>", html=True)
        self.assertLess(home.content.index(b"Sponsored</h2>"), home.content.index(b"New and updated"))
        shown = sum(DayCount.objects.values_list("shown", flat=True))
        self.assertEqual(shown, 4)                                                   # each page that drew one

        # The click: counted, and on to the listing.
        ad = page.context["ads"][0]
        gone = visitor.get(f"/promote/go/{ad.token}/")
        self.assertRedirects(gone, "/apps/org.acme.editor/", fetch_redirect_response=False)
        self.assertEqual((Click.objects.count(), self.balance(self.acme)), (1, Decimal("19.90")))
        visitor.get(f"/promote/go/{ad.token}/")                                      # the back button, and again
        self.assertEqual((Click.objects.count(), self.balance(self.acme)), (1, Decimal("19.90")))
        self.assertRedirects(visitor.get("/promote/go/nonsense/"), "/", fetch_redirect_response=False)
        # The download that follows counts for the campaign, once.
        package = self.editor.latest_release().packages.first()
        visitor.get(f"/apps/org.acme.editor/download/{package.pk}/")
        visitor.get(f"/apps/org.acme.editor/download/{package.pk}/")
        self.assertEqual(sum(DayCount.objects.values_list("installs", flat=True)), 1)
        # The publisher's own click is not charged.
        self.sign_in("dev@example.org")
        mine = self.client.get("/apps/?q=photo").context["ads"][0]
        self.client.get(f"/promote/go/{mine.token}/")
        self.assertEqual((Click.objects.filter(valid=False, reason="own").count(), self.balance(self.acme)), (1, Decimal("19.90")))

    def test_why_and_reporting_an_advertisement(self):
        self.fund(self.acme)
        self.campaign(self.editor)
        visitor = Browser()
        ad = visitor.get("/apps/?q=photo").context["ads"][0]
        why = visitor.get(f"/promote/why/{ad.token}/")
        self.assertContains(why, "Acme")                                             # who paid
        self.assertContains(why, "a search for &quot;photo&quot;")                   # what it matched
        self.assertContains(why, "Nothing about you was used")
        self.assertEqual(visitor.get("/promote/why/nonsense/").status_code, 404)
        visitor.post(f"/promote/report/{ad.token}/", {"kind": "other", "text": "This is not a photo editor."})
        report = Report.objects.get()
        self.assertEqual((report.target_kind, report.reporter), ("ad", ""))
        self.assertIn("org.acme.editor in store-search", report.target)

    def test_pausing_ending_and_the_report_page(self):
        self.fund(self.acme)
        campaign = self.campaign(self.editor)
        services.clicked(self.ads(q="photo")[0].token, address="10.0.0.1")
        self.sign_in("dev@example.org")
        page = self.client.get(f"/promote/campaigns/{campaign.pk}/")
        self.assertContains(page, "Running.")
        self.assertEqual(page.context["report"]["total"]["clicks"], 1)
        self.assertContains(self.client.get("/promote/"), "Photo Editor")
        self.client.post(f"/promote/campaigns/{campaign.pk}/pause/")
        self.assertEqual((Campaign.objects.get().state, self.ads(q="photo")), (Campaign.PAUSED, []))
        self.client.post(f"/promote/campaigns/{campaign.pk}/resume/")
        self.assertEqual(len(self.ads(q="photo")), 1)
        self.client.post(f"/promote/campaigns/{campaign.pk}/end/")
        self.client.post(f"/promote/campaigns/{campaign.pk}/resume/")                 # ended stays ended
        self.assertEqual(Campaign.objects.get().state, Campaign.ENDED)
        self.assertRedirects(self.client.get(f"/promote/campaigns/{campaign.pk}/edit/"), f"/promote/campaigns/{campaign.pk}/", fetch_redirect_response=False)


class BalanceTests(PromoteCase):
    def test_paying_in_through_the_payment_provider(self):
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/promote/balance/"), "Continue to pay")
        self.assertContains(self.client.post("/promote/balance/", {"amount": "2.00"}), "The least to pay in is 10.00 EUR")
        gone = self.client.post("/promote/balance/", {"amount": "25.00"})
        topup = TopUp.objects.get()
        self.assertRedirects(gone, f"/promote/topup/{topup.public_id}/", fetch_redirect_response=False)
        self.assertContains(self.client.get(gone["Location"]), "The test payment provider")
        # Coming back proves nothing: only the provider's signed report pays in.
        self.client.get(f"/promote/topup/{topup.public_id}/return/")
        self.assertEqual(self.balance(self.acme), Decimal("0.00"))
        forged = {**services.test_topup_report(topup, "2"), "md5sig": "0" * 32}
        from ultrastore_portal.billing import services as billing
        with self.assertRaises(billing.BillingError):
            services.topup_report(forged)
        less = services.test_topup_report(topup, "2")
        less.update(amount="1.00", mb_amount="1.00")
        with self.assertRaises(billing.BillingError):
            services.topup_report(less)                                             # signed for another amount than was paid in
        self.assertEqual(self.balance(self.acme), Decimal("0.00"))
        self.client.post(f"/promote/topup/{topup.public_id}/test/", {"outcome": "paid"})
        self.client.post(f"/promote/topup/{topup.public_id}/test/", {"outcome": "paid"})   # the report again
        self.assertEqual(self.balance(self.acme), Decimal("25.00"))
        topup.refresh_from_db()
        self.assertEqual((topup.status, LedgerEntry.objects.get().kind, bool(topup.receipt_no)), (TopUp.PAID, LedgerEntry.TOPUP, True))
        self.assertContains(self.client.get("/promote/balance/"), "Top-up, receipt no.")
        # Somebody else's top-up is not one's own.
        self.sign_in("other@example.org")
        self.assertEqual(self.client.get(f"/promote/topup/{topup.public_id}/").status_code, 404)

    def test_paying_in_is_a_switch_of_its_own(self):
        self.sign_in("dev@example.org")
        with self.settings(PROMOTE_TOPUPS=False):
            page = self.client.get("/promote/balance/")
            self.assertContains(page, "Paying into a balance is not open yet")
            self.assertNotContains(page, "Continue to pay")
            self.client.post("/promote/balance/", {"amount": "25.00"})
            self.assertEqual(TopUp.objects.count(), 0)


class OperatorTests(PromoteCase):
    def post(self, **fields):
        return self.client.post("/operator/promote/", fields)

    def test_the_operator_sets_the_prices_and_stops_what_must_stop(self):
        self.sign_in("dev@example.org")
        self.assertEqual(self.client.get("/operator/promote/").status_code, 403)
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/promote/"), "Placements")
        # A voucher: a first balance.
        self.post(action="voucher", publisher="acme", amount="15.00", note="Welcome")
        self.assertEqual((self.balance(self.acme), LedgerEntry.objects.get().note), (Decimal("15.00"), "Welcome"))
        campaign = self.campaign(self.editor)
        self.assertEqual(self.ads(q="photo")[0].price, Decimal("0.10"))
        # The floor is the operator's.
        place = Placement.objects.get(key="store-search")
        self.post(action="placement", placement=place.pk, floor="0.25", slots="1", state="open")
        self.assertEqual(self.ads(q="photo")[0].price, Decimal("0.25"))
        self.post(action="placement", placement=place.pk, floor="0.25", slots="1", state="closed")
        self.assertEqual(self.ads(q="photo"), [])
        self.post(action="placement", placement=place.pk, floor="0.25", slots="1", state="open")
        # A shop that is not built is not opened from here.
        planned = Placement.objects.get(key="market-search")
        self.post(action="placement", placement=planned.pk, floor="0.20", slots="2", state="open")
        self.assertEqual(Placement.objects.get(pk=planned.pk).state, Placement.PLANNED)
        self.assertContains(self.client.post("/operator/promote/", {"action": "placement", "placement": place.pk, "floor": "free", "slots": "1"}, follow=True), "A floor is a price")
        # Stopping a campaign needs the reason its publisher reads.
        self.assertContains(self.client.post("/operator/promote/", {"action": "stop", "campaign": campaign.pk}, follow=True), "Say why")
        self.post(action="stop", campaign=campaign.pk, reason="The card promises what the app does not do.")
        self.assertEqual(self.ads(q="photo"), [])
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get(f"/promote/campaigns/{campaign.pk}/"), "The card promises what the app does not do.")
        self.sign_in("operator@example.org")
        self.post(action="release", campaign=campaign.pk)
        self.assertEqual(len(self.ads(q="photo")), 1)
        # A click credited back, and a setting changed: each a row of the audit log.
        click = services.clicked(self.ads(q="photo")[0].token, address="10.0.0.1")
        self.post(action="credit", click=click.pk)
        self.assertEqual(self.balance(self.acme), Decimal("15.00"))
        self.post(action="setting", name="results_per_ad", value="8")
        self.assertEqual(services.setting("results_per_ad"), "8")
        self.assertContains(self.client.post("/operator/promote/", {"action": "setting", "name": "results_per_ad", "value": "many"}, follow=True), "is a number")
        actions = set(AuditEntry.objects.filter(action__startswith="promote.").values_list("action", flat=True))
        self.assertTrue({"promote.voucher", "promote.placement", "promote.campaign.stop", "promote.campaign.release", "promote.credit", "promote.setting"} <= actions)


class ApiTests(PromoteCase):
    def decide(self, **body):
        return self.client.post("/v1/promote/decide", body, content_type="application/json")

    def test_a_client_asks_what_goes_in_a_placement(self):
        self.fund(self.acme)
        campaign = self.campaign(self.editor)
        answer = self.decide(placement="store-search", context={"q": "photo", "os": "linux", "country": "de"}, results=12).json()
        ad = answer["ads"][0]
        self.assertEqual((ad["id"], ad["kind"], ad["label"], ad["payer"]), ("org.acme.editor", "app", "Sponsored", {"id": "acme", "name": "Acme"}))
        self.assertIn("photo", ad["why"][0])
        self.assertEqual(self.decide(placement="store-search", context={"q": "photo"}, seen=[campaign.pk]).json(), {"ads": []})
        self.assertEqual(self.decide(placement="nowhere").json(), {"ads": []})
        self.assertEqual(self.decide().status_code, 400)
        # Asked without saying who asks: a session sent along is refused.
        refused = self.client.post("/v1/promote/decide", {"placement": "store-search"}, content_type="application/json", headers={"Authorization": "Bearer abc"})
        self.assertEqual((refused.status_code, refused.json()["error"]), (400, "session_not_wanted"))

        # What the client then reports, in one batch.
        events = {"events": [{"token": ad["token"], "type": "shown"}, {"token": ad["token"], "type": "shown"}, {"token": ad["token"], "type": "clicked"},
                             {"token": ad["token"], "type": "installed"}, {"token": "nonsense", "type": "clicked"}, {"type": "shown"}, "junk"]}
        self.assertEqual(self.client.post("/v1/promote/events", events, content_type="application/json").json(), {"accepted": 3})
        count = DayCount.objects.get()
        self.assertEqual((count.shown, count.clicks, count.installs, count.country, count.spent), (1, 1, 1, "DE", Decimal("0.10")))
        self.assertEqual(self.client.post("/v1/promote/events", {"events": "no"}, content_type="application/json").status_code, 400)
        why = self.client.get(f"/v1/promote/why/{ad['token']}").json()
        self.assertEqual((why["payer"]["name"], why["placement"]["key"]), ("Acme", "store-search"))
        self.assertEqual(self.client.get("/v1/promote/why/nonsense").status_code, 404)
        # Switched off, the answer is an empty list, not an error: a client need not know.
        with self.settings(PROMOTE=False):
            self.assertEqual(self.decide(placement="store-search", context={"q": "photo"}).json(), {"ads": []})


class ConsoleTests(PromoteCase):
    """Phase 2: what the console adds to a campaign."""

    def setUp(self):
        super().setUp()
        from ultrastore_portal.promote.models import Keyword
        self.Keyword = Keyword
        self.fund(self.acme)
        self.fund(self.other)
        self.made = self.campaign(self.editor)

    def word(self, text, match="broad", negative=False, campaign=None):
        return services.add_keyword(campaign or self.made, text, match, negative, None)

    def shown_for(self, q) -> bool:
        return self.editor in [ad.app for ad in self.ads(q=q)]

    def test_a_campaign_chooses_its_searches(self):
        self.assertTrue(self.shown_for("photo"))                       # without words of its own: every search the listing answers
        self.word("Retouch")
        self.assertEqual(self.Keyword.objects.get().text, "retouch")
        self.assertFalse(self.shown_for("photo"))                      # with words of its own: those searches, and no others
        self.assertTrue(self.shown_for("picture retouch"))             # the word matches, and the listing holds half of what was searched
        self.assertFalse(self.shown_for("retouch"))                    # the word matches - the listing holds nothing of it: not at any price
        self.assertFalse(self.shown_for("retouch spreadsheet tables"))
        # The words together and in order; and exactly the search.
        self.word("raw photo", "phrase")
        self.assertTrue(self.shown_for("edit raw photo"))
        self.assertFalse(self.shown_for("photo raw"))
        self.word("photo editor", "exact")
        self.assertTrue(self.shown_for("Photo  Editor"))               # as it is compared: lower case, single spaces
        self.assertFalse(self.shown_for("photo editor free"))
        # What it excludes is never its search, whatever else matches - for a campaign without words of its own too.
        self.word("free", negative=True)
        self.assertFalse(self.shown_for("free raw photo"))
        plain = self.campaign(self.rival)
        self.word("develop", negative=True, campaign=plain)
        self.assertIn(self.rival, [ad.app for ad in self.ads(q="photo")])
        self.assertNotIn(self.rival, [ad.app for ad in self.ads(q="photo develop")])
        # Words are for searches: a category list and the row on the front page do not ask them.
        self.assertEqual(len(self.ads("store-category", category="graphics")), 2)
        with self.assertRaises(services.Refused):
            self.word("   ")
        with self.assertRaises(services.Refused):
            self.word("photo", "roughly")

    def test_an_offer_for_one_place(self):
        Campaign.objects.filter(pk=self.made.pk).update(max_click=Decimal("0.20"))
        self.campaign(self.rival, max_click="0.30")
        self.assertEqual([ad.app for ad in self.ads(q="photo")], [self.rival, self.editor])
        services.set_bids(self.made, {"store-search": Decimal("0.50"), "market-search": Decimal("9.00")}, None)   # a place it is not ticked for is not its to bid on
        first = self.ads(q="photo")
        self.assertEqual([(ad.app, ad.price) for ad in first][0], (self.editor, Decimal("0.31")))   # ahead there, at what it took
        self.assertEqual(self.ads("store-category", category="graphics")[0].app, self.rival)         # elsewhere its own offer still holds
        self.assertEqual(self.made.bids.count(), 1)
        with self.assertRaises(services.Refused):
            services.set_bids(self.made, {"store-search": Decimal("0.02")}, None)                    # under the floor
        services.set_bids(self.made, {"store-search": None}, None)
        self.assertEqual(self.made.bids.count(), 0)

    def test_what_was_searched_is_named_only_when_it_is_many_peoples(self):
        for _ in range(12):
            ad = [ad for ad in self.ads(q="photo") if ad.app == self.editor][0]
            services.shown(ad.token)
        services.clicked(ad.token, address="10.0.0.1")
        for q in ("photo raw", "raw photo picture"):
            services.shown([ad for ad in self.ads(q=q) if ad.app == self.editor][0].token)
        rows = services.terms_report(self.made)
        self.assertEqual([(row["term"], row["shown"], row["clicks"], row["named"]) for row in rows], [("photo", 12, 1, True), ("other searches", 2, 0, False)])
        # As a file: a search that a spreadsheet would run as a formula is made plain text.
        for _ in range(10):
            services._count_term(self.made.pk, {"w": {"q": "=HYPERLINK(evil)"}}, shown=1)
        table = services.report_csv(self.made, "terms")
        self.assertIn(["'=hyperlink(evil)", "10", "0", "0.0"], table)
        self.assertEqual(table[0], ["Search", "Shown", "Clicks", "Click rate %"])
        # And it goes after a month, as the click rows do.
        from ultrastore_portal.promote.models import TermCount
        TermCount.objects.update(day=timezone.localdate() - dt.timedelta(days=40))
        call_command("promote", "cleanup", stdout=io.StringIO())
        self.assertEqual(TermCount.objects.count(), 0)

    def test_the_advertising_policy(self):
        from ultrastore_portal.promote.models import PolicyAcceptance, PolicyVersion
        self.assertTrue(self.shown_for("photo"))                       # no policy in force: nothing to accept
        PolicyVersion.objects.create(version=1, text="Advertise what the listing is.")
        self.assertFalse(self.shown_for("photo"))                      # in force, and not accepted: it waits
        self.assertEqual(services.standing(self.made, Decimal("5.00"))[0], "waiting")
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/promote/"), "The advertising policy has a new version")
        url = f"/promote/campaigns/{self.made.pk}/edit/"
        self.assertContains(self.client.get(url), "I accept the")
        fields = {"places": ["store-search", "store-category"], "daily_budget": "5.00"}
        self.assertContains(self.client.post(url, fields), "Accept the advertising policy to go on.")
        self.client.post(url, {**fields, "accept_policy": "on"})
        self.assertEqual(PolicyAcceptance.objects.get().version, 1)
        self.assertTrue(self.shown_for("photo"))
        self.assertNotContains(self.client.get(url), "I accept the")
        self.assertContains(self.client.get("/promote/policy/"), "Advertise what the listing is.")
        PolicyVersion.objects.create(version=2, text="And nothing political.", changes="Political advertising is refused.")
        self.assertFalse(self.shown_for("photo"))                      # a new version: it waits again
        self.assertContains(self.client.get("/promote/policy/"), "Political advertising is refused.")
        PolicyVersion.objects.create(version=3, text="Later.", in_force_from=timezone.localdate() + dt.timedelta(days=30))
        self.assertEqual(PolicyVersion.current().version, 2)           # one that is not in force yet is not asked for

    def test_who_of_a_publisher_may_do_what(self):
        from ultrastore_portal.store.models import PublisherMember
        people = {}
        for role in ("marketing", "billing", "analyst", "developer"):
            people[role] = User.objects.create_user(f"{role}@example.org", PASSWORD, name=role.title(), email_verified=True)
            PublisherMember.objects.create(publisher=self.acme, user=people[role], role=role)
        promote = "/console/apps/org.acme.editor/promote/"
        fields = {"places": ["store-search"], "daily_budget": "5.00"}
        expected = {  # role: (reads advertising, starts a campaign, pays in, opens the apps)
            "marketing": (200, 302, 403, 403), "billing": (200, 403, 302, 403), "analyst": (200, 403, 403, 403), "developer": (200, 403, 403, 200)}
        for role, (reads, starts, pays, apps) in expected.items():
            self.sign_in(f"{role}@example.org")
            self.assertEqual(self.client.get(f"/promote/campaigns/{self.made.pk}/").status_code, reads, role)
            self.assertEqual(self.client.post(promote, fields).status_code, starts, role)
            self.assertEqual(self.client.post("/promote/balance/", {"amount": "25.00"}).status_code, pays, role)
            self.assertEqual(self.client.get("/console/apps/org.acme.editor/").status_code, apps, role)
            self.assertEqual(self.client.post(f"/promote/campaigns/{self.made.pk}/pause/").status_code, 302 if starts == 302 else 403, role)
            self.assertEqual(self.client.post(f"/promote/campaigns/{self.made.pk}/words/", {"text": "x"}).status_code, 302 if starts == 302 else 403, role)
        # Someone who is not there for the apps lands on advertising, and is not shown the apps' pages.
        self.sign_in("marketing@example.org")
        self.assertRedirects(self.client.get("/console/"), "/promote/", fetch_redirect_response=False)
        self.assertNotContains(self.client.get("/promote/"), "Create an app")
        self.sign_in("analyst@example.org")
        self.assertContains(self.client.get("/promote/"), "is for the publisher's owner and its Marketing members")
        self.assertEqual(self.client.get(f"/promote/campaigns/{self.made.pk}/report.csv").status_code, 200)   # reading includes the file

    def test_the_campaign_page_and_its_file(self):
        self.sign_in("dev@example.org")
        url = f"/promote/campaigns/{self.made.pk}/"
        self.assertContains(self.client.get(url), "It is shown for every search its listing answers")
        self.client.post(url + "words/", {"text": "raw photo", "match": "phrase"})
        self.client.post(url + "words/", {"text": "free", "negative": "1"})
        page = self.client.get(url)
        self.assertEqual(([w.text for w in page.context["words"]], [w.text for w in page.context["excluded"]]), (["raw photo"], ["free"]))
        self.assertContains(page, "never for")
        self.client.post(url + "words/", {"remove": page.context["words"][0].pk})
        self.assertEqual(self.made.keywords.count(), 1)
        self.client.post(url + "bids/", {"bid-store-search": "0.40", "bid-store-home": ""})
        self.assertEqual([(b.placement.key, b.max_click) for b in self.made.bids.all()], [("store-search", Decimal("0.40"))])
        self.assertContains(self.client.post(url + "bids/", {"bid-store-search": "cheap"}, follow=True), "is not an amount")
        self.assertContains(self.client.post(url + "bids/", {"bid-store-search": "0.01"}, follow=True), "is under the least a click costs")
        services.clicked(self.ads("store-home")[0].token, address="10.0.0.1")
        answer = self.client.get(url + "report.csv?what=places")
        self.assertEqual(answer["Content-Type"], "text/csv; charset=utf-8")
        self.assertIn("attachment", answer["Content-Disposition"])
        lines = answer.content.decode("utf-8-sig").splitlines()
        self.assertEqual(lines[0], "Place,Shown,Clicks,Clicks not counted,Spent,Installs,Orders,Order value")
        self.assertIn("The Sponsored row on the front page,0,1,0,0.05,0,0,0.00", lines)
        self.assertEqual(self.client.get(url + "report.csv?what=everything").status_code, 404)
        theirs = self.campaign(self.rival)
        self.assertEqual(self.client.get(f"/promote/campaigns/{theirs.pk}/report.csv").status_code, 404)
        self.assertEqual(self.client.post(f"/promote/campaigns/{theirs.pk}/words/", {"text": "x"}).status_code, 404)

    def test_what_the_numbers_suggest(self):
        self.assertEqual(services.suggestions(self.made, Decimal("20.00")), [])
        place = Placement.objects.get(key="store-search")
        today = timezone.localdate()
        for back in (1, 2, 4):
            DayCount.objects.create(campaign=self.made, placement=place, day=today - dt.timedelta(days=back), spent=Decimal("4.90"), shown=300, clicks=1)
        found = services.suggestions(self.made, Decimal("6.00"))
        self.assertTrue(any("ran out on 3 of the last 7 days" in line for line in found))
        self.assertTrue(any("seen and seldom clicked" in line for line in found))
        self.assertTrue(any("less than three days" in line for line in found))
        DayCount.objects.filter(campaign=self.made).update(clicks=20)
        self.assertTrue(any("click and do not install" in line for line in services.suggestions(self.made, Decimal("60.00"))))

    def test_anyone_can_see_who_advertises_what(self):
        visitor = Browser()
        self.assertContains(visitor.get("/promote/library/"), "No advertisement has been shown yet")
        self.word("raw photo", "phrase")
        self.word("free", negative=True)
        services.shown(self.ads("store-home")[0].token)
        self.campaign(self.rival)                                      # never shown: not in the library
        page = visitor.get("/promote/library/")
        rows = page.context["page"].object_list
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual((row["campaign"], row["publisher"].name, row["running"]), (self.made, "Acme", True))
        self.assertEqual(row["shown_for"], "raw photo")                # what it is shown for is named; what it excludes is its own business
        self.assertEqual([place.key for place in row["places"]], ["store-search", "store-category", "store-home", "store-detail"])
        self.assertContains(page, "runs now")
        self.assertContains(page, "Acme")
        self.assertEqual(len(visitor.get("/promote/library/?q=nobody").context["page"].object_list), 0)
        self.assertEqual(len(visitor.get("/promote/library/?q=acme").context["page"].object_list), 1)
        listed = self.client.get("/v1/promote/library").json()["ads"]
        self.assertEqual([(ad["id"], ad["payer"]["id"], ad["running"], ad["shown_for"]) for ad in listed], [("org.acme.editor", "acme", True, "raw photo")])
        # Ended, it stays in the library with the day it was last shown.
        services.set_state(self.made, Campaign.ENDED, None)
        self.assertIsNotNone(Campaign.objects.get(pk=self.made.pk).ended_at)
        row = visitor.get("/promote/library/").context["page"].object_list[0]
        self.assertEqual((row["running"], row["last"]), (False, timezone.localdate()))
        with self.settings(PROMOTE=False):
            self.assertEqual(visitor.get("/promote/library/").status_code, 404)
            self.assertEqual(self.client.get("/v1/promote/library").json(), {"ads": []})

    def test_not_this_one_again_is_the_browsers_own(self):
        page = Browser().get("/")
        self.assertContains(page, f'data-ad="{self.ads("store-home")[0].campaign.pk}"')
        self.assertContains(page, "data-ad-hide")
        script = (Path(__file__).resolve().parent.parent / "ultrastore_portal" / "static" / "portal" / "portal.js").read_text()
        self.assertIn("localStorage", script)
        self.assertNotIn("fetch(", script.split("function setUpAds")[1].split("setUpFields(document);")[0])   # hiding tells the store nothing
