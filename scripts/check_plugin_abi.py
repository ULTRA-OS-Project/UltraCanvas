#!/usr/bin/env python3
"""Plug-in ABIs: a change to a contract between the host and its plug-in
modules must move that contract's version.

Two kinds of module are loaded at run time and called across a binary
contract, each with a version that gates the loading:

  ultraim   UltraIM's provider modules (ChatCube Direct, Matrix, Telegram -
            Plugins/UltraIM/*.so / .dll / .dylib). They are called through
            IUltraIMProviderPlugin's vtable and hand UltraIMTypes.h's
            structs back and forth; ULTRAIM_PLUGIN_HOST_ABI_VERSION covers
            all of it, a module exports the version it was built for and the
            host loads only an equal one. Version 1 grew three interface
            methods without a bump before this was checked.
  element   the element plug-ins (UltraCanvasElementPlugins.h: charts,
            widgets). The host table is append-only - a plugin requires a
            table at least as new as its own, ULTRACANVAS_PLUGIN_HOST_ABI_VERSION
            - and the C++ types crossing it carry the "ucN" of
            ULTRACANVAS_FRAMEWORK_ABI_TAG, compared exactly. A change to the
            header moves one of the two. The classes a plug-in creates
            (derived from UltraCanvasUIElement) are not this script's: their
            layout is every framework header's, and the build checks that
            itself - a plug-in exports the hash of the headers it was
            compiled against and the loader refuses another
            (UltraCanvasFrameworkHeadersId.h).

The gates only work if the numbers move whenever the contracts do - which
used to be remembered, not checked. This script fingerprints each
contract's headers - without comments, without the text inside string
literals (an error message is not layout), spacing collapsed and the version
markers themselves left out - and compares the fingerprint and the version
with scripts/plugin_abi.txt:

  fingerprint and version as recorded     clean
  fingerprint changed, version not        FAIL: bump the version, then --update
  version moved                           FAIL until --update records it

  python3 scripts/check_plugin_abi.py                     # the check (CI: plugin-abi.yml)
  python3 scripts/check_plugin_abi.py --update            # record, after a bump
  python3 scripts/check_plugin_abi.py --update --no-abi-change "why"
                                                          # a change that is not one: a free
                                                          # function, a constant, a default body
  python3 scripts/check_plugin_abi.py --self-test
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECORD = ROOT / "scripts/plugin_abi.txt"


@dataclass
class Contract:
    name: str
    headers: list[str]
    # Each marker: a regex whose group 1 is a number. Together they are the
    # contract's version; the text they match is left out of the fingerprint.
    markers: list[tuple[str, re.Pattern]]
    bump: str                                   # what to bump, for the message


CONTRACTS = [
    Contract(
        "ultraim",
        ["UltraCanvas/include/UltraIM/UltraIMPlugins.h", "UltraCanvas/include/UltraIM/UltraIMTypes.h"],
        [("abi", re.compile(r"constexpr\s+int\s+ULTRAIM_PLUGIN_HOST_ABI_VERSION\s*=\s*(\d+)\s*;"))],
        "ULTRAIM_PLUGIN_HOST_ABI_VERSION in UltraCanvas/include/UltraIM/UltraIMPlugins.h (and say what "
        "changed in its comment)",
    ),
    Contract(
        "element",
        ["UltraCanvas/include/UltraCanvasElementPlugins.h"],
        [("table", re.compile(r"constexpr\s+int\s+ULTRACANVAS_PLUGIN_HOST_ABI_VERSION\s*=\s*(\d+)\s*;")),
         ("uc", re.compile(r"-cxx20-uc(\d+)\""))],
        "in UltraCanvas/include/UltraCanvasElementPlugins.h ULTRACANVAS_PLUGIN_HOST_ABI_VERSION for a member "
        "appended to UltraCanvasPluginHost, or the \"ucN\" of ULTRACANVAS_FRAMEWORK_ABI_TAG for a change to "
        "the types crossing the boundary",
    ),
]


def normalize(text: str, markers: list[re.Pattern] = ()) -> str:
    """The text a compiler sees, give or take: no comments, string literals
    emptied, spacing collapsed, and the version markers left out (they are
    compared on their own)."""
    for marker in markers:
        text = marker.sub("", text)
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
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    flat = " ".join("".join(out).split())
    # Spacing beside punctuation is layout of the text, not of the types.
    return re.sub(r" ?([^\w ]) ?", r"\1", flat)


def current(contract: Contract) -> tuple[dict[str, int], str]:
    digest = hashlib.sha256()
    versions: dict[str, int] = {}
    patterns = [p for _, p in contract.markers]
    for header in contract.headers:
        text = (ROOT / header).read_text(encoding="utf-8")
        for key, pattern in contract.markers:
            match = pattern.search(text)
            if match:
                versions[key] = int(match.group(1))
        digest.update(Path(header).name.encode() + b"\0" + normalize(text, patterns).encode() + b"\0")
    missing = [key for key, _ in contract.markers if key not in versions]
    if missing:
        sys.exit(f"check_plugin_abi: {contract.name}: version marker(s) not found: {', '.join(missing)}")
    return versions, digest.hexdigest()


def recorded() -> dict[str, tuple[dict[str, int], str]]:
    out: dict[str, tuple[dict[str, int], str]] = {}
    if not RECORD.exists():
        return out
    for line in RECORD.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, kind, *rest = line.split()
        versions, fingerprint = out.get(name, ({}, ""))
        if kind == "fingerprint":
            fingerprint = rest[0]
        else:
            versions = dict(versions)
            versions[kind] = int(rest[0])
        out[name] = (versions, fingerprint)
    return out


def write_record(entries: dict[str, tuple[dict[str, int], str, str]]) -> None:
    lines = ["# The plug-in ABIs as last recorded - see scripts/check_plugin_abi.py.",
             "# Written by `check_plugin_abi.py --update`; never edit by hand.", ""]
    for contract in CONTRACTS:
        versions, fingerprint, note = entries[contract.name]
        lines.append(f"# {contract.name}: {note}")
        for key, _ in contract.markers:
            lines.append(f"{contract.name} {key} {versions[key]}")
        lines.append(f"{contract.name} fingerprint {fingerprint}")
        lines.append("")
    RECORD.write_text("\n".join(lines), encoding="utf-8")


def describe(versions: dict[str, int]) -> str:
    return ", ".join(f"{key} {value}" for key, value in versions.items())


def self_test() -> int:
    base = "struct A { int x; }; // a note\nclass I { virtual void F(); };"
    cases = [
        (base.replace("// a note", "// another note"), True, "a changed comment"),
        (base.replace(";", " ;"), True, "spacing"),
        ("/* block */" + base, True, "a block comment"),
        (base.replace("int x;", "int x; int y;"), False, "a new struct member"),
        (base.replace("virtual void F();", "virtual void F(); virtual void G();"), False, "a new virtual"),
    ]
    failures = 0
    for text, same, what in cases:
        if (normalize(text) == normalize(base)) != same:
            print(f"self-test FAIL: {what} {'changed' if same else 'did not change'} the fingerprint")
            failures += 1
    if normalize('E("one");') != normalize('E("two");'):
        print("self-test FAIL: the text of a string literal changed the fingerprint")
        failures += 1
    for contract in CONTRACTS:
        patterns = [p for _, p in contract.markers]
        for key, pattern in contract.markers:
            # Each marker as it is written in the real header, with two values.
            text = (ROOT / contract.headers[0]).read_text(encoding="utf-8")
            for header in contract.headers[1:]:
                text += (ROOT / header).read_text(encoding="utf-8")
            match = pattern.search(text)
            if not match:
                print(f"self-test FAIL: {contract.name}: the {key} marker matches nothing in the header")
                failures += 1
                continue
            bumped = text[:match.start(1)] + str(int(match.group(1)) + 1) + text[match.end(1):]
            if normalize(text, patterns) != normalize(bumped, patterns):
                print(f"self-test FAIL: {contract.name}: the {key} marker is part of the fingerprint")
                failures += 1
    print("check_plugin_abi: self-test " + ("passed" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true", help="record the current versions and fingerprints")
    parser.add_argument("--no-abi-change", metavar="WHY",
                        help="with --update: the headers changed but no binary contract did")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

    old = recorded()
    failed = False
    entries: dict[str, tuple[dict[str, int], str, str]] = {}
    for contract in CONTRACTS:
        versions, fingerprint = current(contract)
        old_versions, old_fingerprint = old.get(contract.name, ({}, ""))
        name = contract.name
        went_back = [key for key, value in versions.items() if value < old_versions.get(key, 0)]
        moved = versions != old_versions
        changed = fingerprint != old_fingerprint
        fix = f"bump {contract.bump}, then run python3 scripts/check_plugin_abi.py --update"
        note = f"recorded at {describe(versions)}"

        if went_back:
            print(f"check_plugin_abi: {name}: the version went back ({describe(old_versions)} -> {describe(versions)})")
            failed = True
        elif args.update:
            if changed and not moved and not args.no_abi_change:
                print(f"check_plugin_abi: {name}: the headers changed and the version did not.\n  {fix}.\n"
                      "  A change that is not part of the binary contract (a free function, a constant, a "
                      "default body) is recorded with --update --no-abi-change \"why\".")
                failed = True
            elif changed and not moved:
                note = f"no ABI change at {describe(versions)}: {args.no_abi_change}"
        elif not changed and not moved:
            print(f"check_plugin_abi: {name}: clean ({describe(versions)})")
        elif changed and not moved:
            print(f"check_plugin_abi: {name}: {', '.join(contract.headers)} changed and the version did not "
                  f"(still {describe(versions)}).\n  A module built against the old header would be loaded and "
                  f"called through a table or a type that no longer matches.\n  {fix}.\n"
                  "  Not a change to the contract (a free function, a constant, a default body)? "
                  "python3 scripts/check_plugin_abi.py --update --no-abi-change \"why\"")
            failed = True
        else:
            print(f"check_plugin_abi: {name}: the version is {describe(versions)}, scripts/plugin_abi.txt records "
                  f"{describe(old_versions) or 'none'}: run python3 scripts/check_plugin_abi.py --update and commit it")
            failed = True
        if not (changed or moved):
            # Keep the note it was recorded with.
            note = next((line.split(": ", 1)[1] for line in RECORD.read_text(encoding="utf-8").splitlines()
                         if line.startswith(f"# {name}: ")), note) if RECORD.exists() else note
        entries[name] = (versions, fingerprint, note)

    if args.update:
        if failed:
            return 1
        write_record(entries)
        print("check_plugin_abi: recorded " + "; ".join(
            f"{c.name} {describe(entries[c.name][0])} ({entries[c.name][1][:12]})" for c in CONTRACTS))
        return 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
