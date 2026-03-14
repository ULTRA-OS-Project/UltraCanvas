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

# Also collect DLLs needed by the DLLs themselves (transitive deps)
# Run ldd on each copied DLL to catch anything missed
for dll in "$DIST_DIR"/*.dll; do
    if [ -f "$dll" ]; then
        ldd "$dll" 2>/dev/null | grep -i '/mingw64/' | awk '{print $3}' | sort -u | while read -r dep; do
            dep_name=$(basename "$dep")
            if [ -f "$dep" ] && [ ! -f "$DIST_DIR/$dep_name" ]; then
                cp "$dep" "$DIST_DIR/"
                echo "  $dep_name (transitive)"
            fi
        done
    fi
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
