# VideoFX/CMakeLists.txt
# Build configuration for the VideoFX module (video editing / conversion).
# FFmpeg is optional: without it the same API is built from a stub whose calls
# return VideoFXResult::NotAvailable, so consumers never need an #ifdef.
# Version: 0.1.0
# Last Modified: 2026-09-29
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

project(VideoFX
    VERSION 0.1.0
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
    core/VideoFXJob.cpp
)

if(VIDEOFX_FFMPEG_FOUND)
    list(APPEND VIDEOFX_SOURCES
        core/VideoFXBackend.cpp
        core/VideoFXBackend.h
        core/VideoFXProbe.cpp
        core/VideoFXExport.cpp
        core/VideoFXRemux.cpp
    )
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
if(VIDEOFX_BUILD_TOOL)
    add_executable(videofx tools/videofx_cli.cpp)
    target_link_libraries(videofx PRIVATE VideoFX::VideoFX)
    if(WIN32)
        target_link_libraries(videofx PRIVATE shell32)   # CommandLineToArgvW: UTF-8 arguments
    endif()
endif()

message(STATUS "VideoFX ${PROJECT_VERSION}: engine=${VIDEOFX_FFMPEG_FOUND}, tool=${VIDEOFX_BUILD_TOOL}")
