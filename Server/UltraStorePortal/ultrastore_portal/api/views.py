"""The store as a JSON service, under <root>/v1 - what a buyer's client
reads:

    GET  /v1/catalog.json, /v1/catalog.json.sig   the whole signed catalog (schema 2,
                                                  Apps/UltraStore/core/Catalog.cpp), with an ETag
    GET  /v1/apps?q=&category=&publisher=&price=&os=&sort=&page=&per_page=
    GET  /v1/apps/<id>                            one app as the catalog carries it
    GET  /v1/categories                           the fixed list, with how many apps each has
    GET  /v1/publishers/<id>                      a publisher and its apps
    GET  /v1/publish/key                          the public key the catalog is signed with
    GET  /v1/terms/current                        the publisher terms in force
    POST /v1/reports                              a problem reported from an app's page
    GET  /v1/reports/<id>/outcome?secret=         the one line the reporter learns
    GET  /healthz

Errors are {"error", "error_description"} with a 4xx status. Everything a
GET answers is public, so any origin may read it.

Version: 0.1.0
"""

from __future__ import annotations

import json

from django.conf import settings
from django.core.paginator import EmptyPage, Paginator
from django.db import connection
from django.http import HttpResponse, HttpResponseNotModified, JsonResponse
from django.urls import reverse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_http_methods

from ..store import catalog, constants
from ..store.models import App, Publisher, Report, TermsVersion
from ..store.queries import listed_apps, search_apps
from ..web.utils import too_many


def answer(data, status: int = 200) -> JsonResponse:
    response = JsonResponse(data, status=status, json_dumps_params={"ensure_ascii": False}, safe=False)
    response["Access-Control-Allow-Origin"] = "*"
    return response


def error(status: int, code: str, description: str) -> JsonResponse:
    return answer({"error": code, "error_description": description}, status)


def _signed(request, body: bytes, content_type: str, snapshot) -> HttpResponse:
    etag = f'"{snapshot.sequence}-{snapshot.digest[:16]}"'
    if request.headers.get("If-None-Match") == etag:
        response = HttpResponseNotModified()
    else:
        response = HttpResponse(body, content_type=content_type)
    response["ETag"] = etag
    response["Cache-Control"] = "public, max-age=60"
    response["Access-Control-Allow-Origin"] = "*"
    return response


@require_GET
def catalog_json(request):
    snapshot = catalog.current_snapshot()
    return _signed(request, bytes(snapshot.body), "application/json", snapshot)


@require_GET
def catalog_signature(request):
    snapshot = catalog.current_snapshot()
    return _signed(request, (snapshot.signature + "\n").encode(), "text/plain; charset=utf-8", snapshot)


def card(app: App) -> dict:
    """An app as a list shows it."""
    release = app.latest_release()
    packages = list(release.packages.all()) if release else []
    out = catalog.app_object(app)
    return {
        "id": app.app_id, "name": app.name, "publisher": {"id": app.publisher.slug, "name": app.publisher.name},
        "summary": app.summary, "category": app.category, "icon": out.get("icon", ""), "pricing": out["pricing"],
        "featured": app.featured, "version": release.version if release else "",
        "updated": release.published_at.strftime("%Y-%m-%d") if release and release.published_at else "",
        "systems": sorted({p.os for p in packages}),
        "url": settings.BASE_URL + reverse("api:app", args=[app.app_id]),
        "web_url": settings.BASE_URL + app.get_absolute_url(),
    }


@require_GET
def apps(request):
    try:
        per_page = min(max(int(request.GET.get("per_page", "24")), 1), 100)
        number = max(int(request.GET.get("page", "1")), 1)
    except ValueError:
        return error(400, "bad_request", "page and per_page are numbers")
    paginator = Paginator(search_apps(request.GET), per_page)
    try:
        page = paginator.page(number)
    except EmptyPage:
        return answer({"count": paginator.count, "page": number, "pages": paginator.num_pages, "results": []})
    return answer({"count": paginator.count, "page": number, "pages": paginator.num_pages, "results": [card(a) for a in page]})


@require_GET
def app(request, app_id: str):
    found = catalog.public_apps().filter(app_id=app_id).first()
    if found is None:
        return error(404, "no_such_app", "The store has no app with this id")
    out = catalog.app_object(found)
    out["publisher_name"] = found.publisher.name
    out["web_url"] = settings.BASE_URL + found.get_absolute_url()
    return answer(out)


@require_GET
def categories(request):
    counts: dict[str, int] = {}
    for category in listed_apps().values_list("category", flat=True):
        counts[category] = counts.get(category, 0) + 1
    return answer([{"id": c, "name": name, "apps": counts.get(c, 0)} for c, name in constants.CATEGORIES])


@require_GET
def publisher(request, slug: str):
    found = Publisher.objects.filter(slug=slug, state=Publisher.APPROVED).first()
    if found is None:
        return error(404, "no_such_publisher", "The store has no publisher with this id")
    return answer({"id": found.slug, "name": found.name, "url": found.website, "description": found.description,
                   "web_url": settings.BASE_URL + found.get_absolute_url(),
                   "apps": [card(a) for a in listed_apps().filter(publisher=found)]})


@require_GET
def publish_key(request):
    key = catalog.signing_key()
    return answer({"public_key": key.public_hex, "trusted_keys": catalog.trusted_keys(key)})


@require_GET
def terms_current(request):
    terms = TermsVersion.current()
    if terms is None:
        return error(404, "no_terms", "No terms are in force")
    return answer({"version": terms.version, "text": terms.text, "changes": terms.changes, "in_force_from": terms.in_force_from.isoformat()})


@csrf_exempt
@require_http_methods(["POST", "OPTIONS"])
def reports(request):
    """A report from a store's app page: {target_kind, target, kind, text,
    reporter?} -> {id, secret} (Apps/UltraStore/core/Report.cpp)."""
    if request.method == "OPTIONS":
        response = HttpResponse(status=204)
        response["Access-Control-Allow-Origin"] = "*"
        response["Access-Control-Allow-Methods"] = "POST"
        response["Access-Control-Allow-Headers"] = "Content-Type"
        return response
    if too_many(request, "report", 10, 3600):
        return error(429, "too_many_requests", "Too many reports from this address; try again later")
    try:
        body = json.loads(request.body or b"{}")
    except ValueError:
        body = None
    if not isinstance(body, dict):
        return error(400, "bad_request", "The body is a JSON object")
    target_kind = str(body.get("target_kind") or "app")
    target = str(body.get("target") or "").strip()
    text = str(body.get("text") or "").strip()
    if target_kind not in dict(Report.TARGET_KINDS) or not target or not text:
        return error(400, "bad_request", "A report names its target_kind (app, release, merchant), its target and a text")
    if len(text) > 4000 or len(target) > 160:
        return error(400, "bad_request", "The text is at most 4000 characters")
    made = Report.objects.create(target_kind=target_kind, target=target, kind=str(body.get("kind") or "other")[:32],
                                 text=text, reporter=str(body.get("reporter") or "")[:255])
    return answer({"id": made.public_id, "secret": made.secret, "state": made.state}, 201)


@require_GET
def report_outcome(request, public_id: str):
    made = Report.objects.filter(public_id=public_id).first()
    if made is None or request.GET.get("secret") != made.secret:
        return error(404, "no_such_report", "No report with this id and secret")
    return answer({"id": made.public_id, "state": made.state, "outcome": made.outcome})


@require_GET
def healthz(request):
    try:
        with connection.cursor() as cursor:
            cursor.execute("SELECT 1")
    except Exception as failure:   # noqa: BLE001 - whatever the driver raises
        return JsonResponse({"status": "database", "error_description": str(failure)}, status=503)
    snapshot = catalog.latest_snapshot()
    return JsonResponse({"status": "ok", "catalog_sequence": snapshot.sequence if snapshot else 0})
