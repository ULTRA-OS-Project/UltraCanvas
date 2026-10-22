#!/usr/bin/env python3
"""List the framework headers that carry no version history.

Every element header under UltraCanvas/include keeps its releases in its
head - `// Version: 1.3.1 - what changed` lines, newest first - so a reader
can tell what moved and when without the changelog. A header without any
`Version:` line has no such trail: UltraCanvasDatePicker.h had none until
2026-10-21, although it had been through five releases.

Usage:
    scripts/check_version_history.py            list them, count them, exit 0
    scripts/check_version_history.py --strict   exit 1 when any is listed
    scripts/check_version_history.py DIR...     other directories to scan
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIRS = [os.path.join(ROOT, "UltraCanvas", "include")]
VERSION_LINE = re.compile(r"^\s*(//|\*)\s*Version:", re.MULTILINE)


def headers_without_history(directory):
    missing = []
    for folder, _dirs, files in os.walk(directory):
        for name in sorted(files):
            if not name.endswith((".h", ".hpp")):
                continue
            path = os.path.join(folder, name)
            try:
                with open(path, encoding="utf-8", errors="replace") as handle:
                    head = handle.read(8000)   # the history sits in the head
            except OSError:
                continue
            if not VERSION_LINE.search(head):
                missing.append(os.path.relpath(path, ROOT))
    return missing


def main(argv):
    strict = "--strict" in argv
    dirs = [a for a in argv if not a.startswith("--")] or DEFAULT_DIRS
    missing = []
    total = 0
    for directory in dirs:
        for folder, _dirs, files in os.walk(directory):
            total += sum(1 for f in files if f.endswith((".h", ".hpp")))
        missing += headers_without_history(directory)
    for path in missing:
        print(path)
    print(f"check_version_history: {len(missing)} of {total} header(s) carry no Version: line"
          + (" - add the history to each, newest first" if missing else ""))
    return 1 if strict and missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
