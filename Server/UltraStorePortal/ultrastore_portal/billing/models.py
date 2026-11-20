"""A purchase: the order a store or a visitor opened, and every movement of
money on it - the sale, a subscription's renewals, refunds, a chargeback.

Version: 0.1.0
"""

from __future__ import annotations

import secrets
import uuid

from django.db import models

from ..store.models import App


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
    receipt_no = models.PositiveBigIntegerField(null=True, blank=True, unique=True)
    note = models.CharField(max_length=300, blank=True)
    at = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        ordering = ["id"]

    def __str__(self) -> str:
        return f"{self.kind} {self.amount} {self.currency}"

    @property
    def signed_amount(self):
        return self.amount if self.kind in (self.SALE, self.RENEWAL) else -self.amount
