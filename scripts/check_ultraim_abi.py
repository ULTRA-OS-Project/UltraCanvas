#!/usr/bin/env python3
"""UltraIM's module ABI: a change to the provider interface must bump its version.

A provider module (ChatCube Direct, Matrix, Telegram - a .so / .dll / .dylib
under Plugins/UltraIM) is called through IUltraIMProviderPlugin's vtable and
hands UltraIMTypes.h's structs back and forth. One built against another
version of those calls past the end of a vtable or reads a struct laid out
differently. ULTRAIM_PLUGIN_HOST_ABI_VERSION (UltraIMPlugins.h) is the gate:
a module exports the version it was built for and the host loads only an
equal one. The gate only works if the number moves whenever the contract
does - and that used to be remembered, not checked: virtual functions were
added three times under version 1.

This script fingerprints the contract - all of UltraIMPlugins.h and
UltraIMTypes.h, without comments, without the text inside string literals
(an error message is not layout) and with whitespace collapsed - and compares
it and the version with scripts/ultraim_abi.txt:

  fingerprint and version as recorded     clean
  fingerprint changed, version not        FAIL: bump the version, then --update
  version bumped                          FAIL until --update records it

  python3 scripts/check_ultraim_abi.py                    # the check (CI: ultraim-abi.yml)
  python3 scripts/check_ultraim_abi.py --update           # record, after a bump
  python3 scripts/check_ultraim_abi.py --update --no-abi-change "why"
                                                          # a change that is not one: a free
                                                          # function, a constant, a default body
  python3 scripts/check_ultraim_abi.py --self-test
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = [
    ROOT / "UltraCanvas/include/UltraIM/UltraIMPlugins.h",
    ROOT / "UltraCanvas/include/UltraIM/UltraIMTypes.h",
]
RECORD = ROOT / "scripts/ultraim_abi.txt"
VERSION_RE = re.compile(r"constexpr\s+int\s+ULTRAIM_PLUGIN_HOST_ABI_VERSION\s*=\s*(\d+)\s*;")


def normalize(text: str) -> str:
    """The text a compiler sees, give or take: no comments, string literals
    emptied, whitespace collapsed, and the version line itself left out (it
    is compared on its own)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    flat = " ".join("".join(out).split())
    flat = VERSION_RE.sub("", flat)
    # Spacing beside punctuation is layout of the text, not of the types.
    return re.sub(r" ?([^\w ]) ?", r"\1", flat)


def current() -> tuple[int, str]:
    digest = hashlib.sha256()
    version = None
    for header in HEADERS:
        text = header.read_text(encoding="utf-8")
        match = VERSION_RE.search(text)
        if match:
            version = int(match.group(1))
        digest.update(header.name.encode() + b"\0" + normalize(text).encode() + b"\0")
    if version is None:
        sys.exit("check_ultraim_abi: ULTRAIM_PLUGIN_HOST_ABI_VERSION not found in UltraIMPlugins.h")
    return version, digest.hexdigest()


def recorded() -> tuple[int, str]:
    values = {}
    for line in RECORD.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition(" ")
        values[key] = value.strip()
    return int(values["version"]), values["fingerprint"]


def write_record(version: int, fingerprint: str, note: str) -> None:
    RECORD.write_text(
        "# UltraIM's module ABI as last recorded - see scripts/check_ultraim_abi.py.\n"
        "# Written by `check_ultraim_abi.py --update`; never edit by hand.\n"
        f"# {note}\n"
        f"version {version}\n"
        f"fingerprint {fingerprint}\n",
        encoding="utf-8")


def self_test() -> int:
    base = "struct A { int x; }; // a note\nclass I { virtual void F(); };"
    cases = [
        (base.replace("// a note", "// another note"), True, "a changed comment"),
        (base.replace(";", " ;"), True, "whitespace"),
        (base.replace("int x;", "int x; int y;"), False, "a new struct member"),
        (base.replace("virtual void F();", "virtual void F(); virtual void G();"), False, "a new virtual"),
        ("/* block */" + base, True, "a block comment"),
    ]
    failures = 0
    for text, same, what in cases:
        if (normalize(text) == normalize(base)) != same:
            print(f"self-test FAIL: {what} {'changed' if same else 'did not change'} the fingerprint")
            failures += 1
    if normalize('E("one");') != normalize('E("two");'):
        print("self-test FAIL: the text of a string literal changed the fingerprint")
        failures += 1
    if normalize("constexpr int ULTRAIM_PLUGIN_HOST_ABI_VERSION = 1; X") != normalize(
            "constexpr int ULTRAIM_PLUGIN_HOST_ABI_VERSION = 2; X"):
        print("self-test FAIL: the version line is part of the fingerprint")
        failures += 1
    print("check_ultraim_abi: self-test " + ("passed" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true", help="record the current version and fingerprint")
    parser.add_argument("--no-abi-change", metavar="WHY",
                        help="with --update: the headers changed but the binary contract did not")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

    version, fingerprint = current()
    old_version, old_fingerprint = recorded()
    fix = ("bump ULTRAIM_PLUGIN_HOST_ABI_VERSION in UltraCanvas/include/UltraIM/UltraIMPlugins.h "
           "(and say what changed in its comment), then run python3 scripts/check_ultraim_abi.py --update")

    if version < old_version:
        print(f"check_ultraim_abi: the version went back, from {old_version} to {version}")
        return 1

    if args.update:
        if fingerprint != old_fingerprint and version == old_version and not args.no_abi_change:
            print("check_ultraim_abi: the provider interface or UltraIMTypes.h changed and the version did not.\n"
                  f"  {fix}.\n  A change that is not part of the binary contract (a free function, a "
                  "constant, a default body) is recorded with --update --no-abi-change \"why\".")
            return 1
        note = (f"recorded with the version bumped to {version}" if not args.no_abi_change
                else f"no ABI change at version {version}: {args.no_abi_change}")
        write_record(version, fingerprint, note)
        print(f"check_ultraim_abi: recorded version {version}, fingerprint {fingerprint[:12]}")
        return 0

    if version == old_version and fingerprint == old_fingerprint:
        print(f"check_ultraim_abi: clean (version {version})")
        return 0
    if version == old_version:
        print("check_ultraim_abi: UltraIMPlugins.h or UltraIMTypes.h changed and ULTRAIM_PLUGIN_HOST_ABI_VERSION "
              f"did not (still {version}).\n  A module built against the old headers would be loaded and called "
              f"through a vtable or a struct that no longer match.\n  {fix}.\n"
              "  Not a change to the contract (a free function, a constant, a default body)? "
              "python3 scripts/check_ultraim_abi.py --update --no-abi-change \"why\"")
        return 1
    print(f"check_ultraim_abi: the version is {version}, scripts/ultraim_abi.txt records {old_version}: "
          "run python3 scripts/check_ultraim_abi.py --update and commit it")
    return 1


if __name__ == "__main__":
    sys.exit(main())
