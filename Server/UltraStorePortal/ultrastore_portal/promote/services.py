"""UltraAds: what changes - a campaign made or stopped, a balance paid into,
an advertisement shown or clicked, an order counted back to its click - and
the reports read from it. Which advertisement goes where is decide.py.

The rules:

- **Prepaid.** A click draws the balance down; at zero the campaigns wait.
  What goes in is a top-up through the store's payment provider, a voucher
  or a credit from the operator, each a ledger entry.
- **A click is charged once.** The token an advertisement was handed out
  with names the click; a second showing of the same token changes nothing.
- **What does not count is not charged**: the publisher's own click, a
  burst from one address, a click on a campaign that has ended since, and
  what the operator finds afterwards and credits back.
- **Counting without following anyone.** Shown and clicked are numbers per
  campaign, placement, country and day. A click row holds no account and no
  address, and goes after a month. An order names its click only through
  the token its buyer's store kept and sent with the checkout.

Version: 0.6.0 - search words and what was searched, an offer per place,
                 the policy, who of a publisher may do what, suggestions,
                 the public library, a report as CSV
Version: 0.5.0
"""

from __future__ import annotations

import datetime as dt
import logging
import secrets
from decimal import ROUND_UP, Decimal

from django.conf import settings
from django.core import signing
from django.core.cache import cache
from django.core.mail import send_mail
from django.db import IntegrityError, transaction
from django.db.models import F, Max, Min, Sum
from django.utils import timezone

from ..store.models import Publisher, PublisherMember, Setting
from ..store.services import audit
from .models import (Attribution, Balance, Campaign, Click, DayCount, Keyword, LedgerEntry, Placement, PlacementBid, PolicyAcceptance,
                     PolicyVersion, TermCount, TopUp)

log = logging.getLogger(__name__)

TOKEN_SALT = "ultrastore-portal.promote.ad"
CENT = Decimal("0.01")

# What the operator can change (the operator's Advertising page); kept as
# rows of the portal's own settings under "promote.<name>".
DEFAULTS = {
    "results_per_ad": "5",      # at most one advertisement for this many results on a page
    "auto_factor": "2",         # a campaign without a highest price offers the floor times this
    "attribution_days": "7",    # an order counts for a click this many days back
    "click_days": "30",         # how long a click row is kept
    "burst_clicks": "5",        # more clicks than this on one campaign from one address in ten minutes do not count
    "low_balance": "5.00",      # the publisher is mailed once when the balance falls under this
    "min_topup": "10.00",
    "new_daily_cap": "25.00",   # the most a campaign may spend a day while its publisher is new to advertising
    "new_days": "30",
    "pacing": "1",              # 1: a day's budget is spread over the day
    "small_numbers": "10",      # a report line that rests on fewer showings is folded into "other"
    "relevance_floor": "50",    # of a search's words, the share (in a hundred) a listing must hold to be shown for a chosen search word
    "max_keywords": "200",
}

CAMPAIGNS, MONEY = "campaigns", "money"
RIGHTS = {CAMPAIGNS: {PublisherMember.OWNER, PublisherMember.MARKETING}, MONEY: {PublisherMember.OWNER, PublisherMember.BILLING}}


class Refused(Exception):
    """A change the state does not allow; the message is for the person."""


def enabled() -> bool:
    return bool(settings.PROMOTE)


def setting(name: str) -> str:
    row = Setting.objects.filter(key="promote." + name).first()
    return row.value if row and row.value != "" else DEFAULTS[name]


def number(name: str) -> Decimal:
    try:
        return Decimal(setting(name))
    except ArithmeticError:
        return Decimal(DEFAULTS[name])


def set_setting(name: str, value: str, actor) -> None:
    if name not in DEFAULTS:
        raise Refused(f"There is no setting {name}.")
    try:
        if Decimal(value) < 0:
            raise ArithmeticError
    except ArithmeticError:
        raise Refused(f"{name} is a number, zero or more.") from None
    Setting.objects.update_or_create(key="promote." + name, defaults={"value": value})
    audit(actor, "promote.setting", "promote", name, value=value)


def money(value) -> str:
    return f"{Decimal(value):.2f}"


def may(role: str, what: str) -> bool:
    """Who of a publisher's people may do what: an owner everything,
    Marketing the campaigns, Billing the balance; everyone reads."""
    return role in RIGHTS[what]


def normalise(text: str) -> str:
    """A search as it is compared and counted: lower case, single spaces,
    six words and eighty characters at the most."""
    return " ".join((text or "").lower().split()[:6])[:80].strip()


def cents_up(value: Decimal) -> Decimal:
    return value.quantize(CENT, rounding=ROUND_UP)


# ------------------------------------------------------------------ balance

def balance_of(publisher: Publisher) -> Balance:
    row, _ = Balance.objects.get_or_create(publisher=publisher)
    return row


@transaction.atomic
def credit(publisher: Publisher, kind: str, amount: Decimal, note: str = "", actor=None) -> LedgerEntry:
    """Money into a balance, with its ledger entry."""
    if amount <= 0:
        raise Refused("An amount is more than nothing.")
    balance_of(publisher)
    balance = Balance.objects.select_for_update().get(publisher=publisher)
    balance.amount += amount
    balance.low_notified = False
    if balance.first_funded is None:
        balance.first_funded = timezone.now()
    balance.save()
    entry = LedgerEntry.objects.create(publisher=publisher, kind=kind, amount=amount, balance_after=balance.amount, note=note[:300], actor=actor if getattr(actor, "pk", None) else None)
    audit(actor, "promote." + kind, "publisher", publisher.slug, amount=money(amount), balance=money(balance.amount), note=note[:300])
    return entry


def is_new(publisher: Publisher) -> bool:
    """New to advertising: the first money came in less than a month ago,
    or none has yet. A new publisher's campaigns have a ceiling per day."""
    first = balance_of(publisher).first_funded
    return first is None or first > timezone.now() - dt.timedelta(days=int(number("new_days")))


def _low_balance_notice(publisher: Publisher, balance: Balance) -> None:
    if balance.low_notified or balance.amount >= number("low_balance"):
        return
    Balance.objects.filter(pk=balance.pk).update(low_notified=True)
    text = (f"Hello,\n\nthe advertising balance of {publisher.name} is down to {money(balance.amount)} {settings.PROMOTE_CURRENCY}. "
            f"When it is empty your campaigns wait, and run again as soon as there is a balance.\n\n"
            f"{settings.BASE_URL}/promote/balance/\n\nUltraStore\n")
    try:
        send_mail("UltraStore: your advertising balance is running low", text, None, [publisher.support_email])
    except Exception as error:   # noqa: BLE001 - SMTP fails in many ways; the campaign goes on
        log.warning("Low-balance notice to %s not sent: %s", publisher.slug, error)


# ---------------------------------------------------------------- campaigns

def check_campaign(publisher: Publisher, placements: list[Placement], daily_budget: Decimal, max_click: Decimal | None) -> None:
    if not placements:
        raise Refused("Tick at least one place for the advertisement to appear in.")
    closed = [p for p in placements if not p.is_open]
    if closed:
        raise Refused(f"{closed[0]} is not open for advertisements.")
    if max_click is not None:
        too_low = [p for p in placements if max_click < p.floor]
        if too_low:
            raise Refused(f"{money(max_click)} is under the least a click costs in \"{too_low[0].name}\" ({money(too_low[0].floor)}). Raise it, or untick that place.")
    cap = number("new_daily_cap")
    if is_new(publisher) and daily_budget > cap:
        raise Refused(f"In its first {setting('new_days')} days of advertising a campaign spends at most {money(cap)} {settings.PROMOTE_CURRENCY} a day.")


@transaction.atomic
def save_campaign(campaign: Campaign, placements: list[Placement], actor) -> Campaign:
    made = campaign.pk is None
    if campaign.app.publisher_id != campaign.publisher_id:
        raise Refused("A publisher promotes its own listings.")
    if campaign.app.kind != "app":
        raise Refused("Only an app is promoted, not a runtime or a library.")
    check_campaign(campaign.publisher, placements, campaign.daily_budget, campaign.max_click)
    campaign.save()
    campaign.placements.set(placements)
    audit(actor, "promote.campaign.add" if made else "promote.campaign.change", "campaign", str(campaign.pk), app=campaign.app.app_id,
          places=[p.key for p in placements], daily=money(campaign.daily_budget), max_click=money(campaign.max_click) if campaign.max_click else "")
    return campaign


def set_state(campaign: Campaign, state: str, actor) -> Campaign:
    if campaign.state == Campaign.ENDED:
        raise Refused("An ended campaign stays ended. Start a new one.")
    if state not in (Campaign.ACTIVE, Campaign.PAUSED, Campaign.ENDED):
        raise Refused("A campaign runs, is paused or has ended.")
    campaign.state = state
    campaign.ended_at = timezone.now() if state == Campaign.ENDED else None
    campaign.save(update_fields=["state", "ended_at", "updated"])
    audit(actor, "promote.campaign." + state, "campaign", str(campaign.pk))
    return campaign


def stop(campaign: Campaign, actor, stopped: bool, reason: str = "") -> Campaign:
    """The operator's: a campaign taken out, with the reason its publisher
    reads, or let run again."""
    if stopped and not reason.strip():
        raise Refused("Say why: the publisher reads it.")
    campaign.stopped, campaign.stop_reason = stopped, reason.strip()[:300] if stopped else ""
    campaign.save(update_fields=["stopped", "stop_reason", "updated"])
    audit(actor, "promote.campaign.stop" if stopped else "promote.campaign.release", "campaign", str(campaign.pk), reason=campaign.stop_reason)
    return campaign


# ------------------------------------------------- search words, offers by place

def add_keyword(campaign: Campaign, text: str, match: str, negative: bool, actor) -> Keyword:
    text = normalise(text)
    if not text:
        raise Refused("A search word is a word, or a few.")
    if match not in dict(Keyword.MATCHES):
        raise Refused("A search word matches broadly, as a phrase or exactly.")
    if campaign.keywords.count() >= int(number("max_keywords")):
        raise Refused(f"A campaign has at most {setting('max_keywords')} search words.")
    row, made = Keyword.objects.get_or_create(campaign=campaign, text=text, match=match, negative=negative)
    if made:
        audit(actor, "promote.keyword.add", "campaign", str(campaign.pk), text=text, match=match, negative=negative)
    return row


def remove_keyword(campaign: Campaign, pk, actor) -> None:
    row = campaign.keywords.filter(pk=pk).first()
    if row is not None:
        audit(actor, "promote.keyword.remove", "campaign", str(campaign.pk), text=row.text, match=row.match, negative=row.negative)
        row.delete()


@transaction.atomic
def set_bids(campaign: Campaign, offers: dict, actor) -> None:
    """The most to pay for a click, place by place: {placement key: an
    amount, or None for the campaign's own}. Only for places it is ticked for."""
    places = {p.key: p for p in campaign.placements.all()}
    for key, amount in offers.items():
        place = places.get(key)
        if place is None:
            continue
        if amount is None:
            PlacementBid.objects.filter(campaign=campaign, placement=place).delete()
        elif amount < place.floor:
            raise Refused(f"{money(amount)} is under the least a click costs in \"{place.name}\" ({money(place.floor)}).")
        else:
            PlacementBid.objects.update_or_create(campaign=campaign, placement=place, defaults={"max_click": amount})
    audit(actor, "promote.bids", "campaign", str(campaign.pk), offers={key: money(a) if a is not None else "" for key, a in offers.items() if key in places})


# ------------------------------------------------------------------- policy

def policy_due(publisher: Publisher) -> PolicyVersion | None:
    """The advertising policy in force that this publisher has not
    accepted yet; None when there is none, or it is on it."""
    current = PolicyVersion.current()
    if current is None or PolicyAcceptance.objects.filter(publisher=publisher, version=current.version).exists():
        return None
    return current


def accept_policy(publisher: Publisher, user) -> None:
    current = PolicyVersion.current()
    if current is None:
        return
    _, made = PolicyAcceptance.objects.get_or_create(publisher=publisher, version=current.version,
                                                     defaults={"accepted_by": user if getattr(user, "pk", None) else None})
    if made:
        audit(user, "promote.policy.accept", "publisher", publisher.slug, version=current.version)


def standing(campaign: Campaign, balance: Decimal | None = None) -> tuple[str, str]:
    """A campaign's standing as its publisher reads it: the campaign's own,
    and before that whether the publisher is on the advertising policy."""
    word, sentence = campaign.standing(balance)
    if word in ("running", "waiting") and policy_due(campaign.publisher) is not None:
        return "waiting", "The advertising policy has a new version: it runs again once it is accepted."
    return word, sentence


# ------------------------------------------------------------------- tokens

def make_token(campaign: Campaign, placement: Placement, price: Decimal, country: str, why: dict) -> str:
    return signing.dumps({"j": secrets.token_hex(12), "c": campaign.pk, "p": placement.key, "x": money(price), "k": country, "w": why},
                         salt=TOKEN_SALT, compress=True)


def read_token(token: str, max_age: int = 24 * 3600) -> dict | None:
    """What a token says, when it is one of ours and in time."""
    try:
        data = signing.loads(token or "", salt=TOKEN_SALT, max_age=max_age)
    except signing.BadSignature:
        return None
    return data if isinstance(data, dict) and {"j", "c", "p", "x"} <= set(data) else None


# ----------------------------------------------------------------- counting

def _count(campaign_id: int, placement_id: int, country: str, **steps) -> None:
    day = timezone.localdate()
    key = {"campaign_id": campaign_id, "placement_id": placement_id, "day": day, "country": country}
    try:
        with transaction.atomic():
            DayCount.objects.get_or_create(**key)
    except IntegrityError:   # two requests made the day's row at once: it is there
        pass
    DayCount.objects.filter(**key).update(**{name: F(name) + step for name, step in steps.items()})


def _parts(token: str, max_age: int = 24 * 3600):
    data = read_token(token, max_age)
    if data is None:
        return None
    campaign = Campaign.objects.select_related("publisher", "app").filter(pk=data["c"]).first()
    placement = Placement.objects.filter(key=data["p"]).first()
    if campaign is None or placement is None:
        return None
    return data, campaign, placement


def shown(token: str) -> bool:
    """An advertisement was on the screen: counted once per token."""
    parts = _parts(token)
    if parts is None:
        return False
    data, campaign, placement = parts
    if not cache.add("promote-shown:" + data["j"], 1, 24 * 3600):
        return False
    _count(campaign.pk, placement.pk, data.get("k", ""), shown=1)
    _count_term(campaign.pk, data, shown=1)
    return True


def _count_term(campaign_id: int, data: dict, **steps) -> None:
    """What was searched, counted for the campaign's report of search words."""
    term = normalise((data.get("w") or {}).get("q", ""))
    if not term:
        return
    key = {"campaign_id": campaign_id, "day": timezone.localdate(), "term": term}
    try:
        with transaction.atomic():
            TermCount.objects.get_or_create(**key)
    except IntegrityError:
        pass
    TermCount.objects.filter(**key).update(**{name: F(name) + step for name, step in steps.items()})


def is_own(user, publisher: Publisher) -> bool:
    return bool(getattr(user, "is_authenticated", False)) and PublisherMember.objects.filter(publisher=publisher, user=user).exists()


def clicked(token: str, *, address: str = "", own: bool = False) -> Click | None:
    """A click on an advertisement: charged once, when it counts. The
    address is used to notice a burst and is not kept."""
    parts = _parts(token)
    if parts is None:
        return None
    data, campaign, placement = parts
    price = Decimal(data["x"])
    reason = ""
    if own:
        reason = "own"
    elif campaign.standing()[0] != "running":
        reason = "ended"
    elif address:
        name = f"promote-burst:{campaign.pk}:{address}"
        if not cache.add(name, 1, 600):
            try:
                if cache.incr(name) > int(number("burst_clicks")):
                    reason = "burst"
            except ValueError:
                pass
    try:
        with transaction.atomic():
            click = Click.objects.create(jti=data["j"], campaign=campaign, placement=placement, country=data.get("k", ""), price=price,
                                         valid=not reason, reason=reason)
            if not reason:
                balance_of(campaign.publisher)
                balance = Balance.objects.select_for_update().get(publisher=campaign.publisher)
                if balance.amount < price:
                    click.valid, click.reason = False, "funds"
                    click.save(update_fields=["valid", "reason"])
                else:
                    balance.amount -= price
                    balance.save(update_fields=["amount"])
                    Campaign.objects.filter(pk=campaign.pk).update(spent=F("spent") + price)
    except IntegrityError:
        return Click.objects.filter(jti=data["j"]).first()   # the same click, told twice
    if click.valid:
        _count(campaign.pk, placement.pk, click.country, clicks=1, spent=price)
        _count_term(campaign.pk, data, clicks=1)
        _low_balance_notice(campaign.publisher, balance)
    else:
        _count(campaign.pk, placement.pk, click.country, invalid=1)
    return click


def installed(token: str) -> bool:
    """The promoted app was installed (or downloaded) after a click that
    counted: once per click."""
    data = read_token(token, int(number("attribution_days")) * 24 * 3600)
    if data is None:
        return False
    if Click.objects.filter(jti=data["j"], valid=True, installed=False).update(installed=True) != 1:
        return False
    click = Click.objects.get(jti=data["j"])
    _count(click.campaign_id, click.placement_id, click.country, installs=1)
    return True


@transaction.atomic
def invalidate(click: Click, actor, note: str = "") -> Click:
    """The operator found a click that should not have counted: its price
    goes back to the balance."""
    if not click.valid:
        raise Refused("This click was not charged.")
    click.valid, click.reason = False, "operator"
    click.save(update_fields=["valid", "reason"])
    Campaign.objects.filter(pk=click.campaign_id).update(spent=F("spent") - click.price)
    DayCount.objects.filter(campaign_id=click.campaign_id, placement_id=click.placement_id, day=timezone.localdate(click.at), country=click.country) \
        .update(clicks=F("clicks") - 1, invalid=F("invalid") + 1, spent=F("spent") - click.price)
    credit(click.campaign.publisher, LedgerEntry.CREDIT, click.price, note or f"A click on {click.campaign.name} that did not count", actor)
    return click


# -------------------------------------------------------------- attribution

def order_opened(order, token: str) -> None:
    """An order whose buyer's store sent the token of the last clicked
    advertisement: kept, when the click counted and was for this listing."""
    data = read_token(token, int(number("attribution_days")) * 24 * 3600)
    if data is None:
        return
    click = Click.objects.select_related("campaign").filter(jti=data["j"], valid=True).first()
    if click is None or click.campaign.app_id != order.app_id:
        return
    Attribution.objects.get_or_create(order=order, defaults={"campaign": click.campaign, "placement_id": click.placement_id,
                                                             "country": click.country, "click": click})


def order_paid(order) -> None:
    """The order is paid: it counts in its campaign's report, once."""
    row = Attribution.objects.filter(order=order, counted=False).first()
    if row is None or Attribution.objects.filter(pk=row.pk, counted=False).update(counted=True) != 1:
        return
    _count(row.campaign_id, row.placement_id, row.country, orders=1, revenue=order.amount)


# ------------------------------------------------------------------ reports

FIELDS = ("shown", "clicks", "invalid", "spent", "installs", "orders", "revenue")


def _line(label, row: dict) -> dict:
    shown_, clicks, spent, orders = row.get("shown") or 0, row.get("clicks") or 0, row.get("spent") or Decimal(0), row.get("orders") or 0
    return {
        "label": label, "shown": shown_, "clicks": clicks, "invalid": row.get("invalid") or 0, "spent": spent,
        "installs": row.get("installs") or 0, "orders": orders, "revenue": row.get("revenue") or Decimal(0),
        "rate": (Decimal(clicks) * 100 / shown_).quantize(Decimal("0.1")) if shown_ else None,
        "per_click": (spent / clicks).quantize(CENT) if clicks else None,
        "per_order": (spent / orders).quantize(CENT) if orders else None,
    }


def _sums(rows):
    return rows.aggregate(**{name: Sum(name) for name in FIELDS})


def report(campaigns, days: int = 30) -> dict:
    """The totals of some campaigns over the last days, by day, by place
    and by country. A country that rests on fewer showings than the
    threshold is folded into "other": a report is about an audience, never
    about a person."""
    since = timezone.localdate() - dt.timedelta(days=days - 1)
    rows = DayCount.objects.filter(campaign__in=campaigns, day__gte=since)
    by_day = [_line(row["day"], row) for row in rows.values("day").annotate(**{n: Sum(n) for n in FIELDS}).order_by("-day")]
    by_place = [_line(f"{row['placement__name']}", row)
                for row in rows.values("placement__name", "placement__position").annotate(**{n: Sum(n) for n in FIELDS}).order_by("placement__position")]
    small = int(number("small_numbers"))
    by_country, other = [], {name: 0 for name in FIELDS}
    for row in rows.exclude(country="").values("country").annotate(**{n: Sum(n) for n in FIELDS}).order_by("-shown"):
        if (row["shown"] or 0) < small:
            for name in FIELDS:
                other[name] += row[name] or 0
        else:
            by_country.append(_line(row["country"], row))
    if any(other.values()):
        by_country.append(_line("other", other))
    return {"days": days, "total": _line("Total", _sums(rows)), "by_day": by_day, "by_place": by_place, "by_country": by_country}


def terms_report(campaign: Campaign, days: int = 30) -> list[dict]:
    """What people searched when this campaign was shown, most shown first.
    A search that rests on fewer showings than the threshold is not named:
    a rare search can be one person's, and is nobody's business. They are
    counted together as "other searches"."""
    since = timezone.localdate() - dt.timedelta(days=days - 1)
    small = int(number("small_numbers"))
    rows, other = [], {"shown": 0, "clicks": 0}
    for row in TermCount.objects.filter(campaign=campaign, day__gte=since).values("term").annotate(shown=Sum("shown"), clicks=Sum("clicks")).order_by("-shown", "term"):
        if (row["shown"] or 0) < small:
            other["shown"] += row["shown"] or 0
            other["clicks"] += row["clicks"] or 0
        else:
            rows.append({"term": row["term"], "shown": row["shown"], "clicks": row["clicks"], "named": True})
    if other["shown"]:
        rows.append({"term": "other searches", **other, "named": False})
    for row in rows:
        row["rate"] = (Decimal(row["clicks"]) * 100 / row["shown"]).quantize(Decimal("0.1")) if row["shown"] else None
    return rows


def suggestions(campaign: Campaign, balance: Decimal) -> list[str]:
    """What the numbers of a campaign suggest, in a sentence each. Nothing
    here changes anything: the publisher decides."""
    today, found = timezone.localdate(), []
    week = dict(DayCount.objects.filter(campaign=campaign, day__gte=today - dt.timedelta(days=6), day__lt=today)
                .values("day").annotate(s=Sum("spent")).values_list("day", "s"))
    spent_out = sum(1 for spent in week.values() if spent and spent >= campaign.daily_budget * Decimal("0.95"))
    if spent_out >= 3:
        found.append(f"The budget ran out on {spent_out} of the last 7 days. A higher budget per day would have shown it longer on those days.")
    total = report([campaign])["total"]
    if total["shown"] >= 500 and total["rate"] is not None and total["rate"] < Decimal("0.5"):
        found.append("It is seen and seldom clicked. What people see is the icon, the name and the one-line summary: those are worth a second look.")
    if total["clicks"] >= 30 and total["installs"] + total["orders"] == 0:
        found.append("People click and do not install. The listing's page - its screenshots, its description, its price - is where they turn back.")
    if campaign.state == Campaign.ACTIVE and Decimal(0) < balance < campaign.daily_budget * 3:
        found.append("The balance covers less than three days of this campaign's budget.")
    if campaign.keywords.filter(negative=False).exists() and total["shown"] == 0 and campaign.created < timezone.now() - dt.timedelta(days=3):
        found.append("It has its own search words and was not shown in three days: nobody searched for them, or the listing does not hold the words searched.")
    return found


def library(days: int = 365) -> list[dict]:
    """Every advertisement that ran in the last year or runs now: what was
    promoted, who paid, from when to when, where, and for which searches.
    Public: anyone can see who advertises what in the store."""
    since = timezone.localdate() - dt.timedelta(days=days)
    shown = dict(DayCount.objects.filter(day__gte=since, shown__gt=0).values("campaign").annotate(first=Min("day"), last=Max("day")).values_list("campaign", "first"))
    last = dict(DayCount.objects.filter(day__gte=since, shown__gt=0).values("campaign").annotate(last=Max("day")).values_list("campaign", "last"))
    rows = []
    for campaign in Campaign.objects.filter(pk__in=shown).select_related("app", "publisher").prefetch_related("placements", "keywords"):
        words = [k.text for k in campaign.keywords.all() if not k.negative]
        rows.append({
            "campaign": campaign, "app": campaign.app, "publisher": campaign.publisher, "first": shown[campaign.pk], "last": last[campaign.pk],
            "running": campaign.standing()[0] == "running", "places": list(campaign.placements.all()),
            "shown_for": ", ".join(words) if words else "Searches, categories and apps its listing fits"})
    rows.sort(key=lambda row: (not row["running"], -row["last"].toordinal(), row["app"].name))
    return rows


def csv_cell(value) -> str:
    """A cell of an exported report. Text that a spreadsheet would take for
    a formula - a search can be anything a person typed - is made plain."""
    text = "" if value is None else str(value)
    return "'" + text if text[:1] in ("=", "+", "-", "@", "\t", "\r") and not isinstance(value, (int, Decimal, dt.date)) else text


def report_csv(campaign: Campaign, what: str, days: int = 30) -> list[list[str]]:
    data = report([campaign], days)
    if what == "terms":
        return [["Search", "Shown", "Clicks", "Click rate %"]] + [[csv_cell(r["term"]), str(r["shown"]), str(r["clicks"]), "" if r["rate"] is None else str(r["rate"])]
                                                                 for r in terms_report(campaign, days)]
    head = ["Day" if what == "days" else "Place", "Shown", "Clicks", "Clicks not counted", "Spent", "Installs", "Orders", "Order value"]
    rows = data["by_day"] if what == "days" else data["by_place"]
    return [head] + [[csv_cell(r["label"]), str(r["shown"]), str(r["clicks"]), str(r["invalid"]), money(r["spent"]), str(r["installs"]), str(r["orders"]),
                      money(r["revenue"])] for r in rows]


def spent_by_day(publisher: Publisher, days: int = 30) -> list[dict]:
    since = timezone.localdate() - dt.timedelta(days=days - 1)
    rows = DayCount.objects.filter(campaign__publisher=publisher, day__gte=since, spent__gt=0)
    return list(rows.values("day").annotate(spent=Sum("spent"), clicks=Sum("clicks")).order_by("-day"))


def revenue(days: int = 30) -> dict:
    """The operator's figures: what advertising brought in, and by place."""
    since = timezone.localdate() - dt.timedelta(days=days - 1)
    rows = DayCount.objects.filter(day__gte=since)
    by_place = [_line(row["placement__name"], row)
                for row in rows.values("placement__name", "placement__position").annotate(**{n: Sum(n) for n in FIELDS}).order_by("placement__position")]
    by_day = [_line(row["day"], row) for row in rows.values("day").annotate(**{n: Sum(n) for n in FIELDS}).order_by("-day")]
    return {"days": days, "total": _line("Total", _sums(rows)), "today": _line("Today", _sums(rows.filter(day=timezone.localdate()))),
            "by_place": by_place, "by_day": by_day}


# ------------------------------------------------------------------ top-ups

def topups_open() -> tuple[bool, str]:
    """Whether a balance can be paid into, and why not when it cannot."""
    from ..billing import services as billing
    if not settings.PROMOTE_TOPUPS:
        return False, "Paying into a balance is not open yet. The store grants a first balance as a voucher."
    if billing.missing_settings():
        return False, "The store's payment provider is not set up."
    return True, ""


def open_topup(publisher: Publisher, amount: Decimal, user) -> TopUp:
    possible, why_not = topups_open()
    if not possible:
        raise Refused(why_not)
    if amount < number("min_topup"):
        raise Refused(f"The least to pay in is {money(number('min_topup'))} {settings.PROMOTE_CURRENCY}.")
    made = TopUp.objects.create(publisher=publisher, amount=amount, currency=settings.PROMOTE_CURRENCY, provider=settings.PAYMENT_PROVIDER,
                                created_by=user if getattr(user, "pk", None) else None)
    audit(user, "promote.topup.open", "topup", made.public_id, publisher=publisher.slug, amount=money(amount))
    return made


@transaction.atomic
def topup_report(fields: dict[str, str]) -> TopUp:
    """The payment provider's signed report on a top-up - the same report,
    signed the same way, that pays an order (billing/services.py). Only it
    puts money into a balance; the browser coming back proves nothing."""
    from ..billing import services as billing
    from ..billing import skrill
    merchant_id, secret_word = billing.merchant()
    if not secret_word or not skrill.verify_status(fields, merchant_id, secret_word):
        raise billing.BillingError(403, "bad_signature", "The status report's signature is not the payment provider's")
    made = TopUp.objects.select_for_update().select_related("publisher").filter(public_id=fields.get("transaction_id", "")).first()
    if made is None:
        raise billing.BillingError(404, "no_such_order", "The status report names no top-up of ours")
    status = fields.get("status", "")
    if made.status == TopUp.PAID:
        return made   # the provider repeats a report it got no answer to
    if status != skrill.STATUS_PROCESSED:
        made.status = {skrill.STATUS_CANCELLED: TopUp.CANCELLED, skrill.STATUS_FAILED: TopUp.FAILED}.get(status, made.status)
        made.note = f"status {status}"
        made.save(update_fields=["status", "note"])
        return made
    amount = billing.parse_money(fields.get("amount") or fields.get("mb_amount", ""))
    currency = (fields.get("currency") or fields.get("mb_currency", "")).upper()
    if amount != made.amount or currency != made.currency:
        raise billing.BillingError(400, "amount_mismatch", "The amount paid is not the top-up's")
    made.status, made.paid_at = TopUp.PAID, timezone.now()
    made.provider_transaction = fields.get("mb_transaction_id", "")
    made.receipt_no = billing.next_receipt_no()
    made.save()
    credit(made.publisher, LedgerEntry.TOPUP, made.amount, f"Top-up, receipt no. {made.receipt_no}", made.created_by)
    return made


def test_topup_report(made: TopUp, status: str) -> dict[str, str]:
    """The report the test provider's button sends, signed as Skrill signs."""
    from ..billing import services as billing
    from ..billing import skrill
    merchant_id, secret_word = billing.merchant()
    amount = money(made.amount)
    return {"transaction_id": made.public_id, "mb_transaction_id": "test-" + secrets.token_hex(6), "mb_amount": amount, "mb_currency": made.currency,
            "amount": amount, "currency": made.currency, "status": status, "merchant_id": merchant_id,
            "md5sig": skrill.status_signature(merchant_id, made.public_id, secret_word, amount, made.currency, status)}


# ------------------------------------------------------------------ cleanup

def cleanup() -> int:
    """Click rows, and what was searched, older than they are kept:
    removed. The totals by day stay."""
    days = int(number("click_days"))
    removed, _ = Click.objects.filter(at__lt=timezone.now() - dt.timedelta(days=days)).delete()
    TermCount.objects.filter(day__lt=timezone.localdate() - dt.timedelta(days=days)).delete()
    return removed
