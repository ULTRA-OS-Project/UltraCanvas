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
#   3. Where no release archive fits - the MSYS2 CLANG64 / CLANGARM64 builds,
#      whose *-windows-gnullvm ABI wasmtime publishes no library for - the
#      C API is built from wasmtime's source release (its Rust dependencies
#      vendored in it, so Cargo builds offline) with the native Rust
#      toolchain the Vectorizer plugin uses too (MSYS2's
#      mingw-w64-clang-*-rust). ULTRACANVAS_WASMTIME_FROM_SOURCE=ON takes
#      this path on any platform (for testing it off Windows).
# No Rust toolchain is needed for 1 and 2. Without one for 3, WasmHost
# builds without an engine and says so.
#
# Version: 1.2.0 - the C API built from the source release where no prebuilt
#                  library fits (MSYS2 CLANG64 / CLANGARM64)
# Version: 1.1.0 - the system zstd ahead of the archive's own on Linux
# Version: 1.0.0
# Last Modified: 2026-10-15
# Author: UltraCanvas Framework / ULTRA OS

set(ULTRACANVAS_WASMTIME_VERSION "49.0.2")
set(ULTRACANVAS_WASMTIME_FOUND FALSE)
# Linked ahead of UltraCanvas::wasmtime by its users (see the Linux branch).
set(ULTRACANVAS_WASMTIME_PRELINK "")
set(ULTRACANVAS_WASMTIME_DIR "" CACHE PATH "An unpacked wasmtime C API (include/, lib/); empty downloads the release")
option(ULTRACANVAS_WASMTIME_FROM_SOURCE "Build wasmtime's C API from its source release with Cargo, even where a prebuilt one exists" OFF)
# The source release (v49.0.2), with its crates vendored.
set(_uc_wt_src_sha256 "8dbfaa56c576471e954e96e24e57acaab2457f68a194a97386ecc2c56f1faba2")

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

if(ULTRACANVAS_WASMTIME_FROM_SOURCE AND NOT ULTRACANVAS_WASMTIME_DIR)
    set(_uc_wt_asset "")
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

# ---- 3. built from the source release ---------------------------------------
if(NOT ULTRACANVAS_WASMTIME_FOUND AND NOT ULTRACANVAS_WASMTIME_DIR
   AND (ULTRACANVAS_WASMTIME_FROM_SOURCE OR (MINGW AND NOT _uc_wt_asset)))
    # A native Rust toolchain: next to the C compiler (MSYS2's
    # mingw-w64-clang-*-rust), else rustup's. MSYS2's /usr/bin POSIX rust
    # cannot build for Windows (see the Vectorizer plugin).
    get_filename_component(_uc_wt_cc_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    unset(_UC_WT_CARGO CACHE)
    unset(_UC_WT_RUSTC CACHE)
    find_program(_UC_WT_CARGO cargo HINTS "${_uc_wt_cc_bin}" "$ENV{USERPROFILE}/.cargo/bin" "$ENV{HOME}/.cargo/bin")
    find_program(_UC_WT_RUSTC rustc HINTS "${_uc_wt_cc_bin}" "$ENV{USERPROFILE}/.cargo/bin" "$ENV{HOME}/.cargo/bin")
    set(_uc_wt_target "")
    if(_UC_WT_CARGO AND _UC_WT_RUSTC)
        execute_process(COMMAND "${_UC_WT_RUSTC}" -vV OUTPUT_VARIABLE _uc_wt_rustc_vv RESULT_VARIABLE _uc_wt_rc)
        if(_uc_wt_rc EQUAL 0 AND _uc_wt_rustc_vv MATCHES "host: ([^ \n]+)")
            set(_uc_wt_target "${CMAKE_MATCH_1}")
        endif()
    endif()
    if(WIN32 AND _uc_wt_target AND NOT _uc_wt_target MATCHES "windows")
        set(_uc_wt_target "")       # a POSIX rust: it would build for Cygwin
    endif()
    if(NOT _uc_wt_target)
        message(WARNING "WasmHost: no wasmtime library for this platform, and no native Rust toolchain "
                        "to build one (install mingw-w64-clang-x86_64-rust, or -aarch64 on ARM), "
                        "so UltraWeb will not run apps")
    else()
        include(FetchContent)
        FetchContent_Declare(ultracanvas_wasmtime_src
            URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${ULTRACANVAS_WASMTIME_VERSION}/wasmtime-v${ULTRACANVAS_WASMTIME_VERSION}-src.tar.gz"
            URL_HASH SHA256=${_uc_wt_src_sha256}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        # The archive has no top-level CMakeLists.txt, so this only
        # downloads and unpacks (its crates/c-api CMake project is not used).
        FetchContent_MakeAvailable(ultracanvas_wasmtime_src)
        set(_uc_wt_capi "${ultracanvas_wasmtime_src_SOURCE_DIR}/crates/c-api")
        set(_uc_wt_cargo_dir "${CMAKE_BINARY_DIR}/wasmtime-cargo")
        if(_uc_wt_target MATCHES "windows-msvc")
            set(_uc_wt_lib "${_uc_wt_cargo_dir}/${_uc_wt_target}/release/wasmtime.lib")
        else()
            set(_uc_wt_lib "${_uc_wt_cargo_dir}/${_uc_wt_target}/release/libwasmtime.a")
        endif()

        # What WasmHost uses, and no more: the compiler, WebAssembly text,
        # WASI preview 1, threads for compiling in parallel, and the GC the
        # exceptions proposal needs. (The release libraries carry every
        # feature; wasi-http among them would bring a TLS stack to build.)
        set(_uc_wt_features cranelift wat wasi parallel-compilation gc gc-drc)
        set(_uc_wt_feature_args "")
        foreach(_f IN LISTS _uc_wt_features)
            list(APPEND _uc_wt_feature_args "--features=${_f}")
            string(TOUPPER "WASMTIME_FEATURE_${_f}" _fvar)
            string(REPLACE "-" "_" _fvar "${_fvar}")
            set(${_fvar} ON)
        endforeach()
        # wasmtime/conf.h says which features the library has.
        configure_file("${_uc_wt_capi}/include/wasmtime/conf.h.in"
                       "${CMAKE_BINARY_DIR}/wasmtime-include/wasmtime/conf.h")

        add_custom_command(
            OUTPUT "${_uc_wt_lib}"
            COMMAND ${CMAKE_COMMAND} -E env
                    CARGO_PROFILE_RELEASE_PANIC=abort
                    "CARGO_TARGET_DIR=${_uc_wt_cargo_dir}"
                    "${_UC_WT_CARGO}" build --release --offline --locked
                    --target ${_uc_wt_target} --package wasmtime-c-api
                    --no-default-features ${_uc_wt_feature_args}
            WORKING_DIRECTORY "${_uc_wt_capi}"
            COMMENT "Building wasmtime ${ULTRACANVAS_WASMTIME_VERSION}'s C API with Cargo (${_uc_wt_target}; a few minutes)"
            USES_TERMINAL
            VERBATIM)
        add_custom_target(ultracanvas_wasmtime_cargo DEPENDS "${_uc_wt_lib}")

        add_library(ultracanvas_wasmtime INTERFACE)
        add_dependencies(ultracanvas_wasmtime ultracanvas_wasmtime_cargo)
        target_include_directories(ultracanvas_wasmtime INTERFACE
            "${_uc_wt_capi}/include" "${CMAKE_BINARY_DIR}/wasmtime-include")
        target_compile_definitions(ultracanvas_wasmtime INTERFACE "WASM_API_EXTERN=" "WASI_API_EXTERN=")
        find_package(Threads REQUIRED)
        if(WIN32)
            target_link_libraries(ultracanvas_wasmtime INTERFACE "${_uc_wt_lib}"
                ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt synchronization)
        elseif(APPLE)
            target_link_libraries(ultracanvas_wasmtime INTERFACE "${_uc_wt_lib}"
                Threads::Threads "-framework CoreFoundation")
        else()
            target_link_libraries(ultracanvas_wasmtime INTERFACE "${_uc_wt_lib}"
                Threads::Threads ${CMAKE_DL_LIBS} m)
        endif()
        add_library(UltraCanvas::wasmtime ALIAS ultracanvas_wasmtime)
        set(ULTRACANVAS_WASMTIME_FOUND TRUE)
        message(STATUS "  WasmHost: wasmtime's C API built from source with ${_UC_WT_CARGO} (${_uc_wt_target})")
    endif()
endif()
