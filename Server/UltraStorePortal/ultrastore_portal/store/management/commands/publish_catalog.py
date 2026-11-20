"""ultrastore-portal publish_catalog [--force]: the catalog asked for from
the command line (a timer, or after a change made in the admin site)."""

from django.core.management.base import BaseCommand

from ultrastore_portal.store import catalog


class Command(BaseCommand):
    help = "Publish the signed catalog if what is public changed (--force: publish anyway)."

    def add_arguments(self, parser):
        parser.add_argument("--force", action="store_true", help="A new sequence even when nothing changed.")

    def handle(self, *args, force: bool = False, **options):
        snapshot, made = catalog.publish_catalog(None, force=force)
        state = "published" if made else "unchanged"
        self.stdout.write(f"catalog {snapshot.sequence} {state}: {snapshot.apps} apps, {snapshot.components} components, key {snapshot.public_key}")
        self.stdout.write(str(catalog.publish_folder() / "catalog.json"))
