#!/usr/bin/env python3
"""Draw our vector writers' output in LibreOffice and check it.

Our readers and writers can agree with each other and still be wrong: before
the fill-rule fix, the EMF and WMF writers set WINDING (which LibreOffice
ignores) and the CDR and ODG writers lost a ring's middle, yet every round
trip through our own readers passed. This check asks another program.

Steps:
  1. VectorExportFixtures (Tests/VectorExportFixtures.cpp) writes each fixture
     drawing through every vector writer, with a manifest of probes: points
     in document coordinates and what must be there.
  2. LibreOffice converts each file to PNG (one call per format, so the
     outputs of rings.emf and rings.svg do not collide).
  3. On each PNG the drawing is found (the box of its non-white pixels, which
     is the fixtures' content box, (10, 10) - (290, 90) on the page),
     every probe is mapped onto it and sampled.

The formats in CHECKED must convert and pass, but for EXPECTED_FAILURES:
files LibreOffice's own import draws wrong, each with its reason. LibreOffice does not read the
rest (Xara, our EPS without a preview image, PostScript), or reads them as
another of these; they are listed as not checked, so a new writer shows up
here and can be added once LibreOffice reads it.

Usage:
  check_libreoffice_render.py --fixtures build/bin/VectorExportFixtures
                              [--out DIR] [--soffice soffice]
  check_libreoffice_render.py --dir DIR     (fixtures already written)
Exit code: 0 when every checked format passed.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

try:
    from PIL import Image
except ImportError:  # pragma: no cover - CI installs python3-pil
    sys.exit("check_libreoffice_render: needs Pillow (python3-pil)")

# The formats LibreOffice reads and must draw as we meant. svgz, otg and
# fodg are the same writers as svg and odg in another wrapper; ai is our PDF.
CHECKED = ["emf", "wmf", "svg", "svgz", "odg", "otg", "fodg", "pdf", "ai", "cdr", "dxf"]

# Files LibreOffice draws wrong although the file is right: its import, not
# our writer. Each must still fail - when one passes, the check fails too and
# asks for it to come off this list, so the list never hides a regression.
EXPECTED_FAILURES = {
    "rings.pdf": "LibreOffice's PDF import fills every shape even-odd, so the non-zero ring "
                 "wound one way gets a hole (poppler draws it solid)",
    "rings.ai": "our .ai is our PDF: the same import",
    "rings.dxf": "LibreOffice's DXF import drops a HATCH whose boundary has curved (spline) edges",
    "shapes.dxf": "the same: the ellipse's hatch is dropped, the rectangle's and the triangle's drawn",
}


def run_fixtures(program, out):
    result = subprocess.run([program, out], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.exit(f"check_libreoffice_render: {program} failed to write every fixture")


def convert(soffice, files, outdir, profile):
    """One LibreOffice call: files -> PNGs in outdir."""
    os.makedirs(outdir, exist_ok=True)
    cmd = [soffice, f"-env:UserInstallation=file://{profile}", "--headless", "--norestore",
           "--convert-to", "png", "--outdir", outdir] + files
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        print(f"  LibreOffice timed out on {', '.join(os.path.basename(f) for f in files)}")


def content_box(img):
    """The box of the pixels that are not white (or transparent)."""
    rgba = img.convert("RGBA")
    w, h = rgba.size
    px = rgba.load()
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 128 and min(r, g, b) < 235:
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    return None if x1 < 0 else (x0, y0, x1 + 1, y1 + 1)


def sample(img, x, y):
    """The mean colour of the 3 x 3 pixels round (x, y), on white."""
    rgba = img.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size
    acc, n = [0, 0, 0], 0
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            xx, yy = min(w - 1, max(0, x + dx)), min(h - 1, max(0, y + dy))
            r, g, b, a = px[xx, yy]
            for i, c in enumerate((r, g, b)):
                acc[i] += (c * a + 255 * (255 - a)) // 255
            n += 1
    return tuple(c // n for c in acc)


def matches(colour, expect):
    r, g, b = colour
    if expect == "dark":
        return 0.299 * r + 0.587 * g + 0.114 * b < 100
    if expect == "light":
        return min(r, g, b) > 200
    want = [int(v) for v in expect.split(",")]
    return all(abs(c - v) <= 60 for c, v in zip(colour, want))


def check_png(png, manifest, fixture):
    """Failures for one drawn file, as text; empty when it passed."""
    img = Image.open(png)
    box = content_box(img)
    if box is None:
        return ["nothing was drawn"]
    cx0, cy0, cx1, cy1 = manifest["content"]
    bx0, by0, bx1, by1 = box
    sx, sy = (bx1 - bx0) / (cx1 - cx0), (by1 - by0) / (cy1 - cy0)
    failures = []
    # The drawing keeps its shape: a writer that loses the page's aspect, or
    # a part of the drawing, shows here before any probe.
    want = (cx1 - cx0) / (cy1 - cy0)
    got = (bx1 - bx0) / max(1, by1 - by0)
    if abs(got - want) / want > 0.05:
        failures.append(f"the drawing is {bx1 - bx0} x {by1 - by0} px: its shape is not the page's "
                        f"({got:.2f} wide to 1 high, not {want:.2f})")
    for p in manifest["fixtures"][fixture]:
        x = int(round(bx0 + (p["x"] - cx0) * sx))
        y = int(round(by0 + (p["y"] - cy0) * sy))
        colour = sample(img, x, y)
        if not matches(colour, p["expect"]):
            failures.append(f"{p['what']}: expected {p['expect']}, found rgb{colour} at ({x}, {y})")
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fixtures", help="the VectorExportFixtures program, to write the fixtures")
    ap.add_argument("--dir", help="a folder the fixtures were already written to")
    ap.add_argument("--out", help="where to keep the fixtures and LibreOffice's PNGs (default: a temp folder)")
    ap.add_argument("--soffice", default=shutil.which("soffice") or shutil.which("libreoffice") or "soffice")
    args = ap.parse_args()
    if not args.fixtures and not args.dir:
        ap.error("give --fixtures or --dir")

    work = args.out or tempfile.mkdtemp(prefix="uc-lo-render-")
    fixtures_dir = args.dir or os.path.join(work, "fixtures")
    if not args.dir:
        os.makedirs(fixtures_dir, exist_ok=True)
        run_fixtures(args.fixtures, fixtures_dir)
    with open(os.path.join(fixtures_dir, "manifest.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    profile = tempfile.mkdtemp(prefix="uc-lo-profile-")

    by_format = {}
    for entry in manifest["files"]:
        by_format.setdefault(entry["format"], []).append(entry)

    failed, passed, expected, unchecked = [], [], [], []
    for fmt in sorted(by_format):
        if fmt not in CHECKED:
            unchecked.append(fmt)
            continue
        outdir = os.path.join(work, "png", fmt)
        convert(args.soffice, [os.path.join(fixtures_dir, e["file"]) for e in by_format[fmt]], outdir, profile)
        for e in by_format[fmt]:
            png = os.path.join(outdir, os.path.splitext(e["file"])[0] + ".png")
            problems = ["LibreOffice did not convert it"] if not os.path.exists(png) else \
                check_png(png, manifest, e["fixture"])
            known = EXPECTED_FAILURES.get(e["file"])
            if problems and known:
                expected.append(e["file"])
                print(f"xfail {e['file']}: {known}")
            elif problems:
                failed.append(e["file"])
                print(f"FAIL  {e['file']}")
                for p in problems:
                    print(f"      {p}")
            elif known:
                failed.append(e["file"])
                print(f"FAIL  {e['file']}: passes now - take it off EXPECTED_FAILURES ({known})")
            else:
                passed.append(e["file"])
                print(f"ok    {e['file']}")
    missing = [f for f in CHECKED if f not in by_format]
    print(f"\n{len(passed)} passed, {len(failed)} failed, {len(expected)} known LibreOffice import limits; "
          f"PNGs in {os.path.join(work, 'png')}")
    if unchecked:
        print("not checked (LibreOffice does not read them as written): " + ", ".join("." + f for f in unchecked))
    if missing:
        print("checked formats this build did not write: " + ", ".join("." + f for f in missing))
    shutil.rmtree(profile, ignore_errors=True)
    return 1 if failed or missing else 0


if __name__ == "__main__":
    sys.exit(main())
