#!/bin/bash
# package-win.sh - Build and create a standalone Windows distribution with all required DLLs
# Run from MSYS2 MinGW64 shell after building the project
#
# Usage:
#   ./package-win.sh [--no-sign] [package-name.zip]
#     --no-sign   Skip Authenticode signing of the EXEs
#
set -e

DO_SIGN=true
PACKAGE_ZIP=""
while [ $# -gt 0 ]; do
    case "$1" in
        --no-sign) DO_SIGN=false; shift ;;
        *)         PACKAGE_ZIP="$1"; shift ;;
    esac
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
# MSYS2 environment root: /mingw64 (Intel, MINGW64) or /clangarm64
# (ARM64, CLANGARM64). MSYSTEM_PREFIX is set by every MSYS2 login shell.
MSYS_PREFIX="${MSYSTEM_PREFIX:-/mingw64}"
MINGW_BIN="$MSYS_PREFIX/bin"
DIST_DIR="dist"

# Architecture label for the package name (aarch64 → arm64 to match the
# artifact naming used on the other platforms).
ARCH="${MSYSTEM_CARCH:-x86_64}"
[ "$ARCH" = "aarch64" ] && ARCH="arm64"

if [ -z "$PACKAGE_ZIP" ]; then
  PACKAGE_ZIP="UCDemo-Windows-$VERSION-$ARCH.zip"
fi

# Create output directory
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# Copy the executables (and any co-located plugin/import DLLs). Executables now
# output to the build root (CMAKE_RUNTIME_OUTPUT_DIRECTORY), not build/bin.
cp ./build/*.exe "$DIST_DIR/"
cp ./build/*.dll "$DIST_DIR/" 2>/dev/null || true
echo "Copied EXE"

if $DO_SIGN; then
    powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode Sign -ExePath dist/UltraCanvasTexter.exe
    powershell -ExecutionPolicy Bypass -File SignUltraDemo.ps1 -Mode Sign -ExePath dist/UltraCanvasDemo.exe
else
    echo "Skipping code signing (--no-sign)"
fi

# Collect all required DLLs using ldd, filtering to only MSYS2/MinGW DLLs
echo ""
echo "Collecting DLLs..."
DLL_COUNT=0

# MSYS2's ldd resolves dependencies by actually loading the module, running
# DllMain of everything it pulls in. Some DLLs (seen with ImageMagick coder
# modules on ARM64) deadlock in that init path and hang ldd forever — bound
# every call so a bad module is skipped instead of wedging the build.
LDD="timeout -k 5 15 ldd"

for EXE_PATH in $DIST_DIR/*.exe ; do
  $LDD "$EXE_PATH" | grep -i "$MSYS_PREFIX/" | awk '{print $3}' | sort -u | while read -r dll; do
      if [ -f "$dll" ]; then
          cp "$dll" "$DIST_DIR/"
          echo "  $(basename "$dll")"
          DLL_COUNT=$((DLL_COUNT + 1))
      fi
  done
done

# Copy libvips modules (only the ones we need and can bundle)
VIPS_MODULE_DIR=$(ls -d $MSYS_PREFIX/lib/vips-modules-* 2>/dev/null | head -1)
if [ -d "$VIPS_MODULE_DIR" ]; then
    VIPS_MOD_DEST="$DIST_DIR/lib/$(basename "$VIPS_MODULE_DIR")"
    mkdir -p "$VIPS_MOD_DEST"
    # vips-heif: HEIF/AVIF support, vips-magick: ImageMagick bridge (BMP, etc.)
    for mod in vips-heif.dll vips-magick.dll; do
        [ -f "$VIPS_MODULE_DIR/$mod" ] && cp "$VIPS_MODULE_DIR/$mod" "$VIPS_MOD_DEST/"
    done
    echo "  Copied vips modules: vips-heif, vips-magick"
fi

# Copy libheif codec plugins (AOM for AVIF, x265 for HEIC, etc.)
if [ -d "$MSYS_PREFIX/lib/libheif" ]; then
    mkdir -p "$DIST_DIR/lib/libheif"
    cp $MSYS_PREFIX/lib/libheif/*.dll "$DIST_DIR/lib/libheif/" 2>/dev/null || true
    echo "  Copied libheif codec plugins"
fi

# Copy ImageMagick coder modules and config
cp $MSYS_PREFIX/bin/libMagickCore*.dll "$DIST_DIR/" 2>/dev/null

IM_LIB_DIR=$(ls -d $MSYS_PREFIX/lib/ImageMagick-* 2>/dev/null | head -1)
if [ -d "$IM_LIB_DIR" ]; then
    IM_BASENAME=$(basename "$IM_LIB_DIR")
    CODERS_DEST="$DIST_DIR/lib/$IM_BASENAME/modules-Q16HDRI/coders"
    mkdir -p "$CODERS_DEST"
    # Copy ALL coder modules (dll + la), not just BMP. libvips has no native
    # loader for several legacy raster formats (TGA, PSD, PCX, ICO, ...) and
    # falls back to ImageMagick's magickload for them; the supported-format
    # inventory advertises those formats whenever magickload is present. If a
    # coder module is missing, ImageMagick fails with
    # "NoDecodeDelegateForThisImageFormat" (e.g. .tga would not open at all).
    # Shipping every coder keeps what the demo advertises in sync with what it
    # can actually decode.
    cp "$IM_LIB_DIR/modules-Q16HDRI/coders/"*.dll "$CODERS_DEST/" 2>/dev/null || true
    cp "$IM_LIB_DIR/modules-Q16HDRI/coders/"*.la  "$CODERS_DEST/" 2>/dev/null || true
    if [ -d "$IM_LIB_DIR/config-Q16HDRI" ]; then
        cp -r "$IM_LIB_DIR/config-Q16HDRI" "$DIST_DIR/lib/$IM_BASENAME/"
    fi
    CODER_COUNT=$(find "$CODERS_DEST" -name '*.dll' | wc -l)
    echo "  Copied $CODER_COUNT ImageMagick coder modules from $IM_BASENAME"
fi
# Copy ImageMagick configuration XMLs
IM_ETC_DIR=$(ls -d $MSYS_PREFIX/etc/ImageMagick-* 2>/dev/null | head -1)
if [ -d "$IM_ETC_DIR" ]; then
    mkdir -p "$DIST_DIR/etc/$(basename "$IM_ETC_DIR")"
    cp "$IM_ETC_DIR"/*.xml "$DIST_DIR/etc/$(basename "$IM_ETC_DIR")/"
    echo "  Copied ImageMagick config XMLs"
fi

# Also collect DLLs needed by the DLLs themselves (transitive deps)
# Run ldd on ALL copied DLLs including those in subdirectories (vips modules, IM coders)
find "$DIST_DIR" -name '*.dll' | while read -r dll; do
    $LDD "$dll" 2>/dev/null | grep -i "$MSYS_PREFIX/" | awk '{print $3}' | sort -u | while read -r dep; do
        dep_name=$(basename "$dep")
        if [ -f "$dep" ] && [ ! -f "$DIST_DIR/$dep_name" ]; then
            cp "$dep" "$DIST_DIR/"
            echo "  $dep_name (transitive from $(basename "$dll"))"
        fi
    done
done

# Copy GLib schema files if they exist (needed by some GTK/GLib apps)
#if [ -d "$MSYS_PREFIX/share/glib-2.0/schemas" ]; then
#    mkdir -p "$DIST_DIR/share/glib-2.0/schemas"
#    cp $MSYS_PREFIX/share/glib-2.0/schemas/gschemas.compiled "$DIST_DIR/share/glib-2.0/schemas/" 2>/dev/null || true
#fi

# Copy GDK-Pixbuf loaders for image format support (used by libvips)
# PIXBUF_DIR="$MSYS_PREFIX/lib/gdk-pixbuf-2.0"
# if [ -d "$PIXBUF_DIR" ]; then
#     PIXBUF_VER=$(ls "$PIXBUF_DIR" | head -1)
#     if [ -n "$PIXBUF_VER" ]; then
#         mkdir -p "$DIST_DIR/lib/gdk-pixbuf-2.0/$PIXBUF_VER/loaders"
#         cp "$PIXBUF_DIR/$PIXBUF_VER/loaders/"*.dll "$DIST_DIR/lib/gdk-pixbuf-2.0/$PIXBUF_VER/loaders/" 2>/dev/null || true
#         cp "$PIXBUF_DIR/$PIXBUF_VER/loaders.cache" "$DIST_DIR/lib/gdk-pixbuf-2.0/$PIXBUF_VER/" 2>/dev/null || true
#         echo "  Copied gdk-pixbuf loaders"
#     fi
# fi
mkdir -p $DIST_DIR/Resources/DemoApp
cp -r media Docs $DIST_DIR/Resources
# Demo "View Source" loads files from Resources/DemoApp/ (see GetResourcesDir()
# + the "DemoApp/..." paths registered in UltraCanvasDemo.cpp).
cp -r Apps/DemoApp/*.cpp $DIST_DIR/Resources/DemoApp/

cd $DIST_DIR
zip -r ../$PACKAGE_ZIP *
cd ..
echo ""
TOTAL_DLLS=$(find "$DIST_DIR" -maxdepth 1 -name '*.dll' | wc -l)
echo "=== Package complete ==="
echo "  Files: $EXE_NAME + $TOTAL_DLLS DLLs"
echo "  Location: $DIST_DIR/"
