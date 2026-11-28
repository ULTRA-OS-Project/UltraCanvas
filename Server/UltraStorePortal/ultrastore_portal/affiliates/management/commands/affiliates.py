"""ultrastore-portal affiliates ...: the affiliate programme from the
command line.

    affiliates maintain   blocks whose time is over, lifted; commissions
                          whose days are over, approved, and their holdback
                          set aside; holdbacks whose day has come, released;
                          clicks past the days a link counts, counted by day
                          and removed (a daily timer)
    affiliates payouts [--until YYYY-MM-DD]
                          the payouts for everything due on the day - by
                          default the last day of last month (a monthly
                          timer); the operator pays each and marks it paid
    affiliates owed       the balances no payout has taken, by affiliate,
                          and what is held back
    affiliates cost       what the programme cost: from the store's fee, and
                          from the publishers' boosts
"""

import datetime as dt

from django.core.management.base import BaseCommand, CommandError

from ultrastore_portal.affiliates import services
from ultrastore_portal.affiliates.models import AffiliateProfile


class Command(BaseCommand):
    help = "Affiliates: the daily maintenance, the monthly payouts, what is owed, what it cost."

    def add_arguments(self, parser):
        sub = parser.add_subparsers(dest="what", required=True)
        for name in ("maintain", "owed", "cost"):
            sub.add_parser(name)
        sub.add_parser("payouts").add_argument("--until", type=dt.date.fromisoformat, default=None)

    def handle(self, *args, what: str, **options):
        if what == "payouts":
            return self.payouts(options["until"])
        getattr(self, what)()

    def maintain(self):
        done = services.maintain()
        self.stdout.write(f"{done['approved']} commission(s) approved, {done['released']} holdback(s) released, {done['blocks_ended']} block(s) ended, "
                          f"{done['clicks_counted']} click(s) counted and removed")

    def payouts(self, until):
        try:
            made = services.build(until)
        except services.AffiliateError as refused:
            raise CommandError(str(refused)) from refused
        for payout in made:
            self.stdout.write(f"{payout.pk}\t{payout.affiliate.handle}\t{payout.amount} {payout.currency}\tuntil {payout.until}")
        self.stdout.write(f"{len(made)} payout(s) made")

    def owed(self):
        for profile in AffiliateProfile.objects.select_related("user").order_by("handle"):
            for row in services.sums(profile):
                if row["balance"] or row["held"]:
                    carried = "" if row["payable"] or row["balance"] <= 0 else "\tcarried"
                    self.stdout.write(f"{profile.handle}\t{row['balance']} {row['currency']}\theld back {row['held']}{carried}")

    def cost(self):
        for row in services.cost():
            self.stdout.write(f"{row['currency']}\t{row['total']}\tfrom the store's fee {row['operator']}\tfrom publishers' boosts {row['merchants']}")
