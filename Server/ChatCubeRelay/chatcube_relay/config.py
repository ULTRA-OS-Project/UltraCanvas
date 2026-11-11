"""Settings, from the environment (or a .env file the service manager loads)."""

from __future__ import annotations

import os
from dataclasses import dataclass, field


def _list(name: str) -> list[str]:
    raw = os.environ.get(name, "")
    return [item.strip() for item in raw.split(",") if item.strip()]


@dataclass
class Settings:
    database: str = "chatcube-relay.sqlite3"
    host: str = "127.0.0.1"
    port: int = 8790
    token_ttl_seconds: int = 30 * 24 * 3600
    challenge_ttl_seconds: int = 120
    # Envelopes are text messages; files travel peer to peer.
    max_envelope_bytes: int = 64 * 1024
    max_batch: int = 256                 # copies in one POST /v1/messages
    max_group_members: int = 64
    queue_ttl_seconds: int = 30 * 24 * 3600
    stun_urls: list[str] = field(default_factory=list)
    turn_urls: list[str] = field(default_factory=list)
    turn_secret: str = ""                # coturn static-auth-secret (use-auth-secret)
    turn_ttl_seconds: int = 12 * 3600
    # This ChatCube's Telegram application (my.telegram.org), handed to signed-in
    # clients by GET /v1/telegram-credentials so the keys live here, not in the
    # application: 0 / "" = not configured.
    telegram_api_id: int = 0
    telegram_api_hash: str = ""

    # ---- account recovery (README: "Account recovery") ----------------------
    # The key the relay encrypts the recovery copies of its users' keys with:
    # 32 random bytes, base64 (`openssl rand -base64 32`). Without it, and
    # without both an e-mail and an SMS sender, the relay offers no recovery.
    recovery_key: str = ""
    # E-mail: an SMTP server. smtp_security: "starttls" (port 587), "ssl"
    # (port 465) or "none" (a local MTA).
    smtp_host: str = ""
    smtp_port: int = 587
    smtp_security: str = "starttls"
    smtp_user: str = ""
    smtp_password: str = ""
    smtp_from: str = ""                  # "ChatCube <no-reply@example.org>"
    # SMS: "twilio", "webhook" (a POST of {"to", "text"} to a URL, with a
    # Bearer token when one is set), or "" for none.
    sms: str = ""
    twilio_account_sid: str = ""
    twilio_auth_token: str = ""
    twilio_from: str = ""                # the sending number, "+1555..."
    sms_webhook_url: str = ""
    sms_webhook_token: str = ""
    # Development and tests only: every e-mail and SMS is appended to this
    # file (a JSON object per line) instead of being sent.
    outbox: str = ""
    # Where the e-mailed links point: the relay's public https:// address
    # ("https://relay.example.org"). Without it, the address the request came
    # in on - right without a proxy in front, wrong behind one.
    public_url: str = ""
    confirm_link_ttl_seconds: int = 24 * 3600     # an address confirmation link
    recovery_ttl_seconds: int = 30 * 60           # a recovery, its link and its polling
    code_ttl_seconds: int = 600          # a code (the phone's) is good for ten minutes
    code_attempts: int = 5               # then it is spent
    sends_per_hour: int = 5              # links or codes to one address or account in an hour
    # Recoveries started from one network address in an hour. Higher than
    # the per-address limit: many people can share one (a company's NAT, a
    # carrier's CGNAT), and each address has its own limit anyway.
    recovery_starts_per_client: int = 30

    @classmethod
    def from_environment(cls) -> "Settings":
        s = cls()
        s.database = os.environ.get("CHATCUBE_RELAY_DB", s.database)
        s.host = os.environ.get("CHATCUBE_RELAY_HOST", s.host)
        s.port = int(os.environ.get("CHATCUBE_RELAY_PORT", s.port))
        s.token_ttl_seconds = int(os.environ.get("CHATCUBE_RELAY_TOKEN_TTL", s.token_ttl_seconds))
        s.stun_urls = _list("CHATCUBE_RELAY_STUN_URLS")
        s.turn_urls = _list("CHATCUBE_RELAY_TURN_URLS")
        s.turn_secret = os.environ.get("CHATCUBE_RELAY_TURN_SECRET", "")
        s.telegram_api_id = int(os.environ.get("CHATCUBE_RELAY_TELEGRAM_API_ID", "0") or 0)
        s.telegram_api_hash = os.environ.get("CHATCUBE_RELAY_TELEGRAM_API_HASH", "").strip()
        env = lambda name, default="": os.environ.get("CHATCUBE_RELAY_" + name, default).strip()
        s.recovery_key = env("RECOVERY_KEY")
        s.smtp_host = env("SMTP_HOST")
        s.smtp_port = int(env("SMTP_PORT", str(s.smtp_port)) or s.smtp_port)
        s.smtp_security = env("SMTP_SECURITY", s.smtp_security).lower()
        s.smtp_user = env("SMTP_USER")
        s.smtp_password = os.environ.get("CHATCUBE_RELAY_SMTP_PASSWORD", "")
        s.smtp_from = env("SMTP_FROM")
        s.sms = env("SMS").lower()
        s.twilio_account_sid = env("TWILIO_ACCOUNT_SID")
        s.twilio_auth_token = env("TWILIO_AUTH_TOKEN")
        s.twilio_from = env("TWILIO_FROM")
        s.sms_webhook_url = env("SMS_WEBHOOK_URL")
        s.sms_webhook_token = env("SMS_WEBHOOK_TOKEN")
        s.outbox = env("OUTBOX")
        s.public_url = env("PUBLIC_URL").rstrip("/")
        s.recovery_starts_per_client = int(env("RECOVERY_STARTS_PER_CLIENT", str(s.recovery_starts_per_client))
                                           or s.recovery_starts_per_client)
        return s
