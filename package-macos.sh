#!/bin/bash
# package-macos.sh - Create macOS .app bundles for UltraCanvas applications
# Packages UltraCanvasTexter and UltraCanvasDemo with bundled dylibs,
# Info.plist, .icns icons, and optional DMG creation.
#
# Usage: ./package-macos.sh [options]
#   --build-dir DIR    Build directory (default: build)
#   --output-dir DIR   Output directory (default: dist-macos)
#   --dmg              Also create a DMG disk image
#   --no-sign          Skip code signing
#   --notarize         Submit signed bundles to Apple notary service and staple
#                      (requires APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD env vars)
#
# Environment variables:
#   APPLE_SIGN_ID        Override the default code-signing identity
#   APPLE_ID             Apple ID email (for --notarize)
#   APPLE_TEAM_ID        Apple Developer Team ID (for --notarize)
#   APPLE_APP_PASSWORD   App-specific password (for --notarize)

set -e -x -o pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────

BUILD_DIR="build"
OUTPUT_DIR="dist-macos"
CREATE_DMG=false
DO_SIGN=true
NOTARIZE=false
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENTITLEMENTS_PATH="MacOS/entitlements.plist"
IDENTITY="${APPLE_SIGN_ID:-Developer ID Application: Cloverleaf RISCOS Computer UG (haftungsbeschrankt) (29638T25M9)}"

# ── Argument parsing ─────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --dmg)        CREATE_DMG=true; shift ;;
        --no-sign)    DO_SIGN=false; shift ;;
        --notarize)   NOTARIZE=true; shift ;;
        -h|--help)
            sed -n '2,18p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Version and environment ──────────────────────────────────────────────────

if [ ! -f "$SCRIPT_DIR/UltraCanvas/VERSION" ]; then
    echo "Error: VERSION file not found"
    exit 1
fi
VERSION=$(cat "$SCRIPT_DIR/UltraCanvas/VERSION" | tr -d '[:space:]')

if ! command -v brew &>/dev/null; then
    echo "Error: Homebrew is required"
    exit 1
fi
HOMEBREW_PREFIX=$(brew --prefix)

echo "=== UltraCanvas macOS Packager ==="
echo "  Version:         $VERSION"
echo "  Build dir:       $BUILD_DIR"
echo "  Output dir:      $OUTPUT_DIR"
echo "  Homebrew prefix: $HOMEBREW_PREFIX"
echo "  Code signing:    $DO_SIGN"
echo "  Notarize:        $NOTARIZE"
echo "  Create DMG:      $CREATE_DMG"
echo ""

# ── Validate prerequisites ───────────────────────────────────────────────────

for tool in sips iconutil otool install_name_tool; do
    if ! command -v "$tool" &>/dev/null; then
        echo "Error: Required tool '$tool' not found. Install Xcode command-line tools."
        exit 1
    fi
done

if $DO_SIGN && ! command -v codesign &>/dev/null; then
    echo "Error: codesign not found. Install Xcode command-line tools or use --no-sign."
    exit 1
fi

if $CREATE_DMG && ! command -v hdiutil &>/dev/null; then
    echo "Error: hdiutil not found (required for --dmg)."
    exit 1
fi

if $NOTARIZE; then
    if ! $DO_SIGN; then
        echo "Error: --notarize requires signing (do not combine with --no-sign)"
        exit 1
    fi
    if ! command -v xcrun &>/dev/null; then
        echo "Error: xcrun not found (required for --notarize)"
        exit 1
    fi
    if [ -z "${APPLE_ID:-}" ] || [ -z "${APPLE_TEAM_ID:-}" ] || [ -z "${APPLE_APP_PASSWORD:-}" ]; then
        echo "Error: --notarize requires APPLE_ID, APPLE_TEAM_ID, and APPLE_APP_PASSWORD env vars"
        exit 1
    fi
fi

# ── Helper: Generate .icns from PNG ──────────────────────────────────────────

generate_icns() {
    local src_png="$1"
    local output_icns="$2"

    if [ ! -f "$src_png" ]; then
        echo "  Warning: Icon source not found: $src_png (skipping icon)"
        return 1
    fi

    local tmpdir
    tmpdir=$(mktemp -d)
    local iconset_dir="$tmpdir/AppIcon.iconset"
    mkdir -p "$iconset_dir"

    # Generate all required sizes
    sips -z 16 16     "$src_png" --out "$iconset_dir/icon_16x16.png"      >/dev/null 2>&1
    sips -z 32 32     "$src_png" --out "$iconset_dir/icon_16x16@2x.png"   >/dev/null 2>&1
    sips -z 32 32     "$src_png" --out "$iconset_dir/icon_32x32.png"      >/dev/null 2>&1
    sips -z 64 64     "$src_png" --out "$iconset_dir/icon_32x32@2x.png"   >/dev/null 2>&1
    sips -z 128 128   "$src_png" --out "$iconset_dir/icon_128x128.png"    >/dev/null 2>&1
    sips -z 256 256   "$src_png" --out "$iconset_dir/icon_128x128@2x.png" >/dev/null 2>&1
    sips -z 256 256   "$src_png" --out "$iconset_dir/icon_256x256.png"    >/dev/null 2>&1
    sips -z 512 512   "$src_png" --out "$iconset_dir/icon_256x256@2x.png" >/dev/null 2>&1
    sips -z 512 512   "$src_png" --out "$iconset_dir/icon_512x512.png"    >/dev/null 2>&1
    sips -z 1024 1024 "$src_png" --out "$iconset_dir/icon_512x512@2x.png" >/dev/null 2>&1

    iconutil -c icns "$iconset_dir" -o "$output_icns"
    rm -rf "$tmpdir"
    echo "  Generated icon: $(basename "$output_icns")"
}

# ── Helper: Generate Info.plist ──────────────────────────────────────────────

generate_plist() {
    local plist_path="$1"
    local exe_name="$2"
    local display_name="$3"
    local bundle_id="$4"
    local version="$5"
    local category="$6"
    local extra_plist_entries="$7"

    cat > "$plist_path" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${display_name}</string>
    <key>CFBundleDisplayName</key>
    <string>${display_name}</string>
    <key>CFBundleIdentifier</key>
    <string>${bundle_id}</string>
    <key>CFBundleVersion</key>
    <string>${version}</string>
    <key>CFBundleShortVersionString</key>
    <string>${version}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleExecutable</key>
    <string>${exe_name}</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright (C) 2026 Cloverleaf UG. All rights reserved.</string>
    <key>LSApplicationCategoryType</key>
    <string>${category}</string>
${extra_plist_entries}
</dict>
</plist>
PLIST
    echo "  Generated Info.plist"
}

# ── Helper: Bundle dylibs ───────────────────────────────────────────────────

bundle_dylibs() {
    local exe_path="$1"
    local frameworks_dir="$2"

    echo "  Bundling dynamic libraries..."

    # Collect all dylibs using breadth-first traversal. Each queue entry is
    # "source_dir|current_path":
    #   source_dir   - directory the binary *originated* from on the host
    #                  (used to resolve @loader_path references)
    #   current_path - where we actually read the binary from (exe in MacOS/
    #                  or a copy in Frameworks/). Mach-O load commands are
    #                  preserved byte-for-byte by cp -L, so otool reads are
    #                  identical to the original.
    # Use a file-based queue since macOS ships with Bash 3.2 (no associative arrays)
    local queue_file
    queue_file=$(mktemp)
    echo "$(dirname "$exe_path")|$exe_path" > "$queue_file"
    local count=0

    while [ -s "$queue_file" ]; do
        local entry
        entry=$(head -1 "$queue_file")
        sed -i '' '1d' "$queue_file"

        local source_dir="${entry%%|*}"
        local current="${entry#*|}"

        # LC_RPATH entries from the binary, for resolving @rpath/... deps.
        # otool -l output format:
        #     cmd LC_RPATH
        # cmdsize NN
        #    path /opt/homebrew/opt/openexr/lib (offset 12)
        local rpaths
        rpaths=$(otool -l "$current" 2>/dev/null | awk '
            /cmd LC_RPATH/ {in_rpath=1; next}
            in_rpath && $1 == "path" {print $2; in_rpath=0}
        ')

        local deps
        deps=$(otool -L "$current" 2>/dev/null | tail -n +2 | awk '{print $1}')

        for dep in $deps; do
            # Skip system libraries
            case "$dep" in
                /System/*|/usr/lib/*|/usr/X11/*) continue ;;
                @executable_path/*) continue ;;
            esac

            local dep_basename
            dep_basename=$(basename "$dep")

            # Skip if already copied to Frameworks
            if [ -f "$frameworks_dir/$dep_basename" ]; then
                continue
            fi

            # Resolve the reference to an actual host filesystem path.
            local dep_source=""
            case "$dep" in
                @rpath/*)
                    local rel="${dep#@rpath/}"
                    # Try each LC_RPATH entry from the referring binary
                    local rpath resolved_rpath
                    for rpath in $rpaths; do
                        case "$rpath" in
                            @loader_path/*)
                                resolved_rpath="$source_dir/${rpath#@loader_path/}"
                                ;;
                            @executable_path/*)
                                continue
                                ;;
                            *)
                                resolved_rpath="$rpath"
                                ;;
                        esac
                        if [ -f "$resolved_rpath/$rel" ]; then
                            dep_source="$resolved_rpath/$rel"
                            break
                        fi
                    done
                    # Fall back to $HOMEBREW_PREFIX/lib (homebrew symlinks
                    # all kegs here, so this catches libs like libIlmThread
                    # that OpenEXR references via @rpath).
                    if [ -z "$dep_source" ] && [ -f "$HOMEBREW_PREFIX/lib/$rel" ]; then
                        dep_source="$HOMEBREW_PREFIX/lib/$rel"
                    fi
                    ;;
                @loader_path/*)
                    local rel="${dep#@loader_path/}"
                    if [ -f "$source_dir/$rel" ]; then
                        dep_source="$source_dir/$rel"
                    fi
                    ;;
                /*)
                    dep_source="$dep"
                    ;;
            esac

            if [ -z "$dep_source" ] || [ ! -f "$dep_source" ]; then
                echo "  Warning: could not resolve $dep (from $(basename "$current"))"
                continue
            fi

            # Only bundle Homebrew libraries
            case "$dep_source" in
                ${HOMEBREW_PREFIX}/*|/opt/homebrew/*|/usr/local/*) ;;
                *) continue ;;
            esac

            # Resolve symlinks and copy
            local resolved_dep
            resolved_dep=$(python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$dep_source" 2>/dev/null || echo "$dep_source")

            if [ -f "$resolved_dep" ]; then
                cp -L "$resolved_dep" "$frameworks_dir/$dep_basename"
                chmod 644 "$frameworks_dir/$dep_basename"
                count=$((count + 1))
                # Queue the copy for BFS. source_dir is the real host
                # directory so @loader_path deps resolve against siblings
                # on disk, not the (empty) Frameworks dir.
                echo "$(dirname "$resolved_dep")|$frameworks_dir/$dep_basename" >> "$queue_file"
            fi
        done
    done

    rm -f "$queue_file"

    echo "  Copied $count dylibs"

    # Fix install names
    echo "  Fixing install names..."

    # Fix the executable
    fix_install_names "$exe_path" "$frameworks_dir"

    # Fix each bundled dylib
    for dylib in "$frameworks_dir"/*.dylib; do
        if [ -f "$dylib" ]; then
            local dylib_name
            dylib_name=$(basename "$dylib")
            install_name_tool -id "@executable_path/../Frameworks/$dylib_name" "$dylib" 2>/dev/null || true
            fix_install_names "$dylib" "$frameworks_dir"
        fi
    done

    echo "  Install names fixed"
}

fix_install_names() {
    local binary="$1"
    local frameworks_dir="$2"

    local deps
    deps=$(otool -L "$binary" 2>/dev/null | tail -n +2 | awk '{print $1}')

    for dep in $deps; do
        local dep_basename
        dep_basename=$(basename "$dep")

        if [ -f "$frameworks_dir/$dep_basename" ]; then
            install_name_tool -change "$dep" \
                "@executable_path/../Frameworks/$dep_basename" \
                "$binary" 2>/dev/null || true
        fi
    done
}

# ── Helper: Code sign ────────────────────────────────────────────────────────

codesign_bundle() {
    local app_bundle="$1"

    echo "  Signing bundle..."

    # Sign frameworks first (inside-out), with hardened runtime + secure timestamp
    for dylib in "$app_bundle/Contents/Frameworks/"*.dylib; do
        if [ -f "$dylib" ]; then
            codesign --force --timestamp --options runtime \
                --sign "$IDENTITY" "$dylib"
        fi
    done

    # Sign the main executable with hardened runtime + secure timestamp
    codesign --force --timestamp --options runtime \
        --sign "$IDENTITY" "$app_bundle/Contents/MacOS/"*

    # Sign the outer bundle with hardened runtime, secure timestamp, and entitlements
    codesign --force --timestamp --options runtime \
        --sign "$IDENTITY" --entitlements "$ENTITLEMENTS_PATH" "$app_bundle"

    # Verify the final bundle
    codesign --verify --verbose=4 --strict "$app_bundle"

    echo "  Bundle signed"
}

# ── Helper: Notarize bundle ──────────────────────────────────────────────────

notarize_bundle() {
    local app_bundle="$1"
    local zip_path="${app_bundle%.app}-notarize.zip"
    local submit_log
    submit_log=$(mktemp)

    echo "  Creating zip for notarization..."
    /usr/bin/ditto -c -k --keepParent "$app_bundle" "$zip_path"

    echo "  Submitting to Apple notary service (this may take a few minutes)..."
    # Tee to a temp file so we keep live progress output AND can parse the result
    xcrun notarytool submit "$zip_path" \
        --apple-id "$APPLE_ID" \
        --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" \
        --wait 2>&1 | tee "$submit_log"

    rm -f "$zip_path"

    # Parse the submission ID and final status from the captured output
    local submission_id status
    submission_id=$(awk '/^  id:/ {print $2; exit}' "$submit_log")
    status=$(awk '/^  status:/ {print $2}' "$submit_log" | tail -n 1)
    rm -f "$submit_log"

    if [ "$status" != "Accepted" ]; then
        echo "  ERROR: Notarization status is '$status' (expected 'Accepted')"
        if [ -n "$submission_id" ]; then
            echo "  Fetching notarization log for submission $submission_id..."
            xcrun notarytool log "$submission_id" \
                --apple-id "$APPLE_ID" \
                --team-id "$APPLE_TEAM_ID" \
                --password "$APPLE_APP_PASSWORD" || true
        fi
        exit 1
    fi

    echo "  Stapling notarization ticket..."
    xcrun stapler staple "$app_bundle"

    echo "  Verifying stapled bundle..."
    xcrun stapler validate "$app_bundle"

    echo "  Notarized: $(basename "$app_bundle")"
}

# ── Build one app bundle ─────────────────────────────────────────────────────

build_app_bundle() {
    local exe_name="$1"
    local display_name="$2"
    local bundle_id="$3"
    local icon_src="$4"
    local category="$5"
    local extra_plist="$6"

    local exe_path="$BUILD_DIR/bin/$exe_name"
    if [ ! -f "$exe_path" ]; then
        echo "Warning: Executable not found: $exe_path (skipping $display_name)"
        return 1
    fi

    local app_dir="$OUTPUT_DIR/${exe_name}.app"
    local contents_dir="$app_dir/Contents"

    echo "── Packaging $display_name ──"

    # Create directory structure
    mkdir -p "$contents_dir/MacOS"
    mkdir -p "$contents_dir/Resources"
    mkdir -p "$contents_dir/Frameworks"

    # PkgInfo
    echo -n "APPL????" > "$contents_dir/PkgInfo"

    # Generate .icns
    generate_icns "$SCRIPT_DIR/$icon_src" "$contents_dir/Resources/AppIcon.icns"

    # Generate Info.plist
    generate_plist "$contents_dir/Info.plist" \
        "$exe_name" "$display_name" "$bundle_id" "$VERSION" "$category" "$extra_plist"

    # Copy executable
    cp "$exe_path" "$contents_dir/MacOS/$exe_name"
    chmod 755 "$contents_dir/MacOS/$exe_name"
    echo "  Copied executable"

    # Copy media assets to Resources/media/
    if [ -d "$SCRIPT_DIR/media" ]; then
        cp -R "$SCRIPT_DIR/media" "$contents_dir/Resources/media"
        echo "  Copied media assets"
    else
        echo "  Warning: media/ directory not found"
    fi

    # Bundle Homebrew dylibs
    bundle_dylibs "$contents_dir/MacOS/$exe_name" "$contents_dir/Frameworks"

    # Code sign
    if $DO_SIGN; then
        codesign_bundle "$app_dir"
    fi

    # Notarize and staple
    if $NOTARIZE; then
        notarize_bundle "$app_dir"
    fi

    local bundle_size
    bundle_size=$(du -sh "$app_dir" | cut -f1)
    echo "  Bundle size: $bundle_size"
    echo ""
}

# ── Main ─────────────────────────────────────────────────────────────────────

# Clean and create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Document types for Texter (text editor)
TEXTER_DOC_TYPES='    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Text Document</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>public.plain-text</string>
                <string>public.utf8-plain-text</string>
                <string>net.daringfireball.markdown</string>
                <string>public.source-code</string>
            </array>
        </dict>
    </array>'

# Package UltraCanvasTexter
build_app_bundle \
    "UltraCanvasTexter" \
    "UltraCanvas Texter" \
    "com.cloverleaf.UltraCanvasTexter" \
    "media/appicon/Texter.png" \
    "public.app-category.productivity" \
    "$TEXTER_DOC_TYPES"

# Package UltraCanvasDemo
build_app_bundle \
    "UltraCanvasDemo" \
    "UltraCanvas Demo" \
    "com.cloverleaf.UltraCanvasDemo" \
    "media/appicon/Demo.png" \
    "public.app-category.developer-tools" \
    ""

# ── Optional DMG creation ───────────────────────────────────────────────────

if $CREATE_DMG; then
    echo "── Creating DMG ──"
    DMG_NAME="UltraCanvas-${VERSION}.dmg"
    DMG_STAGING="$OUTPUT_DIR/.dmg_staging"

    mkdir -p "$DMG_STAGING"

    # Copy app bundles to staging
    for app in "$OUTPUT_DIR"/*.app; do
        if [ -d "$app" ]; then
            cp -R "$app" "$DMG_STAGING/"
        fi
    done

    # Add Applications symlink for drag-and-drop install
    ln -s /Applications "$DMG_STAGING/Applications"

    # Create compressed DMG
    hdiutil create \
        -volname "UltraCanvas $VERSION" \
        -srcfolder "$DMG_STAGING" \
        -ov -format UDZO \
        "$OUTPUT_DIR/$DMG_NAME"

    rm -rf "$DMG_STAGING"

    DMG_SIZE=$(du -sh "$OUTPUT_DIR/$DMG_NAME" | cut -f1)
    echo "  DMG created: $OUTPUT_DIR/$DMG_NAME ($DMG_SIZE)"
    echo ""
fi

# ── Summary ──────────────────────────────────────────────────────────────────

echo "=== Packaging complete ==="
echo "  Output: $OUTPUT_DIR/"
ls -1 "$OUTPUT_DIR/" | while read -r item; do
    if [ -d "$OUTPUT_DIR/$item" ]; then
        echo "    $item/ ($(du -sh "$OUTPUT_DIR/$item" | cut -f1))"
    else
        echo "    $item ($(du -sh "$OUTPUT_DIR/$item" | cut -f1))"
    fi
done
