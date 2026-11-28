"""The tax in a sale: VAT by the buyer's country, and the invoice.

The store is the seller (the portal's *Decisions*: "Who sells? The store"),
so it owes the tax and writes the invoice.

- **The price includes the tax.** A buyer pays the listed price wherever
  they are, as a consumer price has to be shown; the tax is taken out of
  it: of 119.00 at 19 %, 19.00 is tax and 100.00 is net. The store's fee
  and the publisher's share are reckoned on the net (split.py).
- **The rate is the buyer's country's** on the day the money moves: the
  rule for digital services sold to consumers in the EU (the one-stop
  shop). The country comes with the order - the buy page asks, a client
  sends it - and is the seller's when none came. The rates are the
  operator's data (`VatRate`), entered on the operator's tax page; `tax
  seed` loads a starting table to check. An EU country with no rate is
  charged the seller's, and says so on the payment, so the tax page can
  list it. A country outside the EU is charged no EU tax.
- **An invoice per payment**, a credit note per refund and chargeback, in
  one numbered series without gaps (a counter per year, counted under a
  lock), each a copy of what it says as it stood.

Nothing here runs until the seller's details are configured
(SELLER_NAME, SELLER_ADDRESS, SELLER_VAT_ID, SELLER_COUNTRY): without them
a payment carries no tax and no invoice, as before 0.9.0.

Not here, and for the tax adviser before the first sale: a business buyer's
VAT id and the reverse charge, the two pieces of evidence the one-stop shop
asks for a buyer's country, the taxes of countries outside the EU, and
reduced rates (e-books in some countries).

Version: 0.9.0
"""

from __future__ import annotations

import datetime as dt
from decimal import ROUND_HALF_UP, Decimal

from django.conf import settings
from django.utils import timezone

from ..store.models import Setting
from .models import Invoice, Payment, VatRate

CENT = Decimal("0.01")
ZERO = Decimal("0.00")
OUTSIDE = "XX"   # the buy page's "outside the European Union"

# The member states, by ISO 3166-1 code and the name the buy page shows.
EU = {"AT": "Austria", "BE": "Belgium", "BG": "Bulgaria", "HR": "Croatia", "CY": "Cyprus", "CZ": "Czechia", "DK": "Denmark",
      "EE": "Estonia", "FI": "Finland", "FR": "France", "DE": "Germany", "GR": "Greece", "HU": "Hungary", "IE": "Ireland",
      "IT": "Italy", "LV": "Latvia", "LT": "Lithuania", "LU": "Luxembourg", "MT": "Malta", "NL": "Netherlands", "PL": "Poland",
      "PT": "Portugal", "RO": "Romania", "SK": "Slovakia", "SI": "Slovenia", "ES": "Spain", "SE": "Sweden"}

# A starting table for `tax seed`: the standard rates as the author knew
# them on 2026-11-26. Sources disagreed that day on Estonia and Romania;
# every row is to be checked against the Commission's table before selling.
STARTING_RATES = {"AT": "20", "BE": "21", "BG": "20", "HR": "25", "CY": "19", "CZ": "21", "DK": "25", "EE": "24", "FI": "25.5",
                  "FR": "20", "DE": "19", "GR": "24", "HU": "27", "IE": "23", "IT": "22", "LV": "21", "LT": "21", "LU": "17",
                  "MT": "18", "NL": "21", "PL": "23", "PT": "23", "RO": "21", "SK": "23", "SI": "22", "ES": "21", "SE": "25"}

SELLER_SETTINGS = ("SELLER_NAME", "SELLER_ADDRESS", "SELLER_VAT_ID", "SELLER_COUNTRY")


def missing_settings() -> list[str]:
    """What keeps the store from reckoning tax and writing invoices."""
    return ["ULTRASTORE_PORTAL_" + name for name in SELLER_SETTINGS if not getattr(settings, name)]


def configured() -> bool:
    return not missing_settings()


def countries() -> list[tuple[str, str]]:
    """The buy page's choices: the member states by name, then the rest."""
    return sorted(EU.items(), key=lambda item: item[1]) + [(OUTSIDE, "Outside the European Union")]


def clean_country(value: str) -> str:
    """A country as an order keeps it: two letters, or nothing."""
    value = (value or "").strip().upper()
    if value == "EL":   # the VAT prefix of Greece
        value = "GR"
    return value if len(value) == 2 and value.isalpha() else ""


def rate_for(country: str, day: dt.date) -> tuple[Decimal, str]:
    """The rate for a buyer in `country` on `day`, and a note when it is
    not simply that country's rate."""
    seller = settings.SELLER_COUNTRY
    country = country or seller
    if country not in EU:
        return ZERO, "outside the EU: no EU VAT"
    row = VatRate.objects.filter(country=country, valid_from__lte=day).order_by("-valid_from").first()
    if row is not None:
        return row.percent, ""
    fallback = VatRate.objects.filter(country=seller, valid_from__lte=day).order_by("-valid_from").first()
    if fallback is not None and country != seller:
        return fallback.percent, f"no rate for {country}: the seller's ({seller})"
    return ZERO, f"no rate for {country}"


def vat_in(gross: Decimal, percent: Decimal) -> Decimal:
    """The tax inside a price that includes it, to the cent, half up."""
    if percent <= 0:
        return ZERO
    return (gross * percent / (100 + percent)).quantize(CENT, rounding=ROUND_HALF_UP)


def sale_tax(order, amount: Decimal, day: dt.date | None = None) -> dict:
    """The tax fields of a sale's or a renewal's payment."""
    if not configured():
        return {"country": "", "vat_percent": ZERO, "vat_amount": ZERO, "vat_note": ""}
    country = order.country or settings.SELLER_COUNTRY
    percent, note = rate_for(country, day or timezone.localdate())
    return {"country": country, "vat_percent": percent, "vat_amount": vat_in(amount, percent), "vat_note": note}


def problems() -> list[str]:
    """What the operator's tax page lists to put right."""
    found = [f"{name} is not set" for name in missing_settings()]
    if configured():
        today = timezone.localdate()
        without = [code for code in sorted(EU) if not VatRate.objects.filter(country=code, valid_from__lte=today).exists()]
        if without:
            found.append("No VAT rate for: " + ", ".join(without))
        noted = Payment.objects.exclude(vat_note="").exclude(vat_note__startswith="outside").count()
        if noted:
            found.append(f"{noted} payment(s) were taxed without their own country's rate (the payment's note says how)")
    return found


def seed(day: dt.date | None = None) -> int:
    """The starting table, for the countries that have no rate yet."""
    day = day or dt.date(2026, 1, 1)
    made = 0
    for country, percent in STARTING_RATES.items():
        if not VatRate.objects.filter(country=country).exists():
            VatRate.objects.create(country=country, percent=Decimal(percent), valid_from=day, note="starting table, to be checked")
            made += 1
    return made


def next_invoice_number(day: dt.date) -> str:
    """The next number of the year's series, counted under a lock so two
    payments at once never share one and none is skipped. Call inside a
    transaction."""
    key = f"invoice_no:{day.year}"
    Setting.objects.get_or_create(key=key, defaults={"value": "0"})
    row = Setting.objects.select_for_update().get(key=key)
    number = int(row.value or "0") + 1
    row.value = str(number)
    row.save(update_fields=["value"])
    return f"{settings.INVOICE_PREFIX}{day.year}-{number:06d}"


def issue_invoice(payment: Payment) -> Invoice | None:
    """The invoice of a sale or a renewal, the credit note of a refund or a
    chargeback. Nothing while the seller's details are not configured, and
    nothing twice."""
    if not configured() or Invoice.objects.filter(payment=payment).exists():
        return None
    order = payment.order
    day = timezone.localdate()
    corrects = ""
    if payment.is_reversal:
        paid = [p for p in order.payments.all() if not p.is_reversal]
        original = next((p for p in paid if payment.reverses and p.provider_transaction == payment.reverses), paid[-1] if paid else None)
        earlier = Invoice.objects.filter(payment=original).first() if original is not None else None
        corrects = earlier.number if earlier else ""
    what = {Payment.SALE: "", Payment.RENEWAL: " - renewal", Payment.REFUND: " - refund", Payment.CHARGEBACK: " - chargeback"}[payment.kind]
    sign = -1 if payment.is_reversal else 1
    return Invoice.objects.create(
        number=next_invoice_number(day), kind=Invoice.CREDIT_NOTE if payment.is_reversal else Invoice.INVOICE,
        payment=payment, order=order, issued=day, corrects=corrects,
        seller_name=settings.SELLER_NAME, seller_address=settings.SELLER_ADDRESS, seller_vat_id=settings.SELLER_VAT_ID,
        seller_country=settings.SELLER_COUNTRY, buyer_email=order.payer_email, buyer_country=payment.country,
        description=f"{order.app_name} ({order.app.app_id}){what}"[:200],
        net=sign * payment.net_amount, vat_percent=payment.vat_percent, vat=sign * payment.vat_amount, gross=sign * payment.amount,
        currency=payment.currency, note=payment.vat_note)


def quarter_of(day: dt.date) -> tuple[int, int]:
    return day.year, (day.month - 1) // 3 + 1


def quarter_report(year: int, quarter: int) -> dict:
    """What the store owes in tax for a quarter, by the buyer's country and
    the rate: the figures of the one-stop-shop return, from the invoices."""
    first = dt.date(year, 3 * quarter - 2, 1)
    after = dt.date(year + 1, 1, 1) if quarter == 4 else dt.date(year, 3 * quarter + 1, 1)
    rows: dict[tuple[str, str, Decimal], dict] = {}
    for invoice in Invoice.objects.filter(issued__gte=first, issued__lt=after):
        key = (invoice.currency, invoice.buyer_country or "-", invoice.vat_percent)
        row = rows.setdefault(key, {"currency": key[0], "country": key[1], "country_name": EU.get(key[1], "Outside the EU" if key[1] not in EU else key[1]),
                                    "percent": format(key[2].normalize(), "f"), "invoices": 0, "net": ZERO, "vat": ZERO, "gross": ZERO})
        row["invoices"] += 1
        row["net"] += invoice.net
        row["vat"] += invoice.vat
        row["gross"] += invoice.gross
    ordered = [rows[key] for key in sorted(rows)]
    totals: dict[str, dict] = {}
    for row in ordered:
        total = totals.setdefault(row["currency"], {"currency": row["currency"], "net": ZERO, "vat": ZERO, "gross": ZERO})
        for name in ("net", "vat", "gross"):
            total[name] += row[name]
    return {"year": year, "quarter": quarter, "first": first, "last": after - dt.timedelta(days=1), "rows": ordered, "totals": list(totals.values())}
