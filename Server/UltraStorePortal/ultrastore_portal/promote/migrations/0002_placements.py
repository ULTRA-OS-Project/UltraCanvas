"""The placements: the app store's, open, and one group for every shop and
marketplace of the design that is not built yet - listed for publishers as
"not open yet" on the Promote page, so that the list of where an
advertisement could appear is whole from the first day.

The floors are starting values for the operator to set (the operator's
Advertising page), not decided prices."""

from decimal import Decimal

from django.db import migrations

OPEN, PLANNED = "open", "planned"
ROWS = [
    # key, area, page, name, what an advertisement looks like there, at the most, floor, state
    ("store-search", "store", "search", "In search results", "The app's own card among the results of a search it answers, marked Sponsored.", 2, "0.10", OPEN),
    ("store-category", "store", "category", "In category lists", "The app's own card at the top of its category's list, marked Sponsored.", 2, "0.08", OPEN),
    ("store-home", "store", "home", "The Sponsored row on the front page", "The app's own card in a row of its own below the Editor's picks.", 4, "0.05", OPEN),
    ("store-detail", "store", "detail", "Beside similar apps", "The app's own card under \"Sponsored - similar apps\" on the page of an app of its category.", 2, "0.06", OPEN),
    ("games-search", "games", "search", "In search results", "The game's own card among the results of a search.", 2, "0.10", PLANNED),
    ("games-genre", "games", "category", "In genre lists", "The game's own card at the top of its genre's list.", 2, "0.08", PLANNED),
    ("games-home", "games", "home", "The Sponsored row on the front page", "The game's own card in a row of its own.", 4, "0.05", PLANNED),
    ("market-search", "market", "search", "In search results", "The product's own listing among the results of a search.", 3, "0.15", PLANNED),
    ("market-department", "market", "category", "In department lists", "The product's own listing at the top of its department.", 3, "0.12", PLANNED),
    ("market-product", "market", "detail", "Beside related items", "The product's own listing under \"Sponsored related items\" on a product's page.", 4, "0.10", PLANNED),
    ("market-home", "market", "home", "On the front page", "The product's own listing in a marked row.", 4, "0.08", PLANNED),
    ("trade-search", "trade", "search", "In product search", "The product's own listing among the results of a company's search.", 2, "0.40", PLANNED),
    ("trade-suppliers", "trade", "search", "In supplier search", "The supplier's own profile among the results. Never among the quotations of a request.", 2, "0.60", PLANNED),
    ("video-grid", "video", "home", "In the video grid", "The video's or the channel's own card in the grid.", 2, "0.05", PLANNED),
    ("video-next", "video", "detail", "In Up next", "The video's own card in the list beside a video.", 1, "0.05", PLANNED),
    ("music-home", "music", "home", "On the front page", "The album's, the artist's or the podcast's own card in a marked row.", 3, "0.05", PLANNED),
    ("music-search", "music", "search", "In search results", "Its own card among the results of a search.", 2, "0.08", PLANNED),
    ("books-browse", "books", "category", "In browse and search", "The title's own card in a genre's list or a search.", 2, "0.08", PLANNED),
    ("books-also", "books", "detail", "In Readers also bought", "The title's own card beside another title. Never inside a book.", 2, "0.06", PLANNED),
]


def seed(apps, schema_editor):
    Placement = apps.get_model("promote", "Placement")
    for position, (key, area, page, name, shows, slots, floor, state) in enumerate(ROWS, start=1):
        Placement.objects.update_or_create(key=key, defaults={
            "area": area, "page": page, "name": name, "shows": shows, "slots": slots, "floor": Decimal(floor), "state": state, "position": position})


def unseed(apps, schema_editor):
    apps.get_model("promote", "Placement").objects.filter(key__in=[row[0] for row in ROWS]).delete()


class Migration(migrations.Migration):
    dependencies = [("promote", "0001_initial")]
    operations = [migrations.RunPython(seed, unseed)]
