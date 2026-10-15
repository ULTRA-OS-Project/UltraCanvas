"""Settings, read from the environment (and an optional .env file).

Every variable carries the OAUTH_CALLBACK_ prefix. A provider is served only
when its client id is set; a provider without one answers 404 to every call.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

PREFIX = "OAUTH_CALLBACK_"


@dataclass(frozen=True)
class ClientRegistration:
    client_id: str
    client_secret: str = ""

    @property
    def configured(self) -> bool:
        return bool(self.client_id)


@dataclass(frozen=True)
class Settings:
    # The address the providers redirect to, without a trailing slash. The
    # redirect URI registered with each provider is
    # <public_url>/callback/<provider>.
    public_url: str = "https://oauth-callback.ultraos.eu"
    # How long a started sign-in may wait for the browser (seconds), and how
    # long a delivered code stays for pick-up by the application.
    session_ttl: int = 600
    result_ttl: int = 120
    # Requests per minute, per client address, on the endpoints a stranger
    # could hammer (starting a sign-in, exchanging a token). nginx limits
    # too; this one holds when the service runs without it.
    rate_limit_per_minute: int = 60
    # nginx terminates TLS in front of the service and passes the client's
    # address in X-Forwarded-For; the rate limiter reads it only when told to,
    # since the header is forgeable from a client that reaches the service
    # directly.
    trust_proxy: bool = True
    # Timeout for one call to a provider's token endpoint (seconds).
    provider_timeout: float = 20.0
    clients: dict[str, ClientRegistration] = field(default_factory=dict)

    def client(self, provider_id: str) -> ClientRegistration | None:
        reg = self.clients.get(provider_id)
        return reg if reg and reg.configured else None

    def configured_providers(self) -> list[str]:
        return sorted(p for p, reg in self.clients.items() if reg.configured)

    def redirect_uri(self, provider_id: str) -> str:
        return f"{self.public_url}/callback/{provider_id}"


def load_env_file(path: Path) -> dict[str, str]:
    """KEY=value lines; '#' comments; blank lines ignored; quotes stripped.
    Values already in the environment win over the file."""
    out: dict[str, str] = {}
    if not path.is_file():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        out[key] = value
    return out


def _as_bool(value: str, default: bool) -> bool:
    if value == "":
        return default
    return value.strip().lower() in ("1", "true", "yes", "on")


def _as_int(value: str, default: int) -> int:
    try:
        return int(value) if value != "" else default
    except ValueError:
        return default


def _as_float(value: str, default: float) -> float:
    try:
        return float(value) if value != "" else default
    except ValueError:
        return default


def load_settings(env: dict[str, str] | None = None,
                  provider_ids: tuple[str, ...] = ("google", "microsoft", "yahoo", "dropbox")) -> Settings:
    """Build the settings from `env` (default: the process environment), after
    merging an OAUTH_CALLBACK_ENV_FILE (default: ./.env) underneath it."""
    source = dict(os.environ if env is None else env)
    env_file = Path(source.get(PREFIX + "ENV_FILE", ".env"))
    for key, value in load_env_file(env_file).items():
        source.setdefault(key, value)

    def get(name: str) -> str:
        return source.get(PREFIX + name, "").strip()

    clients = {}
    for provider in provider_ids:
        upper = provider.upper()
        clients[provider] = ClientRegistration(
            client_id=get(f"{upper}_CLIENT_ID"),
            client_secret=get(f"{upper}_CLIENT_SECRET"),
        )
    return Settings(
        public_url=(get("PUBLIC_URL") or Settings.public_url).rstrip("/"),
        session_ttl=max(60, _as_int(get("SESSION_TTL"), Settings.session_ttl)),
        result_ttl=max(10, _as_int(get("RESULT_TTL"), Settings.result_ttl)),
        rate_limit_per_minute=max(0, _as_int(get("RATE_LIMIT"), Settings.rate_limit_per_minute)),
        trust_proxy=_as_bool(get("TRUST_PROXY"), Settings.trust_proxy),
        provider_timeout=max(1.0, _as_float(get("PROVIDER_TIMEOUT"), Settings.provider_timeout)),
        clients=clients,
    )
