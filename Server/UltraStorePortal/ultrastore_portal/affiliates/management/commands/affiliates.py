"""ultrastore-portal affiliates ...: the affiliate programme from the
command line.

    affiliates maintain   commissions whose days are over, approved; blocks
                          whose time is over, lifted; clicks past the days a
                          link counts, counted by day and removed (a daily
                          timer)
    affiliates owed       what is approved and not paid, by affiliate
    affiliates cost       what the programme cost: from the store's fee, and
                          from the publishers' boosts
"""

from django.core.management.base import BaseCommand

from ultrastore_portal.affiliates import services
from ultrastore_portal.affiliates.models import AffiliateProfile


class Command(BaseCommand):
    help = "Affiliates: the daily maintenance, what is owed, what it cost."

    def add_arguments(self, parser):
        sub = parser.add_subparsers(dest="what", required=True)
        for name in ("maintain", "owed", "cost"):
            sub.add_parser(name)

    def handle(self, *args, what: str, **options):
        getattr(self, what)()

    def maintain(self):
        done = services.maintain()
        self.stdout.write(f"{done['approved']} commission(s) approved, {done['blocks_ended']} block(s) ended, {done['clicks_counted']} click(s) counted and removed")

    def owed(self):
        for profile in AffiliateProfile.objects.order_by("handle"):
            for row in services.sums(profile):
                if row["balance"]:
                    self.stdout.write(f"{profile.handle}\t{row['balance']} {row['currency']}")

    def cost(self):
        for row in services.cost():
            self.stdout.write(f"{row['currency']}\t{row['total']}\tfrom the store's fee {row['operator']}\tfrom publishers' boosts {row['merchants']}")
