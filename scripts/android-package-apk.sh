#!/usr/bin/env bash
# Package an UltraCanvas app library into an installable, debug-signed APK
# with the Android SDK's build tools - no Gradle.
#
#   1. UltraCanvasActivity.java -> classes.dex   (javac, d8)
#   2. the launcher icon at every screen density (ImageMagick, aapt2 compile)
#      + AndroidManifest.xml + assets/ -> base APK (aapt2 link)
#   3. + classes.dex + lib/<abi>/lib<App>.so     (stripped with the NDK's llvm-strip)
#      + every shared library it needs that Android does not provide
#        (libc++_shared.so; the NDK's own copy)
#   4. zipalign, then apksigner with a throwaway debug key
#
# The assets are what the app opens by path at run time. On the first launch
# after an install UltraCanvasAndroidAssets.cpp copies assets/<path> to
# $HOME/share/<path>, which is where GetResourcesDir() points on Android - the
# same layout a desktop build has in build/share/. The demo's set is the one
# its WebAssembly page preloads (the root CMakeLists.txt, _uc_demo_preload):
# the media it shows, without the four largest 3D samples and the audio and
# video ones, plus the docs and its own sources for the source viewer.
#
# The Gradle template next to the manifest (UltraCanvas/OS/Android/packaging/)
# is the route for an app with its own Gradle project; this script is what CI
# runs, and it needs nothing a GitHub Ubuntu runner does not have already.
#
# Usage: scripts/android-package-apk.sh --abi ABI --lib PATH --out APK
#            [--ndk DIR] [--sdk DIR] [--version-name X.Y.Z] [--icon PNG]
#   ABI      arm64-v8a or x86_64 (the directory under lib/ in the APK)
#   --lib    the app library, e.g. build-android/lib/libUltraCanvasDemo.so;
#            its name must match the manifest's android.app.lib_name
#   --ndk    defaults to $ANDROID_NDK_HOME / $ANDROID_NDK_LATEST_HOME
#   --sdk    defaults to $ANDROID_SDK_ROOT / $ANDROID_HOME
#   --version-name  defaults to the framework version, line 1 of
#            Docs/UltraCanvas/CHANGELOG.md
#   --icon   the launcher icon, any size or shape; defaults to the demo's,
#            media/appicon/Demo.png. Needs ImageMagick (magick or convert).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
packaging="$repo/UltraCanvas/OS/Android/packaging"
java_root="$repo/UltraCanvas/OS/Android/java"
min_sdk=28      # what the dependency sysroot is built for (packaging/README.md)
target_sdk=34

abi="" lib="" out="" ndk="" sdk="" version_name="" icon="$repo/media/appicon/Demo.png"
while [ $# -gt 0 ]; do
    case "$1" in
        --abi) abi="$2"; shift 2 ;;
        --lib) lib="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --ndk) ndk="$2"; shift 2 ;;
        --sdk) sdk="$2"; shift 2 ;;
        --version-name) version_name="$2"; shift 2 ;;
        --icon) icon="$2"; shift 2 ;;
        *) echo "android-package-apk: unknown argument $1" >&2; exit 2 ;;
    esac
done
die() { echo "android-package-apk: $*" >&2; exit 2; }
[ -n "$abi" ] && [ -n "$lib" ] && [ -n "$out" ] || die "--abi, --lib and --out are required"
[ -f "$lib" ] || die "no app library at $lib"
case "$abi" in arm64-v8a|x86_64|armeabi-v7a|x86) ;; *) die "unknown ABI $abi" ;; esac

ndk="${ndk:-${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}}"
sdk="${sdk:-${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/local/lib/android/sdk}}}"
[ -d "$ndk" ] || die "no NDK (pass --ndk or set ANDROID_NDK_HOME)"
[ -d "$sdk/build-tools" ] || die "no SDK build-tools under $sdk (pass --sdk)"

# The newest build-tools and platform the SDK has; the platform must be at
# least targetSdk, since aapt2 links the manifest against it.
build_tools="$(ls -d "$sdk"/build-tools/*/ | sort -V | tail -1)"
android_jar="$(ls -d "$sdk"/platforms/android-*/android.jar | sort -V | tail -1)"
platform_level="$(basename "$(dirname "$android_jar")")"; platform_level="${platform_level#android-}"
platform_level="${platform_level%%[!0-9]*}"
[ "${platform_level:-0}" -ge "$target_sdk" ] ||
    die "newest platform is android-$platform_level, need $target_sdk or later"
for tool in aapt2 d8 zipalign apksigner; do
    [ -x "$build_tools/$tool" ] || die "$build_tools has no $tool"
done
strip="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
[ -x "$strip" ] || die "no llvm-strip in the NDK at $strip"
readelf="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
[ -x "$readelf" ] || die "no llvm-readelf in the NDK at $readelf"
case "$abi" in
    arm64-v8a)   triple=aarch64-linux-android ;;
    armeabi-v7a) triple=arm-linux-androideabi ;;
    x86_64)      triple=x86_64-linux-android ;;
    x86)         triple=i686-linux-android ;;
esac
ndk_libs="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$triple"
[ -f "$icon" ] || die "no icon at $icon"
magick="$(command -v magick || command -v convert || true)"
[ -n "$magick" ] || die "ImageMagick not found (magick or convert), needed for the launcher icon"

if [ -z "$version_name" ]; then
    version_name="$(head -1 "$repo/Docs/UltraCanvas/CHANGELOG.md" | grep -o '\*[0-9][0-9.]*\*' | tr -d '*')"
fi
[[ "$version_name" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "version name '$version_name' is not X.Y.Z"
# 0.9.351 -> 9351: rises with every release, as Android requires of an update.
version_code=$(( BASH_REMATCH[1] * 1000000 + BASH_REMATCH[2] * 1000 + BASH_REMATCH[3] ))

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "android-package-apk: $abi, version $version_name ($version_code)"
echo "  build-tools $(basename "$build_tools"), platform android-$platform_level"

# ---- 1. Java -> dex. Same flags as scripts/android-java-check.sh.
mapfile -t sources < <(find "$java_root" -name '*.java' | sort)
mkdir -p "$work/classes" "$work/dex"
javac -nowarn -Xlint:-options -bootclasspath "$android_jar" -source 8 -target 8 \
      -d "$work/classes" "${sources[@]}"
mapfile -t class_files < <(find "$work/classes" -name '*.class')
"$build_tools/d8" --release --min-api "$min_sdk" --lib "$android_jar" \
      --output "$work/dex" "${class_files[@]}"

# ---- 2. Assets, laid out as $HOME/share expects them.
assets="$work/assets"
mkdir -p "$assets/DemoApp"
for path in media/fonts media/icons media/appicon media/images media/desktop media/vector \
            media/flags media/textsamples media/ebooks media/diagrams media/docs \
            media/3D/3DS media/3D/Alembic media/3D/COLLADA media/3D/DWG \
            media/3D/DXF media/3D/FBX media/3D/MS3D media/3D/OBJ media/3D/STEP \
            media/3D/X3D media/3D/XFile media/3D/spring.obj media/3D/torusknot.obj \
            Docs/UltraCanvas Docs/Modules; do
    [ -e "$repo/$path" ] || continue
    mkdir -p "$assets/$(dirname "$path")"
    cp -R "$repo/$path" "$assets/$path"
done
cp "$repo"/Apps/DemoApp/*.cpp "$repo"/Apps/DemoApp/*.h "$assets/DemoApp/"
echo "  assets $(du -sh "$assets" | cut -f1)"

# ---- 3. The launcher icon the manifest names (@mipmap/ic_launcher): one
# square PNG per screen density, the source scaled to fit and centred on
# transparency, so an icon that is not square is not stretched. The launcher
# picks the density of the screen; 192 px is xxxhdpi, the largest.
for density in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
    px="${density#*:}"
    mkdir -p "$work/res/mipmap-${density%%:*}"
    "$magick" "$icon" -background none -resize "${px}x${px}" -gravity center \
              -extent "${px}x${px}" "PNG32:$work/res/mipmap-${density%%:*}/ic_launcher.png"
done
"$build_tools/aapt2" compile --dir "$work/res" -o "$work/res.zip"

# ---- 4. Manifest, icon and assets, then the dex and the stripped library.
"$build_tools/aapt2" link -o "$work/base.apk" \
      --manifest "$packaging/AndroidManifest.xml" -I "$android_jar" \
      --min-sdk-version "$min_sdk" --target-sdk-version "$target_sdk" \
      --version-code "$version_code" --version-name "$version_name" \
      -A "$assets" "$work/res.zip"
mkdir -p "$work/stage/lib/$abi"
cp "$work/dex/classes.dex" "$work/stage/"
"$strip" --strip-unneeded -o "$work/stage/lib/$abi/$(basename "$lib")" "$lib"
echo "  $(basename "$lib") $(du -h "$lib" | cut -f1) -> $(du -h "$work/stage/lib/$abi/$(basename "$lib")" | cut -f1) stripped"

# The libraries it needs. A system library of the minSdk platform (libc,
# liblog, libandroid, libEGL ...) is on every device; the NDK's runtimes
# (libc++_shared.so) are not, and an app ships its own copy - without it the
# activity's dlopen fails before a line of the app runs ("library
# libc++_shared.so not found", the emulator's first run). Anything that is
# neither stops the packaging here rather than on a device.
mapfile -t needed < <("$readelf" -d "$lib" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p')
for dep in "${needed[@]}"; do
    if [ -e "$ndk_libs/$min_sdk/$dep" ]; then
        continue                                   # the platform's
    elif [ -e "$ndk_libs/$dep" ]; then
        cp "$ndk_libs/$dep" "$work/stage/lib/$abi/"   # the NDK's runtime
        echo "  + $dep (from the NDK)"
    else
        die "$(basename "$lib") needs $dep, which is neither an API $min_sdk system library nor in the NDK"
    fi
done
# The library is stored, not deflated, and zipalign below puts it on a page
# boundary, so Android can map it straight from the APK.
(cd "$work/stage" && zip -q "$work/base.apk" classes.dex && zip -q -0 -r "$work/base.apk" lib)

# ---- 5. Align and sign. A debug key made here: an APK for a test install,
# not for a store (a release key is the publisher's, and never in CI).
# -P 16 aligns stored libraries to 16 KB pages (build-tools 35 and later);
# older zipalign knows only 4 KB pages (-p).
"$build_tools/zipalign" -f -P 16 4 "$work/base.apk" "$work/aligned.apk" 2>/dev/null ||
    "$build_tools/zipalign" -f -p 4 "$work/base.apk" "$work/aligned.apk"
keytool -genkeypair -keystore "$work/debug.keystore" -storepass android \
        -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 \
        -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
mkdir -p "$(dirname "$out")"
"$build_tools/apksigner" sign --ks "$work/debug.keystore" --ks-pass pass:android \
        --key-pass pass:android --out "$out" "$work/aligned.apk"

# What a device will see: the signature, the package, the SDK levels and the
# native code it declares.
"$build_tools/apksigner" verify --min-sdk-version "$min_sdk" "$out"
"$build_tools/aapt2" dump badging "$out" |
    grep -E "^(package|minSdkVersion|sdkVersion|targetSdkVersion|application-icon-[0-9]+|launchable-activity|native-code):" || true
echo "android-package-apk: $out ($(du -h "$out" | cut -f1))"
