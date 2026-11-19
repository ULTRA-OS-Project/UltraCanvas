#!/bin/bash
# scripts/package-store-app.sh - one application, or the UltraCanvas runtime,
# as an UltraStore package: an archive cut out of the suite package the
# platform's packager has already made, ready for `ultrastoreadmin package
# add` and then `ultrastoreadmin market submit` (Docs/UltraStore/README.md,
# *Packaging an application for the store*).
#
# The store unpacks a package into <root>/apps/<id>/<version>/ (an app) or
# <root>/components/<id>/<version>/ (a runtime or library) and starts an
# app's entry with its runtime layer - the components its release depends on,
# at the exact versions - on the library path, and the runtime's folder in
# ULTRACANVAS_RUNTIME_DIR, where the framework finds media/ and UltraNet's
# plug-ins after the app's own (StoreQuery.h, PlanLaunch). So a package comes
# in one of three shapes, all cut out of the suite the way
# scripts/package-ultracanvasstart.sh cuts UltraCanvasStart:
#
#   <App>               the app on its own: everything it loads, in one folder.
#                       Runs in every store, an older one included.
#   --runtime           org.ultraos.runtime at the framework's version: the
#                       core library and what the loader resolves for it, the
#                       plug-in families every app may load (all but UltraIM)
#                       with theirs, the run-time modules of libvips, libheif
#                       and ImageMagick, and the media the framework loads -
#                       laid out like the suite, at the top of the archive.
#   <App> --on-runtime  the app without what that runtime carries, its release
#                       depending on org.ultraos.runtime@<framework version>:
#                       a few megabytes where the app on its own is a hundred.
#                       Needs a store that starts an app on its runtime layer
#                       (UltraStore after 0.1.21).
#
#   Linux    dist/UltraCanvas-Linux-<fw>-<arch>/  ->  dist-store/<id>-<v>-linux-<arch>.tar.xz
#            <App>/<App>             the launcher (lib/ on the path, the module folders
#                                    of libvips, libheif and ImageMagick) - the entry
#            <App>/bin/<App>         the executable
#            <App>/bin/Plugins/...   the plug-in families it loads (below)
#            <App>/lib/              what the loader resolves inside the suite's lib/
#            <App>/share/media/      the media the framework loads (MEDIA below)
#            <App>/share/<App>/      the application's own data, where the suite has it
#            The runtime: lib/, bin/Plugins/, share/media/, etc/ at the top.
#   Windows  dist/ (package-win.sh)                ->  dist-store/<id>-<v>-windows-<arch>.zip
#            <App>/<App>.exe and .com, the DLLs their import tables reach inside
#            dist/, Plugins/<family>/ with theirs, cacert.pem, Resources/media,
#            the uc-diagnose launchers, data/ for UltraFIBU
#            The runtime: the core DLL and its closure, Plugins/, Resources/media
#            at the top.
#   macOS    dist-store-macos/ (package-macos.sh --store-app <App>)
#                                                  ->  dist-store/<id>-<v>-macos-<arch>.zip
#            <App>.app with a Contents/Frameworks/ of its own, signed (and
#            notarized with --notarize) there; this script zips it as it is.
#            An app on macOS is always on its own: `open` passes no
#            environment, so neither --runtime nor --on-runtime applies.
#
# Plug-in families: every folder under the suite's Plugins/ goes with every
# application, except UltraIM - ChatCube's network providers and TDLib, some
# tens of megabytes - which goes with ChatCube alone. --plugins names the
# families instead ("none" for none).
#
# <v> is the application's own version, line 1 of its changelog (the table in
# AGENTS.md, *Versioning*), the framework's for the runtime; <id> the store
# id, org.ultraos.<app in lower case> or org.ultraos.runtime, unless --id
# says otherwise. An app's archive holds one folder, <App>/, so the entry
# stays the same from one version to the next. The script ends with the
# `ultrastoreadmin` lines to add the release and the package, the entry, the
# glibc variant (the newest any file in the package asks for) or the macOS
# minimum spelled out.
#
# Before it is declared done the package is checked from inside: the loader
# resolves every library of the executable and the plug-ins within the
# package - with the runtime beside it, for --on-runtime (Linux) - and an
# application that answers --version runs from it.
#
# Usage: scripts/package-store-app.sh <App> [--on-runtime] [options]
#        scripts/package-store-app.sh --runtime [options]
#   <App>             the executable's name in the suite: UltraMail, UltraFiler, ...
#   --runtime         package the UltraCanvas runtime instead of an application
#   --on-runtime      the application without what the runtime carries
#   --source DIR      the suite package: the Linux package folder, the Windows
#                     dist/, the macOS folder of package-macos.sh --store-app
#                     (default: dist/UltraCanvas-Linux-<fw>-<arch>, dist,
#                     dist-store-macos)
#   --output DIR      where the archive goes (default: dist-store)
#   --id ID           the store id
#   --version V       the version
#   --plugins LIST    the plug-in families, space-separated, or "none"
#   --media LIST      the media folders, space-separated (default: MEDIA below)
#   ARCH=<label>      the architecture label (default as the suite packagers
#                     derive it: x86_64 or arm64)
#
# Version: 1.1.0 - the runtime (--runtime) and an app on it (--on-runtime);
#                  macOS from package-macos.sh --store-app
# Version: 1.0.0 - the first version: Linux and Windows
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# The comment at the top of this file, up to the first line that is not one.
usage() { awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"; }

RUNTIME_ID="org.ultraos.runtime"
APP=""
MODE="app"          # app | runtime | on-runtime
SOURCE=""
OUTPUT="dist-store"
STORE_ID=""
PKG_VERSION=""
PLUGINS_ARG=""
PLUGINS_GIVEN=false
# The media the framework loads at run time: the UI's typeface and the other
# bundled fonts, the widget icons, the application icons, media/lib (the
# default window icon and the cursors), the translations and the emoji fonts.
# The demo's samples, videos and 3D scenes stay out. An application that
# opens more (media/microtex for LaTeX, media/flags) names it with --media.
MEDIA=(fonts icons appicon lib locale emoji)
ON_RUNTIME=false

while [ $# -gt 0 ]; do
    case "$1" in
        --runtime)    MODE="runtime"; shift ;;
        --on-runtime) ON_RUNTIME=true; shift ;;
        --source)     SOURCE="$2"; shift 2 ;;
        --output)     OUTPUT="$2"; shift 2 ;;
        --id)         STORE_ID="$2"; shift 2 ;;
        --version)    PKG_VERSION="$2"; shift 2 ;;
        --plugins)    PLUGINS_ARG="$2"; PLUGINS_GIVEN=true; shift 2 ;;
        --media)      read -r -a MEDIA <<< "$2"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            if [ -n "$APP" ]; then echo "One application at a time: $APP, then $1?" >&2; exit 2; fi
            APP="$1"; shift ;;
    esac
done
if [ "$MODE" = "runtime" ]; then
    if [ -n "$APP" ] || $ON_RUNTIME; then
        echo "--runtime packages the runtime alone: no application, no --on-runtime" >&2
        exit 2
    fi
    APP="runtime"
else
    if [ -z "$APP" ]; then
        echo "Name the application: scripts/package-store-app.sh <App> (--help for the options)" >&2
        exit 2
    fi
    case "$APP" in
        */*|*\\*|.*) echo "Error: '$APP' is not an executable's name" >&2; exit 2 ;;
    esac
    $ON_RUNTIME && MODE="on-runtime"
fi

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
if [ -z "$PKG_VERSION" ]; then
    if [ "$MODE" = "runtime" ]; then
        PKG_VERSION="$FW_VERSION"
    else
        CHANGELOG="$(app_changelog "$APP")"
        if [ ! -f "$PROJECT_DIR/$CHANGELOG" ]; then
            echo "Error: no $CHANGELOG to read $APP's version from - pass --version" >&2
            exit 1
        fi
        PKG_VERSION=$(changelog_version "$PROJECT_DIR/$CHANGELOG")
        if [ -z "$PKG_VERSION" ]; then
            echo "Error: could not parse the version from $CHANGELOG - pass --version" >&2
            exit 1
        fi
    fi
fi

if [ -z "$STORE_ID" ]; then
    if [ "$MODE" = "runtime" ]; then
        STORE_ID="$RUNTIME_ID"
    else
        STORE_ID="org.ultraos.$(printf '%s' "$APP" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9.-')"
    fi
fi
if ! [[ "$STORE_ID" =~ ^[a-z0-9-]+(\.[a-z0-9-]+)+$ ]]; then
    echo "Error: '$STORE_ID' is not a reverse-DNS id (org.example.app)" >&2
    exit 2
fi

case "$(uname -s)" in
    Linux)                 OS=linux ;;
    MINGW*|MSYS*|CYGWIN*)  OS=windows ;;
    Darwin)                OS=macos ;;
    *) echo "Error: unsupported system $(uname -s)" >&2; exit 2 ;;
esac
if [ "$OS" = "macos" ] && [ "$MODE" != "app" ]; then
    echo "Error: on macOS an app is a bundle with its own Frameworks/ - there is no runtime package," >&2
    echo "  and \`open\` passes no environment to put one on the path. Package the app alone." >&2
    exit 2
fi

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

mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
STAGE="$OUTPUT/.stage-$APP"
rm -rf "$STAGE"
mkdir -p "$STAGE"
# An app's archive holds the folder <App>/; the runtime's holds its layout at
# the top, since the store unpacks it as the component's folder and looks for
# lib/ there.
if [ "$MODE" = "runtime" ]; then
    PKG="$STAGE/root"
else
    PKG="$STAGE/$APP"
fi
mkdir -p "$PKG"
# The runtime an --on-runtime app is cut against, staged from the same suite.
RT="$STAGE/.runtime"
ARCHIVE_BASE="$STORE_ID-$PKG_VERSION-$OS-$ARCH"

case "$MODE" in
    runtime)    echo "=== The UltraCanvas runtime for the store: $STORE_ID $PKG_VERSION ($OS $ARCH) ===" ;;
    on-runtime) echo "=== $APP for the store on $RUNTIME_ID $FW_VERSION: $STORE_ID $PKG_VERSION ($OS $ARCH) ===" ;;
    *)          echo "=== $APP for the store: $STORE_ID $PKG_VERSION ($OS $ARCH, from the UltraCanvas $FW_VERSION suite) ===" ;;
esac

# ── Shared helpers ──────────────────────────────────────────────────────────

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

# The plug-in families for `who` (an application's name, or "runtime"), from
# the suite's Plugins/ folder: every family but UltraIM, which is ChatCube's.
plugin_families() {
    local dir="$1" who="$2" family
    if $PLUGINS_GIVEN; then
        [ "$PLUGINS_ARG" = "none" ] || printf '%s\n' $PLUGINS_ARG
        return 0
    fi
    [ -d "$dir" ] || return 0
    for family in "$dir"/*/; do
        [ -d "$family" ] || continue
        family="$(basename "$family")"
        if [ "$family" = "UltraIM" ] && [ "$who" != "ChatCube" ]; then
            continue
        fi
        printf '%s\n' "$family"
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

VARIANT=""
MIN_OS=""
ENTRY=""          # set with RUN, for an application
RUN=()
# What the store adds to the environment of an app on the runtime
# (PlanLaunch), for the run below; "" when nothing. Strings, not an array:
# macOS's bash 3.2 calls an empty array unbound under set -u.
RUN_LIBRARY_PATH=""
RUN_RUNTIME_DIR=""

# ════════════════════════════════════════════════════════════════════════════
# Linux: out of the portable bundle
# ════════════════════════════════════════════════════════════════════════════

# The libraries a file loads, as the loader resolves them with the suite's
# lib/ on the path. Read once, then searched: under pipefail `ldd | grep -q`
# reads the status of an ldd that grep cut short, not grep's answer.
ldd_listing() { LD_LIBRARY_PATH="$1" ldd "$2" 2>/dev/null || true; }

# collect STRICT DEST_LIB EXCLUDE_LIB FILE...: copies into DEST_LIB what the
# loader resolves inside the suite's lib/ for each file - ldd lists a file's
# whole closure, so one pass per file is complete - leaving out a library
# EXCLUDE_LIB (the runtime's lib/, or "") has. What it resolves elsewhere is
# the host's own (glibc, the GL and X11 stack), which the suite leaves to the
# host on purpose (package-linux.sh, EXCLUDE_PATTERNS). For an executable and
# its plug-ins (STRICT=true) a library the loader cannot find is an error;
# for a run-time module of a library it is a warning, since the suite
# carries those modules as the host had them.
collect() {
    local strict="$1" dest="$2" exclude="$3"; shift 3
    local file listing name arrow path rest
    for file in "$@"; do
        [ -f "$file" ] || continue
        listing=$(ldd_listing "$SOURCE/lib" "$file")
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
            [ -e "$dest/$name" ] && continue
            [ -n "$exclude" ] && [ -e "$exclude/$name" ] && continue
            cp -L "$path" "$dest/$name"
            COLLECTED=$((COLLECTED + 1))
        done <<< "$listing"
    done
}

# Every ELF file under the folders given. Ends with status 0 whatever the
# last file was: its output feeds pipelines run under pipefail.
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

# copy_plugins DEST WHO EXCLUDE_ROOT: the plug-in families of WHO into
# DEST/bin/Plugins, but a family EXCLUDE_ROOT (the runtime) already has.
# Prints the families taken.
copy_plugins() {
    local dest="$1" who="$2" exclude="$3" family
    while IFS= read -r family; do
        [ -n "$family" ] || continue
        if [ ! -d "$SOURCE/bin/Plugins/$family" ]; then
            echo "Error: the suite has no bin/Plugins/$family" >&2
            exit 1
        fi
        [ -n "$exclude" ] && [ -d "$exclude/bin/Plugins/$family" ] && continue
        mkdir -p "$dest/bin/Plugins"
        cp -a "$SOURCE/bin/Plugins/$family" "$dest/bin/Plugins/$family"
        printf '%s\n' "$family"
    done < <(plugin_families "$SOURCE/bin/Plugins" "$who")
}

# copy_modules DEST: the modules the libraries in DEST/lib open at run time,
# which ldd cannot see - libvips' loaders and libheif's codec plug-ins
# (HEIC, AVIF, JPEG XL), ImageMagick's coders behind libvips' magick loader
# - with their layout, where the launcher points libvips (VIPSHOME), libheif
# (LIBHEIF_PLUGIN_PATH) and ImageMagick (MAGICK_*) at them, when the library
# that opens them is in DEST/lib; then their own closure.
copy_modules() {
    local dest="$1" d rel
    local dirs=() modules=()
    has_lib() { compgen -G "$dest/lib/$1" > /dev/null; }
    if has_lib 'libvips.so*'; then
        for d in "$SOURCE"/lib/*/vips-modules-*; do
            [ -d "$d" ] || continue
            rel="${d#"$SOURCE/"}"
            mkdir -p "$dest/$(dirname "$rel")"
            cp -a "$d" "$dest/$rel"
            dirs+=("$dest/$rel")
        done
    fi
    if has_lib 'libheif.so*' && [ -d "$SOURCE/lib/libheif/plugins" ]; then
        mkdir -p "$dest/lib/libheif"
        cp -a "$SOURCE/lib/libheif/plugins" "$dest/lib/libheif/"
        dirs+=("$dest/lib/libheif/plugins")
    fi
    if [ "${#dirs[@]}" -gt 0 ]; then
        mapfile -t modules < <(elf_files "${dirs[@]}")
        collect false "$dest/lib" "" "${modules[@]}"
    fi
    if has_lib 'libMagickCore*'; then
        for d in "$SOURCE"/lib/ImageMagick-*; do
            [ -d "$d" ] || continue
            cp -a "$d" "$dest/lib/"
            mapfile -t modules < <(elf_files "$dest/lib/$(basename "$d")")
            collect false "$dest/lib" "" "${modules[@]}"
        done
        if [ -d "$SOURCE/etc/ImageMagick-6" ]; then
            mkdir -p "$dest/etc"
            cp -a "$SOURCE/etc/ImageMagick-6" "$dest/etc/"
        fi
    fi
}

# stage_linux_runtime DEST: the runtime's layout in DEST. Prints its roots
# (the core library and the plug-ins), for the check from inside.
stage_linux_runtime() {
    local dest="$1" core f
    mkdir -p "$dest/lib" "$dest/share"
    if ! compgen -G "$SOURCE/lib/libUltraCanvas.so*" > /dev/null; then
        echo "Error: $SOURCE/lib has no libUltraCanvas.so - is it a shared build's package (package-linux.sh)?" >&2
        exit 1
    fi
    # The core under every name the suite has for it (the soname's link too).
    cp -a "$SOURCE"/lib/libUltraCanvas.so* "$dest/lib/"
    core=""
    for f in "$dest"/lib/libUltraCanvas.so*; do
        [ -L "$f" ] || { core="$f"; break; }
    done
    copy_plugins "$dest" runtime "" > "$STAGE/.runtime-plugins"
    COLLECTED=0
    mapfile -t RUNTIME_ROOTS < <(printf '%s\n' "$core"; elf_files "$dest/bin/Plugins")
    collect true "$dest/lib" "" "${RUNTIME_ROOTS[@]}"
    copy_modules "$dest"
    copy_media "$SOURCE/share/media" "$dest/share/media"
}

# The launcher of an app: lib/ on the library path, then the module folders
# of libvips, libheif and ImageMagick from the app's own folder or, on a
# runtime, from the runtime's (ULTRACANVAS_RUNTIME_DIR, which the store sets).
write_launcher() {
    local dest="$1"
    cat > "$dest/$APP" <<WRAP
#!/bin/sh
# $APP as UltraStore installs it (scripts/package-store-app.sh): lib/ on the
# library path, after which the store's own entries - the runtime's lib/ when
# the app runs on one - stay; the run-time module folders from the app's
# folder, or from the runtime's (ULTRACANVAS_RUNTIME_DIR).
HERE="\$(dirname "\$(readlink -f "\$0")")"
export LD_LIBRARY_PATH="\$HERE/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
for R in "\$HERE" "\${ULTRACANVAS_RUNTIME_DIR:-}"; do
    [ -n "\$R" ] && [ -d "\$R/lib" ] || continue
    if [ -z "\${VIPSHOME:-}" ] && ls -d "\$R"/lib/*/vips-modules-* >/dev/null 2>&1; then
        export VIPSHOME="\$R"
    fi
    if [ -z "\${LIBHEIF_PLUGIN_PATH:-}" ] && [ -d "\$R/lib/libheif/plugins" ]; then
        export LIBHEIF_PLUGIN_PATH="\$R/lib/libheif/plugins"
    fi
    IM_LIB="\$(ls -d "\$R"/lib/ImageMagick-* 2>/dev/null | head -1)"
    if [ -z "\${MAGICK_HOME:-}" ] && [ -n "\$IM_LIB" ]; then
        export MAGICK_HOME="\$R"
        export MAGICK_CONFIGURE_PATH="\$R/etc/ImageMagick-6:\$IM_LIB/config-Q16"
        export MAGICK_CODER_MODULE_PATH="\$IM_LIB/modules-Q16/coders"
        export MAGICK_FILTER_MODULE_PATH="\$IM_LIB/modules-Q16/filters"
    fi
done
# UltraFIBU's chart of accounts and tax keys, where the package has them.
if [ -d "\$HERE/share/UltraFIBU/data" ]; then
    export ULTRAFIBU_DATA_DIR="\$HERE/share/UltraFIBU/data"
fi
exec "\$HERE/bin/$APP" "\$@"
WRAP
    chmod 755 "$dest/$APP"
}

if [ "$OS" = "linux" ]; then
    [ -n "$SOURCE" ] || SOURCE="dist/UltraCanvas-Linux-$FW_VERSION-$ARCH"
    if [ ! -d "$SOURCE/lib" ]; then
        echo "Error: $SOURCE is not a suite package - run package-linux.sh first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"

    if [ "$MODE" = "runtime" ]; then
        echo "Collecting the runtime..."
        stage_linux_runtime "$PKG"
        echo "  Plug-ins: $(tr '\n' ' ' < "$STAGE/.runtime-plugins")"
        echo "  $COLLECTED libraries beside the core"
        ROOTS=("${RUNTIME_ROOTS[@]}")
        CHECK_PATH="$PKG/lib"
    else
        if [ ! -x "$SOURCE/bin/$APP" ]; then
            echo "Error: $SOURCE/bin/$APP is not there - run package-linux.sh first (or pass --source)" >&2
            exit 1
        fi
        EXCLUDE=""
        if [ "$MODE" = "on-runtime" ]; then
            echo "Staging $RUNTIME_ID $FW_VERSION from the same suite, to leave out what it carries..."
            mkdir -p "$RT"
            stage_linux_runtime "$RT" > /dev/null
            EXCLUDE="$RT"
        fi
        mkdir -p "$PKG/bin" "$PKG/lib"
        cp "$SOURCE/bin/$APP" "$PKG/bin/$APP"
        chmod 755 "$PKG/bin/$APP"
        write_launcher "$PKG"
        mapfile -t PLUGIN_LIST < <(copy_plugins "$PKG" "$APP" "$EXCLUDE")
        echo "  Plug-ins: ${PLUGIN_LIST[*]:-none}${EXCLUDE:+ (and those of the runtime)}"

        echo "Collecting libraries..."
        COLLECTED=0
        mapfile -t ROOTS < <(printf '%s\n' "$PKG/bin/$APP"; elf_files "$PKG/bin/Plugins")
        collect true "$PKG/lib" "${EXCLUDE:+$EXCLUDE/lib}" "${ROOTS[@]}"
        # The run-time modules of the libraries in the app's own lib/: all of
        # them for an app on its own, those the runtime does not carry for
        # one on it.
        copy_modules "$PKG"
        if [ -z "$EXCLUDE" ]; then
            echo "  $COLLECTED libraries from the suite's lib/"
            mkdir -p "$PKG/share"
            copy_media "$SOURCE/share/media" "$PKG/share/media"
        else
            echo "  $COLLECTED libraries the runtime does not carry"
        fi
        while IFS= read -r own; do
            [ -n "$own" ] || continue
            mkdir -p "$PKG/share"
            cp -R "$SOURCE/share/$own" "$PKG/share/$own"
            echo "  The application's own data: share/$own"
        done < <(own_data_folder "$SOURCE/share")
        CHECK_PATH="$PKG/lib${EXCLUDE:+:$EXCLUDE/lib}"
        ENTRY="$APP/$APP"
        RUN=("$PKG/$APP")
        if [ -n "$EXCLUDE" ]; then
            # As the store starts it (PlanLaunch): the runtime's lib/ on the
            # path and its folder named.
            RUN_LIBRARY_PATH="LD_LIBRARY_PATH=$EXCLUDE/lib"
            RUN_RUNTIME_DIR="$EXCLUDE"
        fi
    fi

    # The oldest glibc the package runs on: the newest GLIBC_ version any of
    # its files asks for - the catalog's variant (glibc-<x.y>).
    GLIBC=$(elf_files "$PKG" | while IFS= read -r f; do
                { readelf -V --wide "$f" 2>/dev/null || true; } | { grep -o 'GLIBC_[0-9][0-9.]*' || true; }
            done | sed 's/^GLIBC_//' | sort -V -u | tail -1)
    if [ -n "$GLIBC" ]; then
        VARIANT="glibc-$(printf '%s' "$GLIBC" | cut -d. -f1-2)"
    fi

    # The proof: with only the package's lib/ on the path (and the runtime's,
    # for an app on it) the loader finds every library of the executable and
    # the plug-ins that it found in the suite.
    echo "Checking the package from inside..."
    missing=0
    for file in "${ROOTS[@]}"; do
        listing=$(ldd_listing "$CHECK_PATH" "$file")
        if grep -q 'not found' <<< "$listing"; then
            echo "  ERROR: $(basename "$file") does not load from the package:" >&2
            grep 'not found' <<< "$listing" >&2
            missing=$((missing + 1))
        fi
    done
    if [ "$missing" -gt 0 ]; then
        exit 1
    fi
    echo "  every library of ${#ROOTS[@]} file(s) resolves inside the package${EXCLUDE:+ and the runtime}"
    FORMAT="tar.xz"

# ════════════════════════════════════════════════════════════════════════════
# Windows: out of package-win.sh's dist/
# ════════════════════════════════════════════════════════════════════════════
elif [ "$OS" = "windows" ]; then
    [ -n "$SOURCE" ] || SOURCE="dist"
    if [ ! -d "$SOURCE" ]; then
        echo "Error: $SOURCE is not there - run package-win.sh first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"
    PYTHON="$(command -v python3 || command -v python || true)"
    if [ -z "$PYTHON" ]; then
        echo "Error: python3 is needed to read the import tables (scripts/pe_imports.py)" >&2
        exit 1
    fi
    # MSYS2's mingw Python is a Windows program: give it paths it reads.
    topath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }
    # The import tables, read statically by scripts/pe_imports.py as
    # package-ultracanvasstart.sh reads them (ldd would load every module):
    # the closure over dist/ of every DLL an import names that dist/ carries,
    # and theirs in turn.
    closure_into() {
        local dest="$1"; shift
        local root
        for root in "$@"; do
            "$PYTHON" "$(topath "$SCRIPT_DIR/pe_imports.py")" \
                --closure "$(topath "$root")" \
                --from "$(topath "$SOURCE")" --into "$(topath "$dest")" > /dev/null
        done
    }
    # copy_win_plugins DEST WHO EXCLUDE_ROOT: as on Linux, under Plugins/.
    # Prints the plug-in DLLs (not TDLib's: those sit beside tdjson.dll under
    # their hashed names and are loaded from there).
    copy_win_plugins() {
        local dest="$1" who="$2" exclude="$3" family dll
        while IFS= read -r family; do
            [ -n "$family" ] || continue
            if [ ! -d "$SOURCE/Plugins/$family" ]; then
                echo "Error: the suite has no Plugins/$family" >&2
                exit 1
            fi
            [ -n "$exclude" ] && [ -d "$exclude/Plugins/$family" ] && continue
            mkdir -p "$dest/Plugins"
            cp -R "$SOURCE/Plugins/$family" "$dest/Plugins/$family"
            for dll in "$dest/Plugins/$family"/*.dll; do
                [ -f "$dll" ] && printf '%s\n' "$dll"
            done
        done < <(plugin_families "$SOURCE/Plugins" "$who")
        return 0
    }
    stage_windows_runtime() {
        local dest="$1" core="" c
        for c in libUltraCanvas.dll UltraCanvas.dll; do
            [ -f "$SOURCE/$c" ] && { core="$c"; break; }
        done
        if [ -z "$core" ]; then
            echo "Error: $SOURCE has no libUltraCanvas.dll - is it package-win.sh's dist/?" >&2
            exit 1
        fi
        cp "$SOURCE/$core" "$dest/$core"
        mapfile -t RUNTIME_ROOTS < <(printf '%s\n' "$dest/$core"; copy_win_plugins "$dest" runtime "")
        closure_into "$dest" "${RUNTIME_ROOTS[@]}"
        copy_media "$SOURCE/Resources/media" "$dest/Resources/media"
    }

    if [ "$MODE" = "runtime" ]; then
        echo "Collecting the runtime..."
        stage_windows_runtime "$PKG"
    else
        if [ ! -f "$SOURCE/$APP.exe" ]; then
            echo "Error: $SOURCE/$APP.exe is not there - run package-win.sh first (or pass --source)" >&2
            exit 1
        fi
        EXCLUDE=""
        if [ "$MODE" = "on-runtime" ]; then
            echo "Staging $RUNTIME_ID $FW_VERSION from the same suite, to leave out what it carries..."
            mkdir -p "$RT"
            stage_windows_runtime "$RT"
            EXCLUDE="$RT"
        fi
        cp "$SOURCE/$APP.exe" "$PKG/$APP.exe"
        # The console companion, where the application has one
        # (cmake/UltraCanvasWindowsGuiApp.cmake).
        if [ -f "$SOURCE/$APP.com" ]; then cp "$SOURCE/$APP.com" "$PKG/$APP.com"; fi
        mapfile -t ROOTS < <(printf '%s\n' "$PKG/$APP.exe"; copy_win_plugins "$PKG" "$APP" "$EXCLUDE")
        echo "Collecting DLLs..."
        closure_into "$PKG" "${ROOTS[@]}"
        if [ -n "$EXCLUDE" ]; then
            # What the runtime carries goes: the loader finds it there, on the
            # PATH the store sets (PlanLaunch).
            for dll in "$PKG"/*.dll; do
                [ -f "$dll" ] && [ -f "$EXCLUDE/$(basename "$dll")" ] && rm -f "$dll"
            done
        else
            copy_media "$SOURCE/Resources/media" "$PKG/Resources/media"
        fi
        # The CA bundle the bundled libcurl verifies HTTPS against: beside the
        # executable, where UltraNet looks for it.
        if [ -f "$SOURCE/cacert.pem" ]; then
            cp "$SOURCE/cacert.pem" "$PKG/cacert.pem"
        else
            echo "  Warning: no cacert.pem in $SOURCE - HTTPS would fail its TLS check"
        fi
        # UltraFIBU's chart of accounts and tax keys: data/ beside the executable.
        if [ "$(printf '%s' "$APP" | tr '[:upper:]' '[:lower:]')" = "ultrafibu" ] && [ -d "$SOURCE/data" ]; then
            cp -R "$SOURCE/data" "$PKG/data"
            echo "  The application's own data: data/"
        fi
        # The startup troubleshooting launchers (Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md).
        for f in uc-diagnose.bat uc-diagnose.ps1; do
            if [ -f "$SOURCE/$f" ]; then cp "$SOURCE/$f" "$PKG/$f"; fi
        done
        ENTRY="$APP/$APP.exe"
        RUN=("$PKG/$APP.exe")
        if [ -n "$EXCLUDE" ]; then
            RUN_LIBRARY_PATH="PATH=$EXCLUDE:$PATH"
            RUN_RUNTIME_DIR="$(topath "$EXCLUDE")"
        fi
    fi
    count=$(find "$PKG" -maxdepth 1 -iname '*.dll' | wc -l | tr -d ' ')
    echo "  $count DLLs from $SOURCE${EXCLUDE:+ that the runtime does not carry}"
    if [ "$count" -eq 0 ] && [ "$MODE" != "on-runtime" ]; then
        echo "Error: no DLL found in $SOURCE - the import tables were not read" >&2
        exit 1
    fi
    FORMAT="zip"

# ════════════════════════════════════════════════════════════════════════════
# macOS: the bundle package-macos.sh --store-app cut out of the suite
# ════════════════════════════════════════════════════════════════════════════
else
    [ -n "$SOURCE" ] || SOURCE="dist-store-macos"
    if [ ! -d "$SOURCE/$APP.app" ]; then
        echo "Error: $SOURCE/$APP.app is not there - run ./package-macos.sh --store-app $APP first (or pass --source)" >&2
        exit 1
    fi
    SOURCE="$(cd "$SOURCE" && pwd)"
    # ditto keeps what codesign sealed (the symbolic links, the extended
    # attributes); cp -R would too, but the archive below is ditto's.
    ditto "$SOURCE/$APP.app" "$PKG/$APP.app"
    if ! codesign --verify --deep --strict "$PKG/$APP.app" 2>/dev/null; then
        echo "Error: $APP.app does not verify (codesign --verify --deep --strict) - package-macos.sh signs it" >&2
        exit 1
    fi
    MIN_OS=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$PKG/$APP.app/Contents/Info.plist" 2>/dev/null || true)
    ENTRY="$APP.app"
    RUN=("$PKG/$APP.app/Contents/MacOS/$APP")
    FORMAT="zip"
fi

# ── An application that answers --version runs from inside the package ─────
# Only one whose executable carries the option's name: another would open its
# window. A run that fails because a library is missing fails the package -
# the loader's message on Linux and macOS; on Windows the status of a process
# Windows would not start (0xC0000135 a DLL not found, 0xC000007B a wrong
# image, of which the shell keeps the low byte, and 127). Any other failure -
# no display on a build machine, an option the program reads differently -
# is a warning: the closure check above stands either way.
if [ -n "$ENTRY" ]; then
    EXE="$PKG/bin/$APP"
    [ "$OS" = "windows" ] && EXE="$PKG/$APP.exe"
    [ "$OS" = "macos" ] && EXE="${RUN[0]}"
    if ! command -v timeout > /dev/null 2>&1; then
        # Without a time limit a program that reads the option otherwise
        # would sit with its window open (macOS has no timeout of its own).
        echo "  (no timeout command here: the packaged application is not run)"
    elif grep -aq -- '--version' "$EXE"; then
        echo "Running the packaged application${RUN_RUNTIME_DIR:+ on the staged runtime}..."
        # The environment the store would add, where it adds any.
        launch=(env)
        if [ -n "$RUN_RUNTIME_DIR" ]; then
            launch+=("$RUN_LIBRARY_PATH" "ULTRACANVAS_RUNTIME_DIR=$RUN_RUNTIME_DIR")
        fi
        launch+=(timeout 30)
        status=0
        out=$("${launch[@]}" "${RUN[@]}" --version 2>&1 < /dev/null) || status=$?
        load_failure=false
        case "$OS" in
            linux) grep -q 'error while loading shared libraries' <<< "$out" && load_failure=true ;;
            macos) grep -q 'Library not loaded' <<< "$out" && load_failure=true ;;
            *)     case "$status" in 53|123|127) load_failure=true ;; esac ;;
        esac
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
fi

# ── The archive ─────────────────────────────────────────────────────────────

ARCHIVE="$OUTPUT/$ARCHIVE_BASE.$FORMAT"
rm -f "$ARCHIVE"
if [ "$FORMAT" = "tar.xz" ]; then
    # xz, as for UltraCanvasStart: the package is mostly shared libraries,
    # which xz packs about a quarter smaller than gzip. tar keeps the
    # execute bits of the launcher, the executable and the plug-ins; the
    # store's installer restores them (a zip made on Windows has none).
    if [ "$MODE" = "runtime" ]; then
        # The top-level names, not "." - no ./ in front of every entry.
        (cd "$PKG" && XZ_OPT="-T0" tar -cJf "$ARCHIVE" -- *)
    else
        XZ_OPT="-T0" tar -C "$STAGE" -cJf "$ARCHIVE" "$APP"
    fi
elif [ "$OS" = "macos" ]; then
    ditto -c -k --keepParent "$PKG/$APP.app" "$ARCHIVE"
elif [ "$MODE" = "runtime" ]; then
    (cd "$PKG" && zip -q -r "$ARCHIVE" -- *)
else
    (cd "$STAGE" && zip -q -r "$ARCHIVE" "$APP")
fi

SHA256=$( (sha256sum "$ARCHIVE" 2>/dev/null || shasum -a 256 "$ARCHIVE") | cut -d' ' -f1)
unpacked=$(du -sk "$PKG" | cut -f1)
packed=$(du -sk "$ARCHIVE" | cut -f1)
mb() { awk -v k="$1" 'BEGIN { printf "%.1f MB", k / 1024 }'; }
rm -rf "$STAGE"

RELEASE="ultrastoreadmin release add $STORE_ID $PKG_VERSION"
[ "$MODE" = "on-runtime" ] && RELEASE+=" --depends $RUNTIME_ID@$FW_VERSION"
ADD="ultrastoreadmin package add $STORE_ID $PKG_VERSION $ARCHIVE --os $OS --arch $ARCH"
[ -n "$ENTRY" ] && ADD+=" --entry $ENTRY"
[ -n "$VARIANT" ] && ADD+=" --variant $VARIANT"
[ -n "$MIN_OS" ] && ADD+=" --min-os $MIN_OS"

echo "=== Package complete ==="
echo "  Archive:  $ARCHIVE ($(mb "$packed"), $(mb "$unpacked") unpacked)"
echo "  SHA-256:  $SHA256"
[ -n "$ENTRY" ] && echo "  Entry:    $ENTRY"
[ -n "$VARIANT" ] && echo "  Variant:  $VARIANT"
[ -n "$MIN_OS" ] && echo "  Min. OS:  $MIN_OS"
echo ""
echo "Into the admin backend:"
if [ "$MODE" = "runtime" ]; then
    echo "  ultrastoreadmin app new $STORE_ID \"UltraCanvas runtime\" --kind runtime --publisher ultra-os-project   # once"
fi
echo "  $RELEASE"
echo "  $ADD"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### $APP for the store ($OS $ARCH)"
        echo ""
        echo "| Item | Size |"
        echo "|---|---:|"
        echo "| **$(basename "$ARCHIVE")** | $(mb "$packed") |"
        echo "| unpacked | $(mb "$unpacked") |"
        echo ""
        echo "\`$RELEASE\`"
        echo ""
        echo "\`$ADD\`"
    } >> "$GITHUB_STEP_SUMMARY"
fi
