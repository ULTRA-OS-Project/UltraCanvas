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
        return s
