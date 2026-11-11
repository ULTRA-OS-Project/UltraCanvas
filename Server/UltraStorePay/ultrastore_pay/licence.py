"""The licence an app checks, and the receipt.

A licence is "ULIC1." + base64url(payload JSON) + "." + base64url(Ed25519
signature over the payload bytes), signed with this service's licence key;
the public half is GET /v1/keys, which the store trusts (through the catalog
in a later schema). The payload names the licence, the app, the order, the
day and what the purchase includes, so the app verifies it offline."""

from __future__ import annotations

import base64
import json

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, PublicFormat, NoEncryption

PREFIX = "ULIC1."


def _b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def _unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


class LicenceKey:
    def __init__(self, seed: bytes) -> None:
        if len(seed) != 32:
            raise ValueError("a licence seed is 32 bytes")
        self._key = Ed25519PrivateKey.from_private_bytes(seed)

    @classmethod
    def generate(cls) -> "LicenceKey":
        return cls(Ed25519PrivateKey.generate().private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption()))

    @property
    def seed_hex(self) -> str:
        return self._key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption()).hex()

    @property
    def public_hex(self) -> str:
        return self._key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()

    def issue(self, *, licence_id: str, app: str, order_id: str, issued: str, updates: str, updates_until: str = "") -> str:
        payload = {"v": 1, "licence": licence_id, "app": app, "order": order_id, "issued": issued, "updates": updates}
        if updates_until:
            payload["updates_until"] = updates_until
        body = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode()
        return PREFIX + _b64(body) + "." + _b64(self._key.sign(body))


def verify(token: str, public_hex: str) -> dict | None:
    """The payload when the token is this key's, else None."""
    if not token.startswith(PREFIX):
        return None
    try:
        body_b64, sig_b64 = token[len(PREFIX):].split(".", 1)
        body, sig = _unb64(body_b64), _unb64(sig_b64)
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_hex)).verify(sig, body)
        return json.loads(body)
    except (ValueError, InvalidSignature):
        return None
