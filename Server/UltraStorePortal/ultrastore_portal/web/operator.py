"""The operator's pages: the queue of publishers that applied and of
releases in review, the decision on each, the reports, the catalog and the
audit log. Staff only.

Version: 0.1.0
"""

from __future__ import annotations

from django.contrib import messages
from django.core.paginator import Paginator
from django.http import FileResponse, Http404
from django.shortcuts import get_object_or_404, redirect, render
from django.views.decorators.http import require_POST

from ..store import catalog, services
from ..store.models import App, AuditEntry, Package, Publisher, Release, Report
from ..store.packages import package_path
from .forms import DecisionForm, ReportDecisionForm
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
