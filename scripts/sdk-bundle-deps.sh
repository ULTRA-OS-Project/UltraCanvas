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

# ---- the modules and their closure ------------------------------------------
# What UltraCanvasConfig.cmake asks pkg-config for, plus what the exported
# targets link by name and what find_dependency() looks for.
MODULES="cairo pango pangocairo freetype2 harfbuzz glib-2.0 gobject-2.0 gio-2.0 tinyxml2"
for optional in vips-cpp fmt libcurl zlib libpng x11 xcursor gl; do
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
    grep -qx "$m" "$WORK/closure" && continue
    if ! "$PKG_CONFIG" --exists "$m" 2>/dev/null; then
        echo "  (no .pc for $m - skipped)"
        continue
    fi
    echo "$m" >> "$WORK/closure"
    # Requires lines read "name >= 1.2, other"; the first word of each
    # comma-separated item is the module name.
    { "$PKG_CONFIG" --print-requires "$m"; "$PKG_CONFIG" --print-requires-private "$m"; } 2>/dev/null \
        | tr ',' '\n' | awk '{print $1}' | grep -v '^$' >> "$WORK/queue" || true
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
        lib/python*|lib/girepository-1.0/*|lib/*/include/*) return 1 ;;
        lib/*.a|lib/*.dll.a|lib/*.dylib|lib/*.la) return 0 ;;
        lib/*/*.a|lib/*/*.dylib) return 0 ;;
        bin/*.dll) [ "$PLATFORM" = windows ] ;;
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
            wanted_rel "$rel" && copy_rel "$PREFIX" "$f"
        done
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
                [ -f "$PREFIX/bin/$name" ] || continue
                [ -f "$DEPS/bin/$name" ] && continue
                cp "$PREFIX/bin/$name" "$DEPS/bin/$name"
                echo "$DEPS/bin/$name" >> "$WORK/dllqueue"
            done
        done
    fi

    # Relocate the .pc files: prefix from the file's own place, no /clang64.
    find "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig" -name '*.pc' -type f | while read -r pc; do
        sed -i.bak -e 's|^prefix=.*|prefix=${pcfiledir}/../..|' -e "s|$PREFIX|\${prefix}|g" "$pc"
        rm -f "$pc.bak"
    done
fi

# ---- macOS (Homebrew) --------------------------------------------------------
if [ "$PLATFORM" = macos ]; then
    HOMEBREW_PREFIX="$(brew --prefix)"
    CELLAR="$(brew --cellar)"
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
            wanted_rel "$rel" && copy_rel "$keg" "$f"
        done
    done < "$WORK/kegs"

    # The dylibs those reference, so the set in deps/lib is closed; then
    # @rpath install names, so a consumer linked with -rpath deps/lib runs
    # without Homebrew. install_name_tool invalidates the signature, so each
    # file is re-signed ad hoc, as package-macos.sh does for a bundle.
    : > "$WORK/dyqueue"
    find "$DEPS/lib" -name '*.dylib' -type f >> "$WORK/dyqueue"
    while [ -s "$WORK/dyqueue" ]; do
        f="$(head -1 "$WORK/dyqueue")"
        sed -i '' '1d' "$WORK/dyqueue"
        otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | while read -r dep; do
            case "$dep" in
                /System/*|/usr/lib/*|@executable_path/*|@loader_path/*) continue ;;
                @rpath/*) src="$HOMEBREW_PREFIX/lib/${dep#@rpath/}" ;;
                "$HOMEBREW_PREFIX"/*|/usr/local/*|/opt/homebrew/*) src="$dep" ;;
                *) continue ;;
            esac
            name="$(basename "$dep")"
            [ -f "$DEPS/lib/$name" ] && continue
            [ -f "$src" ] || continue
            cp -L "$src" "$DEPS/lib/$name"
            chmod 644 "$DEPS/lib/$name"
            echo "$DEPS/lib/$name" >> "$WORK/dyqueue"
        done
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
        done
        codesign --force --sign - "$f" 2>/dev/null || true
    done

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
fi

echo "bundled into $DEPS:"
echo "  headers:   $(find "$DEPS/include" -type f | wc -l | tr -d ' ') files"
echo "  libraries: $(find "$DEPS/lib" -maxdepth 1 \( -name '*.a' -o -name '*.dylib' \) | wc -l | tr -d ' ')"
echo "  .pc files: $(find "$DEPS/lib/pkgconfig" "$DEPS/share/pkgconfig" -name '*.pc' | wc -l | tr -d ' ')"
[ -d "$DEPS/bin" ] && echo "  DLLs:      $(find "$DEPS/bin" -name '*.dll' | wc -l | tr -d ' ')"
du -sh "$DEPS" | awk '{print "  size:      " $1}'
