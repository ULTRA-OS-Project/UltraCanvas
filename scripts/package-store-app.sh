#!/bin/bash
# scripts/package-store-app.sh - one application as an UltraStore package:
# an archive cut out of the suite package the platform's packager has already
# made, carrying the application with exactly the libraries, plug-ins and
# resources it loads, ready for `ultrastoreadmin package add` and then
# `ultrastoreadmin market submit` (Docs/UltraStore/README.md, *Packaging an
# application for the store*).
#
# Why a package of its own, and why self-contained: the suite packages
# (package-linux.sh, package-win.sh) carry some twenty applications over one
# shared lib/, several hundred megabytes, and the store installs one
# application into <root>/apps/<id>/<version>/ and starts the file the
# catalog names as its entry - nothing more. It puts no runtime component on
# the library path, so an application whose package leaned on a shared
# org.ultraos.runtime would install and then not start. Each store package
# therefore carries its own closure, cut out of the suite the way
# scripts/package-ultracanvasstart.sh cuts UltraCanvasStart out of it:
#
#   Linux    dist/UltraCanvas-Linux-<fw>-<arch>/  ->  dist-store/<id>-<v>-linux-<arch>.tar.xz
#            <App>/<App>             the suite's launcher (lib/ on the path) - the entry
#            <App>/bin/<App>         the executable
#            <App>/bin/Plugins/...   the plug-in families it loads (below)
#            <App>/lib/              what the loader resolves inside the suite's lib/
#                                    for the executable and the plug-ins, and for the
#                                    run-time modules of those libraries (libvips'
#                                    loaders, libheif's codecs, ImageMagick's coders)
#            <App>/share/media/      the media the framework loads (MEDIA below)
#            <App>/share/<App>/      the application's own data, where the suite has it
#   Windows  dist/ (package-win.sh)                ->  dist-store/<id>-<v>-windows-<arch>.zip
#            <App>/<App>.exe and .com, the DLLs their import tables reach inside
#            dist/, Plugins/<family>/ with theirs, cacert.pem, Resources/media,
#            the uc-diagnose launchers, data/ for UltraFIBU
#   macOS    not yet: package-macos.sh builds the suite around one shared
#            Frameworks/ and signs it; an application on its own needs the
#            rewrite --start-app does for UltraCanvasStart (build_standalone_app),
#            which this script does not have. The script stops and says so.
#
# Plug-in families: every folder under the suite's Plugins/ goes with every
# application, except UltraIM - ChatCube's network providers and TDLib, some
# tens of megabytes - which goes with ChatCube alone. --plugins names the
# families instead ("none" for none).
#
# <v> is the application's own version, line 1 of its changelog (the table in
# AGENTS.md, *Versioning*); <id> its store id, org.ultraos.<app in lower
# case> unless --id says otherwise. The archive holds one folder, <App>/, so
# the entry stays the same from one version to the next. The script ends by
# printing the `ultrastoreadmin package add` line with the entry spelled out:
# on Linux the admin backend's suggestion would be bin/<App>, the executable
# without the launcher, which does not find lib/. The variant is the newest
# glibc any file in the package asks for (glibc-<x.y>).
#
# Before it is declared done the package is checked from inside: the loader
# resolves every library of the executable and the plug-ins within the
# package (Linux), and an application that answers --version runs from it.
#
# Usage: scripts/package-store-app.sh <App> [options]
#   <App>             the executable's name in the suite: UltraMail, UltraFiler, ...
#   --source DIR      the suite package: the Linux package folder or the Windows
#                     dist/ (default: dist/UltraCanvas-Linux-<fw>-<arch> or dist)
#   --output DIR      where the archive goes (default: dist-store)
#   --id ID           the store id (default: org.ultraos.<app in lower case>)
#   --version V       the version (default: line 1 of the application's changelog)
#   --plugins LIST    the plug-in families, space-separated, or "none"
#   --media LIST      the media folders, space-separated (default: MEDIA below)
#   ARCH=<label>      the architecture label (default as the suite packagers
#                     derive it: x86_64 or arm64)
#
# Version: 1.0.0 - the first version: Linux and Windows
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# The comment at the top of this file, up to the first line that is not one.
usage() { awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"; }

APP=""
SOURCE=""
OUTPUT="dist-store"
STORE_ID=""
APP_VERSION=""
PLUGINS_ARG=""
PLUGINS_GIVEN=false
# The media the framework loads at run time: the UI's typeface and the other
# bundled fonts, the widget icons, the application icons, media/lib (the
# default window icon and the cursors), the translations and the emoji fonts.
# The demo's samples, videos and 3D scenes stay out. An application that
# opens more (media/microtex for LaTeX, media/flags) names it with --media.
MEDIA=(fonts icons appicon lib locale emoji)

while [ $# -gt 0 ]; do
    case "$1" in
        --source)  SOURCE="$2"; shift 2 ;;
        --output)  OUTPUT="$2"; shift 2 ;;
        --id)      STORE_ID="$2"; shift 2 ;;
        --version) APP_VERSION="$2"; shift 2 ;;
        --plugins) PLUGINS_ARG="$2"; PLUGINS_GIVEN=true; shift 2 ;;
        --media)   read -r -a MEDIA <<< "$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            if [ -n "$APP" ]; then echo "One application at a time: $APP, then $1?" >&2; exit 2; fi
            APP="$1"; shift ;;
    esac
done
if [ -z "$APP" ]; then
    echo "Name the application: scripts/package-store-app.sh <App> (--help for the options)" >&2
    exit 2
fi
case "$APP" in
    */*|*\\*|.*) echo "Error: '$APP' is not an executable's name" >&2; exit 2 ;;
esac

# ── The versions ─────────────────────────────────────────────────────────────

# Line 1 of a changelog: `#### YYYY-MM-DD *x.y.z*` (Texter's has two parts).
changelog_version() {
    sed -nE '1s/^#### [0-9-]+ \*([0-9]+(\.[0-9]+)+)\*.*/\1/p' "$1"
}

FW_VERSION=$(changelog_version "$PROJECT_DIR/Docs/UltraCanvas/CHANGELOG.md")
if [ -z "$FW_VERSION" ]; then
    echo "Error: could not parse the version from Docs/UltraCanvas/CHANGELOG.md" >&2
    exit 1
fi

# The application's changelog: Docs/<App>/ for most, and the few whose
# executable is named otherwise than their folder (cmake/UltraCanvasVersion.cmake).
app_changelog() {
    case "$1" in
        UltraAIApp)              echo "Docs/UltraAI/CHANGELOG.md" ;;
        UOS-Settings)            echo "Docs/UOSSettings/CHANGELOG.md" ;;
        ultrafibu)               echo "Docs/UltraFIBU/CHANGELOG.md" ;;
        ultrapress)              echo "Docs/UltraPress/CHANGELOG.md" ;;
        UltraCanvasDemo|ultramsg) echo "Docs/UltraCanvas/CHANGELOG.md" ;;
        anchorpoint-cli)         echo "Docs/AnchorPoint/CHANGELOG.md" ;;
        *)                       echo "Docs/$1/CHANGELOG.md" ;;
    esac
}
if [ -z "$APP_VERSION" ]; then
    CHANGELOG="$(app_changelog "$APP")"
    if [ ! -f "$PROJECT_DIR/$CHANGELOG" ]; then
        echo "Error: no $CHANGELOG to read $APP's version from - pass --version" >&2
        exit 1
    fi
    APP_VERSION=$(changelog_version "$PROJECT_DIR/$CHANGELOG")
    if [ -z "$APP_VERSION" ]; then
        echo "Error: could not parse the version from $CHANGELOG - pass --version" >&2
        exit 1
    fi
fi

if [ -z "$STORE_ID" ]; then
    STORE_ID="org.ultraos.$(printf '%s' "$APP" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9.-')"
fi
if ! [[ "$STORE_ID" =~ ^[a-z0-9-]+(\.[a-z0-9-]+)+$ ]]; then
    echo "Error: '$STORE_ID' is not a reverse-DNS id (org.example.app)" >&2
    exit 2
fi

case "$(uname -s)" in
    Linux)                 OS=linux ;;
    MINGW*|MSYS*|CYGWIN*)  OS=windows ;;
    Darwin)
        echo "Error: macOS is not supported yet. The suite (package-macos.sh) shares one Frameworks/" >&2
        echo "  between its applications; one on its own needs the rewrite package-macos.sh --start-app" >&2
        echo "  does for UltraCanvasStart (build_standalone_app), signed and notarized there." >&2
        exit 2 ;;
    *) echo "Error: unsupported system $(uname -s)" >&2; exit 2 ;;
esac

if [ "$OS" = "windows" ]; then
    ARCH="${ARCH:-${MSYSTEM_CARCH:-x86_64}}"
    [ "$ARCH" = "aarch64" ] && ARCH="arm64"
else
    case "$(uname -m)" in
        x86_64|amd64)   ARCH="${ARCH:-x86_64}" ;;
        aarch64|arm64)  ARCH="${ARCH:-arm64}"  ;;
        *)              ARCH="${ARCH:-$(uname -m)}" ;;
    esac
fi

# The plug-in families of this application, from the suite's Plugins/ folder.
plugin_families() {
    local dir="$1" family
    if $PLUGINS_GIVEN; then
        [ "$PLUGINS_ARG" = "none" ] || printf '%s\n' $PLUGINS_ARG
        return 0
    fi
    [ -d "$dir" ] || return 0
    for family in "$dir"/*/; do
        [ -d "$family" ] || continue
        family="$(basename "$family")"
        if [ "$family" = "UltraIM" ] && [ "$APP" != "ChatCube" ]; then
            continue
        fi
        printf '%s\n' "$family"
    done
}

mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
STAGE="$OUTPUT/.stage-$APP"
PKG="$STAGE/$APP"
rm -rf "$STAGE"
mkdir -p "$PKG"
ARCHIVE_BASE="$STORE_ID-$APP_VERSION-$OS-$ARCH"

echo "=== $APP for the store: $STORE_ID $APP_VERSION ($OS $ARCH, from the UltraCanvas $FW_VERSION suite) ==="

copy_media() {
    local from="$1" to="$2" m
    mkdir -p "$to"
    for m in "${MEDIA[@]}"; do
        if [ -d "$from/$m" ]; then
            cp -R "$from/$m" "$to/$m"
        else
            echo "  Warning: $from/$m is not in the suite package"
        fi
    done
}

# The suite's folder of the application's own data (share/UltraFIBU for
# ultrafibu): its name equals the application's, ignoring case.
own_data_folder() {
    local parent="$1" entry name lower_app
    lower_app="$(printf '%s' "$APP" | tr '[:upper:]' '[:lower:]')"
    [ -d "$parent" ] || return 0
    for entry in "$parent"/*/; do
        [ -d "$entry" ] || continue
        name="$(basename "$entry")"
        case "$name" in media|Docs|DemoApp) continue ;; esac
        if [ "$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')" = "$lower_app" ]; then
            printf '%s\n' "$name"
        fi
    done
}

PLUGIN_LIST=()
VARIANT=""

if [ "$OS" = "linux" ]; then
    # ── Linux: out of the portable bundle ─────────────────────────────────
    [ -n "$SOURCE" ] || SOURCE="dist/UltraCanvas-Linux-$FW_VERSION-$ARCH"
    if [ ! -x "$SOURCE/bin/$APP" ]; then
        echo "Error: $SOURCE/bin/$APP is not there - run package-linux.sh first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"
    mkdir -p "$PKG/bin" "$PKG/lib" "$PKG/share"

    cp "$SOURCE/bin/$APP" "$PKG/bin/$APP"
    chmod 755 "$PKG/bin/$APP"
    # The launcher the suite wrote: lib/ on LD_LIBRARY_PATH, and the module
    # folders below for libvips, libheif and ImageMagick where they are.
    if [ -f "$SOURCE/$APP" ]; then
        cp "$SOURCE/$APP" "$PKG/$APP"
    else
        cat > "$PKG/$APP" <<WRAP
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\$0")")"
export LD_LIBRARY_PATH="\$HERE/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "\$HERE/bin/$APP" "\$@"
WRAP
    fi
    chmod 755 "$PKG/$APP"

    # The plug-in families, as the suite lays them out (bin/Plugins/<family>).
    while IFS= read -r family; do
        [ -n "$family" ] || continue
        if [ ! -d "$SOURCE/bin/Plugins/$family" ]; then
            echo "Error: the suite has no bin/Plugins/$family" >&2
            exit 1
        fi
        mkdir -p "$PKG/bin/Plugins"
        cp -a "$SOURCE/bin/Plugins/$family" "$PKG/bin/Plugins/$family"
        PLUGIN_LIST+=("$family")
    done < <(plugin_families "$SOURCE/bin/Plugins")
    echo "  Plug-ins: ${PLUGIN_LIST[*]:-none}"

    # The libraries: what the loader resolves inside the suite's lib/ with
    # that folder on the library path - ldd lists the whole closure of a
    # file, so one pass per file is complete. What it resolves elsewhere is
    # the host's own (glibc, the GL and X11 stack), which the suite leaves to
    # the host on purpose (package-linux.sh, EXCLUDE_PATTERNS). For the
    # executable and its plug-ins a library the loader cannot find is an
    # error; for a run-time module of a library (strict=false) it is a
    # warning, since the suite carries those modules as the host had them.
    collect() {
        local strict="$1"; shift
        local file listing name arrow path rest added=0
        for file in "$@"; do
            [ -f "$file" ] || continue
            # Read once, then searched: under pipefail `ldd | grep -q` reads
            # the status of an ldd that grep cut short, not grep's answer.
            listing=$(LD_LIBRARY_PATH="$SOURCE/lib" ldd "$file" 2>/dev/null || true)
            if grep -q 'not found' <<< "$listing"; then
                if [ "$strict" = true ]; then
                    echo "Error: $(basename "$file") needs libraries the suite's lib/ does not have:" >&2
                    grep 'not found' <<< "$listing" >&2
                    exit 1
                fi
                echo "  Warning: $(basename "$file") needs $(grep 'not found' <<< "$listing" | awk '{print $1}' | tr '\n' ' ')- not in the suite either"
            fi
            while read -r name arrow path rest; do
                [ "$arrow" = "=>" ] || continue
                case "$path" in "$SOURCE/lib/"*) ;; *) continue ;; esac
                [ -e "$PKG/lib/$name" ] && continue
                cp -L "$path" "$PKG/lib/$name"
                added=$((added + 1))
            done <<< "$listing"
        done
        COLLECTED=$((COLLECTED + added))
    }
    # Every ELF file under the folders given. Ends with status 0 whatever
    # the last file was: its output feeds pipelines run under pipefail.
    elf_files() {
        local dir f
        for dir in "$@"; do
            [ -d "$dir" ] || continue
            while IFS= read -r -d '' f; do
                if [ "$(head -c4 "$f" | tr -d '\0')" = $'\x7fELF' ]; then
                    printf '%s\n' "$f"
                fi
            done < <(find "$dir" -type f -print0)
        done
        return 0
    }

    echo "Collecting libraries..."
    COLLECTED=0
    mapfile -t ROOTS < <(printf '%s\n' "$PKG/bin/$APP"; elf_files "$PKG/bin/Plugins")
    collect true "${ROOTS[@]}"

    # The modules the libraries open at run time, which ldd cannot see:
    # libvips' loaders and libheif's codec plug-ins (HEIC, AVIF, JPEG XL),
    # and ImageMagick's coders behind libvips' magick loader. Taken with
    # their layout, where the launcher points libvips (VIPSHOME), libheif
    # (LIBHEIF_PLUGIN_PATH) and ImageMagick (MAGICK_*) at them, when the
    # library that opens them is in the closure - then their own closure.
    has_lib() { compgen -G "$PKG/lib/$1" > /dev/null; }
    MODULE_DIRS=()
    if has_lib 'libvips.so*'; then
        for d in "$SOURCE"/lib/*/vips-modules-*; do
            [ -d "$d" ] || continue
            rel="${d#"$SOURCE/"}"
            mkdir -p "$PKG/$(dirname "$rel")"
            cp -a "$d" "$PKG/$rel"
            MODULE_DIRS+=("$PKG/$rel")
        done
    fi
    if has_lib 'libheif.so*' && [ -d "$SOURCE/lib/libheif/plugins" ]; then
        mkdir -p "$PKG/lib/libheif"
        cp -a "$SOURCE/lib/libheif/plugins" "$PKG/lib/libheif/"
        MODULE_DIRS+=("$PKG/lib/libheif/plugins")
    fi
    if [ "${#MODULE_DIRS[@]}" -gt 0 ]; then
        mapfile -t MODULES < <(elf_files "${MODULE_DIRS[@]}")
        collect false "${MODULES[@]}"
    fi
    if has_lib 'libMagickCore*'; then
        for d in "$SOURCE"/lib/ImageMagick-*; do
            [ -d "$d" ] || continue
            cp -a "$d" "$PKG/lib/"
            mapfile -t MODULES < <(elf_files "$PKG/lib/$(basename "$d")")
            collect false "${MODULES[@]}"
        done
        if [ -d "$SOURCE/etc/ImageMagick-6" ]; then
            mkdir -p "$PKG/etc"
            cp -a "$SOURCE/etc/ImageMagick-6" "$PKG/etc/"
        fi
    fi
    echo "  $COLLECTED libraries from the suite's lib/"
    if [ "${#MODULE_DIRS[@]}" -gt 0 ] || has_lib 'libMagickCore*'; then
        echo "  with the run-time modules of libvips / libheif / ImageMagick"
    fi

    copy_media "$SOURCE/share/media" "$PKG/share/media"
    while IFS= read -r own; do
        [ -n "$own" ] || continue
        cp -R "$SOURCE/share/$own" "$PKG/share/$own"
        echo "  The application's own data: share/$own"
    done < <(own_data_folder "$SOURCE/share")

    # The oldest glibc the package runs on: the newest GLIBC_ version any of
    # its files asks for - the catalog's variant (glibc-<x.y>).
    GLIBC=$(elf_files "$PKG" | while IFS= read -r f; do
                { readelf -V --wide "$f" 2>/dev/null || true; } | { grep -o 'GLIBC_[0-9][0-9.]*' || true; }
            done | sed 's/^GLIBC_//' | sort -V -u | tail -1)
    if [ -n "$GLIBC" ]; then
        GLIBC="$(printf '%s' "$GLIBC" | cut -d. -f1-2)"
        VARIANT="glibc-$GLIBC"
    fi

    # The proof: from inside the package, with only its own lib/ on the
    # path, the loader finds every library of the executable and the
    # plug-ins that it found in the suite.
    echo "Checking the package from inside..."
    missing=0
    for file in "${ROOTS[@]}"; do
        listing=$(LD_LIBRARY_PATH="$PKG/lib" ldd "$file" 2>/dev/null || true)
        if grep -q 'not found' <<< "$listing"; then
            echo "  ERROR: $(basename "$file") does not load from the package:" >&2
            grep 'not found' <<< "$listing" >&2
            missing=$((missing + 1))
        fi
    done
    if [ "$missing" -gt 0 ]; then
        exit 1
    fi
    echo "  every library of ${#ROOTS[@]} file(s) resolves inside the package"
    RUN=("$PKG/$APP")
    ENTRY="$APP/$APP"
    FORMAT="tar.xz"
else
    # ── Windows: out of package-win.sh's dist/ ─────────────────────────────
    [ -n "$SOURCE" ] || SOURCE="dist"
    if [ ! -f "$SOURCE/$APP.exe" ]; then
        echo "Error: $SOURCE/$APP.exe is not there - run package-win.sh first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"
    cp "$SOURCE/$APP.exe" "$PKG/$APP.exe"
    # The console companion, where the application has one
    # (cmake/UltraCanvasWindowsGuiApp.cmake).
    [ -f "$SOURCE/$APP.com" ] && cp "$SOURCE/$APP.com" "$PKG/$APP.com"

    ROOTS=("$PKG/$APP.exe")
    while IFS= read -r family; do
        [ -n "$family" ] || continue
        if [ ! -d "$SOURCE/Plugins/$family" ]; then
            echo "Error: the suite has no Plugins/$family" >&2
            exit 1
        fi
        mkdir -p "$PKG/Plugins"
        cp -R "$SOURCE/Plugins/$family" "$PKG/Plugins/$family"
        PLUGIN_LIST+=("$family")
        # The plug-ins' own DLLs, not TDLib's: those sit beside tdjson.dll
        # under their hashed names and are loaded from there.
        for dll in "$PKG/Plugins/$family"/*.dll; do
            [ -f "$dll" ] && ROOTS+=("$dll")
        done
    done < <(plugin_families "$SOURCE/Plugins")
    echo "  Plug-ins: ${PLUGIN_LIST[*]:-none}"

    # The import tables, read statically by scripts/pe_imports.py as
    # package-ultracanvasstart.sh reads them (ldd would load every module):
    # the closure over dist/ of every DLL an import names that dist/
    # carries, and theirs in turn, for the executable and each plug-in.
    echo "Collecting DLLs..."
    PYTHON="$(command -v python3 || command -v python || true)"
    if [ -z "$PYTHON" ]; then
        echo "Error: python3 is needed to read the import tables (scripts/pe_imports.py)" >&2
        exit 1
    fi
    # MSYS2's mingw Python is a Windows program: give it paths it reads.
    topath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }
    for root in "${ROOTS[@]}"; do
        "$PYTHON" "$(topath "$SCRIPT_DIR/pe_imports.py")" \
            --closure "$(topath "$root")" \
            --from "$(topath "$SOURCE")" --into "$(topath "$PKG")" > /dev/null
    done
    count=$(find "$PKG" -maxdepth 1 -iname '*.dll' | wc -l | tr -d ' ')
    echo "  $count DLLs from $SOURCE"
    if [ "$count" -eq 0 ]; then
        echo "Error: no DLL of $APP found in $SOURCE - the import tables were not read" >&2
        exit 1
    fi

    # The CA bundle the bundled libcurl verifies HTTPS against (package-win.sh
    # puts it beside the executables).
    if [ -f "$SOURCE/cacert.pem" ]; then
        cp "$SOURCE/cacert.pem" "$PKG/cacert.pem"
    else
        echo "  Warning: no cacert.pem in $SOURCE - HTTPS would fail its TLS check"
    fi
    copy_media "$SOURCE/Resources/media" "$PKG/Resources/media"
    # UltraFIBU's chart of accounts and tax keys: data/ beside the executable.
    if [ "$(printf '%s' "$APP" | tr '[:upper:]' '[:lower:]')" = "ultrafibu" ] && [ -d "$SOURCE/data" ]; then
        cp -R "$SOURCE/data" "$PKG/data"
        echo "  The application's own data: data/"
    fi
    # The startup troubleshooting launchers (Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md).
    for f in uc-diagnose.bat uc-diagnose.ps1; do
        [ -f "$SOURCE/$f" ] && cp "$SOURCE/$f" "$PKG/$f"
    done
    RUN=("$PKG/$APP.exe")
    ENTRY="$APP/$APP.exe"
    FORMAT="zip"
fi

# ── An application that answers --version runs from inside the package ─────
# Only one whose executable carries the option's name: another would open its
# window. A run that fails because a library is missing fails the package -
# the loader's message on Linux; on Windows the status of a process Windows
# would not start (0xC0000135 a DLL not found, 0xC000007B a wrong image, of
# which the shell keeps the low byte, and 127). Any other failure - no display
# on a build machine, an option the program reads differently - is a warning:
# the closure check above stands either way.
EXE="$PKG/bin/$APP"
[ "$OS" = "windows" ] && EXE="$PKG/$APP.exe"
if grep -aq -- '--version' "$EXE"; then
    echo "Running the packaged application..."
    status=0
    out=$(timeout 30 "${RUN[@]}" --version 2>&1 < /dev/null) || status=$?
    load_failure=false
    if [ "$OS" = "linux" ]; then
        grep -q 'error while loading shared libraries' <<< "$out" && load_failure=true
    else
        case "$status" in 53|123|127) load_failure=true ;; esac
    fi
    if $load_failure; then
        echo "Error: the packaged $APP does not start (exit status $status):" >&2
        printf '%s\n' "$out" >&2
        exit 1
    elif [ "$status" -eq 0 ]; then
        echo "  $(printf '%s\n' "$out" | head -1)"
    else
        echo "  Warning: $APP --version ended with status $status (not a missing library): $(printf '%s\n' "$out" | head -1)"
    fi
else
    echo "  ($APP does not answer --version: the check above is the proof)"
fi

# ── The archive ─────────────────────────────────────────────────────────────

ARCHIVE="$OUTPUT/$ARCHIVE_BASE.$FORMAT"
rm -f "$ARCHIVE"
if [ "$FORMAT" = "tar.xz" ]; then
    # xz, as for UltraCanvasStart: the package is mostly shared libraries,
    # which xz packs about a quarter smaller than gzip. tar keeps the
    # execute bits of the launcher, the executable and the plug-ins; the
    # store's installer restores them (a zip made on Windows has none).
    XZ_OPT="-T0" tar -C "$STAGE" -cJf "$ARCHIVE" "$APP"
else
    (cd "$STAGE" && zip -q -r "$ARCHIVE" "$APP")
fi

SHA256=$( (sha256sum "$ARCHIVE" 2>/dev/null || shasum -a 256 "$ARCHIVE") | cut -d' ' -f1)
unpacked=$(du -sk "$PKG" | cut -f1)
packed=$(du -sk "$ARCHIVE" | cut -f1)
mb() { awk -v k="$1" 'BEGIN { printf "%.1f MB", k / 1024 }'; }
rm -rf "$STAGE"

ADD="ultrastoreadmin package add $STORE_ID $APP_VERSION $ARCHIVE --os $OS --arch $ARCH --entry $ENTRY"
[ -n "$VARIANT" ] && ADD+=" --variant $VARIANT"

echo "=== Package complete ==="
echo "  Archive:  $ARCHIVE ($(mb "$packed"), $(mb "$unpacked") unpacked)"
echo "  SHA-256:  $SHA256"
echo "  Entry:    $ENTRY"
[ -n "$VARIANT" ] && echo "  Variant:  $VARIANT"
echo ""
echo "Into the admin backend (after \`ultrastoreadmin release add $STORE_ID $APP_VERSION\`):"
echo "  $ADD"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### $APP for the store ($OS $ARCH)"
        echo ""
        echo "| Item | Size |"
        echo "|---|---:|"
        echo "| **$(basename "$ARCHIVE")** | $(mb "$packed") |"
        echo "| $APP/ (unpacked) | $(mb "$unpacked") |"
        echo ""
        echo "\`$ADD\`"
    } >> "$GITHUB_STEP_SUMMARY"
fi
