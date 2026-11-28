#!/usr/bin/env python3
"""Put the GNU source archives a vcpkg manifest needs into vcpkg's download
cache, fetched from other GNU mirrors, before vcpkg runs.

Why: vcpkg's GNU ports (gperf, libiconv, gettext, ...) download only from
ftpmirror.gnu.org and ftp.gnu.org. When both time out - as they did twice in
one morning for gperf, failing the Android arm64 sysroot after half an hour -
the whole build fails although dozens of other GNU mirrors serve the same
file. vcpkg takes an archive already in its downloads folder when the file's
SHA512 is the one the port names, so fetching it beforehand from any mirror
is enough, and the hash keeps it honest.

What it does:
  1. the ports the manifest needs: its dependencies (with the features asked
     for) and theirs, recursively, from the vcpkg checkout's ports/ and any
     overlay port folders - every dependency, platform-qualified or not, so
     the set is never too small;
  2. in each port's portfile.cmake, every vcpkg_download_distfile whose URLS
     name a GNU host, with its FILENAME and SHA512 (${VERSION} and the like
     filled in from the port's vcpkg.json);
  3. for each archive missing from the downloads folder, or there with
     another hash: the port's own URLs, then the same path on other GNU
     mirrors, until one gives the right SHA512.

A failure here is never fatal: vcpkg then tries itself, as before.

Usage:
  vcpkg_seed_gnu_sources.py --vcpkg-root DIR --manifest DIR --downloads DIR
                            [--feature NAME]... [--overlay DIR]... [--mirror URL]...
                            [--dry-run]
  --mirror  another GNU tree to try first (its URL ends in /gnu/), e.g. a
            mirror near the runner
"""

import argparse
import hashlib
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

# The same tree on other hosts, tried after the port's own URLs: the GNU
# project's mirror list (https://www.gnu.org/prep/ftp.html), spread over
# continents and operators.
MIRRORS = [
    "https://mirrors.kernel.org/gnu/",
    "https://mirror.csclub.uwaterloo.ca/gnu/",
    "https://mirrors.ocf.berkeley.edu/gnu/",
    "https://ftp.fau.de/gnu/",
    "https://mirror.us-midwest-1.nexcess.net/gnu/",
    "https://mirrors.dotsrc.org/gnu/",
]
GNU_PATH = re.compile(r"https?://(?:ftpmirror\.gnu\.org|ftp\.gnu\.org/pub|ftp\.gnu\.org)/gnu/(.+)$")
DISTFILE = re.compile(r"vcpkg_download_distfile\s*\((.*?)\)", re.S)
TIMEOUT = 60


def log(message):
    print(f"vcpkg_seed_gnu_sources: {message}", file=sys.stderr)


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def port_dir(name, ports_roots):
    for root in ports_roots:
        candidate = Path(root) / name
        if (candidate / "vcpkg.json").is_file():
            return candidate
    return None


def dependency_names(entries, chosen_features=()):
    """Names from a dependencies list; features named on a dependency count."""
    out = []
    for entry in entries or []:
        if isinstance(entry, str):
            out.append((entry, ()))
        elif isinstance(entry, dict) and "name" in entry:
            feats = [f if isinstance(f, str) else f.get("name", "") for f in entry.get("features", [])]
            out.append((entry["name"], tuple(f for f in feats if f)))
    return out


def closure(manifest, features, ports_roots):
    """Every port the manifest can pull in, with the features it asks of each."""
    wanted = {}
    queue = list(dependency_names(manifest.get("dependencies")))
    for feature in features:
        queue += dependency_names(manifest.get("features", {}).get(feature, {}).get("dependencies"))
    while queue:
        name, feats = queue.pop()
        base = name.split(":")[0]
        before = wanted.get(base)
        merged = set(before or ()) | set(feats)
        if before is not None and merged == before:
            continue
        wanted[base] = merged
        directory = port_dir(base, ports_roots)
        if not directory:
            continue
        spec = read_json(directory / "vcpkg.json") or {}
        queue += dependency_names(spec.get("dependencies"))
        names = set(merged)
        if before is None:
            names |= {f if isinstance(f, str) else f.get("name", "") for f in spec.get("default-features", [])}
        for feature in names:
            queue += dependency_names(spec.get("features", {}).get(feature, {}).get("dependencies"))
    return wanted


def substitute(text, spec):
    version = str(spec.get("version") or spec.get("version-semver") or spec.get("version-date")
                  or spec.get("version-string") or "")
    values = {"VERSION": version, "PORT": spec.get("name", "")}
    return re.sub(r"\$\{(\w+)\}", lambda m: values.get(m.group(1), m.group(0)), text)


def gnu_archives(directory):
    spec = read_json(directory / "vcpkg.json") or {}
    try:
        portfile = (directory / "portfile.cmake").read_text(encoding="utf-8")
    except OSError:
        return []
    found = []
    for block in DISTFILE.findall(portfile):
        block = substitute(block, spec)
        urls = re.findall(r'"(https?://[^"]+)"', block)
        filename = re.search(r"FILENAME\s+\"?([^\s\")]+)\"?", block)
        sha512 = re.search(r"SHA512\s+([0-9a-fA-F]{128})", block)
        paths = [m.group(1) for m in (GNU_PATH.match(u) for u in urls) if m]
        if not paths or not filename or not sha512 or "${" in filename.group(1) or "${" in paths[0]:
            continue
        found.append((filename.group(1), sha512.group(1).lower(), urls, paths[0]))
    return found


def sha512_of(path):
    digest = hashlib.sha512()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(url, target):
    request = urllib.request.Request(url, headers={"User-Agent": "vcpkg-seed/1"})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response, open(target, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--vcpkg-root", required=True)
    parser.add_argument("--manifest", required=True, help="the folder holding vcpkg.json")
    parser.add_argument("--downloads", required=True)
    parser.add_argument("--feature", action="append", default=[])
    parser.add_argument("--overlay", action="append", default=[], help="an overlay ports folder")
    parser.add_argument("--mirror", action="append", default=[], help="a GNU tree ending in /gnu/, tried first")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    manifest = read_json(Path(args.manifest) / "vcpkg.json")
    if manifest is None:
        log(f"no vcpkg.json in {args.manifest}; nothing to seed")
        return 0
    ports_roots = list(args.overlay) + [str(Path(args.vcpkg_root) / "ports")]
    downloads = Path(args.downloads)
    downloads.mkdir(parents=True, exist_ok=True)

    archives = {}
    for name in sorted(closure(manifest, args.feature, ports_roots)):
        directory = port_dir(name, ports_roots)
        if directory:
            for archive in gnu_archives(directory):
                archives[archive[0]] = (name,) + archive
    if not archives:
        log("the manifest needs no GNU archive")
        return 0

    for filename, (port, _, sha512, urls, gnu_path) in sorted(archives.items()):
        target = downloads / filename
        if target.is_file() and sha512_of(target) == sha512:
            log(f"{filename} ({port}): already there")
            continue
        first = [m if m.endswith("/") else m + "/" for m in args.mirror]
        candidates = [m + gnu_path for m in first] + urls + [m + gnu_path for m in MIRRORS]
        if args.dry_run:
            log(f"{filename} ({port}): would fetch from {len(candidates)} places, first {candidates[0]}")
            continue
        partial = downloads / (filename + ".seed-part")
        for url in candidates:
            try:
                fetch(url, partial)
                if sha512_of(partial) != sha512:
                    log(f"{filename}: {url} gave another file; next mirror")
                    continue
                os.replace(partial, target)
                log(f"{filename} ({port}): from {url}")
                break
            except Exception as error:  # a mirror down, slow or refusing: the next one
                log(f"{filename}: {url} failed ({error.__class__.__name__}); next mirror")
        else:
            log(f"{filename} ({port}): no mirror had it; vcpkg will try itself")
        if partial.exists():
            partial.unlink()
    return 0


if __name__ == "__main__":
    sys.exit(main())
