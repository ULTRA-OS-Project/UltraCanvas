"""The WSGI entry: gunicorn ultrastore_portal.wsgi."""
import os

from django.core.wsgi import get_wsgi_application

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "ultrastore_portal.settings")
application = get_wsgi_application()
