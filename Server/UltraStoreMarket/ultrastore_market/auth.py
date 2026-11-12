"""Who is asking.

The operator's window and command line carry the operator's bearer token.
A merchant's backend signs each request with the merchant's Ed25519 key -
the key that signs its submissions, the one Apps/UltraStore/keys describes:
`Authorization: Merchant <id>:<timestamp>:<hex signature>`, the signature
over "<METHOD> <path>\\n<timestamp>\\n" followed by the body bytes. A
timestamp outside the window is refused, so a captured request is not
replayed later. The store's report form and the terms are public."""

from __future__ import annotations

import time

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat


def signed_bytes(method: str, path: str, timestamp: str, body: bytes) -> bytes:
    return f"{method.upper()} {path}\n{timestamp}\n".encode() + body


def sign_request(seed: bytes, merchant_id: str, method: str, path: str, body: bytes = b"", timestamp: str | None = None) -> str:
    """The Authorization header a merchant's client sends."""
    stamp = timestamp or str(int(time.time()))
    signature = Ed25519PrivateKey.from_private_bytes(seed).sign(signed_bytes(method, path, stamp, body)).hex()
    return f"Merchant {merchant_id}:{stamp}:{signature}"


def parse_merchant_header(authorization: str | None) -> tuple[str, str, str] | None:
    """(merchant id, timestamp, signature hex) from the header, or None."""
    if not authorization or not authorization.startswith("Merchant "):
        return None
    parts = authorization[len("Merchant "):].strip().split(":")
    if len(parts) != 3 or not all(parts):
        return None
    return parts[0], parts[1], parts[2]


def verify_request(key_hex: str, method: str, path: str, timestamp: str, signature_hex: str, body: bytes, *, window_seconds: int = 300,
                   now: float | None = None) -> bool:
    try:
        stamp = int(timestamp)
        if abs((now if now is not None else time.time()) - stamp) > window_seconds:
            return False
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(key_hex)).verify(bytes.fromhex(signature_hex), signed_bytes(method, path, timestamp, body))
        return True
    except (ValueError, InvalidSignature):
        return False


def verify_bytes(key_hex: str, signature_hex: str, data: bytes) -> bool:
    """A signature over `data` by the merchant's key (a submission's component)."""
    try:
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(key_hex)).verify(bytes.fromhex(signature_hex), data)
        return True
    except (ValueError, InvalidSignature):
        return False


def public_hex(seed: bytes) -> str:
    return Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()
