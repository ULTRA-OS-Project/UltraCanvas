"""What the store owes a publisher, and paying it.

Every payment carries the publisher's share (split.py). A **payout**
gathers the shares that are due, per publisher and currency:

- a sale or a renewal is due PAYOUT_HOLD_DAYS (30) after it was paid: the
  time in which it is most likely to be given back;
- a refund or a chargeback is due at once, and takes its share off;
- a sum under PAYOUT_MINIMUM, or under nothing, makes no payout: its
  payments stay as they are and count the next time, so a refund after a
  payout comes off the one that follows.

A payment is in one payout and no more. **The portal moves no money.** A
payout says what to pay and where (the publisher's details as they stood);
the operator pays it at the bank or the provider and marks it paid with the
transfer's reference. Paying through the provider's own interface, and the
tax document a payout is to the publisher (a self-billed credit note), are
not built.

Version: 0.9.0
"""

from __future__ import annotations

import datetime as dt
from decimal import Decimal

from django.conf import settings
from django.db import transaction
from django.utils import timezone

from ..store.services import audit
from .models import Payment, Payout

ZERO = Decimal("0.00")


class PayoutError(Exception):
    pass


def default_until(today: dt.date | None = None) -> dt.date:
    """The last day of the month before this one."""
    today = today or timezone.localdate()
    return today.replace(day=1) - dt.timedelta(days=1)


def _due(until: dt.date):
    """The payments no payout has taken yet that are due on `until`."""
    held = until - dt.timedelta(days=settings.PAYOUT_HOLD_DAYS)
    for payment in Payment.objects.filter(payout__isnull=True, at__date__lte=until).select_related("order__app__publisher").order_by("id"):
        if payment.is_reversal or timezone.localtime(payment.at).date() <= held:
            yield payment


def owed(publisher) -> list[dict]:
    """Per currency: what no payout has taken yet, due or still waiting."""
    rows: dict[str, dict] = {}
    for payment in Payment.objects.filter(payout__isnull=True, order__app__publisher=publisher):
        row = rows.setdefault(payment.currency, {"currency": payment.currency, "amount": ZERO, "payments": 0})
        row["amount"] += payment.signed_publisher_share
        row["payments"] += 1
    return [rows[currency] for currency in sorted(rows)]


@transaction.atomic
def build(until: dt.date | None = None, actor=None) -> list[Payout]:
    """The payouts for everything due on `until`. Safe to run again: what a
    payout took is taken."""
    until = until or default_until()
    if until > timezone.localdate():
        raise PayoutError("A payout cannot be made for days that have not come")
    groups: dict[tuple[int, str], list[Payment]] = {}
    for payment in _due(until):
        groups.setdefault((payment.order.app.publisher_id, payment.currency), []).append(payment)
    made = []
    for (_, currency), payments in sorted(groups.items()):
        amount = sum((p.signed_publisher_share for p in payments), ZERO)
        if amount < settings.PAYOUT_MINIMUM:
            continue   # carried: these payments count the next time
        publisher = payments[0].order.app.publisher
        payout = Payout.objects.create(publisher=publisher, currency=currency, until=until, amount=amount,
                                       details=publisher.payout_details, created_by=actor if getattr(actor, "pk", None) else None)
        Payment.objects.filter(pk__in=[p.pk for p in payments]).update(payout=payout)
        audit(actor, "payout.made", "payout", str(payout.pk), publisher=publisher.slug, amount=str(amount), currency=currency,
              until=until.isoformat(), payments=len(payments))
        made.append(payout)
    return made


@transaction.atomic
def mark_paid(payout: Payout, actor, reference: str, note: str = "") -> Payout:
    payout = Payout.objects.select_for_update().get(pk=payout.pk)
    if payout.state == Payout.PAID:
        raise PayoutError("This payout is marked paid already")
    reference = (reference or "").strip()
    if not reference:
        raise PayoutError("Give the transfer's reference: it is what proves the payout was paid")
    payout.state = Payout.PAID
    payout.reference = reference[:120]
    payout.note = (note or "").strip()[:300]
    payout.paid_at = timezone.now()
    payout.paid_by = actor if getattr(actor, "pk", None) else None
    payout.save()
    audit(actor, "payout.paid", "payout", str(payout.pk), publisher=payout.publisher.slug, amount=str(payout.amount),
          currency=payout.currency, reference=payout.reference)
    return payout


def lines(payout: Payout) -> dict:
    """A payout as its statement shows it: the payments, and the sums."""
    payments = list(payout.payments.select_related("order__app").order_by("id"))
    return {"payments": payments,
            "gross": sum((p.signed_amount for p in payments), ZERO), "vat": sum((p.signed_vat_amount for p in payments), ZERO),
            "fee": sum((p.signed_store_fee for p in payments), ZERO), "share": sum((p.signed_publisher_share for p in payments), ZERO)}
