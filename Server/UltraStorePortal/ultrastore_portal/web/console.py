"""The developer console: a publisher's own pages - the application for a
publisher account, the apps, each app's listing and pictures, its releases
and their packages, and sending a release for review.

Version: 0.1.0
"""

from __future__ import annotations

from django.conf import settings
from django.contrib import messages
from django.contrib.auth.decorators import login_required
from django.core.exceptions import ValidationError
from django.shortcuts import get_object_or_404, redirect, render
from django.views.decorators.http import require_POST

from ..billing.models import Payment
from ..store import services
from ..store.models import App, Package, Release, Screenshot, TermsVersion
from ..store.packages import store_upload
from .forms import AppCreateForm, AppListingForm, PackageForm, PublisherApplyForm, PublisherProfileForm, ReleaseForm, ScreenshotForm
from .utils import publisher_required

CONSOLE = "console/base.html"


@login_required
def home(request):
    publisher = services.publisher_of(request.user)
    if publisher is None:
        return redirect("console:apply")
    if not publisher.approved:
        return render(request, "console/standing.html", {"publisher": publisher})
    apps = publisher.apps.prefetch_related("releases").order_by("name")
    rows = []
    for app in apps:
        releases = list(app.releases.all())
        rows.append({
            "app": app,
            "live": app.latest_release(),
            "in_review": [r for r in releases if r.state == Release.SUBMITTED],
            "attention": [r for r in releases if r.state in (Release.CHANGES, Release.REJECTED)],
            "drafts": [r for r in releases if r.state == Release.DRAFT],
        })
    return render(request, "console/home.html", {"publisher": publisher, "rows": rows, "terms_due": publisher.terms_due()})


@login_required
def apply(request):
    if services.publisher_of(request.user):
        return redirect("console:home")
    form = PublisherApplyForm(request.POST or None, initial={"support_email": request.user.email})
    if request.method == "POST" and form.is_valid():
        try:
            services.apply_for_publisher(request.user, form.save(commit=False))
        except services.Refused as refused:
            form.add_error(None, str(refused))
        else:
            messages.success(request, "Your application is with the store's operator. We mail you the decision.")
            return redirect("console:home")
    return render(request, "console/apply.html", {"form": form, "terms": TermsVersion.current()})


@publisher_required
def publisher_profile(request):
    form = PublisherProfileForm(request.POST or None, instance=request.publisher)
    if request.method == "POST" and form.is_valid():
        form.save()
        services.audit(request.user, "publisher.edit", "publisher", request.publisher.slug, fields=form.changed_data)
        services.republish(request.user)
        messages.success(request, "Saved.")
        return redirect("console:publisher")
    return render(request, "console/publisher.html", {"form": form, "publisher": request.publisher, "terms_due": request.publisher.terms_due()})


@publisher_required
def sales(request):
    """What the publisher's apps sold: every payment and every reversal,
    and the net per currency."""
    payments = (Payment.objects.filter(order__app__publisher=request.publisher).select_related("order__app").order_by("-at", "-id"))
    totals: dict[str, dict] = {}
    for payment in payments:
        row = totals.setdefault(payment.currency, {"currency": payment.currency, "sold": 0, "reversed": 0, "net": 0, "sales": 0})
        if payment.kind in (Payment.SALE, Payment.RENEWAL):
            row["sold"] += payment.amount
            row["sales"] += 1
        else:
            row["reversed"] += payment.amount
        row["net"] += payment.signed_amount
    return render(request, "console/sales.html", {"payments": payments[:500], "totals": list(totals.values())})


@require_POST
@publisher_required
def accept_terms(request):
    services.accept_terms(request.publisher, request.user)
    messages.success(request, "The terms are accepted.")
    return redirect("console:home")


@publisher_required
def app_new(request):
    form = AppCreateForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        app = form.save(commit=False)
        app.publisher = request.publisher
        app.save()
        services.audit(request.user, "app.create", "app", app.app_id, name=app.name)
        return redirect("console:app", app.app_id)
    return render(request, "form_page.html", {
        "layout": CONSOLE, "form": form, "title": "Create an app", "submit": "Create app",
        "lead": "The id and the kind stay as they are once the app exists. Everything else is edited on the listing."})


def own_app(request, app_id: str) -> App:
    return get_object_or_404(App.objects.prefetch_related("releases__packages", "screenshots"), app_id=app_id, publisher=request.publisher)


@publisher_required
def app_overview(request, app_id: str):
    app = own_app(request, app_id)
    return render(request, "console/app.html", {
        "app": app, "releases": app.releases.all(), "problems": services.listing_problems(app), "live": app.latest_release()})


@publisher_required
def app_listing(request, app_id: str):
    app = own_app(request, app_id)
    form = AppListingForm(request.POST or None, request.FILES or None, instance=app)
    if request.method == "POST" and form.is_valid():
        form.save()
        services.audit(request.user, "app.edit", "app", app.app_id, fields=form.changed_data)
        services.republish(request.user)
        messages.success(request, "The listing is saved." + (" It is live in the store." if app.is_public else ""))
        return redirect("console:listing", app.app_id)
    return render(request, "console/listing.html", {
        "app": app, "form": form, "shot_form": ScreenshotForm(), "max_screenshots": settings.MAX_SCREENSHOTS})


@require_POST
@publisher_required
def screenshot_add(request, app_id: str):
    app = own_app(request, app_id)
    form = ScreenshotForm(request.POST, request.FILES)
    if app.screenshots.count() >= settings.MAX_SCREENSHOTS:
        messages.error(request, f"A listing shows at most {settings.MAX_SCREENSHOTS} screenshots.")
    elif form.is_valid():
        shot = form.save(commit=False)
        shot.app = app
        shot.position = (app.screenshots.order_by("-position").values_list("position", flat=True).first() or 0) + 1
        shot.save()
        services.republish(request.user)
        messages.success(request, "The screenshot is added.")
    else:
        messages.error(request, " ".join(e for errors in form.errors.values() for e in errors))
    return redirect("console:listing", app.app_id)


@require_POST
@publisher_required
def screenshot_delete(request, app_id: str, pk: int):
    app = own_app(request, app_id)
    shot = get_object_or_404(Screenshot, pk=pk, app=app)
    shot.image.delete(save=False)
    shot.delete()
    services.republish(request.user)
    return redirect("console:listing", app.app_id)


@publisher_required
def release_new(request, app_id: str):
    app = own_app(request, app_id)
    form = ReleaseForm(request.POST or None, app=app)
    if request.method == "POST" and form.is_valid():
        release = form.save()
        services.audit(request.user, "release.create", "release", str(release))
        return redirect("console:release", app.app_id, release.pk)
    return render(request, "form_page.html", {
        "layout": CONSOLE, "app": app, "form": form, "title": f"New release of {app.name}", "submit": "Create release",
        "lead": "A release is one version. Create it, add a package per system, then send it for review."})


def own_release(request, app_id: str, pk: int) -> Release:
    return get_object_or_404(Release.objects.select_related("app__publisher"), pk=pk, app__app_id=app_id, app__publisher=request.publisher)


@publisher_required
def release_detail(request, app_id: str, pk: int):
    release = own_release(request, app_id, pk)
    form = ReleaseForm(request.POST or None, instance=release) if release.editable else None
    if request.method == "POST" and form is not None and form.is_valid():
        form.save()
        messages.success(request, "Saved.")
        return redirect("console:release", app_id, pk)
    return render(request, "console/release.html", {
        "app": release.app, "release": release, "form": form, "packages": release.packages.all(),
        "package_form": PackageForm(release=release) if release.editable else None,
        "problems": (services.listing_problems(release.app) + services.release_problems(release)) if release.editable else [],
        "max_mb": settings.MAX_PACKAGE_MB})


@require_POST
@publisher_required
def package_add(request, app_id: str, pk: int):
    release = own_release(request, app_id, pk)
    form = PackageForm(request.POST, request.FILES, release=release)
    if not release.editable:
        messages.error(request, "A release in review or published takes no more packages: make a new version.")
    elif form.is_valid():
        try:
            stored = store_upload(form.cleaned_data["file"])
            package = form.save(commit=False)
            package.sha256, package.size, package.format = stored.sha256, stored.size, stored.format
            package.original_name = (form.cleaned_data["file"].name or "")[:255]
            services.add_package(release, package, stored.names, request.user)
            messages.success(request, f"The {package.os}/{package.arch} package is added.")
        except ValidationError as error:
            messages.error(request, " ".join(error.messages))
        except services.Refused as refused:
            messages.error(request, str(refused))
    else:
        messages.error(request, " ".join(e for errors in form.errors.values() for e in errors))
    return redirect("console:release", app_id, pk)


@require_POST
@publisher_required
def package_delete(request, app_id: str, pk: int, package_pk: int):
    release = own_release(request, app_id, pk)
    try:
        services.delete_package(get_object_or_404(Package, pk=package_pk, release=release), request.user)
    except services.Refused as refused:
        messages.error(request, str(refused))
    return redirect("console:release", app_id, pk)


@require_POST
@publisher_required
def release_action(request, app_id: str, pk: int, action: str):
    release = own_release(request, app_id, pk)
    try:
        if action == "submit":
            services.submit_release(release, request.user)
            messages.success(request, f"{release.app.name} {release.version} is sent for review. We mail you the decision.")
        elif action == "withdraw":
            services.withdraw_release(release, request.user)
            messages.success(request, "The release is withdrawn.")
        elif action == "delete":
            services.delete_release(release, request.user)
            messages.success(request, "The release is deleted.")
            return redirect("console:app", app_id)
        else:
            messages.error(request, "Unknown action.")
    except services.Refused as refused:
        messages.error(request, str(refused))
    return redirect("console:release", app_id, pk)

