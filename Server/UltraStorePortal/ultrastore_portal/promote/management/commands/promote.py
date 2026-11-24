"""ultrastore-portal promote ...: advertising from the command line.

    promote cleanup                       click rows older than they are kept, removed (a daily timer)
    promote voucher <publisher> <amount>  a balance granted by the store
    promote placements                    the placements, their floors and states
"""

from decimal import Decimal, InvalidOperation

from django.core.management.base import BaseCommand, CommandError

from ultrastore_portal.promote import services
from ultrastore_portal.promote.models import LedgerEntry, Placement
from ultrastore_portal.store.models import Publisher


class Command(BaseCommand):
    help = "Advertising (UltraAds): cleanup, vouchers, placements."

    def add_arguments(self, parser):
        sub = parser.add_subparsers(dest="what", required=True)
        sub.add_parser("cleanup")
        sub.add_parser("placements")
        voucher = sub.add_parser("voucher")
        voucher.add_argument("publisher")
        voucher.add_argument("amount")
        voucher.add_argument("--note", default="A voucher from the store")

    def handle(self, *args, what: str, **options):
        if what == "cleanup":
            self.stdout.write(f"removed {services.cleanup()} click rows")
        elif what == "placements":
            for placement in Placement.objects.all():
                self.stdout.write(f"{placement.key}\t{placement.state}\t{placement.slots}\t{placement.floor}\t{placement}")
        else:
            publisher = Publisher.objects.filter(slug=options["publisher"]).first()
            if publisher is None:
                raise CommandError(f"No publisher {options['publisher']}.")
            try:
                entry = services.credit(publisher, LedgerEntry.VOUCHER, Decimal(options["amount"]), options["note"])
            except (InvalidOperation, services.Refused) as refused:
                raise CommandError(str(refused) or "An amount is a number.") from refused
            self.stdout.write(f"{publisher.slug}: balance {entry.balance_after}")
