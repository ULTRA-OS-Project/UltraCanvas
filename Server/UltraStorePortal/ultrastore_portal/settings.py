"""The portal's settings, every one of them from the environment
(ULTRASTORE_PORTAL_<NAME>, deploy/ultrastore-portal.env.example).

Version: 0.1.0
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

from django.core.exceptions import ImproperlyConfigured

PACKAGE_DIR_ = Path(__file__).resolve().parent
BASE_DIR = PACKAGE_DIR_.parent


def env(name: str, default: str = "") -> str:
    return os.environ.get("ULTRASTORE_PORTAL_" + name, default).strip()


def env_list(name: str) -> list[str]:
    return [item.strip() for item in env(name).split(",") if item.strip()]


def database_from_url(url: str) -> dict:
    """mysql://user:password@host:3306/db, postgresql://..., sqlite:///path
    (a bare path means the same) - the URLs the other store services take."""
    if "://" not in url:
        return {"ENGINE": "django.db.backends.sqlite3", "NAME": url}
    parts = urlsplit(url)
    scheme = parts.scheme.lower()
    if scheme == "sqlite":
        return {"ENGINE": "django.db.backends.sqlite3", "NAME": unquote(parts.path[1:] if parts.netloc == "" and parts.path.startswith("//") else parts.path) or ":memory:"}
    engines = {"mysql": "django.db.backends.mysql", "mariadb": "django.db.backends.mysql",
               "postgresql": "django.db.backends.postgresql", "postgres": "django.db.backends.postgresql"}
    if scheme not in engines:
        raise ImproperlyConfigured(f"ULTRASTORE_PORTAL_DB names no known database: {scheme}")
    database = {
        "ENGINE": engines[scheme],
        "NAME": unquote(parts.path.lstrip("/")),
        "USER": unquote(parts.username or ""),
        "PASSWORD": unquote(parts.password or ""),
        "HOST": parts.hostname or "",
        "PORT": str(parts.port or ""),
        "CONN_MAX_AGE": 60,
    }
    query = parse_qs(parts.query)
    if engines[scheme].endswith("mysql"):
        database["OPTIONS"] = {"charset": "utf8mb4"}
        if "unix_socket" in query:
            database["OPTIONS"]["unix_socket"] = query["unix_socket"][0]
    return database


RUNNING_TESTS = len(sys.argv) > 1 and sys.argv[1] == "test"
DEBUG = env("DEBUG", "0") == "1"

SECRET_KEY = env("SECRET_KEY")
if not SECRET_KEY:
    if not (DEBUG or RUNNING_TESTS):
        raise ImproperlyConfigured("Set ULTRASTORE_PORTAL_SECRET_KEY (or ULTRASTORE_PORTAL_DEBUG=1 on a developer's machine)")
    SECRET_KEY = "development-only-not-a-secret"

# The one root the store lives under. There is one store and one catalog:
# every URL the portal writes - the catalog's packages and pictures, the
# services the catalog names, the links in a mail - is this root plus a
# path, never a second host. A developer's machine is its own root.
STORE_ROOT_URL = "https://store.ultraos.eu"
BASE_URL = env("BASE_URL", "http://127.0.0.1:8000" if (DEBUG or RUNNING_TESTS) else STORE_ROOT_URL).rstrip("/")
_base = urlsplit(BASE_URL)
ALLOWED_HOSTS = env_list("ALLOWED_HOSTS") or [_base.hostname or "localhost"]
if DEBUG or RUNNING_TESTS:
    ALLOWED_HOSTS += ["localhost", "127.0.0.1", "testserver"]
HTTPS = _base.scheme == "https"
if HTTPS:
    CSRF_TRUSTED_ORIGINS = [f"https://{_base.netloc}"]
    SESSION_COOKIE_SECURE = True
    CSRF_COOKIE_SECURE = True
    SECURE_PROXY_SSL_HEADER = ("HTTP_X_FORWARDED_PROTO", "https")
    SECURE_HSTS_SECONDS = 31536000
SECURE_CONTENT_TYPE_NOSNIFF = True
SECURE_REFERRER_POLICY = "same-origin"
X_FRAME_OPTIONS = "DENY"
SESSION_COOKIE_HTTPONLY = True
SESSION_COOKIE_SAMESITE = "Lax"

INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.staticfiles",
    "ultrastore_portal.accounts",
    "ultrastore_portal.store",
    "ultrastore_portal.billing",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
]

ROOT_URLCONF = "ultrastore_portal.urls"
WSGI_APPLICATION = "ultrastore_portal.wsgi.application"

TEMPLATES = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [PACKAGE_DIR_ / "templates"],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
                "ultrastore_portal.web.context.portal",
            ],
        },
    },
]

# A database server in production, SQLite for tests and development only
# (Docs/UltraStore/README.md, Decisions).
DATABASES = {"default": database_from_url(env("DB", str(BASE_DIR / "var" / "portal.sqlite3")))}
if DATABASES["default"]["ENGINE"].endswith("sqlite3") and DATABASES["default"]["NAME"] != ":memory:":
    Path(DATABASES["default"]["NAME"]).parent.mkdir(parents=True, exist_ok=True)
DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"

AUTH_USER_MODEL = "accounts.User"
LOGIN_URL = "account:signin"
LOGIN_REDIRECT_URL = "console:home"
LOGOUT_REDIRECT_URL = "store:home"
AUTH_PASSWORD_VALIDATORS = [
    {"NAME": "django.contrib.auth.password_validation.UserAttributeSimilarityValidator"},
    {"NAME": "django.contrib.auth.password_validation.MinimumLengthValidator", "OPTIONS": {"min_length": 12}},
    {"NAME": "django.contrib.auth.password_validation.CommonPasswordValidator"},
    {"NAME": "django.contrib.auth.password_validation.NumericPasswordValidator"},
]
# A mailed link (the address proven, a password reset) is good this long.
PASSWORD_RESET_TIMEOUT = 2 * 24 * 3600

LANGUAGE_CODE = "en"
TIME_ZONE = "UTC"
USE_I18N = True
USE_TZ = True

# What the portal keeps on disk, all under one folder:
#   media/     icons and screenshots, public (/media/)
#   packages/  every uploaded package under its hash - never served directly
#   public/    the publish folder, laid out as the web server's document
#              root: v1/catalog.json, its signature, and the packages of
#              published releases (v1/packages/, v1/restricted/)
#   static/    collectstatic's output (/static/)
DATA_DIR = Path(env("DATA_DIR", str(BASE_DIR / "var")))
MEDIA_ROOT = DATA_DIR / "media"
MEDIA_URL = "/media/"
PACKAGE_DIR = DATA_DIR / "packages"
PUBLISH_DIR = DATA_DIR / "public"
STATIC_ROOT = DATA_DIR / "static"
STATIC_URL = "/static/"
STATICFILES_DIRS = [PACKAGE_DIR_ / "static"]

MAX_PACKAGE_MB = int(env("MAX_PACKAGE_MB", "512"))
MAX_IMAGE_MB = int(env("MAX_IMAGE_MB", "8"))
MAX_SCREENSHOTS = int(env("MAX_SCREENSHOTS", "8"))
# Requests carrying no file stay small; an upload is streamed to disk.
DATA_UPLOAD_MAX_MEMORY_SIZE = 2 * 1024 * 1024
FILE_UPLOAD_MAX_MEMORY_SIZE = 2 * 1024 * 1024
FILE_UPLOAD_TEMP_DIR = None

# The catalog (store/catalog.py): the operator's Ed25519 seed (32 bytes,
# hex; empty: one is made on the first publish and kept in the database),
# more keys for trusted_keys (a key being rotated in), and the sequence the
# portal counts on from when it takes over the address a catalog was already
# published at.
SIGNING_SEED = env("SIGNING_SEED")
TRUSTED_KEYS = env_list("TRUSTED_KEYS")
SEQUENCE_FLOOR = int(env("SEQUENCE_FLOOR", "0"))


def _under_root(name: str) -> str:
    """The root plus the path the setting names ('/get'); '' when unset."""
    value = env(name)
    if not value:
        return ""
    if not value.startswith("/") or value.startswith("//") or "://" in value:
        raise ImproperlyConfigured(f"ULTRASTORE_PORTAL_{name} is a path under the store's root (/get), not a URL: {value}")
    return BASE_URL + value.rstrip("/")


# Everything the catalog names lives under the one root, and all of it is
# the portal's own: the catalog (<root>/v1/catalog.json, the packages beside
# it), the app pages, the reports (<root>/v1/reports), the checkout
# (<root>/v1/checkout). The portal replaces the earlier store services and
# names none of them.
CATALOG_URL = BASE_URL + "/v1"
MARKET_URL = BASE_URL

# Buying (billing/services.py). The provider: "skrill" (Skrill Quick
# Checkout, Paysafe), "test" (a button in the provider's place - a
# developer's machine and the tests only), or empty: the store sells
# nothing, shows the prices and names no payment service in the catalog.
PAYMENT_PROVIDER = env("PAYMENT_PROVIDER").lower()
if PAYMENT_PROVIDER not in ("", "skrill", "test"):
    raise ImproperlyConfigured(f"ULTRASTORE_PORTAL_PAYMENT_PROVIDER is skrill, test or empty: {PAYMENT_PROVIDER}")
if PAYMENT_PROVIDER == "test" and not (DEBUG or RUNNING_TESTS):
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_PAYMENT_PROVIDER=test sells for nothing: it runs only with ULTRASTORE_PORTAL_DEBUG=1")
# The Skrill merchant account: where the money goes, the secret word the
# status reports are signed with, the merchant id, and the API password for
# refunds and for ending a recurring payment (the merchant portal, Developer
# settings).
SKRILL_PAY_TO_EMAIL = env("SKRILL_PAY_TO_EMAIL")
SKRILL_SECRET_WORD = env("SKRILL_SECRET_WORD")
SKRILL_MERCHANT_ID = env("SKRILL_MERCHANT_ID")
SKRILL_API_PASSWORD = env("SKRILL_API_PASSWORD")
SKRILL_PAY_URL = env("SKRILL_PAY_URL", "https://pay.skrill.com")
SKRILL_API_URL = env("SKRILL_API_URL", "https://www.skrill.com/app")
# The licence key's seed (32 bytes, hex; empty: made on the first sale and
# kept in the database), the days a subscription's licence outlasts its paid
# period while the provider retries a failed renewal, and the secret and the
# lifetime of a buyer's download link (nginx secure_link_md5 on
# /v1/restricted/, deploy/nginx.conf - the same secret there).
LICENCE_SEED = env("LICENCE_SEED")
GRACE_DAYS = int(env("GRACE_DAYS", "7"))
RESTRICTED_SECRET = env("RESTRICTED_SECRET") or ("development-only-link-secret" if (DEBUG or RUNNING_TESTS) else "")
RESTRICTED_TTL = int(env("RESTRICTED_TTL", "600"))
# Where a visitor without the native store gets it (the app page's link): a
# path under the root as well.
STORE_DOWNLOAD_URL = _under_root("STORE_DOWNLOAD_PATH")

# Mail: smtp://user:password@host:587 or smtps://...:465; empty prints the
# mail to the log, which is what a developer's machine wants.
DEFAULT_FROM_EMAIL = env("MAIL_FROM", "UltraStore <noreply@localhost>")
SERVER_EMAIL = DEFAULT_FROM_EMAIL
_smtp = env("SMTP_URL")
if _smtp:
    _mail = urlsplit(_smtp)
    EMAIL_BACKEND = "django.core.mail.backends.smtp.EmailBackend"
    EMAIL_HOST = _mail.hostname or "localhost"
    EMAIL_PORT = _mail.port or (465 if _mail.scheme == "smtps" else 587)
    EMAIL_HOST_USER = unquote(_mail.username or "")
    EMAIL_HOST_PASSWORD = unquote(_mail.password or "")
    EMAIL_USE_SSL = _mail.scheme == "smtps"
    EMAIL_USE_TLS = _mail.scheme == "smtp" and EMAIL_PORT != 25
    EMAIL_TIMEOUT = 20
elif RUNNING_TESTS:
    EMAIL_BACKEND = "django.core.mail.backends.locmem.EmailBackend"
else:
    EMAIL_BACKEND = "django.core.mail.backends.console.EmailBackend"

CACHES = {"default": {"BACKEND": "django.core.cache.backends.locmem.LocMemCache"}}

LOGGING = {
    "version": 1,
    "disable_existing_loggers": False,
    "handlers": {"console": {"class": "logging.StreamHandler"}},
    "root": {"handlers": ["console"], "level": env("LOG_LEVEL", "INFO")},
}
