#!/usr/bin/env python3
"""Fold pending changelog entries into each changelog under its next version.

Line 1 of a product's `CHANGELOG.md` IS its version:
`cmake/UltraCanvasVersion.cmake` reads it and every `project(VERSION ...)`,
compile definition and packaging script follows. Every branch therefore wanted
to write that one line, and two open at once always collided — on 2026-09-23 a
single branch was renumbered five times in one morning (0.9.23 -> 0.9.27 ->
0.9.28 -> 0.9.29 -> 0.9.31), each renumber throwing away a six-platform CI
matrix, and 0.9.29 was consumed and lost in the churn. The application
changelogs went the same way once several branches touched one app: on
2026-10-21 one pull request was renumbered five times (UltraFiler 1.77.1 ->
1.83.0 -> 1.87.0, UltraPaint 0.2.26 -> 0.2.27), conflicting each time on
line 1 of the changelog and of UltraFiler's .rc and .manifest.

Branches now drop their bullets in `<dir of CHANGELOG.md>/changelog.d/<name>.md`
— `Docs/UltraCanvas/changelog.d/` for the framework, `Docs/<App>/changelog.d/`
for an application — with no header and no number, and this script assigns
the numbers ONCE, on main, after the merge: `.github/workflows/changelog-fold.yml`
runs it on every push to main that brings an entry. Every product
`cmake/UltraCanvasVersion.cmake` declares is folded, each into its own file
under its own next version.

The next version follows the product's own convention, read from its file
(see `next_version`): UltraFiler 1.88.0 -> 1.89.0, UltraPaint 0.2.28 ->
0.2.29, Texter 1.69 -> 1.70, the framework 0.9.364 -> 0.9.365.

When a product whose Windows resource files hold a literal copy of its version
(UltraTexter, UltraFiler) is folded, `./set-version.sh` is run so the .rc and
.manifest match the new line 1 in the same commit.

    python3 scripts/fold_changelog.py            # fold, print the new versions
    python3 scripts/fold_changelog.py --check    # report, change nothing
    python3 scripts/fold_changelog.py --version 0.10.0             # framework
    python3 scripts/fold_changelog.py --version UltraFiler=2.0.0   # one app

For each product folded it prints one `::folded::` line, which the workflow
reads: `::folded::<x.y.z>` for the framework, `::folded::<Name> <x.y.z>` for an
application (<Name> is the directory that holds its CHANGELOG.md).

Exit code is 0 whether or not anything was pending — a push with no entries is
the normal case, not an error. It is 1 only when something is wrong: an entry
that carries its own header, an unparsable changelog, a version that would not
go forwards, an override for a product with nothing pending. Nothing is written
unless every product folds cleanly.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VERSION_CMAKE = REPO / "cmake" / "UltraCanvasVersion.cmake"
FRAMEWORK = "ULTRACANVAS"

# `#### YYYY-MM-DD *x.y.z*` — the format cmake/UltraCanvasVersion.cmake parses.
HEADER = re.compile(r"^#### (\d{4}-\d{2}-\d{2}) \*(\d+(?:\.\d+)*)\*\s*$")
DECLARE = re.compile(r'^\s*_ultracanvas_declare_product\(\s*(\S+)\s+"([^"]+)"')

# Products whose Windows .rc / .manifest carry a literal version that
# set-version.sh rewrites (scripts/check_changelog.py: WINDOWS_RESOURCES).
RESOURCE_PRODUCTS = {"ULTRATEXTER", "ULTRAFILER"}


def products() -> list[tuple[str, Path]]:
    """(prefix, changelog) for every product cmake declares, in its order."""
    found = []
    for line in VERSION_CMAKE.read_text(encoding="utf-8").splitlines():
        m = DECLARE.match(line)
        if m:
            found.append((m.group(1), REPO / m.group(2)))
    if not found:
        raise SystemExit(f"fold_changelog: no products declared in {VERSION_CMAKE}")
    return found


def name_of(changelog: Path) -> str:
    """How a product is named in output and in --version: its directory."""
    return changelog.parent.name


def pending(directory: Path) -> list[Path]:
    """Entry files, in a deterministic order. README.md documents, not entries."""
    if not directory.is_dir():
        return []
    return sorted(p for p in directory.glob("*.md") if p.name != "README.md")


def versions(text: str) -> list[tuple[int, ...]]:
    return [tuple(int(p) for p in m.group(2).split("."))
            for m in (HEADER.match(line) for line in text.splitlines()) if m]


def show(version: tuple[int, ...]) -> str:
    return ".".join(str(p) for p in version)


def parse_version(text: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(?:\.\d+)*", text):
        raise ValueError(f"{text!r} is not a version like 1.2.3")
    return tuple(int(p) for p in text.split("."))


def _pad(v: tuple[int, ...], n: int) -> tuple[int, ...]:
    return v + (0,) * (n - len(v))


def next_version(text: str, label: str) -> tuple[int, ...]:
    """The next version in this file's own convention.

    The step is the component that changed between the two highest versions
    in the file, with every later component zeroed: 1.88.0 over 1.87.0 is a
    minor cadence (-> 1.89.0), 0.2.28 over 0.2.27 a patch one (-> 0.2.29),
    1.69 over 1.68 the last component (-> 1.70).

    A pair that starts a new series is not the cadence: 0.10.0 over 0.9.364
    changed the minor, but the release before it counted patches, so the next
    is 0.10.1, not 0.11.0. Such a pair (the lower version has non-zero
    components after the changed one) is skipped and the next pair down is
    read. With no usable pair the last component is bumped.

    The bump is applied to the HIGHEST version, not line 1: the framework file
    carries sixteen duplicated numbers from before any of this was checked,
    and a malformed line 1 must not be able to hand out a number that goes
    backwards.
    """
    found = sorted(set(versions(text)), reverse=True)
    if not found:
        raise ValueError(f"{label} has no `#### YYYY-MM-DD *x.y.z*` entry")
    top = found[0]
    step = len(top) - 1
    for hi, lo in zip(found, found[1:]):
        n = max(len(hi), len(lo))
        h, l = _pad(hi, n), _pad(lo, n)
        i = next(k for k in range(n) if h[k] != l[k])
        if any(l[i + 1:]):
            continue   # a new series began here; look further down
        step = min(i, len(top) - 1)
        break
    return (*top[:step], top[step] + 1, *(0 for _ in top[step + 1:]))


def read_entry(path: Path) -> str:
    """One entry's bullets, with the header rule enforced."""
    text = path.read_text(encoding="utf-8").strip("\n")
    for number, line in enumerate(text.splitlines(), start=1):
        if HEADER.match(line) or line.startswith("#### "):
            raise ValueError(
                f"{path.relative_to(REPO)}:{number}: a pending entry must not carry its "
                f"own version header — the number is assigned here, on main, which is "
                f"the whole point of the directory")
    if not text.strip():
        raise ValueError(f"{path.relative_to(REPO)}: empty")
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="report what would be folded and change nothing")
    ap.add_argument("--version", metavar="[NAME=]X.Y.Z", action="append", default=[],
                    help="use this version instead of the bump: a bare version is the "
                         "framework's, NAME=X.Y.Z an application's (NAME is the directory "
                         "of its CHANGELOG.md, e.g. UltraFiler). May be repeated.")
    ap.add_argument("--date", metavar="YYYY-MM-DD",
                    help="date for the entries (default: today, UTC)")
    args = ap.parse_args()

    declared = products()
    names = {name_of(log): prefix for prefix, log in declared}
    framework_name = name_of(dict(declared)[FRAMEWORK])

    try:
        overrides: dict[str, tuple[int, ...]] = {}
        for item in args.version:
            name, _, value = item.rpartition("=")
            name = name or framework_name
            if name not in names:
                raise ValueError(f"--version {item}: no product {name!r}; one of "
                                 f"{', '.join(sorted(names))}")
            overrides[names[name]] = parse_version(value)

        plan = []   # (prefix, name, changelog, entries, new text, version)
        date = args.date or dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d")
        for prefix, changelog in declared:
            entries = pending(changelog.parent / "changelog.d")
            if not entries:
                if prefix in overrides:
                    raise ValueError(f"--version for {name_of(changelog)}, which "
                                     f"has nothing pending")
                continue
            label = changelog.relative_to(REPO).as_posix()
            text = changelog.read_text(encoding="utf-8")
            bullets = [read_entry(p) for p in entries]
            if prefix in overrides:
                version = overrides[prefix]
                highest = max(versions(text), default=())
                if version <= highest:
                    raise ValueError(
                        f"--version {show(version)} for {name_of(changelog)} is "
                        f"not above {show(highest)}, the highest version in {label}; the "
                        f"product's version would go backwards")
            else:
                version = next_version(text, label)
            entry = f"#### {date} *{show(version)}*\n" + "\n".join(bullets) + "\n"
            plan.append((prefix, name_of(changelog), changelog, entries,
                         entry + "\n" + text, version))
    except ValueError as exc:
        print(f"fold_changelog: {exc}")
        return 1

    if not plan:
        print("fold_changelog: nothing pending.")
        return 0

    verb = "would fold" if args.check else "folded"
    for prefix, name, changelog, entries, new_text, version in plan:
        files = ", ".join(p.name for p in entries)
        print(f"fold_changelog: {name}: {verb} {len(entries)} entr"
              f"{'y' if len(entries) == 1 else 'ies'} ({files}) as {show(version)}.")
    if args.check:
        return 0

    for prefix, name, changelog, entries, new_text, version in plan:
        changelog.write_text(new_text, encoding="utf-8")
        for path in entries:
            path.unlink()

    # windres reads the .rc and .manifest from disk, so their literal copy of
    # the version has to move in the same commit as line 1.
    if any(prefix in RESOURCE_PRODUCTS for prefix, *_ in plan):
        sys.stdout.flush()   # keep our lines ahead of the script's when piped
        done = subprocess.run(["bash", str(REPO / "set-version.sh")], cwd=REPO)
        if done.returncode != 0:
            print("fold_changelog: set-version.sh failed - the Windows resource files "
                  "are behind the changelog")
            return 1

    # The workflow reads these to write the commit message and skip a no-op push.
    for prefix, name, changelog, entries, new_text, version in plan:
        if prefix == FRAMEWORK:
            print(f"::folded::{show(version)}")
        else:
            print(f"::folded::{name} {show(version)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
