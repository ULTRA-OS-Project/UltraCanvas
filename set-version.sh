#!/usr/bin/env bash
# set-version.sh — Propagate the changelog version to the Windows resource files
#
# The changelogs are the single source of truth for version numbers:
#   Docs/UltraCanvas/CHANGELOG.md   ->  UltraCanvas / DemoApp / ...
#   Docs/Texter/CHANGELOG.md        ->  UltraTexter
#   Docs/UltraFiler/CHANGELOG.md    ->  UltraFiler
#   Docs/UltraCleaner/CHANGELOG.md  ->  UltraCleaner
# in every case the version on the first line, `#### YYYY-MM-DD *x.y.z*`.
#
# Almost nothing needs this script any more. The packaging scripts
# (build-demoapp-appimage.sh, package-win.sh, package-macos.sh) parse the
# changelog for artefact file names, and cmake/UltraCanvasVersion.cmake parses
# the same line at configure time for project() versions and for the
# ULTRACANVAS_VERSION / ULTRATEXTER_VERSION / ULTRAFILER_VERSION /
# ULTRACLEANER_VERSION compile definitions the apps display. Those can no
# longer drift.
#
# What is left are the Windows files that are read from disk by windres/rc.exe
# rather than generated — the .rc and .manifest of UltraTexter and UltraFiler.
# Run this script after adding a changelog entry that bumps either of those
# apps; a CMake configure on any platform warns when they are stale.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# First line of a changelog: `#### YYYY-MM-DD *x.y[.z]*` -> `x.y[.z]`.
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
        echo "       expected format: '#### YYYY-MM-DD *x.y.z*'" >&2
        exit 1
    fi
    printf '%s' "$v"
}

# Pad a dotted version to the four components a Windows resource wants, in the
# dotted (manifest) and comma-separated (.rc) spellings.
pad4() {
    local IFS='.'
    read -r -a parts <<< "$1"
    while (( ${#parts[@]} < 4 )); do parts+=("0"); done
    printf '%s.%s.%s.%s %s,%s,%s,%s' \
        "${parts[0]}" "${parts[1]}" "${parts[2]}" "${parts[3]}" \
        "${parts[0]}" "${parts[1]}" "${parts[2]}" "${parts[3]}"
}

# Write one app's version into its .rc and .manifest.
#   $1 app label, $2 changelog, $3 .manifest, $4 .rc
apply_version() {
    local label="$1" changelog="$2" manifest="$3" rc="$4"
    local version dot4 comma4
    version="$(extract_version "$SCRIPT_DIR/$changelog")"
    if [[ ! "$version" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
        echo "ERROR: Invalid version format '$version' for $label" >&2
        exit 1
    fi
    read -r dot4 comma4 <<< "$(pad4 "$version")"

    echo "Setting $label to $version in the Windows resource files"

    # --- <app>.manifest: version="x.y.z.0" ---
    # Only the app's own <assemblyIdentity> version (8-space indented,
    # top-level element). Must NOT touch the XML prolog
    # `<?xml version="1.0"?>` or the Common-Controls dependency
    # `version="6.0.0.0"` (16-space indented) — a malformed prolog makes
    # Windows ignore the whole manifest (disabling DPI awareness), and a wrong
    # Common-Controls version fails the ComCtl32 v6 bind.
    sed -i "s/^\(        version=\"\)[0-9][0-9.]*\"/\1$dot4\"/" \
        "$SCRIPT_DIR/$manifest"
    echo "  Updated $SCRIPT_DIR/$manifest"

    # --- <app>.rc ---
    # FILEVERSION / PRODUCTVERSION  x,y,z,0
    sed -i "s/\(FILEVERSION  *\)[0-9][0-9,]*/\1$comma4/" "$SCRIPT_DIR/$rc"
    sed -i "s/\(PRODUCTVERSION  *\)[0-9][0-9,]*/\1$comma4/" "$SCRIPT_DIR/$rc"
    # VALUE "FileVersion" / "ProductVersion", "x.y.z\0"
    sed -i "s/\(\"FileVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$version\\\\0\"/" \
        "$SCRIPT_DIR/$rc"
    sed -i "s/\(\"ProductVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$version\\\\0\"/" \
        "$SCRIPT_DIR/$rc"
    echo "  Updated $SCRIPT_DIR/$rc"
}

apply_version "TEXTER_VERSION" "Docs/Texter/CHANGELOG.md" \
    "Apps/Texter/UltraTexter.manifest" "Apps/Texter/UltraTexter.rc"

apply_version "ULTRAFILER_VERSION" "Docs/UltraFiler/CHANGELOG.md" \
    "Apps/UltraFiler/UltraFiler.manifest" "Apps/UltraFiler/UltraFiler.rc"

echo "Done. Everything else derives its version from the changelog at build time."
