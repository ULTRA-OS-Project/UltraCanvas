"""The signature on what a client reads.

index.json, catalogue.json and topics.json are each signed with the
server's Ed25519 key: the 64-byte detached signature over the exact bytes
served, as 128 lower-case hex characters - the convention of UltraStore's
catalog.json.sig. The signature comes with the document in the
X-UltraAI-Signature header, so a document and its signature are always of
the same revision, and as <document>.sig beside it for a mirror that copies
files. keys.json lists the public keys a client may pin: the current one,
and any next one announced ahead of a rotation."""

from __future__ import annotations

import json

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.serialization import Encoding, NoEncryption, PrivateFormat, PublicFormat

SIGNATURE_HEADER = "X-UltraAI-Signature"


def canonical_bytes(document: dict) -> bytes:
    """The bytes a document is served and signed as: one serialisation,
    compact and in key order, so the .sig fetched a moment later still
    matches while nothing changed."""
    return json.dumps(document, separators=(",", ":"), sort_keys=True, ensure_ascii=False).encode("utf-8")


class SigningKey:
    def __init__(self, seed: bytes) -> None:
        if len(seed) != 32:
            raise ValueError("a signing seed is 32 bytes (64 hex characters)")
        self._key = Ed25519PrivateKey.from_private_bytes(seed)

    @classmethod
    def generate(cls) -> "SigningKey":
        return cls(Ed25519PrivateKey.generate().private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption()))

    @property
    def seed_hex(self) -> str:
        return self._key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption()).hex()

    @property
    def public_hex(self) -> str:
        return self._key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()

    def sign_hex(self, data: bytes) -> str:
        return self._key.sign(data).hex()


def verify_hex(data: bytes, signature_hex: str, public_hex: str) -> bool:
    try:
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_hex)).verify(bytes.fromhex(signature_hex.strip()), data)
        return True
    except (InvalidSignature, ValueError):
        return False
