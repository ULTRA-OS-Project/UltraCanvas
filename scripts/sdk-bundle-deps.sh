#!/usr/bin/env bash
# scripts/sdk-bundle-deps.sh <sdk-prefix>
#
# Adds the development files of the libraries the framework uses to an
# UltraCanvas SDK install prefix (Docs/UltraCanvasSDK.md), on Windows (MSYS2)
# and macOS (Homebrew), where those trees are relocatable:
#
#   <sdk>/deps/include/...        the headers (cairo, pango, glib, vips, ...)
#   <sdk>/deps/lib/...            import libraries (.dll.a) / dylibs, static
#                                 archives, lib/pkgconfig/*.pc, lib/cmake/*
#   <sdk>/deps/share/pkgconfig    the .pc files some packages keep there
#   <sdk>/deps/bin/*.dll          Windows: the DLLs the core and those
#                                 libraries need at run time
#
# Which libraries: the pkg-config closure (Requires and Requires.private) of
# the modules UltraCanvasConfig.cmake re-finds on the consuming machine, plus
# what the exported targets find through CMake (fmt, libcurl, zlib). Each
# .pc file in the closure names the package that owns it (pacman -Qo on
# MSYS2, the Cellar keg on Homebrew) and that package's include/ and lib/
# are copied as they are.
#
# The .pc files are made relocatable: `prefix=${pcfiledir}/../..`, which
# pkgconf and pkg-config 0.29+ both expand to the directory two levels above
# the .pc file - deps/ - and every absolute reference to the build machine's
# prefix becomes ${prefix}. UltraCanvasConfig.cmake puts deps/ on
# CMAKE_PREFIX_PATH, which FindPkgConfig turns into deps/lib/pkgconfig and
# deps/share/pkgconfig on PKG_CONFIG_PATH, so a consumer needs nothing else.
#
# macOS: the bundled dylibs get @rpath install names and reference each other
# by @rpath, and UltraCanvasConfig.cmake links consumers with an rpath to
# deps/lib, so an application built against the SDK runs on a Mac without
# Homebrew. Linux is left alone: the distribution's -dev packages are the
# right source there, and the script says so and exits 0.
#
# Under set -e a while loop whose last body command is a failed `test && x`
# ends the script, so the loops here use if/then and the piped ones carry
# `|| true`; cp and sed report their own failures.
#
# Run from the repository root after `cmake --install build --prefix <sdk>`;
# CI's "Install and build against the CMake package" step does this and then
# builds Tests/PackageConsumer against the bundled files alone
# (PKG_CONFIG_LIBDIR pointed at nothing) to prove they are enough.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
set -eu

SDK="${1:-}"
if [ -z "$SDK" ] || [ ! -f "$SDK/lib/cmake/UltraCanvas/UltraCanvasConfig.cmake" ]; then
    echo "usage: $0 <sdk-prefix>   (a prefix written by cmake --install)" >&2
    exit 2
fi
SDK="$(cd "$SDK" && pwd)"
DEPS="$SDK/deps"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PLATFORM=windows ;;
    Darwin)               PLATFORM=macos ;;
    *)
        echo "sdk-bundle-deps: nothing to bundle on $(uname -s); the distribution's -dev packages are the source there"
        exit 0 ;;
esac

PKG_CONFIG="${PKG_CONFIG:-$(command -v pkg-config || command -v pkgconf || true)}"
if [ -z "$PKG_CONFIG" ]; then
    echo "sdk-bundle-deps: pkg-config is needed to find the libraries" >&2
    exit 1
fi

# macOS: the libraries come from the vcpkg prefix CI builds for the oldest
# supported macOS (scripts/macos-deps.sh, exported as UC_MACOS_DEPS_PREFIX),
# or from Homebrew when that is not set.
MAC_DEPS=""
if [ "$PLATFORM" = macos ] && [ -n "${UC_MACOS_DEPS_PREFIX:-}" ]; then
    MAC_DEPS="$(cd "$UC_MACOS_DEPS_PREFIX" && pwd -P)"
    # Only the prefix's own .pc files, as the build saw them.
    export PKG_CONFIG_LIBDIR="$MAC_DEPS/lib/pkgconfig:$MAC_DEPS/share/pkgconfig"
    export PKG_CONFIG_PATH=""
elif [ "$PLATFORM" = macos ]; then
    # Homebrew keeps a keg-only formula's .pc file out of the shared
    # lib/pkgconfig (libarchive, for one, which vips.pc requires), so the
    # closure below would miss it. Every installed formula has an opt/<name>
    # link; put each one's lib/pkgconfig on the search path for the queries
    # that follow.
    _brew_prefix="$(brew --prefix)"
    _extra="$(ls -d "$_brew_prefix"/opt/*/lib/pkgconfig 2>/dev/null | tr '\n' ':')"
    export PKG_CONFIG_PATH="${_extra}${PKG_CONFIG_PATH:-}"
fi

# ---- the modules and their closure ------------------------------------------
# What UltraCanvasConfig.cmake asks pkg-config for, plus what the exported
# targets link by name and what find_dependency() looks for.
MODULES="cairo pango pangocairo freetype2 harfbuzz glib-2.0 gobject-2.0 gio-2.0 tinyxml2"
for optional in vips-cpp fmt libcurl zlib libpng x11 xcursor gl; do
    # The vcpkg prefix's libcurl is tesseract's; the framework links Apple's
    # (cmake/UltraCanvasMacOSDeps.cmake), so a consumer must find that one.
    [ -n "$MAC_DEPS" ] && [ "$optional" = libcurl ] && continue
    if "$PKG_CONFIG" --exists "$optional" 2>/dev/null; then
        MODULES="$MODULES $optional"
    fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
: > "$WORK/closure"
: > "$WORK/queue"
for m in $MODULES; do echo "$m" >> "$WORK/queue"; done
while [ -s "$WORK/queue" ]; do
    m="$(head -1 "$WORK/queue")"
    sed -i.bak '1d' "$WORK/queue" && rm -f "$WORK/queue.bak"
    if grep -qx "$m" "$WORK/closure"; then continue; fi
    if ! "$PKG_CONFIG" --exists "$m" 2>/dev/null; then
        echo "  (no .pc for $m - skipped)"
        continue
    fi
    echo "$m" >> "$WORK/closure"
    # Requires lines read "name >= 1.2, other"; the first word of each
    # comma-separated item is the module name. Homebrew's vips.pc names its
    # keg-only libarchive by the full path of its .pc file
    # (/opt/homebrew/opt/libarchive/lib/pkgconfig/libarchive.pc); that is
    # the module "libarchive", found through the opt/ directories above.
    { "$PKG_CONFIG" --print-requires "$m"; "$PKG_CONFIG" --print-requires-private "$m"; } 2>/dev/null \
        | tr ',' '\n' | awk '{print $1}' | sed -e 's|.*/||' -e 's|\.pc$||' | grep -v '^$' >> "$WORK/queue" || true
done
echo "pkg-config closure: $(wc -l < "$WORK/closure" | tr -d ' ') modules"

# The .pc file of each module, as a real path.
realpath_of() {
    python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1" 2>/dev/null || echo "$1"
}
: > "$WORK/pcfiles"
while read -r m; do
    dir="$("$PKG_CONFIG" --variable=pcfiledir "$m" 2>/dev/null || true)"
    [ -n "$dir" ] && [ -f "$dir/$m.pc" ] && realpath_of "$dir/$m.pc" >> "$WORK/pcfiles"
done < "$WORK/closure"

mkdir -p "$DEPS/include" "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig"

copy_rel() {
    # copy_rel <root> <file under root>: the file at the same relative path under deps/
    local root="$1" file="$2" rel dest
    rel="${file#"$root"/}"
    dest="$DEPS/$rel"
    mkdir -p "$(dirname "$dest")"
    if [ -L "$file" ]; then
        cp -R "$file" "$dest" 2>/dev/null || cp -L "$file" "$dest"
    else
        cp "$file" "$dest"
    fi
}

# Is this path one of the development files worth carrying?
wanted_rel() {
    case "$1" in
        include/*) return 0 ;;
        lib/pkgconfig/*|share/pkgconfig/*|lib/cmake/*) return 0 ;;
        lib/python*|lib/girepository-1.0/*) return 1 ;;
        lib/*/include/*) return 0 ;;    # glibconfig.h and its kind live in lib/<pkg>/include
        lib/*.a|lib/*.dll.a|lib/*.dylib|lib/*.la) return 0 ;;
        lib/*/*.a|lib/*/*.dylib) return 0 ;;
        share/*/*.cmake) [ -n "$MAC_DEPS" ] ;;   # vcpkg keeps CMake configs in share/<port>/
        bin/*.dll|lib/*/*.dll) [ "$PLATFORM" = windows ] ;;
        *) return 1 ;;
    esac
}

# ---- Windows (MSYS2) ---------------------------------------------------------
if [ "$PLATFORM" = windows ]; then
    PREFIX="${MINGW_PREFIX:-/clang64}"
    : > "$WORK/owners"
    while read -r pc; do
        # MSYS2's pkgconf is a native program and reports native paths
        # (C:/msys64/clang64/...); pacman wants the POSIX spelling.
        case "$pc" in /*) ;; *) command -v cygpath >/dev/null 2>&1 && pc="$(cygpath -u "$pc")" ;; esac
        pacman -Qoq "$pc" 2>/dev/null >> "$WORK/owners" || echo "  (no package owns $pc)"
    done < "$WORK/pcfiles"
    sort -u "$WORK/owners" -o "$WORK/owners"
    echo "packages: $(tr '\n' ' ' < "$WORK/owners")"
    while read -r pkg; do
        pacman -Qlq "$pkg" | while read -r f; do
            case "$f" in "$PREFIX"/*) ;; *) continue ;; esac
            [ -f "$f" ] || [ -L "$f" ] || continue
            rel="${f#"$PREFIX"/}"
            if wanted_rel "$rel"; then copy_rel "$PREFIX" "$f"; fi
        done || true
    done < "$WORK/owners"

    # The DLLs the core needs at run time, by walking the PE import tables
    # (the way package-win.sh does; objdump reads without loading).
    if command -v objdump >/dev/null 2>&1; then OBJDUMP=objdump;
    elif command -v llvm-objdump >/dev/null 2>&1; then OBJDUMP=llvm-objdump;
    else OBJDUMP=""; fi
    if [ -n "$OBJDUMP" ]; then
        mkdir -p "$DEPS/bin"
        : > "$WORK/dllqueue"
        for f in "$SDK"/bin/*.dll "$DEPS"/bin/*.dll; do [ -f "$f" ] && echo "$f" >> "$WORK/dllqueue"; done
        while [ -s "$WORK/dllqueue" ]; do
            f="$(head -1 "$WORK/dllqueue")"
            sed -i.bak '1d' "$WORK/dllqueue" && rm -f "$WORK/dllqueue.bak"
            "$OBJDUMP" -p "$f" 2>/dev/null | sed -n 's/.*DLL Name:[[:space:]]*//p' | while read -r name; do
                if [ -f "$PREFIX/bin/$name" ] && [ ! -f "$DEPS/bin/$name" ]; then
                    cp "$PREFIX/bin/$name" "$DEPS/bin/$name"
                    echo "$DEPS/bin/$name" >> "$WORK/dllqueue"
                fi
            done || true
        done
    fi

    # Relocate the .pc files: prefix from the file's own place, no /clang64.
    find "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig" -name '*.pc' -type f | while read -r pc; do
        sed -i.bak -e 's|^prefix=.*|prefix=${pcfiledir}/../..|' -e "s|$PREFIX|\${prefix}|g" "$pc"
        rm -f "$pc.bak"
    done
fi

# ---- macOS (vcpkg) -----------------------------------------------------------
# vcpkg lists each package's files in <install root>/vcpkg/info/
# <port>_<version>_<triplet>.list, as "<triplet>/<path>": the package that owns
# a .pc file is the list naming it, and its files are the ones to carry. Its
# .pc files are already relocatable and its dylibs already have @rpath
# install names; the steps shared with Homebrew below leave both as they are.
if [ "$PLATFORM" = macos ] && [ -n "$MAC_DEPS" ]; then
    INFO="$(dirname "$MAC_DEPS")/vcpkg/info"
    TRIPLET="$(basename "$MAC_DEPS")"
    : > "$WORK/lists"
    while read -r pc; do
        rel="${pc#"$MAC_DEPS"/}"
        list="$(grep -lx "$TRIPLET/$rel" "$INFO"/*.list 2>/dev/null | head -1 || true)"
        if [ -n "$list" ]; then echo "$list" >> "$WORK/lists"; else echo "  (no vcpkg package owns $pc)"; fi
    done < "$WORK/pcfiles"
    sort -u "$WORK/lists" -o "$WORK/lists"
    echo "packages: $(sed -e 's|.*/||' -e 's|_.*||' "$WORK/lists" | tr '\n' ' ')"
    while read -r list; do
        while read -r entry; do
            rel="${entry#"$TRIPLET"/}"
            f="$MAC_DEPS/$rel"
            [ -f "$f" ] || [ -L "$f" ] || continue
            if wanted_rel "$rel"; then copy_rel "$MAC_DEPS" "$f"; fi
        done < "$list"
    done < "$WORK/lists"
    MAC_LIBROOT="$MAC_DEPS"
fi

# ---- macOS (Homebrew) --------------------------------------------------------
if [ "$PLATFORM" = macos ] && [ -z "$MAC_DEPS" ]; then
    HOMEBREW_PREFIX="$(brew --prefix)"
    CELLAR="$(brew --cellar)"
    MAC_LIBROOT="$HOMEBREW_PREFIX"
    : > "$WORK/kegs"
    while read -r pc; do
        case "$pc" in
            "$CELLAR"/*)
                rest="${pc#"$CELLAR"/}"
                formula="${rest%%/*}"; rest="${rest#*/}"; version="${rest%%/*}"
                echo "$CELLAR/$formula/$version" >> "$WORK/kegs" ;;
            *) echo "  ($pc is not in the Cellar - skipped)" ;;
        esac
    done < "$WORK/pcfiles"
    sort -u "$WORK/kegs" -o "$WORK/kegs"
    echo "kegs: $(sed "s|$CELLAR/||" "$WORK/kegs" | tr '\n' ' ')"
    while read -r keg; do
        find "$keg/include" "$keg/lib" "$keg/share/pkgconfig" \( -type f -o -type l \) 2>/dev/null | while read -r f; do
            rel="${f#"$keg"/}"
            if wanted_rel "$rel"; then copy_rel "$keg" "$f"; fi
        done || true
    done < "$WORK/kegs"
fi

# ---- macOS (both) ------------------------------------------------------------
if [ "$PLATFORM" = macos ]; then
    # The dylibs those reference, so the set in deps/lib is closed; then
    # @rpath install names, so a consumer linked with -rpath deps/lib runs
    # without Homebrew or the vcpkg prefix. install_name_tool invalidates the
    # signature, so each file is re-signed ad hoc, as package-macos.sh does
    # for a bundle.
    : > "$WORK/dyqueue"
    find "$DEPS/lib" -name '*.dylib' -type f >> "$WORK/dyqueue"
    while [ -s "$WORK/dyqueue" ]; do
        f="$(head -1 "$WORK/dyqueue")"
        sed -i '' '1d' "$WORK/dyqueue"
        otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | while read -r dep; do
            case "$dep" in
                /System/*|/usr/lib/*|@executable_path/*|@loader_path/*) continue ;;
                @rpath/*) src="$MAC_LIBROOT/lib/${dep#@rpath/}" ;;
                "$MAC_LIBROOT"/*|/usr/local/*|/opt/homebrew/*) src="$dep" ;;
                *) continue ;;
            esac
            name="$(basename "$dep")"
            if [ ! -f "$DEPS/lib/$name" ] && [ -f "$src" ]; then
                cp -L "$src" "$DEPS/lib/$name"
                chmod 644 "$DEPS/lib/$name"
                echo "$DEPS/lib/$name" >> "$WORK/dyqueue"
            fi
        done || true
    done
    find "$DEPS/lib" -name '*.dylib' -type f | while read -r f; do
        chmod u+w "$f"
        install_name_tool -id "@rpath/$(basename "$f")" "$f" 2>/dev/null || true
        otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | while read -r dep; do
            name="$(basename "$dep")"
            case "$dep" in @rpath/*|/System/*|/usr/lib/*) continue ;; esac
            if [ -f "$DEPS/lib/$name" ]; then
                install_name_tool -change "$dep" "@rpath/$name" "$f" 2>/dev/null || true
            fi
        done || true
        codesign --force --sign - "$f" 2>/dev/null || true
    done

fi

# vcpkg's .pc files say prefix=${pcfiledir}/../.. already; any absolute path
# into the prefix that slipped through becomes ${prefix} too.
if [ "$PLATFORM" = macos ] && [ -n "$MAC_DEPS" ]; then
    find "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig" -name '*.pc' -type f | while read -r pc; do
        sed -i '' -e 's|^prefix=.*|prefix=${pcfiledir}/../..|' -e "s|$MAC_DEPS|\${prefix}|g" "$pc"
    done
fi

if [ "$PLATFORM" = macos ] && [ -z "$MAC_DEPS" ]; then
    # Relocate the .pc files: the keg prefixes, the opt/ links and the
    # Homebrew prefix itself all become ${prefix}.
    find "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig" -name '*.pc' -type f | while read -r pc; do
        sed -i '' -e 's|^prefix=.*|prefix=${pcfiledir}/../..|' "$pc"
        while read -r keg; do
            formula="$(basename "$(dirname "$keg")")"
            sed -i '' -e "s|$keg|\${prefix}|g" -e "s|$HOMEBREW_PREFIX/opt/$formula|\${prefix}|g" "$pc"
        done < "$WORK/kegs"
        sed -i '' -e "s|$CELLAR/[^/]*/[^/]*|\${prefix}|g" -e "s|$HOMEBREW_PREFIX/opt/[^/]*|\${prefix}|g" \
                  -e "s|$HOMEBREW_PREFIX|\${prefix}|g" "$pc"
    done

    # The libraries macOS itself provides (zlib, bzip2, expat, libffi,
    # libxml2, libarchive, libcurl) have no keg: Homebrew's pkg-config finds
    # them through stub .pc files it keeps per macOS version, which point at
    # the system SDK. vips.pc requires libarchive, so a consumer looking only
    # at deps/ needs those stubs too; they are copied as they are (no prefix
    # of ours to relocate) and only where no bundled .pc has the name.
    # The stubs live in Homebrew's own repository: the prefix on Apple
    # silicon (/opt/homebrew), but /usr/local/Homebrew on Intel, where the
    # prefix is /usr/local. brew --repository names it on both.
    STUBS="$(brew --repository)/Library/Homebrew/os/mac/pkgconfig"
    if [ -d "$STUBS" ]; then
        stubdir="$STUBS/$(sw_vers -productVersion 2>/dev/null | cut -d. -f1)"
        [ -d "$stubdir" ] || stubdir="$(ls -d "$STUBS"/*/ 2>/dev/null | sort -V | tail -1)"
        for pc in "$stubdir"/*.pc; do
            [ -f "$pc" ] || continue
            if [ ! -f "$DEPS/lib/pkgconfig/$(basename "$pc")" ]; then
                cp "$pc" "$DEPS/lib/pkgconfig/"
            fi
        done
        echo "  system-library stubs from $stubdir"
    fi
fi

# A package's CMake config may name a program next to its library
# (MSYS2's CURLConfig.cmake imports CURL::curl as bin/curl.exe), and CMake
# refuses the whole config when one referenced file is missing. Carry the
# programs the bundled configs name, and nothing else from bin/.
if [ -d "$DEPS/lib/cmake" ]; then
    grep -rhoE '\$\{_IMPORT_PREFIX\}/bin/[^"]+' "$DEPS/lib/cmake" 2>/dev/null | sort -u | while read -r ref; do
        name="${ref#*/bin/}"
        for root in "${PREFIX:-}" "${CELLAR:-}"; do
            [ -n "$root" ] || continue
            if [ "$PLATFORM" = macos ]; then
                src="$(ls -d "$root"/*/*/bin/"$name" 2>/dev/null | head -1 || true)"
            else
                src="$root/bin/$name"
            fi
            if [ -n "$src" ] && [ -f "$src" ] && [ ! -f "$DEPS/bin/$name" ]; then
                mkdir -p "$DEPS/bin"
                cp "$src" "$DEPS/bin/$name"
            fi
        done
    done || true
fi

echo "bundled into $DEPS:"
echo "  headers:   $(find "$DEPS/include" -type f | wc -l | tr -d ' ') files"
echo "  libraries: $(find "$DEPS/lib" -maxdepth 1 \( -name '*.a' -o -name '*.dylib' \) | wc -l | tr -d ' ')"
echo "  .pc files: $(find "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig" -name '*.pc' | wc -l | tr -d ' ')"
[ -d "$DEPS/bin" ] && echo "  DLLs:      $(find "$DEPS/bin" -name '*.dll' | wc -l | tr -d ' ')"
du -sh "$DEPS" | awk '{print "  size:      " $1}'
