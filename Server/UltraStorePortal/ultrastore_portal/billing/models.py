"""A purchase: the order a store or a visitor opened, and every movement of
money on it - the sale, a subscription's renewals, refunds, a chargeback.

Version: 0.9.0 - the buyer's country on an order; a payment's VAT and the
                 payout it was paid in; VatRate, Invoice, Payout
Version: 0.7.0 - a payment carries its split: the store's fee, and the
                 publisher's share (split.py)
Version: 0.1.0
"""

from __future__ import annotations

import secrets
import uuid

from django.db import models

from django.conf import settings

from ..store.models import App, Publisher


def _order_id() -> str:
    return uuid.uuid4().hex


def _order_secret() -> str:
    return secrets.token_urlsafe(24)


class Order(models.Model):
    """One purchase of one app. The id names it (to the payment provider
    too); the secret is what its buyer holds - the native store keeps it
    with the licence, a web buyer gets it in the link."""

    OPEN, PENDING, PAID, CANCELLED, FAILED, CHARGEBACK, REFUNDED = "open", "pending", "paid", "cancelled", "failed", "chargeback", "refunded"
    STATES = [(OPEN, "Open"), (PENDING, "Pending"), (PAID, "Paid"), (CANCELLED, "Cancelled"), (FAILED, "Failed"),
              (CHARGEBACK, "Chargeback"), (REFUNDED, "Refunded")]

    public_id = models.CharField(max_length=32, unique=True, default=_order_id, editable=False)
    secret = models.CharField(max_length=64, default=_order_secret, editable=False)
    app = models.ForeignKey(App, on_delete=models.PROTECT, related_name="orders")
    app_name = models.CharField(max_length=60)
    amount = models.DecimalField(max_digits=9, decimal_places=2, help_text="What the buyer pays (a subscription's first payment).")
    currency = models.CharField(max_length=3)
    list_price = models.DecimalField(max_digits=9, decimal_places=2, help_text="The catalog's price; a subscription's renewal amount.")
    code = models.CharField(max_length=40, blank=True)
    code_applied = models.BooleanField(default=False)
    status = models.CharField(max_length=16, choices=STATES, default=OPEN, db_index=True)
    source = models.CharField(max_length=8, default="store", help_text="store: the UltraStore app; web: the app's page.")
    language = models.CharField(max_length=8, blank=True)
    country = models.CharField(max_length=2, blank=True, help_text="Where the buyer is, ISO 3166-1 (XX: outside the EU); it decides the VAT rate. Empty: the seller's country.")
    provider = models.CharField(max_length=16)
    provider_transaction = models.CharField(max_length=64, blank=True)
    payer_email = models.EmailField(blank=True)
    paid_at = models.DateTimeField(null=True, blank=True, db_index=True)
    licence_id = models.CharField(max_length=32, blank=True, db_index=True)
    licence = models.TextField(blank=True)
    receipt_no = models.PositiveBigIntegerField(null=True, blank=True)
    # A subscription: the pricing model's cycle, the day the paid period
    # ends, the day the licence carries (that plus the grace days).
    cycle = models.CharField(max_length=8, blank=True)
    paid_until = models.DateField(null=True, blank=True)
    valid_until = models.DateField(null=True, blank=True)
    rec_payment_id = models.CharField(max_length=64, blank=True)
    subscription_status = models.CharField(max_length=16, blank=True)
    # What the purchase includes, as the listing had it when the order was paid.
    updates = models.CharField(max_length=8, blank=True)
    updates_until = models.DateField(null=True, blank=True)
    revoked_at = models.DateTimeField(null=True, blank=True)
    revoked_reason = models.CharField(max_length=32, blank=True)
    refunded = models.DecimalField(max_digits=9, decimal_places=2, default=0)
    notes = models.TextField(blank=True)
    created = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        ordering = ["-created"]

    def __str__(self) -> str:
        return f"{self.app_name} {self.amount} {self.currency} ({self.status})"

    @property
    def revoked(self) -> bool:
        return self.revoked_at is not None

    @property
    def is_paid(self) -> bool:
        return self.status == self.PAID

    def note(self, text: str) -> None:
        self.notes = (self.notes + "\n" if self.notes else "") + text


class Payment(models.Model):
    """One movement of money on an order."""

    SALE, RENEWAL, REFUND, CHARGEBACK = "sale", "renewal", "refund", "chargeback"
    KINDS = [(SALE, "Sale"), (RENEWAL, "Renewal"), (REFUND, "Refund"), (CHARGEBACK, "Chargeback")]

    order = models.ForeignKey(Order, on_delete=models.CASCADE, related_name="payments")
    kind = models.CharField(max_length=16, choices=KINDS)
    amount = models.DecimalField(max_digits=9, decimal_places=2)
    currency = models.CharField(max_length=3)
    provider_transaction = models.CharField(max_length=64, blank=True, db_index=True)
    reverses = models.CharField(max_length=64, blank=True, help_text="The provider's transaction a refund or a chargeback takes back.")
    # The split, written when the money moves (split.py): the store's fee of
    # this amount at the percentage in force then; the rest is the publisher's.
    fee_percent = models.DecimalField(max_digits=5, decimal_places=2, default=0, help_text="The store's fee when the money moved, in percent.")
    store_fee = models.DecimalField(max_digits=9, decimal_places=2, default=0, help_text="The store's part of the amount; a reversal gives it back.")
    # The tax in the amount (tax.py): the buyer's country and its rate when
    # the money moved. The price includes it; the fee is reckoned on what is
    # left. All zero while the store's tax details are not configured.
    country = models.CharField(max_length=2, blank=True)
    vat_percent = models.DecimalField(max_digits=5, decimal_places=2, default=0)
    vat_amount = models.DecimalField(max_digits=9, decimal_places=2, default=0)
    vat_note = models.CharField(max_length=120, blank=True)
    # The payout the publisher's share of this payment was paid in (payouts.py).
    payout = models.ForeignKey("Payout", null=True, blank=True, on_delete=models.PROTECT, related_name="payments")
    receipt_no = models.PositiveBigIntegerField(null=True, blank=True, unique=True)
    note = models.CharField(max_length=300, blank=True)
    at = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        ordering = ["id"]

    def __str__(self) -> str:
        return f"{self.kind} {self.amount} {self.currency}"

    @property
    def is_reversal(self) -> bool:
        return self.kind not in (self.SALE, self.RENEWAL)

    @property
    def signed_amount(self):
        return -self.amount if self.is_reversal else self.amount

    @property
    def fee_percent_text(self) -> str:
        return format(self.fee_percent.normalize(), "f")

    @property
    def net_amount(self):
        """The amount without the tax in it."""
        return self.amount - self.vat_amount

    @property
    def publisher_share(self):
        return self.amount - self.vat_amount - self.store_fee

    @property
    def signed_vat_amount(self):
        return -self.vat_amount if self.is_reversal else self.vat_amount

    @property
    def signed_net_amount(self):
        return -self.net_amount if self.is_reversal else self.net_amount

    @property
    def vat_percent_text(self) -> str:
        return format(self.vat_percent.normalize(), "f")

    @property
    def signed_store_fee(self):
        return -self.store_fee if self.is_reversal else self.store_fee

    @property
    def signed_publisher_share(self):
        return -self.publisher_share if self.is_reversal else self.publisher_share


class VatRate(models.Model):
    """The standard VAT rate of a country from a day on (tax.py). The
    operator's data: entered and corrected on the operator's tax page."""

    country = models.CharField(max_length=2, help_text="ISO 3166-1, two letters (Greece is GR).")
    percent = models.DecimalField(max_digits=5, decimal_places=2)
    valid_from = models.DateField()
    note = models.CharField(max_length=200, blank=True)

    class Meta:
        ordering = ["country", "-valid_from"]
        constraints = [models.UniqueConstraint(fields=["country", "valid_from"], name="one_rate_per_country_and_day")]

    def __str__(self) -> str:
        return f"{self.country} {self.percent} % from {self.valid_from}"


class Invoice(models.Model):
    """A tax invoice for a payment, or the credit note for a refund or a
    chargeback: numbered in one series without gaps, and a copy of
    everything on it as it stood - an invoice is never reckoned again."""

    INVOICE, CREDIT_NOTE = "invoice", "credit_note"
    KINDS = [(INVOICE, "Invoice"), (CREDIT_NOTE, "Credit note")]

    number = models.CharField(max_length=32, unique=True)
    kind = models.CharField(max_length=16, choices=KINDS)
    payment = models.OneToOneField(Payment, on_delete=models.PROTECT, related_name="invoice")
    order = models.ForeignKey(Order, on_delete=models.PROTECT, related_name="invoices")
    issued = models.DateField()
    corrects = models.CharField(max_length=32, blank=True, help_text="The invoice a credit note takes back, by its number.")
    seller_name = models.CharField(max_length=200)
    seller_address = models.TextField()
    seller_vat_id = models.CharField(max_length=32)
    seller_country = models.CharField(max_length=2)
    buyer_email = models.EmailField(blank=True)
    buyer_country = models.CharField(max_length=2, blank=True)
    description = models.CharField(max_length=200)
    net = models.DecimalField(max_digits=9, decimal_places=2)
    vat_percent = models.DecimalField(max_digits=5, decimal_places=2)
    vat = models.DecimalField(max_digits=9, decimal_places=2)
    gross = models.DecimalField(max_digits=9, decimal_places=2)
    currency = models.CharField(max_length=3)
    note = models.CharField(max_length=300, blank=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return f"{self.get_kind_display()} {self.number}"

    @property
    def vat_percent_text(self) -> str:
        return format(self.vat_percent.normalize(), "f")


class Payout(models.Model):
    """What the store owes a publisher for the payments up to a day, in one
    currency, and whether it was paid (payouts.py). The portal moves no
    money: the operator pays, and says here that it is done."""

    OPEN, PAID = "open", "paid"
    STATES = [(OPEN, "To pay"), (PAID, "Paid")]

    publisher = models.ForeignKey(Publisher, on_delete=models.PROTECT, related_name="payouts")
    currency = models.CharField(max_length=3)
    until = models.DateField(help_text="The last day whose payments are in it.")
    amount = models.DecimalField(max_digits=11, decimal_places=2)
    state = models.CharField(max_length=8, choices=STATES, default=OPEN, db_index=True)
    details = models.TextField(blank=True, help_text="Where the publisher is paid, as it stood when the payout was made.")
    reference = models.CharField(max_length=120, blank=True, help_text="The bank's or the provider's reference of the transfer.")
    note = models.CharField(max_length=300, blank=True)
    created = models.DateTimeField(auto_now_add=True)
    created_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    paid_at = models.DateTimeField(null=True, blank=True)
    paid_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return f"{self.publisher} {self.amount} {self.currency} until {self.until}"
