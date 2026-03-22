#!/usr/bin/env bash
# set-version.sh — Propagate VERSION file to all source files
# Usage: ./set-version.sh [version]
#   If version argument is given, it also updates the VERSION file.
#   Otherwise reads the existing VERSION file.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/VERSION"

# If a version was passed as argument, write it to VERSION first
if [[ $# -ge 1 ]]; then
    echo "$1" > "$VERSION_FILE"
fi

# Read and validate version
if [[ ! -f "$VERSION_FILE" ]]; then
    echo "ERROR: VERSION file not found at $VERSION_FILE" >&2
    exit 1
fi

VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: Invalid version format '$VERSION' (expected x.y.z)" >&2
    exit 1
fi

IFS='.' read -r MAJOR MINOR PATCH <<< "$VERSION"
COMMA_VER="$MAJOR,$MINOR,$PATCH,0"
DOT4_VER="$VERSION.0"

echo "Setting version to $VERSION"

# --- Apps/Texter/CMakeLists.txt: VERSION x.y.z inside project() ---
FILE="$SCRIPT_DIR/Apps/Texter/CMakeLists.txt"
sed -i "s/^\(        VERSION \)[0-9][0-9.]*/\1$VERSION/" "$FILE"
echo "  Updated $FILE"

# --- UltraCanvas/CMakeLists.txt: set(ULTRACANVAS_VERSION "x.y.z") ---
FILE="$SCRIPT_DIR/UltraCanvas/CMakeLists.txt"
sed -i "s/\(set(ULTRACANVAS_VERSION \"\)[0-9][0-9.]*\"/\1$VERSION\"/" "$FILE"
echo "  Updated $FILE"

# --- CMakeLists.txt (root): VERSION x.y.z inside project() ---
FILE="$SCRIPT_DIR/CMakeLists.txt"
sed -i "s/^\(    VERSION \)[0-9][0-9.]*/\1$VERSION/" "$FILE"
echo "  Updated $FILE"

# --- UltraCanvas/core/UltraCanvasUtils.cpp: versionString = "..." ---
FILE="$SCRIPT_DIR/UltraCanvas/core/UltraCanvasUtils.cpp"
sed -i "s/\(versionString = \"\)[^\"]*\"/\1$VERSION\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraCanvasTextEditor.h: std::string version = "..." ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraCanvasTextEditor.h"
sed -i "s/\(std::string version = \"\)[^\"]*\"/\1$VERSION\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraTexter.manifest: version="x.y.z.0" ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraTexter.manifest"
sed -i "s/\(version=\"\)[0-9][0-9.]*\"/\1$DOT4_VER\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraTexter.rc ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraTexter.rc"
# FILEVERSION  x,y,z,0
sed -i "s/\(FILEVERSION  *\)[0-9][0-9,]*/\1$COMMA_VER/" "$FILE"
# PRODUCTVERSION  x,y,z,0
sed -i "s/\(PRODUCTVERSION  *\)[0-9][0-9,]*/\1$COMMA_VER/" "$FILE"
# VALUE "FileVersion", "x.y.z\0"
sed -i "s/\(\"FileVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$VERSION\\\\0\"/" "$FILE"
# VALUE "ProductVersion", "x.y.z\0"
sed -i "s/\(\"ProductVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$VERSION\\\\0\"/" "$FILE"
echo "  Updated $FILE"

echo "Done. Version set to $VERSION across all files."
