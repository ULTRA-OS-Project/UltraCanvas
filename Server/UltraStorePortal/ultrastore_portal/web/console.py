"""The developer console: a publisher's own pages - the application for a
publisher account, the apps, each app's listing and pictures, its releases
and their packages, and sending a release for review.

What changes a page in place - a picture uploaded, a package added, edited
or removed - is a form the page's script sends (portal.js; the request says
X-Requested-With: XMLHttpRequest). Such a request is answered with JSON:
{"ok": true, "fragments": {selector: html}} names the parts of the page to
draw again, {"ok": false, "error" | "errors": {field: [...]}} what was wrong.
The same address sent by a browser without the script answers with a
redirect and a message, as every other form here does.

Version: 0.9.0 - Sales shows the tax and the payouts; a payout's statement;
                 the owner says where the share is paid
Version: 0.7.0 - Sales shows each payment's split and the publisher's share
Version: 0.3.0 - pictures uploaded where they are chosen and packages added,
                 edited and removed without leaving the page; no release
                 before the listing has what a review needs
Version: 0.1.0
"""

from __future__ import annotations

from django.conf import settings
from django.contrib import messages
from django.contrib.auth.decorators import login_required
from django.core.exceptions import PermissionDenied, ValidationError
from django.http import JsonResponse
from django.shortcuts import get_object_or_404, redirect, render
from django.template.loader import render_to_string
from django.urls import reverse
from django.views.decorators.http import require_POST

from ..billing import payouts, split, tax
from ..billing.models import Payment, Payout
from ..store.models import PublisherMember
from ..store import services
from ..store.models import App, Package, Release, Screenshot, TermsVersion
from ..store.packages import store_upload
from .forms import (AppCreateForm, AppListingForm, IconForm, PackageEditForm, PackageForm, PublisherApplyForm, PublisherProfileForm,
                    ReleaseForm, ScreenshotForm)
from .utils import publisher_required

CONSOLE = "console/base.html"


def in_place(request) -> bool:
    """Whether the page's script sent this and draws the answer itself."""
    return request.headers.get("X-Requested-With") == "XMLHttpRequest"


def done(fragments: dict[str, str], **more) -> JsonResponse:
    return JsonResponse({"ok": True, "fragments": fragments, **more})


def refused(error: str = "", errors: dict | None = None, status: int = 400) -> JsonResponse:
    return JsonResponse({"ok": False, "error": error, "errors": errors or {}}, status=status)


def errors_of(form) -> dict[str, list[str]]:
    """A form's errors by field; what belongs to no field is under ""."""
    return {("" if name == "__all__" else name): [str(e) for e in errors] for name, errors in form.errors.items()}


def first_error(form) -> str:
    return " ".join(e for errors in form.errors.values() for e in errors)


@login_required
def home(request):
    membership = services.membership_of(request.user)
    if membership is None:
        return redirect("console:apply")
    publisher = membership.publisher
    if not publisher.approved:
        return render(request, "console/standing.html", {"publisher": publisher})
    if membership.role not in membership.APPS:   # Marketing, Billing, Analyst: advertising is what they are here for
        if settings.PROMOTE:
            return redirect("promote:home")
        raise PermissionDenied
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
    form = PublisherProfileForm(request.POST or None, instance=request.publisher, owner=request.publisher_role == PublisherMember.OWNER)
    if request.method == "POST" and form.is_valid():
        form.save()
        services.audit(request.user, "publisher.edit", "publisher", request.publisher.slug, fields=form.changed_data)
        services.republish(request.user)
        messages.success(request, "Saved.")
        return redirect("console:publisher")
    return render(request, "console/publisher.html", {"form": form, "publisher": request.publisher, "terms_due": request.publisher.terms_due()})


@publisher_required
def sales(request):
    """What the publisher's apps sold: every payment and every reversal
    with its split, and per currency what buyers paid, the store's fee and
    the publisher's share (billing/split.py)."""
    payments = (Payment.objects.filter(order__app__publisher=request.publisher).select_related("order__app").order_by("-at", "-id"))
    totals: dict[str, dict] = {}
    for payment in payments:
        row = totals.setdefault(payment.currency, {"currency": payment.currency, "sold": 0, "reversed": 0, "net": 0, "sales": 0, "fee": 0, "share": 0})
        if payment.kind in (Payment.SALE, Payment.RENEWAL):
            row["sold"] += payment.amount
            row["sales"] += 1
        else:
            row["reversed"] += payment.amount
        row["net"] += payment.signed_amount
        row["fee"] += payment.signed_store_fee
        row["share"] += payment.signed_publisher_share
    for row in totals.values():
        row["vat"] = row["net"] - row["fee"] - row["share"]   # what of the net was tax
    return render(request, "console/sales.html", {
        "payments": payments[:500], "totals": list(totals.values()), "fee_percent": split.percent_text(split.fee_percent()),
        "taxed": tax.configured() or any(row["vat"] for row in totals.values()), "owed": payouts.owed(request.publisher),
        "payouts": Payout.objects.filter(publisher=request.publisher)[:60], "hold_days": settings.PAYOUT_HOLD_DAYS,
        "payout_details": request.publisher.payout_details})


@publisher_required
def payout(request, pk: int):
    """A payout's statement: what it paid, payment by payment."""
    subject = get_object_or_404(Payout, pk=pk, publisher=request.publisher)
    return render(request, "console/payout.html", {"payout": subject, **payouts.lines(subject)})


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


def listing_context(app: App) -> dict:
    """What the listing page's parts that change in place are drawn from."""
    return {"app": app, "shots": list(app.screenshots.all()), "max_screenshots": settings.MAX_SCREENSHOTS, "max_image_mb": settings.MAX_IMAGE_MB,
            "problems": services.listing_problems(app), "has_releases": app.releases.exists()}


def listing_fragments(request, app_id: str) -> dict[str, str]:
    """The icon, the screenshots and what the listing still lacks, drawn
    again from the records as they are now."""
    context = listing_context(own_app(request, app_id))
    return {"#listing-problems": render_to_string("console/_listing_problems.html", context, request),
            "#icon": render_to_string("console/_icon.html", context, request),
            "#shots": render_to_string("console/_screenshots.html", context, request)}


@publisher_required
def app_listing(request, app_id: str):
    app = own_app(request, app_id)
    form = AppListingForm(request.POST or None, instance=app)
    if request.method == "POST" and form.is_valid():
        form.save()
        services.audit(request.user, "app.edit", "app", app.app_id, fields=form.changed_data)
        services.republish(request.user)
        messages.success(request, "The listing is saved." + (" It is live in the store." if app.is_public else ""))
        return redirect("console:listing", app.app_id)
    return render(request, "console/listing.html", {**listing_context(app), "form": form, "icon_form": IconForm(), "shot_form": ScreenshotForm()})


def picture_answer(request, app_id: str, error: str, success: str):
    """The answer to a picture uploaded or removed: the page's parts for
    the script, a message and the listing page for a plain form."""
    if in_place(request):
        return refused(error) if error else done(listing_fragments(request, app_id))
    if error:
        messages.error(request, error)
    elif success:
        messages.success(request, success)
    return redirect("console:listing", app_id)


@require_POST
@publisher_required
def icon_upload(request, app_id: str):
    """The icon, saved as soon as it is chosen - the listing's other fields
    are a form of their own and are not touched."""
    app = own_app(request, app_id)
    form = IconForm(request.POST, request.FILES, instance=app)
    if not form.is_valid():
        return picture_answer(request, app_id, first_error(form), "")
    form.save()
    services.audit(request.user, "app.edit", "app", app.app_id, fields=["icon"])
    services.republish(request.user)
    return picture_answer(request, app_id, "", "The icon is saved.")


@require_POST
@publisher_required
def screenshot_add(request, app_id: str):
    app = own_app(request, app_id)
    form = ScreenshotForm(request.POST, request.FILES)
    if app.screenshots.count() >= settings.MAX_SCREENSHOTS:
        return picture_answer(request, app_id, f"A listing shows at most {settings.MAX_SCREENSHOTS} screenshots.", "")
    if not form.is_valid():
        return picture_answer(request, app_id, first_error(form), "")
    shot = form.save(commit=False)
    shot.app = app
    shot.position = (app.screenshots.order_by("-position").values_list("position", flat=True).first() or 0) + 1
    shot.save()
    services.republish(request.user)
    return picture_answer(request, app_id, "", "The screenshot is added.")


@require_POST
@publisher_required
def screenshot_delete(request, app_id: str, pk: int):
    app = own_app(request, app_id)
    shot = get_object_or_404(Screenshot, pk=pk, app=app)
    shot.image.delete(save=False)
    shot.delete()
    services.republish(request.user)
    return picture_answer(request, app_id, "", "")


@publisher_required
def release_new(request, app_id: str):
    app = own_app(request, app_id)
    if services.listing_problems(app):
        # A release goes to review with the listing; there is nothing to
        # send before the listing has what a review needs.
        messages.warning(request, "Fill in the store listing first: a release is made once the listing has what a review needs.")
        return redirect("console:listing", app.app_id)
    form = ReleaseForm(request.POST or None, app=app)
    if request.method == "POST" and form.is_valid():
        release = form.save()
        services.audit(request.user, "release.create", "release", str(release))
        return redirect("console:release", app.app_id, release.pk)
    return render(request, "form_page.html", {
        "layout": CONSOLE, "app": app, "form": form, "title": f"New release of {app.name}", "submit": "Create release",
        "cancel_url": reverse("console:app", args=[app.app_id]),
        "lead": "A release is one version. Create it, add a package per system, then send it for review."})


def own_release(request, app_id: str, pk: int) -> Release:
    return get_object_or_404(Release.objects.select_related("app__publisher"), pk=pk, app__app_id=app_id, app__publisher=request.publisher)


def release_context(release: Release) -> dict:
    """What the release page's parts that change in place are drawn from."""
    return {"app": release.app, "release": release, "packages": list(release.packages.all()),
            "problems": (services.listing_problems(release.app) + services.release_problems(release)) if release.editable else []}


def release_fragments(request, release: Release) -> dict[str, str]:
    """The packages, what the release still lacks and the buttons that
    depend on it, drawn again from the records as they are now."""
    context = release_context(release)
    return {"#release-actions": render_to_string("console/_release_actions.html", context, request),
            "#release-problems": render_to_string("console/_release_problems.html", context, request),
            "#packages": render_to_string("console/_packages.html", context, request)}


@publisher_required
def release_detail(request, app_id: str, pk: int):
    release = own_release(request, app_id, pk)
    form = ReleaseForm(request.POST or None, instance=release) if release.editable else None
    if request.method == "POST" and form is not None and form.is_valid():
        form.save()
        messages.success(request, "Saved.")
        return redirect("console:release", app_id, pk)
    return render(request, "console/release.html", {
        **release_context(release), "form": form,
        "package_form": PackageForm(release=release) if release.editable else None, "max_mb": settings.MAX_PACKAGE_MB})


def package_answer(request, release: Release, errors: dict[str, list[str]], success: str, **more):
    """The answer to a package added, edited or removed."""
    if in_place(request):
        return refused(errors=errors) if errors else done(release_fragments(request, release), **more)
    if errors:
        messages.error(request, " ".join(e for found in errors.values() for e in found))
    elif success:
        messages.success(request, success)
    return redirect("console:release", release.app.app_id, release.pk)


@require_POST
@publisher_required
def package_add(request, app_id: str, pk: int):
    release = own_release(request, app_id, pk)
    form = PackageForm(request.POST, request.FILES, release=release)
    if not release.editable:
        return package_answer(request, release, {"": ["A release in review or published takes no more packages: make a new version."]}, "")
    if not form.is_valid():
        return package_answer(request, release, errors_of(form), "")
    try:
        stored = store_upload(form.cleaned_data["file"])
    except ValidationError as error:
        return package_answer(request, release, {"file": list(error.messages)}, "")
    package = form.save(commit=False)
    package.sha256, package.size, package.format = stored.sha256, stored.size, stored.format
    package.original_name = (form.cleaned_data["file"].name or "")[:255]
    try:
        services.add_package(release, package, stored.names, request.user)
    except services.Refused as refusal:
        return package_answer(request, release, {"entry": [str(refusal)]}, "")
    return package_answer(request, release, {}, f"The {package.os}/{package.arch} package is added.", reset=True)


@publisher_required
def package_edit(request, app_id: str, pk: int, package_pk: int):
    """A package's system, processor, needs and entry corrected while the
    release is a draft or has changes requested - a slip there no longer
    means removing the package and uploading the archive again. Opened by
    the release page in a dialog (the form alone), and a page of its own
    without the script."""
    release = own_release(request, app_id, pk)
    package = get_object_or_404(Package, pk=package_pk, release=release)
    if not release.editable:
        return package_answer(request, release, {"": ["A release in review or published keeps its packages as they are."]}, "")
    form = PackageEditForm(request.POST or None, instance=package)
    if request.method == "POST" and form.is_valid():
        try:
            services.edit_package(form.save(commit=False), request.user, form.changed_data)
        except services.Refused as refusal:
            form.add_error("entry" if "entry" in form.fields else None, str(refusal))
        else:
            return package_answer(request, release, {}, f"The {package.os}/{package.arch} package is saved.", close=True)
    if in_place(request) and request.method == "POST":
        return refused(errors=errors_of(form))
    context = {"app": release.app, "release": release, "package": package, "form": form, "dialog": in_place(request)}
    return render(request, "console/_package_edit.html" if in_place(request) else "console/package_edit.html", context)


@require_POST
@publisher_required
def package_delete(request, app_id: str, pk: int, package_pk: int):
    release = own_release(request, app_id, pk)
    try:
        services.delete_package(get_object_or_404(Package, pk=package_pk, release=release), request.user)
    except services.Refused as refusal:
        return package_answer(request, release, {"": [str(refusal)]}, "")
    return package_answer(request, release, {}, "")


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

