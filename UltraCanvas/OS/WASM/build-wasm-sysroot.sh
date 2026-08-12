#!/usr/bin/env bash
# Build the wasm sysroot UltraCanvas needs under emcmake: cairo (pixman),
# pango (glib, fribidi, harfbuzz), fontconfig, freetype, tinyxml2 and libvips,
# plus their transitive deps (zlib, libpng, expat, libffi, pcre2).
#
# Where a dependency is also built by kleisauke/wasm-vips, the recipe below
# follows that project (including its pre-patched glib/libvips branches).
#
# Requirements: an activated emsdk (source emsdk_env.sh), meson, ninja, cmake,
# gperf, python3. Network access to github.com and (for the freedesktop
# tarballs) either their release hosts or an Ubuntu 24.04 deb-src mirror.
#
# Usage:
#   source /path/to/emsdk/emsdk_env.sh
#   ./build-wasm-sysroot.sh [sysroot-dir] [work-dir]
#
# Idempotent: each stage is skipped when its pkg-config file already exists.
set -euo pipefail

command -v emcc >/dev/null || { echo "emcc not on PATH — source emsdk_env.sh first" >&2; exit 1; }

export SYSROOT=${1:-$PWD/wasm-sysroot}
WORK=${2:-$PWD/wasm-sysroot-build}
DEPS=$WORK/deps
mkdir -p "$DEPS" "$SYSROOT"

# --- toolchain environment ---------------------------------------------------
# The whole sysroot is compiled with threads and wasm exception handling; the
# UltraCanvas library build (see CMakeLists.txt WASM branch) uses the same ABI.
export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig:$SYSROOT/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export EM_PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export PKG_CONFIG="pkg-config --static"
export CPATH="$SYSROOT/include"
export CFLAGS="-O2 -pthread -fwasm-exceptions -fvisibility=hidden -msimd128"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-pthread -fwasm-exceptions -L$SYSROOT/lib"
export CHOST=wasm32-unknown-linux
export MAKEFLAGS="-j$(nproc)"

# pkg-config wrapper confined to the sysroot (meson invokes it without env)
cat > "$WORK/wasm-pkg-config" <<EOF
#!/bin/sh
export PKG_CONFIG_LIBDIR=$SYSROOT/lib/pkgconfig:$SYSROOT/share/pkgconfig
unset PKG_CONFIG_PATH
exec pkg-config "\$@"
EOF
chmod +x "$WORK/wasm-pkg-config"

EMSCRIPTEN_DIR=$(dirname "$(command -v emcc)")
NODE_BIN=$(command -v node)
cat > "$WORK/emscripten-cross.ini" <<EOF
[binaries]
c = '$EMSCRIPTEN_DIR/emcc'
cpp = '$EMSCRIPTEN_DIR/em++'
ar = '$EMSCRIPTEN_DIR/emar'
ranlib = '$EMSCRIPTEN_DIR/emranlib'
strip = '$EMSCRIPTEN_DIR/emstrip'
nm = '$EMSCRIPTEN_DIR/emnm'
pkg-config = ['$WORK/wasm-pkg-config', '--static']
exe_wrapper = '$NODE_BIN'

[built-in options]
c_args = ['-O2', '-pthread', '-fwasm-exceptions', '-fvisibility=hidden', '-msimd128']
c_link_args = ['-pthread', '-fwasm-exceptions']
cpp_args = ['-O2', '-pthread', '-fwasm-exceptions', '-fvisibility=hidden', '-msimd128']
cpp_link_args = ['-pthread', '-fwasm-exceptions']
default_library = 'static'
# Keep -sPTHREAD_POOL_SIZE out of generated .pc files
c_thread_count = 0
cpp_thread_count = 0

[host_machine]
system = 'emscripten'
cpu_family = 'wasm32'
cpu = 'wasm32'
endian = 'little'

[properties]
needs_exe_wrapper = true
EOF
MESON_ARGS="--cross-file=$WORK/emscripten-cross.ini --prefix=$SYSROOT --default-library=static --buildtype=release"

stage() { echo -e "\n===== $1 ====="; }

# Fetch an Ubuntu 24.04 source package when the upstream (freedesktop/GNOME)
# release host is unreachable. Requires deb-src entries in apt sources.
apt_source() { (cd "$1" && apt-get source "$2" >/dev/null); }

# --- pcre2 (glib regex) -------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/libpcre2-8.pc" ] || (
  stage pcre2
  mkdir -p $DEPS/pcre2 && cd $DEPS/pcre2
  [ -f configure ] || curl -Ls https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.bz2 | tar xjC . --strip-components=1
  emconfigure ./configure --host=$CHOST --prefix=$SYSROOT --enable-static --disable-shared \
    --disable-dependency-tracking --disable-pcre2grep-callout --disable-pcre2grep-jit
  make install
)

# --- zlib-ng (zlib compat) ----------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/zlib.pc" ] || (
  stage zlib-ng
  [ -d $DEPS/zlib-ng ] || git clone --depth 1 --branch 2.3.3 https://github.com/zlib-ng/zlib-ng.git $DEPS/zlib-ng
  cd $DEPS/zlib-ng
  emcmake cmake -B_build -S. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SYSROOT \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DZLIB_COMPAT=ON \
    -DWITH_RUNTIME_CPU_DETECTION=OFF -DWITH_OPTIM=OFF
  make -C _build install
)

# --- libffi (gobject) -----------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/libffi.pc" ] || (
  stage libffi
  mkdir -p $DEPS/ffi && cd $DEPS/ffi
  [ -f configure ] || curl -Ls https://github.com/libffi/libffi/releases/download/v3.7.1/libffi-3.7.1.tar.gz | tar xzC . --strip-components=1
  sed -i 's/ -fexceptions//g' configure
  emconfigure ./configure --host=$CHOST --prefix=$SYSROOT --enable-static --disable-shared \
    --disable-dependency-tracking --disable-builddir --disable-multi-os-directory \
    --disable-raw-api --disable-structs --disable-docs
  make install
)

# --- expat ----------------------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/expat.pc" ] || (
  stage expat
  mkdir -p $DEPS/expat && cd $DEPS/expat
  [ -f configure ] || curl -Ls https://github.com/libexpat/libexpat/releases/download/R_2_8_2/expat-2.8.2.tar.xz | tar xJC . --strip-components=1
  emconfigure ./configure --host=$CHOST --prefix=$SYSROOT --enable-static --disable-shared \
    --disable-dependency-tracking --without-xmlwf --without-docbook --without-examples \
    --without-tests --without-arc4random --without-arc4random-buf \
    --without-getrandom --without-sys-getrandom
  make install dist_cmake_DATA= nodist_cmake_DATA=
)

# --- glib (kleisauke's pre-patched emscripten branch) ---------------------------
[ -f "$SYSROOT/lib/pkgconfig/glib-2.0.pc" ] || (
  stage glib
  [ -d $DEPS/glib ] || git clone --depth 1 --branch wasm-vips-2.89.3 https://github.com/kleisauke/glib.git $DEPS/glib
  cd $DEPS/glib
  [ -d subprojects/gvdb/.git ] || git clone --depth 1 https://github.com/GNOME/gvdb.git subprojects/gvdb
  meson setup _build $MESON_ARGS --force-fallback-for=gvdb \
    -Dintrospection=disabled -Dselinux=disabled -Dxattr=false -Dlibmount=disabled \
    -Dsysprof=disabled -Dnls=disabled -Dglib_debug=disabled -Dtests=false \
    -Dglib_assert=false -Dglib_checks=false
  meson install -C _build --tag devel
)

# --- libpng ----------------------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/libpng.pc" ] || (
  stage libpng
  [ -d $DEPS/libpng ] || git clone --depth 1 --branch v1.6.58 https://github.com/pnggroup/libpng.git $DEPS/libpng
  cd $DEPS/libpng
  emcmake cmake -B_build -S. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SYSROOT \
    -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF \
    -DPNG_HARDWARE_OPTIMIZATIONS=OFF -DCMAKE_FIND_ROOT_PATH=$SYSROOT \
    -DZLIB_LIBRARY=$SYSROOT/lib/libz.a -DZLIB_INCLUDE_DIR=$SYSROOT/include
  make -C _build install
)

# --- freetype (no harfbuzz to avoid the circular dep) -----------------------------
[ -f "$SYSROOT/lib/pkgconfig/freetype2.pc" ] || (
  stage freetype
  [ -d $DEPS/freetype ] || git clone --depth 1 --branch VER-2-13-3 https://github.com/freetype/freetype.git $DEPS/freetype
  cd $DEPS/freetype
  meson setup _build $MESON_ARGS \
    -Dharfbuzz=disabled -Dbrotli=disabled -Dbzip2=disabled -Dpng=disabled \
    -Dzlib=system -Dtests=disabled
  meson install -C _build
)

# --- fontconfig (Ubuntu 24.04 source: 2.15.0, meson) -------------------------------
[ -f "$SYSROOT/lib/pkgconfig/fontconfig.pc" ] || (
  stage fontconfig
  mkdir -p $DEPS/fontconfig-src
  [ -d $DEPS/fontconfig-src/fontconfig-2.15.0 ] || apt_source $DEPS/fontconfig-src fontconfig
  cd $DEPS/fontconfig-src/fontconfig-2.15.0
  rm -rf _build
  meson setup _build $MESON_ARGS \
    -Ddoc=disabled -Dnls=disabled -Dtests=disabled -Dtools=disabled \
    -Dcache-build=disabled -Diconv=disabled
  meson install -C _build
)

# --- pixman (Ubuntu 24.04 source: 0.42.2, meson) ------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/pixman-1.pc" ] || (
  stage pixman
  mkdir -p $DEPS/pixman-src
  [ -d $DEPS/pixman-src/pixman-0.42.2 ] || apt_source $DEPS/pixman-src pixman
  cd $DEPS/pixman-src/pixman-0.42.2
  rm -rf _build
  meson setup _build $MESON_ARGS -Dtests=disabled -Dgtk=disabled -Dlibpng=disabled
  meson install -C _build
)

# --- cairo (Ubuntu 24.04 source: 1.18.0, meson) --------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/cairo.pc" ] || (
  stage cairo
  mkdir -p $DEPS/cairo-src
  [ -d $DEPS/cairo-src/cairo-1.18.0 ] || apt_source $DEPS/cairo-src cairo
  cd $DEPS/cairo-src/cairo-1.18.0
  rm -rf _build
  meson setup _build $MESON_ARGS \
    -Dtests=disabled -Dxlib=disabled -Dxcb=disabled -Dquartz=disabled -Ddwrite=disabled \
    -Dfontconfig=enabled -Dfreetype=enabled -Dpng=enabled -Dzlib=enabled \
    -Dglib=disabled -Dspectre=disabled -Dtee=disabled -Dsymbol-lookup=disabled
  meson install -C _build
)

# --- fribidi ---------------------------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/fribidi.pc" ] || (
  stage fribidi
  mkdir -p $DEPS/fribidi && cd $DEPS/fribidi
  [ -f meson.build ] || curl -Ls https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz | tar xJC . --strip-components=1
  rm -rf _build
  meson setup _build $MESON_ARGS -Ddocs=false -Dtests=false -Dbin=false
  meson install -C _build
)

# --- harfbuzz ---------------------------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/harfbuzz.pc" ] || (
  stage harfbuzz
  mkdir -p $DEPS/harfbuzz && cd $DEPS/harfbuzz
  [ -f meson.build ] || curl -Ls https://github.com/harfbuzz/harfbuzz/releases/download/12.1.0/harfbuzz-12.1.0.tar.xz | tar xJC . --strip-components=1
  rm -rf _build
  # hb.hh promotes warnings to errors via pragmas; emscripten's newer clang
  # trips -Wunused-template, so disable the pragma block.
  meson setup _build $MESON_ARGS \
    "-Dcpp_args=-O2 -pthread -fwasm-exceptions -fvisibility=hidden -msimd128 -DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR" \
    -Dfreetype=enabled -Dglib=enabled -Dgobject=disabled -Dicu=disabled \
    -Dcairo=disabled -Dchafa=disabled -Dtests=disabled -Ddocs=disabled \
    -Dintrospection=disabled -Dbenchmark=disabled
  meson install -C _build
)

# --- pango (Ubuntu 24.04 source: 1.52.1, meson) --------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/pango.pc" ] || (
  stage pango
  mkdir -p $DEPS/pango-src
  [ -d $DEPS/pango-src/pango1.0-1.52.1+ds ] || apt_source $DEPS/pango-src pango1.0
  cd $DEPS/pango-src/pango1.0-1.52.1+ds
  rm -rf _build
  meson setup _build $MESON_ARGS \
    -Dintrospection=disabled -Dbuild-testsuite=false -Dbuild-examples=false \
    -Dcairo=enabled -Dfontconfig=enabled -Dfreetype=enabled -Dxft=disabled \
    -Dlibthai=disabled -Dsysprof=disabled
  meson install -C _build
)

# --- tinyxml2 --------------------------------------------------------------------------------
[ -f "$SYSROOT/lib/pkgconfig/tinyxml2.pc" ] || (
  stage tinyxml2
  [ -d $DEPS/tinyxml2 ] || git clone --depth 1 --branch 11.0.0 https://github.com/leethomason/tinyxml2.git $DEPS/tinyxml2
  cd $DEPS/tinyxml2
  emcmake cmake -B_build -S. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SYSROOT \
    -DBUILD_SHARED_LIBS=OFF -Dtinyxml2_BUILD_TESTING=OFF
  make -C _build install
)

# --- libvips (kleisauke's pre-patched emscripten branch; C++ API on for vips-cpp) -------------
[ -f "$SYSROOT/lib/pkgconfig/vips.pc" ] || (
  stage libvips
  [ -d $DEPS/vips ] || git clone --depth 1 --branch wasm-vips-8.18.5 https://github.com/kleisauke/libvips.git $DEPS/vips
  cd $DEPS/vips
  # Skip man pages / po / tools / tests subdirs like wasm-vips does
  sed -i "/subdir('man')/{N;N;N;N;d;}" meson.build
  rm -rf _build
  meson setup _build $MESON_ARGS \
    -Ddeprecated=false -Dexamples=false -Dcplusplus=true -Dauto_features=disabled \
    -Dintrospection=disabled -Dmodules=disabled -Dpng=enabled -Dzlib=enabled
  meson install -C _build --tag runtime,devel
)

stage "DONE — sysroot pkg-config modules"
ls "$SYSROOT/lib/pkgconfig/"
