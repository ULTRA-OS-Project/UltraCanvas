"""Settings, from the environment (or a .env file the service manager loads)."""

from __future__ import annotations

import os
from dataclasses import dataclass, field


@dataclass
class Settings:
    # A URL: mysql://user:password@host:3306/db in production (PostgreSQL
    # works too), sqlite:///path (or a bare path) for tests and development
    # only (ultrastore_db).
    database: str = "sqlite:///ultrastore-market.sqlite3"
    host: str = "127.0.0.1"
    port: int = 8792
    public_base_url: str = "http://127.0.0.1:8792"
    # The operator's bearer token: the operator's window and command line.
    operator_token: str = ""
    # A merchant's signed request is good for this long around its timestamp.
    request_window_seconds: int = 300
    # The publish (publish.py): where catalog.json and its signature go, the
    # operator's signing seed (32 bytes, hex; empty: made on the first publish
    # and kept in the database), more keys the catalog's trusted_keys name
    # (a key being rotated in), the payment service the catalog names and
    # its licence key.
    publish_dir: str = ""
    signing_seed: str = ""
    trusted_keys: list[str] = field(default_factory=list)
    payment_url: str = ""
    licence_key: str = ""

    @classmethod
    def from_environment(cls) -> "Settings":
        s = cls()
        env = os.environ.get
        s.database = env("ULTRASTORE_MARKET_DB", s.database)
        s.host = env("ULTRASTORE_MARKET_HOST", s.host)
        s.port = int(env("ULTRASTORE_MARKET_PORT", s.port))
        s.public_base_url = env("ULTRASTORE_MARKET_BASE_URL", s.public_base_url).rstrip("/")
        s.operator_token = env("ULTRASTORE_MARKET_OPERATOR_TOKEN", "")
        s.request_window_seconds = int(env("ULTRASTORE_MARKET_REQUEST_WINDOW", s.request_window_seconds))
        s.publish_dir = env("ULTRASTORE_MARKET_PUBLISH_DIR", "")
        s.signing_seed = env("ULTRASTORE_MARKET_SIGNING_SEED", "").strip()
        s.trusted_keys = [k.strip() for k in env("ULTRASTORE_MARKET_TRUSTED_KEYS", "").split(",") if k.strip()]
        s.payment_url = env("ULTRASTORE_MARKET_PAYMENT_URL", "").rstrip("/")
        s.licence_key = env("ULTRASTORE_MARKET_LICENCE_KEY", "").strip()
        return s

    def configured(self) -> list[str]:
        return [] if self.operator_token else ["ULTRASTORE_MARKET_OPERATOR_TOKEN"]
