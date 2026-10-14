#!/bin/bash
# scripts/package-ultracanvasstart.sh - UltraCanvasStart on its own: one small
# archive per platform, cut out of the suite package the platform's packager
# has already made.
#
# UltraCanvasStart sets a computer up for UltraCanvas development, so it has
# to reach a computer that has nothing yet - no toolchain, no clone of the
# repository. Building it from source is the very step it exists to remove,
# and the suite packages (package-linux.sh, package-win.sh) carry it only as
# one of some twenty applications in a download of several hundred megabytes.
# This script takes the suite package and extracts the one application with
# exactly the libraries and resources it loads:
#
#   Linux    dist/UltraCanvas-Linux-<v>-<arch>/   ->  dist-start/UltraCanvasStart-Linux-<v>-<arch>.tar.xz
#            bin/UltraCanvasStart and its launcher, lib/ (the closure ldd
#            resolves inside the suite's lib/), share/media (fonts, icons)
#   Windows  dist/  (package-win.sh)              ->  dist-start/UltraCanvasStart-Windows-<v>-<arch>.zip
#            UltraCanvasStart.exe, the DLLs its import table reaches inside
#            dist/, cacert.pem for the SDK download, Resources/media (fonts,
#            icons), the uc-diagnose launchers
#   macOS    package-macos.sh --start-app does the same from the signed suite,
#            because signing and notarization live there.
#
# <v> is the framework version (line 1 of Docs/UltraCanvas/CHANGELOG.md), the
# same as the SDK archives' and the release tag's: the setup application and
# the SDK it fetches belong to the same release.
#
# The archive is run before it is declared done: the packaged application
# prints --version from inside the package, which proves the closure loads.
#
# Usage: scripts/package-ultracanvasstart.sh [--source DIR] [--output DIR]
#   --source DIR   the suite package: the Linux package folder or the Windows
#                  dist/ (default: dist/UltraCanvas-Linux-<v>-<arch> or dist)
#   --output DIR   where the archive goes (default: dist-start)
#   ARCH=<label>   the architecture label in the name (default as the suite
#                  packagers derive it: x86_64 or arm64)
#
# Version: 1.1.0 - the Linux archive is xz
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
APP=UltraCanvasStart
SOURCE=""
OUTPUT="dist-start"

while [ $# -gt 0 ]; do
    case "$1" in
        --source) SOURCE="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

# The media the framework and the application load at run time: the bundled
# fonts (the UI's own typeface, used before the system's are scanned), the
# widget icons, the application icons and media/lib (the default window icon
# and the cursors). The demo's samples, videos and 3D scenes stay out.
MEDIA_SUBSET=(fonts icons appicon lib)

VERSION=$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+\.[0-9]+\.[0-9]+)\*.*/\1/p' \
    "$PROJECT_DIR/Docs/UltraCanvas/CHANGELOG.md")
if [ -z "$VERSION" ]; then
    echo "Error: could not parse the version from Docs/UltraCanvas/CHANGELOG.md" >&2
    exit 1
fi

case "$(uname -s)" in
    Linux)                 OS=Linux ;;
    MINGW*|MSYS*|CYGWIN*)  OS=Windows ;;
    Darwin)
        echo "Error: on macOS run ./package-macos.sh --start-app (signing and notarization live there)" >&2
        exit 2 ;;
    *) echo "Error: unsupported system $(uname -s)" >&2; exit 2 ;;
esac

if [ "$OS" = "Windows" ]; then
    ARCH="${ARCH:-${MSYSTEM_CARCH:-x86_64}}"
    [ "$ARCH" = "aarch64" ] && ARCH="arm64"
else
    case "$(uname -m)" in
        x86_64|amd64)   ARCH="${ARCH:-x86_64}" ;;
        aarch64|arm64)  ARCH="${ARCH:-arm64}"  ;;
        *)              ARCH="${ARCH:-$(uname -m)}" ;;
    esac
fi

PKGNAME="$APP-$OS-$VERSION-$ARCH"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
PKG="$OUTPUT/$PKGNAME"
rm -rf "$PKG"

echo "=== $APP on its own ($OS $ARCH, UltraCanvas $VERSION) ==="

# The read-me at the top of the archive: what this is and how to start it.
write_readme() {
    local start="$1"
    cat > "$PKG/README.txt" <<EOF
UltraCanvasStart - the setup application of UltraCanvas $VERSION ($OS, $ARCH)

UltraCanvasStart sets this computer up for writing UltraCanvas applications:
it finds out what is installed, installs the development packages that are
missing, downloads the matching UltraCanvas SDK, writes a project skeleton
and explains how to work on it with an AI assistant.

Start it:  $start
Headless:  $start --check      what is installed and what is missing
           $start --plan       ...and the steps that would set it up

The SDK it downloads is the release of the same version:
https://github.com/ULTRA-OS-Project/UltraCanvas/releases/tag/v$VERSION

Documentation: Docs/GettingStarted.md and Apps/UltraCanvasStart/README.md in
https://github.com/ULTRA-OS-Project/UltraCanvas
EOF
}

copy_media_subset() {
    local from="$1" to="$2" m
    mkdir -p "$to"
    for m in "${MEDIA_SUBSET[@]}"; do
        if [ -d "$from/$m" ]; then
            cp -R "$from/$m" "$to/$m"
        else
            echo "  Warning: $from/$m is not in the suite package"
        fi
    done
}

if [ "$OS" = "Linux" ]; then
    # ── Linux: out of the portable bundle ─────────────────────────────────
    if [ -z "$SOURCE" ]; then
        SOURCE="dist/UltraCanvas-Linux-$VERSION-$ARCH"
    fi
    if [ ! -x "$SOURCE/bin/$APP" ]; then
        echo "Error: $SOURCE/bin/$APP is not there - run package-linux.sh first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"
    mkdir -p "$PKG/bin" "$PKG/lib" "$PKG/share"

    cp "$SOURCE/bin/$APP" "$PKG/bin/$APP"
    chmod 755 "$PKG/bin/$APP"
    # The launcher the suite wrote: it puts lib/ on LD_LIBRARY_PATH.
    if [ -f "$SOURCE/$APP" ]; then
        cp "$SOURCE/$APP" "$PKG/$APP"
    else
        cat > "$PKG/$APP" <<'WRAP'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/bin/UltraCanvasStart" "$@"
WRAP
    fi
    chmod +x "$PKG/$APP"

    # The libraries: what the dynamic loader resolves inside the suite's
    # lib/ when that folder is on the library path - ldd lists the whole
    # closure, so one pass is complete. What it resolves elsewhere is the
    # host's own (glibc, the GL and X11 stack), which the suite leaves to
    # the host on purpose (package-linux.sh, EXCLUDE_PATTERNS).
    echo "Collecting libraries..."
    missing=$(LD_LIBRARY_PATH="$SOURCE/lib" ldd "$SOURCE/bin/$APP" | grep -c 'not found' || true)
    if [ "$missing" -gt 0 ]; then
        echo "Error: $missing libraries of $APP are not found with the suite's lib/ on the path:" >&2
        LD_LIBRARY_PATH="$SOURCE/lib" ldd "$SOURCE/bin/$APP" | grep 'not found' >&2
        exit 1
    fi
    count=0
    while read -r name arrow path _; do
        [ "$arrow" = "=>" ] || continue
        case "$path" in
            "$SOURCE/lib/"*) ;;
            *) continue ;;
        esac
        cp -L "$path" "$PKG/lib/$name"
        count=$((count + 1))
    done < <(LD_LIBRARY_PATH="$SOURCE/lib" ldd "$SOURCE/bin/$APP")
    echo "  $count libraries from the suite's lib/"

    copy_media_subset "$SOURCE/share/media" "$PKG/share/media"
    write_readme "./$APP"

    # xz, not gzip: the package is mostly shared libraries, which xz packs
    # 28% smaller (35 MB against 49 MB for the same tree on 2026-10-09), and
    # the archive is a download for a computer with nothing yet. -T0 uses
    # every core; single-threaded the compression took a minute.
    ARCHIVE="$OUTPUT/$PKGNAME.tar.xz"
    XZ_OPT="-T0" tar -C "$OUTPUT" -cJf "$ARCHIVE" "$PKGNAME"
    RUN=("$PKG/$APP")

else
    # ── Windows: out of package-win.sh's dist/ ─────────────────────────────
    [ -z "$SOURCE" ] && SOURCE="dist"
    if [ ! -f "$SOURCE/$APP.exe" ]; then
        echo "Error: $SOURCE/$APP.exe is not there - run package-win.sh first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"
    mkdir -p "$PKG"
    cp "$SOURCE/$APP.exe" "$PKG/$APP.exe"

    # The import table, read statically as package-win.sh reads it: ldd
    # would load every module and runs into the codec deadlocks on ARM64.
    if command -v objdump >/dev/null 2>&1; then
        OBJDUMP=objdump
    elif command -v llvm-objdump >/dev/null 2>&1; then
        OBJDUMP=llvm-objdump
    else
        echo "Error: need objdump or llvm-objdump to read the import tables" >&2
        exit 1
    fi
    pe_deps() {
        "$OBJDUMP" -p "$1" 2>/dev/null | sed -n 's/.*DLL Name:[[:space:]]*//p' | tr -d '\r'
    }

    # The closure over dist/: every DLL an import names that dist/ carries
    # (Windows' own are not in dist/ and stay the system's), and theirs in
    # turn, until nothing new is named.
    echo "Collecting DLLs..."
    queue=("$PKG/$APP.exe")
    count=0
    while [ "${#queue[@]}" -gt 0 ]; do
        current="${queue[0]}"
        queue=("${queue[@]:1}")
        while read -r name; do
            [ -n "$name" ] || continue
            [ -f "$SOURCE/$name" ] || continue
            [ -f "$PKG/$name" ] && continue
            cp "$SOURCE/$name" "$PKG/$name"
            count=$((count + 1))
            queue+=("$PKG/$name")
        done < <(pe_deps "$current" | sort -u)
    done
    echo "  $count DLLs from $SOURCE"

    # The CA bundle: the SDK download is HTTPS, and the bundled libcurl
    # verifies against this file (package-win.sh puts it beside the .exe).
    if [ -f "$SOURCE/cacert.pem" ]; then
        cp "$SOURCE/cacert.pem" "$PKG/cacert.pem"
    else
        echo "  Warning: no cacert.pem in $SOURCE - the SDK download would fail its TLS check"
    fi

    copy_media_subset "$SOURCE/Resources/media" "$PKG/Resources/media"

    # The startup troubleshooting launchers (Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md).
    for f in uc-diagnose.bat uc-diagnose.ps1; do
        [ -f "$SOURCE/$f" ] && cp "$SOURCE/$f" "$PKG/$f"
    done
    write_readme "$APP.exe"

    ARCHIVE="$OUTPUT/$PKGNAME.zip"
    rm -f "$ARCHIVE"
    (cd "$OUTPUT" && zip -q -r "$ARCHIVE" "$PKGNAME")
    RUN=("$PKG/$APP.exe")
fi

# ── The proof: the packaged application runs from inside the package ───────
echo "Running the packaged application..."
if ! out=$("${RUN[@]}" --version 2>&1); then
    echo "Error: the packaged $APP does not run:" >&2
    echo "$out" >&2
    exit 1
fi
case "$out" in
    "$APP "*) echo "  $out" | head -1 ;;
    *) echo "Error: unexpected --version output from the packaged $APP:" >&2; echo "$out" >&2; exit 1 ;;
esac

# ── Summary ─────────────────────────────────────────────────────────────────
unpacked=$(du -sk "$PKG" | cut -f1)
archive=$(du -sk "$ARCHIVE" | cut -f1)
mb() { awk -v k="$1" 'BEGIN { printf "%.1f MB", k / 1024 }'; }
echo "=== Package complete ==="
echo "  Archive:  $ARCHIVE ($(mb "$archive"))"
echo "  Unpacked: $PKG ($(mb "$unpacked"))"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### $APP on its own ($OS $ARCH)"
        echo ""
        echo "| Item | Size |"
        echo "|---|---:|"
        echo "| **$(basename "$ARCHIVE")** (download) | $(mb "$archive") |"
        echo "| $PKGNAME/ (unpacked) | $(mb "$unpacked") |"
    } >> "$GITHUB_STEP_SUMMARY"
fi
