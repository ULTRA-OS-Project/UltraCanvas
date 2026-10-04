#!/bin/bash
# package-macos.sh - Create the macOS UltraCanvas suite: every app in one
# folder with one shared Frameworks/ (see "Suite layout" below), the
# `ultramsg` command-line tool beside them, and an optional DMG.
#
# Usage: ./package-macos.sh [options]
#   --build-dir DIR    Build directory (default: build)
#   --output-dir DIR   Output directory (default: dist-macos)
#   --dmg              Also create a DMG disk image (signed, and notarized with --notarize)
#   --no-sign          Skip code signing
#   --notarize         Notarize the suite folder (one submission) and staple each app
#                      (requires APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD env vars)
#
# Environment variables:
#   MACOSX_DEPLOYMENT_TARGET  Oldest macOS the apps must run on (CI: 15.0).
#                        Written as each app's LSMinimumSystemVersion; fails
#                        if a binary in the suite needs a newer macOS. Unset,
#                        each app gets the newest minimum its binaries declare
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

# ── Suite layout ─────────────────────────────────────────────────────────────
#
#   <output>/UltraCanvas/
#     Frameworks/                 every bundled dylib, once
#     Texter.app, UltraFiler.app, ...   no Contents/Frameworks/ of their own
#     ultramsg/bin/ultramsg       the command-line tool (own, empty Frameworks/)
#
# Each .app used to carry its own copy of the ~90 Homebrew dylibs (95-131 MB);
# with eight apps that was ~830 MB of the same libraries, and every new app
# added another ~95 MB to the download. Now the apps' load commands point at
# @executable_path/../../../Frameworks/ - from <app>.app/Contents/MacOS/ up to
# the suite folder - so they share one copy, and a new app costs only its own
# executable and resources. verify_suite fails the run if an app ends up with
# libraries of its own or a reference outside the shared folder.
#
# The price: an app works only inside the suite folder. Install by dragging
# the whole UltraCanvas folder to /Applications, not a single app out of it.
SUITE_NAME="UltraCanvas"
APP_FW_REF="@executable_path/../../../Frameworks"
TOOL_FW_REF="@executable_path/../Frameworks"

# ── Argument parsing ─────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --dmg)        CREATE_DMG=true; shift ;;
        --no-sign)    DO_SIGN=false; shift ;;
        --notarize)   NOTARIZE=true; shift ;;
        -h|--help)
            sed -n '2,22p' "$0" | sed 's/^# \?//'
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

# The oldest macOS the apps must run on - the same variable the compiler,
# CMake and cargo read, so the code and this promise agree. See
# check_min_macos for what is checked against it.
MIN_MACOS="${MACOSX_DEPLOYMENT_TARGET:-}"
if [ -n "$MIN_MACOS" ] && ! [[ "$MIN_MACOS" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]]; then
    echo "Error: MACOSX_DEPLOYMENT_TARGET='$MIN_MACOS' is not a macOS version (e.g. 15.0)"
    exit 1
fi

echo "=== UltraCanvas macOS Packager ==="
echo "  Version:         $VERSION"
echo "  Build dir:       $BUILD_DIR"
echo "  Output dir:      $OUTPUT_DIR"
echo "  Homebrew prefix: $HOMEBREW_PREFIX"
echo "  Minimum macOS:   ${MIN_MACOS:-measured per app (MACOSX_DEPLOYMENT_TARGET unset)}"
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

    # LSMinimumSystemVersion is not written here: finish_suite adds it once
    # every app and the shared Frameworks/ are in place and their minimum macOS
    # has been read (check_min_macos).
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

# $3 is the load-command prefix the references are rewritten to
# ($APP_FW_REF for an app or its plug-in, $TOOL_FW_REF for a tool).
bundle_dylibs() {
    local exe_path="$1"
    local frameworks_dir="$2"
    local fw_ref="$3"

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
    local queue_file copied_file
    queue_file=$(mktemp)
    copied_file=$(mktemp)
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
                echo "$frameworks_dir/$dep_basename" >> "$copied_file"
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
    fix_install_names "$exe_path" "$frameworks_dir" "$fw_ref"

    # Fix the dylibs this call copied. In the shared suite Frameworks/ the
    # ones an earlier app brought are already rewritten, and a dylib's own
    # references are the same whichever app pulled it in.
    local dylib dylib_name
    while IFS= read -r dylib; do
        dylib_name=$(basename "$dylib")
        install_name_tool -id "$fw_ref/$dylib_name" "$dylib" 2>/dev/null || true
        fix_install_names "$dylib" "$frameworks_dir" "$fw_ref"
    done < "$copied_file"
    rm -f "$copied_file"

    echo "  Install names fixed"
}

fix_install_names() {
    local binary="$1"
    local frameworks_dir="$2"
    local fw_ref="$3"

    local deps
    deps=$(otool -L "$binary" 2>/dev/null | tail -n +2 | awk '{print $1}')

    for dep in $deps; do
        local dep_basename
        dep_basename=$(basename "$dep")

        if [ -f "$frameworks_dir/$dep_basename" ]; then
            install_name_tool -change "$dep" \
                "$fw_ref/$dep_basename" \
                "$binary" 2>/dev/null || true
        fi
    done
}

# ── Helper: Strip ────────────────────────────────────────────────────────────
#
# The Linux packager strips its binaries (--strip-unneeded, 391 -> 190 MB);
# this one shipped every executable, plug-in and dylib with its full symbol
# table. `strip -S -x` drops the debug map and the local symbols and keeps the
# global ones - the executables are linked with exported symbols that the
# dlopen()ed LaTeX module binds to, so a plain `strip` (which drops globals
# too) would break it. Runs after bundle_dylibs' install_name_tool rewrites
# and before signing: both change the file, so the signature has to come last.
strip_binaries() {
    local f before after
    before=$(du -sk "$@" 2>/dev/null | awk '{s+=$1} END {print s+0}')
    while IFS= read -r -d '' f; do
        # Mach-O only (the magic of a thin or fat binary), not resources.
        case "$(file -b "$f")" in
            Mach-O*)
                chmod u+w "$f"
                strip -S -x "$f" 2>/dev/null || echo "  Warning: could not strip $(basename "$f")"
                ;;
        esac
    done < <(find "$@" -type f -print0 2>/dev/null)
    after=$(du -sk "$@" 2>/dev/null | awk '{s+=$1} END {print s+0}')
    echo "  Stripped binaries: $((before / 1024)) MB -> $((after / 1024)) MB"
}

# ── Helper: Minimum macOS ────────────────────────────────────────────────────
#
# Every Mach-O file records the oldest macOS it runs on (LC_BUILD_VERSION's
# minos; LC_VERSION_MIN_MACOSX in old binaries), and dyld refuses to load one
# built for a newer macOS than the running system - the executable or any
# dylib it pulls in:
#   Library not loaded: ... (built for macOS 26.0 which is newer than running OS)
# LSMinimumSystemVersion does not change that; it only has Finder refuse the
# app with a readable message instead. So an app runs on the newest minimum
# among its own binaries and the shared Frameworks/ it loads, whatever its
# Info.plist says - and the plist said 12.0 for months while the arm64 build,
# on the macOS 26 runner behind macos-latest, made apps that started only on
# macOS 26.
#
# Our own code follows MACOSX_DEPLOYMENT_TARGET, but the Homebrew dylibs carry
# whatever their bottle was built for - usually the major version of the macOS
# that built it, sometimes that machine's exact version (tesseract's arm64
# Sequoia bottle declared 15.7.5 on 2026-10-04). So the minimum is read from
# the binaries, not assumed.

# macho_min_macos and version_newer
. "$SCRIPT_DIR/scripts/macos-min-version.sh"

# Where check_min_macos records each item's minimum, for the summary table.
MIN_MACOS_LOG=$(mktemp)

# check_min_macos NAME PLIST FLOOR DIR... - read the minimum macOS of every
# Mach-O file under DIR..., fail when one needs a newer macOS than MIN_MACOS,
# and write LSMinimumSystemVersion into PLIST ("" for an item without one):
# MIN_MACOS when it is set, otherwise the newest minimum found, starting from
# FLOOR - the shared Frameworks/' minimum for an app, "" otherwise. Leaves
# that minimum in CHECKED_MIN_MACOS. Runs before signing, because editing
# Info.plist afterwards breaks the bundle's seal.
check_min_macos() {
    local name="$1" plist="$2" floor="$3"
    shift 3
    local too_new="" f v
    while IFS= read -r -d '' f; do
        case "$(file -b "$f")" in
            Mach-O*) ;;
            *) continue ;;
        esac
        v=$(macho_min_macos "$f")
        [ -n "$v" ] || continue
        if [ -z "$floor" ] || version_newer "$v" "$floor"; then
            floor="$v"
        fi
        if [ -n "$MIN_MACOS" ] && version_newer "$v" "$MIN_MACOS"; then
            too_new+="    macOS $v  ${f#"$OUTPUT_DIR"/}"$'\n'
        fi
    done < <(find "$@" -type f -print0 2>/dev/null)

    if [ -z "$floor" ]; then
        echo "  ERROR: no Mach-O file with a minimum macOS found for $name"
        exit 1
    fi

    if [ -n "$too_new" ]; then
        local msg="$name needs macOS $floor, but MACOSX_DEPLOYMENT_TARGET promises $MIN_MACOS"
        [ -n "${GITHUB_ACTIONS:-}" ] && echo "::error::$msg"
        echo "  ERROR: $msg. These binaries would not load on macOS $MIN_MACOS:"
        printf '%s' "$too_new"
        echo "  A Homebrew dylib carries the macOS its bottle was built for: build on a"
        echo "  macOS no newer than $MIN_MACOS and run scripts/homebrew-rebuild-newer-kegs.sh"
        echo "  after brew install, or raise MACOSX_DEPLOYMENT_TARGET (.github/workflows/build.yml)."
        exit 1
    fi

    local declared="${MIN_MACOS:-$floor}"
    if [ -n "$plist" ]; then
        /usr/libexec/PlistBuddy -c "Add :LSMinimumSystemVersion string $declared" "$plist"
    fi
    CHECKED_MIN_MACOS="$floor"
    echo "$name $declared" >> "$MIN_MACOS_LOG"
    echo "  $name: minimum macOS $declared (newest minimum among its binaries: $floor)"
}

# ── Helper: Code sign ────────────────────────────────────────────────────────

codesign_bundle() {
    local app_bundle="$1"

    echo "  Signing bundle..."

    # The shared suite Frameworks/ is signed once, before any app
    # (sign_shared_frameworks); the bundle holds only its plug-ins and
    # executable.

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
#
# $1 may also be a disk image: notarytool takes a .dmg as it is, so only a
# bundle or a tool folder is zipped first.
notarize_bundle() {
    local app_bundle="$1"
    local staple="${2:-true}"
    local submit_path="$app_bundle" zip_path=""
    local submit_log
    submit_log=$(mktemp)

    if [ -d "$app_bundle" ]; then
        zip_path="${app_bundle%.app}-notarize.zip"
        submit_path="$zip_path"
        echo "  Creating zip for notarization..."
        /usr/bin/ditto -c -k --keepParent "$app_bundle" "$zip_path"
    fi

    echo "  Submitting to Apple notary service (this may take a few minutes)..."
    # Tee to a temp file so we keep live progress output AND can parse the result
    xcrun notarytool submit "$submit_path" \
        --apple-id "$APPLE_ID" \
        --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" \
        --wait 2>&1 | tee "$submit_log"

    [ -n "$zip_path" ] && rm -f "$zip_path"

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

# Apps that never typeset LaTeX, so their bundles get neither the LaTeX module
# (PlugIns/libUltraCanvasLaTeX.dylib and the libraries it pulls into
# Frameworks/) nor its fonts (media/microtex). Neither app has a text area,
# rich-text or Markdown view of its own; the one Markdown view they reach is
# the modal dialog's message, which shows $...$ as plain text (Greek names
# substituted) when the module is absent - and their dialogs carry only their
# own status and error text. An app added to the bundle list keeps LaTeX by
# default; add it here only after checking the same.
NO_LATEX_APPS=(UltraNetMonitor DeviceExplorer)

# True when the app $1 is in NO_LATEX_APPS.
app_without_latex() {
    local a
    for a in "${NO_LATEX_APPS[@]}"; do
        [ "$a" = "$1" ] && return 0
    done
    return 1
}

# Copy media/ into $1; with $2 = "samples" the demo sample content comes too,
# with $3 = "nolatex" the MicroTeX fonts stay out.
copy_media() {
    local dest="$1" with_samples="$2" latex="${3:-}"
    mkdir -p "$dest"
    local entry name skip s
    for entry in "$SCRIPT_DIR"/media/*; do
        name="$(basename "$entry")"
        skip=false
        [ "$latex" = "nolatex" ] && [ "$name" = "microtex" ] && skip=true
        if [ "$with_samples" != "samples" ]; then
            for s in "${DEMO_SAMPLE_MEDIA[@]}"; do
                [ "$name" = "$s" ] && { skip=true; break; }
            done
        fi
        $skip || cp -R "$entry" "$dest/"
    done
}

# ── Finding built executables ────────────────────────────────────────────────

# Prints the path of executable $1 and succeeds when it was built. Most
# targets land in the build root; some set RUNTIME_OUTPUT_DIRECTORY to bin/
# (UltraFIBU, UltraAuthenticator, UltraPassword), so look in both, as
# package-linux.sh and package-win.sh do.
find_built_exe() {
    local cand
    for cand in "$BUILD_DIR/$1" "$BUILD_DIR/bin/$1"; do
        if [ -f "$cand" ]; then echo "$cand"; return 0; fi
    done
    return 1
}

# Apps that were not built, reported at the end of the run.
SKIPPED_APPS=()

# Runs "$2..." (build_app_bundle or build_cli_tool) for executable $1 when it
# was built, and records it as skipped when it was not.
#
# The check sits outside the build function on purpose. Calling it as
# `build_app_bundle ... || echo skipped` - the obvious way to make a missing
# app non-fatal - switches `set -e` off for the function's entire body: bash
# ignores -e in any command on the left of || or &&. A failure in signing,
# iconutil or the dylib copy would then be carried past, and an unsigned or
# half-built bundle shipped. Called as a plain command, as here, every step
# inside still ends the run when it fails.
package_if_built() {
    local exe_name="$1"; shift
    if ! find_built_exe "$exe_name" >/dev/null; then
        echo "── Skipping $exe_name: not built (looked in $BUILD_DIR and $BUILD_DIR/bin) ──"
        SKIPPED_APPS+=("$exe_name")
        return 0
    fi
    "$@"
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

    local exe_path
    if ! exe_path="$(find_built_exe "$exe_name")"; then
        echo "Error: $exe_name not found in $BUILD_DIR or $BUILD_DIR/bin"
        return 1
    fi

    local app_dir="$SUITE_DIR/${exe_name}.app"
    local contents_dir="$app_dir/Contents"

    echo "── Packaging $display_name ──"

    # Create directory structure. No Contents/Frameworks/: the dylibs go to
    # the suite's shared one (see "Suite layout").
    mkdir -p "$contents_dir/MacOS"
    mkdir -p "$contents_dir/Resources"

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
        local latex_media=""
        app_without_latex "$exe_name" && latex_media="nolatex"
        copy_media "$contents_dir/Resources/media" "$samples" "$latex_media"
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
    if app_without_latex "$exe_name"; then
        echo "  Skipping LaTeX module ($display_name does not typeset LaTeX)"
    else
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
    fi

    # Bundle Homebrew dylibs into the shared suite Frameworks/
    bundle_dylibs "$contents_dir/MacOS/$exe_name" "$SHARED_FW" "$APP_FW_REF"

    # The module's own Homebrew dependencies (cairo, pango, ...) are largely
    # the executable's, but collect and rewrite them from the module as well
    # so a dependency only it has is bundled and its load commands point into
    # the shared Frameworks/ (@executable_path resolves against the app,
    # which is right for a plugin the app loads).
    if [ -f "$contents_dir/PlugIns/libUltraCanvasLaTeX.dylib" ]; then
        bundle_dylibs "$contents_dir/PlugIns/libUltraCanvasLaTeX.dylib" "$SHARED_FW" "$APP_FW_REF"
    fi

    # PlugIns/ is absent from a bundle without the LaTeX module, and a missing
    # path makes strip_binaries' du fail - fatal under set -e -o pipefail.
    # The shared Frameworks/ is stripped once, after the last app.
    local strip_dirs=("$contents_dir/MacOS")
    [ -d "$contents_dir/PlugIns" ] && strip_dirs+=("$contents_dir/PlugIns")
    strip_binaries "${strip_dirs[@]}"

    # Signed and notarized after the last app (finish_suite): the shared
    # Frameworks/ has to be complete and signed first.
    BUILT_APPS+=("$app_dir")

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

    local exe_path
    if ! exe_path="$(find_built_exe "$exe_name")"; then
        echo "Error: $exe_name not found in $BUILD_DIR or $BUILD_DIR/bin"
        return 1
    fi

    local tool_dir="$SUITE_DIR/$exe_name"

    echo "── Packaging $exe_name (command line) ──"

    mkdir -p "$tool_dir/bin" "$tool_dir/Frameworks"
    cp "$exe_path" "$tool_dir/bin/$exe_name"
    chmod 755 "$tool_dir/bin/$exe_name"
    echo "  Copied executable"

    bundle_dylibs "$tool_dir/bin/$exe_name" "$tool_dir/Frameworks" "$TOOL_FW_REF"
    strip_binaries "$tool_dir/bin" "$tool_dir/Frameworks"
    check_min_macos "$exe_name" "" "" "$tool_dir/bin" "$tool_dir/Frameworks"

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

    # Notarized with the rest of the suite folder (finish_suite).

    echo "  Tool size: $(du -sh "$tool_dir" | cut -f1)"
    echo ""
}

# ── Suite checks, signing and notarization ──────────────────────────────────

# Fails the run when the suite breaks its layout. Every reference in an app's
# executable, its plug-ins and the shared dylibs must be a system library or
# a dylib that is in the shared Frameworks/, and no app may carry
# Contents/Frameworks/. That is the rule that keeps a new app from adding
# another ~95 MB copy of the libraries: an app packaged any other way than
# through build_app_bundle trips it.
verify_suite() {
    echo "── Verifying the suite layout ──"
    local errors=0 app bin dep name
    for app in "${BUILT_APPS[@]}"; do
        if [ -d "$app/Contents/Frameworks" ] && \
           [ -n "$(ls -A "$app/Contents/Frameworks" 2>/dev/null)" ]; then
            echo "  ERROR: $(basename "$app") has its own Contents/Frameworks/ - apps share $SUITE_NAME/Frameworks/"
            errors=$((errors + 1))
        fi
    done
    while IFS= read -r -d '' bin; do
        case "$(file -b "$bin")" in Mach-O*) ;; *) continue ;; esac
        while IFS= read -r dep; do
            case "$dep" in
                /System/*|/usr/lib/*) ;;
                "$APP_FW_REF"/*)
                    name="${dep#"$APP_FW_REF"/}"
                    if [ ! -f "$SHARED_FW/$name" ]; then
                        echo "  ERROR: $(basename "$bin") needs $name, which is not in $SUITE_NAME/Frameworks/"
                        errors=$((errors + 1))
                    fi
                    ;;
                "${HOMEBREW_PREFIX}"/*|/opt/homebrew/*|/usr/local/*)
                    echo "  ERROR: $(basename "$bin") still loads $dep from Homebrew"
                    errors=$((errors + 1))
                    ;;
            esac
        done < <(otool -L "$bin" 2>/dev/null | tail -n +2 | awk '{print $1}')
    done < <(find "$SHARED_FW" "${BUILT_APPS[@]/%//Contents/MacOS}" \
                  "${BUILT_APPS[@]/%//Contents/PlugIns}" -type f -print0 2>/dev/null)
    if [ "$errors" -gt 0 ]; then
        echo "  $errors layout error(s) - see \"Suite layout\" at the top of this script"
        exit 1
    fi
    echo "  ${#BUILT_APPS[@]} apps share $(find "$SHARED_FW" -name '*.dylib' | wc -l | tr -d ' ') dylibs in $SUITE_NAME/Frameworks/"
}

sign_shared_frameworks() {
    echo "  Signing the shared Frameworks/..."
    local dylib
    for dylib in "$SHARED_FW/"*.dylib; do
        if [ -f "$dylib" ]; then
            codesign --force --timestamp --options runtime \
                --sign "$IDENTITY" "$dylib"
        fi
    done
}

# After the last app: strip the shared Frameworks/ once, check the layout and
# the minimum macOS, sign the shared Frameworks/ and then each app, and
# notarize the whole suite folder in one submission - the apps, their plug-ins,
# the shared dylibs and ultramsg - instead of one round trip to Apple per app.
# Each app then gets its ticket stapled; the folder itself cannot carry one.
finish_suite() {
    echo "── Finishing the $SUITE_NAME suite ──"
    strip_binaries "$SHARED_FW"
    verify_suite

    # Before signing, which seals the Info.plist the minimum is written to.
    # The shared Frameworks/ is read once, and each app's own executable and
    # plug-ins on top of it: every app loads from the same folder.
    echo "── Minimum macOS ──"
    check_min_macos "Frameworks/" "" "" "$SHARED_FW"
    local shared_min="$CHECKED_MIN_MACOS" app
    for app in "${BUILT_APPS[@]}"; do
        local own=("$app/Contents/MacOS")
        [ -d "$app/Contents/PlugIns" ] && own+=("$app/Contents/PlugIns")
        check_min_macos "$(basename "$app")" "$app/Contents/Info.plist" "$shared_min" "${own[@]}"
    done

    if $DO_SIGN; then
        sign_shared_frameworks
        local app
        for app in "${BUILT_APPS[@]}"; do
            echo "── Signing $(basename "$app") ──"
            codesign_bundle "$app"
        done
    fi

    if $NOTARIZE; then
        notarize_bundle "$SUITE_DIR" false
        local app
        for app in "${BUILT_APPS[@]}"; do
            echo "  Stapling $(basename "$app")..."
            xcrun stapler staple "$app"
            xcrun stapler validate "$app"
        done
    fi
    echo ""
}

# ── Main ─────────────────────────────────────────────────────────────────────

# Clean and create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
SUITE_DIR="$OUTPUT_DIR/$SUITE_NAME"
SHARED_FW="$SUITE_DIR/Frameworks"
mkdir -p "$SHARED_FW"
BUILT_APPS=()

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
package_if_built "Texter" build_app_bundle \
    "Texter" \
    "UltraCanvas Texter" \
    "com.cloverleaf.UltraCanvasTexter" \
    "media/appicon/Texter.png" \
    "public.app-category.productivity" \
    "$TEXTER_DOC_TYPES"

# Package UltraCanvasDemo
package_if_built "UltraCanvasDemo" build_app_bundle \
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
package_if_built "UltraFiler" build_app_bundle \
    "UltraFiler" \
    "UltraFiler" \
    "com.cloverleaf.UltraFiler" \
    "media/appicon/UltraFiler.png" \
    "public.app-category.utilities" \
    ""

# Package UltraViewer (universal media viewer)
package_if_built "UltraViewer" build_app_bundle \
    "UltraViewer" \
    "UltraViewer" \
    "com.cloverleaf.UltraViewer" \
    "media/appicon/UltraViewer.png" \
    "public.app-category.photography" \
    "$VIEWER_DOC_TYPES"

# Package UltraNetMonitor
package_if_built "UltraNetMonitor" build_app_bundle \
    "UltraNetMonitor" \
    "UltraNetMonitor" \
    "com.cloverleaf.UltraNetMonitor" \
    "media/appicon/UltraNetMonitor.png" \
    "public.app-category.utilities" \
    ""

# Package DeviceExplorer
package_if_built "DeviceExplorer" build_app_bundle \
    "DeviceExplorer" \
    "DeviceExplorer" \
    "com.cloverleaf.DeviceExplorer" \
    "media/appicon/DeviceExplorer.png" \
    "public.app-category.utilities" \
    ""

# Package UltraAuthenticator and UltraPassword (both need libsodium, through
# UltraCrypt, and are not built without it).
package_if_built "UltraAuthenticator" build_app_bundle \
    "UltraAuthenticator" \
    "UltraAuthenticator" \
    "com.cloverleaf.UltraAuthenticator" \
    "media/appicon/UltraAuthenticator.png" \
    "public.app-category.utilities" \
    ""

package_if_built "UltraPassword" build_app_bundle \
    "UltraPassword" \
    "UltraPassword" \
    "com.cloverleaf.UltraPassword" \
    "media/appicon/UltraPassword.png" \
    "public.app-category.utilities" \
    ""

# Package the UltraMessage command line (Apps/UltraMessageCli)
package_if_built "ultramsg" build_cli_tool "ultramsg"

# ── What was packaged ───────────────────────────────────────────────────────

if [ "${#SKIPPED_APPS[@]}" -gt 0 ]; then
    echo "Not built, so not packaged: ${SKIPPED_APPS[*]}"
fi
if ! compgen -G "$OUTPUT_DIR/*.app" >/dev/null; then
    echo "Error: no app bundle was produced - is $BUILD_DIR the right build directory?" >&2
    exit 1
fi

# A new app goes above this line, through build_app_bundle - never with its
# own Frameworks/ (verify_suite rejects that). See "Suite layout".
finish_suite

# ── Optional DMG creation ───────────────────────────────────────────────────

if $CREATE_DMG; then
    echo "── Creating DMG ──"
    DMG_NAME="UCDemo-MacOS-${VERSION}-$(uname -m).dmg"
    DMG_STAGING="$OUTPUT_DIR/.dmg_staging"

    mkdir -p "$DMG_STAGING"

    # The suite folder as one item: dragging it to Applications installs the
    # apps together with the Frameworks/ they share.
    cp -R "$SUITE_DIR" "$DMG_STAGING/"

    # Add Applications symlink for drag-and-drop install
    ln -s /Applications "$DMG_STAGING/Applications"

    # Create the compressed DMG. ULMO (LZMA) is the tightest format hdiutil
    # has: measured on CI on 2026-10-02 (arm64), ULFO (LZFSE) came to 523 MB
    # against 503 MB for the zip of the same .app folders, so only LZMA beats
    # that. It needs macOS 10.15 to open, below any macOS the apps inside
    # run on (check_min_macos). hdiutil on CI runners now and then fails
    # with "Resource busy" while the system indexes the staging folder, so it
    # gets three tries.
    dmg_try=1
    until hdiutil create \
            -volname "UltraCanvas $VERSION" \
            -srcfolder "$DMG_STAGING" \
            -ov -format ULMO \
            "$OUTPUT_DIR/$DMG_NAME"; do
        if [ "$dmg_try" -ge 3 ]; then
            echo "  ERROR: hdiutil create failed $dmg_try times"
            exit 1
        fi
        dmg_try=$((dmg_try + 1))
        echo "  hdiutil create failed - retrying ($dmg_try/3) in 10 s"
        sleep 10
    done

    rm -rf "$DMG_STAGING"

    # The image is what a user downloads and opens, so it carries the same
    # Developer ID signature as the apps inside it and, on a notarized run, its
    # own stapled ticket - Gatekeeper then checks it once, offline, on open.
    if $DO_SIGN; then
        echo "  Signing DMG..."
        codesign --force --timestamp --sign "$IDENTITY" "$OUTPUT_DIR/$DMG_NAME"
        codesign --verify --verbose=2 "$OUTPUT_DIR/$DMG_NAME"
    fi
    if $NOTARIZE; then
        notarize_bundle "$OUTPUT_DIR/$DMG_NAME"
    fi

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

# Size breakdown: each app on its own (executable, plug-ins, resources) and
# the shared Frameworks/ they all use, which is where the bulk of the download
# is, with the oldest macOS each one runs on (check_min_macos). Written to the
# job summary too when run in GitHub Actions, so a change to what the apps
# link - or to the macOS they need - shows up on the run page.
echo ""
echo "  Suite contents:"
SIZE_TABLE="| Item | Size | dylibs | Needs macOS |"$'\n'"|---|---:|---:|---:|"
for item in "$SUITE_DIR"/*.app "$SUITE_DIR/ultramsg"; do
    [ -d "$item" ] || continue
    total=$(du -sh "$item" | cut -f1)
    min_macos=$(awk -v n="$(basename "$item")" '$1 == n {print $2}' "$MIN_MACOS_LOG")
    echo "    $(basename "$item"): $total, needs macOS ${min_macos:-?}"
    SIZE_TABLE+=$'\n'"| $(basename "$item") | $total | | ${min_macos:-?} |"
done
fw_size=$(du -sh "$SHARED_FW" | cut -f1)
fw_count=$(find "$SHARED_FW" -name '*.dylib' | wc -l | tr -d ' ')
fw_min_macos=$(awk '$1 == "Frameworks/" {print $2}' "$MIN_MACOS_LOG")
echo "    Frameworks/ (shared by ${#BUILT_APPS[@]} apps): $fw_size in $fw_count dylibs, needs macOS ${fw_min_macos:-?}"
SIZE_TABLE+=$'\n'"| Frameworks/ (shared by ${#BUILT_APPS[@]} apps) | $fw_size | $fw_count | ${fw_min_macos:-?} |"
SIZE_TABLE+=$'\n'"| **$SUITE_NAME/** (unpacked) | $(du -sh "$SUITE_DIR" | cut -f1) | | |"
for dmg in "$OUTPUT_DIR"/*.dmg; do
    [ -f "$dmg" ] || continue
    SIZE_TABLE+=$'\n'"| **$(basename "$dmg")** (download) | $(du -sh "$dmg" | cut -f1) | | |"
done
rm -f "$MIN_MACOS_LOG"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### macOS suite sizes ($(uname -m))"
        echo ""
        echo "$SIZE_TABLE"
    } >> "$GITHUB_STEP_SUMMARY"
fi
