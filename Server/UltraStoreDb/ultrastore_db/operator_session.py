"""The operator's signed session, and the roles - shared by the UltraStore
services, so an operator signed in to the marketplace service (which
keeps the operator accounts) is known to the account service too, with
the same name and role, without a second sign-in.

A session is `op1.<base64url payload>.<hmac-sha256 hex>`: the payload names
the operator (`id`, `name`, `role`) and when the session ends (`exp`, a
Unix time); the MAC is over the payload bytes with the secret both services
are configured with (`ULTRASTORE_MARKET_OPERATOR_SESSION_SECRET`,
`ULTRASTORE_ACCOUNT_OPERATOR_SESSION_SECRET`: the same value). A service
without the secret accepts no session, only its static operator token.

The roles, and what each may do:

| role | may |
|---|---|
| `admin` | everything, the operator accounts included |
| `reviewer` | read the merchants, review and release the submissions, publish the catalog, read the reports, the users and the audit log |
| `support` | read the merchants and the review, handle the reports, send notices, read the users and act for them (a notice, sign out everywhere, release the archive), read the audit log |
| `auditor` | read everything, change nothing |

Version: 0.1.0
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import time

PREFIX = "op1."

READ = ("merchants.read", "review.read", "reports.read", "notices.read", "terms.read", "policy.read", "audit.read", "users.read", "operators.read")
PERMISSIONS: dict[str, frozenset[str]] = {
    "admin": frozenset(READ + ("merchants.manage", "review.decide", "publish", "reports.handle", "notices.send", "terms.manage", "policy.manage",
                               "users.support", "users.manage", "operators.manage")),
    "reviewer": frozenset(READ + ("review.decide", "publish")),
    "support": frozenset(READ + ("reports.handle", "notices.send", "users.support")),
    "auditor": frozenset(READ),
}
ROLES = tuple(PERMISSIONS)


def allowed(role: str, permission: str) -> bool:
    return permission in PERMISSIONS.get(role, frozenset())


def _b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def _unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def issue(secret: str, *, operator_id: str, name: str, role: str, hours: int = 12, now: float | None = None) -> str:
    payload = json.dumps({"id": operator_id, "name": name, "role": role, "exp": int((now if now is not None else time.time()) + hours * 3600)},
                         separators=(",", ":"), sort_keys=True).encode()
    mac = hmac.new(secret.encode(), payload, hashlib.sha256).hexdigest()
    return PREFIX + _b64(payload) + "." + mac


def verify(secret: str, token: str, now: float | None = None) -> dict | None:
    """The session's payload when the token is this secret's and not over,
    else None."""
    if not secret or not token.startswith(PREFIX):
        return None
    try:
        body_b64, mac = token[len(PREFIX):].split(".", 1)
        payload = _unb64(body_b64)
        if not hmac.compare_digest(hmac.new(secret.encode(), payload, hashlib.sha256).hexdigest(), mac):
            return None
        data = json.loads(payload)
        if not isinstance(data, dict) or data.get("role") not in PERMISSIONS:
            return None
        if int(data.get("exp", 0)) < (now if now is not None else time.time()):
            return None
        return data
    except (ValueError, TypeError):
        return None
