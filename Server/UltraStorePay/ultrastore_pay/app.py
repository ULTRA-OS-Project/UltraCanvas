"""The HTTP API of the payment service.

  POST /v1/checkout            the store asks for a purchase: an order and the page that sends the buyer to Skrill
  GET  /pay/{order}            that page (the Quick Checkout form, submitted at once)
  POST /v1/skrill/status       Skrill's status report (status_url): verified, the order paid, the licence issued;
                               a chargeback takes the licence back
  POST /v1/skrill/recurring    Skrill's report of a subscription's later payment (rec_status_url): the period extended,
                               the licence re-issued
  GET  /pay/{order}/return     where Skrill sends the buyer back; says the store will pick the purchase up
  GET  /pay/{order}/cancel     where Skrill sends a buyer who gave up
  GET  /v1/orders/{order}      the store asks (with the order's secret) whether it is paid, and gets the licence;
                               a subscription's period, a revoked licence, the payments
  GET  /v1/orders/{order}/receipt   the receipt, with the secret (?no= for one payment's)
  POST /v1/orders/{order}/download  a short-lived signed URL for a restricted package of a paid order
  POST /v1/orders/{order}/subscription/cancel   the buyer ends a subscription (with the secret)
  POST /v1/orders/{order}/refund    the operator refunds a payment (admin token): the licence taken back when all of it
  GET  /v1/licences/{licence}  whether a licence stands: valid, expired or revoked
  GET  /v1/sales?since=N       the sales and renewals after N, for the admin backend (bearer token)
  GET  /v1/reversals?since=N   the refunds and chargebacks after N, for the admin backend (bearer token)
  GET  /v1/keys                the licence public key
  GET  /healthz

Errors are {"error", "error_description"} with a 4xx status.

Version: 0.2.0 - subscriptions, refunds, chargebacks, the licence check
Version: 0.1.0
"""

from __future__ import annotations

import base64
import calendar
import datetime as dt
import hashlib
import hmac
import html
import secrets
import time
from typing import Any

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, PlainTextResponse, Response
from pydantic import BaseModel, Field

from . import __version__
from .catalog import CatalogError, CatalogSource, Fetcher, cents, decimal, discounted, http_fetch, normalize_code
from .config import Settings
from .licence import LicenceKey
from .pages import LOGO_DARK, LOGO_LIGHT, STATIC_DIR, STYLESHEET, page, receipt_html, receipt_text, wants_html
from .skrill import (CYCLES, STATUS_CANCELLED, STATUS_CHARGEBACK, STATUS_FAILED, STATUS_NAMES, STATUS_PROCESSED, ApiCall, SkrillApi,
                     SkrillApiError, checkout_fields, checkout_page, http_api_call, verify_status)
from .store import Order, Payment, Store


class CheckoutBody(BaseModel):
    app: str = Field(min_length=3, max_length=200)
    code: str = ""
    language: str = Field(default="EN", min_length=2, max_length=2)


class RefundBody(BaseModel):
    amount: str = ""          # decimal; empty: the whole payment
    transaction: str = ""     # the Skrill transaction to refund; empty: the latest sale or renewal
    note: str = ""
    revoke: bool | None = None   # the licence taken back; by default when everything paid is refunded


def _today() -> str:
    return time.strftime("%Y-%m-%d", time.gmtime())


def add_days(day: str, days: int) -> str:
    return (dt.date.fromisoformat(day) + dt.timedelta(days=days)).isoformat()


def add_cycle(day: str, cycle: str) -> str:
    """The same day one month or one year on; the last day of a shorter month."""
    d = dt.date.fromisoformat(day)
    if cycle == "year":
        year, month = d.year + 1, d.month
    else:
        year, month = (d.year + 1, 1) if d.month == 12 else (d.year, d.month + 1)
    return dt.date(year, month, min(d.day, calendar.monthrange(year, month)[1])).isoformat()


_page = page


def create_app(settings: Settings | None = None, fetcher: Fetcher = http_fetch, api_call: ApiCall = http_api_call) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = Store(settings.database)
    catalog = CatalogSource(settings.catalog_url, settings.catalog_keys, settings.catalog_ttl_seconds, fetcher)
    skrill_api = SkrillApi(settings.skrill_api_url, settings.skrill_pay_to_email, settings.skrill_api_password, api_call)

    # The licence key: from the environment, else made once and kept.
    if settings.licence_seed:
        key = LicenceKey(bytes.fromhex(settings.licence_seed))
    elif store.meta("licence_seed"):
        key = LicenceKey(bytes.fromhex(store.meta("licence_seed")))
    else:
        key = LicenceKey.generate()
        store.set_meta("licence_seed", key.seed_hex)

    app = FastAPI(title="UltraStore payment service", version=__version__)
    app.state.store = store
    app.state.catalog = catalog
    app.state.licence_key = key

    @app.exception_handler(HTTPException)
    async def http_error(_request: Request, exc: HTTPException):
        content = exc.detail if isinstance(exc.detail, dict) else {"error": "error", "error_description": str(exc.detail)}
        return JSONResponse(status_code=exc.status_code, content=content)

    def error(status: int, code: str, description: str) -> HTTPException:
        return HTTPException(status_code=status, detail={"error": code, "error_description": description})

    def order_or_404(order_id: str, secret: str | None = None) -> Order:
        order = store.get_order(order_id)
        if not order or (secret is not None and not hmac.compare_digest(order.secret, secret)):
            raise error(404, "no_such_order", "No such order")
        return order

    def _update_terms(app_id: str) -> tuple[str, str]:
        try:
            entry = catalog.current().apps.get(app_id)
        except CatalogError:
            entry = None
        return (entry.updates, entry.updates_until) if entry else ("always", "")

    def admin(authorization: str | None) -> None:
        token = (authorization or "").removeprefix("Bearer ").strip()
        if not settings.admin_token or not hmac.compare_digest(token, settings.admin_token):
            raise error(401, "unauthorized", "The admin token is missing or wrong")

    def issue(order: Order, licence_id: str, valid_until: str) -> str:
        # What the purchase includes comes from the catalog as it stands
        # when the order is paid: the store enforces it on updates.
        terms = _update_terms(order.app)
        return key.issue(licence_id=licence_id, app=order.app, order_id=order.id, issued=order.paid_at[:10] or _today(),
                         updates=terms[0], updates_until=terms[1], valid_until=valid_until)

    def period(start: str, cycle: str) -> tuple[str, str]:
        """The paid-until day and the licence's valid-until day of a period
        starting on `start`."""
        paid_until = add_cycle(start, cycle)
        return paid_until, add_days(paid_until, settings.subscription_grace_days)

    def chargeback(order: Order, fields: dict[str, str]) -> None:
        """A chargeback on any of the order's payments takes the licence
        back; Skrill's repeat of the report changes nothing."""
        transaction = fields.get("mb_transaction_id", "")
        if any(p.kind == "chargeback" and p.reverses == transaction for p in order.payments):
            return
        amount = fields.get("mb_amount", "") or order.amount
        refunded = decimal((cents(order.refunded) or 0) + (cents(amount) or 0))
        store.record_reversal(order.id, "chargeback", skrill_transaction=transaction, reverses=transaction, amount=amount,
                              currency=fields.get("mb_currency", "") or order.currency, refunded=refunded, revoke=True,
                              note="chargeback" + (f" ({fields['failed_reason_code']})" if fields.get("failed_reason_code") else ""))

    async def verified_report(request: Request) -> tuple[dict[str, str], Order]:
        form = await request.form()
        fields = {k: str(v) for k, v in form.items()}
        if not verify_status(fields, settings.skrill_merchant_id, settings.skrill_secret_word):
            raise error(403, "bad_signature", "The status report's signature is not Skrill's")
        order = store.get_order(fields.get("transaction_id", ""))
        if not order:
            raise error(404, "no_such_order", "The status report names no order of ours")
        return fields, order

    # ----- the purchase -----
    @app.post("/v1/checkout")
    async def checkout(body: CheckoutBody) -> dict[str, Any]:
        missing = settings.configured()
        if missing:
            raise error(503, "not_configured", "The payment service is not configured: " + ", ".join(missing))
        try:
            cat = catalog.current()
        except CatalogError as e:
            raise error(503, "catalog_unavailable", str(e)) from e
        app_entry = cat.apps.get(body.app)
        if not app_entry:
            raise error(404, "no_such_app", f"The catalog does not list {body.app}")
        if app_entry.pricing.model == "free" or not app_entry.pricing.price:
            raise error(400, "free_app", f"{app_entry.name} is free: nothing to pay")
        cycle = CYCLES.get(app_entry.pricing.model, "")
        code = cat.find_code(body.code) if body.code else None
        applies = bool(code and code.applies_to(body.app) and code.valid_on(_today()))
        try:
            amount, applied = discounted(app_entry.pricing, code if applies else None)
        except CatalogError as e:
            raise error(500, "bad_price", str(e)) from e
        # A code takes its discount off the first payment of a subscription; the renewals are at the list price.
        order = store.create_order(app=body.app, app_name=app_entry.name, amount=decimal(amount), currency=app_entry.pricing.currency,
                                   list_price=app_entry.pricing.price, code=normalize_code(body.code) if code else "", code_applied=applied,
                                   cycle=cycle)
        return {**order.public(), "secret": order.secret, "checkout_url": f"{settings.public_base_url}/pay/{order.id}",
                "environment": settings.environment}

    # The look of the pages: the stylesheet and the mark, from the package,
    # cached for a day (a new version of the service changes the file names
    # it links when it changes them).
    @app.get("/pay/static/style.css")
    async def stylesheet() -> Response:
        return Response(STYLESHEET, media_type="text/css", headers={"Cache-Control": "public, max-age=86400"})

    @app.get("/pay/static/{name}")
    async def static_file(name: str) -> FileResponse:
        if name not in (LOGO_LIGHT, LOGO_DARK):
            raise error(404, "no_such_file", "No such file")
        return FileResponse(STATIC_DIR / name, media_type="image/png", headers={"Cache-Control": "public, max-age=86400"})

    @app.get("/pay/{order_id}", response_class=HTMLResponse)
    async def pay(order_id: str) -> str:
        order = order_or_404(order_id)
        if order.paid_at:
            return _page("Already paid", f"<p>{html.escape(order.app_name)} is paid for. UltraStore picks the licence up on its own; you can close this page.</p>")
        fields = checkout_fields(pay_to_email=settings.skrill_pay_to_email, base_url=settings.public_base_url, order_id=order.id,
                                 amount=order.amount, currency=order.currency, app_name=order.app_name, app_id=order.app,
                                 cycle=order.cycle, rec_amount=order.list_price, rec_grace_days=settings.subscription_grace_days)
        return checkout_page(settings.skrill_pay_url, fields, order.app_name, order.amount, order.currency, order.cycle, order.list_price)

    @app.post("/v1/skrill/status")
    async def skrill_status(request: Request) -> PlainTextResponse:
        fields, order = await verified_report(request)
        status = fields.get("status", "")
        name = STATUS_NAMES.get(status, "unknown")
        if status == STATUS_PROCESSED:
            if order.paid_at:
                return PlainTextResponse("OK")   # Skrill repeats a report it got no answer to
            # The amount Skrill took, in the order's currency, is the order's.
            if fields.get("mb_currency", "") != order.currency or fields.get("mb_amount", "") != order.amount:
                store.set_status(order.id, "pending", skrill_transaction=fields.get("mb_transaction_id", ""),
                                 note=f"amount mismatch: Skrill reports {fields.get('mb_amount')} {fields.get('mb_currency')}")
                raise error(400, "amount_mismatch", "The amount paid is not the order's")
            licence_id = secrets.token_hex(8)
            paid_until, valid_until = period(_today(), order.cycle) if order.cycle else ("", "")
            order.paid_at = _today()
            token = issue(order, licence_id, valid_until)
            store.mark_paid(order.id, skrill_transaction=fields.get("mb_transaction_id", ""), payer_email=fields.get("pay_from_email", ""),
                            licence_id=licence_id, licence=token, paid_until=paid_until, valid_until=valid_until,
                            rec_payment_id=fields.get("rec_payment_id", ""))
            return PlainTextResponse("OK")
        if status == STATUS_CHARGEBACK and order.paid_at:
            chargeback(order, fields)
            return PlainTextResponse("OK")
        if not order.paid_at:
            store.set_status(order.id, name if name != "unknown" else "pending", skrill_transaction=fields.get("mb_transaction_id", ""),
                             note=f"status {status}" + (f" ({fields.get('failed_reason_code')})" if fields.get("failed_reason_code") else ""))
        return PlainTextResponse("OK")

    @app.post("/v1/skrill/recurring")
    async def skrill_recurring(request: Request) -> PlainTextResponse:
        """A subscription's later payment: processed extends the period
        and re-issues the licence; failed is noted (the licence lasts
        through the grace days); cancelled ends the subscription."""
        fields, order = await verified_report(request)
        if not order.cycle or not order.paid_at:
            raise error(409, "not_a_subscription", "The order is not a paid subscription")
        status = fields.get("status", "")
        transaction = fields.get("mb_transaction_id", "")
        rec_id = fields.get("rec_payment_id", "")
        if status == STATUS_PROCESSED:
            if store.payment_by_transaction(transaction):
                return PlainTextResponse("OK")   # the repeat of a report
            if order.revoked:
                store.note(order.id, f"renewal {transaction} after the licence was revoked: not applied")
                return PlainTextResponse("OK")
            if fields.get("mb_currency", "") != order.currency or fields.get("mb_amount", "") != order.list_price:
                store.note(order.id, f"renewal amount mismatch: Skrill reports {fields.get('mb_amount')} {fields.get('mb_currency')}")
                raise error(400, "amount_mismatch", "The amount paid is not the subscription's")
            # The next period starts where the paid one ends, or today when that is past.
            start = max(order.paid_until, _today()) if order.paid_until else _today()
            paid_until, valid_until = period(start, order.cycle)
            token = issue(order, order.licence_id, valid_until)
            if rec_id and not order.rec_payment_id:
                store.set_subscription(order.id, "active", rec_payment_id=rec_id)
            store.record_renewal(order.id, skrill_transaction=transaction, amount=order.list_price, currency=order.currency,
                                 paid_until=paid_until, valid_until=valid_until, licence=token)
            return PlainTextResponse("OK")
        if status == STATUS_CHARGEBACK:
            chargeback(order, fields)
            return PlainTextResponse("OK")
        if status == STATUS_FAILED:
            store.set_subscription(order.id, "failing", rec_payment_id=rec_id,
                                   note=f"renewal failed ({fields.get('failed_reason_code', '?')}), paid until {order.paid_until}")
        elif status == STATUS_CANCELLED:
            store.set_subscription(order.id, "cancelled", rec_payment_id=rec_id, note="the recurring payment was cancelled")
        else:
            store.note(order.id, f"recurring status {status}")
        return PlainTextResponse("OK")

    @app.get("/pay/{order_id}/return", response_class=HTMLResponse)
    async def returned(order_id: str) -> str:
        order = order_or_404(order_id)
        if order.paid_at:
            billing = f" The subscription renews every {html.escape(order.cycle)}; UltraStore shows it and lets you cancel it." if order.cycle else ""
            return _page("Thank you", f"<p class=\"ok\">{html.escape(order.app_name)} is yours.</p>"
                         f"<p>Receipt no. {order.receipt_no}, {html.escape(order.amount)} {html.escape(order.currency)}.{billing} "
                         "UltraStore picks the licence up on its own; you can close this page.</p>")
        return _page("Thank you", f"<p>Skrill is confirming the payment for {html.escape(order.app_name)}.</p>"
                     "<p>UltraStore shows the purchase as soon as the confirmation arrives; you can close this page.</p>")

    @app.get("/pay/{order_id}/cancel", response_class=HTMLResponse)
    async def cancelled(order_id: str) -> str:
        order = order_or_404(order_id)
        if order.status == "open":
            store.set_status(order.id, "cancelled", note="the buyer came back by cancel_url")
        return _page("Nothing was charged", f"<p>The purchase of {html.escape(order.app_name)} was cancelled.</p>"
                     "<p>You can close this page and try again from UltraStore.</p>")

    # ----- the store asks -----
    @app.get("/v1/orders/{order_id}")
    async def order_status(order_id: str, secret: str) -> dict[str, Any]:
        return order_or_404(order_id, secret).public()

    @app.get("/v1/orders/{order_id}/receipt")
    async def receipt(order_id: str, secret: str, no: int = 0, accept: str | None = Header(default=None)) -> Response:
        # A page for a browser (the store's Receipt button opens one), text for everything else.
        order = order_or_404(order_id, secret)
        if not order.paid_at:
            raise error(409, "not_paid", "The order is not paid")
        payment = next((p for p in order.payments if p.receipt_no == no), None) if no else (order.payments[0] if order.payments else None)
        if no and not payment:
            raise error(404, "no_such_receipt", "The order has no receipt with that number")
        if wants_html(accept):
            return HTMLResponse(receipt_html(order, payment))
        return PlainTextResponse(receipt_text(order, payment))

    @app.post("/v1/orders/{order_id}/download")
    async def download(order_id: str, secret: str, file: str) -> dict[str, Any]:
        order = order_or_404(order_id, secret)
        if order.status != "paid":
            raise error(409, "not_paid", "The order is not paid" if not order.paid_at else "The licence was taken back: " + order.revoked_reason)
        if order.valid_until and order.valid_until < _today():
            raise error(409, "expired", f"The subscription ended on {order.paid_until}")
        if not settings.restricted_secret or not settings.restricted_base_url:
            raise error(503, "not_configured", "Signed download URLs are not configured")
        try:
            cat = catalog.current()
        except CatalogError as e:
            raise error(503, "catalog_unavailable", str(e)) from e
        app_entry = cat.apps.get(order.app)
        if not app_entry or file not in app_entry.restricted_files:
            raise error(404, "no_such_file", "The file is not one of the app's restricted packages")
        # nginx secure_link_md5 "$secure_link_expires$uri $secret": the URI is
        # the path the web server sees.
        path = "/" + file
        expires = int(time.time()) + settings.restricted_ttl_seconds
        digest = hashlib.md5(f"{expires}{path} {settings.restricted_secret}".encode()).digest()   # noqa: S324 - nginx's scheme
        md5 = base64.urlsafe_b64encode(digest).decode().rstrip("=")
        return {"url": f"{settings.restricted_base_url}{path}?md5={md5}&expires={expires}", "expires": expires}

    @app.post("/v1/orders/{order_id}/subscription/cancel")
    async def cancel_subscription(order_id: str, secret: str) -> dict[str, Any]:
        """The buyer ends the subscription: Skrill charges no more, and
        the licence lasts until the paid period ends."""
        order = order_or_404(order_id, secret)
        if not order.cycle or not order.paid_at:
            raise error(409, "not_a_subscription", "The order is not a paid subscription")
        if order.subscription_status == "cancelled":
            return order.public()
        if order.rec_payment_id:
            if not skrill_api.configured:
                raise error(503, "not_configured", "The Skrill API password is not configured: the subscription cannot be cancelled here")
            try:
                skrill_api.cancel_recurring(order.rec_payment_id)
            except SkrillApiError as e:
                raise error(502, "skrill_refused", str(e)) from e
        store.set_subscription(order.id, "cancelled", note="cancelled by the buyer from the store")
        return order_or_404(order_id).public()

    @app.get("/v1/licences/{licence_id}")
    async def licence_status(licence_id: str) -> dict[str, Any]:
        """Whether a licence stands - an app that is online may ask; an
        app that is not trusts the signed token until valid_until."""
        order = store.order_of_licence(licence_id)
        if not order or not order.paid_at:
            raise error(404, "no_such_licence", "No such licence")
        if order.revoked:
            status = "revoked"
        elif order.valid_until and order.valid_until < _today():
            status = "expired"
        else:
            status = "valid"
        return {"licence_id": licence_id, "app": order.app, "status": status, "cycle": order.cycle, "valid_until": order.valid_until,
                "revoked_reason": order.revoked_reason}

    # ----- the admin backend asks -----
    @app.post("/v1/orders/{order_id}/refund")
    async def refund(order_id: str, body: RefundBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        """A refund through Skrill: the whole payment or part of it. The
        licence is taken back when everything paid is refunded, or when
        asked (`revoke`); a partial refund leaves it."""
        admin(authorization)
        order = order_or_404(order_id)
        if not order.paid_at:
            raise error(409, "not_paid", "The order is not paid")
        if not skrill_api.configured:
            raise error(503, "not_configured", "The Skrill API password is not configured: refunds cannot be made here")
        payments = [p for p in order.payments if p.kind in ("sale", "renewal")]
        payment = next((p for p in payments if p.skrill_transaction == body.transaction), None) if body.transaction else (payments[-1] if payments else None)
        if not payment:
            raise error(404, "no_such_payment", "The order has no such payment")
        paid_total = sum(cents(p.amount) or 0 for p in payments)
        refunded_so_far = cents(order.refunded) or 0
        amount = cents(body.amount) if body.amount else cents(payment.amount)
        if amount is None or amount <= 0:
            raise error(400, "bad_amount", "The amount is not a decimal above zero")
        if amount > (cents(payment.amount) or 0) or refunded_so_far + amount > paid_total:
            raise error(400, "bad_amount", f"The order has {decimal(paid_total - refunded_so_far)} {order.currency} left to refund")
        try:
            done = skrill_api.refund(payment.skrill_transaction, amount=decimal(amount) if body.amount else "", note=body.note)
        except SkrillApiError as e:
            raise error(502, "skrill_refused", str(e)) from e
        refunded = refunded_so_far + amount
        revoke = body.revoke if body.revoke is not None else refunded >= paid_total
        updated = store.record_reversal(order.id, "refund", skrill_transaction=done.get("mb_transaction_id", ""), reverses=payment.skrill_transaction,
                                        amount=decimal(amount), currency=order.currency, refunded=decimal(refunded), revoke=revoke, note=body.note)
        return updated.public()

    def _feed_entry(seq: int, payment: Payment, order: Order) -> dict[str, Any]:
        return {"seq": seq, "order_id": order.id, "reference": payment.reference,
                "kind": payment.kind, "at": payment.at, "code": order.code if payment.kind == "sale" and (order.code_applied or order.code) else "",
                "app": order.app, "amount": payment.amount, "currency": order.currency, "receipt_no": payment.receipt_no,
                "skrill_transaction": payment.skrill_transaction, "cycle": order.cycle}

    @app.get("/v1/sales")
    async def sales(since: int = 0, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        admin(authorization)
        out = [_feed_entry(p.seq, p, order) for p, order in store.sales_since(since)]
        return {"sales": out, "next": out[-1]["seq"] if out else since}

    @app.get("/v1/reversals")
    async def reversals(since: int = 0, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        """The refunds and the chargebacks, for the ledger to take the
        credits back; each names the sale or renewal it reverses."""
        admin(authorization)
        out = []
        for p, order in store.reversals_since(since):
            entry = _feed_entry(p.reversal_seq, p, order)
            reversed_payment = next((q for q in order.payments if q.kind in ("sale", "renewal") and q.skrill_transaction == p.reverses), None)
            entry["reverses"] = reversed_payment.reference if reversed_payment else order.id
            entry["revoked"] = p.revoking   # this one took the licence back
            out.append(entry)
        return {"reversals": out, "next": out[-1]["seq"] if out else since}

    @app.get("/v1/keys")
    async def keys() -> dict[str, Any]:
        return {"licence_public_key": key.public_hex, "environment": settings.environment}

    @app.get("/healthz", response_class=PlainTextResponse)
    async def healthz() -> str:
        missing = settings.configured()
        return "ok" if not missing else "not configured: " + ", ".join(missing)

    return app
