"""Settings, from the environment (or a .env file the service manager loads)."""

from __future__ import annotations

import os
from dataclasses import dataclass


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
        return s

    def configured(self) -> list[str]:
        return [] if self.operator_token else ["ULTRASTORE_MARKET_OPERATOR_TOKEN"]
