# Apps/AnchorPoint/CMakeLists.txt
# AnchorPoint - peer-to-peer file sharing (no server file storage).
# The version is the first line of Docs/AnchorPoint/CHANGELOG.md.
#
# Targets:
#   anchorpoint_core  static lib: transfer protocol + raw-socket transport (UltraCrypt)
#   anchorpoint-cli   headless CLI (always built). Not `anchorpoint`: on the
#                     case-insensitive file systems of macOS and Windows that
#                     is the same file as the window's `AnchorPoint`.
#   AnchorPointTests  protocol tests, run by ctest (-DANCHORPOINT_BUILD_TESTS=OFF to skip).
#                     Named <App>Tests like every suite: the Windows package
#                     leaves *Tests.exe out, and a test binary in it fails CI.
#   AnchorPoint       UltraCanvas GUI (built when the framework is available;
#                     toggle with -DANCHORPOINT_BUILD_GUI=ON/OFF)
cmake_minimum_required(VERSION 3.16)

# ANCHORPOINT_VERSION is read from the first line of
# Docs/AnchorPoint/CHANGELOG.md. Must be included before project(), which
# uses it.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasVersion.cmake")

project(AnchorPoint
        VERSION ${ANCHORPOINT_VERSION}
        DESCRIPTION "AnchorPoint - peer-to-peer file sharing"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The Windows manifest every UltraCanvas program carries: the UTF-8 code page
# (so file names in any script reach the program intact, `argv` included) and
# long paths. A no-op elsewhere. Included here too for a standalone build,
# where the root CMakeLists.txt that includes it is not read.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasWinManifest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasAppIcon.cmake")

option(ANCHORPOINT_BUILD_GUI "Build the UltraCanvas GUI (needs the framework toolchain)" ON)
option(ANCHORPOINT_BUILD_TESTS "Build the protocol tests" ON)

# ===========================================================================
# Transfer core (depends only on UltraCrypt/libsodium)
# ===========================================================================
# Raw-socket backend today; swappable for an UltraNet backend later. Kept as a
# static lib so both the CLI and the GUI link the exact same code.
#
# The core used to have no dependencies at all, paid for with a hand-rolled
# SHA-256 in net/Sha256.h. That is now UltraCrypt's job. UltraCrypt is itself a
# small static lib over libsodium — deliberately not part of the UI framework —
# so the core still builds without UltraCanvas; it just needs libsodium.
add_library(anchorpoint_core STATIC
        net/RawSocketTransport.cpp
        net/Protocol.cpp)
target_include_directories(anchorpoint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/net)
target_compile_features(anchorpoint_core PUBLIC cxx_std_20)

# A parent build (the framework, or the GUI branch below) already defines the
# UltraCrypt target; standalone we compile its one source file directly rather
# than pulling in all of UltraCanvas for a hash.
if(TARGET UltraCrypt)
    target_link_libraries(anchorpoint_core PUBLIC UltraCrypt)
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/include/UltraCrypt/UltraCryptCore.h")
        set(ANCHORPOINT_ULTRACANVAS "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/include/UltraCrypt/UltraCryptCore.h")
        set(ANCHORPOINT_ULTRACANVAS "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas")
    else()
        message(FATAL_ERROR
                "AnchorPoint: UltraCrypt not found. The transfer core needs it for "
                "SHA-256 integrity checks.")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED libsodium)

    # UltraCrypt's secure Base32 decoder delegates to the framework's shared
    # codec, which is one more self-contained file (UltraCanvasBase32 in the
    # framework build).
    target_sources(anchorpoint_core PRIVATE
            ${ANCHORPOINT_ULTRACANVAS}/core/UltraCrypt/UltraCryptCore.cpp
            ${ANCHORPOINT_ULTRACANVAS}/core/UltraCrypt/UltraCryptAes.cpp
            ${ANCHORPOINT_ULTRACANVAS}/core/UltraCanvasBase32.cpp)
    target_include_directories(anchorpoint_core PUBLIC
            ${ANCHORPOINT_ULTRACANVAS}/include)
    target_include_directories(anchorpoint_core PRIVATE ${SODIUM_INCLUDE_DIRS})
    target_compile_definitions(anchorpoint_core PUBLIC ULTRACRYPT_HAVE_SODIUM=1)
    target_link_directories(anchorpoint_core PUBLIC ${SODIUM_LIBRARY_DIRS})
    target_link_libraries(anchorpoint_core PUBLIC ${SODIUM_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(anchorpoint_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(anchorpoint_core PUBLIC Threads::Threads)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(anchorpoint_core PRIVATE -Wall -Wextra)
endif()

# ===========================================================================
# Headless CLI (always)
# ===========================================================================
add_executable(anchorpoint-cli cli/main.cpp)
target_link_libraries(anchorpoint-cli PRIVATE anchorpoint_core)
set_target_properties(anchorpoint-cli PROPERTIES OUTPUT_NAME "anchorpoint-cli")
ultracanvas_embed_windows_manifest(anchorpoint-cli)
install(TARGETS anchorpoint-cli RUNTIME DESTINATION bin)

# ===========================================================================
# Tests (headless, core only)
# ===========================================================================
if(ANCHORPOINT_BUILD_TESTS)
    enable_testing()
    add_executable(AnchorPointTests tests/ProtocolTests.cpp)
    target_link_libraries(AnchorPointTests PRIVATE anchorpoint_core)
    add_test(NAME AnchorPointProtocol COMMAND AnchorPointTests)
endif()

# ===========================================================================
# GUI (UltraCanvas)
# ===========================================================================
if(ANCHORPOINT_BUILD_GUI)
    # Locate the UltraCanvas sibling module.
    if(ULTRACANVAS_LIBRARY)
        # Provided by a parent build.
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/CMakeLists.txt")
        set(ULTRACANVAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/CMakeLists.txt")
        set(ULTRACANVAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas")
    endif()

    if(NOT ULTRACANVAS_LIBRARY AND NOT ULTRACANVAS_ROOT)
        message(WARNING "AnchorPoint: UltraCanvas not found - skipping GUI (core + CLI still build).")
    else()
        # Resolve fmt at THIS scope before pulling in the framework, so the
        # imported fmt::fmt target is visible when we link the GUI. (Building
        # the framework standalone otherwise leaves fmt::fmt scoped to the
        # UltraCanvas subdirectory only.)
        if(NOT TARGET fmt::fmt)
            find_package(fmt CONFIG QUIET)
        endif()

        if(ULTRACANVAS_ROOT AND NOT ULTRACANVAS_LIBRARY)
            add_subdirectory(${ULTRACANVAS_ROOT} ${CMAKE_BINARY_DIR}/UltraCanvas)
        endif()

        find_package(PkgConfig REQUIRED)
        if(WIN32)
            set(ANCHORPOINT_PLATFORM "Windows")
        elseif(APPLE)
            set(ANCHORPOINT_PLATFORM "MacOS")
        elseif(UNIX)
            set(ANCHORPOINT_PLATFORM "Linux")
        endif()

        if(ANCHORPOINT_PLATFORM STREQUAL "Linux")
            pkg_check_modules(X11 REQUIRED x11)
            pkg_check_modules(CAIRO REQUIRED cairo)
            pkg_check_modules(PANGO REQUIRED pango)
            pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
            pkg_check_modules(FREETYPE REQUIRED freetype2)
            pkg_check_modules(TINYXML2 REQUIRED tinyxml2)
            pkg_check_modules(VIPS REQUIRED vips-cpp)
            include_directories(${X11_INCLUDE_DIRS} ${CAIRO_INCLUDE_DIRS}
                    ${PANGO_INCLUDE_DIRS} ${PANGOCAIRO_INCLUDE_DIRS}
                    ${FREETYPE_INCLUDE_DIRS} ${TINYXML2_INCLUDE_DIRS}
                    ${VIPS_INCLUDE_DIRS})
            link_directories(${X11_LIBRARY_DIRS} ${CAIRO_LIBRARY_DIRS}
                    ${PANGO_LIBRARY_DIRS} ${PANGOCAIRO_LIBRARY_DIRS}
                    ${FREETYPE_LIBRARY_DIRS} ${TINYXML2_LIBRARY_DIRS}
                    ${VIPS_LIBRARY_DIRS})
        endif()

        add_executable(AnchorPoint
                gui/main.cpp
                gui/AnchorPointApp.cpp)
        # One copy per user; a second start brings the running one forward
        # (UltraCanvasSingleInstance.h). The GUI only: the headless
        # `anchorpoint-cli` runs as many times as it is started. Included here
        # too for a standalone build, where the root CMakeLists.txt that
        # defines the function is not read.
        include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasSingleInstance.cmake")
        ultracanvas_single_instance(AnchorPoint)

        target_include_directories(AnchorPoint PRIVATE
                ${ULTRACANVAS_INCLUDE_DIRS}
                ${CMAKE_CURRENT_SOURCE_DIR})
        if(ULTRACANVAS_ROOT)   # standalone: the framework added above
            target_include_directories(AnchorPoint PRIVATE ${ULTRACANVAS_ROOT}/include)
        endif()

        target_link_libraries(AnchorPoint PRIVATE
                anchorpoint_core
                ${ULTRACANVAS_LIBRARY})

        if(TARGET fmt::fmt)
            target_link_libraries(AnchorPoint PRIVATE fmt::fmt)
        endif()
        if(ULTRACANVAS_PLUGIN_TARGETS)
            target_link_libraries(AnchorPoint PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
        endif()
        if(WIN32)
            target_link_libraries(AnchorPoint PRIVATE ws2_32 iconv)
        endif()

        target_compile_features(AnchorPoint PRIVATE cxx_std_20)
        # The version shown in the window title: the first line of
        # Docs/AnchorPoint/CHANGELOG.md (cmake/UltraCanvasVersion.cmake).
        target_compile_definitions(AnchorPoint PRIVATE
                ANCHORPOINT_VERSION="${ANCHORPOINT_VERSION}")
        set_target_properties(AnchorPoint PROPERTIES
                OUTPUT_NAME "AnchorPoint"
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        # Windows Release builds open no console window beside AnchorPoint's
        # (cmake/UltraCanvasWindowsGuiApp.cmake). The headless `anchorpoint-cli`
        # stays a console program.
        include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasWindowsGuiApp.cmake")
        ultracanvas_windows_gui_app(AnchorPoint)
        ultracanvas_embed_windows_manifest(AnchorPoint)

        # The app icon: an anchor with 101011 across its shank, in the
        # UltraCanvas logo's colours. media/appicon/AnchorPoint.svg is the
        # source; the PNG beside it is its 256 px render, which main.cpp
        # hands to the window. Embedded in the .exe for Explorer and the
        # taskbar (Windows only), and installed as the theme icon the
        # desktop entry names (Linux).
        set(ANCHORPOINT_ICON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../media/appicon")
        ultracanvas_embed_app_icon(AnchorPoint "${ANCHORPOINT_ICON_DIR}/AnchorPoint.png")
        install(TARGETS AnchorPoint RUNTIME DESTINATION bin)
        if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
            install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/AnchorPoint.desktop
                    DESTINATION share/applications)
            install(FILES ${ANCHORPOINT_ICON_DIR}/AnchorPoint.png
                    DESTINATION share/icons/hicolor/256x256/apps)
            install(FILES ${ANCHORPOINT_ICON_DIR}/AnchorPoint.svg
                    DESTINATION share/icons/hicolor/scalable/apps)
        endif()
        message(STATUS "AnchorPoint ${PROJECT_VERSION}: building core + CLI + GUI (${ANCHORPOINT_PLATFORM}).")
    endif()
else()
    message(STATUS "AnchorPoint ${PROJECT_VERSION}: building core + CLI (GUI disabled).")
endif()
