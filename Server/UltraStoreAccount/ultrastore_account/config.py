"""Settings, from the environment (or a .env file the service manager loads)."""

from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass
class Settings:
    # A URL: mysql://user:password@host:3306/db in production (PostgreSQL
    # works too), sqlite:///path (or a bare path) for tests and development
    # only (ultrastore_db).
    database: str = "sqlite:///ultrastore-account.sqlite3"
    host: str = "127.0.0.1"
    port: int = 8793
    public_base_url: str = "http://127.0.0.1:8793"
    # The operator's bearer token: the operator's window and command line.
    # The static token is the bootstrap admin; an operator signed in to the
    # marketplace service carries a signed session, which this service
    # verifies with the same secret (ultrastore_db.operator_session), so the
    # name and the role reach both services from one sign-in.
    operator_token: str = ""
    operator_session_secret: str = ""
    # The payment service's licence key (its GET /v1/keys, hex): a licence a
    # store attaches to an account is verified against it; empty refuses
    # every licence.
    licence_key: str = ""
    # The mail the codes and the notices go out by: smtp://user:password@host:587
    # (STARTTLS), smtps://...:465, or empty to print them to the log (a
    # development machine).
    smtp_url: str = ""
    mail_from: str = "UltraStore <noreply@localhost>"
    # How long a session lives without being used, in days; a code in minutes;
    # the grace before a requested deletion is carried out, in days; how many
    # machines a licence may be installed on when the licence names no number.
    session_idle_days: int = 90
    code_minutes: int = 10
    deletion_grace_days: int = 30
    default_seats: int = 3

    @classmethod
    def from_environment(cls) -> "Settings":
        s = cls()
        env = os.environ.get
        s.database = env("ULTRASTORE_ACCOUNT_DB", s.database)
        s.host = env("ULTRASTORE_ACCOUNT_HOST", s.host)
        s.port = int(env("ULTRASTORE_ACCOUNT_PORT", s.port))
        s.public_base_url = env("ULTRASTORE_ACCOUNT_BASE_URL", s.public_base_url).rstrip("/")
        s.operator_token = env("ULTRASTORE_ACCOUNT_OPERATOR_TOKEN", "")
        s.operator_session_secret = env("ULTRASTORE_ACCOUNT_OPERATOR_SESSION_SECRET", "").strip()
        s.licence_key = env("ULTRASTORE_ACCOUNT_LICENCE_KEY", "").strip()
        s.smtp_url = env("ULTRASTORE_ACCOUNT_SMTP_URL", "").strip()
        s.mail_from = env("ULTRASTORE_ACCOUNT_MAIL_FROM", s.mail_from)
        s.session_idle_days = int(env("ULTRASTORE_ACCOUNT_SESSION_IDLE_DAYS", s.session_idle_days))
        s.code_minutes = int(env("ULTRASTORE_ACCOUNT_CODE_MINUTES", s.code_minutes))
        s.deletion_grace_days = int(env("ULTRASTORE_ACCOUNT_DELETION_GRACE_DAYS", s.deletion_grace_days))
        s.default_seats = int(env("ULTRASTORE_ACCOUNT_DEFAULT_SEATS", s.default_seats))
        return s

    def configured(self) -> list[str]:
        missing = []
        if not self.operator_token:
            missing.append("ULTRASTORE_ACCOUNT_OPERATOR_TOKEN")
        if not self.licence_key:
            missing.append("ULTRASTORE_ACCOUNT_LICENCE_KEY")
        return missing
