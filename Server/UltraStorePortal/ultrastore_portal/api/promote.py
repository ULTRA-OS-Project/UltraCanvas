"""UltraAds as a JSON service, for a store's client (the native store, the
web store) that draws its own pages:

    POST /v1/promote/decide          {placement, count?, results?, context: {q, category, listing, os, country}, seen: [campaign ids]}
                                     -> {ads: [{token, kind, id, name, label, payer: {id, name}, why: [...], why_url}]}
    POST /v1/promote/events          {events: [{token, type: shown | clicked | installed}]} -> {accepted}
    GET  /v1/promote/why/<token>     who paid, and what it matched
    GET  /v1/promote/library         every advertisement that runs or ran in the last year
    POST /v1/promote/skrill/status   the payment provider's signed report on a top-up (form fields) -> OK

**The question "what goes here?" is asked without saying who asks.** A
request to decide that carries an Authorization header is refused, so that
no client sends its session along by habit, and nothing here reads a
session or a cookie. The answer depends on the page alone.

Version: 0.5.0
"""

from __future__ import annotations

import json

from django.conf import settings
from django.http import HttpResponse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_POST

from ..billing import services as billing
from ..promote import decide as chooser
from ..promote import services
from ..promote.models import Campaign, Placement
from ..web.utils import client_address, too_many
from .views import answer, error


def _body(request):
    try:
        body = json.loads(request.body or b"{}")
    except ValueError:
        return None
    return body if isinstance(body, dict) else None


def _text(value, limit: int = 120) -> str:
    return value.strip()[:limit] if isinstance(value, str) else ""


@csrf_exempt
@require_POST
def decide(request):
    if request.headers.get("Authorization"):
        return error(400, "session_not_wanted", "Ask what goes in a placement without saying who you are")
    body = _body(request)
    if body is None or not _text(body.get("placement")):
        return error(400, "bad_request", "The body is a JSON object naming the placement")
    context = body.get("context") if isinstance(body.get("context"), dict) else {}
    count = body.get("count") if isinstance(body.get("count"), int) else None
    results = body.get("results") if isinstance(body.get("results"), int) else None
    seen = tuple(pk for pk in (body.get("seen") if isinstance(body.get("seen"), list) else []) if isinstance(pk, int))
    ads = chooser.decide(_text(body["placement"], 40), q=_text(context.get("q")), category=_text(context.get("category"), 32),
                         listing=_text(context.get("listing")), os=_text(context.get("os"), 16).lower(),
                         country=_text(context.get("country"), 2), seen=seen, count=count, results=results)
    return answer({"ads": [{
        "token": ad.token, "kind": "app", "id": ad.app.app_id, "name": ad.app.name, "campaign": ad.campaign.pk, "label": "Sponsored",
        "payer": {"id": ad.campaign.publisher.slug, "name": ad.campaign.publisher.name}, "why": ad.why,
        "why_url": f"{settings.BASE_URL}/promote/why/{ad.token}/",
    } for ad in ads]})


@csrf_exempt
@require_POST
def events(request):
    if too_many(request, "promote-events", 600, 600):
        return error(429, "too_many_requests", "Too many events from this address; try again later")
    body = _body(request)
    listed = body.get("events") if body else None
    if not isinstance(listed, list):
        return error(400, "bad_request", "The body is a JSON object with a list of events")
    accepted = 0
    for event in listed[:100]:
        if not isinstance(event, dict) or not isinstance(event.get("token"), str):
            continue
        kind, token = event.get("type"), event["token"]
        if kind == "shown":
            accepted += services.shown(token)
        elif kind == "clicked":
            accepted += services.clicked(token, address=client_address(request)) is not None
        elif kind == "installed":
            accepted += services.installed(token)
    return answer({"accepted": accepted})


@require_GET
def why(request, token: str):
    data = services.read_token(token, max_age=30 * 24 * 3600)
    campaign = Campaign.objects.select_related("app", "publisher").filter(pk=data["c"]).first() if data else None
    placement = Placement.objects.filter(key=data["p"]).first() if data else None
    if campaign is None or placement is None:
        return error(404, "no_such_ad", "No such advertisement")
    return answer({"label": "Sponsored", "id": campaign.app.app_id, "name": campaign.app.name,
                   "payer": {"id": campaign.publisher.slug, "name": campaign.publisher.name},
                   "placement": {"key": placement.key, "name": placement.name, "area": placement.area},
                   "why": chooser.explain(placement, data.get("w") or {}),
                   "used": "What was on the page. Nothing about you."})


@require_GET
def library(request):
    if not services.enabled():
        return answer({"ads": []})
    return answer({"ads": [{
        "id": row["app"].app_id, "name": row["app"].name, "payer": {"id": row["publisher"].slug, "name": row["publisher"].name},
        "first_shown": row["first"].isoformat(), "last_shown": row["last"].isoformat(), "running": row["running"],
        "places": [place.key for place in row["places"]], "shown_for": row["shown_for"],
    } for row in services.library()]})


@csrf_exempt
@require_POST
def skrill_status(request):
    if settings.PAYMENT_PROVIDER != "skrill":
        return error(404, "not_configured", "Skrill is not the store's payment provider")
    try:
        services.topup_report({key: str(value) for key, value in request.POST.items()})
    except billing.BillingError as refused:
        return error(refused.status, refused.code, refused.description)
    return HttpResponse("OK", content_type="text/plain")
