"""One-time codes shared by the UltraStore services: the time-based second
factor (TOTP, RFC 6238: SHA-1, 30 s, six digits, one step of drift either
way - what UltraAuthenticator and every other authenticator app speaks),
the recovery codes (eight characters, kept hashed, each good once), and a
six-digit code sent by mail, kept salted and hashed for a few minutes. The
account service keeps a person's with these; the marketplace service keeps
an operator's.

Version: 0.1.0
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import secrets
import struct
import time
from urllib.parse import quote

RECOVERY_ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"


def new_code() -> str:
    return f"{secrets.randbelow(1_000_000):06d}"


def hash_code(code: str, salt: str) -> str:
    return hashlib.sha256((salt + ":" + code).encode()).hexdigest()


def new_totp_secret() -> str:
    """A base32 secret of 20 bytes, as the authenticator apps take it."""
    return base64.b32encode(secrets.token_bytes(20)).decode().rstrip("=")


def otpauth_url(secret: str, account: str, issuer: str = "UltraStore") -> str:
    return f"otpauth://totp/{quote(issuer)}:{quote(account)}?secret={secret}&issuer={quote(issuer)}&algorithm=SHA1&digits=6&period=30"


def totp_code(secret: str, at: float | None = None, step: int = 30) -> str:
    key = base64.b32decode(secret + "=" * (-len(secret) % 8))
    counter = int((at if at is not None else time.time()) // step)
    mac = hmac.new(key, struct.pack(">Q", counter), hashlib.sha1).digest()
    offset = mac[-1] & 0x0F
    value = (struct.unpack(">I", mac[offset:offset + 4])[0] & 0x7FFFFFFF) % 1_000_000
    return f"{value:06d}"


def verify_totp(secret: str, code: str, at: float | None = None, drift: int = 1) -> bool:
    now = at if at is not None else time.time()
    code = code.strip().replace(" ", "")
    return any(hmac.compare_digest(totp_code(secret, now + d * 30), code) for d in range(-drift, drift + 1))


def new_recovery_codes(count: int = 8) -> list[str]:
    return ["".join(secrets.choice(RECOVERY_ALPHABET) for _ in range(8)) for _ in range(count)]


def hash_recovery(code: str) -> str:
    return hashlib.sha256(code.strip().lower().replace("-", "").encode()).hexdigest()
