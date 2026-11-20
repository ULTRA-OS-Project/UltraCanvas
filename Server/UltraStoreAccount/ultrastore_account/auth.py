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

import hashlib
import hmac
import secrets

SCRYPT_N, SCRYPT_R, SCRYPT_P = 1 << 15, 8, 1


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


# The one-time codes and the second factor are the services' shared ones
# (ultrastore_db.totp), re-exported here so the service and its tests keep
# their names.
from ultrastore_db.totp import (RECOVERY_ALPHABET, hash_code, hash_recovery, new_code, new_recovery_codes, new_totp_secret, otpauth_url,   # noqa: E402,F401
                                totp_code, verify_totp)
