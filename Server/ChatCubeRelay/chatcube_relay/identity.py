"""Identities: an Ed25519 public key, and the user id derived from it.

The user id is the lowercase hex of the first 16 bytes of SHA-256 of the
public key, so nobody can claim an id without the matching key, and the
relay never chooses one."""

from __future__ import annotations

import base64
import binascii
import hashlib

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

REGISTER_CONTEXT = b"chatcube-register-v1\n"
LOGIN_CONTEXT = b"chatcube-login-v1\n"


def decode_b64(value: str) -> bytes | None:
    try:
        return base64.b64decode(value, validate=True)
    except (binascii.Error, ValueError, TypeError):
        return None


def user_id_for(public_key: bytes) -> str:
    return hashlib.sha256(public_key).digest()[:16].hex()


def verify(public_key: bytes, message: bytes, signature: bytes) -> bool:
    if len(public_key) != 32 or len(signature) != 64:
        return False
    try:
        Ed25519PublicKey.from_public_bytes(public_key).verify(signature, message)
        return True
    except (InvalidSignature, ValueError):
        return False


def register_message(public_key_b64: str, display_name: str) -> bytes:
    return REGISTER_CONTEXT + public_key_b64.encode() + b"\n" + display_name.encode("utf-8")


def login_message(nonce_b64: str) -> bytes:
    return LOGIN_CONTEXT + nonce_b64.encode()
