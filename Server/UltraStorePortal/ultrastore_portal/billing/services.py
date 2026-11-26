"""Buying: an order opened, the payment provider's report believed or not,
the licence issued, a subscription renewed, money given back, and the
download link signed for a buyer.

The rules are the ones the native store was written against
(Docs/UltraStore/README.md, *Payment*; Apps/UltraStore/core/Purchase.h):

- An order is priced from the listing when it is opened. Its id names it
  to the provider; its secret is the buyer's.
- **Only the provider's signed, server-to-server report pays an order.**
  The browser coming back proves nothing. The report's signature, amount
  and currency are checked against the order; a repeat changes nothing.
- A paid order has a licence (licence.py), a receipt number, and for a
  subscription a paid period; each renewal the provider reports starts the
  next period and re-issues the licence under the same id.
- A refund is the operator's, through the provider; a chargeback is the
  provider's report. Either takes the licence back when everything paid is
  reversed (or when the operator says so).
- A paid app's packages without a trial are in the publish folder's
  restricted/; a buyer gets a link to one that the web server accepts for
  ten minutes (nginx secure_link_md5).

Two providers: Skrill Quick Checkout (skrill.py), and "test", which a
developer's machine and the tests use - the hand-off page has a button in
Skrill's place that sends the same signed report through the same code.

Version: 0.9.0 - an order keeps the buyer's country; a payment carries its
                 tax and gets its invoice or credit note (tax.py)
Version: 0.7.0 - every payment is written with its split (split.py): the
                 store's fee and the publisher's share; a refund and a
                 chargeback give the fee back in proportion
Version: 0.1.0
"""

from __future__ import annotations

import base64
import calendar
import datetime as dt
import hashlib
import hmac
import logging
import secrets
import time
from decimal import Decimal, InvalidOperation

from django.conf import settings
from django.core.mail import send_mail
from django.db import transaction
from django.template.loader import render_to_string
from django.utils import timezone

from ..store.models import App, Package, Release, Setting
from ..store.services import audit
from . import skrill, split, tax
from .licence import LicenceKey
from .models import Order, Payment

log = logging.getLogger(__name__)

CYCLES = {"monthly": "month", "yearly": "year"}   # the listing's pricing model -> the billing cycle
TEST_MERCHANT_ID = "test-merchant"
TEST_SECRET_WORD = "test-secret-word"

# The transport of the store's own requests to Skrill; the tests put a fake here.
api_call: skrill.ApiCall = skrill.http_api_call


class BillingError(Exception):
    """A request the purchase's state does not allow: the HTTP status, the
    error code and the sentence for the person."""

    def __init__(self, status: int, code: str, description: str) -> None:
        super().__init__(description)
        self.status, self.code, self.description = status, code, description


# ------------------------------------------------------------------ settings

def missing_settings() -> list[str]:
    """What keeps the store from selling; empty when it can."""
    provider = settings.PAYMENT_PROVIDER
    if not provider:
        return ["ULTRASTORE_PORTAL_PAYMENT_PROVIDER"]
    missing = []
    if provider == "skrill":
        for name in ("SKRILL_PAY_TO_EMAIL", "SKRILL_SECRET_WORD", "SKRILL_MERCHANT_ID"):
            if not getattr(settings, name):
                missing.append("ULTRASTORE_PORTAL_" + name)
    if not settings.RESTRICTED_SECRET:
        missing.append("ULTRASTORE_PORTAL_RESTRICTED_SECRET")
    return missing


def selling() -> bool:
    return not missing_settings()


def merchant() -> tuple[str, str]:
    """The merchant id and the secret word a status report is signed with."""
    if settings.PAYMENT_PROVIDER == "test":
        return TEST_MERCHANT_ID, TEST_SECRET_WORD
    return settings.SKRILL_MERCHANT_ID, settings.SKRILL_SECRET_WORD


def skrill_api() -> skrill.SkrillApi:
    return skrill.SkrillApi(settings.SKRILL_API_URL, settings.SKRILL_PAY_TO_EMAIL, settings.SKRILL_API_PASSWORD, api_call)


def licence_key() -> LicenceKey:
    """The store's licence key: the configured seed, else the one kept in
    the database, made on the first call."""
    if settings.LICENCE_SEED:
        return LicenceKey(bytes.fromhex(settings.LICENCE_SEED))
    row = Setting.objects.filter(key="licence_seed").first()
    if row and row.value:
        return LicenceKey(bytes.fromhex(row.value))
    key = LicenceKey.generate()
    row, created = Setting.objects.get_or_create(key="licence_seed", defaults={"value": key.seed_hex})
    return key if created else LicenceKey(bytes.fromhex(row.value))


# --------------------------------------------------------------------- money

def money(value) -> str:
    return f"{Decimal(value):.2f}"


def parse_money(text: str) -> Decimal | None:
    try:
        value = Decimal(str(text).strip())
    except (InvalidOperation, ValueError):
        return None
    return value if value.is_finite() else None


def next_receipt_no() -> int:
    """The next receipt number: one counter for the store, counted under a
    lock so two payments at once never share one. Call inside a
    transaction."""
    Setting.objects.get_or_create(key="receipt_no", defaults={"value": "0"})
    row = Setting.objects.select_for_update().get(key="receipt_no")
    number = int(row.value or "0") + 1
    row.value = str(number)
    row.save(update_fields=["value"])
    return number


def add_cycle(day: dt.date, cycle: str) -> dt.date:
    """The same day one month or one year on; the last day of a shorter month."""
    if cycle == "year":
        year, month = day.year + 1, day.month
    else:
        year, month = (day.year + 1, 1) if day.month == 12 else (day.year, day.month + 1)
    return dt.date(year, month, min(day.day, calendar.monthrange(year, month)[1]))


def period(start: dt.date, cycle: str) -> tuple[dt.date, dt.date]:
    """The paid-until day and the licence's valid-until day of a period
    starting on `start`: the grace days are the ones the provider retries a
    failed renewal in."""
    paid_until = add_cycle(start, cycle)
    return paid_until, paid_until + dt.timedelta(days=settings.GRACE_DAYS)


# -------------------------------------------------------------------- orders

def sellable_app(app_id: str) -> App:
    app = App.objects.public().filter(app_id=app_id, kind="app").select_related("publisher").first()
    if app is None:
        raise BillingError(404, "no_such_app", f"The store does not list {app_id}")
    if app.is_free or not app.price:
        raise BillingError(400, "free_app", f"{app.name} is free: nothing to pay")
    if app.lifecycle == "withdrawn":
        raise BillingError(409, "withdrawn", f"{app.name} is withdrawn: it is not sold any more")
    return app


def create_order(app_id: str, *, source: str = "store", email: str = "", language: str = "", code: str = "", promote: str = "",
                 country: str = "") -> Order:
    missing = missing_settings()
    if missing:
        raise BillingError(503, "not_configured", "The store is not set up to sell: " + ", ".join(missing))
    app = sellable_app(app_id)
    order = Order.objects.create(
        app=app, app_name=app.name, amount=app.price, currency=app.currency.upper(), list_price=app.price,
        country=tax.clean_country(country),
        code=code[:40], code_applied=False,   # discount codes are not built: the code is kept, the price is the list price
        cycle=CYCLES.get(app.pricing_model, ""), provider=settings.PAYMENT_PROVIDER, source=source, payer_email=email, language=language[:8])
    audit(None, "order.open", "order", order.public_id, app=app.app_id, amount=money(order.amount), currency=order.currency, source=source)
    if promote:   # the token of the advertisement the buyer last clicked, kept by their store
        from ..promote import services as advertising
        advertising.order_opened(order, promote)
    return order


def order_by_secret(public_id: str, secret: str | None) -> Order:
    order = Order.objects.select_related("app__publisher").filter(public_id=public_id).first()
    if order is None or secret is None or not hmac.compare_digest(order.secret, secret):
        raise BillingError(404, "no_such_order", "No such order")
    return order


def checkout_url(order: Order) -> str:
    return f"{settings.BASE_URL}/pay/{order.public_id}/"


def order_page_url(order: Order) -> str:
    return f"{settings.BASE_URL}/orders/{order.public_id}/?secret={order.secret}"


def order_public(order: Order) -> dict:
    """The order as its buyer's store reads it (Purchase.cpp)."""
    out = {"order_id": order.public_id, "app": order.app.app_id, "app_name": order.app_name, "amount": money(order.amount),
           "currency": order.currency, "list_price": money(order.list_price), "code": order.code, "code_applied": order.code_applied,
           "status": order.status, "created_at": order.created.strftime("%Y-%m-%dT%H:%M:%SZ"), "cycle": order.cycle}
    if order.paid_at:
        out.update({"paid_at": order.paid_at.strftime("%Y-%m-%d"), "licence_id": order.licence_id, "receipt_no": order.receipt_no or 0})
    if order.is_paid:
        out["licence"] = order.licence
    if order.cycle:
        out.update({"paid_until": order.paid_until.isoformat() if order.paid_until else "",
                    "valid_until": order.valid_until.isoformat() if order.valid_until else "",
                    "subscription_status": order.subscription_status})
    if order.revoked:
        out.update({"revoked": True, "revoked_at": order.revoked_at.strftime("%Y-%m-%d"), "revoked_reason": order.revoked_reason})
    if order.refunded:
        out["refunded"] = money(order.refunded)
    payments = list(order.payments.all())
    if payments:
        numbers = dict(order.invoices.values_list("payment_id", "number"))
        out["payments"] = [{"kind": p.kind, "at": p.at.strftime("%Y-%m-%d"), "amount": money(p.amount), "currency": p.currency,
                            "receipt_no": p.receipt_no or 0, **({"invoice_no": numbers[p.pk]} if p.pk in numbers else {})} for p in payments]
    return out


def issue(order: Order) -> str:
    """The order's licence token, with what the purchase includes."""
    return licence_key().issue(
        licence_id=order.licence_id, app=order.app.app_id, order_id=order.public_id,
        issued=(order.paid_at or timezone.now()).strftime("%Y-%m-%d"), updates=order.updates or "always",
        updates_until=order.updates_until.isoformat() if order.updates_until else "",
        valid_until=order.valid_until.isoformat() if order.valid_until else "")


def send_receipt(order: Order) -> None:
    """The buyer's mail: what was bought and the link that reaches the
    order again. A mail that cannot go out does not undo the sale."""
    if not order.payer_email:
        return
    try:
        send_mail(f"UltraStore: your purchase of {order.app_name}",
                  render_to_string("mail/receipt.txt", {"order": order, "link": order_page_url(order), "amount": money(order.amount)}),
                  None, [order.payer_email])
    except Exception as error:   # noqa: BLE001 - SMTP fails in many ways
        log.warning("Receipt for order %s not sent: %s", order.public_id, error)


# ------------------------------------------------------ the provider reports

def _reported_amount(fields: dict[str, str]) -> tuple[Decimal | None, str]:
    """What the buyer was charged, in the order's terms: the amount and the
    currency the form posted, which Skrill reports back beside what reached
    the merchant's account (mb_amount, mb_currency - the same pair unless
    the account is kept in another currency)."""
    if fields.get("amount") and fields.get("currency"):
        return parse_money(fields["amount"]), fields["currency"].upper()
    return parse_money(fields.get("mb_amount", "")), fields.get("mb_currency", "").upper()


def _verified(fields: dict[str, str]) -> Order:
    """The order a report is about, locked, once the report's signature is
    the provider's. Call inside a transaction."""
    merchant_id, secret_word = merchant()
    if not secret_word or not skrill.verify_status(fields, merchant_id, secret_word):
        raise BillingError(403, "bad_signature", "The status report's signature is not the payment provider's")
    order = Order.objects.select_for_update().select_related("app").filter(public_id=fields.get("transaction_id", "")).first()
    if order is None:
        raise BillingError(404, "no_such_order", "The status report names no order of ours")
    return order


def _reason(fields: dict[str, str]) -> str:
    return f" ({fields['failed_reason_code']})" if fields.get("failed_reason_code") else ""


def _chargeback(order: Order, fields: dict[str, str]) -> None:
    """A chargeback on any of the order's payments takes the licence back;
    the provider's repeat of the report changes nothing."""
    reverses = fields.get("mb_transaction_id", "")
    if order.payments.filter(kind=Payment.CHARGEBACK, reverses=reverses).exists():
        return
    amount = parse_money(fields.get("mb_amount", "")) or order.amount
    tax.issue_invoice(Payment.objects.create(
        order=order, kind=Payment.CHARGEBACK, amount=amount, currency=order.currency, provider_transaction="",
        reverses=reverses, note="chargeback" + _reason(fields), **split.reversal_split(order, reverses, amount)))
    order.refunded += amount
    order.status = Order.CHARGEBACK
    order.revoked_at = timezone.now()
    order.revoked_reason = "chargeback"
    if order.cycle:
        order.subscription_status = "cancelled"
    order.save()
    audit(None, "order.chargeback", "order", order.public_id, app=order.app.app_id, amount=money(amount))


@transaction.atomic
def process_status(fields: dict[str, str]) -> Order:
    """The provider's report on an order's payment (POST /v1/skrill/status)."""
    order = _verified(fields)
    status = fields.get("status", "")
    name = skrill.STATUS_NAMES.get(status, "")
    transaction_id = fields.get("mb_transaction_id", "")
    if status == skrill.STATUS_PROCESSED:
        if order.paid_at:
            return order   # the provider repeats a report it got no answer to
        amount, currency = _reported_amount(fields)
        if amount != order.amount or currency != order.currency:
            order.status = Order.PENDING
            order.provider_transaction = transaction_id
            order.note(f"amount mismatch: the provider reports {fields.get('amount') or fields.get('mb_amount')} {currency}")
            raise _Mismatch(order)   # report_status keeps the note: the mismatch is what the operator looks for
        now = timezone.now()
        order.status = Order.PAID
        order.paid_at = now
        order.provider_transaction = transaction_id
        order.payer_email = order.payer_email or fields.get("pay_from_email", "")[:254]
        order.rec_payment_id = fields.get("rec_payment_id", "")
        order.licence_id = secrets.token_hex(8)
        # What the purchase includes is the listing's as it stands when the order is paid.
        order.updates = order.app.updates_period
        order.updates_until = order.app.updates_until if order.app.updates_period == "until" else None
        if order.cycle:
            order.paid_until, order.valid_until = period(now.date(), order.cycle)
            order.subscription_status = "active"
        order.receipt_no = next_receipt_no()
        order.licence = issue(order)
        order.save()
        tax.issue_invoice(Payment.objects.create(
            order=order, kind=Payment.SALE, amount=order.amount, currency=order.currency,
            provider_transaction=transaction_id, receipt_no=order.receipt_no, **split.sale_split(order, order.amount)))
        audit(None, "order.paid", "order", order.public_id, app=order.app.app_id, amount=money(order.amount), currency=order.currency,
              receipt=order.receipt_no)
        from ..promote import services as advertising
        advertising.order_paid(order)
        transaction.on_commit(lambda: send_receipt(order))
        return order
    if status == skrill.STATUS_CHARGEBACK and order.paid_at:
        _chargeback(order, fields)
        return order
    if not order.paid_at:
        order.status = name if name in (Order.CANCELLED, Order.FAILED) else Order.PENDING
        order.provider_transaction = transaction_id
        order.note(f"status {status}{_reason(fields)}")
        order.save()
    return order


class _Mismatch(BillingError):
    """The amount paid is not the order's: the note on the order is kept
    (the exception leaves the transaction's block through `mismatch_safe`)."""

    def __init__(self, order: Order) -> None:
        super().__init__(400, "amount_mismatch", "The amount paid is not the order's")
        self.order = order


def report_status(fields: dict[str, str]) -> Order:
    """process_status, with the note of a mismatch kept although the report
    is refused."""
    try:
        return process_status(fields)
    except _Mismatch as mismatch:
        mismatch.order.save(update_fields=["status", "provider_transaction", "notes"])
        raise


@transaction.atomic
def process_recurring(fields: dict[str, str]) -> Order:
    """A subscription's later payment (POST /v1/skrill/recurring):
    processed extends the period and re-issues the licence; failed is noted
    (the licence lasts through the grace days); cancelled ends it."""
    order = _verified(fields)
    if not order.cycle or not order.paid_at:
        raise BillingError(409, "not_a_subscription", "The order is not a paid subscription")
    status = fields.get("status", "")
    transaction_id = fields.get("mb_transaction_id", "")
    rec_id = fields.get("rec_payment_id", "")
    if rec_id and not order.rec_payment_id:
        order.rec_payment_id = rec_id
    if status == skrill.STATUS_PROCESSED:
        if transaction_id and Payment.objects.filter(provider_transaction=transaction_id).exists():
            return order   # the repeat of a report
        if order.revoked:
            order.note(f"renewal {transaction_id} after the licence was taken back: not applied")
            order.save()
            return order
        amount, currency = _reported_amount(fields)
        if amount != order.list_price or currency != order.currency:
            raise BillingError(400, "amount_mismatch", "The amount paid is not the subscription's")
        # The next period starts where the paid one ends, or today when that is past.
        today = timezone.localdate()
        start = max(order.paid_until, today) if order.paid_until else today
        order.paid_until, order.valid_until = period(start, order.cycle)
        order.subscription_status = "active"
        order.licence = issue(order)
        order.save()
        number = next_receipt_no()
        tax.issue_invoice(Payment.objects.create(
            order=order, kind=Payment.RENEWAL, amount=order.list_price, currency=order.currency,
            provider_transaction=transaction_id, receipt_no=number, **split.sale_split(order, order.list_price)))
        audit(None, "order.renewed", "order", order.public_id, app=order.app.app_id, amount=money(order.list_price), receipt=number,
              paid_until=order.paid_until.isoformat())
        return order
    if status == skrill.STATUS_CHARGEBACK:
        _chargeback(order, fields)
        return order
    if status == skrill.STATUS_FAILED:
        order.subscription_status = "failing"
        order.note(f"renewal failed{_reason(fields)}, paid until {order.paid_until}")
    elif status == skrill.STATUS_CANCELLED:
        order.subscription_status = "cancelled"
        order.note("the recurring payment was cancelled")
    else:
        order.note(f"recurring status {status}")
    order.save()
    return order


def test_report(order: Order, status: str = skrill.STATUS_PROCESSED, *, recurring: bool = False, amount: str = "", transaction_id: str = "") -> dict[str, str]:
    """A status report as the test provider sends it: Skrill's fields,
    signed with the test secret, so the same code decides."""
    merchant_id, secret_word = TEST_MERCHANT_ID, TEST_SECRET_WORD
    value = amount or money(order.list_price if recurring else order.amount)
    fields = {"pay_to_email": "test@example.org", "pay_from_email": order.payer_email or "buyer@example.org", "merchant_id": merchant_id,
              "transaction_id": order.public_id, "mb_transaction_id": transaction_id or "T" + secrets.token_hex(6), "mb_amount": value,
              "mb_currency": order.currency, "amount": value, "currency": order.currency, "status": status}
    if order.cycle:
        fields["rec_payment_id"] = order.rec_payment_id or "R" + order.public_id[:10]
    fields["md5sig"] = skrill.status_signature(merchant_id, order.public_id, secret_word, value, order.currency, status)
    return fields


# ---------------------------------------------------- the buyer's own doings

def cancel_subscription(order: Order) -> Order:
    """The buyer ends the subscription: the provider charges no more, and
    the licence lasts until the paid period ends."""
    if not order.cycle or not order.paid_at:
        raise BillingError(409, "not_a_subscription", "The order is not a paid subscription")
    if order.subscription_status == "cancelled":
        return order
    if order.provider == "skrill" and order.rec_payment_id:
        api = skrill_api()
        if not api.configured:
            raise BillingError(503, "not_configured", "The Skrill API password is not configured: the subscription cannot be cancelled here")
        try:
            api.cancel_recurring(order.rec_payment_id)
        except skrill.SkrillApiError as error:
            raise BillingError(502, "provider_refused", str(error)) from error
    order.subscription_status = "cancelled"
    order.note("cancelled by the buyer")
    order.save(update_fields=["subscription_status", "notes"])
    audit(None, "order.subscription_cancel", "order", order.public_id, app=order.app.app_id)
    return order


def signed_path(path: str, *, now: int | None = None) -> tuple[str, int]:
    """A path with nginx's secure link on it (secure_link_md5
    "$secure_link_expires$uri <secret>"), and when it ends."""
    expires = int(now if now is not None else time.time()) + settings.RESTRICTED_TTL
    digest = hashlib.md5(f"{expires}{path} {settings.RESTRICTED_SECRET}".encode()).digest()   # noqa: S324 - nginx's scheme
    return f"{path}?md5={base64.urlsafe_b64encode(digest).decode().rstrip('=')}&expires={expires}", expires


def link_is_valid(path: str, md5: str, expires: str, *, now: int | None = None) -> bool:
    """Whether a signed path is still good - what nginx decides in
    production, here for a developer's machine."""
    try:
        until = int(expires)
    except (TypeError, ValueError):
        return False
    if until < int(now if now is not None else time.time()) or not settings.RESTRICTED_SECRET:
        return False
    digest = hashlib.md5(f"{until}{path} {settings.RESTRICTED_SECRET}".encode()).digest()   # noqa: S324
    return hmac.compare_digest(base64.urlsafe_b64encode(digest).decode().rstrip("="), md5 or "")


def restricted_packages(app: App):
    """The app's packages a buyer may fetch: those of its published releases."""
    return Package.objects.filter(release__app=app, release__state=Release.PUBLISHED)


def download_url(order: Order, file: str) -> dict:
    """A link to one of the app's restricted packages, for this order's
    buyer: `file` as the catalog's URL names it, restricted/<sha256>.<format>."""
    if not order.is_paid:
        raise BillingError(409, "not_paid", "The order is not paid" if not order.paid_at else "The licence was taken back: " + order.revoked_reason)
    if order.valid_until and order.valid_until < timezone.localdate():
        raise BillingError(409, "expired", f"The subscription ended on {order.paid_until}")
    if not settings.RESTRICTED_SECRET:
        raise BillingError(503, "not_configured", "Signed download links are not configured")
    name = file.removeprefix("restricted/")
    sha256, _, fmt = name.partition(".")
    if not file.startswith("restricted/") or not order.app.restricted_packages \
            or not restricted_packages(order.app).filter(sha256=sha256, format=fmt).exists():
        raise BillingError(404, "no_such_file", "The file is not one of the app's restricted packages")
    path, expires = signed_path(f"/v1/restricted/{name}")
    return {"url": settings.BASE_URL + path, "expires": expires}


# ------------------------------------------------------------- the operator

def paid_total(order: Order) -> Decimal:
    return sum((p.amount for p in order.payments.all() if p.kind in (Payment.SALE, Payment.RENEWAL)), Decimal("0"))


@transaction.atomic
def refund(order: Order, actor, *, amount: str = "", revoke: bool | None = None, note: str = "") -> Order:
    """Money given back through the provider: the latest payment, whole or
    `amount` of it. The licence is taken back when everything paid is
    refunded, or when asked; a partial refund leaves it."""
    order = Order.objects.select_for_update().select_related("app").get(pk=order.pk)
    if not order.paid_at:
        raise BillingError(409, "not_paid", "The order is not paid")
    payments = [p for p in order.payments.all() if p.kind in (Payment.SALE, Payment.RENEWAL)]
    if not payments:
        raise BillingError(404, "no_such_payment", "The order has no payment to refund")
    payment = payments[-1]
    value = parse_money(amount) if amount else payment.amount
    if value is None or value <= 0:
        raise BillingError(400, "bad_amount", "The amount is not a number above zero")
    total = paid_total(order)
    if value > payment.amount or order.refunded + value > total:
        raise BillingError(400, "bad_amount", f"The order has {money(total - order.refunded)} {order.currency} left to refund")
    provider_transaction = ""
    if order.provider == "skrill":
        api = skrill_api()
        if not api.configured:
            raise BillingError(503, "not_configured", "The Skrill API password is not configured: refunds cannot be made here")
        try:
            done = api.refund(payment.provider_transaction, amount=money(value) if amount else "", note=note)
        except skrill.SkrillApiError as error:
            raise BillingError(502, "provider_refused", str(error)) from error
        provider_transaction = done.get("mb_transaction_id", "")
    order.refunded += value
    take_back = revoke if revoke is not None else order.refunded >= total
    tax.issue_invoice(Payment.objects.create(
        order=order, kind=Payment.REFUND, amount=value, currency=order.currency, provider_transaction=provider_transaction,
        reverses=payment.provider_transaction, receipt_no=next_receipt_no(), note=note[:300],
        **split.reversal_split(order, payment.provider_transaction, value)))
    if take_back and not order.revoked:
        order.status = Order.REFUNDED
        order.revoked_at = timezone.now()
        order.revoked_reason = "refund"
        if order.cycle:
            order.subscription_status = "cancelled"
    order.save()
    audit(actor, "order.refund", "order", order.public_id, app=order.app.app_id, amount=money(value), revoked=bool(take_back), note=note)
    return order


def licence_status(licence_id: str) -> dict:
    """Whether a licence stands - an app that is online may ask; an app
    that is not trusts the signed token until valid_until."""
    order = Order.objects.select_related("app").filter(licence_id=licence_id).exclude(licence_id="").first()
    if order is None or not order.paid_at:
        raise BillingError(404, "no_such_licence", "No such licence")
    if order.revoked:
        status = "revoked"
    elif order.valid_until and order.valid_until < timezone.localdate():
        status = "expired"
    else:
        status = "valid"
    return {"licence_id": licence_id, "app": order.app.app_id, "status": status, "cycle": order.cycle,
            "valid_until": order.valid_until.isoformat() if order.valid_until else "", "revoked_reason": order.revoked_reason}
