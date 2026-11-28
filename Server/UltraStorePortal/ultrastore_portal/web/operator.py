"""The operator's pages: the queue of publishers that applied and of
releases in review, the decision on each, the reports, the catalog and the
audit log. Staff only.

Version: 0.9.0 - tax (the rates, a quarter's figures) and the payouts
Version: 0.7.0 - an order's payments with their split
Version: 0.1.0
"""

from __future__ import annotations

from django.conf import settings
from django.contrib import messages
from django.core.paginator import Paginator
from django.http import FileResponse, Http404
from django.shortcuts import get_object_or_404, redirect, render
from django.views.decorators.http import require_POST

import datetime as dt

from django.utils import timezone

from ..billing import payouts, tax
from ..billing import services as billing
from ..billing.models import Order, Payout, VatRate
from ..store import catalog, services
from ..store.models import App, AuditEntry, Package, Publisher, Release, Report
from ..store.packages import package_path
from .forms import DecisionForm, ReportDecisionForm, VatRateForm
from .utils import operator_required


@operator_required
def home(request):
    return render(request, "operator/home.html", {
        "applications": Publisher.objects.filter(state=Publisher.APPLIED).order_by("created"),
        "queue": Release.objects.filter(state=Release.SUBMITTED).select_related("app__publisher").order_by("submitted_at"),
        "reports": Report.objects.filter(state=Report.OPEN)[:20],
        "snapshot": catalog.latest_snapshot(),
        "counts": {
            "publishers": Publisher.objects.filter(state=Publisher.APPROVED).count(),
            "apps": App.objects.public().count(),
            "suspended": App.objects.filter(suspended=True).count() + Publisher.objects.filter(state=Publisher.SUSPENDED).count(),
        },
        "recent": AuditEntry.objects.all()[:12],
        "selling_missing": billing.missing_settings(),
    })


@operator_required
def publishers(request):
    return render(request, "operator/publishers.html", {"publishers": Publisher.objects.all().order_by("-created")})


@operator_required
def publisher(request, slug: str):
    subject = get_object_or_404(Publisher, slug=slug)
    if request.method == "POST":
        form = DecisionForm(request.POST)
        if form.is_valid():
            try:
                services.decide_publisher(subject, request.user, form.cleaned_data["action"], form.cleaned_data["message"])
                messages.success(request, f"{subject.name}: {subject.get_state_display().lower()}.")
                return redirect("operator:publisher", slug)
            except services.Refused as refused:
                messages.error(request, str(refused))
    return render(request, "operator/publisher.html", {
        "publisher": subject, "members": subject.members.select_related("user"), "apps": subject.apps.all(),
        "history": AuditEntry.objects.filter(target_kind="publisher", target=subject.slug)[:30]})


@operator_required
def release(request, pk: int):
    subject = get_object_or_404(Release.objects.select_related("app__publisher"), pk=pk)
    if request.method == "POST":
        form = DecisionForm(request.POST)
        if form.is_valid():
            try:
                services.decide_release(subject, request.user, form.cleaned_data["action"], form.cleaned_data["message"])
                messages.success(request, f"{subject}: {subject.get_state_display().lower()}.")
                return redirect("operator:home")
            except services.Refused as refused:
                messages.error(request, str(refused))
    app = subject.app
    return render(request, "operator/release.html", {
        "release": subject, "app": app, "packages": subject.packages.all(),
        "missing": services.missing_dependencies(subject),
        "problems": services.listing_problems(app) + services.release_problems(subject),
        "earlier": app.releases.exclude(pk=subject.pk),
        "history": AuditEntry.objects.filter(target_kind="release", target=str(subject))[:30]})


@operator_required
def package(request, pk: int):
    """The review's *Download build*: the bytes from the package store."""
    subject = get_object_or_404(Package, pk=pk)
    path = package_path(subject.sha256, subject.format)
    if not path.is_file():
        raise Http404
    name = f"{subject.release.app.app_id}-{subject.release.version}-{subject.os}-{subject.arch}.{subject.format}"
    return FileResponse(open(path, "rb"), as_attachment=True, filename=name)


@operator_required
def apps(request):
    return render(request, "operator/apps.html", {"apps": App.objects.select_related("publisher").prefetch_related("releases").order_by("name")})


@require_POST
@operator_required
def app_action(request, app_id: str, action: str):
    subject = get_object_or_404(App, app_id=app_id)
    try:
        if action == "suspend":
            services.set_app_suspended(subject, request.user, True, request.POST.get("reason", ""))
        elif action == "reinstate":
            services.set_app_suspended(subject, request.user, False)
        elif action in ("feature", "unfeature"):
            services.set_app_featured(subject, request.user, action == "feature")
        else:
            raise services.Refused("Unknown action.")
        messages.success(request, f"{subject.name}: done.")
    except services.Refused as refused:
        messages.error(request, str(refused))
    return redirect("operator:apps")


@operator_required
def report(request, pk: int):
    subject = get_object_or_404(Report, pk=pk)
    form = ReportDecisionForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        try:
            services.handle_report(subject, request.user, **form.cleaned_data)
            messages.success(request, "The report is closed.")
            return redirect("operator:home")
        except services.Refused as refused:
            messages.error(request, str(refused))
    return render(request, "operator/report.html", {"report": subject, "form": form,
                                                    "app": App.objects.filter(app_id=subject.target).first()})


@operator_required
def orders(request):
    state = request.GET.get("state", "")
    found = Order.objects.select_related("app__publisher")
    if state in dict(Order.STATES):
        found = found.filter(status=state)
    else:
        found = found.exclude(status__in=[Order.OPEN, Order.CANCELLED])   # what money moved on
    page = Paginator(found, 50).get_page(request.GET.get("page"))
    return render(request, "operator/orders.html", {"page": page, "state": state, "states": Order.STATES,
                                                    "missing": billing.missing_settings()})


@operator_required
def order(request, pk: int):
    subject = get_object_or_404(Order.objects.select_related("app__publisher"), pk=pk)
    if request.method == "POST":
        revoke = {"yes": True, "no": False}.get(request.POST.get("revoke", ""))
        try:
            billing.refund(subject, request.user, amount=request.POST.get("amount", "").strip(), revoke=revoke, note=request.POST.get("note", "").strip())
            messages.success(request, "The refund is made.")
            return redirect("operator:order", pk)
        except billing.BillingError as refused:
            messages.error(request, refused.description)
    subject.refresh_from_db()
    paid = billing.paid_total(subject)
    return render(request, "operator/order.html", {"order": subject, "payments": subject.payments.all(), "left": paid - subject.refunded,
                                                   "history": AuditEntry.objects.filter(target_kind="order", target=subject.public_id)[:30]})


@operator_required
def tax_page(request):
    """Tax: what is missing, the VAT rates, and a quarter's figures by
    country and rate - what the one-stop-shop return asks for."""
    form = VatRateForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        data = form.cleaned_data
        row, made = VatRate.objects.update_or_create(country=data["country"], valid_from=data["valid_from"],
                                                     defaults={"percent": data["percent"], "note": data["note"]})
        services.audit(request.user, "tax.rate", "vat_rate", f"{row.country} {row.valid_from}", percent=str(row.percent), new=made)
        messages.success(request, f"{row.country}: {row.percent} % from {row.valid_from}.")
        return redirect("operator:tax")
    today = timezone.localdate()
    year, quarter = tax.quarter_of(today)
    try:
        year, quarter = int(request.GET.get("year", year)), int(request.GET.get("quarter", quarter))
        if not (2000 <= year <= 2200 and 1 <= quarter <= 4):
            raise ValueError
    except ValueError:
        year, quarter = tax.quarter_of(today)
    current = []
    for code, name in sorted(tax.EU.items(), key=lambda item: item[1]):
        rows = list(VatRate.objects.filter(country=code))
        current.append({"code": code, "name": name, "now": next((r for r in rows if r.valid_from <= today), None),
                        "later": [r for r in rows if r.valid_from > today]})
    return render(request, "operator/tax.html", {
        "form": form, "problems": tax.problems(), "configured": tax.configured(), "countries": current,
        "report": tax.quarter_report(year, quarter),
        "previous": (year, quarter - 1) if quarter > 1 else (year - 1, 4), "following": (year, quarter + 1) if quarter < 4 else (year + 1, 1)})


@operator_required
def payouts_page(request):
    """The payouts: make them for everything due, and see what is to pay."""
    until = payouts.default_until()
    if request.method == "POST":
        try:
            until = dt.date.fromisoformat(request.POST.get("until", ""))
            made = payouts.build(until, request.user)
            messages.success(request, f"{len(made)} payout(s) made for what was due on {until}." if made else f"Nothing was due on {until}.")
            return redirect("operator:payouts")
        except (ValueError, payouts.PayoutError) as refused:
            messages.error(request, str(refused) if isinstance(refused, payouts.PayoutError) else "Give the day as YYYY-MM-DD.")
    state = request.GET.get("state", Payout.OPEN)
    found = Payout.objects.select_related("publisher")
    if state in dict(Payout.STATES):
        found = found.filter(state=state)
    page = Paginator(found, 50).get_page(request.GET.get("page"))
    return render(request, "operator/payouts.html", {"page": page, "state": state, "states": Payout.STATES, "until": until,
                                                     "hold_days": settings.PAYOUT_HOLD_DAYS, "minimum": settings.PAYOUT_MINIMUM})


@operator_required
def payout_page(request, pk: int):
    """One payout: what it holds, where it goes, and marking it paid."""
    subject = get_object_or_404(Payout.objects.select_related("publisher"), pk=pk)
    if request.method == "POST":
        try:
            payouts.mark_paid(subject, request.user, request.POST.get("reference", ""), request.POST.get("note", ""))
            messages.success(request, "The payout is marked paid.")
            return redirect("operator:payout", pk)
        except payouts.PayoutError as refused:
            messages.error(request, str(refused))
        subject.refresh_from_db()
    return render(request, "operator/payout.html", {"payout": subject, **payouts.lines(subject)})


@require_POST
@operator_required
def publish(request):
    snapshot, made = catalog.publish_catalog(request.user, force=request.POST.get("force") == "1")
    messages.success(request, f"Catalog {snapshot.sequence} is published." if made else f"Nothing changed: catalog {snapshot.sequence} stands.")
    return redirect("operator:home")


@operator_required
def audit(request):
    page = Paginator(AuditEntry.objects.all(), 50).get_page(request.GET.get("page"))
    return render(request, "operator/audit.html", {"page": page})
