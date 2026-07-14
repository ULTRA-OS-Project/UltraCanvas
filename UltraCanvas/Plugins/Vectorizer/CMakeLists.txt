# UltraCanvas/Plugins/Vectorizer/CMakeLists.txt
# Vectorizer Plugin (VTracer via Rust shim) - Builds as separate static library
# Version: 0.1.0
# Last Modified: 2026-07-14
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

# On some Rust toolchains the staticlib pulls in additional system
# libraries (pthread, dl, m on Linux; userenv, bcrypt, ntdll on Windows).
# Corrosion handles this automatically via its INTERFACE_LINK_LIBRARIES;
# nothing extra to do here.

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
