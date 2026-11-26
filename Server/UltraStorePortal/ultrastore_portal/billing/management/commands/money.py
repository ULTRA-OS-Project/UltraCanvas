"""ultrastore-portal money ...: tax and payouts from the command line.

    money seed-rates            the starting table of VAT rates, for the
                                countries that have none yet; to be checked
                                on the operator's tax page before selling
    money rates                 the VAT rates in force today
    money quarter YEAR Q        the tax of a quarter by country and rate (the
                                figures of the one-stop-shop return)
    money payouts [--until YYYY-MM-DD]
                                the payouts for everything due on that day
                                (default: the last day of last month)
    money owed                  what no payout has taken yet, by publisher
"""

import datetime as dt

from django.core.management.base import BaseCommand, CommandError
from django.utils import timezone

from ultrastore_portal.billing import payouts, tax
from ultrastore_portal.billing.models import VatRate
from ultrastore_portal.store.models import Publisher


class Command(BaseCommand):
    help = "Tax and payouts: the VAT rates, a quarter's tax, the payouts."

    def add_arguments(self, parser):
        sub = parser.add_subparsers(dest="what", required=True)
        sub.add_parser("seed-rates")
        sub.add_parser("rates")
        quarter = sub.add_parser("quarter")
        quarter.add_argument("year", type=int)
        quarter.add_argument("q", type=int, choices=[1, 2, 3, 4])
        sub.add_parser("payouts").add_argument("--until")
        sub.add_parser("owed")

    def handle(self, *args, what: str, **options):
        getattr(self, what.replace("-", "_"))(**options)

    def seed_rates(self, **_):
        self.stdout.write(f"{tax.seed()} rate(s) added from the starting table. Check every rate on the operator's tax page before selling.")

    def rates(self, **_):
        today = timezone.localdate()
        for country in sorted(tax.EU):
            row = VatRate.objects.filter(country=country, valid_from__lte=today).order_by("-valid_from").first()
            self.stdout.write(f"{country}\t{row.percent if row else '-'}\t{row.valid_from if row else 'no rate'}")

    def quarter(self, year, q, **_):
        report = tax.quarter_report(year, q)
        for row in report["rows"]:
            self.stdout.write(f"{row['country']}\t{row['percent']} %\t{row['invoices']}\t{row['net']}\t{row['vat']}\t{row['gross']}\t{row['currency']}")
        for total in report["totals"]:
            self.stdout.write(f"total\t\t\t{total['net']}\t{total['vat']}\t{total['gross']}\t{total['currency']}")
        if not report["rows"]:
            self.stdout.write("no invoices in that quarter")

    def payouts(self, until, **_):
        try:
            day = dt.date.fromisoformat(until) if until else None
            made = payouts.build(day)
        except (ValueError, payouts.PayoutError) as refused:
            raise CommandError(str(refused)) from refused
        for payout in made:
            self.stdout.write(f"{payout.pk}\t{payout.publisher.slug}\t{payout.amount} {payout.currency}\tuntil {payout.until}")
        self.stdout.write(f"{len(made)} payout(s) made")

    def owed(self, **_):
        for publisher in Publisher.objects.order_by("slug"):
            for row in payouts.owed(publisher):
                self.stdout.write(f"{publisher.slug}\t{row['amount']} {row['currency']}\t{row['payments']} payment(s)")
