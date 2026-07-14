# Apps/AnchorPoint/CMakeLists.txt
# AnchorPoint - peer-to-peer file sharing (no server file storage).
# Version: 0.2.0
#
# Targets:
#   anchorpoint_core  static lib: transfer protocol + raw-socket transport (no deps)
#   anchorpoint       headless CLI (always built)
#   AnchorPoint       UltraCanvas GUI (built when the framework is available;
#                     toggle with -DANCHORPOINT_BUILD_GUI=ON/OFF)
cmake_minimum_required(VERSION 3.16)

project(AnchorPoint
        VERSION 0.2.0
        DESCRIPTION "AnchorPoint - peer-to-peer file sharing"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(ANCHORPOINT_BUILD_GUI "Build the UltraCanvas GUI (needs the framework toolchain)" ON)

# ===========================================================================
# Transfer core (no external dependencies)
# ===========================================================================
# Raw-socket backend today; swappable for an UltraNet backend later. Kept as a
# static lib so both the CLI and the GUI link the exact same code.
add_library(anchorpoint_core STATIC
        net/RawSocketTransport.cpp
        net/Protocol.cpp)
target_include_directories(anchorpoint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/net)
target_compile_features(anchorpoint_core PUBLIC cxx_std_20)

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
add_executable(anchorpoint cli/main.cpp)
target_link_libraries(anchorpoint PRIVATE anchorpoint_core)
set_target_properties(anchorpoint PROPERTIES OUTPUT_NAME "anchorpoint")
install(TARGETS anchorpoint RUNTIME DESTINATION bin)

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

        target_include_directories(AnchorPoint PRIVATE
                ${ULTRACANVAS_INCLUDE_DIRS}
                ${ULTRACANVAS_ROOT}/include
                ${CMAKE_CURRENT_SOURCE_DIR})

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
        set_target_properties(AnchorPoint PROPERTIES
                OUTPUT_NAME "AnchorPoint"
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        install(TARGETS AnchorPoint RUNTIME DESTINATION bin)
        message(STATUS "AnchorPoint ${PROJECT_VERSION}: building core + CLI + GUI (${ANCHORPOINT_PLATFORM}).")
    endif()
else()
    message(STATUS "AnchorPoint ${PROJECT_VERSION}: building core + CLI (GUI disabled).")
endif()
