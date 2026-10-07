# cmake/UltraCanvasWasmtime.cmake
# wasmtime's prebuilt C API, for the WasmHost module
# (UltraCanvas/{include,core}/WasmHost). Defines the imported target
# UltraCanvas::wasmtime when a library is available for this platform and
# sets ULTRACANVAS_WASMTIME_FOUND.
#
# Where the library comes from, in order:
#   1. ULTRACANVAS_WASMTIME_DIR - an unpacked wasmtime-<version>-<target>-c-api
#      directory (include/, lib/). For offline builds and CI caches.
#   2. The release archive for this platform, downloaded by FetchContent and
#      checked against the SHA-256 below.
# No Rust toolchain is needed for either. The MSYS2 CLANG64 / CLANGARM64
# builds (the *-windows-gnullvm ABI) have no prebuilt library: there the
# c-api crate would have to be built with Corrosion, as the Vectorizer
# plugin builds its crate - not done yet, so WasmHost builds without an
# engine there (Docs/UltraWeb/UltraWebProposal.md §4.1, §14).
#
# Version: 1.1.0 - the system zstd ahead of the archive's own on Linux
# Version: 1.0.0
# Last Modified: 2026-10-07
# Author: UltraCanvas Framework / ULTRA OS

set(ULTRACANVAS_WASMTIME_VERSION "49.0.2")
set(ULTRACANVAS_WASMTIME_FOUND FALSE)
# Linked ahead of UltraCanvas::wasmtime by its users (see the Linux branch).
set(ULTRACANVAS_WASMTIME_PRELINK "")
set(ULTRACANVAS_WASMTIME_DIR "" CACHE PATH "An unpacked wasmtime C API (include/, lib/); empty downloads the release")

# The release asset for this platform, and its SHA-256 (v49.0.2).
set(_uc_wt_asset "")
set(_uc_wt_sha256 "")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _uc_wt_cpu)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 _uc_wt_cpu)
endif()
if(_uc_wt_cpu MATCHES "^(x86_64|amd64)$")
    set(_uc_wt_cpu "x86_64")
elseif(_uc_wt_cpu MATCHES "^(aarch64|arm64)$")
    set(_uc_wt_cpu "aarch64")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _uc_wt_cpu STREQUAL "x86_64")
    set(_uc_wt_asset "x86_64-linux-c-api.tar.xz")
    set(_uc_wt_sha256 "4818e139aeb83b7adbaaf41b267bf18992c18463ae26d1a9acdcfc63ea9eb316")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _uc_wt_cpu STREQUAL "aarch64")
    set(_uc_wt_asset "aarch64-linux-c-api.tar.xz")
    set(_uc_wt_sha256 "a97a874152b5fb49c4720b39b96a71d00ba61eab5ad59976163b7ddd1a3b14c0")
elseif(APPLE AND _uc_wt_cpu STREQUAL "x86_64")
    set(_uc_wt_asset "x86_64-macos-c-api.tar.xz")
    set(_uc_wt_sha256 "1a1fd4bcec77d65bab1bdd4c38e4168825b7679c2f105bf573c4ef1cf5e779b1")
elseif(APPLE AND _uc_wt_cpu STREQUAL "aarch64")
    set(_uc_wt_asset "aarch64-macos-c-api.tar.xz")
    set(_uc_wt_sha256 "f7000ab1661495d09b9dc87a11b356941b4e6097f295434bcb30a8cf1393b0d5")
elseif(MSVC AND _uc_wt_cpu STREQUAL "x86_64")
    set(_uc_wt_asset "x86_64-windows-c-api.zip")
    set(_uc_wt_sha256 "928dad67acbf4a4f97be8d528b271ac72f8e329da21e327b0724d3af86fe3f44")
elseif(MSVC AND _uc_wt_cpu STREQUAL "aarch64")
    set(_uc_wt_asset "aarch64-windows-c-api.zip")
    set(_uc_wt_sha256 "224fc6db1a214fec8626786b6f5362c47f832490775f9e79e8e9cd0299b48a95")
elseif(MINGW AND _uc_wt_cpu STREQUAL "x86_64" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # The GNU ABI (gcc + libgcc), not MSYS2's CLANG64 gnullvm.
    set(_uc_wt_asset "x86_64-mingw-c-api.zip")
    set(_uc_wt_sha256 "113603c5627adc5ea89d80554434662198aa205d248ba7837c609604ee87ff1c")
endif()

set(_uc_wt_root "")
if(ULTRACANVAS_WASMTIME_DIR)
    set(_uc_wt_root "${ULTRACANVAS_WASMTIME_DIR}")
elseif(_uc_wt_asset)
    include(FetchContent)
    FetchContent_Declare(ultracanvas_wasmtime_c_api
        URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${ULTRACANVAS_WASMTIME_VERSION}/wasmtime-v${ULTRACANVAS_WASMTIME_VERSION}-${_uc_wt_asset}"
        URL_HASH SHA256=${_uc_wt_sha256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    # The archive has no CMakeLists.txt, so this only downloads and unpacks.
    FetchContent_MakeAvailable(ultracanvas_wasmtime_c_api)
    set(_uc_wt_root "${ultracanvas_wasmtime_c_api_SOURCE_DIR}")
endif()

if(_uc_wt_root)
    find_library(_uc_wt_lib NAMES wasmtime.lib libwasmtime.a
                 PATHS "${_uc_wt_root}/lib" NO_DEFAULT_PATH NO_CACHE)
    if(_uc_wt_lib AND EXISTS "${_uc_wt_root}/include/wasmtime.h")
        add_library(UltraCanvas::wasmtime STATIC IMPORTED GLOBAL)
        set_target_properties(UltraCanvas::wasmtime PROPERTIES
            IMPORTED_LOCATION "${_uc_wt_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_uc_wt_root}/include"
            # Static linking: the headers would otherwise declare dllimport
            # on Windows.
            INTERFACE_COMPILE_DEFINITIONS "WASM_API_EXTERN=;WASI_API_EXTERN=")
        find_package(Threads REQUIRED)
        if(WIN32)
            set_property(TARGET UltraCanvas::wasmtime PROPERTY INTERFACE_LINK_LIBRARIES
                ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt)
        elseif(APPLE)
            set_property(TARGET UltraCanvas::wasmtime PROPERTY INTERFACE_LINK_LIBRARIES
                Threads::Threads "-framework CoreFoundation")
        else()
            set_property(TARGET UltraCanvas::wasmtime PROPERTY INTERFACE_LINK_LIBRARIES
                Threads::Threads ${CMAKE_DL_LIBS} m)
            # The release library carries its own zstd, with hidden symbols.
            # Where another shared library on the link line uses the system
            # zstd - libarchive does on Ubuntu 24.04, where libvips-dev pulls
            # it in and VirtualFS links it - GNU ld refuses the executable:
            # "hidden symbol `ZSTD_freeCStream' ... is referenced by DSO".
            # Linked ahead of the archive, the system zstd answers wasmtime's
            # calls and the archive's copy is never pulled in. Only a zstd as
            # new as the one wasmtime bundles (1.5); otherwise nothing changes.
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(_UC_WT_ZSTD QUIET libzstd>=1.5)
                if(_UC_WT_ZSTD_FOUND)
                    find_library(_uc_wt_zstd_lib NAMES zstd
                                 HINTS ${_UC_WT_ZSTD_LIBRARY_DIRS} NO_CACHE)
                    if(_uc_wt_zstd_lib)
                        set(ULTRACANVAS_WASMTIME_PRELINK "${_uc_wt_zstd_lib}")
                    endif()
                endif()
            endif()
        endif()
        set(ULTRACANVAS_WASMTIME_FOUND TRUE)
    else()
        message(WARNING "wasmtime C API not usable at ${_uc_wt_root} (need include/wasmtime.h and lib/)")
    endif()
endif()
