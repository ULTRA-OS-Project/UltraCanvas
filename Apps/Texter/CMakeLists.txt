# CMakeLists.txt
# UltraTexter - Standalone Text Editor Application
# Version: 1.0.0
# Last Modified: 2026-01-24
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

project(UltraTexter
        VERSION 1.0.0
        DESCRIPTION "UltraTexter - Text Editor powered by UltraCanvas Framework"
        LANGUAGES CXX
)

message(STATUS "")
message(STATUS "========================================")
message(STATUS "   UltraTexter Application Build")
message(STATUS "========================================")
message(STATUS "")

# ===== GLOBAL SETTINGS =====
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ===== FIND REQUIRED PACKAGES =====
find_package(PkgConfig REQUIRED)

# ===== BUILD ULTRACANVAS CORE LIBRARY =====
message(STATUS "")
message(STATUS "── Building UltraCanvas Core Library ──")

if(ULTRACANVAS_LIBRARY)
else ()
    # Check if UltraCanvas is available as subdirectory or find it
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/CMakeLists.txt")
        # UltraCanvas is sibling directory
        #    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/3rdparty/fmt ${CMAKE_BINARY_DIR}/fmt)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas ${CMAKE_BINARY_DIR}/UltraCanvas)
        set(ULTRACANVAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/CMakeLists.txt")
        # UltraTexter is in Apps folder
        #    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/3rdparty/fmt ${CMAKE_BINARY_DIR}/fmt)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas ${CMAKE_BINARY_DIR}/UltraCanvas)
        set(ULTRACANVAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas")
    else()
        message(FATAL_ERROR "UltraCanvas library not found. Please ensure UltraCanvas is available.")
    endif()

    # ===== DETECT PLATFORM =====
    if(WIN32)
        set(ULTRACANVAS_PLATFORM "Windows")
    elseif(APPLE)
        set(ULTRACANVAS_PLATFORM "MacOS")
    elseif(UNIX)
        set(ULTRACANVAS_PLATFORM "Linux")
    endif()
endif ()

message(STATUS "Platform: ${ULTRACANVAS_PLATFORM}")

# ===== PLATFORM-SPECIFIC DEPENDENCIES =====
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    pkg_check_modules(X11 REQUIRED x11)
    pkg_check_modules(CAIRO REQUIRED cairo)
    pkg_check_modules(PANGO REQUIRED pango)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    pkg_check_modules(TINYXML2 REQUIRED tinyxml2)
    pkg_check_modules(VIPS REQUIRED vips-cpp)

    include_directories(
            ${X11_INCLUDE_DIRS}
            ${CAIRO_INCLUDE_DIRS}
            ${PANGO_INCLUDE_DIRS}
            ${VIPS_INCLUDE_DIRS}
            ${PANGOCAIRO_INCLUDE_DIRS}
            ${FREETYPE_INCLUDE_DIRS}
            ${TINYXML2_INCLUDE_DIRS}
    )

    link_directories(
            ${X11_LIBRARY_DIRS}
            ${CAIRO_LIBRARY_DIRS}
            ${PANGO_LIBRARY_DIRS}
            ${PANGOCAIRO_LIBRARY_DIRS}
            ${FREETYPE_LIBRARY_DIRS}
            ${TINYXML2_LIBRARY_DIRS}
            ${VIPS_LIBRARY_DIRS}
    )
elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
    pkg_check_modules(CAIRO REQUIRED cairo)
    pkg_check_modules(PANGO REQUIRED pango)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
    pkg_check_modules(GLIB REQUIRED glib-2.0)
    pkg_check_modules(VIPS REQUIRED vips-cpp)
    pkg_check_modules(TINYXML2 REQUIRED tinyxml2)

    # Homebrew paths
    set(HOMEBREW_PREFIX "/opt/homebrew" CACHE PATH "Homebrew prefix")

    include_directories(
            ${CAIRO_INCLUDE_DIRS}
            ${PANGO_INCLUDE_DIRS}
            ${PANGOCAIRO_INCLUDE_DIRS}
            ${FREETYPE_INCLUDE_DIRS}
            ${HARFBUZZ_INCLUDE_DIRS}
            ${GLIB_INCLUDE_DIRS}
            ${VIPS_INCLUDE_DIRS}
            ${TINYXML2_INCLUDE_DIRS}
            ${HOMEBREW_PREFIX}/include
            ${HOMEBREW_PREFIX}/include/pango-1.0
            ${HOMEBREW_PREFIX}/include/glib-2.0
            ${HOMEBREW_PREFIX}/lib/glib-2.0/include
            ${HOMEBREW_PREFIX}/include/harfbuzz
            ${HOMEBREW_PREFIX}/include/cairo
            ${HOMEBREW_PREFIX}/include/freetype2
    )

    link_directories(
            ${CAIRO_LIBRARY_DIRS}
            ${PANGO_LIBRARY_DIRS}
            ${PANGOCAIRO_LIBRARY_DIRS}
            ${FREETYPE_LIBRARY_DIRS}
            ${HARFBUZZ_LIBRARY_DIRS}
            ${GLIB_LIBRARY_DIRS}
            ${VIPS_LIBRARY_DIRS}
            ${TINYXML2_LIBRARY_DIRS}
            ${HOMEBREW_PREFIX}/lib
    )
endif()

# ===== ULTRATEXTER APPLICATION =====
message(STATUS "")
message(STATUS "── Building UltraTexter Application ──")

# Application source files
set(ULTRATEXTER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasTextEditor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasTextEditorHelpers.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasEncoding.cpp
)

# Create executable
add_executable(UltraTexter
        ${ULTRATEXTER_SOURCES}
)

# Include directories
target_include_directories(UltraTexter PRIVATE
        ${ULTRACANVAS_INCLUDE_DIRS}
        ${ULTRACANVAS_ROOT}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against UltraCanvas core library
target_link_libraries(UltraTexter PRIVATE
        ${ULTRACANVAS_LIBRARY}
)

# Link fmt library
target_link_libraries(UltraTexter PRIVATE fmt::fmt)

# iconv - on Linux it's part of glibc, on macOS it needs explicit linking
if(APPLE)
    target_link_libraries(UltraTexter PRIVATE iconv)
endif()

if(WIN32)
    target_link_libraries(UltraTexter PRIVATE iconv)
endif()

# Link against plugin libraries if available
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraTexter PRIVATE
            ${ULTRACANVAS_PLUGIN_TARGETS}
    )
endif()

# Compile features
target_compile_features(UltraTexter PRIVATE cxx_std_20)

# Compile definitions
target_compile_definitions(UltraTexter PRIVATE
        ULTRATEXTER_VERSION="${PROJECT_VERSION}"
        $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(UltraTexter PRIVATE
            -Wall -Wextra
    )
endif()

# Set executable properties
set_target_properties(UltraTexter PROPERTIES
        OUTPUT_NAME "UltraTexter"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ===== INSTALLATION =====
install(TARGETS UltraTexter
        RUNTIME DESTINATION bin
)

# ===== CONFIGURATION SUMMARY =====
message(STATUS "")
message(STATUS "===============================================================")
message(STATUS "  UltraTexter Configuration Summary")
message(STATUS "===============================================================")
message(STATUS "  Project:        ${PROJECT_NAME}")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform:       ${ULTRACANVAS_PLATFORM}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "===============================================================")
message(STATUS "  Output:         ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/UltraTexter")
message(STATUS "===============================================================")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  cmake --build . --parallel")
message(STATUS "")
