#!/usr/bin/env bash
# set-version.sh — Propagate VERSION file to all source files

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEXTER_VERSION_FILE="$SCRIPT_DIR/Apps/Texter/VERSION"
UC_VERSION_FILE="$SCRIPT_DIR/UltraCanvas/VERSION"


# Read and validate version
if [[ ! -f "$TEXTER_VERSION_FILE" ]]; then
    echo "ERROR: VERSION file not found at $TEXTER_VERSION_FILE" >&2
    exit 1
fi

TEXTER_VERSION="$(tr -d '[:space:]' < "$TEXTER_VERSION_FILE")"

# Read and validate version
if [[ ! -f "$UC_VERSION_FILE" ]]; then
    echo "ERROR: VERSION file not found at $UC_VERSION_FILE" >&2
    exit 1
fi

UC_VERSION="$(tr -d '[:space:]' < "$UC_VERSION_FILE")"

if [[ ! "$UC_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: Invalid version format '$UC_VERSION' (expected x.y.z)" >&2
    exit 1
fi

if [[ ! "$TEXTER_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: Invalid version format '$TEXTER_VERSION' (expected x.y.z)" >&2
    exit 1
fi

IFS='.' read -r MAJOR MINOR PATCH <<< "$TEXTER_VERSION"
TEXTER_COMMA_VER="$MAJOR,$MINOR,$PATCH,0"
TEXTER_DOT4_VER="$TEXTER_VERSION.0"

IFS='.' read -r MAJOR MINOR PATCH <<< "$UC_VERSION"
UC_COMMA_VER="$MAJOR,$MINOR,$PATCH,0"
UC_DOT4_VER="$UC_VERSION.0"

echo "Setting TEXTER_VERSION to $TEXTER_VERSION, UC_VERSION to $UC_VERSION"

# --- Apps/Texter/CMakeLists.txt: TEXTER_VERSION x.y.z inside project() ---
FILE="$SCRIPT_DIR/Apps/Texter/CMakeLists.txt"
sed -i "s/^\(        VERSION \)[0-9][0-9.]*/\1$TEXTER_VERSION/" "$FILE"
echo "  Updated $FILE"

# --- UltraCanvas/CMakeLists.txt: set(ULTRACANVAS_VERSION "x.y.z") ---
FILE="$SCRIPT_DIR/UltraCanvas/CMakeLists.txt"
sed -i "s/\(set(ULTRACANVAS_VERSION \"\)[0-9][0-9.]*\"/\1$UC_VERSION\"/" "$FILE"
echo "  Updated $FILE"

# --- CMakeLists.txt (root): VERSION x.y.z inside project() ---
FILE="$SCRIPT_DIR/CMakeLists.txt"
sed -i "s/^\(    VERSION \)[0-9][0-9.]*/\1$UC_VERSION/" "$FILE"
echo "  Updated $FILE"

# --- UltraCanvas/core/UltraCanvasUtils.cpp: TEXTER_VERSIONString = "..." ---
FILE="$SCRIPT_DIR/UltraCanvas/core/UltraCanvasUtils.cpp"
sed -i "s/\(versionString = \"\)[^\"]*\"/\1$UC_VERSION\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraCanvasTextEditor.cpp: std::string TEXTER_VERSION = "..." ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraCanvasTextEditor.cpp"
sed -i "s/\(std::string UltraCanvasTextEditor::version = \"\)[^\"]*\"/\1$TEXTER_VERSION\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraTexter.manifest: TEXTER_VERSION="x.y.z.0" ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraTexter.manifest"
sed -i "s/\(version=\"\)[0-9][0-9.]*\"/\1$TEXTER_DOT4_VER\"/" "$FILE"
echo "  Updated $FILE"

# --- Apps/Texter/UltraTexter.rc ---
FILE="$SCRIPT_DIR/Apps/Texter/UltraTexter.rc"
# FILEVERSION  x,y,z,0
sed -i "s/\(FILEVERSION  *\)[0-9][0-9,]*/\1$TEXTER_COMMA_VER/" "$FILE"
# PRODUCTVERSION  x,y,z,0
sed -i "s/\(PRODUCTVERSION  *\)[0-9][0-9,]*/\1$TEXTER_COMMA_VER/" "$FILE"
# VALUE "FileVersion", "x.y.z\0"
sed -i "s/\(\"FileVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$TEXTER_VERSION\\\\0\"/" "$FILE"
# VALUE "ProductVersion", "x.y.z\0"
sed -i "s/\(\"ProductVersion\",  *\"\)[0-9][0-9.]*\\\\0\"/\1$TEXTER_VERSION\\\\0\"/" "$FILE"
echo "  Updated $FILE"

echo "Done. TEXTER_VERSION set to $TEXTER_VERSION, UC_VERSION set to $UC_VERSION across all files."
