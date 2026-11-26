"""Buying, as the buyer's store asks for it (Apps/UltraStore/core/
Purchase.cpp; the catalog's payment_url is the store's root):

    POST /v1/checkout {app, code?, language?, promote?}   an order: order_id, secret, amount, currency, checkout_url
                                                     (promote: the token of the advertisement last clicked)
    GET  /v1/orders/<id>?secret=                     its state; when paid the licence, licence_id, receipt_no
    GET  /v1/orders/<id>/receipt?secret=[&no=]       the receipt: a page for a browser, text for anything else
    POST /v1/orders/<id>/download?secret=&file=      {url, expires}: a link to a restricted package, ten minutes
    POST /v1/orders/<id>/subscription/cancel?secret= the buyer ends a subscription
    GET  /v1/licences/<licence id>                   valid, expired or revoked
    GET  /v1/keys                                    the licence key's public half
    POST /v1/skrill/status, /v1/skrill/recurring     the provider's signed reports (form fields) -> OK

Version: 0.1.0
"""

from __future__ import annotations

import json

from django.conf import settings
from django.http import HttpResponse
from django.shortcuts import render
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_POST

from ..billing import services as billing
from ..billing.models import Invoice, Payment
from ..web.utils import too_many
from .views import answer, error


def guarded(view):
    """A BillingError as the JSON error it names."""
    def wrapper(request, *args, **kwargs):
        try:
            return view(request, *args, **kwargs)
        except billing.BillingError as refused:
            return error(refused.status, refused.code, refused.description)
    wrapper.__name__ = view.__name__
    wrapper.__doc__ = view.__doc__
    return wrapper


@csrf_exempt
@require_POST
@guarded
def checkout(request):
    if too_many(request, "checkout", 30, 3600):
        return error(429, "too_many_requests", "Too many orders from this address; try again later")
    try:
        body = json.loads(request.body or b"{}")
    except ValueError:
        body = None
    if not isinstance(body, dict) or not str(body.get("app") or "").strip():
        return error(400, "bad_request", "The body is a JSON object naming the app")
    order = billing.create_order(str(body["app"]).strip(), source="store", code=str(body.get("code") or ""), language=str(body.get("language") or ""),
                                 promote=str(body.get("promote") or ""), country=str(body.get("country") or ""), ref=str(body.get("ref") or ""))
    return answer({**billing.order_public(order), "secret": order.secret, "checkout_url": billing.checkout_url(order),
                   "environment": "sandbox" if settings.PAYMENT_PROVIDER == "test" else "production"})


@require_GET
@guarded
def order(request, public_id: str):
    return answer(billing.order_public(billing.order_by_secret(public_id, request.GET.get("secret"))))


@require_GET
@guarded
def receipt(request, public_id: str):
    found = billing.order_by_secret(public_id, request.GET.get("secret"))
    if not found.paid_at:
        return error(409, "not_paid", "The order is not paid")
    payments = list(found.payments.all())
    number = request.GET.get("no")
    payment = next((p for p in payments if str(p.receipt_no) == number), None) if number else (payments[0] if payments else None)
    if payment is None:
        return error(404, "no_such_receipt", "The order has no receipt with that number")
    context = {"order": found, "payment": payment, "is_reversal": payment.kind in (Payment.REFUND, Payment.CHARGEBACK),
               "invoice": Invoice.objects.filter(payment=payment).first()}
    # With the store's tax details configured the payment has a tax invoice
    # (billing/tax.py), and that is what its receipt is.
    name = "pay/invoice" if context["invoice"] else "pay/receipt"
    if "text/html" in request.headers.get("Accept", ""):
        return render(request, name + ".html", context)
    return render(request, name + ".txt", context, content_type="text/plain; charset=utf-8")


@csrf_exempt
@require_POST
@guarded
def download(request, public_id: str):
    found = billing.order_by_secret(public_id, request.GET.get("secret"))
    return answer(billing.download_url(found, request.GET.get("file", "")))


@csrf_exempt
@require_POST
@guarded
def subscription_cancel(request, public_id: str):
    found = billing.order_by_secret(public_id, request.GET.get("secret"))
    return answer(billing.order_public(billing.cancel_subscription(found)))


@require_GET
@guarded
def licence(request, licence_id: str):
    return answer(billing.licence_status(licence_id))


@require_GET
def keys(request):
    if not billing.selling():
        return error(503, "not_configured", "The store is not set up to sell: " + ", ".join(billing.missing_settings()))
    return answer({"licence_public_key": billing.licence_key().public_hex,
                   "environment": "sandbox" if settings.PAYMENT_PROVIDER == "test" else "production"})


def _report(request, handler):
    if settings.PAYMENT_PROVIDER != "skrill":
        return error(404, "not_configured", "Skrill is not the store's payment provider")
    fields = {key: str(value) for key, value in request.POST.items()}
    try:
        handler(fields)
    except billing.BillingError as refused:
        return error(refused.status, refused.code, refused.description)
    return HttpResponse("OK", content_type="text/plain")


@csrf_exempt
@require_POST
def skrill_status(request):
    return _report(request, billing.report_status)


@csrf_exempt
@require_POST
def skrill_recurring(request):
    return _report(request, billing.process_recurring)
