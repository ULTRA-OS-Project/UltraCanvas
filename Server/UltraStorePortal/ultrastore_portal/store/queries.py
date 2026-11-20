"""What the store's pages and the JSON service list: one search, so the
web page and a buyer's client see the same apps in the same order.

Version: 0.1.0
"""

from __future__ import annotations

from django.db.models import Q

from . import constants
from .models import App, Release

SORTS = {"updated": ["-last_published", "name"], "new": ["-first_published", "name"], "name": ["name"], "downloads": ["-downloads", "name"]}


def listed_apps():
    return App.objects.listed().select_related("publisher").prefetch_related("releases__packages")


def search_apps(params):
    """The listed apps the parameters ask for: q (words in the name, the
    summary, the tags, the id, the publisher), category, publisher, price
    (free, paid), os, sort (updated, new, name, downloads)."""
    apps = listed_apps()
    q = (params.get("q") or "").strip()
    for word in q.split()[:6]:
        apps = apps.filter(Q(name__icontains=word) | Q(summary__icontains=word) | Q(tags__icontains=word)
                           | Q(app_id__icontains=word) | Q(publisher__name__icontains=word))
    category = (params.get("category") or "").strip().lower()
    if category in constants.CATEGORY_NAMES:
        apps = apps.filter(category=category)
    elif category:
        apps = apps.none()
    publisher = (params.get("publisher") or "").strip().lower()
    if publisher:
        apps = apps.filter(publisher__slug=publisher)
    price = (params.get("price") or "").strip().lower()
    if price == "free":
        apps = apps.filter(pricing_model="free")
    elif price == "paid":
        apps = apps.exclude(pricing_model="free")
    system = (params.get("os") or "").strip().lower()
    if system:
        apps = apps.filter(releases__state=Release.PUBLISHED, releases__packages__os=system)
    return apps.order_by(*SORTS.get((params.get("sort") or "").strip().lower(), SORTS["updated"])).distinct()
