"""The package store: every uploaded archive under its SHA-256, in a folder
the web server never serves. A package becomes public only when the
publish lays it out beside the catalog (catalog.py), so a build in review
is reachable by the reviewer alone.

Version: 0.1.0
"""

from __future__ import annotations

import hashlib
import os
import tarfile
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path

from django.conf import settings
from django.core.exceptions import ValidationError

from . import constants


@dataclass
class StoredPackage:
    sha256: str
    size: int
    format: str
    names: list[str] | None   # the archive's entries, where Python reads the format


def package_path(sha256: str, fmt: str) -> Path:
    return Path(settings.PACKAGE_DIR) / f"{sha256}.{fmt}"


def _archive_names(path: Path, fmt: str) -> list[str] | None:
    """The entries of an archive Python can list; None for a format it
    cannot (7z, tar.zst, an AppImage). A file that is not what its name
    says is refused."""
    try:
        if fmt == "zip":
            with zipfile.ZipFile(path) as archive:
                return archive.namelist()
        if fmt in ("tar", "tar.gz", "tgz", "tar.xz", "tar.bz2"):
            with tarfile.open(path) as archive:
                return archive.getnames()
    except (zipfile.BadZipFile, tarfile.TarError, EOFError, OSError) as error:
        raise ValidationError(f"The file is not a readable {fmt} archive ({error}).")
    if fmt == "7z":
        with open(path, "rb") as handle:
            if handle.read(6) != b"7z\xbc\xaf\x27\x1c":
                raise ValidationError("The file is not a 7z archive.")
    return None


def entry_in_archive(entry: str, names: list[str] | None) -> bool:
    """Whether the entry names a file or a folder of the archive; true when
    the archive could not be listed."""
    if names is None or not entry:
        return True
    wanted = entry.strip("/").removeprefix("./")
    for name in names:
        clean = name.strip("/").removeprefix("./")
        if clean == wanted or clean.startswith(wanted + "/"):
            return True
    return False


def store_upload(upload) -> StoredPackage:
    """An uploaded file into the package store: hashed while it is written,
    named by the hash, checked to be the archive its name says. The same
    bytes sent twice are kept once."""
    fmt = constants.package_format_of(upload.name or "")
    if not fmt:
        endings = ", ".join(ending for ending, _ in constants.PACKAGE_FORMATS)
        raise ValidationError(f"The store unpacks these archives: {endings}")
    limit = settings.MAX_PACKAGE_MB * 1024 * 1024
    if upload.size and upload.size > limit:
        raise ValidationError(f"A package is at most {settings.MAX_PACKAGE_MB} MB.")
    folder = Path(settings.PACKAGE_DIR)
    folder.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    size = 0
    handle, temp_name = tempfile.mkstemp(dir=folder, suffix=".part")
    temp = Path(temp_name)
    try:
        with os.fdopen(handle, "wb") as out:
            for chunk in upload.chunks():
                size += len(chunk)
                if size > limit:
                    raise ValidationError(f"A package is at most {settings.MAX_PACKAGE_MB} MB.")
                digest.update(chunk)
                out.write(chunk)
        if size == 0:
            raise ValidationError("The file is empty.")
        names = _archive_names(temp, fmt)
        sha256 = digest.hexdigest()
        target = package_path(sha256, fmt)
        if target.is_file() and target.stat().st_size == size:
            temp.unlink()
        else:
            os.chmod(temp, 0o640)
            temp.replace(target)
        return StoredPackage(sha256=sha256, size=size, format=fmt, names=names)
    except BaseException:
        temp.unlink(missing_ok=True)
        raise


def remove_if_unused(sha256: str, fmt: str) -> None:
    """The bytes taken away once no package row names them."""
    from .models import Package

    if not Package.objects.filter(sha256=sha256, format=fmt).exists():
        package_path(sha256, fmt).unlink(missing_ok=True)
