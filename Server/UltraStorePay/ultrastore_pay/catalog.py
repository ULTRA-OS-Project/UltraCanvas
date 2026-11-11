"""The store's catalog, as this service prices from it.

The same signed catalog.json the stores read, verified with the same kind
of key (Apps/UltraStore/core/Verifier.h): the price and the codes come from
it, so what the buyer saw in the store is what is charged. The arithmetic
on the discount is the store's (StoreQuery.cpp, DiscountedPrice): hundredths,
half up, never below zero, only in the app's currency."""

from __future__ import annotations

import json
import time
import urllib.request
from dataclasses import dataclass, field
from typing import Callable

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey


class CatalogError(Exception):
    pass


Fetcher = Callable[[str], bytes]


def http_fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=20) as r:   # noqa: S310 - the catalog's own https URL
        return r.read()


def cents(decimal: str) -> int | None:
    """Hundredths of a decimal string such as "29.90"; None when it is not one."""
    whole, frac, digits, point, any_ = 0, 0, 0, False, False
    for c in decimal:
        if c == ".":
            if point:
                return None
            point = True
            continue
        if not c.isdigit():
            return None
        any_ = True
        if not point:
            whole = whole * 10 + int(c)
        elif digits < 2:
            frac = frac * 10 + int(c)
            digits += 1
    if not any_:
        return None
    if digits == 1:
        frac *= 10
    return whole * 100 + frac


def decimal(value: int) -> str:
    return f"{value // 100}.{value % 100:02d}"


def normalize_code(code: str) -> str:
    return "".join(c for c in code if not c.isspace()).upper()


@dataclass
class Pricing:
    model: str = "free"
    price: str = ""
    currency: str = ""
    trial_days: int = 0


@dataclass
class App:
    id: str
    name: str
    pricing: Pricing
    restricted_files: list[str] = field(default_factory=list)   # restricted/<sha256>.<format>, from the package URLs
    updates: str = "always"              # what a purchase includes: always, never (the bought version only), until
    updates_until: str = ""              # the day, for "until"


@dataclass
class Code:
    code: str
    kind: str = "discount"
    discount_percent: int = 0
    discount_amount: str = ""
    currency: str = ""
    apps: list[str] = field(default_factory=list)
    valid_from: str = ""
    valid_until: str = ""
    affiliate: str = ""

    def applies_to(self, app_id: str) -> bool:
        return not self.apps or app_id in self.apps

    def valid_on(self, day: str) -> bool:
        return (not self.valid_from or day >= self.valid_from) and (not self.valid_until or day <= self.valid_until)


@dataclass
class Catalog:
    sequence: int
    apps: dict[str, App]
    codes: dict[str, Code]

    def find_code(self, text: str) -> Code | None:
        return self.codes.get(normalize_code(text))


def parse_catalog(text: bytes) -> Catalog:
    try:
        root = json.loads(text)
    except ValueError as e:
        raise CatalogError(f"the catalog is not valid JSON: {e}") from e
    apps: dict[str, App] = {}
    for a in root.get("apps", []):
        pr = a.get("pricing") or {}
        pricing = Pricing(model=pr.get("model", "free"), price=str(pr.get("price", "")), currency=pr.get("currency", ""),
                          trial_days=int(pr.get("trial_days", 0) or 0))
        files = []
        for r in a.get("releases", []):
            for p in r.get("packages", []):
                url = p.get("url", "")
                if "/restricted/" in url:
                    files.append(url[url.index("/restricted/") + 1:])
        up = a.get("updates") or {}
        period = up.get("period", "always")
        if period not in ("always", "never", "until"):
            period = "always"
        apps[a["id"]] = App(id=a["id"], name=a.get("name", a["id"]), pricing=pricing, restricted_files=files,
                            updates=period, updates_until=str(up.get("until", "")) if period == "until" else "")
    codes: dict[str, Code] = {}
    for c in root.get("codes", []):
        code = Code(code=normalize_code(c["code"]), kind=c.get("kind", "discount"), discount_percent=int(c.get("discount_percent", 0) or 0),
                    discount_amount=str(c.get("discount_amount", "")), currency=c.get("currency", ""), apps=list(c.get("apps", [])),
                    valid_from=c.get("valid_from", ""), valid_until=c.get("valid_until", ""), affiliate=c.get("affiliate", ""))
        codes[code.code] = code
    return Catalog(sequence=int(root.get("sequence", 0)), apps=apps, codes=codes)


def verify_signature(catalog_bytes: bytes, signature_text: bytes, keys_hex: list[str]) -> None:
    sig = signature_text.strip()
    signature = bytes.fromhex(sig.decode()) if len(sig) == 128 else sig
    if len(signature) != 64:
        raise CatalogError("the catalog's signature file is not a signature")
    for hex_key in keys_hex:
        try:
            Ed25519PublicKey.from_public_bytes(bytes.fromhex(hex_key)).verify(signature, catalog_bytes)
            return
        except (InvalidSignature, ValueError):
            continue
    raise CatalogError("the catalog's signature matches none of the trusted keys")


def discounted(pricing: Pricing, code: Code | None) -> tuple[int, bool]:
    """The price in hundredths after the code, and whether the code applied."""
    price = cents(pricing.price)
    if price is None:
        raise CatalogError(f"the price {pricing.price!r} is not a decimal")
    if not code:
        return price, False
    if code.discount_percent > 0:
        return max(0, price - (price * code.discount_percent + 50) // 100), True
    if code.discount_amount:
        if code.currency and code.currency != pricing.currency:
            return price, False
        off = cents(code.discount_amount)
        if off is None:
            return price, False
        return max(0, price - off), True
    return price, True   # a partner code without a discount still counts


class CatalogSource:
    """The catalog, fetched and verified, kept for a while."""

    def __init__(self, base_url: str, keys_hex: list[str], ttl_seconds: int = 300, fetcher: Fetcher = http_fetch) -> None:
        self.base_url = base_url.rstrip("/")
        self.keys = keys_hex
        self.ttl = ttl_seconds
        self.fetch = fetcher
        self._catalog: Catalog | None = None
        self._fetched_at = 0.0

    def current(self) -> Catalog:
        if self._catalog and time.monotonic() - self._fetched_at < self.ttl:
            return self._catalog
        if not self.base_url:
            raise CatalogError("no catalog URL is configured")
        body = self.fetch(self.base_url + "/catalog.json")
        sig = self.fetch(self.base_url + "/catalog.json.sig")
        verify_signature(body, sig, self.keys)
        catalog = parse_catalog(body)
        if self._catalog and catalog.sequence < self._catalog.sequence:
            raise CatalogError("the server offered an older catalog than the one already seen")
        self._catalog, self._fetched_at = catalog, time.monotonic()
        return catalog

    def forget(self) -> None:
        self._catalog = None
