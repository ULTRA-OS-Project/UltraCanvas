"""UltraAds: sponsored listings in the app store, and the console around
them (Docs/Research/UltraAdsDesignProposal.md, phases 1 and 2).

    Placement   a named position on a page that may hold advertisements:
                which shop, which page, how many, the least a click costs
    Campaign    a publisher promotes one listing: where (the placements it
                ticked), how much a day, how long
    Balance     what a publisher has paid in and not yet spent
    LedgerEntry what went into a balance: a top-up, a voucher, a credit
    TopUp       a payment into a balance, at the payment provider
    DayCount    per campaign, placement, country and day: shown, clicked,
                spent, installed, ordered. Totals and nothing else
    Click       one click, for the invoice and for finding the ones that do
                not count. No account, no address. Deleted after a month
    Attribution an order and the click it came from
    Keyword     a search word of a campaign's own choosing, or one it
                excludes
    PlacementBid the most a campaign pays for a click in one place
    TermCount   what people searched when a campaign was shown, as numbers
                per day; kept a month, shown above a threshold
    PolicyVersion, PolicyAcceptance  the advertising policy and who is on it

There is no record of who saw or clicked what: an advertisement is chosen
from what is on the page (decide.py), and counted as a number.

Version: 0.6.0 - Keyword, PlacementBid, TermCount, PolicyVersion,
                 PolicyAcceptance; Campaign.ended_at
Version: 0.5.0
"""

from __future__ import annotations

import uuid
from decimal import Decimal

from django.conf import settings
from django.core.validators import MinValueValidator
from django.db import models
from django.utils import timezone

from ..store.models import App, Publisher

AREAS = [("store", "App store"), ("games", "Games shop"), ("market", "Marketplace"), ("trade", "B2B trade"),
         ("video", "Video"), ("music", "Music and podcasts"), ("books", "Books and audiobooks")]
AREA_NAMES = dict(AREAS)
HOME_AREA = "store"   # the one shop that exists: what "this shop" means on the Promote page


class Placement(models.Model):
    OPEN, CLOSED, PLANNED = "open", "closed", "planned"
    STATES = [(OPEN, "Open"), (CLOSED, "Switched off"), (PLANNED, "Not open yet")]
    SEARCH, CATEGORY, HOME, DETAIL = "search", "category", "home", "detail"   # what the page is, and so what the context is

    key = models.SlugField(max_length=40, unique=True, help_text="store-search: the name the pages and the clients ask by.")
    area = models.CharField(max_length=16, choices=AREAS)
    page = models.CharField(max_length=16, help_text="search, category, home or detail: what an advertisement there is chosen from.")
    name = models.CharField(max_length=80)
    shows = models.CharField(max_length=200, help_text="What an advertisement looks like there, in a line.")
    slots = models.PositiveSmallIntegerField(default=2, help_text="How many advertisements at the most.")
    floor = models.DecimalField(max_digits=6, decimal_places=2, default=Decimal("0.10"), validators=[MinValueValidator(Decimal("0.01"))],
                                help_text="The least a click costs.")
    state = models.CharField(max_length=8, choices=STATES, default=PLANNED)
    position = models.PositiveSmallIntegerField(default=0)

    class Meta:
        ordering = ["position", "id"]

    def __str__(self) -> str:
        return f"{self.area_name}: {self.name}"

    @property
    def area_name(self) -> str:
        return AREA_NAMES.get(self.area, self.area)

    @property
    def is_open(self) -> bool:
        return self.state == self.OPEN


class Campaign(models.Model):
    ACTIVE, PAUSED, ENDED = "active", "paused", "ended"
    STATES = [(ACTIVE, "Running"), (PAUSED, "Paused"), (ENDED, "Ended")]

    publisher = models.ForeignKey(Publisher, on_delete=models.CASCADE, related_name="campaigns")
    app = models.ForeignKey(App, on_delete=models.CASCADE, related_name="campaigns")
    name = models.CharField(max_length=80)
    placements = models.ManyToManyField(Placement, related_name="campaigns")
    daily_budget = models.DecimalField(max_digits=8, decimal_places=2, validators=[MinValueValidator(Decimal("1.00"))])
    total_budget = models.DecimalField(max_digits=10, decimal_places=2, null=True, blank=True)
    max_click = models.DecimalField(max_digits=6, decimal_places=2, null=True, blank=True,
                                    help_text="The most to pay for a click. Empty: the service bids, up to the budget.")
    starts = models.DateField(default=timezone.localdate)
    ends = models.DateField(null=True, blank=True)
    state = models.CharField(max_length=8, choices=STATES, default=ACTIVE, db_index=True)
    # The operator's.
    stopped = models.BooleanField(default=False, db_index=True)
    stop_reason = models.CharField(max_length=300, blank=True)
    spent = models.DecimalField(max_digits=12, decimal_places=2, default=0, editable=False)
    created_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    created = models.DateTimeField(auto_now_add=True)
    updated = models.DateTimeField(auto_now=True)
    ended_at = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return f"{self.name} ({self.app.app_id})"

    def standing(self, balance: Decimal | None = None) -> tuple[str, str]:
        """(a word, a sentence): whether the campaign is being shown right
        now and, when it is not, the one reason a publisher can act on."""
        today = timezone.localdate()
        if self.stopped:
            return "stopped", f"Stopped by the store: {self.stop_reason}"
        if self.state == self.ENDED or (self.ends and self.ends < today):
            return "ended", "Ended."
        if self.state == self.PAUSED:
            return "paused", "Paused."
        if not self.app.is_public or self.app.visibility != "listed":
            return "waiting", "The listing is not public in the store."
        if self.starts > today:
            return "waiting", f"Starts on {self.starts:%Y-%m-%d}."
        if self.total_budget is not None and self.spent >= self.total_budget:
            return "ended", "The total budget is spent."
        if balance is not None and balance <= 0:
            return "waiting", "The balance is empty: it runs again as soon as there is one."
        return "running", "Running."


class Balance(models.Model):
    publisher = models.OneToOneField(Publisher, on_delete=models.CASCADE, related_name="promote_balance")
    amount = models.DecimalField(max_digits=12, decimal_places=2, default=0)
    low_notified = models.BooleanField(default=False)
    first_funded = models.DateTimeField(null=True, blank=True)

    def __str__(self) -> str:
        return f"{self.publisher}: {self.amount}"


class LedgerEntry(models.Model):
    """What went into a balance, appended and never edited. What went out
    of it is the spending, which is counted by day (DayCount)."""

    TOPUP, VOUCHER, CREDIT = "topup", "voucher", "credit"
    KINDS = [(TOPUP, "Top-up"), (VOUCHER, "Voucher"), (CREDIT, "Credit for clicks that did not count")]

    publisher = models.ForeignKey(Publisher, on_delete=models.CASCADE, related_name="promote_entries")
    kind = models.CharField(max_length=8, choices=KINDS)
    amount = models.DecimalField(max_digits=10, decimal_places=2)
    balance_after = models.DecimalField(max_digits=12, decimal_places=2)
    note = models.CharField(max_length=300, blank=True)
    actor = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    created = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        ordering = ["-id"]
        verbose_name_plural = "ledger entries"


def _topup_id() -> str:
    return uuid.uuid4().hex


class TopUp(models.Model):
    OPEN, PAID, CANCELLED, FAILED = "open", "paid", "cancelled", "failed"
    STATES = [(OPEN, "Open"), (PAID, "Paid"), (CANCELLED, "Cancelled"), (FAILED, "Failed")]

    public_id = models.CharField(max_length=32, unique=True, default=_topup_id, editable=False)
    publisher = models.ForeignKey(Publisher, on_delete=models.PROTECT, related_name="promote_topups")
    amount = models.DecimalField(max_digits=8, decimal_places=2)
    currency = models.CharField(max_length=3)
    status = models.CharField(max_length=10, choices=STATES, default=OPEN, db_index=True)
    provider = models.CharField(max_length=16)
    provider_transaction = models.CharField(max_length=64, blank=True)
    receipt_no = models.PositiveBigIntegerField(null=True, blank=True)
    note = models.CharField(max_length=300, blank=True)
    created_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    paid_at = models.DateTimeField(null=True, blank=True)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-id"]


class DayCount(models.Model):
    campaign = models.ForeignKey(Campaign, on_delete=models.CASCADE, related_name="counts")
    placement = models.ForeignKey(Placement, on_delete=models.CASCADE, related_name="counts")
    day = models.DateField(db_index=True)
    country = models.CharField(max_length=2, blank=True)
    shown = models.PositiveIntegerField(default=0)
    clicks = models.PositiveIntegerField(default=0)
    invalid = models.PositiveIntegerField(default=0)
    spent = models.DecimalField(max_digits=10, decimal_places=2, default=0)
    installs = models.PositiveIntegerField(default=0)
    orders = models.PositiveIntegerField(default=0)
    revenue = models.DecimalField(max_digits=10, decimal_places=2, default=0)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["campaign", "placement", "day", "country"], name="one_count_per_day")]


class Click(models.Model):
    """One click. Nothing here says who clicked: no account, no address."""

    jti = models.CharField(max_length=32, unique=True)
    campaign = models.ForeignKey(Campaign, on_delete=models.CASCADE, related_name="clicks")
    placement = models.ForeignKey(Placement, on_delete=models.CASCADE, related_name="+")
    country = models.CharField(max_length=2, blank=True)
    price = models.DecimalField(max_digits=6, decimal_places=2)
    valid = models.BooleanField(default=True)
    reason = models.CharField(max_length=16, blank=True, help_text="Why it did not count: own, burst, funds, ended, operator.")
    installed = models.BooleanField(default=False)
    at = models.DateTimeField(default=timezone.now, db_index=True)

    class Meta:
        ordering = ["-id"]


class Attribution(models.Model):
    """An order that followed a click on an advertisement for the same
    listing; counted in the campaign's report once the order is paid."""

    order = models.OneToOneField("billing.Order", on_delete=models.CASCADE, related_name="promote_attribution")
    campaign = models.ForeignKey(Campaign, on_delete=models.CASCADE, related_name="attributions")
    placement = models.ForeignKey(Placement, on_delete=models.CASCADE, related_name="+")
    country = models.CharField(max_length=2, blank=True)
    click = models.ForeignKey(Click, null=True, blank=True, on_delete=models.SET_NULL, related_name="attributions")
    counted = models.BooleanField(default=False)
    created = models.DateTimeField(auto_now_add=True)


class Keyword(models.Model):
    """A search word a campaign chose - shown for the searches it matches
    instead of for every search the listing answers - or one it excludes."""

    EXACT, PHRASE, BROAD = "exact", "phrase", "broad"
    MATCHES = [(BROAD, "Every word, in any order"), (PHRASE, "The words together, in this order"), (EXACT, "Exactly this search")]

    campaign = models.ForeignKey(Campaign, on_delete=models.CASCADE, related_name="keywords")
    text = models.CharField(max_length=80)
    match = models.CharField(max_length=8, choices=MATCHES, default=BROAD)
    negative = models.BooleanField(default=False)

    class Meta:
        ordering = ["negative", "text"]
        constraints = [models.UniqueConstraint(fields=["campaign", "text", "match", "negative"], name="one_keyword_per_campaign")]

    def __str__(self) -> str:
        return ("-" if self.negative else "") + self.text

    def matches(self, words: list[str]) -> bool:
        mine = self.text.split()
        if self.match == self.EXACT:
            return words == mine
        if self.match == self.PHRASE:
            return any(words[i:i + len(mine)] == mine for i in range(len(words) - len(mine) + 1))
        return set(mine) <= set(words)


class PlacementBid(models.Model):
    campaign = models.ForeignKey(Campaign, on_delete=models.CASCADE, related_name="bids")
    placement = models.ForeignKey(Placement, on_delete=models.CASCADE, related_name="+")
    max_click = models.DecimalField(max_digits=6, decimal_places=2)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["campaign", "placement"], name="one_bid_per_place")]


class TermCount(models.Model):
    """What was searched when a campaign's advertisement was shown: the
    words and two numbers, per day. Nothing says who searched."""

    campaign = models.ForeignKey(Campaign, on_delete=models.CASCADE, related_name="terms")
    day = models.DateField(db_index=True)
    term = models.CharField(max_length=80)
    shown = models.PositiveIntegerField(default=0)
    clicks = models.PositiveIntegerField(default=0)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["campaign", "day", "term"], name="one_term_per_day")]


class PolicyVersion(models.Model):
    """One version of the advertising policy. A publisher advertises once
    it has accepted the version in force; the operator writes it."""

    version = models.PositiveIntegerField(unique=True)
    text = models.TextField()
    changes = models.TextField(blank=True, help_text="What changed against the version before.")
    in_force_from = models.DateField(default=timezone.localdate)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-version"]

    def __str__(self) -> str:
        return f"Advertising policy {self.version}"

    @classmethod
    def current(cls) -> "PolicyVersion | None":
        return cls.objects.filter(in_force_from__lte=timezone.localdate()).order_by("-version").first()


class PolicyAcceptance(models.Model):
    publisher = models.ForeignKey(Publisher, on_delete=models.CASCADE, related_name="policy_acceptances")
    version = models.PositiveIntegerField()
    accepted_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    accepted_at = models.DateTimeField(default=timezone.now)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["publisher", "version"], name="one_acceptance_per_version")]
