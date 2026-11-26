"""The split of a payment between the store and the publisher.

The store keeps a fee - a percentage of what the buyer paid
(ULTRASTORE_PORTAL_STORE_FEE_PERCENT, 15) - and the rest is the
publisher's share. The split is written on the payment when the money
moves and never reckoned again, so a fee changed later leaves what was sold
before as it was.

A refund or a chargeback gives the fee back in the proportion of the money
given back: half of a payment refunded returns half of its fee, and the
last cent of a payment returns whatever of its fee is left, so a payment
refunded in parts nets to nothing on both sides.

What is not here: VAT (the fee is reckoned on the amount paid until the
portal knows the tax in it), the payment provider's fee, invoices and
payouts.

Version: 0.7.0
"""

from __future__ import annotations

from decimal import ROUND_HALF_UP, Decimal

from django.conf import settings

CENT = Decimal("0.01")
ZERO = Decimal("0.00")


def fee_percent() -> Decimal:
    return settings.STORE_FEE_PERCENT


def percent_text(percent: Decimal) -> str:
    """A percentage as a person reads it: 15, 12.5 - never 15.00 or 1E+1."""
    return format(percent.normalize(), "f")


def fee_of(amount: Decimal, percent: Decimal) -> Decimal:
    """`percent` of `amount`, to the cent, half up."""
    return (amount * percent / 100).quantize(CENT, rounding=ROUND_HALF_UP)


def sale_split(amount: Decimal) -> dict:
    """The fields of a sale's or a renewal's payment: today's fee."""
    percent = fee_percent()
    return {"fee_percent": percent, "store_fee": fee_of(amount, percent)}


def reversal_fee(original_amount: Decimal, original_fee: Decimal, amount: Decimal, amount_before: Decimal, fee_before: Decimal) -> Decimal:
    """The fee given back with `amount` of a payment of `original_amount`
    that carried `original_fee`, when `amount_before` of it and
    `fee_before` of its fee were given back already."""
    left = max(original_fee - fee_before, ZERO)
    if original_amount <= 0 or amount_before + amount >= original_amount:
        return left
    return min((original_fee * amount / original_amount).quantize(CENT, rounding=ROUND_HALF_UP), left)


def reversal_split(order, reverses: str, amount: Decimal) -> dict:
    """The fields of a refund's or a chargeback's payment: the fee of the
    payment it takes back (`reverses`, the provider's transaction; the
    order's latest payment when that names none), in proportion."""
    from .models import Payment

    paid = [p for p in order.payments.all() if p.kind in (Payment.SALE, Payment.RENEWAL)]
    original = next((p for p in paid if reverses and p.provider_transaction == reverses), paid[-1] if paid else None)
    if original is None:
        percent = fee_percent()
        return {"fee_percent": percent, "store_fee": fee_of(amount, percent)}
    earlier = [p for p in order.payments.all()
               if p.kind in (Payment.REFUND, Payment.CHARGEBACK) and original.provider_transaction and p.reverses == original.provider_transaction]
    fee = reversal_fee(original.amount, original.store_fee, amount,
                       sum((p.amount for p in earlier), ZERO), sum((p.store_fee for p in earlier), ZERO))
    return {"fee_percent": original.fee_percent, "store_fee": fee}
