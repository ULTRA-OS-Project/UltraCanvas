"""The payment service's licence, read here: `ULIC1.<base64url payload>.
<base64url signature>`, Ed25519 over the payload bytes with the payment
service's licence key (its GET /v1/keys). The account service attaches a
licence to an account only when the signature is the payment service's,
so a made-up token never becomes a licence that follows a person."""

from __future__ import annotations

import base64
import json

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

PREFIX = "ULIC1."


def _unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def verify(token: str, public_hex: str) -> dict | None:
    """The payload when the token is this key's, else None."""
    if not token.startswith(PREFIX):
        return None
    try:
        body_b64, sig_b64 = token[len(PREFIX):].split(".", 1)
        body, sig = _unb64(body_b64), _unb64(sig_b64)
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_hex)).verify(sig, body)
        payload = json.loads(body)
        return payload if isinstance(payload, dict) and payload.get("licence") and payload.get("app") else None
    except (ValueError, InvalidSignature):
        return None
