#!/bin/bash
# package-macos.sh - Create macOS .app bundles for UltraCanvas applications
# Packages Texter, UltraCanvasDemo, UltraFiler, UltraViewer, UltraNetMonitor
# and DeviceExplorer as .app bundles with bundled dylibs, Info.plist and .icns icons, the `ultramsg`
# command-line tool as a bin/ + Frameworks/ folder, and an optional DMG.
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
            sed -n '2,19p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Version and environment ──────────────────────────────────────────────────

UC_CHANGELOG="$SCRIPT_DIR/Docs/UltraCanvas/CHANGELOG.md"
if [ ! -f "$UC_CHANGELOG" ]; then
    echo "Error: changelog not found at $UC_CHANGELOG"
    exit 1
fi
VERSION=$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+\.[0-9]+\.[0-9]+)\*.*/\1/p' "$UC_CHANGELOG")
if [ -z "$VERSION" ]; then
    echo "Error: could not parse version from $UC_CHANGELOG (expected '#### YYYY-MM-DD *x.y.z*')"
    exit 1
fi

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

    # Plug-in modules loaded at runtime (the LaTeX engine) are signed like
    # the frameworks: inside-out, before the executable and the bundle.
    for dylib in "$app_bundle/Contents/PlugIns/"*.dylib; do
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

# $2 = false skips stapling: a ticket can only be stapled to a bundle, a disk
# image or an installer package, never to a bare command-line executable.
# Gatekeeper looks the notarization of such a tool up online instead.
notarize_bundle() {
    local app_bundle="$1"
    local staple="${2:-true}"
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

    if [ "$staple" = "true" ]; then
        echo "  Stapling notarization ticket..."
        xcrun stapler staple "$app_bundle"

        echo "  Verifying stapled bundle..."
        xcrun stapler validate "$app_bundle"
    fi

    echo "  Notarized: $(basename "$app_bundle")"
}

# ── Demo sample content ──────────────────────────────────────────────────────
#
# media/ is two things: what the framework and the apps read at run time
# (icons, fonts, the MicroTeX fonts, OCR data, app icons, ...) and the DemoApp's
# sample files - 3D models, videos, pictures, vector drawings, sound, e-books.
# The samples are ~112 MB of media's ~121 MB, and every .app used to get its own
# copy: six bundles made the macOS artifact 920 MB against Linux's 190 MB, which
# ships media/ once (share/media). Only UltraCanvasDemo opens them, so only its
# bundle carries them; nothing else in the tree reads these folders (checked
# 2026-09-28 - the only other mentions are code comments).
#
# Listed here as exclusions rather than the runtime folders as inclusions, so a
# runtime folder added later is shipped by default instead of silently missing.
DEMO_SAMPLE_MEDIA=(3D videos images vector audios ebooks textsamples LaTex diagrams sample.pdf)

# Copy media/ into $1; with $2 = "samples" the demo sample content comes too.
copy_media() {
    local dest="$1" with_samples="$2"
    mkdir -p "$dest"
    local entry name skip s
    for entry in "$SCRIPT_DIR"/media/*; do
        name="$(basename "$entry")"
        skip=false
        if [ "$with_samples" != "samples" ]; then
            for s in "${DEMO_SAMPLE_MEDIA[@]}"; do
                [ "$name" = "$s" ] && { skip=true; break; }
            done
        fi
        $skip || cp -R "$entry" "$dest/"
    done
}

# ── Build one app bundle ─────────────────────────────────────────────────────

build_app_bundle() {
    local exe_name="$1"
    local display_name="$2"
    local bundle_id="$3"
    local icon_src="$4"
    local category="$5"
    local extra_plist="$6"
    local samples="${7:-}"   # "samples": the demo's sample media and sources

    local exe_path="$BUILD_DIR/$exe_name"
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

    # Copy media assets to Resources/media/ (the sample content only for the
    # demo - see DEMO_SAMPLE_MEDIA)
    if [ -d "$SCRIPT_DIR/media" ]; then
        copy_media "$contents_dir/Resources/media" "$samples"
        if [ "$samples" = "samples" ]; then
            echo "  Copied media assets (with the demo samples)"
        else
            echo "  Copied media assets (without the demo samples)"
        fi
    else
        echo "  Warning: media/ directory not found"
    fi

    # Copy documentation to Resources/Docs/ (used by the demo's "View Docs").
    if [ -d "$SCRIPT_DIR/Docs" ]; then
        cp -R "$SCRIPT_DIR/Docs" "$contents_dir/Resources/Docs"
        echo "  Copied documentation"
    fi

    # Copy demo example sources to Resources/DemoApp/ so the demo's
    # "View Source" can load them (paths registered in UltraCanvasDemo.cpp).
    # The demo's alone: no other app has a "View Source".
    if [ "$samples" = "samples" ] && ls "$SCRIPT_DIR"/Apps/DemoApp/*.cpp >/dev/null 2>&1; then
        mkdir -p "$contents_dir/Resources/DemoApp"
        cp "$SCRIPT_DIR"/Apps/DemoApp/*.cpp "$contents_dir/Resources/DemoApp/"
        echo "  Copied demo example sources"
    fi

    # The LaTeX math module is a dlopen()ed CMake MODULE emitted to build/lib;
    # inside a bundle the core's loader probes Contents/PlugIns/ (as
    # <exe>/../PlugIns/). Without it CreateLaTeXView() returns nullptr and the
    # demo's LaTeX page reports the module as not found.
    # CMake's default suffix for a MODULE on macOS is ".so"; the build sets
    # ".dylib", but accept a tree from before that and ship it under the
    # name the loader asks for.
    local latex_module=""
    for cand in "$BUILD_DIR/lib/libUltraCanvasLaTeX.dylib" "$BUILD_DIR/lib/libUltraCanvasLaTeX.so"; do
        if [ -f "$cand" ]; then latex_module="$cand"; break; fi
    done
    if [ -n "$latex_module" ]; then
        mkdir -p "$contents_dir/PlugIns"
        cp "$latex_module" "$contents_dir/PlugIns/libUltraCanvasLaTeX.dylib"
        chmod 644 "$contents_dir/PlugIns/libUltraCanvasLaTeX.dylib"
        echo "  Copied LaTeX module: $(basename "$latex_module") -> PlugIns/libUltraCanvasLaTeX.dylib"
    else
        echo "  Warning: LaTeX module not found in $BUILD_DIR/lib - this bundle will not render LaTeX"
    fi

    # Bundle Homebrew dylibs
    bundle_dylibs "$contents_dir/MacOS/$exe_name" "$contents_dir/Frameworks"

    # The module's own Homebrew dependencies (cairo, pango, ...) are largely
    # the executable's, but collect and rewrite them from the module as well
    # so a dependency only it has is bundled and its load commands point into
    # Frameworks/ (@executable_path resolves against the app, which is right
    # for a plugin the app loads).
    if [ -f "$contents_dir/PlugIns/libUltraCanvasLaTeX.dylib" ]; then
        bundle_dylibs "$contents_dir/PlugIns/libUltraCanvasLaTeX.dylib" "$contents_dir/Frameworks"
    fi

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

# ── Package one command-line tool ────────────────────────────────────────────
#
# A command-line tool is not an .app: it ships as <name>/bin/<name> with its
# Homebrew dylibs in <name>/Frameworks/, the same relative layout a bundle
# has, so bundle_dylibs' @executable_path/../Frameworks rewrite holds for it.

build_cli_tool() {
    local exe_name="$1"

    local exe_path="$BUILD_DIR/$exe_name"
    if [ ! -f "$exe_path" ]; then
        echo "Warning: Executable not found: $exe_path (skipping $exe_name)"
        return 1
    fi

    local tool_dir="$OUTPUT_DIR/$exe_name"

    echo "── Packaging $exe_name (command line) ──"

    mkdir -p "$tool_dir/bin" "$tool_dir/Frameworks"
    cp "$exe_path" "$tool_dir/bin/$exe_name"
    chmod 755 "$tool_dir/bin/$exe_name"
    echo "  Copied executable"

    bundle_dylibs "$tool_dir/bin/$exe_name" "$tool_dir/Frameworks"

    if $DO_SIGN; then
        echo "  Signing tool..."
        for dylib in "$tool_dir/Frameworks/"*.dylib; do
            if [ -f "$dylib" ]; then
                codesign --force --timestamp --options runtime \
                    --sign "$IDENTITY" "$dylib"
            fi
        done
        codesign --force --timestamp --options runtime \
            --sign "$IDENTITY" "$tool_dir/bin/$exe_name"
        codesign --verify --verbose=4 --strict "$tool_dir/bin/$exe_name"
        echo "  Tool signed"
    fi

    if $NOTARIZE; then
        notarize_bundle "$tool_dir" false
    fi

    echo "  Tool size: $(du -sh "$tool_dir" | cut -f1)"
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

# Package Texter
build_app_bundle \
    "Texter" \
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
    "" \
    samples

# Document types for UltraViewer (universal media viewer). Viewer role, so
# Finder offers it under "Open With" for the media it displays without
# claiming to be their editor.
VIEWER_DOC_TYPES='    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Media Document</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>public.image</string>
                <string>public.svg-image</string>
                <string>public.movie</string>
                <string>public.audio</string>
                <string>com.adobe.pdf</string>
                <string>org.idpf.epub-container</string>
                <string>public.plain-text</string>
            </array>
        </dict>
    </array>'

# Package UltraFiler (file manager)
build_app_bundle \
    "UltraFiler" \
    "UltraFiler" \
    "com.cloverleaf.UltraFiler" \
    "media/appicon/UltraFiler.png" \
    "public.app-category.utilities" \
    ""

# Package UltraViewer (universal media viewer)
build_app_bundle \
    "UltraViewer" \
    "UltraViewer" \
    "com.cloverleaf.UltraViewer" \
    "media/appicon/UltraViewer.png" \
    "public.app-category.photography" \
    "$VIEWER_DOC_TYPES"

# Package UltraNetMonitor
build_app_bundle \
    "UltraNetMonitor" \
    "UltraNetMonitor" \
    "com.cloverleaf.UltraNetMonitor" \
    "media/appicon/UltraNetMonitor.png" \
    "public.app-category.utilities" \
    ""

# Package DeviceExplorer
build_app_bundle \
    "DeviceExplorer" \
    "DeviceExplorer" \
    "com.cloverleaf.DeviceExplorer" \
    "media/appicon/DeviceExplorer.png" \
    "public.app-category.utilities" \
    ""

# Package the UltraMessage command line (Apps/UltraMessageCli)
build_cli_tool "ultramsg"

# ── Optional DMG creation ───────────────────────────────────────────────────

if $CREATE_DMG; then
    echo "── Creating DMG ──"
    DMG_NAME="UCDemo-MacOS-${VERSION}-$(uname -m).dmg"
    DMG_STAGING="$OUTPUT_DIR/.dmg_staging"

    mkdir -p "$DMG_STAGING"

    # Copy app bundles to staging
    for app in "$OUTPUT_DIR"/*.app; do
        if [ -d "$app" ]; then
            cp -R "$app" "$DMG_STAGING/"
        fi
    done

    # And the command-line tool folder beside them
    if [ -d "$OUTPUT_DIR/ultramsg" ]; then
        cp -R "$OUTPUT_DIR/ultramsg" "$DMG_STAGING/"
    fi

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
