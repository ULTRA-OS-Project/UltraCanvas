#!/bin/bash
# package-linux.sh - Create a portable, self-contained Linux distribution that
# bundles every UltraCanvas app together with a shared lib/ directory holding all
# dependent .so files. Unlike an AppImage (one app per bundle) this ships all the
# apps in a single relocatable tree, so the heavy dependency set is paid for once.
#
# Resulting layout:
#   UltraCanvas-Linux-<ver>-<arch>/            <- arch: x86_64 or arm64
#     UltraCanvasDemo  UltraCanvasTexter  UltraFiler ...   <- wrapper scripts
#     bin/                                                 <- real ELF executables
#     lib/                                                 <- libUltraCanvas.so + all deps
#         ImageMagick-<ver>/                               <- ImageMagick coder modules
#     etc/ImageMagick-6/                                   <- ImageMagick config
#     share/{media,Docs,DemoApp}/                          <- runtime resources
#
# Usage: ./package-linux.sh
# Env overrides:
#   BUILDDIR=<dir>   build tree to package (default: cmake-build-release)
#   OUTPUTDIR=<dir>  where the package/tarball is written (default: dist)
#   SKIP_BUILD=1     package an existing build without reconfiguring/rebuilding
#   ARCH=<name>      architecture label in the package name (default: derived
#                    from `uname -m`, x86_64 or arm64)

set -euo pipefail

PROJECTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDDIR="${BUILDDIR:-$PROJECTDIR/cmake-build-release}"
OUTPUTDIR="${OUTPUTDIR:-$PROJECTDIR/dist}"
SKIP_BUILD="${SKIP_BUILD:-0}"

# Architecture label for the package name, and the Debian multiarch triplet the
# host's libraries live under. Both are derived from the running machine: this
# packager bundles the host's own .so files, so it only ever produces a native
# package (no cross-packaging). The labels match the ones the CI matrix uses for
# the macOS/Windows arm64 artifacts.
case "$(uname -m)" in
    x86_64|amd64)   ARCH="${ARCH:-x86_64}" ;;
    aarch64|arm64)  ARCH="${ARCH:-arm64}"  ;;
    *)              ARCH="${ARCH:-$(uname -m)}" ;;
esac
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null \
    || gcc -print-multiarch 2>/dev/null \
    || echo "$(uname -m)-linux-gnu")"

# Apps to include (executable target names, output to the build root).
APPS=(UltraCanvasDemo UltraCanvasTexter UltraFiler UltraMail UltraAIApp UltraViewer)

# Shared libraries that must come from the host, NOT be bundled: the glibc/loader
# core, and the GPU/GL/driver + display stack that has to match the running system.
# libstdc++ / libgcc_s ARE bundled on purpose so the package runs on older distros.
EXCLUDE_PATTERNS=(
    'ld-linux' 'libc.so' 'libm.so' 'libdl.so' 'libpthread.so' 'librt.so'
    'libresolv.so' 'libutil.so' 'libnsl.so' 'libnss_' 'libpthread' 'libmvec.so'
    'libanl.so' 'libBrokenLocale.so' 'libcrypt.so' 'libthread_db'
    'libGL.so' 'libGLX' 'libGLdispatch' 'libEGL' 'libGLESv1' 'libGLESv2'
    'libOpenGL' 'libglapi' 'libgbm' 'libdrm'
    'libX11' 'libxcb' 'libX11-xcb' 'libwayland'
)

# --- helpers -----------------------------------------------------------------

log() { printf '%s\n' "$*"; }

is_excluded() {
    local name="$1" pat
    for pat in "${EXCLUDE_PATTERNS[@]}"; do
        [[ "$name" == "$pat"* ]] && return 0
    done
    return 1
}

# --- version -----------------------------------------------------------------

VERSION=$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+\.[0-9]+\.[0-9]+)\*.*/\1/p' \
    "$PROJECTDIR/Docs/UltraCanvas/CHANGELOG.md")
if [ -z "$VERSION" ]; then
    echo "Error: could not parse version from Docs/UltraCanvas/CHANGELOG.md" >&2
    exit 1
fi
log "=== UltraCanvas portable Linux packager (v$VERSION, $ARCH) ==="

# --- build (shared) ----------------------------------------------------------

if [ "$SKIP_BUILD" != "1" ]; then
    log "Configuring + building (shared) into $BUILDDIR ..."
    cmake -S "$PROJECTDIR" -B "$BUILDDIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DULTRACANVAS_BUILD_SHARED=ON
    cmake --build "$BUILDDIR" -j"$(nproc)"
else
    log "SKIP_BUILD=1 -> packaging existing build in $BUILDDIR"
fi

# --- skeleton ----------------------------------------------------------------

PKGNAME="UltraCanvas-Linux-$VERSION-$ARCH"
PKG="$OUTPUTDIR/$PKGNAME"
rm -rf "$PKG"
mkdir -p "$PKG"/{bin,lib,etc,share}

# --- executables -------------------------------------------------------------

FOUND_APPS=()
for app in "${APPS[@]}"; do
    if [ -x "$BUILDDIR/$app" ]; then
        cp "$BUILDDIR/$app" "$PKG/bin/"
        FOUND_APPS+=("$app")
        log "  exe  $app"
    else
        log "  skip $app (not built)"
    fi
done
if [ "${#FOUND_APPS[@]}" -eq 0 ]; then
    echo "Error: no executables found in $BUILDDIR" >&2
    exit 1
fi

# --- UltraCanvas + plugin libs ----------------------------------------------

if compgen -G "$BUILDDIR/lib/*.so*" > /dev/null; then
    cp -a "$BUILDDIR"/lib/*.so* "$PKG/lib/"
fi

# --- collect dependent .so (transitive closure) ------------------------------

log "Collecting dependent shared libraries ..."
declare -A SKIPPED
copy_deps_once() {
    local added=0 target dep resolved base
    while IFS= read -r -d '' target; do
        # ldd lines:  libfoo.so.1 => /path/libfoo.so.1 (0x..)
        while read -r base _arrow resolved _addr; do
            [ "$_arrow" = "=>" ] || continue
            [ -f "$resolved" ] || continue
            if is_excluded "$base"; then
                SKIPPED["$base"]=1
                continue
            fi
            [ -e "$PKG/lib/$base" ] && continue
            cp -L "$resolved" "$PKG/lib/$base"
            log "    + $base"
            added=1
        done < <(ldd "$target" 2>/dev/null)
    done < <(find "$PKG/bin" "$PKG/lib" -type f \( -name '*.so*' -o -perm -u+x \) -print0)
    # return 0 (success -> keep looping) if we added a new lib this pass
    [ "$added" -eq 1 ]
}
# Repeat until a full pass adds nothing new.
while copy_deps_once; do :; done

if [ "${#SKIPPED[@]}" -gt 0 ]; then
    log "  (left to host: ${!SKIPPED[*]})"
fi

# --- ImageMagick delegate (used by libvips) ----------------------------------

log "Bundling ImageMagick delegate ..."
IM_LIBDIR=$(ls -d "/usr/lib/$MULTIARCH"/ImageMagick-* 2>/dev/null | head -1 || true)
if [ -n "$IM_LIBDIR" ]; then
    cp -a "/usr/lib/$MULTIARCH"/libMagickCore-*.so* "$PKG/lib/" 2>/dev/null || true
    cp -a "/usr/lib/$MULTIARCH"/libMagickWand-*.so* "$PKG/lib/" 2>/dev/null || true
    cp -a "$IM_LIBDIR" "$PKG/lib/"
    log "  modules: $(basename "$IM_LIBDIR")"
fi
if [ -d /etc/ImageMagick-6 ]; then
    cp -a /etc/ImageMagick-6 "$PKG/etc/"
fi

# --- wrapper launcher scripts ------------------------------------------------

log "Writing wrapper launchers ..."
for app in "${FOUND_APPS[@]}"; do
    cat > "$PKG/$app" <<'WRAP'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
IM_LIB="$(ls -d "$HERE"/lib/ImageMagick-* 2>/dev/null | head -1)"
if [ -n "$IM_LIB" ]; then
    export MAGICK_HOME="$HERE"
    export MAGICK_CONFIGURE_PATH="$HERE/etc/ImageMagick-6:$IM_LIB/config-Q16"
    export MAGICK_CODER_MODULE_PATH="$IM_LIB/modules-Q16/coders"
    export MAGICK_FILTER_MODULE_PATH="$IM_LIB/modules-Q16/filters"
fi
exec "$HERE/bin/__APP__" "$@"
WRAP
    sed -i "s/__APP__/$app/" "$PKG/$app"
    chmod +x "$PKG/$app"
done

# --- resources ---------------------------------------------------------------

log "Copying resources ..."
mkdir -p "$PKG/share"
cp -r "$PROJECTDIR/media" "$PKG/share/media"
cp -r "$PROJECTDIR/Docs"  "$PKG/share/Docs"
mkdir -p "$PKG/share/DemoApp"
cp "$PROJECTDIR"/Apps/DemoApp/*.cpp "$PKG/share/DemoApp/" 2>/dev/null || true

# --- archive -----------------------------------------------------------------

log "Creating tarball ..."
tar -C "$OUTPUTDIR" -czf "$OUTPUTDIR/$PKGNAME.tar.gz" "$PKGNAME"

TOTAL_SIZE=$(du -sh "$PKG" | cut -f1)
log ""
log "=== Package complete ==="
log "  Apps:     ${FOUND_APPS[*]}"
log "  Size:     $TOTAL_SIZE"
log "  Dir:      $PKG"
log "  Tarball:  $OUTPUTDIR/$PKGNAME.tar.gz"
