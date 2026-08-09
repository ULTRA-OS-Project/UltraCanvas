#!/bin/bash
set -e

export APPLE_ID="info@riscoscloverleaf.com"
export APPLE_TEAM_ID="29638T25M9"
export APPLE_APP_PASSWORD="bhep-idme-ydhq-lrsz"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
UC_CHANGELOG="$SCRIPT_DIR/Docs/UltraCanvas/CHANGELOG.md"
if [ ! -f "$UC_CHANGELOG" ]; then
    echo "Error: changelog not found at $UC_CHANGELOG" >&2
    exit 1
fi
VERSION=$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+\.[0-9]+\.[0-9]+)\*.*/\1/p' "$UC_CHANGELOG")
if [ -z "$VERSION" ]; then
    echo "Error: could not parse version from $UC_CHANGELOG (expected '#### YYYY-MM-DD *x.y.z*')" >&2
    exit 1
fi

"$SCRIPT_DIR/package-macos.sh" --notarize

PACKAGE_ZIP="UCDemo-MacOS-$VERSION-x86_64.zip"
cd "$SCRIPT_DIR/dist-macos"
rm -f "$SCRIPT_DIR/$PACKAGE_ZIP"
zip -r "$SCRIPT_DIR/$PACKAGE_ZIP" *.app
cd "$SCRIPT_DIR"
echo "Created $SCRIPT_DIR/$PACKAGE_ZIP"
