"""Passwords, session tokens, one-time codes and time-based second factors.

A password is kept as scrypt (N 2^15, r 8, p 1, a 16-byte salt) in the form
`scrypt$<n>$<r>$<p>$<salt hex>$<hash hex>`; the service never holds the
password itself and the operator never sees the hash. A session token is 32
random bytes, given to the store once and kept hashed (SHA-256). A code sent
by mail is six digits, kept hashed, good for a few minutes. The second factor
is TOTP (RFC 6238, SHA-1, 30 s, six digits) as UltraAuthenticator and every
other authenticator app speaks it, with one step of drift either way; the
recovery codes are ten of eight characters, kept hashed, each good once."""

from __future__ import annotations

import base64
import hashlib
import hmac
import secrets
import struct
import time
from urllib.parse import quote

SCRYPT_N, SCRYPT_R, SCRYPT_P = 1 << 15, 8, 1
RECOVERY_ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=SCRYPT_N, r=SCRYPT_R, p=SCRYPT_P, dklen=32, maxmem=1 << 27)
    return f"scrypt${SCRYPT_N}${SCRYPT_R}${SCRYPT_P}${salt.hex()}${digest.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        kind, n, r, p, salt_hex, digest_hex = stored.split("$")
        if kind != "scrypt":
            return False
        digest = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt_hex), n=int(n), r=int(r), p=int(p), dklen=32, maxmem=1 << 27)
        return hmac.compare_digest(digest.hex(), digest_hex)
    except ValueError:
        return False


def password_problems(password: str) -> str:
    """Why a password is refused, or empty."""
    if len(password) < 12:
        return "A password is at least 12 characters"
    if len(password) > 256:
        return "A password is at most 256 characters"
    if password.lower() == password and password.isalpha():
        return "A password of letters alone, all lower case, is too easy"
    return ""


def new_token() -> str:
    """A session token as the store keeps it: `us_` and 32 random bytes, hex."""
    return "us_" + secrets.token_hex(32)


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def new_code() -> str:
    return f"{secrets.randbelow(1_000_000):06d}"


def hash_code(code: str, salt: str) -> str:
    return hashlib.sha256((salt + ":" + code).encode()).hexdigest()


# ----- TOTP -----
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
