"""Which advertisement goes where.

An advertisement is chosen from **what is on the page** - the placement,
the search words, the category, the listing being looked at, the system the
store runs on - and from nothing about who is looking: this module is never
handed a request, a session or an account, so it could not use one.

For a placement, each time:

1. **Who may.** A campaign that runs, ticked this placement, promotes a
   listing that is public, has budget left today and a balance.
2. **Is it relevant.** In a search, the listing must answer the search by
   the store's own rule (every word found in it); in a category, be of the
   category; beside a listing, be of its category and not be it. Below that
   an advertisement is not shown at any price.
3. **Rank.** The offer times a quality figure: how often this listing was
   clicked in this placement, against a modest prior, so that a new
   campaign is neither favoured nor buried.
4. **Price.** The winner pays what it took to stay ahead of the next one
   (of another publisher: one's own campaigns never bid against each
   other), never more than it offered and never less than the placement's
   floor, which the operator sets. With one advertiser that is the floor - a price
   list; with several it is an auction.

The shop's own results and their order are made first, elsewhere
(store/queries.py), and are not touched: money moves no listing in them.

Version: 0.5.0
"""

from __future__ import annotations

import datetime as dt
from dataclasses import dataclass
from decimal import Decimal

from django.db.models import Q, Sum
from django.utils import timezone

from ..store.models import App, Release
from . import services
from .models import Balance, Campaign, DayCount, Placement

PRIOR_CLICKS, PRIOR_SHOWN = Decimal(1), Decimal(50)   # two clicks in a hundred, until the campaign has numbers of its own
QUALITY_DAYS = 30


@dataclass
class Ad:
    campaign: Campaign
    placement: Placement
    price: Decimal
    token: str
    why: list[str]

    @property
    def app(self) -> App:
        return self.campaign.app


def allowed(placement: Placement, results: int | None, count: int | None = None) -> int:
    """How many advertisements a page may carry in this placement: its own
    slots, what was asked for, and one for so many results - none on a page
    with no results at all."""
    slots = placement.slots if count is None else min(placement.slots, max(count, 0))
    if results is None:
        return slots
    if results <= 0:
        return 0
    return min(slots, max(1, results // max(1, int(services.number("results_per_ad")))))


def _matches(app: App, words: list[str]) -> bool:
    """The store's own search rule (store/queries.py): every word is in the
    name, the summary, the tags, the id or the publisher's name."""
    haystack = " ".join([app.name, app.summary, app.tags, app.app_id, app.publisher.name]).lower()
    return all(word in haystack for word in words)


def _why(placement: Placement, q: str, category_name: str, beside: App | None) -> tuple[dict, list[str]]:
    if placement.page == Placement.SEARCH:
        return {"q": q[:80]}, [f"It matches your search for \"{q[:80]}\"."]
    if placement.page == Placement.CATEGORY:
        return {"cat": category_name}, [f"You are looking at the category {category_name}."]
    if placement.page == Placement.DETAIL and beside is not None:
        return {"beside": beside.name}, [f"It is in the same category as {beside.name}, which you are looking at."]
    return {}, ["It is shown on this page to everyone who opens it."]


def explain(placement: Placement, why: dict) -> list[str]:
    """The same sentences, from what a token carries."""
    if why.get("q"):
        return [f"It matched a search for \"{why['q']}\"."]
    if why.get("cat"):
        return [f"It was shown in the category {why['cat']}."]
    if why.get("beside"):
        return [f"It is in the same category as {why['beside']}, the app it was shown beside."]
    return ["It was shown on that page to everyone who opened it."]


def decide(placement_key: str, *, q: str = "", category: str = "", listing: str = "", os: str = "", country: str = "",
           seen: tuple = (), skip: tuple = (), count: int | None = None, results: int | None = None, now: dt.datetime | None = None) -> list[Ad]:
    """The advertisements for one placement, best first. `results` is how
    many unpaid results the page has (a list, a search); `seen` the
    campaigns the device says have had their share; `listing` the app a
    detail page is about; `skip` the listings (app ids) not to advertise on
    this page - the ones a short list already shows."""
    if not services.enabled():
        return []
    placement = Placement.objects.filter(key=placement_key, state=Placement.OPEN).first()
    if placement is None:
        return []
    room = allowed(placement, results, count)
    if room <= 0:
        return []
    now = now or timezone.now()
    today = timezone.localdate(now)
    words = [word.lower() for word in q.split()[:6]]
    if placement.page == Placement.SEARCH and not words:
        return []
    from ..store import constants
    category_name = constants.CATEGORY_NAMES.get(category, "")
    if placement.page == Placement.CATEGORY and not category_name:
        return []
    beside = None
    if placement.page == Placement.DETAIL:
        beside = App.objects.filter(app_id=listing).select_related("publisher").first()
        if beside is None or not beside.category:
            return []

    campaigns = (Campaign.objects.filter(state=Campaign.ACTIVE, stopped=False, placements=placement, starts__lte=today, app__in=App.objects.listed())
                 .filter(Q(ends__isnull=True) | Q(ends__gte=today))
                 .exclude(pk__in=[pk for pk in seen if isinstance(pk, int)][:200])
                 .select_related("app__publisher", "publisher").prefetch_related("app__releases__packages"))
    eligible = []
    for campaign in campaigns[:500]:
        app = campaign.app
        if placement.page == Placement.SEARCH and not _matches(app, words):
            continue
        if placement.page == Placement.CATEGORY and app.category != category:
            continue
        if beside is not None and (app.pk == beside.pk or app.category != beside.category):
            continue
        if app.app_id in skip:
            continue
        if os and not any(p.os == os for r in app.releases.all() if r.state == Release.PUBLISHED for p in r.packages.all()):
            continue
        if campaign.total_budget is not None and campaign.spent >= campaign.total_budget:
            continue
        eligible.append(campaign)
    if not eligible:
        return []

    ids = [campaign.pk for campaign in eligible]
    balances = dict(Balance.objects.filter(publisher__in={c.publisher_id for c in eligible}).values_list("publisher_id", "amount"))
    spent_today = dict(DayCount.objects.filter(campaign__in=ids, day=today).values("campaign").annotate(s=Sum("spent")).values_list("campaign", "s"))
    since = today - dt.timedelta(days=QUALITY_DAYS)
    history = {row["campaign"]: row for row in DayCount.objects.filter(campaign__in=ids, placement=placement, day__gte=since)
               .values("campaign").annotate(shown=Sum("shown"), clicks=Sum("clicks"))}
    paced = services.setting("pacing") == "1"
    part_of_day = Decimal(now.hour * 3600 + now.minute * 60 + now.second) / Decimal(86400)
    auto = placement.floor * services.number("auto_factor")

    ranked = []
    for campaign in eligible:
        balance = balances.get(campaign.publisher_id, Decimal(0))
        offer = campaign.max_click if campaign.max_click is not None else auto
        if offer < placement.floor or balance < placement.floor:
            continue
        so_far = spent_today.get(campaign.pk) or Decimal(0)
        budget = campaign.daily_budget
        if paced:   # a day's budget is spread over the day, a little ahead of the clock
            budget = min(budget, campaign.daily_budget * min(Decimal(1), part_of_day + Decimal("0.1")))
        if so_far + placement.floor > budget:
            continue
        past = history.get(campaign.pk, {})
        quality = (Decimal(past.get("clicks") or 0) + PRIOR_CLICKS) / (Decimal(past.get("shown") or 0) + PRIOR_SHOWN)
        ranked.append((offer * quality, quality, offer, min(balance, budget - so_far), campaign))
    ranked.sort(key=lambda row: (row[0], row[4].pk), reverse=True)

    chosen, apps = [], set()
    for row in ranked:   # one advertisement per listing on a page
        if row[4].app_id not in apps:
            apps.add(row[4].app_id)
            chosen.append(row)
    ads = []
    for index, (rank, quality, offer, limit, campaign) in enumerate(chosen[:room]):
        price = placement.floor
        # What it took to stay ahead of the next one - the next one of
        # another publisher: nobody's two campaigns bid against each other.
        below = next((row for row in chosen[index + 1:] if row[4].publisher_id != campaign.publisher_id), None)
        if below is not None:
            price = max(price, services.cents_up(below[0] / quality) + services.CENT)
        price = min(price, offer, limit)
        token_why, sentences = _why(placement, q, category_name, beside)
        ads.append(Ad(campaign, placement, price, services.make_token(campaign, placement, price, country[:2].upper(), token_why), sentences))
    return ads


def for_page(placement_key: str, **context) -> list[Ad]:
    """The advertisements for a page the portal draws itself, each counted
    as shown - the page is the showing."""
    ads = decide(placement_key, **context)
    for ad in ads:
        services.shown(ad.token)
    return ads
