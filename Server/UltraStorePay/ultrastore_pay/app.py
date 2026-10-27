"""The HTTP API of the payment service.

  POST /v1/checkout            the store asks for a purchase: an order and the page that sends the buyer to Skrill
  GET  /pay/{order}            that page (the Quick Checkout form, submitted at once)
  POST /v1/skrill/status       Skrill's status report (status_url): verified, the order paid, the licence issued
  GET  /pay/{order}/return     where Skrill sends the buyer back; says the store will pick the purchase up
  GET  /pay/{order}/cancel     where Skrill sends a buyer who gave up
  GET  /v1/orders/{order}      the store asks (with the order's secret) whether it is paid, and gets the licence
  GET  /v1/orders/{order}/receipt   the receipt, with the secret
  POST /v1/orders/{order}/download  a short-lived signed URL for a restricted package of a paid order
  GET  /v1/sales?since=N       the paid orders after N, for the admin backend (bearer token)
  GET  /v1/keys                the licence public key
  GET  /healthz

Errors are {"error", "error_description"} with a 4xx status."""

from __future__ import annotations

import base64
import hashlib
import hmac
import html
import secrets
import time
from typing import Any

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, PlainTextResponse
from pydantic import BaseModel, Field

from . import __version__
from .catalog import CatalogError, CatalogSource, Fetcher, decimal, discounted, http_fetch, normalize_code
from .config import Settings
from .licence import LicenceKey
from .skrill import STATUS_NAMES, STATUS_PROCESSED, checkout_fields, checkout_page, verify_status
from .store import Order, Store


class CheckoutBody(BaseModel):
    app: str = Field(min_length=3, max_length=200)
    code: str = ""
    language: str = Field(default="EN", min_length=2, max_length=2)


def _today() -> str:
    return time.strftime("%Y-%m-%d", time.gmtime())


def _page(title: str, body: str) -> str:
    return (
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        f"<title>{html.escape(title)}</title>\n<style>body{{font-family:sans-serif;max-width:36em;margin:3em auto;padding:0 1em;color:#1f2937}}</style>\n"
        f"</head>\n<body>\n<h1>{html.escape(title)}</h1>\n{body}\n</body>\n</html>\n"
    )


def create_app(settings: Settings | None = None, fetcher: Fetcher = http_fetch) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = Store(settings.database)
    catalog = CatalogSource(settings.catalog_url, settings.catalog_keys, settings.catalog_ttl_seconds, fetcher)

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

    def admin(authorization: str | None) -> None:
        token = (authorization or "").removeprefix("Bearer ").strip()
        if not settings.admin_token or not hmac.compare_digest(token, settings.admin_token):
            raise error(401, "unauthorized", "The admin token is missing or wrong")

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
        code = cat.find_code(body.code) if body.code else None
        applies = bool(code and code.applies_to(body.app) and code.valid_on(_today()))
        try:
            amount, applied = discounted(app_entry.pricing, code if applies else None)
        except CatalogError as e:
            raise error(500, "bad_price", str(e)) from e
        order = store.create_order(app=body.app, app_name=app_entry.name, amount=decimal(amount), currency=app_entry.pricing.currency,
                                   list_price=app_entry.pricing.price, code=normalize_code(body.code) if code else "", code_applied=applied)
        return {**order.public(), "secret": order.secret, "checkout_url": f"{settings.public_base_url}/pay/{order.id}",
                "environment": settings.environment}

    @app.get("/pay/{order_id}", response_class=HTMLResponse)
    async def pay(order_id: str) -> str:
        order = order_or_404(order_id)
        if order.status == "paid":
            return _page("Already paid", "<p>This purchase is paid. UltraStore picks the licence up on its own.</p>")
        fields = checkout_fields(pay_to_email=settings.skrill_pay_to_email, base_url=settings.public_base_url, order_id=order.id,
                                 amount=order.amount, currency=order.currency, app_name=order.app_name, app_id=order.app)
        return checkout_page(settings.skrill_pay_url, fields, order.app_name, order.amount, order.currency)

    @app.post("/v1/skrill/status")
    async def skrill_status(request: Request) -> PlainTextResponse:
        form = await request.form()
        fields = {k: str(v) for k, v in form.items()}
        if not verify_status(fields, settings.skrill_merchant_id, settings.skrill_secret_word):
            raise error(403, "bad_signature", "The status report's signature is not Skrill's")
        order = store.get_order(fields.get("transaction_id", ""))
        if not order:
            raise error(404, "no_such_order", "The status report names no order of ours")
        status = fields.get("status", "")
        name = STATUS_NAMES.get(status, "unknown")
        if status == STATUS_PROCESSED:
            if order.status == "paid":
                return PlainTextResponse("OK")   # Skrill repeats a report it got no answer to
            # The amount Skrill took, in the order's currency, is the order's.
            if fields.get("mb_currency", "") != order.currency or fields.get("mb_amount", "") != order.amount:
                store.set_status(order.id, "pending", skrill_transaction=fields.get("mb_transaction_id", ""),
                                 note=f"amount mismatch: Skrill reports {fields.get('mb_amount')} {fields.get('mb_currency')}")
                raise error(400, "amount_mismatch", "The amount paid is not the order's")
            licence_id = secrets.token_hex(8)
            token = key.issue(licence_id=licence_id, app=order.app, order_id=order.id, issued=_today(), updates="per-catalog")
            store.mark_paid(order.id, skrill_transaction=fields.get("mb_transaction_id", ""), payer_email=fields.get("pay_from_email", ""),
                            licence_id=licence_id, licence=token)
            return PlainTextResponse("OK")
        if order.status != "paid":
            store.set_status(order.id, name if name != "unknown" else "pending", skrill_transaction=fields.get("mb_transaction_id", ""),
                             note=f"status {status}" + (f" ({fields.get('failed_reason_code')})" if fields.get("failed_reason_code") else ""))
        return PlainTextResponse("OK")

    @app.get("/pay/{order_id}/return", response_class=HTMLResponse)
    async def returned(order_id: str) -> str:
        order = order_or_404(order_id)
        if order.status == "paid":
            return _page("Thank you", f"<p>{html.escape(order.app_name)} is yours. UltraStore picks the licence up on its own; you can close this page.</p>")
        return _page("Thank you", "<p>Skrill is confirming the payment. UltraStore shows the purchase as soon as the confirmation arrives; you can close this page.</p>")

    @app.get("/pay/{order_id}/cancel", response_class=HTMLResponse)
    async def cancelled(order_id: str) -> str:
        order = order_or_404(order_id)
        if order.status == "open":
            store.set_status(order.id, "cancelled", note="the buyer came back by cancel_url")
        return _page("Nothing was charged", "<p>The purchase was cancelled. You can close this page and try again from UltraStore.</p>")

    # ----- the store asks -----
    @app.get("/v1/orders/{order_id}")
    async def order_status(order_id: str, secret: str) -> dict[str, Any]:
        return order_or_404(order_id, secret).public()

    @app.get("/v1/orders/{order_id}/receipt", response_class=PlainTextResponse)
    async def receipt(order_id: str, secret: str) -> str:
        order = order_or_404(order_id, secret)
        if order.status != "paid":
            raise error(409, "not_paid", "The order is not paid")
        lines = [f"UltraStore receipt no. {order.receipt_no}", f"Date: {order.paid_at}", f"Order: {order.id}", "",
                 f"{order.app_name} ({order.app})", f"List price: {order.list_price} {order.currency}"]
        if order.code_applied:
            lines.append(f"Code {order.code} applied")
        lines += [f"Paid: {order.amount} {order.currency}", f"Skrill transaction: {order.skrill_transaction}", f"Licence: {order.licence_id}", "",
                  "Thank you for buying through UltraStore."]
        return "\n".join(lines) + "\n"

    @app.post("/v1/orders/{order_id}/download")
    async def download(order_id: str, secret: str, file: str) -> dict[str, Any]:
        order = order_or_404(order_id, secret)
        if order.status != "paid":
            raise error(409, "not_paid", "The order is not paid")
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

    # ----- the admin backend asks -----
    @app.get("/v1/sales")
    async def sales(since: int = 0, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        admin(authorization)
        out = []
        for seq, order in store.sales_since(since):
            out.append({"seq": seq, "order_id": order.id, "at": order.paid_at, "code": order.code if order.code_applied or order.code else "",
                        "app": order.app, "amount": order.amount, "currency": order.currency, "receipt_no": order.receipt_no,
                        "skrill_transaction": order.skrill_transaction})
        return {"sales": out, "next": out[-1]["seq"] if out else since}

    @app.get("/v1/keys")
    async def keys() -> dict[str, Any]:
        return {"licence_public_key": key.public_hex, "environment": settings.environment}

    @app.get("/healthz", response_class=PlainTextResponse)
    async def healthz() -> str:
        missing = settings.configured()
        return "ok" if not missing else "not configured: " + ", ".join(missing)

    return app
