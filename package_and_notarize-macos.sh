#!/bin/bash
set -e

# Notarization credentials come from the environment, never from this file:
# it is committed, and anything written here is readable by everyone with
# access to the repository (and stays in its history). Set them in the shell,
# e.g. from the login keychain:
#   export APPLE_ID=... APPLE_TEAM_ID=...
#   export APPLE_APP_PASSWORD="$(security find-generic-password -s ultracanvas-notary -w)"
for var in APPLE_ID APPLE_TEAM_ID APPLE_APP_PASSWORD; do
    if [ -z "${!var:-}" ]; then
        echo "Error: $var is not set - export APPLE_ID, APPLE_TEAM_ID and APPLE_APP_PASSWORD first" >&2
        exit 1
    fi
done

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

PACKAGE_ZIP="UCDemo-MacOS-$VERSION-$(uname -m).zip"
cd "$SCRIPT_DIR/dist-macos"
rm -f "$SCRIPT_DIR/$PACKAGE_ZIP"
# The whole suite folder: its apps load their libraries from the shared
# UltraCanvas/Frameworks/ beside them, so an app zipped on its own would not
# start (see "Suite layout" in package-macos.sh).
zip -r -y "$SCRIPT_DIR/$PACKAGE_ZIP" UltraCanvas
cd "$SCRIPT_DIR"
echo "Created $SCRIPT_DIR/$PACKAGE_ZIP"
