#!/usr/bin/env bash
# scripts/homebrew-rebuild-newer-kegs.sh FORMULA...
#
# Finds the Homebrew libraries, among the formulae named and their runtime
# dependencies, that declare a newer minimum macOS than
# MACOSX_DEPLOYMENT_TARGET; rebuilds those formulae from source on this
# machine; checks again, and fails naming whatever is still too new.
#
# Why: package-macos.sh bundles these libraries into the apps, and dyld refuses
# to load one built for a newer macOS than the running system, so an app runs
# only where its newest library allows. package-macos.sh checks that too, but
# only at the end of a 20-minute build; this runs right after `brew install`.
# A bottle normally declares the major version it was built for - 15.0 for a
# Sequoia bottle - but not always: on 2026-10-04 the arm64 Sequoia bottle of
# tesseract declared 15.7.5, the exact system it was built on, so every app
# linking the OCR plug-in would have refused to start on macOS 15.0 to 15.7.4.
# A source build gets Homebrew's own deployment target, the major version of
# the machine building it - which is what MACOSX_DEPLOYMENT_TARGET names in CI,
# where each macOS leg's runner is the oldest macOS it supports. A formula
# whose Homebrew source build cannot run gets a recipe of its own below
# (rebuild_<formula>), which builds the library for MACOSX_DEPLOYMENT_TARGET
# and installs it over the keg.
#
# CI runs it after installing the dependencies; MACOSX_DEPLOYMENT_TARGET must
# be set.

set -euo pipefail

target="${MACOSX_DEPLOYMENT_TARGET:-}"
if [ -z "$target" ] || [ $# -eq 0 ]; then
    echo "usage: MACOSX_DEPLOYMENT_TARGET=<version> $0 FORMULA..." >&2
    exit 2
fi

# macho_min_macos and version_newer
. "$(dirname "$0")/macos-min-version.sh"

prefix=$(brew --prefix)

# Recipes for formulae `brew reinstall --build-from-source` cannot rebuild.
# Each installs over the keg, keeping its paths and install names, so
# pkg-config, the build and package-macos.sh see the same files as before.

# Homebrew's tesseract formula also fetches a third-party snum.traineddata
# whose URL answered 404 on 2026-10-04, so its source build fails before
# compiling anything. The apps need only the library: build the keg's upstream
# release with the formula's own autotools steps (without the training tools
# and language data, which the bottle already installed and the apps do not
# load from there).
rebuild_tesseract() {
    local version keg src
    version=$(brew list --versions tesseract | awk '{print $2}')
    version="${version%%_*}"   # 5.5.3_1 -> 5.5.3: a revision is Homebrew's own
    keg=$(brew --prefix tesseract)
    src=$(mktemp -d)
    brew install autoconf automake libtool
    echo "Building tesseract $version from source into $keg"
    curl -fsSL --retry 3 \
        "https://github.com/tesseract-ocr/tesseract/archive/refs/tags/$version.tar.gz" \
        | tar -xz -C "$src" --strip-components 1
    (
        cd "$src"
        ./autogen.sh
        # libarchive is keg-only in Homebrew, so pkg-config needs its path to
        # link the same libarchive the bottle did.
        PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
            ./configure --prefix="$keg" --datarootdir="$prefix/share" \
                --disable-debug --disable-dependency-tracking --disable-silent-rules
        make -j"$(sysctl -n hw.ncpu)"
        # A poured bottle leaves some of its files read-only.
        chmod -R u+w "$keg/"
        make install
    )
    rm -rf "$src"
}

# The formulae named and every runtime dependency of any of them (--union:
# without it, brew deps prints only what they all have in common).
formulae=$({ printf '%s\n' "$@"; brew deps --formula --union "$@"; } | sed 's#.*/##' | sort -u)

# too_new_in FORMULA - print "<minimum> <path>" for each library in FORMULA's
# keg that needs a newer macOS than the target. The trailing slash makes find
# enter lib/ through the opt/ symlink; -type f then skips the unversioned
# symlinks (libfoo.dylib -> libfoo.5.dylib) so each file is read once.
too_new_in() {
    local lib="$prefix/opt/$1/lib/" f v
    [ -d "$lib" ] || return 0
    while IFS= read -r -d '' f; do
        case "$(file -b "$f")" in
            Mach-O*) ;;
            *) continue ;;
        esac
        v=$(macho_min_macos "$f")
        if [ -n "$v" ] && version_newer "$v" "$target"; then
            echo "$v ${f#"$prefix"/}"
        fi
    done < <(find "$lib" -type f \( -name '*.dylib' -o -name '*.so' \) -print0)
}

# How the keg got here, from its install receipt - a poured bottle or a source
# build, and the macOS it was built on where the receipt says.
keg_origin() {
    python3 - "$prefix/opt/$1/INSTALL_RECEIPT.json" <<'PY' 2>/dev/null || echo "no install receipt"
import json, sys
r = json.load(open(sys.argv[1]))
built_on = (r.get("built_on") or {}).get("os_version") or "an unrecorded macOS"
how = "poured from a bottle" if r.get("poured_from_bottle") else "built from source"
print(f"{how}, built on {built_on}")
PY
}

# Sets $stale to the formulae with a library that is too new, and lists them.
scan() {
    stale=""
    local f found
    for f in $formulae; do
        found=$(too_new_in "$f")
        [ -n "$found" ] || continue
        stale="$stale $f"
        echo "  $f ($(keg_origin "$f")):"
        printf '%s\n' "$found" | sed 's/^/    needs macOS /'
    done
}

count=$(printf '%s\n' "$formulae" | wc -l | tr -d ' ')
echo "Homebrew libraries needing a newer macOS than $target, among $count formulae:"
scan
if [ -z "$stale" ]; then
    echo "  none"
    exit 0
fi

echo "Rebuilding from source for macOS $target:$stale"
for f in $stale; do
    recipe="rebuild_${f//[^A-Za-z0-9_]/_}"
    if declare -F "$recipe" >/dev/null; then
        "$recipe"
    else
        brew reinstall --build-from-source "$f"
    fi
done

echo "After the rebuild:"
scan
if [ -n "$stale" ]; then
    msg="still built for a newer macOS than $target after a source build:$stale"
    [ -n "${GITHUB_ACTIONS:-}" ] && echo "::error::$msg"
    echo "ERROR: $msg"
    echo "Their build ignores the deployment target, so it has to be built here"
    echo "with one, or MACOSX_DEPLOYMENT_TARGET raised (.github/workflows/build.yml)."
    exit 1
fi
echo "  none"
