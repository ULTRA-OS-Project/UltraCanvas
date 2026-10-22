# UltraCanvas/Plugins/Vectorizer/CMakeLists.txt
# Vectorizer Plugin (VTracer via Rust shim) - Builds as separate static library
# Version: 0.1.3
# Last Modified: 2026-10-21
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

# ===== RUST TOOLCHAIN VIA CORROSION =====
# A native Rust toolchain on Windows (the MSYS2 POSIX one breaks the build)
# and Corrosion itself: cmake/UltraCanvasRust.cmake, shared with the Matrix
# provider's Olm shim.
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/UltraCanvasRust.cmake)
ultracanvas_find_rust_toolchain(_uc_rust_ok _uc_rust_why)
if(NOT _uc_rust_ok)
    message(FATAL_ERROR "The Vectorizer plugin needs a NATIVE Windows Rust toolchain. ${_uc_rust_why}\n"
        "Or disable this plugin:  cmake -DULTRACANVAS_PLUGIN_VECTORIZER=OFF ...")
endif()
ultracanvas_fetch_corrosion()

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
# The Rust staticlib is an imported target Corrosion defines in this build
# tree, which an installed consumer cannot see: in the package the plug-in
# names the installed archive by path instead (see INSTALLATION below), and
# the system libraries rustc's native-static-libs would have carried.
include(GNUInstallDirs)
set(_uc_vtracer_archive
    "${CMAKE_STATIC_LIBRARY_PREFIX}vtracer_c${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_uc_vtracer_native_libs "")
if(UNIX AND NOT APPLE)
    set(_uc_vtracer_native_libs pthread dl m rt util)
endif()
target_link_libraries(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${VTRACER_C_TARGET}>
        "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${_uc_vtracer_archive}>"
        "$<INSTALL_INTERFACE:${_uc_vtracer_native_libs}>"
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
# Part of the framework's package (UltraCanvas/CMakeLists.txt, INSTALLATION
# AND THE CMAKE PACKAGE): imported as UltraCanvas::${PLUGIN_NAME} by
# find_package(UltraCanvas). GNUInstallDirs is included here too because this
# directory also configures standalone.
include(GNUInstallDirs)
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/UltraCanvasVectorizerPlugin.h
    DESTINATION include/ultracanvas/plugins/vectorizer
)
# The staticlib itself. Corrosion wraps the imported archive as
# <crate>-static behind an INTERFACE target of the crate's name.
if(TARGET ${VTRACER_C_TARGET}-static)
    set(_uc_vtracer_file_target ${VTRACER_C_TARGET}-static)
else()
    set(_uc_vtracer_file_target ${VTRACER_C_TARGET})
endif()
install(FILES $<TARGET_FILE:${_uc_vtracer_file_target}>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RENAME ${_uc_vtracer_archive}
)

# ===== STATUS OUTPUT =====
message(STATUS "  VTracer crate: vtracer 0.6.11 (pinned in vtracer-c/Cargo.toml)")
message(STATUS "  Output library: lib${PLUGIN_NAME}.a (+ rust staticlib)")
