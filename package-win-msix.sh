#!/bin/bash
# package-win-msix.sh - Build an MSIX package of the Windows applications
#
# Run from an MSYS2 CLANG64 (x86_64) or CLANGARM64 (arm64) shell AFTER
# package-win.sh, which builds dist/. Needs the Windows SDK (makeappx.exe,
# makepri.exe, signtool.exe), Python 3 and ImageMagick - all already on the
# CI runners and in the MSYS2 environment build.yml sets up.
#
# Usage:
#   ./package-win-msix.sh [--mode test|store|signed]
#
# The modes (Docs/UltraCanvas/UltraCanvasWindowsMSIX.md says more):
#   test    (default) Not signed. The Publisher carries Microsoft's "unsigned
#           package" OID, so Windows 11 installs it from an elevated
#           PowerShell with `Add-AppxPackage -AllowUnsigned <file>`.
#           For testing only - never distribute it.
#   store   Not signed, with the identity Partner Center assigned to the app
#           (Product identity page): MSIX_IDENTITY_NAME, MSIX_PUBLISHER and
#           MSIX_PUBLISHER_DISPLAY_NAME must be set. Upload this one to
#           Partner Center; the Store signs it.
#   signed  Signed with your own code signing certificate, for distribution
#           outside the Store: MSIX_SIGN_PFX (+ MSIX_SIGN_PASSWORD), or
#           MSIX_SIGN_THUMBPRINT for a certificate in the Windows certificate
#           store. MSIX_PUBLISHER must be exactly that certificate's subject.
#
# Output: dist-msix/UltraCanvas-Windows-<version>-<arch>[-test|-store].msix
#
# Environment:
#   MSIX_IDENTITY_NAME           default ULTRAOSDevelopment.UltraCanvas
#   MSIX_PUBLISHER               default the subject SignUltraDemo.ps1 uses
#   MSIX_PUBLISHER_DISPLAY_NAME  default ULTRA OS Development GmbH
#   MSIX_TIMESTAMP_URL           default http://timestamp.digicert.com
#   WINDOWS_SDK_BIN              folder holding makeappx.exe, if not found
#
set -euo pipefail

MODE=test
while [ $# -gt 0 ]; do
    case "$1" in
        --mode)   MODE="${2:-}"; shift 2 ;;
        --mode=*) MODE="${1#--mode=}"; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//;$d'; exit 0 ;;
        *) echo "Error: unknown argument $1" >&2; exit 1 ;;
    esac
done
case "$MODE" in
    test|store|signed) ;;
    *) echo "Error: --mode must be test, store or signed (got '$MODE')" >&2; exit 1 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

UC_CHANGELOG="Docs/UltraCanvas/CHANGELOG.md"
VERSION=$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+\.[0-9]+\.[0-9]+)\*.*/\1/p' "$UC_CHANGELOG")
if [ -z "$VERSION" ]; then
    echo "Error: could not parse version from $UC_CHANGELOG (expected '#### YYYY-MM-DD *x.y.z*')" >&2
    exit 1
fi

# The package's architecture is the one dist/ was built for: package-win.sh
# ran in this same MSYS2 environment, and scripts/verify-pe.sh has checked
# every binary in dist/ against it.
case "${MSYSTEM_CARCH:-x86_64}" in
    x86_64)  MSIX_ARCH=x64;   ARCH_LABEL=x86_64 ;;
    aarch64) MSIX_ARCH=arm64; ARCH_LABEL=arm64 ;;
    *) echo "Error: no MSIX architecture for MSYSTEM_CARCH=$MSYSTEM_CARCH" >&2; exit 1 ;;
esac

DEFAULT_PUBLISHER="CN=ULTRA OS Development GmbH, O=ULTRA OS Development GmbH, L=Unknown, C=DE"
if [ "$MODE" = store ]; then
    # A Store upload whose identity differs from Partner Center's is refused,
    # so there is no default to fall back on here.
    for var in MSIX_IDENTITY_NAME MSIX_PUBLISHER MSIX_PUBLISHER_DISPLAY_NAME; do
        if [ -z "${!var:-}" ]; then
            echo "Error: --mode store needs $var - copy it from Partner Center > the app > Product identity" >&2
            exit 1
        fi
    done
fi
IDENTITY_NAME="${MSIX_IDENTITY_NAME:-ULTRAOSDevelopment.UltraCanvas}"
PUBLISHER="${MSIX_PUBLISHER:-$DEFAULT_PUBLISHER}"
PUBLISHER_DISPLAY_NAME="${MSIX_PUBLISHER_DISPLAY_NAME:-ULTRA OS Development GmbH}"

if [ "$MODE" = signed ] && [ -z "${MSIX_SIGN_PFX:-}" ] && [ -z "${MSIX_SIGN_THUMBPRINT:-}" ]; then
    echo "Error: --mode signed needs MSIX_SIGN_PFX (and MSIX_SIGN_PASSWORD) or MSIX_SIGN_THUMBPRINT" >&2
    exit 1
fi

if [ ! -d dist ] || ! compgen -G "dist/*.exe" >/dev/null; then
    echo "Error: dist/ holds no executables - run ./package-win.sh first" >&2
    exit 1
fi

# ── Windows SDK tools ───────────────────────────────────────────────────────

# The host's own build of the tools, newest SDK first, then the x64 build
# (which Windows on ARM runs emulated).
HOST_ARCH=x64
[ "${PROCESSOR_ARCHITECTURE:-}" = "ARM64" ] && HOST_ARCH=arm64

find_sdk_tool() {
    local tool="$1" kits arch found
    if [ -n "${WINDOWS_SDK_BIN:-}" ]; then
        found="$(cygpath -u "$WINDOWS_SDK_BIN")/$tool"
        [ -f "$found" ] && { printf '%s\n' "$found"; return 0; }
        return 1
    fi
    for kits in "/c/Program Files (x86)/Windows Kits/10/bin" "/c/Program Files/Windows Kits/10/bin"; do
        for arch in "$HOST_ARCH" x64; do
            found="$(printf '%s\n' "$kits"/10.*/"$arch"/"$tool" | sort -rV | head -1)"
            [ -f "$found" ] && { printf '%s\n' "$found"; return 0; }
        done
    done
    return 1
}

MAKEAPPX="$(find_sdk_tool makeappx.exe)" || {
    echo "Error: makeappx.exe not found - install the Windows SDK, or set WINDOWS_SDK_BIN" >&2
    exit 1
}
MAKEPRI="$(find_sdk_tool makepri.exe)" || {
    echo "Error: makepri.exe not found beside makeappx.exe in the Windows SDK" >&2
    exit 1
}
echo "makeappx: $MAKEAPPX"
echo "makepri:  $MAKEPRI"

# MSYS2 rewrites arguments that look like POSIX paths before it starts a
# native program, and makeappx's switches are exactly that shape: /d would
# reach it as D:/ and /p as P:/. The SDK tools get their arguments as written,
# and every path is handed to them in Windows form.
win() { MSYS2_ARG_CONV_EXCL='*' "$@"; }
winpath() { cygpath -w "$1"; }

# ── Layout ──────────────────────────────────────────────────────────────────

WORK_DIR="build-msix/$MODE"
LAYOUT="$WORK_DIR/layout"
PRI_ROOT="$WORK_DIR/pri"
PRI_CONFIG="$WORK_DIR/priconfig.xml"
OUT_DIR="dist-msix"
SUFFIX=""
[ "$MODE" != signed ] && SUFFIX="-$MODE"
PACKAGE="$OUT_DIR/UltraCanvas-Windows-$VERSION-$ARCH_LABEL$SUFFIX.msix"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR" "$OUT_DIR"
rm -f "$PACKAGE"

echo ""
echo "── MSIX layout ($MODE, $MSIX_ARCH, $VERSION) ──"
LAYOUT_ARGS=(
    --dist dist
    --layout "$LAYOUT"
    --pri-root "$PRI_ROOT"
    --pri-config "$PRI_CONFIG"
    --version "$VERSION"
    --arch "$MSIX_ARCH"
    --identity-name "$IDENTITY_NAME"
    --publisher "$PUBLISHER"
    --publisher-display-name "$PUBLISHER_DISPLAY_NAME"
)
[ "$MODE" = test ] && LAYOUT_ARGS+=(--unsigned-test)
PYTHON="$(command -v python3 || command -v python)"
"$PYTHON" scripts/make_msix_layout.py "${LAYOUT_ARGS[@]}"

# ── resources.pri: which logo file serves which size and scale ─────────────

echo ""
echo "── Indexing the logos (makepri) ──"
win "$MAKEPRI" new \
    /pr "$(winpath "$PRI_ROOT")" \
    /cf "$(winpath "$PRI_CONFIG")" \
    /mn "$(winpath "$PRI_ROOT/AppxManifest.xml")" \
    /of "$(winpath "$WORK_DIR/resources.pri")" \
    /o
if compgen -G "$WORK_DIR/resources.*.pri" >/dev/null; then
    echo "Error: makepri split the index ($(ls "$WORK_DIR"/resources.*.pri | xargs -n1 basename | tr '\n' ' ')); the configuration must have no <packaging> element" >&2
    exit 1
fi
cp "$WORK_DIR/resources.pri" "$LAYOUT/resources.pri"

# ── Pack ────────────────────────────────────────────────────────────────────

echo ""
echo "── Packing (makeappx) ──"
win "$MAKEAPPX" pack \
    /d "$(winpath "$LAYOUT")" \
    /p "$(winpath "$PACKAGE")" \
    /h SHA256 \
    /o

# ── Sign ────────────────────────────────────────────────────────────────────

if [ "$MODE" = signed ]; then
    SIGNTOOL="$(find_sdk_tool signtool.exe)" || {
        echo "Error: signtool.exe not found in the Windows SDK" >&2
        exit 1
    }
    TIMESTAMP_URL="${MSIX_TIMESTAMP_URL:-http://timestamp.digicert.com}"
    echo ""
    echo "── Signing (signtool) ──"
    # /fd must be the hash makeappx packed with (/h SHA256 above). signtool
    # refuses the package with 0x8007000B when MSIX_PUBLISHER is not the
    # certificate's subject, letter for letter.
    if [ -n "${MSIX_SIGN_PFX:-}" ]; then
        SIGN_ARGS=(/f "$(winpath "$MSIX_SIGN_PFX")")
        [ -n "${MSIX_SIGN_PASSWORD:-}" ] && SIGN_ARGS+=(/p "$MSIX_SIGN_PASSWORD")
    else
        SIGN_ARGS=(/sha1 "$MSIX_SIGN_THUMBPRINT")
    fi
    win "$SIGNTOOL" sign /fd SHA256 "${SIGN_ARGS[@]}" \
        /tr "$TIMESTAMP_URL" /td SHA256 "$(winpath "$PACKAGE")"
    win "$SIGNTOOL" verify /pa "$(winpath "$PACKAGE")" || \
        echo "Warning: the signature does not chain to a root this machine trusts (expected for a self-signed certificate)" >&2
fi

echo ""
echo "=== MSIX complete ==="
echo "  Package: $PACKAGE ($(du -h "$PACKAGE" | cut -f1))"
case "$MODE" in
    test)   echo "  Install (Windows 11, elevated PowerShell): Add-AppxPackage -AllowUnsigned $(basename "$PACKAGE")" ;;
    store)  echo "  Upload it in Partner Center > the app > Packages; the Store signs it." ;;
    signed) echo "  Install: double-click it, or Add-AppxPackage $(basename "$PACKAGE")" ;;
esac
