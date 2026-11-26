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

Since 0.9.0 the tax comes out first (tax.py): the price includes VAT, the
fee is reckoned on what is left - the net - and the publisher's share is
the net less the fee. A reversal returns the tax in the same proportion as
the fee. While the store's tax details are not configured there is no tax
in a payment and the fee is reckoned on the amount paid, as in 0.7.0.

What is not here: the payment provider's fee.

Version: 0.9.0 - the tax first, the fee on the net
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


def sale_split(order, amount: Decimal) -> dict:
    """The fields of a sale's or a renewal's payment: the tax of the
    buyer's country today, and today's fee on what is left."""
    from . import tax

    taxed = tax.sale_tax(order, amount)
    percent = fee_percent()
    return {**taxed, "fee_percent": percent, "store_fee": fee_of(amount - taxed["vat_amount"], percent)}


def reversal_fee(original_amount: Decimal, original_fee: Decimal, amount: Decimal, amount_before: Decimal, fee_before: Decimal) -> Decimal:
    """The fee given back with `amount` of a payment of `original_amount`
    that carried `original_fee`, when `amount_before` of it and
    `fee_before` of its fee were given back already."""
    left = max(original_fee - fee_before, ZERO)
    if original_amount <= 0 or amount_before + amount >= original_amount:
        return left
    return min((original_fee * amount / original_amount).quantize(CENT, rounding=ROUND_HALF_UP), left)


def reversal_split(order, reverses: str, amount: Decimal) -> dict:
    """The fields of a refund's or a chargeback's payment: the tax and the
    fee of the payment it takes back (`reverses`, the provider's
    transaction; the order's latest payment when that names none), each in
    proportion."""
    from .models import Payment

    paid = [p for p in order.payments.all() if p.kind in (Payment.SALE, Payment.RENEWAL)]
    original = next((p for p in paid if reverses and p.provider_transaction == reverses), paid[-1] if paid else None)
    if original is None:
        return sale_split(order, amount)
    earlier = [p for p in order.payments.all()
               if p.kind in (Payment.REFUND, Payment.CHARGEBACK) and original.provider_transaction and p.reverses == original.provider_transaction]
    before = sum((p.amount for p in earlier), ZERO)
    fee = reversal_fee(original.amount, original.store_fee, amount, before, sum((p.store_fee for p in earlier), ZERO))
    vat = reversal_fee(original.amount, original.vat_amount, amount, before, sum((p.vat_amount for p in earlier), ZERO))
    return {"fee_percent": original.fee_percent, "store_fee": fee, "country": original.country, "vat_percent": original.vat_percent,
            "vat_amount": vat, "vat_note": original.vat_note}
