#!/usr/bin/env python3
# scripts/report_plugin_sizes.py
# How big the file-format plug-ins are: one row per static plug-in archive
# in a build tree (Vector, CDR and its bundled libcdr, XAR, EPS, Models, OCR,
# Vectorizer and its Rust library), with the archive's file size and the code
# and data its objects hold. Printed as a Markdown table, for the CI job
# summary.
#
#   python3 scripts/report_plugin_sizes.py [build-dir] [--title TEXT]
#
# Why: the plug-ins are static, so every application that registers one
# carries its own copy. Whether to make them shared libraries depends on how
# big they are, and OCR and the Vectorizer only build where Tesseract and
# Rust are installed - in CI. "Code + data" is what a copy adds to an
# executable; the file size also counts symbols and debug information.
#
# Never fails the build: a missing size tool or an unreadable archive gives
# a row with the file size only.
#
# Version: 1.0.0
# Last Modified: 2026-10-12
# Author: UltraCanvas Framework

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

# File name (without lib prefix and extension) -> what it is
KNOWN = {
    "ultracanvas_vector_plugin": "Vector (DXF, DWG family, EMF, WMF, ...)",
    "ultracanvas_models_plugin": "Models (3DS, OBJ, PLY, STEP, Alembic, COLLADA, .blend, ...)",
    "ultracanvas_cdr_plugin": "CDR (CorelDRAW)",
    "ultracanvas_libcdr": "libcdr, bundled for CDR",
    "ultracanvas_xar_plugin": "XAR (Xara)",
    "ultracanvas_eps_plugin": "EPS (PostScript)",
    "ultracanvas_ocr_plugin": "OCR (Tesseract)",
    "ultracanvas_vectorizer_plugin": "Vectorizer (raster to SVG)",
    "vtracer_c": "VTracer, the Rust library behind the Vectorizer",
}


def stem_of(path):
    name = path.name
    for ext in (".a", ".lib"):
        if name.endswith(ext):
            name = name[: -len(ext)]
    if name.startswith("lib"):
        name = name[3:]
    return name


def find_archives(build):
    found = {}
    for root, dirs, files in os.walk(build):
        # Rust's own build tree holds the dependency crates' rlibs, not ours
        dirs[:] = [d for d in dirs if d not in ("deps", "incremental", ".fingerprint", "CMakeFiles")]
        for f in files:
            p = pathlib.Path(root) / f
            if p.suffix not in (".a", ".lib"):
                continue
            stem = stem_of(p)
            if stem in KNOWN and stem not in found:
                found[stem] = p
    return found


def size_tool():
    for tool in ("llvm-size", "size"):
        path = shutil.which(tool)
        if path:
            return path
    return None


def code_and_data(tool, archive):
    """Bytes of code + initialised data over all members, or None."""
    if not tool:
        return None
    try:
        out = subprocess.run([tool, str(archive)], capture_output=True, text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    total, seen = 0, False
    darwin = None
    for line in out.splitlines():
        cols = line.split()
        if not cols:
            continue
        if cols[0] == "text" and len(cols) >= 2 and cols[1] == "data":
            darwin = False                      # Berkeley: text data bss dec hex filename
            continue
        if cols[0] == "__TEXT":
            darwin = True                       # Mach-O: __TEXT __DATA __OBJC others dec hex
            continue
        if len(cols) >= 3 and re.fullmatch(r"\d+", cols[0]) and re.fullmatch(r"\d+", cols[1]):
            total += int(cols[0]) + int(cols[1])
            seen = True
    return total if seen else None


def mb(n):
    return "%.1f MB" % (n / 1048576.0) if n is not None else "-"


def main():
    parser = argparse.ArgumentParser(description="Sizes of the file-format plug-in archives")
    parser.add_argument("build", nargs="?", default="build")
    parser.add_argument("--title", default="Format plug-in sizes")
    args = parser.parse_args()

    build = pathlib.Path(args.build)
    archives = find_archives(build) if build.is_dir() else {}
    tool = size_tool()
    print("### %s" % args.title)
    print("")
    if not archives:
        print("No plug-in archives found under `%s`." % build)
        return 0
    print("| Plug-in | Archive | Code + data | File |")
    print("|---|---|---:|---:|")
    total_code, total_file = 0, 0
    for stem in KNOWN:
        if stem not in archives:
            continue
        p = archives[stem]
        code = code_and_data(tool, p)
        size = p.stat().st_size
        total_code += code or 0
        total_file += size
        print("| %s | `%s` | %s | %s |" % (KNOWN[stem], p.name, mb(code), mb(size)))
    print("| **all of them** | | **%s** | %s |" % (mb(total_code), mb(total_file)))
    missing = [KNOWN[s] for s in KNOWN if s not in archives]
    print("")
    if missing:
        print("Not built here: " + "; ".join(missing) + ".")
        print("")
    print("Code + data is what one copy adds to an application that registers the plug-in "
          "(`%s`)." % (os.path.basename(tool) if tool else "no size tool found"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
