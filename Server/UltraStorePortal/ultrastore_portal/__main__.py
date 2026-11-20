"""ultrastore-portal <command>: Django's command line with the portal's
settings (python -m ultrastore_portal, manage.py and the installed script
are the same thing)."""
import os
import sys


def main() -> None:
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "ultrastore_portal.settings")
    from django.core.management import execute_from_command_line

    execute_from_command_line(sys.argv)


if __name__ == "__main__":
    main()
