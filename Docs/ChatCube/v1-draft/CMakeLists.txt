# ChatCube/CMakeLists.txt
# ChatCube — UltraCanvas-based multi-protocol instant messenger
# Builds the main ChatCube executable and all provider plugin modules.
# Follows the UltraCanvas application build pattern.
# Version: 1.0.0
# Last Modified: 2026-04-13
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)
project(ChatCube VERSION 2.0.0 LANGUAGES CXX)

# ============================================================================
# C++ STANDARD
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# OPTIONS
# ============================================================================

option(CHATCUBE_BUILD_MATRIX_PLUGIN    "Build the Matrix provider plugin"          ON)
option(CHATCUBE_BUILD_TELEGRAM_PLUGIN  "Build the Telegram provider plugin"         OFF)
option(CHATCUBE_BUILD_XMPP_PLUGIN      "Build the XMPP provider plugin"             OFF)
option(CHATCUBE_BUILD_DIRECT_PLUGIN    "Build the ChatCube P2P Direct plugin"       ON)
option(CHATCUBE_HTTP_BACKEND_CURL      "Use libcurl as the HTTP backend"            ON)
option(CHATCUBE_HTTP_BACKEND_PLATFORM  "Use platform-native HTTP (fallback)"        OFF)

message(STATUS "")
message(STATUS "========================================")
message(STATUS "Configuring ChatCube v${PROJECT_VERSION}")
message(STATUS "========================================")

# ============================================================================
# FIND ULTRACANVAS
# Assumes UltraCanvas is built as a sibling directory or installed.
# Set ULTRACANVAS_DIR to override the search path.
# ============================================================================

if(NOT DEFINED ULTRACANVAS_DIR)
    set(ULTRACANVAS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas"
        CACHE PATH "Path to UltraCanvas framework root")
endif()

# Add UltraCanvas as a subdirectory if it has its own CMakeLists
if(EXISTS "${ULTRACANVAS_DIR}/CMakeLists.txt")
    add_subdirectory("${ULTRACANVAS_DIR}" UltraCanvas_build)
    set(ULTRACANVAS_FOUND TRUE)
    set(ULTRACANVAS_INCLUDE_DIR "${ULTRACANVAS_DIR}/include")
    message(STATUS "UltraCanvas: found at ${ULTRACANVAS_DIR}")
else()
    # Try find_package fallback (installed version)
    find_package(UltraCanvas QUIET)
    if(UltraCanvas_FOUND)
        set(ULTRACANVAS_FOUND TRUE)
        message(STATUS "UltraCanvas: found via find_package")
    else()
        message(FATAL_ERROR
            "UltraCanvas not found. Set -DULTRACANVAS_DIR=/path/to/UltraCanvas")
    endif()
endif()

# ============================================================================
# HTTP BACKEND — required by all network plugins
# ============================================================================

if(CHATCUBE_HTTP_BACKEND_CURL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED libcurl)
    if(CURL_FOUND)
        message(STATUS "HTTP backend: libcurl ${CURL_VERSION}")
        add_compile_definitions(CHATCUBE_USE_CURL)
    else()
        message(FATAL_ERROR "libcurl not found. Install libcurl-dev or set CHATCUBE_HTTP_BACKEND_PLATFORM=ON")
    endif()
endif()

# ============================================================================
# JSON PARSING — nlohmann/json (header-only, fetched if not present)
# ============================================================================

find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "JSON: nlohmann/json found")
else()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message(STATUS "JSON: nlohmann/json fetched from GitHub")
endif()

# ============================================================================
# CHATCUBE CORE LIBRARY
# Shared by the main executable and all plugin modules
# ============================================================================

add_library(ChatCubeCore STATIC
    # Types and data
    core/ChatCubeTypes.cpp
    core/ChatMessageModel.cpp

    # UI components
    core/ChatBubbleDelegate.cpp

    # Plugin framework
    core/ChatCubeProviderManager.cpp
)

target_include_directories(ChatCubeCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_DIR}/Plugins
)

target_link_libraries(ChatCubeCore
    PUBLIC
        UltraCanvas
        nlohmann_json::nlohmann_json
)

target_compile_features(ChatCubeCore PUBLIC cxx_std_20)

# ============================================================================
# MATRIX PLUGIN MODULE
# ============================================================================

if(CHATCUBE_BUILD_MATRIX_PLUGIN)
    add_library(ChatCubeMatrixPlugin SHARED
        Plugins/Matrix/MatrixProviderPlugin.cpp
    )

    target_include_directories(ChatCubeMatrixPlugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Matrix
            ${ULTRACANVAS_INCLUDE_DIR}
    )

    target_link_libraries(ChatCubeMatrixPlugin
        PRIVATE
            ChatCubeCore
            UltraCanvas
            nlohmann_json::nlohmann_json
    )

    if(CHATCUBE_HTTP_BACKEND_CURL)
        target_link_libraries(ChatCubeMatrixPlugin PRIVATE ${CURL_LIBRARIES})
        target_include_directories(ChatCubeMatrixPlugin PRIVATE ${CURL_INCLUDE_DIRS})
    endif()

    # Output to Modules/ directory (UOS app bundle location)
    set_target_properties(ChatCubeMatrixPlugin PROPERTIES
        OUTPUT_NAME "MatrixPlugin"
        SUFFIX      ".module"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ChatCube.app/Resources/Modules"
    )

    message(STATUS "Plugin: Matrix ✓")
endif()

# ============================================================================
# TELEGRAM PLUGIN MODULE (stub — implementation in future step)
# ============================================================================

if(CHATCUBE_BUILD_TELEGRAM_PLUGIN)
    add_library(ChatCubeTelegramPlugin SHARED
        Plugins/Telegram/TelegramProviderPlugin.cpp
    )
    target_include_directories(ChatCubeTelegramPlugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${ULTRACANVAS_INCLUDE_DIR}
    )
    target_link_libraries(ChatCubeTelegramPlugin
        PRIVATE ChatCubeCore UltraCanvas)
    set_target_properties(ChatCubeTelegramPlugin PROPERTIES
        OUTPUT_NAME "TelegramPlugin"
        SUFFIX      ".module"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ChatCube.app/Resources/Modules"
    )
    message(STATUS "Plugin: Telegram ✓")
endif()

# ============================================================================
# CHATCUBE DIRECT P2P PLUGIN (stub — implementation in future step)
# ============================================================================

if(CHATCUBE_BUILD_DIRECT_PLUGIN)
    add_library(ChatCubeDirectPlugin SHARED
        Plugins/Direct/ChatCubeDirectPlugin.cpp
    )
    target_include_directories(ChatCubeDirectPlugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${ULTRACANVAS_INCLUDE_DIR}
    )
    target_link_libraries(ChatCubeDirectPlugin
        PRIVATE ChatCubeCore UltraCanvas)
    set_target_properties(ChatCubeDirectPlugin PROPERTIES
        OUTPUT_NAME "ChatCubeDirect"
        SUFFIX      ".module"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ChatCube.app/Resources/Modules"
    )
    message(STATUS "Plugin: ChatCube P2P Direct ✓")
endif()

# ============================================================================
# CHATCUBE MAIN EXECUTABLE
# ============================================================================

add_executable(ChatCube
    main.cpp
    core/ChatCubeWindow.cpp
)

target_include_directories(ChatCube
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Matrix
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_DIR}/Plugins
)

target_link_libraries(ChatCube
    PRIVATE
        ChatCubeCore
        UltraCanvas
        nlohmann_json::nlohmann_json
)

# Link Matrix plugin statically into the executable (built-in plugin)
if(CHATCUBE_BUILD_MATRIX_PLUGIN)
    target_link_libraries(ChatCube PRIVATE ChatCubeMatrixPlugin)
endif()

if(CHATCUBE_HTTP_BACKEND_CURL)
    target_link_libraries(ChatCube PRIVATE ${CURL_LIBRARIES})
endif()

# ============================================================================
# PLATFORM-SPECIFIC SETTINGS
# ============================================================================

if(WIN32)
    # Windows: set executable icon and subsystem
    set_target_properties(ChatCube PROPERTIES
        WIN32_EXECUTABLE TRUE
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
    message(STATUS "Platform: Windows")

elseif(APPLE)
    # macOS: create proper .app bundle
    set_target_properties(ChatCube PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
    message(STATUS "Platform: macOS")

elseif(UNIX)
    # Linux / RISC OS: standard executable
    target_link_libraries(ChatCube PRIVATE pthread dl)
    message(STATUS "Platform: Linux/Unix")
endif()

# ============================================================================
# RESOURCE INSTALLATION
# Copy the Resources tree into the build output (UOS bundle structure)
# ============================================================================

set(CHATCUBE_BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ChatCube.app")

# Create UOS-compliant directory structure
add_custom_command(TARGET ChatCube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CHATCUBE_BUNDLE_DIR}/Resources/Media/Images"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Media/Audio"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Media/Video"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Locale"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Data/cache/avatars"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Data/cache/attachments"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Frameworks"
        "${CHATCUBE_BUNDLE_DIR}/Resources/Modules"
        "${CHATCUBE_BUNDLE_DIR}/Legals"
        "${CHATCUBE_BUNDLE_DIR}/Samples"
    COMMENT "Creating UOS bundle directory structure"
)

# Copy executable into bundle
add_custom_command(TARGET ChatCube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:ChatCube>"
        "${CHATCUBE_BUNDLE_DIR}/ChatCube$<TARGET_FILE_SUFFIX:ChatCube>"
    COMMENT "Copying executable into UOS bundle"
)

# Copy AppInfo.txt
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AppInfo.txt")
    add_custom_command(TARGET ChatCube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/AppInfo.txt"
            "${CHATCUBE_BUNDLE_DIR}/AppInfo.txt"
        COMMENT "Copying AppInfo.txt"
    )
endif()

# Copy source Resources tree if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
    add_custom_command(TARGET ChatCube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
            "${CHATCUBE_BUNDLE_DIR}/Resources"
        COMMENT "Copying Resources into UOS bundle"
    )
endif()

# ============================================================================
# SUMMARY
# ============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "ChatCube build configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  UltraCanvas:    ${ULTRACANVAS_DIR}")
message(STATUS "  HTTP backend:   ${CHATCUBE_HTTP_BACKEND_CURL}")
message(STATUS "  Matrix plugin:  ${CHATCUBE_BUILD_MATRIX_PLUGIN}")
message(STATUS "  Telegram plugin:${CHATCUBE_BUILD_TELEGRAM_PLUGIN}")
message(STATUS "  XMPP plugin:    ${CHATCUBE_BUILD_XMPP_PLUGIN}")
message(STATUS "  P2P Direct:     ${CHATCUBE_BUILD_DIRECT_PLUGIN}")
message(STATUS "  Bundle output:  ${CHATCUBE_BUNDLE_DIR}")
message(STATUS "========================================")
message(STATUS "")
