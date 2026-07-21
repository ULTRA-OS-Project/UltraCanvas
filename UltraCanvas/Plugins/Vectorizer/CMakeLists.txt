# UltraCanvas/Plugins/Vectorizer/CMakeLists.txt
# Vectorizer Plugin (VTracer via Rust shim) - Builds as separate static library
# Version: 0.1.2
# Last Modified: 2026-07-21
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.22)

set(PLUGIN_NAME "UltraCanvasVectorizerPlugin")
set(PLUGIN_VERSION "0.1.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== FIND DEPENDENCIES =====
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
endif()
pkg_check_modules(LIBVIPS REQUIRED vips vips-cpp)
pkg_check_modules(GLIB    REQUIRED glib-2.0)

# ===== SELECT A NATIVE WINDOWS RUST TOOLCHAIN =====
# The Rust programs shipped in MSYS2's ".../usr/bin" tree are Cygwin/POSIX
# binaries: they use ':' as the PATH-list separator (Unix semantics). When
# cargo/rustc append their own "<target-dir>/release/deps" build directory to
# the dynamic-library path, the drive letter in a native path like
# "C:/msys64/..." is mistaken for a separator and the build aborts with:
#     error: failed to join paths from `$LD_LIBRARY_PATH` together
#     Caused by: path segment contains separator `:`
# This cannot be worked around via the environment (cargo appends that path
# itself, so even an empty LD_LIBRARY_PATH still yields a lone "C:/..." segment
# that fails to join). The cure is a *native* Windows Rust toolchain - rustup or
# the mingw-w64 package - which uses Windows path semantics and matches the
# MinGW C/C++ compiler CMake already uses. Steer Corrosion at one, and if only
# the POSIX msys toolchain is present, fail now with actionable instructions
# instead of the cryptic build-time error above.
if(WIN32 AND NOT DEFINED Rust_COMPILER)
    get_filename_component(_uc_cc_bin "${CMAKE_C_COMPILER}" DIRECTORY)  # e.g. .../mingw64/bin
    set(_uc_rust_hints
        "${_uc_cc_bin}"                  # mingw-w64-x86_64-rust: native gnu, next to the compiler
        "$ENV{USERPROFILE}/.cargo/bin"   # rustup (native Windows install)
        "$ENV{HOME}/.cargo/bin"          # rustup (installed from an MSYS2/MinGW HOME)
    )
    # Re-search every configure so installing the toolchain after a failed run
    # is picked up (find_program otherwise caches a NOTFOUND result).
    unset(_UC_NATIVE_RUSTC CACHE)
    unset(_UC_NATIVE_CARGO CACHE)
    find_program(_UC_NATIVE_RUSTC rustc HINTS ${_uc_rust_hints} NO_DEFAULT_PATH)
    find_program(_UC_NATIVE_CARGO cargo HINTS ${_uc_rust_hints} NO_DEFAULT_PATH)
    if(_UC_NATIVE_RUSTC AND _UC_NATIVE_CARGO)
        set(Rust_COMPILER "${_UC_NATIVE_RUSTC}" CACHE FILEPATH "rustc used by Corrosion" FORCE)
        set(Rust_CARGO    "${_UC_NATIVE_CARGO}" CACHE FILEPATH "cargo used by Corrosion" FORCE)
        message(STATUS "  Vectorizer: native Rust toolchain -> ${_UC_NATIVE_CARGO}")
    else()
        message(FATAL_ERROR
            "The Vectorizer plugin needs a NATIVE Windows Rust toolchain matching the MinGW "
            "compiler, but only the MSYS2 POSIX rust (in .../usr/bin) appears to be installed. "
            "That toolchain aborts the build with 'failed to join paths from $LD_LIBRARY_PATH' "
            "because it treats the ':' in Windows paths as a path separator.\n"
            "Install a native toolchain, then re-run CMake:\n"
            "  * from the MSYS2 MINGW64 shell:  pacman -S mingw-w64-x86_64-rust\n"
            "  * or via rustup:  rustup toolchain install stable-x86_64-pc-windows-gnu\n"
            "                    rustup default        stable-x86_64-pc-windows-gnu\n"
            "If it lives elsewhere, point CMake at it:  -DRust_COMPILER=<path-to-rustc.exe>\n"
            "Or disable this plugin:  cmake -DULTRACANVAS_PLUGIN_VECTORIZER=OFF ...")
    endif()
    unset(_uc_rust_hints)
endif()

# ===== RUST TOOLCHAIN VIA CORROSION =====
include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG        v0.5.1
)
FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vtracer-c/Cargo.toml
    PROFILE       release
    CRATES        vtracer_c
)

# Corrosion exposes the staticlib as the CMake target `vtracer_c`.
set(VTRACER_C_TARGET vtracer_c)

# The Rust staticlib pulls in additional system libraries (pthread, dl, m on
# Linux; ntdll, userenv, ws2_32, ... on Windows). Corrosion normally propagates
# these from `rustc --print=native-static-libs` via INTERFACE_LINK_LIBRARIES,
# but on the *-pc-windows-gnu target that list can come through empty/stale, so
# the Windows system libs are linked explicitly below (see LINK LIBRARIES).

# ===== SOURCE FILES =====
set(VECTORIZER_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UltraCanvasVectorizerPlugin.cpp
)
set(VECTORIZER_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/UltraCanvasVectorizerPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vtracer-c/include/ultracanvas_vtracer.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${VECTORIZER_PLUGIN_SOURCES}
    ${VECTORIZER_PLUGIN_HEADERS}
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_vectorizer_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/ultracanvas/plugins/vectorizer>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vtracer-c/include
        ${LIBVIPS_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
)
if(DEFINED ULTRACANVAS_INCLUDE_DIR)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
endif()

# ===== LINK LIBRARIES =====
target_link_directories(${PLUGIN_NAME}
    PUBLIC ${LIBVIPS_LIBRARY_DIRS} ${GLIB_LIBRARY_DIRS}
)
target_link_libraries(${PLUGIN_NAME}
    PUBLIC
        ${VTRACER_C_TARGET}
        ${LIBVIPS_LIBRARIES}
        ${GLIB_LIBRARIES}
    PRIVATE
        UltraCanvas   # core: declares the link edge so the static plugin's core
                      # symbol references resolve at final link (GNU/MinGW ld order).
)

# Rust's std for x86_64-pc-windows-gnu references native-API symbols that MinGW
# does not link by default, e.g. NtWriteFile / RtlNtStatusToDosError (ntdll),
# GetUserProfileDirectory (userenv), BCrypt* (bcrypt), sockets (ws2_32), and
# backtrace symbolisation (dbghelp). Corrosion is supposed to supply these but
# the auto-detected list can be empty, so link them explicitly. PUBLIC + placed
# after vtracer_c so single-pass GNU ld sees the providers after libvtracer_c.a.
if(WIN32)
    target_link_libraries(${PLUGIN_NAME} PUBLIC
        ntdll userenv ws2_32 dbghelp bcrypt
    )
endif()

# ===== COMPILER SETTINGS =====
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)
target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        ULTRACANVAS_HAS_VECTORIZER_PLUGIN=1
        ULTRACANVAS_VECTORIZER_SUPPORT=1
    PRIVATE
        ULTRACANVAS_VECTORIZER_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_VECTORIZER_PLUGIN_FOUND       TRUE                          PARENT_SCOPE)
set(ULTRACANVAS_VECTORIZER_PLUGIN_TARGET      ${PLUGIN_NAME}                PARENT_SCOPE)
set(ULTRACANVAS_VECTORIZER_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasVectorizerPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/UltraCanvasVectorizerPlugin.h
    DESTINATION include/ultracanvas/plugins/vectorizer
)

# ===== STATUS OUTPUT =====
message(STATUS "  VTracer crate: vtracer 0.6.11 (pinned in vtracer-c/Cargo.toml)")
message(STATUS "  Output library: lib${PLUGIN_NAME}.a (+ rust staticlib)")
