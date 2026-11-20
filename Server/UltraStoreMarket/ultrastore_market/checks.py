"""The review pipeline: what the service finds out about a submission once
its packages are in the package store, behind the checks that were marked
*not run* until now (the store's README, *The operator tier*, *What is a
submission?*). Each check ends `ok`, `failed` or `not run`, with findings
the reviewer reads beside it:

- `scans`: every package's archive opened and walked without unpacking -
  a path that would escape the install folder (absolute, `..`, a drive), a
  symbolic link that points outside, an unpacked size out of proportion
  to the download (a zip bomb), an installer inside (an .msi, a .pkg, a
  setup.exe, an install.sh at the top: the store installs an archive, it
  runs no installer) - and, when `ULTRASTORE_MARKET_SCAN_COMMAND` names a
  virus scanner (clamscan's convention: exit 0 clean, 1 found), each file
  handed to it. Without a scanner the archive scan alone is the check, and
  a finding says so.
- `entry`: the file the store will launch is in the archive - the one the
  package names, or, when it names none, one found by the admin backend's
  own rules (the .exe nearest the top on Windows, the .app bundle on macOS,
  the AppImage, a launcher beside a bin/ folder); an AppImage is its own
  entry.
- `smoke`: the build started in a sandbox - only through
  `ULTRASTORE_MARKET_SMOKE_COMMAND`, a runner the operator provides ({file},
  {entry}, {os}, {arch} in its text; exit 0 is a pass), since the service
  itself never runs a merchant's binary. Without one: not run, said so.
- `listing`: the store's page can be made from the component - a name, a
  summary (in English or the source language), categories, an icon, a
  price and currency when not free, an age rating the store knows, URLs
  that are http(s) - and the packages' access matches the pricing (a
  restricted package on an app with a trial fails *Try*). Screenshots and
  a description are findings, not failures.

A failed `scans` or `entry` makes the submission `failed_checks`, which the
review cannot release; the merchant resubmits. The listing's failures and
every finding are the reviewer's to weigh.

Archives: zip, tar.gz / tgz, tar.xz (the standard library), tar.zst (the
`zstandard` module) and 7z (`py7zr`) - a format whose module is missing is
`not run` for that package, with a finding naming the module; an AppImage
is one ELF file.

Version: 0.1.0
"""

from __future__ import annotations

import io
import os
import re
import shlex
import stat
import subprocess
import tarfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

CHECKS = ("scans", "entry", "smoke", "listing")
AGE_RATINGS = ("", "3", "7", "12", "16", "18")
INSTALLER_NAMES = ("setup.exe", "install.sh", "install.bat", "installer.exe", "postinst", "preinst")
INSTALLER_SUFFIXES = (".msi", ".pkg", ".deb", ".rpm")
BOMB_RATIO = 200          # unpacked : download, with the unpacked size over BOMB_MIN_MB
BOMB_MIN_MB = 64


@dataclass
class Entry:
    name: str
    size: int = 0
    is_dir: bool = False
    is_symlink: bool = False
    link_target: str = ""
    executable: bool = False


@dataclass
class Outcome:
    outcome: str = "ok"            # ok, failed, not run
    findings: list[str] = field(default_factory=list)

    def fail(self, text: str) -> None:
        self.outcome = "failed"
        self.findings.append(text)

    def note(self, text: str) -> None:
        self.findings.append(text)

    def skip(self, text: str) -> None:
        if self.outcome != "failed":
            self.outcome = "not run"
        self.findings.append(text)


# ----- the archives -----
def _format_of(name: str) -> str:
    n = name.lower()
    for known in ("tar.zst", "tar.gz", "tgz", "tar.xz", "zip", "7z", "appimage"):
        if n.endswith("." + known):
            return known
    return ""


def archive_entries(path: Path, fmt: str = "") -> tuple[list[Entry], str]:
    """The archive's entries without unpacking it, and why it could not be
    listed (empty when it could)."""
    fmt = fmt or _format_of(path.name)
    try:
        if fmt == "zip":
            with zipfile.ZipFile(path) as z:
                out = []
                for i in z.infolist():
                    mode = (i.external_attr >> 16) & 0xFFFF
                    link = stat.S_ISLNK(mode)
                    out.append(Entry(name=i.filename, size=i.file_size, is_dir=i.is_dir(), is_symlink=link,
                                     link_target=z.read(i).decode("utf-8", "replace") if link and i.file_size < 4096 else "",
                                     executable=bool(mode & 0o111) and not i.is_dir()))
                return out, ""
        if fmt in ("tar.gz", "tgz", "tar.xz", "tar.zst"):
            if fmt == "tar.zst":
                try:
                    import zstandard  # type: ignore
                except ImportError:
                    return [], "tar.zst needs the zstandard module (pip install 'ultrastore-market[scan]')"
                with open(path, "rb") as raw, zstandard.ZstdDecompressor().stream_reader(raw) as stream:
                    data = io.BytesIO(stream.read())
                tar = tarfile.open(fileobj=data, mode="r:")
            else:
                tar = tarfile.open(path, "r:gz" if fmt in ("tar.gz", "tgz") else "r:xz")
            with tar:
                return [Entry(name=m.name, size=m.size, is_dir=m.isdir(), is_symlink=m.issym() or m.islnk(), link_target=m.linkname,
                              executable=bool(m.mode & 0o111) and m.isfile()) for m in tar.getmembers()], ""
        if fmt == "7z":
            try:
                import py7zr  # type: ignore
            except ImportError:
                return [], "7z needs the py7zr module (pip install 'ultrastore-market[scan]')"
            with py7zr.SevenZipFile(path, "r") as z:
                return [Entry(name=i.filename, size=i.uncompressed or 0, is_dir=i.is_directory, executable=False) for i in z.list()], ""
        if fmt == "appimage":
            with open(path, "rb") as f:
                magic = f.read(4)
            if magic != b"\x7fELF":
                return [], "not an AppImage: the file does not start as an ELF executable"
            return [Entry(name=path.name, size=path.stat().st_size, executable=True)], ""
        return [], f"the format {fmt or path.suffix} is not one the store installs"
    except (OSError, zipfile.BadZipFile, tarfile.TarError, EOFError, ValueError) as e:
        return [], f"the archive does not open: {e}"


def unsafe_path(name: str) -> str:
    """Why a member's path must not be unpacked, or empty."""
    if not name:
        return "an empty name"
    if name.startswith(("/", "\\")) or re.match(r"^[A-Za-z]:", name):
        return "an absolute path"
    parts = PurePosixPath(name.replace("\\", "/")).parts
    if ".." in parts:
        return "a path with .."
    if "\x00" in name:
        return "a NUL in the name"
    return ""


def link_escapes(member: str, target: str) -> bool:
    if not target:
        return False
    if target.startswith(("/", "\\")) or re.match(r"^[A-Za-z]:", target):
        return True
    base = PurePosixPath(member.replace("\\", "/")).parent
    depth = 0
    for part in PurePosixPath(str(base / target.replace("\\", "/"))).parts:
        if part == "..":
            depth -= 1
            if depth < 0:
                return True
        elif part not in (".", ""):
            depth += 1
    return False


# ----- the checks -----
def check_scans(packages: list[dict], files: dict[str, Path], *, scan_command: str, timeout: int) -> Outcome:
    out = Outcome()
    for p in packages:
        sha = str(p.get("sha256", ""))
        path = files.get(sha)
        if not path:
            out.skip(f"{sha[:12]}: not in the package store")
            continue
        entries, problem = archive_entries(path, str(p.get("format", "")))
        if problem:
            out.skip(f"{path.name}: {problem}")
            continue
        unpacked = sum(e.size for e in entries if not e.is_dir)
        download = path.stat().st_size
        if unpacked > BOMB_MIN_MB << 20 and download and unpacked // download > BOMB_RATIO:
            out.fail(f"{path.name}: unpacks to {unpacked >> 20} MB from a {max(download >> 20, 1)} MB download - a zip bomb, or near one")
        for e in entries:
            why = unsafe_path(e.name)
            if why:
                out.fail(f"{path.name}: {e.name!r} is {why}, which would escape the install folder")
            if e.is_symlink and link_escapes(e.name, e.link_target):
                out.fail(f"{path.name}: the link {e.name!r} points outside the archive ({e.link_target})")
            base = PurePosixPath(e.name.replace("\\", "/")).name.lower()
            depth = e.name.replace("\\", "/").strip("/").count("/")
            if depth <= 1 and not e.is_dir and (base in INSTALLER_NAMES or base.endswith(INSTALLER_SUFFIXES)):
                out.fail(f"{path.name}: {e.name} is an installer; the store unpacks an archive and runs no installer")
        if scan_command:
            try:
                r = subprocess.run(shlex.split(scan_command.replace("{file}", shlex.quote(str(path)))), capture_output=True, text=True, timeout=timeout)
            except (OSError, subprocess.TimeoutExpired, ValueError) as e:
                out.skip(f"{path.name}: the virus scanner did not run ({e})")
                continue
            if r.returncode == 0:
                out.note(f"{path.name}: the virus scanner found nothing")
            elif r.returncode == 1:
                out.fail(f"{path.name}: the virus scanner found something: {(r.stdout or r.stderr).strip()[-400:]}")
            else:
                out.skip(f"{path.name}: the virus scanner failed (exit {r.returncode}): {(r.stderr or r.stdout).strip()[-400:]}")
        else:
            out.note(f"{path.name}: {len(entries)} entries walked, {unpacked >> 10} KB unpacked; no virus scanner configured (ULTRASTORE_MARKET_SCAN_COMMAND)")
    if not packages:
        out.skip("the submission names no package")
    return out


def _normalise(name: str) -> str:
    return name.replace("\\", "/").strip("/")


def find_entry(entries: list[Entry], os_name: str) -> str:
    """The admin backend's rules (PackageStore.cpp, SuggestEntry), on the listing."""
    names = sorted((_normalise(e.name) for e in entries if not e.is_dir and e.name), key=lambda n: (n.count("/"), n))
    lower = [n.lower() for n in names]
    if os_name == "windows":
        for n, l in zip(names, lower):
            if l.endswith(".exe") and not l.endswith("uninstall.exe"):
                return n
        return ""
    if os_name == "macos":
        for n, l in zip(names, lower):
            at = l.find(".app/")
            if at >= 0:
                return n[:at + 4]
            if l.endswith(".app"):
                return n
        return ""
    for n, l in zip(names, lower):
        if l.endswith(".appimage"):
            return n
    # A launcher beside bin/: <dir>/<name> with <dir>/bin/<name>, or <name> with bin/<name>.
    present = set(names)
    for n in names:
        parts = n.split("/")
        base = parts[-1]
        folder = "/".join(parts[:-1])
        if base and "." not in base and ((folder + "/bin/" + base).lstrip("/") in present or ("bin/" + base) in present and not folder):
            return n
    for e in sorted((e for e in entries if e.executable and not e.is_dir), key=lambda e: (_normalise(e.name).count("/"), e.name)):
        n = _normalise(e.name)
        if "." not in n.split("/")[-1] and n.count("/") <= 1:
            return n
    return ""


def check_entry(component: dict, packages: list[dict], files: dict[str, Path]) -> Outcome:
    out = Outcome()
    seen = False
    for release in component.get("releases", []):
        for p in release.get("packages", []):
            sha = str(p.get("sha256", ""))
            path = files.get(sha)
            label = f"{p.get('os', '?')}/{p.get('arch', '?')} {sha[:12]}"
            if not path:
                out.skip(f"{label}: not in the package store")
                continue
            seen = True
            fmt = str(p.get("format", "")) or _format_of(path.name)
            entries, problem = archive_entries(path, fmt)
            if problem:
                out.skip(f"{label}: {problem}")
                continue
            if fmt == "appimage":
                out.note(f"{label}: an AppImage is its own entry")
                continue
            named = _normalise(str(p.get("entry", "")))
            present = {_normalise(e.name) for e in entries}
            present |= {n[:n.find(".app/") + 4] for n in present if ".app/" in n}
            if named:
                if named in present or named.rstrip("/") in present:
                    out.note(f"{label}: the entry {named} is in the archive")
                else:
                    found = find_entry(entries, str(p.get("os", "")))
                    out.fail(f"{label}: the entry {named} is not in the archive" + (f" (found {found} instead)" if found else ""))
            else:
                found = find_entry(entries, str(p.get("os", "")))
                if found:
                    out.note(f"{label}: no entry named; {found} found by the store's rules")
                else:
                    out.fail(f"{label}: no entry named and none found in the archive")
    if not seen and out.outcome != "failed":
        out.skip("no package in the package store yet")
    return out


def check_smoke(component: dict, packages: list[dict], files: dict[str, Path], *, smoke_command: str, timeout: int) -> Outcome:
    out = Outcome()
    if not smoke_command:
        out.skip("no smoke runner configured (ULTRASTORE_MARKET_SMOKE_COMMAND: a sandbox that starts the build and exits 0 when a window shows)")
        return out
    for release in component.get("releases", []):
        for p in release.get("packages", []):
            sha = str(p.get("sha256", ""))
            path = files.get(sha)
            label = f"{p.get('os', '?')}/{p.get('arch', '?')} {sha[:12]}"
            if not path:
                out.skip(f"{label}: not in the package store")
                continue
            command = smoke_command
            for key, value in (("{file}", str(path)), ("{entry}", str(p.get("entry", ""))), ("{os}", str(p.get("os", ""))), ("{arch}", str(p.get("arch", "")))):
                command = command.replace(key, shlex.quote(value))
            try:
                r = subprocess.run(shlex.split(command), capture_output=True, text=True, timeout=timeout)
            except (OSError, subprocess.TimeoutExpired, ValueError) as e:
                out.skip(f"{label}: the smoke runner did not finish ({e})")
                continue
            if r.returncode == 0:
                out.note(f"{label}: started and answered: {(r.stdout or 'ok').strip()[-200:]}")
            else:
                out.fail(f"{label}: the smoke run failed (exit {r.returncode}): {(r.stderr or r.stdout).strip()[-400:]}")
    return out


def _text(value) -> dict[str, str]:
    if isinstance(value, str):
        return {"": value} if value.strip() else {}
    if isinstance(value, dict):
        return {k: v for k, v in value.items() if isinstance(v, str) and v.strip()}
    return {}


def _is_url(value: str) -> bool:
    return value.startswith(("http://", "https://")) and len(value) > 8


def check_listing(component: dict, packages: list[dict]) -> Outcome:
    out = Outcome()
    kind = str(component.get("kind", "app"))
    name = str(component.get("name", "")).strip()
    if not name:
        out.fail("no name")
    if kind != "app":
        out.note(f"a {kind}: the listing needs the name alone")
        return out
    summary = _text(component.get("summary"))
    source = str(component.get("source_language", "")) or "en"
    if not summary:
        out.fail("no summary: the store's card shows one line under the name")
    elif "en" not in summary and source not in summary and "" not in summary:
        out.fail(f"the summary is in {', '.join(sorted(summary))} but not in English or the source language ({source})")
    else:
        out.note(f"summary in {', '.join(sorted(k or source for k in summary))}")
    description = _text(component.get("description"))
    if not description:
        out.note("no description: the app page shows the summary alone")
    if not component.get("categories"):
        out.fail("no category: the store lists by category")
    if not str(component.get("icon", "")).strip():
        out.fail("no icon")
    shots = component.get("screenshots") or []
    if not shots:
        out.note("no screenshots: the app page shows none")
    else:
        out.note(f"{len(shots)} screenshot(s)")
    pricing = component.get("pricing") or {}
    model = str(pricing.get("model", "free"))
    if model != "free":
        price, currency = str(pricing.get("price", "")), str(pricing.get("currency", ""))
        if not price or not currency:
            out.fail(f"the pricing is {model} without a price and a currency")
        else:
            out.note(f"{model}: {price} {currency}" + (f", {pricing.get('trial_days')}-day trial" if pricing.get("trial_days") else ""))
    age = str(component.get("age_rating", ""))
    if age not in AGE_RATINGS:
        out.fail(f"the age rating {age!r} is none the store knows ({', '.join(a for a in AGE_RATINGS if a)})")
    for key in ("homepage", "support_url", "privacy_url", "source_url"):
        value = str(component.get(key, ""))
        if value and not _is_url(value):
            out.fail(f"{key} is not an http(s) URL: {value}")
    if component.get("requires_account") and not str(component.get("privacy_url", "")):
        out.note("the app needs an account of its own and names no privacy URL")
    # The packages' access against the pricing.
    restricted = [p for p in packages if str(p.get("access", "")) == "restricted" or "restricted/" in str(p.get("file") or p.get("url") or "")]
    if restricted and model != "free" and int(pricing.get("trial_days", 0) or 0) > 0:
        out.fail("a restricted package on an app with a trial: Try installs the same package and fails without a licence")
    if model != "free" and packages and not restricted:
        out.note("a paid app whose packages are all public: the licence alone gates it")
    if not component.get("releases"):
        out.fail("no release")
    return out


def run_checks(component: dict, packages: list[dict], *, package_dir: str, scan_command: str = "", smoke_command: str = "",
               timeout: int = 120) -> dict[str, Outcome]:
    """Every pipeline check on a submission; the packages' files from the
    package store by their hash."""
    files: dict[str, Path] = {}
    if package_dir:
        for p in packages:
            if p.get("uploaded"):
                sha = str(p.get("sha256", "")).lower()
                fmt = str(p.get("format", "")).lower()
                if not fmt:
                    fmt = _format_of(str(p.get("file") or p.get("url") or "")) or "bin"
                candidate = Path(package_dir) / f"{sha}.{fmt}"
                if candidate.is_file():
                    files[sha] = candidate
    results = {
        "scans": check_scans(packages, files, scan_command=scan_command, timeout=timeout) if package_dir else Outcome("not run", ["no package store: the packages stay on the merchant's server, unscanned"]),
        "entry": check_entry(component, packages, files) if package_dir else Outcome("not run", ["no package store"]),
        "smoke": check_smoke(component, packages, files, smoke_command=smoke_command, timeout=timeout) if package_dir else Outcome("not run", ["no package store"]),
        "listing": check_listing(component, packages),
    }
    return results
