#!/bin/bash
# package-win.sh - Build and create a standalone Windows distribution with all required DLLs
# Run from MSYS2 MinGW64 shell after building the project
#
set -e

VERSION=`date +%Y.%m.%d`
MINGW_BIN="/mingw64/bin"
DIST_DIR="dist"
PACKAGE_ZIP="$1"

if [ "$1" == "" ]; then
  PACKAGE_ZIP="UCDemo-$VERSION.zip"
fi

# Create output directory
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# Copy the executables
cp ./build/bin/UltraCanvas*.exe "$DIST_DIR/"
echo "Copied EXE"

powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode Sign -ExePath dist/UltraCanvasTexter.exe
powershell -ExecutionPolicy Bypass -File SignUltraDemo.ps1 -Mode Sign -ExePath dist/UltraCanvasDemo.exe

# Collect all required DLLs using ldd, filtering to only MSYS2/MinGW DLLs
echo ""
echo "Collecting DLLs..."
DLL_COUNT=0

for EXE_PATH in $DIST_DIR/*.exe ; do
  ldd "$EXE_PATH" | grep -i '/mingw64/' | awk '{print $3}' | sort -u | while read -r dll; do
      if [ -f "$dll" ]; then
          cp "$dll" "$DIST_DIR/"
          echo "  $(basename "$dll")"
          DLL_COUNT=$((DLL_COUNT + 1))
      fi
  done
done

# Copy libvips modules (only the ones we need and can bundle)
VIPS_MODULE_DIR=$(ls -d /mingw64/lib/vips-modules-* 2>/dev/null | head -1)
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
if [ -d "/mingw64/lib/libheif" ]; then
    mkdir -p "$DIST_DIR/lib/libheif"
    cp /mingw64/lib/libheif/*.dll "$DIST_DIR/lib/libheif/" 2>/dev/null || true
    echo "  Copied libheif codec plugins"
fi

# Copy ImageMagick BMP coder module and config
cp /mingw64/bin/libMagickCore*.dll "$DIST_DIR/" 2>/dev/null

IM_LIB_DIR=$(ls -d /mingw64/lib/ImageMagick-* 2>/dev/null | head -1)
if [ -d "$IM_LIB_DIR" ]; then
    IM_BASENAME=$(basename "$IM_LIB_DIR")
    CODERS_DEST="$DIST_DIR/lib/$IM_BASENAME/modules-Q16HDRI/coders"
    mkdir -p "$CODERS_DEST"
    # Only copy the BMP coder (dll + la)
    cp "$IM_LIB_DIR/modules-Q16HDRI/coders/bmp.dll" "$CODERS_DEST/" 2>/dev/null || true
    cp "$IM_LIB_DIR/modules-Q16HDRI/coders/bmp.la"  "$CODERS_DEST/" 2>/dev/null || true
    if [ -d "$IM_LIB_DIR/config-Q16HDRI" ]; then
        cp -r "$IM_LIB_DIR/config-Q16HDRI" "$DIST_DIR/lib/$IM_BASENAME/"
    fi
    echo "  Copied ImageMagick BMP coder from $IM_BASENAME"
fi
# Copy ImageMagick configuration XMLs
IM_ETC_DIR=$(ls -d /mingw64/etc/ImageMagick-* 2>/dev/null | head -1)
if [ -d "$IM_ETC_DIR" ]; then
    mkdir -p "$DIST_DIR/etc/$(basename "$IM_ETC_DIR")"
    cp "$IM_ETC_DIR"/*.xml "$DIST_DIR/etc/$(basename "$IM_ETC_DIR")/"
    echo "  Copied ImageMagick config XMLs"
fi

# Also collect DLLs needed by the DLLs themselves (transitive deps)
# Run ldd on ALL copied DLLs including those in subdirectories (vips modules, IM coders)
find "$DIST_DIR" -name '*.dll' | while read -r dll; do
    ldd "$dll" 2>/dev/null | grep -i '/mingw64/' | awk '{print $3}' | sort -u | while read -r dep; do
        dep_name=$(basename "$dep")
        if [ -f "$dep" ] && [ ! -f "$DIST_DIR/$dep_name" ]; then
            cp "$dep" "$DIST_DIR/"
            echo "  $dep_name (transitive from $(basename "$dll"))"
        fi
    done
done

# Copy GLib schema files if they exist (needed by some GTK/GLib apps)
#if [ -d "/mingw64/share/glib-2.0/schemas" ]; then
#    mkdir -p "$DIST_DIR/share/glib-2.0/schemas"
#    cp /mingw64/share/glib-2.0/schemas/gschemas.compiled "$DIST_DIR/share/glib-2.0/schemas/" 2>/dev/null || true
#fi

# Copy GDK-Pixbuf loaders for image format support (used by libvips)
# PIXBUF_DIR="/mingw64/lib/gdk-pixbuf-2.0"
# if [ -d "$PIXBUF_DIR" ]; then
#     PIXBUF_VER=$(ls "$PIXBUF_DIR" | head -1)
#     if [ -n "$PIXBUF_VER" ]; then
#         mkdir -p "$DIST_DIR/lib/gdk-pixbuf-2.0/$PIXBUF_VER/loaders"
#         cp "$PIXBUF_DIR/$PIXBUF_VER/loaders/"*.dll "$DIST_DIR/lib/gdk-pixbuf-2.0/$PIXBUF_VER/loaders/" 2>/dev/null || true
#         cp "$PIXBUF_DIR/$PIXBUF_VER/loaders.cache" "$DIST_DIR/lib/gdk-pixbuf-2.0/$PIXBUF_VER/" 2>/dev/null || true
#         echo "  Copied gdk-pixbuf loaders"
#     fi
# fi
mkdir -p $DIST_DIR/Resources/Examples
cp -r media Docs $DIST_DIR/Resources
cp -r Apps/DemoApp/*.cpp $DIST_DIR/Resources/Examples/

cd $DIST_DIR
zip -r ../$PACKAGE_ZIP *
cd ..
echo ""
TOTAL_DLLS=$(find "$DIST_DIR" -maxdepth 1 -name '*.dll' | wc -l)
echo "=== Package complete ==="
echo "  Files: $EXE_NAME + $TOTAL_DLLS DLLs"
echo "  Location: $DIST_DIR/"
