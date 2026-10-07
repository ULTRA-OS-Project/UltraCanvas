# VideoFX/CMakeLists.txt
# Build configuration for the VideoFX module (video editing / conversion).
# FFmpeg is optional: without it the same API is built from a stub whose calls
# return VideoFXResult::NotAvailable, so consumers never need an #ifdef.
# Version: 0.5.0
# Last Modified: 2026-10-07
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

project(VideoFX
    VERSION 0.5.0
    DESCRIPTION "VideoFX - video editing and conversion engine"
    LANGUAGES CXX
)

option(VIDEOFX_USE_FFMPEG "Build VideoFX on FFmpeg (libavformat/avcodec/avfilter/swscale)" ON)
option(VIDEOFX_BUILD_TOOL "Build the videofx command-line tool" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# DEPENDENCIES
# ============================================================================
# FFmpeg 4.4 (Ubuntu 22.04) or newer; the version differences are absorbed in
# core/VideoFXBackend.*. swresample is not used directly - the aresample
# filter reaches it through libavfilter.
set(VIDEOFX_FFMPEG_FOUND FALSE)
if(VIDEOFX_USE_FFMPEG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(VIDEOFX_FFMPEG IMPORTED_TARGET
            libavformat>=58.76 libavcodec>=58.134 libavfilter>=7.110 libavutil>=56.70 libswscale>=5.9)
    endif()
    if(VIDEOFX_FFMPEG_FOUND)
        message(STATUS "VideoFX: FFmpeg ${VIDEOFX_FFMPEG_libavformat_VERSION} (libavformat) found - engine enabled")
    else()
        message(STATUS "VideoFX: FFmpeg development libraries not found - building the API stub "
                       "(install libavformat-dev libavcodec-dev libavfilter-dev libswscale-dev)")
    endif()
endif()

# ============================================================================
# SOURCES
# ============================================================================
set(VIDEOFX_HEADERS
    include/VideoFX/VideoFX.h
    include/VideoFX/VideoFXTypes.h
)

# Factories, presets and the effect -> filter text translation have no
# backend dependency; the job runner only calls the public API.
set(VIDEOFX_SOURCES
    core/VideoFXFilterBuilder.cpp
    core/VideoFXFilterBuilder.h
    core/VideoFXKenBurns.cpp
    core/VideoFXKenBurns.h
    core/VideoFXMusic.cpp
    core/VideoFXMusic.h
    core/VideoFXJob.cpp
)

if(VIDEOFX_FFMPEG_FOUND)
    list(APPEND VIDEOFX_SOURCES
        core/VideoFXBackend.cpp
        core/VideoFXBackend.h
        core/VideoFXProbe.cpp
        core/VideoFXExport.cpp
        core/VideoFXRemux.cpp
        core/VideoFXFonts.cpp
        core/VideoFXPlatform.h
    )
    # The executable's directory, where bundled fonts are looked for
    if(WIN32)
        list(APPEND VIDEOFX_SOURCES OS/MSWindows/VideoFXExecutableDir.cpp)
    elseif(APPLE)
        list(APPEND VIDEOFX_SOURCES OS/MacOS/VideoFXExecutableDir.cpp)
    else()
        list(APPEND VIDEOFX_SOURCES OS/Linux/VideoFXExecutableDir.cpp)
    endif()
else()
    list(APPEND VIDEOFX_SOURCES core/VideoFXUnavailable.cpp)
endif()

add_library(VideoFX STATIC ${VIDEOFX_SOURCES} ${VIDEOFX_HEADERS})
add_library(VideoFX::VideoFX ALIAS VideoFX)

target_include_directories(VideoFX
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)
# The single source of VideoFX's version (VideoFX_GetVersion)
target_compile_definitions(VideoFX PRIVATE VIDEOFX_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(VideoFX PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
target_link_libraries(VideoFX PUBLIC Threads::Threads)
if(VIDEOFX_FFMPEG_FOUND)
    # PRIVATE: no FFmpeg header reaches a consumer
    target_link_libraries(VideoFX PRIVATE PkgConfig::VIDEOFX_FFMPEG)
    target_compile_definitions(VideoFX PUBLIC VIDEOFX_HAS_FFMPEG=1)
endif()

# Exported so parents (tests, apps) can tell a working engine from the stub
get_directory_property(VIDEOFX_HAS_PARENT PARENT_DIRECTORY)
if(VIDEOFX_HAS_PARENT)
    set(VIDEOFX_FFMPEG_FOUND ${VIDEOFX_FFMPEG_FOUND} PARENT_SCOPE)
endif()

# ============================================================================
# COMMAND-LINE TOOL
# ============================================================================
# The target is videofx_cli, not videofx: on a case-insensitive file system
# (macOS, Windows) "videofx" and the library "VideoFX" would share one
# CMakeFiles/<target>.dir, and the Makefile build stops with "No rule to make
# target VideoFX/CMakeFiles/VideoFX.dir/depend". For the same reason the
# executable goes to bin/ rather than next to the build tree's VideoFX/ folder.
if(VIDEOFX_BUILD_TOOL)
    add_executable(videofx_cli tools/videofx_cli.cpp)
    set_target_properties(videofx_cli PROPERTIES
        OUTPUT_NAME videofx
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    target_link_libraries(videofx_cli PRIVATE VideoFX::VideoFX)
    if(WIN32)
        target_link_libraries(videofx_cli PRIVATE shell32)   # CommandLineToArgvW: UTF-8 arguments
    endif()
endif()

if(VIDEOFX_FFMPEG_FOUND)
    set(VIDEOFX_ENGINE "FFmpeg")
else()
    set(VIDEOFX_ENGINE "stub (no FFmpeg)")
endif()
message(STATUS "VideoFX ${PROJECT_VERSION}: engine=${VIDEOFX_ENGINE}, tool=${VIDEOFX_BUILD_TOOL}")
