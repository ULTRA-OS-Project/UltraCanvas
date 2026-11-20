"""The one catalog the stores read: generated from the published releases
and signed with the operator's key - catalog.json as
Apps/UltraStore/core/Catalog.cpp reads it (schema 2), with the store's
trusted_keys, the publishers, the portal itself as the place to report a
problem (market_url, the store's one root) and every public app and
component.

Publishing is idempotent: the content without the sequence and the date is
hashed, and a publish that would say the same as the last one makes no new
catalog, so every change of state may ask for one. A new catalog takes the
next sequence (a store refuses one below what it accepted), is kept as the
exact signed bytes (CatalogSnapshot) and written with its signature into
the publish folder beside the packages of the published releases:
v1/packages/<sha256>.<format>, or v1/restricted/ for a paid app's without a
trial, which the web server gives to nobody until the portal sells (the
purchase is not built yet). A package no published release names any more leaves the
folder.

The signing key is an Ed25519 seed (ULTRASTORE_PORTAL_SIGNING_SEED, or one
made on the first publish and kept in the database); its public half is
what a store compiles in (Apps/UltraStore/keys/) or learns from
trusted_keys.

Version: 0.1.0
"""

from __future__ import annotations

import hashlib
import json
import logging
import os
import shutil
import time
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, NoEncryption, PrivateFormat, PublicFormat
from django.conf import settings
from django.db import IntegrityError, transaction

from .models import App, AuditEntry, CatalogSnapshot, Package, Release, Setting
from .packages import package_path

SCHEMA = 2
log = logging.getLogger(__name__)


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


def signing_key() -> SigningKey:
    """The operator's key: the configured seed, else the one kept in the
    database, made on the first call."""
    if settings.SIGNING_SEED:
        return SigningKey(bytes.fromhex(settings.SIGNING_SEED))
    row = Setting.objects.filter(key="signing_seed").first()
    if row and row.value:
        return SigningKey(bytes.fromhex(row.value))
    key = SigningKey.generate()
    row, created = Setting.objects.get_or_create(key="signing_seed", defaults={"value": key.seed_hex})
    return key if created else SigningKey(bytes.fromhex(row.value))


def trusted_keys(key: SigningKey | None = None) -> list[str]:
    key = key or signing_key()
    return [key.public_hex] + [k.lower() for k in settings.TRUSTED_KEYS if k and k.lower() != key.public_hex]


def absolute(path: str) -> str:
    """A URL under the store's one root."""
    if not path:
        return ""
    return path if "://" in path else settings.BASE_URL + (path if path.startswith("/") else "/" + path)


def package_url(package: Package, app: App) -> str:
    folder = "restricted" if app.restricted_packages else "packages"
    return f"{settings.CATALOG_URL}/{folder}/{package.file_name}"


def text_map(app: App, text: str) -> dict:
    """A text in the language it was written in; 'en' is what the store
    falls back to, so a text in another language is offered under it too
    until the translations exist."""
    if not text:
        return {}
    language = app.source_language or "en"
    return {language: text} if language == "en" else {language: text, "en": text}


def package_object(package: Package, app: App) -> dict:
    out = {"os": package.os, "arch": package.arch}
    if package.variant_list:
        out["variant"] = package.variant_list
    if package.min_os:
        out["min_os"] = package.min_os
    out.update({"format": package.format, "url": package_url(package, app), "size": package.size, "sha256": package.sha256})
    if package.entry:
        out["entry"] = package.entry
    return out


def release_object(release: Release, app: App) -> dict:
    out = {"version": release.version, "channel": release.channel}
    if release.published_at:
        out["date"] = release.published_at.strftime("%Y-%m-%d")
    if release.notes:
        out["notes"] = text_map(app, release.notes)
    if release.depends:
        out["depends"] = [{"id": d["id"], "version": d["version"]} for d in release.depends]
    out["packages"] = [package_object(p, app) for p in release.packages.all()]
    return out


def app_object(app: App) -> dict:
    """An app or a component as the catalog carries it."""
    out: dict = {"id": app.app_id, "kind": app.kind, "publisher": app.publisher.slug, "name": app.name}
    if app.summary:
        out["summary"] = text_map(app, app.summary)
    if app.description:
        out["description"] = text_map(app, app.description)
    out["source_language"] = app.source_language or "en"
    if app.category:
        out["categories"] = [app.category]
    if app.tag_list:
        out["tags"] = app.tag_list
    if app.license:
        out["license"] = app.license
    if app.icon:
        out["icon"] = absolute(app.icon.url)
    shots = [absolute(s.image.url) for s in app.screenshots.all()]
    if shots:
        out["screenshots"] = shots
    pricing = {"model": app.pricing_model}
    if not app.is_free:
        pricing.update({"price": f"{app.price:.2f}", "currency": app.currency.upper()})
        if app.trial_days:
            pricing["trial_days"] = app.trial_days
    out["pricing"] = pricing
    updates = {"period": app.updates_period}
    if app.updates_period == "until" and app.updates_until:
        updates["until"] = app.updates_until.isoformat()
    out["updates"] = updates
    out["visibility"] = app.visibility
    out["lifecycle"] = app.lifecycle
    if app.successor:
        out["successor"] = app.successor
    if app.age_rating:
        out["age_rating"] = app.age_rating
    for key in ("requires_account", "open_source", "featured"):
        if getattr(app, key):
            out[key] = True
    for key in ("source_url", "homepage", "support_url", "privacy_url"):
        if getattr(app, key):
            out[key] = getattr(app, key)
    published = sorted(app.published_releases(), key=lambda r: r.published_at or r.created, reverse=True)
    out["releases"] = [release_object(r, app) for r in published]
    return out


def public_apps():
    return (App.objects.public().select_related("publisher")
            .prefetch_related("releases__packages", "screenshots").order_by("app_id"))


def build_catalog(key: SigningKey) -> dict:
    """The catalog's content, without the sequence and the date."""
    apps, components, publishers = [], [], {}
    for app in public_apps():
        (apps if app.kind == "app" else components).append(app_object(app))
        p = app.publisher
        if p.slug not in publishers:
            publishers[p.slug] = {"id": p.slug, "name": p.name, **({"url": p.website} if p.website else {})}
    catalog = {
        "schema": SCHEMA,
        "trusted_keys": trusted_keys(key),
        "market_url": settings.MARKET_URL,
        "publishers": [publishers[k] for k in sorted(publishers)],
        "components": components,
        "apps": apps,
    }
    return catalog


def publish_folder() -> Path:
    return Path(settings.PUBLISH_DIR) / "v1"


def _link(source: Path, target: Path) -> None:
    """A package into the publish folder: a hard link where the two share a
    file system, a copy where they do not."""
    if target.is_file() and target.stat().st_size == source.stat().st_size:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_name(target.name + ".part")
    temp.unlink(missing_ok=True)
    try:
        os.link(source, temp)
    except OSError:
        shutil.copyfile(source, temp)
    os.chmod(temp, 0o644)
    temp.replace(target)


def lay_out_packages() -> tuple[int, list[str]]:
    """The published releases' packages beside the catalog, every other one
    taken out of the folder; the count and what is missing from the
    package store."""
    folder = publish_folder()
    wanted: dict[str, set[str]] = {"packages": set(), "restricted": set()}
    missing: list[str] = []
    count = 0
    for app in public_apps():
        where = "restricted" if app.restricted_packages else "packages"
        for release in app.published_releases():
            for package in release.packages.all():
                source = package_path(package.sha256, package.format)
                if not source.is_file():
                    missing.append(f"{release}: {package.file_name}")
                    continue
                _link(source, folder / where / package.file_name)
                wanted[where].add(package.file_name)
                count += 1
    for where, names in wanted.items():
        directory = folder / where
        if directory.is_dir():
            for existing in directory.iterdir():
                # A file under both names (a paid app with a trial is public) stays where it is wanted.
                if existing.is_file() and existing.name not in names:
                    existing.unlink()
    return count, missing


def _write_atomically(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".part")
    temp.write_bytes(data)
    os.chmod(temp, 0o644)
    temp.replace(path)


def write_snapshot(snapshot: CatalogSnapshot) -> None:
    folder = publish_folder()
    # The signature first: a reader between the two writes finds the old
    # catalog with a signature that does not fit, and keeps what it has.
    _write_atomically(folder / "catalog.json.sig", (snapshot.signature + "\n").encode())
    _write_atomically(folder / "catalog.json", bytes(snapshot.body))


def latest_snapshot() -> CatalogSnapshot | None:
    return CatalogSnapshot.objects.order_by("-sequence").first()


def publish_catalog(actor=None, *, force: bool = False) -> tuple[CatalogSnapshot, bool]:
    """The catalog as it stands now, and whether it is a new one. Nothing
    is published when the content is the last catalog's (unless forced)."""
    key = signing_key()
    content = build_catalog(key)
    digest = hashlib.sha256(json.dumps(content, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()
    last = latest_snapshot()
    laid_out, missing = lay_out_packages()
    if missing:
        log.warning("Published packages missing from the package store: %s", ", ".join(missing))
    if last and last.digest == digest and not force:
        if not (publish_folder() / "catalog.json").is_file():
            write_snapshot(last)
        return last, False
    sequence = max(last.sequence if last else 0, settings.SEQUENCE_FLOOR) + 1
    catalog = {"schema": content["schema"], "sequence": sequence, "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    catalog.update({k: v for k, v in content.items() if k != "schema"})
    body = json.dumps(catalog, indent=2, ensure_ascii=False).encode("utf-8") + b"\n"
    try:
        with transaction.atomic():
            snapshot = CatalogSnapshot.objects.create(
                sequence=sequence, body=body, signature=key.sign(body).hex(), public_key=key.public_hex, digest=digest,
                apps=len(content["apps"]), components=len(content["components"]))
            AuditEntry.objects.create(
                actor=actor if getattr(actor, "pk", None) else None, actor_name=str(actor) if actor else "portal",
                action="catalog.publish", target_kind="catalog", target=str(sequence),
                detail={"apps": snapshot.apps, "components": snapshot.components, "key": key.public_hex, "packages": laid_out, "missing": missing})
    except IntegrityError:
        # Two publishes at once: the other one took the sequence, and says the same.
        return latest_snapshot(), False
    write_snapshot(snapshot)
    return snapshot, True


def current_snapshot() -> CatalogSnapshot:
    """The catalog to serve; an empty first one when nothing was published
    yet, so a store always finds a signed catalog."""
    return latest_snapshot() or publish_catalog()[0]


def verify(body: bytes, signature_hex: str, public_key_hex: str) -> bool:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

    try:
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_key_hex)).verify(bytes.fromhex(signature_hex), body)
        return True
    except (InvalidSignature, ValueError):
        return False

