"""The keys of the identity tokens, and the tokens' signatures.

An identity token is a JWT signed RS256 - the one algorithm every OpenID
Connect client must understand. The key is an RSA key of 2048 bits:

    ULTRASTORE_PORTAL_OIDC_KEY_FILE   a PEM file the service's user alone reads; or
    (unset)                           one made on first use and kept in the database

and its public half is what GET /oauth/jwks answers. A key's id (kid) is its
RFC 7638 thumbprint, so it never has to be chosen. `rotate()` makes a new
key the signer and keeps the old one published until it is retired, so a
token signed a minute before still verifies.

The subject secret is what a person's subject is derived with (provider.py,
`subject`): ULTRASTORE_PORTAL_OIDC_SUBJECT_SECRET, or one made on first use
and kept in the database. It must never change: every service would see
every person as a stranger.

Version: 0.4.0
"""

from __future__ import annotations

import base64
import hashlib
import json
import secrets
from functools import lru_cache
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from django.conf import settings
from django.utils import timezone

from ..store.models import Setting
from .models import SigningKey

ALGORITHM = "RS256"


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def _uint(value: int) -> str:
    return b64(value.to_bytes((value.bit_length() + 7) // 8, "big"))


@lru_cache(maxsize=16)
def _load(pem: str):
    return serialization.load_pem_private_key(pem.encode(), password=None)


def _jwk(private_key) -> dict:
    numbers = private_key.public_key().public_numbers()
    return {"kty": "RSA", "n": _uint(numbers.n), "e": _uint(numbers.e)}


def _kid(private_key) -> str:
    jwk = _jwk(private_key)
    canonical = json.dumps({"e": jwk["e"], "kty": "RSA", "n": jwk["n"]}, separators=(",", ":"), sort_keys=True)
    return b64(hashlib.sha256(canonical.encode()).digest())


def _generate() -> str:
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    return key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()).decode()


def _file_key():
    path = settings.OIDC_KEY_FILE
    return _load(Path(path).read_text()) if path else None


def _stored() -> list[SigningKey]:
    return list(SigningKey.objects.filter(retired__isnull=True).order_by("-id"))


def signing_key():
    """(kid, key): the file's when one is named, else the newest stored
    one, made now when there is none."""
    key = _file_key()
    if key is not None:
        return _kid(key), key
    stored = _stored()
    if not stored:
        pem = _generate()
        row, _ = SigningKey.objects.get_or_create(kid=_kid(_load(pem)), defaults={"pem": pem})
        stored = [row]
    return stored[0].kid, _load(stored[0].pem)


def rotate() -> str:
    """A new stored key becomes the signer; the earlier ones stay published."""
    pem = _generate()
    return SigningKey.objects.create(kid=_kid(_load(pem)), pem=pem).kid


def retire(kid: str) -> bool:
    """A key leaves the published set: tokens it signed stop verifying."""
    return SigningKey.objects.filter(kid=kid, retired__isnull=True).update(retired=timezone.now()) > 0


def _all_keys(include_retired: bool = False) -> dict:
    keys = {}
    file_key = _file_key()
    if file_key is not None:
        keys[_kid(file_key)] = file_key
    rows = SigningKey.objects.all() if include_retired else SigningKey.objects.filter(retired__isnull=True)
    for row in rows:
        keys.setdefault(row.kid, _load(row.pem))
    return keys


def jwks() -> dict:
    signing_key()   # a first key exists before anyone asks for the set
    return {"keys": [{**_jwk(key), "kid": kid, "use": "sig", "alg": ALGORITHM} for kid, key in _all_keys().items()]}


def sign(claims: dict) -> str:
    kid, key = signing_key()
    header = {"alg": ALGORITHM, "typ": "JWT", "kid": kid}
    body = b64(json.dumps(header, separators=(",", ":")).encode()) + "." + b64(json.dumps(claims, separators=(",", ":")).encode())
    return body + "." + b64(key.sign(body.encode(), padding.PKCS1v15(), hashes.SHA256()))


def verify(token: str, include_retired: bool = False) -> dict | None:
    """The claims of a token one of our keys signed, else None. Whether it
    is still in time is the caller's question: a sign-out names the session
    with a token that may have run out."""
    try:
        header_b64, claims_b64, signature_b64 = token.split(".")
        header = json.loads(unb64(header_b64))
        if not isinstance(header, dict) or header.get("alg") != ALGORITHM or not isinstance(header.get("kid"), str):
            return None
        key = _all_keys(include_retired).get(header["kid"])
        if key is None:
            return None
        key.public_key().verify(unb64(signature_b64), (header_b64 + "." + claims_b64).encode(), padding.PKCS1v15(), hashes.SHA256())
        claims = json.loads(unb64(claims_b64))
        return claims if isinstance(claims, dict) else None
    except (ValueError, TypeError, RecursionError, InvalidSignature):   # whatever was sent, it is not one of ours
        return None


def subject_secret() -> bytes:
    if settings.OIDC_SUBJECT_SECRET:
        return settings.OIDC_SUBJECT_SECRET.encode()
    row = Setting.objects.filter(key="oidc_subject_secret").first()
    if row is None:
        row, _ = Setting.objects.get_or_create(key="oidc_subject_secret", defaults={"value": secrets.token_hex(32)})
    return row.value.encode()
