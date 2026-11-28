"""The portal's settings, every one of them from the environment
(ULTRASTORE_PORTAL_<NAME>, deploy/ultrastore-portal.env.example).

Version: 0.12.0 - the affiliates' holdback and payout minimum (AFFILIATE_HOLDBACK_..., _PAYOUT_MINIMUM)
Version: 0.11.0 - the binding's win-back (AFFILIATE_WINBACK_MONTHS, _WINBACK_DAYS)
Version: 0.10.0 - affiliates (AFFILIATES, AFFILIATE_..., TEXT_GATEWAY_...)
Version: 0.9.0 - tax and payouts (SELLER_..., INVOICE_PREFIX, PAYOUT_HOLD_DAYS, PAYOUT_MINIMUM)
Version: 0.8.0 - leaving an account (ACCOUNT_CLOSED_DAYS, ACCOUNT_DELETION_GRACE_DAYS)
Version: 0.7.0 - the store's fee (STORE_FEE_PERCENT)
Version: 0.5.0 - advertising (PROMOTE, PROMOTE_TOPUPS, PROMOTE_CURRENCY)
Version: 0.4.0 - the account's own address (ACCOUNT_URL) and the OpenID
                 Connect provider's settings (OIDC_...)
Version: 0.3.0 - the portal_ui template library, messages as Bootstrap alerts;
                 a password is 8 to 64 characters
Version: 0.1.0
"""

from __future__ import annotations

import os
import sys
from decimal import Decimal, InvalidOperation
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
# The account's own root: where a person signs up and signs in, and the
# issuer other services sign in through (identity/hosts.py). Unset, the
# account lives under the store's root, as it did (<root>/account/). Set -
# https://account.ultraos.eu - it is a second host this same service
# answers, and the store's host signs people in through it like any other
# service. It is written into every token for good: choose it before the
# first service is registered, and do not change it afterwards.
ACCOUNT_URL = env("ACCOUNT_URL").rstrip("/")
ACCOUNT_NAME = env("ACCOUNT_NAME", "ULTRA OS account")
_account = urlsplit(ACCOUNT_URL or BASE_URL)
if ACCOUNT_URL:
    if _account.scheme not in ("http", "https") or not _account.hostname or _account.path.strip("/"):
        raise ImproperlyConfigured(f"ULTRASTORE_PORTAL_ACCOUNT_URL is a host's root (https://account.ultraos.eu): {ACCOUNT_URL}")
    if _account.scheme != _base.scheme:
        raise ImproperlyConfigured("ULTRASTORE_PORTAL_ACCOUNT_URL and ULTRASTORE_PORTAL_BASE_URL are both https, or both http on a developer's machine")
TWO_HOSTS = _account.netloc.lower() != _base.netloc.lower()
ALLOWED_HOSTS = env_list("ALLOWED_HOSTS") or sorted({_base.hostname or "localhost", _account.hostname})
if DEBUG or RUNNING_TESTS:
    ALLOWED_HOSTS += ["localhost", "127.0.0.1", "testserver"]
HTTPS = _base.scheme == "https"
if HTTPS:
    # With two hosts neither is told to trust the other: no form is posted
    # from one to the other, and each host's own origin needs no listing
    # (the web server passes Host and X-Forwarded-Proto on, deploy/nginx.conf).
    # Their cookies are __Host- cookies then: bound to the one host that set
    # them, which no other host under ultraos.eu can set or overwrite.
    if TWO_HOSTS:
        SESSION_COOKIE_NAME = "__Host-sessionid"
        CSRF_COOKIE_NAME = "__Host-csrftoken"
    else:
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
    "ultrastore_portal.identity",
    "ultrastore_portal.store",
    "ultrastore_portal.billing",
    "ultrastore_portal.promote",
    "ultrastore_portal.affiliates",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "ultrastore_portal.identity.hosts.AccountHostMiddleware",   # before anything looks an address up
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "ultrastore_portal.identity.sessions.SignInMiddleware",
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
            # {% load portal_ui %}: a form's field drawn with Bootstrap's classes (web/ui.py).
            "libraries": {"portal_ui": "ultrastore_portal.web.ui"},
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
# Leaving an account (accounts/closure.py): the days a closed account is
# kept, in which its owner can reactivate it, and the days a deletion that
# was asked for waits, in which its owner can take it back. Then it is erased.
ACCOUNT_CLOSED_DAYS = int(env("ACCOUNT_CLOSED_DAYS", "365"))
ACCOUNT_DELETION_GRACE_DAYS = int(env("ACCOUNT_DELETION_GRACE_DAYS", "90"))
if ACCOUNT_CLOSED_DAYS < 1 or ACCOUNT_DELETION_GRACE_DAYS < 1:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_ACCOUNT_CLOSED_DAYS and ULTRASTORE_PORTAL_ACCOUNT_DELETION_GRACE_DAYS are at least one day")
LOGIN_URL = "account:signin"
LOGIN_REDIRECT_URL = "console:home"
LOGOUT_REDIRECT_URL = "store:home"
# A password is 8 to 64 characters - characters of Unicode text, not bytes
# (accounts/validators.py). The two numbers are named once, here: the forms
# say them and the validators check them.
PASSWORD_MIN_LENGTH = 8
PASSWORD_MAX_LENGTH = 64
AUTH_PASSWORD_VALIDATORS = [
    {"NAME": "django.contrib.auth.password_validation.UserAttributeSimilarityValidator"},
    {"NAME": "django.contrib.auth.password_validation.MinimumLengthValidator", "OPTIONS": {"min_length": PASSWORD_MIN_LENGTH}},
    {"NAME": "ultrastore_portal.accounts.validators.MaximumLengthValidator", "OPTIONS": {"max_length": PASSWORD_MAX_LENGTH}},
    {"NAME": "django.contrib.auth.password_validation.CommonPasswordValidator"},
    {"NAME": "django.contrib.auth.password_validation.NumericPasswordValidator"},
]
# A mailed link (the address proven, a password reset) is good this long.
PASSWORD_RESET_TIMEOUT = 2 * 24 * 3600

# Signing in for other services (identity/, OpenID Connect). The key the
# identity tokens are signed with: a PEM file (an RSA key, 2048 bits or
# more), or unset - one is made on first use and kept in the database. The
# secret a person's subject is derived with: unset, made once and kept in
# the database; it must never change. Then how long each thing lasts, in
# seconds: the code, the access token, the identity token, a refresh token
# that is not used.
OIDC_KEY_FILE = env("OIDC_KEY_FILE")
OIDC_SUBJECT_SECRET = env("OIDC_SUBJECT_SECRET")
OIDC_CODE_TTL = int(env("OIDC_CODE_TTL", "120"))
OIDC_ACCESS_TOKEN_TTL = int(env("OIDC_ACCESS_TOKEN_TTL", "3600"))
OIDC_ID_TOKEN_TTL = int(env("OIDC_ID_TOKEN_TTL", "3600"))
OIDC_REFRESH_TOKEN_TTL = int(env("OIDC_REFRESH_TOKEN_TTL", str(90 * 24 * 3600)))

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
# The store's fee: the percentage of what a buyer pays that the store keeps;
# the rest is the publisher's share (billing/split.py). Written on each
# payment when the money moves, so changing it here changes later sales only.
try:
    STORE_FEE_PERCENT = Decimal(env("STORE_FEE_PERCENT", "15"))
except InvalidOperation as error:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_STORE_FEE_PERCENT is a number of percent, such as 15") from error
if not STORE_FEE_PERCENT.is_finite() or not 0 <= STORE_FEE_PERCENT < 100 or STORE_FEE_PERCENT != STORE_FEE_PERCENT.quantize(Decimal("0.01")):
    raise ImproperlyConfigured(f"ULTRASTORE_PORTAL_STORE_FEE_PERCENT is from 0 to under 100, with at most two decimals: {STORE_FEE_PERCENT}")
# Tax (billing/tax.py). The store is the seller: its name, its address
# (lines separated by "|"), its VAT id and its country (ISO 3166-1) stand on
# every invoice. Until all four are set no tax is reckoned and no invoice
# written. INVOICE_PREFIX goes before the year and the running number.
SELLER_NAME = env("SELLER_NAME")
SELLER_ADDRESS = env("SELLER_ADDRESS").replace("|", "\n")
SELLER_VAT_ID = env("SELLER_VAT_ID")
SELLER_COUNTRY = env("SELLER_COUNTRY").upper()
if SELLER_COUNTRY and (len(SELLER_COUNTRY) != 2 or not SELLER_COUNTRY.isalpha()):
    raise ImproperlyConfigured(f"ULTRASTORE_PORTAL_SELLER_COUNTRY is a two-letter country code, such as DE: {SELLER_COUNTRY}")
INVOICE_PREFIX = env("INVOICE_PREFIX", "")
# Payouts (billing/payouts.py): the days a sale waits before its share can
# be paid out - the time in which it is most likely given back - and the
# least a payout is made for; a smaller sum is carried to the next.
PAYOUT_HOLD_DAYS = int(env("PAYOUT_HOLD_DAYS", "30"))
try:
    PAYOUT_MINIMUM = Decimal(env("PAYOUT_MINIMUM", "0.01"))
except InvalidOperation as error:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_PAYOUT_MINIMUM is an amount, such as 25.00") from error
if PAYOUT_HOLD_DAYS < 0 or not PAYOUT_MINIMUM.is_finite() or PAYOUT_MINIMUM < Decimal("0.01"):
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_PAYOUT_HOLD_DAYS is zero or more, ULTRASTORE_PORTAL_PAYOUT_MINIMUM at least 0.01")
# Where a visitor without the native store gets it (the app page's link): a
# path under the root as well.
STORE_DOWNLOAD_URL = _under_root("STORE_DOWNLOAD_PATH")

# Advertising (promote/, UltraAds): sponsored listings in the store. Off
# unless switched on: with it off there is no Promote button, no page of it
# and no advertisement anywhere. Paying into an advertising balance is a
# second switch, off until the store writes tax invoices: until then a
# balance comes from the operator's vouchers. One currency for balances.
PROMOTE = env("PROMOTE", "0") == "1"
PROMOTE_TOPUPS = env("PROMOTE_TOPUPS", "0") == "1"
PROMOTE_CURRENCY = (env("PROMOTE_CURRENCY") or "EUR").upper()

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

# A message's level as the Bootstrap alert that shows it (templates/_messages.html).
MESSAGE_TAGS = {10: "secondary", 20: "primary", 25: "success", 30: "warning", 40: "danger"}

CACHES = {"default": {"BACKEND": "django.core.cache.backends.locmem.LocMemCache"}}

LOGGING = {
    "version": 1,
    "disable_existing_loggers": False,
    "handlers": {"console": {"class": "logging.StreamHandler"}},
    "root": {"handlers": ["console"], "level": env("LOG_LEVEL", "INFO")},
}

# Affiliates (affiliates/, UltraAffiliates phase 1): off unless switched on.
# The base commission in percent of the net price, out of the store's fee;
# the superaffiliate's share of an affiliate's commission; the days a link
# counts; the days a commission waits before it is approved. A text message
# (the code that proves an affiliate's phone number) is one POST to the
# gateway; without one the operator confirms the number at the review.
AFFILIATES = env("AFFILIATES", "0") == "1"
try:
    AFFILIATE_BASE_PERCENT = Decimal(env("AFFILIATE_BASE_PERCENT", "5"))
    AFFILIATE_SUPER_PERCENT = Decimal(env("AFFILIATE_SUPER_PERCENT", "10"))
except InvalidOperation as error:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_AFFILIATE_BASE_PERCENT and _SUPER_PERCENT are numbers of percent") from error
AFFILIATE_LINK_DAYS = int(env("AFFILIATE_LINK_DAYS", "90"))
AFFILIATE_APPROVAL_DAYS = int(env("AFFILIATE_APPROVAL_DAYS", "30"))
# The binding (phase 2): a customer who has paid for nothing for more than
# these months, and pays within these days of following another affiliate's
# link, is won back by that affiliate.
AFFILIATE_WINBACK_MONTHS = int(env("AFFILIATE_WINBACK_MONTHS", "6"))
AFFILIATE_WINBACK_DAYS = int(env("AFFILIATE_WINBACK_DAYS", "14"))
if AFFILIATE_WINBACK_MONTHS < 1 or AFFILIATE_WINBACK_DAYS < 1:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_AFFILIATE_WINBACK_MONTHS and _WINBACK_DAYS are at least 1")
# The money (phase 3): of every approved commission this many percent are
# held back for this many months from the sale, against a refund or a
# chargeback that comes late; a balance under the minimum is carried to the
# next month's payout.
try:
    AFFILIATE_HOLDBACK_PERCENT = Decimal(env("AFFILIATE_HOLDBACK_PERCENT", "5"))
    AFFILIATE_PAYOUT_MINIMUM = Decimal(env("AFFILIATE_PAYOUT_MINIMUM", "25.00"))
except InvalidOperation as error:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_AFFILIATE_HOLDBACK_PERCENT and _PAYOUT_MINIMUM are numbers") from error
AFFILIATE_HOLDBACK_MONTHS = int(env("AFFILIATE_HOLDBACK_MONTHS", "6"))
if not 0 <= AFFILIATE_HOLDBACK_PERCENT <= 50 or AFFILIATE_HOLDBACK_MONTHS < 1 or AFFILIATE_PAYOUT_MINIMUM < Decimal("0.01"):
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_AFFILIATE_HOLDBACK_PERCENT is from 0 to 50, _HOLDBACK_MONTHS at least 1, _PAYOUT_MINIMUM 0.01 or more")
if not 0 <= AFFILIATE_BASE_PERCENT <= STORE_FEE_PERCENT:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_AFFILIATE_BASE_PERCENT comes out of the store's fee: from 0 to ULTRASTORE_PORTAL_STORE_FEE_PERCENT")
if not 0 <= AFFILIATE_SUPER_PERCENT <= 50 or AFFILIATE_LINK_DAYS < 1 or AFFILIATE_APPROVAL_DAYS < 0:
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_AFFILIATE_SUPER_PERCENT is from 0 to 50, _LINK_DAYS at least 1, _APPROVAL_DAYS zero or more")
TEXT_GATEWAY_URL = env("TEXT_GATEWAY_URL")
TEXT_GATEWAY_TOKEN = env("TEXT_GATEWAY_TOKEN")
if TEXT_GATEWAY_URL and not TEXT_GATEWAY_URL.startswith("https://"):
    raise ImproperlyConfigured("ULTRASTORE_PORTAL_TEXT_GATEWAY_URL is an https address")
