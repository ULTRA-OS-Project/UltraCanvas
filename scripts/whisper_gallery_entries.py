#!/usr/bin/env python3
"""The UltraAI gallery's bundle entries for the whisper-server archives a build made.

Reads the archives in --dir, named
    whisper-server-<version>-<revision>-<platform>-<accel>.tar.gz | .zip
and prints, as JSON, the "bundles" list of the gallery index (format
version 1, UltraAI/runtime/src/LocalRuntimeBuiltinData.cpp). Run by
.github/workflows/whisper-server.yml when it publishes a release:

    {"engine": "whisper-server", "version": "v1.9.0",
     "bundles": [{"platform": ..., "accel": ..., "url": ..., "sha256": ...,
                  "size": ..., "minOs": ...}, ...]}

macOS bundles carry --min-macos as "minOs". Standard library only.
"""

import argparse
import hashlib
import json
import pathlib
import re
import sys

NAME = re.compile(
    r"^(?P<engine>whisper-server)-(?P<version>v[0-9][^-]*)-(?P<revision>r[0-9]+)-"
    r"(?P<platform>(?:linux|macos|win)-(?:x64|arm64))-(?P<accel>[a-z0-9]+)\.(?:tar\.gz|zip)$")

# The gallery's order: by platform as the built-in index lists them, then
# CPU before the GPU builds.
PLATFORMS = ["linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "win-x64", "win-arm64"]
ACCELS = ["cpu", "metal", "vulkan", "cuda12", "rocm"]


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", required=True, type=pathlib.Path, help="folder holding the archives")
    ap.add_argument("--url-base", required=True, help="the release's download URL, without a trailing slash")
    ap.add_argument("--min-macos", default="13.3", help='"minOs" of the macOS bundles')
    args = ap.parse_args()

    bundles, versions, engines = [], set(), set()
    for path in sorted(args.dir.iterdir()):
        m = NAME.match(path.name)
        if not m:
            print(f"gallery_entries: not an engine archive, skipped: {path.name}", file=sys.stderr)
            continue
        engines.add(m["engine"])
        versions.add(m["version"])
        entry = {
            "platform": m["platform"],
            "accel": m["accel"],
            "url": f"{args.url_base.rstrip('/')}/{path.name}",
            "sha256": sha256(path),
            "size": path.stat().st_size,
        }
        if m["platform"].startswith("macos"):
            entry["minOs"] = args.min_macos
        bundles.append(entry)
    if not bundles:
        print("gallery_entries: no archives in " + str(args.dir), file=sys.stderr)
        return 1
    if len(versions) != 1 or len(engines) != 1:
        print(f"gallery_entries: one engine and version per release, found {engines} {versions}", file=sys.stderr)
        return 1

    def order(b):
        p = PLATFORMS.index(b["platform"]) if b["platform"] in PLATFORMS else len(PLATFORMS)
        a = ACCELS.index(b["accel"]) if b["accel"] in ACCELS else len(ACCELS)
        return (p, a)

    bundles.sort(key=order)
    json.dump({"engine": engines.pop(), "version": versions.pop(), "bundles": bundles}, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
