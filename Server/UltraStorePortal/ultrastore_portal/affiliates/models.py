"""UltraAffiliates, phase 1 of Docs/Research/UltraAffiliatesDesignProposal.md:
the affiliate, the merchant's boost, a click on an affiliate's link, the
commission a sale earns, what was paid, and the record of a block or a ban.

Version: 0.10.0
"""

from __future__ import annotations

import secrets

from django.conf import settings
from django.core.validators import MaxValueValidator, MinValueValidator, RegexValidator
from django.db import models

from ..billing.models import Order, Payment
from ..store import constants
from ..store.models import App, Publisher

HANDLE_VALIDATOR = RegexValidator(r"^[A-Za-z0-9._]{3,32}$", "3 to 32 letters, digits, dots or underscores.")


def _token() -> str:
    return secrets.token_urlsafe(18)


class AffiliateProfile(models.Model):
    """A person as an affiliate: one profile per account, and per person."""

    APPLIED, ACTIVE, PAUSED, BLOCKED, BANNED, CLOSED = "applied", "active", "paused", "blocked", "banned", "closed"
    STATES = [(APPLIED, "Waiting for the review"), (ACTIVE, "Active"), (PAUSED, "Paused"), (BLOCKED, "Blocked"),
              (BANNED, "Banned"), (CLOSED, "Closed")]
    DOCUMENTS = [("passport", "Passport"), ("id_card", "Identity card"), ("other", "Another official document")]

    user = models.OneToOneField(settings.AUTH_USER_MODEL, on_delete=models.PROTECT, related_name="affiliate")
    handle = models.CharField(max_length=32, unique=True, validators=[HANDLE_VALIDATOR],
                              help_text="Your public name as an affiliate; your links carry it. 3 to 32 letters, digits, dots or underscores.")
    state = models.CharField(max_length=16, choices=STATES, default=APPLIED, db_index=True)
    state_before = models.CharField(max_length=16, blank=True, help_text="What it was before the account was closed or the profile blocked.")
    website = models.URLField(blank=True, help_text="Where you publish, if it is a website.")
    channels = models.TextField(blank=True, max_length=1000, help_text="Where and how you will advertise: a blog, a channel, a newsletter ...")
    legal_name = models.CharField(max_length=200, blank=True, help_text="The name your payouts go to: yours, or your company's.")
    vat_id = models.CharField("VAT id", max_length=32, blank=True, help_text="If you have one.")
    payout_details = models.TextField(blank=True, max_length=500, help_text="Where your commission is paid: the account holder and IBAN, or a Skrill address.")
    terms_accepted_at = models.DateTimeField(null=True, blank=True)
    # The phone number, proven by a texted code (texts.py) or confirmed by
    # the operator at the review where the portal cannot send a text.
    phone = models.CharField(max_length=32, blank=True, help_text="With the country's code: +49 170 1234567.")
    phone_verified_at = models.DateTimeField(null=True, blank=True)
    phone_code_hash = models.CharField(max_length=128, blank=True)
    phone_code_expires = models.DateTimeField(null=True, blank=True)
    phone_code_tries = models.PositiveSmallIntegerField(default=0)
    # The identity check's result. No picture of a document is kept: the
    # operator looks at it and writes down what it says.
    id_name = models.CharField("name on the document", max_length=200, blank=True)
    id_birth_date = models.DateField("date of birth", null=True, blank=True)
    id_document = models.CharField("document", max_length=16, choices=DOCUMENTS, blank=True)
    id_country = models.CharField("issuing country", max_length=2, blank=True)
    id_expires = models.DateField("valid until", null=True, blank=True)
    id_checked_at = models.DateTimeField(null=True, blank=True)
    id_checked_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    # Whoever brought this affiliate in earns a share of each commission, on
    # top of it. Set once, when the profile is made; null: nobody.
    superaffiliate = models.ForeignKey("self", null=True, blank=True, on_delete=models.SET_NULL, related_name="recruits")
    super_percent = models.DecimalField(max_digits=5, decimal_places=2, null=True, blank=True,
                                        validators=[MinValueValidator(0), MaxValueValidator(50)],
                                        help_text="The superaffiliate's share of this affiliate's commissions; empty: the programme's.")
    created = models.DateTimeField(auto_now_add=True)
    activated_at = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ["handle"]

    def __str__(self) -> str:
        return self.handle

    @property
    def active(self) -> bool:
        return self.state == self.ACTIVE

    @property
    def identified(self) -> bool:
        return bool(self.phone_verified_at and self.id_checked_at)


class Boost(models.Model):
    """What a merchant adds to the programme's base commission, to have
    something sold: a percentage of the net price, taken from the
    merchant's own proceeds. The most specific one in force counts."""

    ALL, CATEGORY, APP = "all", "category", "app"
    SCOPES = [(APP, "One app"), (CATEGORY, "A category"), (ALL, "Everything we sell")]

    publisher = models.ForeignKey(Publisher, on_delete=models.CASCADE, related_name="boosts")
    scope = models.CharField(max_length=16, choices=SCOPES, default=APP)
    app = models.ForeignKey(App, null=True, blank=True, on_delete=models.CASCADE, related_name="boosts")
    category = models.CharField(max_length=32, choices=constants.CATEGORIES, blank=True)
    percent = models.DecimalField(max_digits=5, decimal_places=2)
    renewals = models.BooleanField("also on a subscription's renewals", default=False)
    valid_from = models.DateField(null=True, blank=True)
    valid_until = models.DateField(null=True, blank=True)
    saved_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        what = self.app.name if self.app_id else (self.get_category_display() if self.scope == self.CATEGORY else "everything")
        return f"{self.publisher}: +{self.percent} % on {what}"


class Click(models.Model):
    """An affiliate's link, followed: to a product, or to the shop. Kept
    for the days a link counts, then gone but for the day's count."""

    affiliate = models.ForeignKey(AffiliateProfile, on_delete=models.CASCADE, related_name="clicks")
    app = models.ForeignKey(App, null=True, blank=True, on_delete=models.CASCADE, related_name="+")
    label = models.CharField(max_length=32, blank=True)
    token = models.CharField(max_length=40, unique=True, default=_token)
    at = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        ordering = ["-id"]


class ClickDay(models.Model):
    """The clicks of a day, counted: what is left of them after their time."""

    affiliate = models.ForeignKey(AffiliateProfile, on_delete=models.CASCADE, related_name="click_days")
    app = models.ForeignKey(App, null=True, blank=True, on_delete=models.CASCADE, related_name="+")
    label = models.CharField(max_length=32, blank=True)
    day = models.DateField(db_index=True)
    clicks = models.PositiveIntegerField(default=0)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["affiliate", "app", "label", "day"], name="one_count_per_link_and_day")]


class Attribution(models.Model):
    """An order that followed an affiliate's link to its product."""

    order = models.OneToOneField(Order, on_delete=models.CASCADE, related_name="affiliate_attribution")
    affiliate = models.ForeignKey(AffiliateProfile, on_delete=models.PROTECT, related_name="attributions")
    label = models.CharField(max_length=32, blank=True)
    clicked_at = models.DateTimeField()
    created = models.DateTimeField(auto_now_add=True)


class AffiliatePayout(models.Model):
    """What the store paid an affiliate, in one currency. The portal moves
    no money: the operator pays and writes the transfer's reference."""

    affiliate = models.ForeignKey(AffiliateProfile, on_delete=models.PROTECT, related_name="payouts")
    currency = models.CharField(max_length=3)
    amount = models.DecimalField(max_digits=11, decimal_places=2)
    details = models.TextField(blank=True)
    reference = models.CharField(max_length=120)
    note = models.CharField(max_length=300, blank=True)
    paid_at = models.DateTimeField(auto_now_add=True)
    paid_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")

    class Meta:
        ordering = ["-id"]


class Commission(models.Model):
    """One line of an affiliate's earnings, for one payment: what a sale or
    a renewal earned, or what a refund or a chargeback took back (a line
    below zero). A superaffiliate's share is a line of its own, naming the
    line it is a share of."""

    PRODUCT_LINK, SUPER = "product_link", "super"
    BASES = [(PRODUCT_LINK, "Product link"), (SUPER, "Superaffiliate's share")]
    PENDING, APPROVED, REVERSAL = "pending", "approved", "reversal"
    STATES = [(PENDING, "Pending"), (APPROVED, "Approved"), (REVERSAL, "Given back")]

    affiliate = models.ForeignKey(AffiliateProfile, on_delete=models.PROTECT, related_name="commissions")
    payment = models.ForeignKey(Payment, on_delete=models.PROTECT, related_name="commissions")
    order = models.ForeignKey(Order, on_delete=models.PROTECT, related_name="commissions")
    basis = models.CharField(max_length=16, choices=BASES)
    share_of = models.ForeignKey("self", null=True, blank=True, on_delete=models.PROTECT, related_name="shares")
    label = models.CharField(max_length=32, blank=True)
    # What it was reckoned on and with, as it stood: the net amount, the
    # programme's base and the merchant's boost in percent, and who pays what.
    net = models.DecimalField(max_digits=9, decimal_places=2)
    base_percent = models.DecimalField(max_digits=5, decimal_places=2, default=0)
    boost_percent = models.DecimalField(max_digits=5, decimal_places=2, default=0)
    operator_part = models.DecimalField(max_digits=9, decimal_places=2, default=0)
    merchant_part = models.DecimalField(max_digits=9, decimal_places=2, default=0)
    amount = models.DecimalField(max_digits=9, decimal_places=2)
    currency = models.CharField(max_length=3)
    state = models.CharField(max_length=16, choices=STATES, default=PENDING, db_index=True)
    approve_after = models.DateField(null=True, blank=True)
    payout = models.ForeignKey(AffiliatePayout, null=True, blank=True, on_delete=models.PROTECT, related_name="lines")
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-id"]
        constraints = [models.UniqueConstraint(fields=["payment", "affiliate", "basis"], name="one_line_per_payment_affiliate_and_basis")]

    @property
    def percent_text(self) -> str:
        return format((self.base_percent + self.boost_percent).normalize(), "f")


class Sanction(models.Model):
    """A block, a ban, or a block lifted: one row each, never edited. What
    the operator keeps, and what the affiliate is given on request."""

    BLOCK, BAN, LIFT = "block", "ban", "lift"
    KINDS = [(BLOCK, "Blocked"), (BAN, "Banned"), (LIFT, "Block lifted")]

    affiliate = models.ForeignKey(AffiliateProfile, on_delete=models.PROTECT, related_name="sanctions")
    kind = models.CharField(max_length=8, choices=KINDS)
    rule = models.CharField(max_length=200, help_text="The clause of the affiliate terms.")
    facts = models.TextField(help_text="What happened, when, with which merchant's products.")
    evidence = models.TextField(blank=True, help_text="What the decision rests on. Kept; shown to the affiliate except where it names somebody else.")
    until = models.DateField(null=True, blank=True, help_text="A block's last day; empty: until it is lifted.")
    money = models.TextField(blank=True, help_text="Which commissions are taken back, and what is paid.")
    statement = models.TextField(blank=True, help_text="What the affiliate said, an appeal and its outcome.")
    decided_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    at = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-id"]
