#!/usr/bin/env python3
"""Flag UI elements that are missing from the element catalogue.

Docs/UltraCanvas/UltraCanvasUIElements.md answers the question that comes
before every piece of new UI: *does an element for this already exist?* It is
only worth reading if it is complete. It was not: for a long time it listed
the ~60 elements in UltraCanvas/include/ and pointed at UltraCanvas/Plugins/
with one sentence, so the ~70 chart, diagram, gauge, code and document-view
elements under include/Plugins/ were invisible to anyone searching it. In
2026-09 a second progress bar was written from scratch for UltraFiler's status
strip because UltraCanvasGaugeDiagramElement - the framework's progress bar,
in GaugeMode::LinearBar - was in include/Plugins/Diagrams/ and in no table.
The element was found afterwards and the duplicate deleted.

A doc audit fixes that once. This makes it stay fixed: an element added to the
tree and not to the catalogue fails the check, which is the moment the author
is best placed to write the one row that saves the next reader the same week.

An element is any class deriving (directly) from UltraCanvasUIElement,
UltraCanvasContainer, UltraCanvasGLSurface or another catalogued element base.
"Listed" means the class name appears anywhere in the catalogue - usually as
its own row, sometimes named inside a row that covers a family.

Some element classes are deliberately NOT for a caller to reach for: a shared
base, a format decoder sitting behind a catalogued facade, a platform impl.
Those are listed in scripts/element_catalogue_exempt.txt with the reason.

Usage:
    python3 scripts/check_element_catalogue.py [--strict] [--list]

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0, so the check can be introduced without blocking unrelated work.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOGUE = ROOT / "Docs/UltraCanvas/UltraCanvasUIElements.md"
EXEMPT_FILE = ROOT / "scripts/element_catalogue_exempt.txt"

# Where elements live. UltraCanvas/Plugins is on the include path too, which is
# why a header there is written "Models/STL/UltraCanvasSTLElement.h".
SEARCH_DIRS = [
    ROOT / "UltraCanvas/include",
    ROOT / "UltraCanvas/Plugins",
    ROOT / "UltraCanvas/dialogs",
]

# A class is an element when it derives from one of these, or from anything
# whose name ends in Element / ElementBase - which is how the chart and diagram
# families are built (UltraCanvasChartElementBase, UltraCanvasChartEngineElement).
ELEMENT_BASE = re.compile(
    r"\bclass\s+(UltraCanvas[A-Za-z0-9_]+)\s*:\s*public\s+"
    r"(?:[A-Za-z0-9_]*::)?([A-Za-z0-9_]*(?:UIElement|Container|GLSurface|Element|ElementBase))\b"
)


def load_exempt() -> dict[str, str]:
    out: dict[str, str] = {}
    if not EXEMPT_FILE.exists():
        return out
    for line in EXEMPT_FILE.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, reason = line.partition("#")
        out[name.strip()] = reason.strip()
    return out


def find_elements() -> dict[str, pathlib.Path]:
    found: dict[str, pathlib.Path] = {}
    for d in SEARCH_DIRS:
        if not d.is_dir():
            continue
        for header in sorted(d.rglob("*.h")):
            text = header.read_text(errors="replace")
            for name, _base in ELEMENT_BASE.findall(text):
                found.setdefault(name, header)
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 when an element is missing from the catalogue")
    ap.add_argument("--list", action="store_true",
                    help="print every element found and whether it is listed")
    args = ap.parse_args()

    if not CATALOGUE.exists():
        print(f"check_element_catalogue: {CATALOGUE} not found", file=sys.stderr)
        return 1

    catalogue = CATALOGUE.read_text()
    exempt = load_exempt()
    elements = find_elements()

    missing = []
    for name, header in sorted(elements.items()):
        listed = re.search(r"\b%s\b" % re.escape(name), catalogue) is not None
        if args.list:
            mark = "listed " if listed else ("exempt " if name in exempt else "MISSING")
            print(f"  {mark}  {name}  ({header.relative_to(ROOT)})")
        if listed or name in exempt:
            continue
        missing.append((name, header))

    if not missing:
        print(f"check_element_catalogue: clean "
              f"({len(elements)} elements, {len(exempt)} exempt).")
        return 0

    print(f"check_element_catalogue: {len(missing)} element(s) missing from "
          f"{CATALOGUE.relative_to(ROOT)}:\n")
    for name, header in missing:
        print(f"  {name}")
        print(f"      {header.relative_to(ROOT)}")
    print("\nAdd a row saying what each is FOR - the catalogue is read by "
          "someone who\nknows the need and not the name - or, if it is a base "
          "class, a decoder behind a\ncatalogued facade or a platform impl, "
          f"record it with its reason in\n{EXEMPT_FILE.relative_to(ROOT)}.")
    return 1 if args.strict else 0


if __name__ == "__main__":
    sys.exit(main())
