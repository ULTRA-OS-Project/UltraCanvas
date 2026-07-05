# VirtualFS/CMakeLists.txt
# Build configuration for VirtualFS module
# Version: 1.0.0
# Last Modified: 2026-01-10
# Author: ULTRA OS Framework

cmake_minimum_required(VERSION 3.16)

project(VirtualFS
    VERSION 1.0.0
    DESCRIPTION "Virtual File System - Transparent archive access"
    LANGUAGES CXX
)

# ============================================================================
# OPTIONS
# ============================================================================

option(VIRTUALFS_BUILD_SHARED "Build shared library" OFF)
option(VIRTUALFS_BUILD_TESTS "Build unit tests" OFF)
option(VIRTUALFS_BUILD_EXAMPLES "Build examples" OFF)

# Provider options
option(VIRTUALFS_USE_LIBARCHIVE "Use libarchive for multi-format support" ON)
option(VIRTUALFS_USE_ZLIB "Use zlib for basic compression" ON)
option(VIRTUALFS_USE_ZSTD "Use Zstandard compression" OFF)
option(VIRTUALFS_USE_LZ4 "Use LZ4 compression" OFF)
option(VIRTUALFS_USE_BROTLI "Use Brotli compression" OFF)

# ============================================================================
# C++ STANDARD
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# FIND DEPENDENCIES
# ============================================================================

find_package(PkgConfig QUIET)

set(VIRTUALFS_DEFINITIONS "")

# libarchive (primary backend)
if(VIRTUALFS_USE_LIBARCHIVE)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBARCHIVE libarchive)
    endif()
    
    if(NOT LIBARCHIVE_FOUND)
        find_path(LIBARCHIVE_INCLUDE_DIRS archive.h)
        find_library(LIBARCHIVE_LIBRARIES archive)
        if(LIBARCHIVE_INCLUDE_DIRS AND LIBARCHIVE_LIBRARIES)
            set(LIBARCHIVE_FOUND TRUE)
        endif()
    endif()
    
    if(LIBARCHIVE_FOUND)
        message(STATUS "Found libarchive: ${LIBARCHIVE_LIBRARIES}")
        list(APPEND VIRTUALFS_DEFINITIONS VIRTUALFS_HAS_LIBARCHIVE)
    else()
        message(WARNING "libarchive not found - multi-format support disabled")
    endif()
endif()

# zlib
if(VIRTUALFS_USE_ZLIB)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        message(STATUS "Found zlib: ${ZLIB_LIBRARIES}")
        list(APPEND VIRTUALFS_DEFINITIONS VIRTUALFS_HAS_ZLIB)
    endif()
endif()

# Zstandard
if(VIRTUALFS_USE_ZSTD)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZSTD libzstd)
    endif()
    
    if(NOT ZSTD_FOUND)
        find_path(ZSTD_INCLUDE_DIRS zstd.h)
        find_library(ZSTD_LIBRARIES zstd)
        if(ZSTD_INCLUDE_DIRS AND ZSTD_LIBRARIES)
            set(ZSTD_FOUND TRUE)
        endif()
    endif()
    
    if(ZSTD_FOUND)
        message(STATUS "Found Zstandard: ${ZSTD_LIBRARIES}")
        list(APPEND VIRTUALFS_DEFINITIONS VIRTUALFS_HAS_ZSTD)
    endif()
endif()

# LZ4
if(VIRTUALFS_USE_LZ4)
    if(PkgConfig_FOUND)
        pkg_check_modules(LZ4 liblz4)
    endif()
    
    if(LZ4_FOUND)
        message(STATUS "Found LZ4: ${LZ4_LIBRARIES}")
        list(APPEND VIRTUALFS_DEFINITIONS VIRTUALFS_HAS_LZ4)
    endif()
endif()

# Brotli
if(VIRTUALFS_USE_BROTLI)
    if(PkgConfig_FOUND)
        pkg_check_modules(BROTLI libbrotlidec libbrotlienc)
    endif()
    
    if(BROTLI_FOUND)
        message(STATUS "Found Brotli: ${BROTLI_LIBRARIES}")
        list(APPEND VIRTUALFS_DEFINITIONS VIRTUALFS_HAS_BROTLI)
    endif()
endif()

# ============================================================================
# SOURCE FILES
# ============================================================================

set(VIRTUALFS_HEADERS
    include/VirtualFS/VirtualFS.h
    include/VirtualFS/VirtualFSTypes.h
    include/VirtualFS/VirtualFSPath.h
    include/VirtualFS/VirtualFSProvider.h
    include/VirtualFS/VirtualFSManager.h
)

set(VIRTUALFS_SOURCES
    core/VirtualFSManager.cpp
)

# Add provider sources based on available libraries
if(LIBARCHIVE_FOUND)
    list(APPEND VIRTUALFS_SOURCES
        providers/VirtualFSLibArchiveProvider.cpp
    )
    list(APPEND VIRTUALFS_HEADERS
        providers/VirtualFSLibArchiveProvider.h
    )
endif()

# ============================================================================
# LIBRARY TARGET
# ============================================================================

if(VIRTUALFS_BUILD_SHARED)
    add_library(VirtualFS SHARED ${VIRTUALFS_SOURCES} ${VIRTUALFS_HEADERS})
    set_target_properties(VirtualFS PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
    )
else()
    add_library(VirtualFS STATIC ${VIRTUALFS_SOURCES} ${VIRTUALFS_HEADERS})
endif()

# Alias for use in subdirectories
add_library(VirtualFS::VirtualFS ALIAS VirtualFS)

# PUBLIC so consumers compiling against VirtualFS (e.g. the UltraCanvas
# bridge) see the same VIRTUALFS_HAS_* feature set the library was built with
target_compile_definitions(VirtualFS PUBLIC ${VIRTUALFS_DEFINITIONS})

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================

target_include_directories(VirtualFS
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/VirtualFS
        ${CMAKE_CURRENT_SOURCE_DIR}/providers
)

if(LIBARCHIVE_FOUND)
    target_include_directories(VirtualFS PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
endif()

if(ZLIB_FOUND)
    target_include_directories(VirtualFS PRIVATE ${ZLIB_INCLUDE_DIRS})
endif()

if(ZSTD_FOUND)
    target_include_directories(VirtualFS PRIVATE ${ZSTD_INCLUDE_DIRS})
endif()

if(LZ4_FOUND)
    target_include_directories(VirtualFS PRIVATE ${LZ4_INCLUDE_DIRS})
endif()

if(BROTLI_FOUND)
    target_include_directories(VirtualFS PRIVATE ${BROTLI_INCLUDE_DIRS})
endif()

# ============================================================================
# LINK LIBRARIES
# ============================================================================

target_link_libraries(VirtualFS
    PRIVATE
        $<$<BOOL:${LIBARCHIVE_FOUND}>:${LIBARCHIVE_LIBRARIES}>
        $<$<BOOL:${ZLIB_FOUND}>:${ZLIB_LIBRARIES}>
        $<$<BOOL:${ZSTD_FOUND}>:${ZSTD_LIBRARIES}>
        $<$<BOOL:${LZ4_FOUND}>:${LZ4_LIBRARIES}>
        $<$<BOOL:${BROTLI_FOUND}>:${BROTLI_LIBRARIES}>
)

# Platform-specific libraries
if(WIN32)
    # Windows-specific
elseif(APPLE)
    # macOS-specific
    # target_link_libraries(VirtualFS PRIVATE "-framework Compression")
else()
    # Linux/BSD
    target_link_libraries(VirtualFS PRIVATE pthread)
endif()

# ============================================================================
# COMPILER SETTINGS
# ============================================================================

target_compile_features(VirtualFS PUBLIC cxx_std_17)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(VirtualFS PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(VirtualFS PRIVATE
        /W4
        /wd4100  # unreferenced formal parameter
    )
endif()

# Position independent code (for shared library compatibility)
set_target_properties(VirtualFS PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ============================================================================
# TESTS
# ============================================================================

if(VIRTUALFS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# EXAMPLES
# ============================================================================

if(VIRTUALFS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================================================
# INSTALLATION
# ============================================================================

include(GNUInstallDirs)

install(TARGETS VirtualFS
    EXPORT VirtualFSTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT VirtualFSTargets
    FILE VirtualFSTargets.cmake
    NAMESPACE VirtualFS::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VirtualFS
)

# ============================================================================
# PACKAGE CONFIGURATION
# ============================================================================

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VirtualFSConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/VirtualFSConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VirtualFS
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/VirtualFSConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/VirtualFSConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/VirtualFSConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VirtualFS
)

# ============================================================================
# SUMMARY
# ============================================================================

message(STATUS "")
message(STATUS "=== VirtualFS Configuration Summary ===")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Library Type:   $<IF:$<BOOL:${VIRTUALFS_BUILD_SHARED}>,Shared,Static>")
message(STATUS "")
message(STATUS "  Providers:")
message(STATUS "    libarchive:   ${LIBARCHIVE_FOUND}")
message(STATUS "    zlib:         ${ZLIB_FOUND}")
message(STATUS "    Zstandard:    ${ZSTD_FOUND}")
message(STATUS "    LZ4:          ${LZ4_FOUND}")
message(STATUS "    Brotli:       ${BROTLI_FOUND}")
message(STATUS "")
message(STATUS "  Build Options:")
message(STATUS "    Tests:        ${VIRTUALFS_BUILD_TESTS}")
message(STATUS "    Examples:     ${VIRTUALFS_BUILD_EXAMPLES}")
message(STATUS "==========================================")
message(STATUS "")
