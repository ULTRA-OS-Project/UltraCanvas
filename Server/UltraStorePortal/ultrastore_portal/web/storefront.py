"""The store's public pages: the front page, browsing and searching, an
app's page, a publisher's page.

Version: 0.5.0 - the marked advertisements of a page (promote/): the Discover
                 row, a search and a category list, beside a listing
Version: 0.1.0
"""

from __future__ import annotations

from django.conf import settings
from django.core.paginator import Paginator
from django.db.models import F
from django.http import Http404, HttpResponse
from django.shortcuts import get_object_or_404, redirect, render
from django.urls import reverse
from django.utils import timezone

from ..billing import services as billing
from ..promote import services as advertising
from ..promote.decide import for_page as ads_for
from ..store import constants
from ..store.models import App, Package, Publisher, Release, Report, TermsVersion
from ..store.queries import listed_apps, search_apps
from .forms import ReportForm
from .utils import too_many

PAGE_SIZE = 24


def home(request):
    apps = list(listed_apps().order_by("-last_published", "name")[:200])
    featured = [a for a in apps if a.featured]
    by_category = []
    for category, name in constants.CATEGORIES:
        row = [a for a in apps if a.category == category]
        if row:
            by_category.append((category, name, row[:8]))
    return render(request, "store/home.html", {
        "hero": featured[0] if featured else (apps[0] if apps else None),
        "featured": featured[:8],
        "latest": apps[:8],
        "free": [a for a in apps if a.is_free][:8],
        "by_category": by_category,
        "categories_in_use": [(c, n) for c, n, _ in by_category],
        "ads": ads_for("store-home") if apps else [],
    })


def browse(request, category: str = ""):
    params = request.GET.copy()
    if category:
        if category not in constants.CATEGORY_NAMES:
            raise Http404
        params["category"] = category
    page = Paginator(search_apps(params), PAGE_SIZE).get_page(params.get("page"))
    query = params.copy()
    query.pop("page", None)
    if category:
        query.pop("category", None)
    # The advertisements of this page, chosen from the search or the
    # category and never from who is asking; the results above are made
    # first and stay as they are.
    q, ads = (params.get("q") or "").strip(), []
    if page.number == 1:
        # A short list shows every result at a glance: the same app again
        # above it, marked, would tell nobody anything.
        found = page.paginator.count
        skip = tuple(app.app_id for app in page) if found < int(advertising.number("results_per_ad")) else ()
        if q:
            ads = ads_for("store-search", q=q, results=found, skip=skip)
        elif params.get("category"):
            ads = ads_for("store-category", category=params["category"], results=found, skip=skip)
    return render(request, "store/browse.html", {
        "ads": ads, "page": page, "q": (params.get("q") or "").strip(), "category": params.get("category", ""),
        "category_name": constants.CATEGORY_NAMES.get(params.get("category", ""), ""),
        "price": params.get("price", ""), "sort": params.get("sort", "updated"), "query": query.urlencode(),
    })


def public_app_or_404(app_id: str) -> App:
    """An app the store shows: listed, or reached by its link."""
    app = get_object_or_404(App.objects.select_related("publisher").prefetch_related("releases__packages", "screenshots"), app_id=app_id)
    if not app.is_public:
        raise Http404
    return app


def app_detail(request, app_id: str):
    app = public_app_or_404(app_id)
    release = app.latest_release()
    packages = list(release.packages.all()) if release else []
    more = [a for a in listed_apps().filter(publisher=app.publisher).exclude(pk=app.pk)[:4]]
    similar = [a for a in listed_apps().filter(category=app.category).exclude(pk=app.pk).exclude(publisher=app.publisher)[:4]] if app.category else []
    return render(request, "store/app_detail.html", {
        "app": app, "release": release, "packages": packages,
        "systems": sorted({p.os_name for p in packages}),
        "direct_download": not app.restricted_packages,
        "more": more, "similar": similar, "ads": ads_for("store-detail", listing=app.app_id),
        "can_buy": not app.is_free and billing.selling(),
        "deep_link": f"ultrastore://app/{app.app_id}",
    })


def download(request, app_id: str, pk: int):
    """A package of a published release, by the browser: counted, then the
    file the catalog names."""
    app = public_app_or_404(app_id)
    package = get_object_or_404(Package, pk=pk, release__app=app, release__state=Release.PUBLISHED)
    if app.restricted_packages:
        raise Http404
    Package.objects.filter(pk=package.pk).update(downloads=F("downloads") + 1)
    App.objects.filter(pk=app.pk).update(downloads=F("downloads") + 1)
    from . import promote   # a download that followed a click on this listing's advertisement counts for it
    if promote.clicked_token(request, app):
        promote.services.installed(promote.clicked_token(request, app))
    return redirect(f"/v1/packages/{package.file_name}")


def publisher_page(request, slug: str):
    publisher = get_object_or_404(Publisher, slug=slug, state=Publisher.APPROVED)
    return render(request, "store/publisher.html", {"publisher": publisher, "apps": listed_apps().filter(publisher=publisher)})


def report(request, app_id: str):
    app = public_app_or_404(app_id)
    form = ReportForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        if too_many(request, "report", 10, 3600):
            form.add_error(None, "Too many reports from this address. Try again later.")
        else:
            made = Report.objects.create(target_kind="app", target=app.app_id, **form.cleaned_data)
            outcome = settings.BASE_URL + reverse("store:report_outcome", args=[made.public_id]) + f"?secret={made.secret}"
            return render(request, "store/report_done.html", {"app": app, "outcome_url": outcome})
    return render(request, "form_page.html", {
        "form": form, "title": f"Report a problem with {app.name}", "submit": "Send the report",
        "lead": "The report goes to the store's operator. The publisher sees the report, never who sent it."})


def report_outcome(request, public_id: str):
    made = get_object_or_404(Report, public_id=public_id)
    if request.GET.get("secret") != made.secret:
        raise Http404
    return render(request, "store/report_outcome.html", {"report": made})


def terms(request):
    return render(request, "store/terms.html", {"terms": TermsVersion.current()})


def robots(request):
    lines = ["User-agent: *", "Disallow: /console/", "Disallow: /operator/", "Disallow: /admin/", "Disallow: /account/", "Disallow: /promote/",
             f"Sitemap: {settings.BASE_URL}/sitemap.xml", ""]
    return HttpResponse("\n".join(lines), content_type="text/plain")


def sitemap(request):
    urls = [(settings.BASE_URL + "/", timezone.now())]
    urls += [(settings.BASE_URL + a.get_absolute_url(), a.last_published or a.updated) for a in listed_apps()]
    body = ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    body += [f"<url><loc>{loc}</loc><lastmod>{when:%Y-%m-%d}</lastmod></url>" for loc, when in urls]
    body.append("</urlset>")
    return HttpResponse("\n".join(body), content_type="application/xml")
