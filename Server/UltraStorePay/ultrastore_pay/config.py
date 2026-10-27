"""Settings, from the environment (or a .env file the service manager loads).

The merchant credentials live here and nowhere else: never in the store
binary, never in the catalog, never in the admin database."""

from __future__ import annotations

import os
from dataclasses import dataclass, field


def _list(name: str) -> list[str]:
    raw = os.environ.get(name, "")
    return [item.strip() for item in raw.split(",") if item.strip()]


@dataclass
class Settings:
    database: str = "ultrastore-pay.sqlite3"
    host: str = "127.0.0.1"
    port: int = 8791
    # Where this service is reached from the internet: Skrill posts the
    # status report to <base>/v1/skrill/status and sends the buyer back to
    # <base>/pay/<order>/return.
    public_base_url: str = "http://127.0.0.1:8791"
    environment: str = "sandbox"          # "sandbox" or "production"
    # Skrill Quick Checkout (the merchant portal: Settings, Developer settings).
    skrill_pay_url: str = "https://pay.skrill.com"
    skrill_pay_to_email: str = ""         # the merchant account
    skrill_secret_word: str = ""          # signs the status report (md5sig)
    skrill_merchant_id: str = ""          # part of that signature
    skrill_api_password: str = ""         # the API / MQI password (status queries, refunds; not used yet)
    # The signed catalog this service prices from, and the keys it trusts.
    catalog_url: str = ""                 # the base: <url>/catalog.json and .sig
    catalog_keys: list[str] = field(default_factory=list)   # hex Ed25519 public keys
    catalog_ttl_seconds: int = 300
    # The licence key: a 32-byte Ed25519 seed, hex. Empty: one is made on the
    # first start and kept in the database.
    licence_seed: str = ""
    # The admin backend fetches the sales with this bearer token.
    admin_token: str = ""
    # Signed URLs for restricted/ (nginx secure_link_md5): the secret the
    # web server was given, the base URL restricted/ is served from, and
    # how long a URL lives.
    restricted_secret: str = ""
    restricted_base_url: str = ""
    restricted_ttl_seconds: int = 600

    @classmethod
    def from_environment(cls) -> "Settings":
        s = cls()
        env = os.environ.get
        s.database = env("ULTRASTORE_PAY_DB", s.database)
        s.host = env("ULTRASTORE_PAY_HOST", s.host)
        s.port = int(env("ULTRASTORE_PAY_PORT", s.port))
        s.public_base_url = env("ULTRASTORE_PAYMENT_BASE_URL", s.public_base_url).rstrip("/")
        s.environment = env("ULTRASTORE_PAYMENT_ENV", s.environment)
        s.skrill_pay_url = env("ULTRASTORE_SKRILL_PAY_URL", s.skrill_pay_url).rstrip("/")
        s.skrill_pay_to_email = env("ULTRASTORE_SKRILL_PAY_TO_EMAIL", "").strip()
        s.skrill_secret_word = env("ULTRASTORE_SKRILL_SECRET_WORD", "")
        s.skrill_merchant_id = env("ULTRASTORE_SKRILL_MERCHANT_ID", "").strip()
        s.skrill_api_password = env("ULTRASTORE_SKRILL_API_PASSWORD", "")
        s.catalog_url = env("ULTRASTORE_PAY_CATALOG_URL", "").rstrip("/")
        s.catalog_keys = _list("ULTRASTORE_PAY_CATALOG_KEYS")
        s.catalog_ttl_seconds = int(env("ULTRASTORE_PAY_CATALOG_TTL", s.catalog_ttl_seconds))
        s.licence_seed = env("ULTRASTORE_PAY_LICENCE_SEED", "").strip()
        s.admin_token = env("ULTRASTORE_PAY_ADMIN_TOKEN", "")
        s.restricted_secret = env("ULTRASTORE_PAY_RESTRICTED_SECRET", "")
        s.restricted_base_url = env("ULTRASTORE_PAY_RESTRICTED_BASE", "").rstrip("/")
        s.restricted_ttl_seconds = int(env("ULTRASTORE_PAY_RESTRICTED_TTL", s.restricted_ttl_seconds))
        return s

    def configured(self) -> list[str]:
        """What is missing for a real checkout; empty when nothing is."""
        missing = []
        if not self.skrill_pay_to_email:
            missing.append("ULTRASTORE_SKRILL_PAY_TO_EMAIL")
        if not self.skrill_secret_word:
            missing.append("ULTRASTORE_SKRILL_SECRET_WORD")
        if not self.skrill_merchant_id:
            missing.append("ULTRASTORE_SKRILL_MERCHANT_ID")
        if not self.catalog_url:
            missing.append("ULTRASTORE_PAY_CATALOG_URL")
        if not self.catalog_keys:
            missing.append("ULTRASTORE_PAY_CATALOG_KEYS")
        return missing
