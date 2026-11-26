"""ultrastore-portal accounts ...: the accounts from the command line.

    accounts maintain   every closed account past its year and every deletion
                        past its grace, erased (a daily timer); says how many
    accounts leaving    the accounts that are closed or being deleted, with
                        the day each is erased
"""

from django.core.management.base import BaseCommand

from ultrastore_portal.accounts import closure
from ultrastore_portal.accounts.models import User


class Command(BaseCommand):
    help = "Accounts: the daily maintenance, and who is leaving."

    def add_arguments(self, parser):
        sub = parser.add_subparsers(dest="what", required=True)
        sub.add_parser("maintain")
        sub.add_parser("leaving")

    def handle(self, *args, what: str, **options):
        getattr(self, what)()

    def maintain(self):
        counts = closure.maintain()
        self.stdout.write(f"erased: {counts['closed']} closed account(s) past their time, {counts['deleting']} deletion(s) past their grace")

    def leaving(self):
        rows = User.objects.filter(erased_at__isnull=True, erase_after__isnull=False).order_by("erase_after", "id")
        for user in rows:
            self.stdout.write(f"{user.pk}\t{user.state}\terased after {user.erase_after}")
        if not rows:
            self.stdout.write("nobody is leaving")
