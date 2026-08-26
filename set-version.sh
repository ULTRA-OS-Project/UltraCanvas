#!/usr/bin/env bash
# set-version.sh — Propagate the changelog version to the Windows resource files
#
# The changelogs are the single source of truth for version numbers:
#   Docs/UltraCanvas/CHANGELOG.md   ->  UltraCanvas / DemoApp / UltraViewer / ...
#   Docs/Texter/CHANGELOG.md        ->  UltraTexter
#   Docs/UltraCleaner/CHANGELOG.md  ->  UltraCleaner
#   Docs/UltraFiler/CHANGELOG.md    ->  UltraFiler
# in every case the version on the first line, `#### YYYY-MM-DD *x.y.z*`.
#
# Almost nothing needs this script any more. The packaging scripts
# (build-demoapp-appimage.sh, package-win.sh, package-macos.sh) parse the
# changelog for artefact file names, and cmake/UltraCanvasVersion.cmake parses
# the same line at configure time for project() versions and for the
# ULTRACANVAS_VERSION / ULTRATEXTER_VERSION / ULTRACLEANER_VERSION compile
# definitions the apps display. Those can no longer drift.
#
# What is left are the Windows files that are read from disk by windres/rc.exe
# rather than generated — UltraTexter.rc / .manifest and UltraFiler.rc /
# .manifest. Run this script after adding a changelog entry that bumps either
# app; a CMake configure on any platform warns when they are stale.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEXTER_CHANGELOG_FILE="$SCRIPT_DIR/Docs/Texter/CHANGELOG.md"
FILER_CHANGELOG_FILE="$SCRIPT_DIR/Docs/UltraFiler/CHANGELOG.md"

# `#### YYYY-MM-DD *x.y*` or `*x.y.z*` from the first line — the same line
# cmake/UltraCanvasVersion.cmake reads.
extract_version() {
    local file="$1"
    if [[ ! -f "$file" ]]; then
        echo "ERROR: changelog not found at $file" >&2
        exit 1
    fi
    local v
    v="$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+(\.[0-9]+)*)\*.*/\1/p' "$file")"
    if [[ -z "$v" ]]; then
        echo "ERROR: could not parse version from first line of $file" >&2
        echo "       expected format: '#### YYYY-MM-DD *x.y[.z]*'" >&2
        exit 1
    fi
    printf '%s' "$v"
}

# Pad to exactly four components: "1.40" -> 1,40,0,0 / 1.40.0.0, and
# "0.9.0" -> 0,9,0,0 / 0.9.0.0 — the forms VERSIONINFO and the manifest want.
version_comma4() {
    local IFS='.'; read -r -a p <<< "$1"
    while (( ${#p[@]} < 4 )); do p+=(0); done
    printf '%s,%s,%s,%s' "${p[0]}" "${p[1]}" "${p[2]}" "${p[3]}"
}
version_dot4() {
    local IFS='.'; read -r -a p <<< "$1"
    while (( ${#p[@]} < 4 )); do p+=(0); done
    printf '%s.%s.%s.%s' "${p[0]}" "${p[1]}" "${p[2]}" "${p[3]}"
}

TEXTER_VERSION="$(extract_version "$TEXTER_CHANGELOG_FILE")"
if [[ ! "$TEXTER_VERSION" =~ ^[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: Invalid UltraTexter version '$TEXTER_VERSION' (expected x.y)" >&2
    exit 1
fi
TEXTER_COMMA_VER="$(version_comma4 "$TEXTER_VERSION")"
TEXTER_DOT4_VER="$(version_dot4 "$TEXTER_VERSION")"

FILER_VERSION="$(extract_version "$FILER_CHANGELOG_FILE")"
if [[ ! "$FILER_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: Invalid UltraFiler version '$FILER_VERSION' (expected x.y.z)" >&2
    exit 1
fi
FILER_COMMA_VER="$(version_comma4 "$FILER_VERSION")"
FILER_DOT4_VER="$(version_dot4 "$FILER_VERSION")"

echo "Setting TEXTER_VERSION to $TEXTER_VERSION in the Windows resource files"

# --- Apps/Texter/UltraTexter.manifest: version="x.y.0.0" ---
# Only the app's own <assemblyIdentity> version (8-space indented, top-level
# element). Must NOT touch the XML prolog `<?xml version="1.0"?>` or the
# Common-Controls dependency `version="6.0.0.0"` (16-space indented) — a
# malformed prolog makes Windows ignore the whole manifest (disabling DPI
# awareness), and a wrong Common-Controls version fails the ComCtl32 v6 bind.
FILE="$SCRIPT_DIR/Apps/Texter/UltraTexter.manifest"
sed -i "s/^\(        version=\"\)[0-9][0-9.]*\"/\1$TEXTER_DOT4_VER\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraTexter.rc ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraTexter.rc"
# FILEVERSION  x,y,0,0
sed -i "s/\(FILEVERSION  *\)[0-9][0-9,]*/\1$TEXTER_COMMA_VER/" "$FILE"
# PRODUCTVERSION  x,y,0,0
sed -i "s/\(PRODUCTVERSION  *\)[0-9][0-9,]*/\1$TEXTER_COMMA_VER/" "$FILE"
# VALUE "FileVersion", "x.y\0"
sed -i "s/\(\"FileVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$TEXTER_VERSION\\\\0\"/" "$FILE"
# VALUE "ProductVersion", "x.y\0"
sed -i "s/\(\"ProductVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$TEXTER_VERSION\\\\0\"/" "$FILE"
echo "  Updated $FILE"

echo "Setting FILER_VERSION to $FILER_VERSION in the Windows resource files"

# --- Apps/UltraFiler/UltraFiler.manifest: version="x.y.z.0" ---
# Same care as the UltraTexter manifest above: only the app's own
# <assemblyIdentity> version (8-space indented), never the XML prolog or the
# 16-space indented Common-Controls dependency.
FILE="$SCRIPT_DIR/Apps/UltraFiler/UltraFiler.manifest"
sed -i "s/^\(        version=\"\)[0-9][0-9.]*\"/\1$FILER_DOT4_VER\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/UltraFiler/UltraFiler.rc ---
FILE="$SCRIPT_DIR/Apps/UltraFiler/UltraFiler.rc"
sed -i "s/\(FILEVERSION  *\)[0-9][0-9,]*/\1$FILER_COMMA_VER/" "$FILE"
sed -i "s/\(PRODUCTVERSION  *\)[0-9][0-9,]*/\1$FILER_COMMA_VER/" "$FILE"
sed -i "s/\(\"FileVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$FILER_VERSION\\\\0\"/" "$FILE"
sed -i "s/\(\"ProductVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$FILER_VERSION\\\\0\"/" "$FILE"
echo "  Updated $FILE"

echo "Done. Everything else derives its version from the changelog at build time."
