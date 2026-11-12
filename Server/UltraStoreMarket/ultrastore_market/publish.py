"""The catalog the operator publishes: generated from the released
submissions and signed with the operator's key - the one signed
catalog.json the stores read (Apps/UltraStore/core/Catalog.cpp), with the
store's trusted_keys, the publishers, the payment service, this service
as market_url, and each app's latest released component as the merchant
sent it.

The operator's signing key is an Ed25519 seed (ULTRASTORE_MARKET_SIGNING_SEED,
or one made on the first publish and kept in the database); its public
half is what a store compiles in or trusts through trusted_keys.
Publishing writes catalog.json and catalog.json.sig (the signature over
the exact bytes, hex) into the publish folder, which the web server
serves as the catalog base; the packages stay where the merchants' URLs
name them until the package store exists.

Version: 0.1.0
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, NoEncryption, PrivateFormat, PublicFormat

from .store import MarketStore

SCHEMA = 2


class SigningKey:
    def __init__(self, seed: bytes) -> None:
        if len(seed) != 32:
            raise ValueError("a signing seed is 32 bytes")
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

    def sign(self, data: bytes) -> bytes:
        return self._key.sign(data)


@dataclass
class PublishReport:
    sequence: int
    apps: int
    components: int
    publishers: int
    left_out: list[str] = field(default_factory=list)   # the released components not listed, with why
    files: list[str] = field(default_factory=list)
    public_key: str = ""
    catalog_bytes: bytes = b""


def build_catalog(store: MarketStore, *, sequence: int, day: str, trusted_keys: list[str], payment_url: str, licence_key: str,
                  market_url: str) -> tuple[dict, list[str]]:
    """The catalog object, and the released components left out (a
    component that names no id, a runtime listed among apps)."""
    merchants = {m.id: m for m in store.merchants()}
    apps: list[dict] = []
    components: list[dict] = []
    publishers: dict[str, dict] = {}
    left_out: list[str] = []
    for entry in store.released(day):
        component = dict(entry["component"])
        if not component.get("id"):
            left_out.append(f"{entry['submission']}: the component names no id")
            continue
        merchant = merchants.get(entry["merchant"])
        publisher_id = str(component.get("publisher") or entry["merchant"])
        component["publisher"] = publisher_id
        if publisher_id not in publishers:
            url = merchant.contact if merchant and merchant.contact.startswith(("http://", "https://")) else ""
            publishers[publisher_id] = {"id": publisher_id, "name": merchant.name if merchant else publisher_id, **({"url": url} if url else {})}
        kind = str(component.get("kind", "app"))
        (apps if kind == "app" else components).append(component)
    catalog = {
        "schema": SCHEMA,
        "sequence": sequence,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "trusted_keys": trusted_keys,
        "publishers": [publishers[k] for k in sorted(publishers)],
        "market_url": market_url,
        "codes": [],
        "apps": apps,
        "components": components,
    }
    if payment_url:
        catalog["payment_url"] = payment_url
    if licence_key:
        catalog["licence_key"] = licence_key
    return catalog, left_out


def publish(store: MarketStore, key: SigningKey, out_dir: str, *, day: str, extra_trusted_keys: list[str], payment_url: str,
            licence_key: str, market_url: str, actor: str) -> PublishReport:
    """catalog.json and its signature into out_dir; the sequence counted
    up past the last publish and the last release."""
    sequence = max(int(store.meta("publish_sequence") or "0"), int(store.meta("catalog_sequence") or "0")) + 1
    trusted = [key.public_hex] + [k for k in extra_trusted_keys if k and k != key.public_hex]
    catalog, left_out = build_catalog(store, sequence=sequence, day=day, trusted_keys=trusted, payment_url=payment_url,
                                      licence_key=licence_key, market_url=market_url)
    body = json.dumps(catalog, indent=2, ensure_ascii=False).encode("utf-8") + b"\n"
    signature = key.sign(body).hex() + "\n"
    folder = Path(out_dir)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "catalog.json").write_bytes(body)
    (folder / "catalog.json.sig").write_text(signature)
    store.set_meta("publish_sequence", str(sequence))
    store.audit(actor, "publish", "catalog", str(sequence), after={"apps": len(catalog["apps"]), "components": len(catalog["components"]),
                                                                 "key": key.public_hex, "left_out": left_out})
    return PublishReport(sequence=sequence, apps=len(catalog["apps"]), components=len(catalog["components"]), publishers=len(catalog["publishers"]),
                         left_out=left_out, files=[str(folder / "catalog.json"), str(folder / "catalog.json.sig")], public_key=key.public_hex,
                         catalog_bytes=body)
