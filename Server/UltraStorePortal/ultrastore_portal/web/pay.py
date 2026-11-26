"""The pages a buyer sees around a payment: the hand-off to the provider,
where the provider sends them back, the order with its licence and
downloads; and buying from an app's web page.

Version: 0.1.0
"""

from __future__ import annotations

from django import forms
from django.conf import settings
from django.http import Http404, HttpResponse
from django.shortcuts import get_object_or_404, redirect, render
from django.utils import timezone
from django.views.decorators.http import require_POST
from django.views.static import serve

from ..billing import services as billing
from ..billing import skrill, tax
from ..billing.models import Order
from .storefront import public_app_or_404
from .utils import too_many


class BuyForm(forms.Form):
    email = forms.EmailField(label="Your email", help_text="The receipt and the link to your purchase go there.")
    country = forms.ChoiceField(label="Your country", choices=[], help_text="The price includes the VAT of your country; the invoice names it.")

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Asked only when the store reckons tax (billing/tax.py).
        if tax.configured():
            self.fields["country"].choices = tax.countries()
            self.fields["country"].initial = settings.SELLER_COUNTRY
        else:
            del self.fields["country"]


def buy(request, app_id: str):
    """Buying from the app's page: an email for the receipt, then the
    provider. The native store buys through POST /v1/checkout instead."""
    app = public_app_or_404(app_id)
    if app.is_free or not billing.selling():
        return redirect(app)
    form = BuyForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        if too_many(request, "checkout", 30, 3600):
            form.add_error(None, "Too many orders from this address. Try again later.")
        else:
            try:
                from .promote import clicked_token
                order = billing.create_order(app.app_id, source="web", email=form.cleaned_data["email"], promote=clicked_token(request, app),
                                             country=form.cleaned_data.get("country", ""))
            except billing.BillingError as refused:
                form.add_error(None, refused.description)
            else:
                request.session.setdefault("orders", {})[order.public_id] = order.secret
                request.session.modified = True
                return redirect("pay:handoff", order.public_id)
    return render(request, "form_page.html", {
        "form": form, "title": f"Buy {app.name}", "submit": f"Continue to pay {app.price_label()}",
        "lead": "You pay on the payment provider's page; the store never sees your card. "
                "Buying inside the UltraStore app installs the licence for you - on the web you get it by mail."})


def _order(public_id: str) -> Order:
    return get_object_or_404(Order.objects.select_related("app__publisher"), public_id=public_id)


def handoff(request, public_id: str):
    """The page that sends the buyer on to the provider."""
    order = _order(public_id)
    if order.paid_at:
        return render(request, "pay/message.html", {"title": "Already paid", "order": order,
                                                    "text": f"{order.app_name} is paid for. UltraStore picks the licence up on its own; you can close this page."})
    context = {"order": order, "amount": billing.money(order.amount), "list_price": billing.money(order.list_price)}
    if order.provider == "test":
        return render(request, "pay/test.html", context)
    fields = skrill.checkout_fields(
        pay_to_email=settings.SKRILL_PAY_TO_EMAIL, base_url=settings.BASE_URL, order_id=order.public_id, amount=billing.money(order.amount),
        currency=order.currency, app_name=order.app_name, app_id=order.app.app_id, language=order.language or "EN",
        cycle=order.cycle, rec_amount=billing.money(order.list_price), rec_grace_days=settings.GRACE_DAYS)
    if order.payer_email:
        fields["pay_from_email"] = order.payer_email
    return render(request, "pay/handoff.html", {**context, "pay_url": settings.SKRILL_PAY_URL, "fields": fields})


@require_POST
def test_pay(request, public_id: str):
    """The test provider's button: Skrill's report, signed with the test
    secret, through the same code Skrill's goes through."""
    if settings.PAYMENT_PROVIDER != "test":
        raise Http404
    order = _order(public_id)
    outcome = request.POST.get("outcome", "paid")
    status = {"paid": skrill.STATUS_PROCESSED, "failed": skrill.STATUS_FAILED}.get(outcome, skrill.STATUS_CANCELLED)
    if outcome == "cancelled":
        return redirect("pay:cancel", public_id)
    try:
        billing.report_status(billing.test_report(order, status))
    except billing.BillingError as refused:
        return render(request, "pay/message.html", {"title": "The payment was refused", "order": order, "text": refused.description}, status=400)
    return redirect("pay:return", public_id)


def returned(request, public_id: str):
    """Where the provider sends the buyer back. It proves nothing: the
    order is paid when the provider's own report said so."""
    order = _order(public_id)
    secret = request.session.get("orders", {}).get(order.public_id)
    if secret == order.secret:
        return redirect(f"/orders/{order.public_id}/?secret={order.secret}")
    if order.paid_at:
        text = f"{order.app_name} is yours. Receipt no. {order.receipt_no}, {billing.money(order.amount)} {order.currency}."
        if order.cycle:
            text += f" The subscription renews every {order.cycle}; UltraStore shows it and lets you cancel it."
        text += " UltraStore picks the licence up on its own; you can close this page."
        return render(request, "pay/message.html", {"title": "Thank you", "order": order, "text": text, "ok": True})
    return render(request, "pay/message.html", {
        "title": "Thank you", "order": order,
        "text": f"The payment for {order.app_name} is being confirmed. UltraStore shows the purchase as soon as the confirmation arrives; you can close this page."})


def cancelled(request, public_id: str):
    order = _order(public_id)
    if order.status == Order.OPEN:
        order.status = Order.CANCELLED
        order.note("the buyer came back by the cancel link")
        order.save(update_fields=["status", "notes"])
    return render(request, "pay/message.html", {"title": "Nothing was charged", "order": order,
                                                "text": f"The purchase of {order.app_name} was cancelled. You can close this page and try again."})


def _own_order(request, public_id: str) -> Order:
    try:
        return billing.order_by_secret(public_id, request.GET.get("secret"))
    except billing.BillingError:
        raise Http404 from None


def order_page(request, public_id: str):
    """The buyer's page of a purchase, reached by the link with the
    order's secret: its state, the licence, the receipt, the downloads."""
    order = _own_order(request, public_id)
    release = order.app.latest_release() if order.is_paid else None
    response = render(request, "pay/order.html", {
        "order": order, "app": order.app, "amount": billing.money(order.amount), "release": release,
        "packages": list(release.packages.all()) if release else [], "restricted": order.app.restricted_packages,
        "payments": order.payments.all(), "expired": bool(order.valid_until and order.valid_until < timezone.localdate())})
    response["Cache-Control"] = "no-store"   # the link carries the order's secret; so does the page
    return response


def order_licence(request, public_id: str):
    """The licence as the file an app looks for beside its executable."""
    order = _own_order(request, public_id)
    if not order.is_paid:
        raise Http404
    response = HttpResponse(order.licence + "\n", content_type="text/plain; charset=utf-8")
    response["Content-Disposition"] = 'attachment; filename="licence.ulic"'
    return response


def order_download(request, public_id: str, pk: int):
    """A package for this order's buyer: a signed link made at the click,
    so it is never stale on the page."""
    order = _own_order(request, public_id)
    package = get_object_or_404(billing.restricted_packages(order.app), pk=pk)
    if not order.app.restricted_packages:
        return redirect("store:download", order.app.app_id, package.pk)
    try:
        return redirect(billing.download_url(order, f"restricted/{package.file_name}")["url"])
    except billing.BillingError as refused:
        return render(request, "pay/message.html", {"title": "The download is not available", "order": order, "text": refused.description}, status=409)


@require_POST
def order_unsubscribe(request, public_id: str):
    order = _own_order(request, public_id)
    try:
        billing.cancel_subscription(order)
    except billing.BillingError as refused:
        return render(request, "pay/message.html", {"title": "The subscription was not cancelled", "order": order, "text": refused.description},
                      status=refused.status)
    return redirect(f"/orders/{order.public_id}/?secret={order.secret}")


def restricted_file(request, path: str):
    """A developer's machine only: what nginx's secure link does in
    production (deploy/nginx.conf) - the file for a link that is signed and
    not over, 403 without one, 410 after it."""
    uri = f"/v1/restricted/{path}"
    md5, expires = request.GET.get("md5", ""), request.GET.get("expires", "")
    if not md5 or not expires:
        return HttpResponse(status=403)
    if not billing.link_is_valid(uri, md5, expires):
        return HttpResponse(status=410 if expires.isdigit() and billing.link_is_valid(uri, md5, expires, now=0) else 403)
    return serve(request, path, document_root=settings.PUBLISH_DIR / "v1" / "restricted")

