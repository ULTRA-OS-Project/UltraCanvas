# CMakeLists.txt (Root)
# UltraCanvas Demo Application - Links against core library and plugins
# Version: 1.0.0
# Last Modified: 2025-12-14
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

project(UltraCanvasDemoApp
    VERSION 1.0.0
    DESCRIPTION "UltraCanvas Framework Demo Application"
    LANGUAGES CXX
)

message(STATUS "")
message(STATUS "========================================")
message(STATUS "   UltraCanvas Demo Application Build")
message(STATUS "========================================")
message(STATUS "")

# ===== GLOBAL SETTINGS =====
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ===== BUILD OPTIONS =====
option(BUILD_TEXTER_APP "Build the texter application" ON)
option(BUILD_FILEMANAGER_APP "Build the file manager application" OFF) # OFF: source files not yet committed
option(BUILD_DEMO_APP "Build the demo application" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(ULTRACANVAS_PLUGIN_CDR "Build CDR plugin (requires libcdr)" ON)
option(ULTRACANVAS_PLUGIN_XAR "Build XARA plugin" ON)
option(ULTRACANVAS_PLUGIN_VECTOR "Build Vector plugin" OFF)
option(ULTRACANVAS_PLUGIN_PDF "Build PDF plugin (requires poppler)" OFF)

# ===== FIND REQUIRED PACKAGES =====
# On Windows with vcpkg, auto-detect pkgconf for pkg_check_modules support
if(WIN32 AND DEFINED CMAKE_TOOLCHAIN_FILE)
    find_program(PKG_CONFIG_EXECUTABLE
        NAMES pkgconf pkg-config
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/pkgconf"
              "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/pkgconf"
        NO_DEFAULT_PATH
    )
    if(PKG_CONFIG_EXECUTABLE)
        # Ensure pkgconf can find vcpkg's .pc files
        set(ENV{PKG_CONFIG_PATH} "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
        message(STATUS "Using vcpkg pkgconf: ${PKG_CONFIG_EXECUTABLE}")
    endif()
endif()
find_package(PkgConfig REQUIRED)

# ===== BUILD ULTRACANVAS CORE LIBRARY =====
message(STATUS "")
message(STATUS "── Building UltraCanvas Core Library ──")
add_subdirectory(UltraCanvas)

# At this point, the following variables are available from UltraCanvas/CMakeLists.txt:
#   ULTRACANVAS_LIBRARY        - Main library target name
#   ULTRACANVAS_LIBRARIES      - All libraries (core + plugins)
#   ULTRACANVAS_PLUGIN_TARGETS - List of plugin targets
#   ULTRACANVAS_INCLUDE_DIRS   - All include directories

# ===== DEMO APPLICATION =====
if(BUILD_DEMO_APP)
    message(STATUS "")
    message(STATUS "── Building Demo Application ──")

    # Find jsoncpp for demo app
    pkg_check_modules(JSONCPP REQUIRED jsoncpp)

    # Demo app source files
    set(DEMO_SOURCES
            Apps/DemoApp/main.cpp
            Apps/DemoApp/UltraCanvasDemo.cpp
            Apps/DemoApp/UltraCanvasDemoExamples.cpp
            Apps/DemoApp/UltraCanvasButtonExamples.cpp
            Apps/DemoApp/UltraCanvasDropDownExamples.cpp
            Apps/DemoApp/UltraCanvasLabelExamples.cpp
            Apps/DemoApp/UltraCanvasMenuExamples.cpp
            Apps/DemoApp/UltraCanvasSliderExamples.cpp
            Apps/DemoApp/UltraCanvasSVGExamples.cpp
            Apps/DemoApp/UltraCanvasTabExamples.cpp
            Apps/DemoApp/UltraCanvasTextAreaExamples.cpp
            Apps/DemoApp/UltraCanvasTextInputExamples.cpp
            Apps/DemoApp/UltraCanvasTreeViewExamples.cpp
            Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp
            Apps/DemoApp/UltraCanvasDivergingChartExamples.cpp
            Apps/DemoApp/UltraCanvasWatefallChartExamples.cpp
            Apps/DemoApp/UltraCanvasFinancialChartExamples.cpp
            Apps/DemoApp/UltraCanvasCheckboxExamples.cpp
            Apps/DemoApp/UltraCanvasDemoInfoWindow.cpp
            Apps/DemoApp/UltraCanvasSankeyExamples.cpp
            Apps/DemoApp/UltraCanvasPopulationChartExamples.cpp
            Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp
            Apps/DemoApp/UltraCanvasLayoutExamples.cpp
            Apps/DemoApp/UltraCanvasToolbarExamples.cpp
            Apps/DemoApp/UltraCanvasTabExamples.cpp
            Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp
    )

    # These files depend on libvips (non-trivial effort)
    if(NOT WIN32)
        list(APPEND DEMO_SOURCES
            Apps/DemoApp/UltraCanvasBitmapExamples.cpp
            Apps/DemoApp/UltraCanvasBitmapFormatDemo.cpp
        )
    endif()

    # Only include plugin examples if the plugins are actually built
    foreach(PLUGIN_TARGET ${ULTRACANVAS_PLUGIN_TARGETS})
        if(PLUGIN_TARGET MATCHES "CDR")
            list(APPEND DEMO_SOURCES Apps/DemoApp/UltraCanvasCDRExamples.cpp)
        endif()

        if(PLUGIN_TARGET MATCHES "XAR")
            list(APPEND DEMO_SOURCES Apps/DemoApp/UltraCanvasXARExamples.cpp)
        endif()
    endforeach()

    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        include_directories(
                ${X11_INCLUDE_DIRS}
                ${CAIRO_INCLUDE_DIRS}
                ${PANGO_INCLUDE_DIRS}
                ${VIPS_INCLUDE_DIRS}
                ${PANGOCAIRO_INCLUDE_DIRS}
                ${FREETYPE_INCLUDE_DIRS}
                ${TINYXML2_INCLUDE_DIRS}
        )

        # ✅ ADDED: Set link directories
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
        # Set Homebrew prefix
        execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        include_directories(
                ${CAIRO_INCLUDE_DIRS}
                ${PANGO_INCLUDE_DIRS}
                ${PANGOCAIRO_INCLUDE_DIRS}
                ${FREETYPE_INCLUDE_DIRS}
                ${HARFBUZZ_INCLUDE_DIRS}
                ${GLIB_INCLUDE_DIRS}
                ${VIPS_INCLUDE_DIRS}
                ${TINYXML2_INCLUDE_DIRS}
                ${JSONCPP_INCLUDE_DIRS}
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
                ${TINYXML2_LIBRARY_DIRS}
                ${JSONCPP_LIBRARY_DIRS}
                ${HOMEBREW_PREFIX}/lib
                ${HOMEBREW_PREFIX}/opt/vips/lib
        )
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        link_directories(
                ${JSONCPP_LIBRARY_DIRS}
                ${CAIRO_LIBRARY_DIRS}
                ${PANGO_LIBRARY_DIRS}
                ${HARFBUZZ_LIBRARY_DIRS}
                ${GLIB_LIBRARY_DIRS}
                ${TINYXML2_LIBRARY_DIRS}
                ${FREETYPE_LIBRARY_DIRS}
        )
    endif()

    # Create executable
    add_executable(UltraCanvasDemo
        ${DEMO_SOURCES}
    )
    
    # Include directories
    target_include_directories(UltraCanvasDemo PRIVATE
        ${ULTRACANVAS_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/Apps/DemoApp
#        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/3rdparty/fmt/include
    )
    
    # Link against UltraCanvas core library
    target_link_libraries(UltraCanvasDemo PRIVATE
        ${ULTRACANVAS_LIBRARY}
        ${JSONCPP_LIBRARIES}
    )
    target_link_libraries(UltraCanvasDemo PRIVATE fmt::fmt)

    # Link against all plugin libraries
    if(ULTRACANVAS_PLUGIN_TARGETS)
        target_link_libraries(UltraCanvasDemo PRIVATE
            ${ULTRACANVAS_PLUGIN_TARGETS}
        )
        
        # Add compile definitions for available plugins
        foreach(PLUGIN_TARGET ${ULTRACANVAS_PLUGIN_TARGETS})
            string(TOUPPER ${PLUGIN_TARGET} PLUGIN_UPPER)
            target_compile_definitions(UltraCanvasDemo PRIVATE
                ${PLUGIN_UPPER}_AVAILABLE=1
            )
        endforeach()
    endif()
    
    # Compile features
    target_compile_features(UltraCanvasDemo PRIVATE cxx_std_20)
    
    # Compile definitions
    target_compile_definitions(UltraCanvasDemo PRIVATE
        ULTRACANVAS_DEMO_VERSION="${PROJECT_VERSION}"
        $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )
    
    # Compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(UltraCanvasDemo PRIVATE
#            -Wall -Wextra
        )
    endif()
    
    # Set executable properties
    set_target_properties(UltraCanvasDemo PROPERTIES
        OUTPUT_NAME "UltraCanvasDemo"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/..
    )
    
    # Installation
#    install(TARGETS UltraCanvasDemo
#        RUNTIME DESTINATION bin
#    )
    
    message(STATUS "  Demo executable: UltraCanvasDemo")
endif()

# ===== UNIT TESTS =====
if(BUILD_TESTS)
    message(STATUS "")
    message(STATUS "── Building Unit Tests ──")
    
    enable_testing()
    
    # Find test framework
    find_package(Catch2 QUIET)
    
    if(Catch2_FOUND)
        add_subdirectory(Tests)
    else()
        message(STATUS "  Catch2 not found - tests disabled")
        message(STATUS "  Install with: sudo apt install catch2")
    endif()
endif()

# ===== DOCUMENTATION =====
#find_package(Doxygen QUIET)
#if(DOXYGEN_FOUND)
#    option(BUILD_DOCS "Build documentation" OFF)
#    if(BUILD_DOCS)
#        add_subdirectory(Docs)
#    endif()
#endif()

if (BUILD_TEXTER_APP)
    # Create executable
    add_executable(UltraCanvasTexter
            Apps/Texter/UltraCanvasTextEditor.cpp
            Apps/Texter/UltraCanvasTextEditorHelpers.cpp
            Apps/Texter/UltraCanvasTextEditorDialogs.cpp
            Apps/Texter/main.cpp
    )

    # Include directories
    target_include_directories(UltraCanvasTexter PRIVATE
            ${ULTRACANVAS_INCLUDE_DIRS}
            #        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/3rdparty/fmt/include
    )

    # Link against UltraCanvas core library
    target_link_libraries(UltraCanvasTexter PRIVATE
            ${ULTRACANVAS_LIBRARY}
            ${JSONCPP_LIBRARIES}
    )
    target_link_libraries(UltraCanvasTexter PRIVATE fmt::fmt)

    # Compile features
    target_compile_features(UltraCanvasTexter PRIVATE cxx_std_20)

    # Compile definitions
    target_compile_definitions(UltraCanvasTexter PRIVATE
            ULTRACANVAS_DEMO_VERSION="${PROJECT_VERSION}"
            $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )

    # Compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(UltraCanvasTexter PRIVATE
                #            -Wall -Wextra
        )
    endif()

    # Set executable properties
    set_target_properties(UltraCanvasTexter PROPERTIES
            OUTPUT_NAME "UltraCanvasTexter"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/..
    )
endif ()

if (BUILD_FILEMANAGER_APP)
    set(UCFILEMANAGER_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/ListView/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/ListView/DemoListViewFileManager.cpp
    )

    # Create executable
    add_executable(UCFileManager
            ${UCFILEMANAGER_SOURCES}
    )

    # Include directories
    target_include_directories(UCFileManager PRIVATE
            ${ULTRACANVAS_INCLUDE_DIRS}
            ${ULTRACANVAS_ROOT}/include
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Link against UltraCanvas core library
    target_link_libraries(UCFileManager PRIVATE
            ${ULTRACANVAS_LIBRARY}
    )

    # Link fmt library
    target_link_libraries(UCFileManager PRIVATE fmt::fmt)

    # Link against plugin libraries if available
    if(ULTRACANVAS_PLUGIN_TARGETS)
        target_link_libraries(UCFileManager PRIVATE
                ${ULTRACANVAS_PLUGIN_TARGETS}
        )
    endif()

    # Compile features
    target_compile_features(UCFileManager PRIVATE cxx_std_20)

    # Compile definitions
    target_compile_definitions(UCFileManager PRIVATE
            UCFILEMANAGER_VERSION="${PROJECT_VERSION}"
            $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )

    # Compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(UCFileManager PRIVATE
                -Wall -Wextra
        )
    endif()

    # Set executable properties
    set_target_properties(UCFileManager PROPERTIES
            OUTPUT_NAME "UCFileManager"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ===== PACKAGE CONFIGURATION =====
include(CMakePackageConfigHelpers)

# Generate version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/UltraCanvasConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Generate config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/UltraCanvasConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/UltraCanvasConfig.cmake"
    @ONLY
)

# Install config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/UltraCanvasConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/UltraCanvasConfigVersion.cmake"
    DESTINATION lib/cmake/UltraCanvas
)

# ===== CPack CONFIGURATION =====
#set(CPACK_PACKAGE_NAME "UltraCanvas")
#set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
#set(CPACK_PACKAGE_VENDOR "Cloverleaf UG")
#set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform UI & Rendering Framework")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
#set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
#
#if(UNIX AND NOT APPLE)
#    set(CPACK_GENERATOR "DEB;TGZ")
#    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcairo2, libpango-1.0-0, libfreetype6")
#elseif(APPLE)
#    set(CPACK_GENERATOR "DragNDrop;TGZ")
#elseif(WIN32)
#    set(CPACK_GENERATOR "NSIS;ZIP")
#endif()
#
#include(CPack)

# ===== FINAL CONFIGURATION SUMMARY =====
message(STATUS "")
message(STATUS "                 Configuration Summary")
message(STATUS "===============================================================")
message(STATUS "  Project:        ${PROJECT_NAME}")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "===============================================================")
message(STATUS "  Libraries:")
message(STATUS "    Core:         ${ULTRACANVAS_LIBRARY}")
if(ULTRACANVAS_PLUGIN_TARGETS)
message(STATUS "    Plugins:      ${ULTRACANVAS_PLUGIN_TARGETS}")
else()
message(STATUS "    Plugins:      (none)")
endif()
message(STATUS "===============================================================")
message(STATUS "  Targets:")
if(BUILD_DEMO_APP)
message(STATUS "    Demo App:     ultracanvas_demo")
endif()
if(BUILD_TESTS)
message(STATUS "    Tests:        ultracanvas_tests")
endif()
message(STATUS "===============================================================")
message(STATUS "  Output Directories:")
message(STATUS "    Binaries:     ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "    Libraries:    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "===============================================================")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  cmake --build . --parallel")
message(STATUS "  cmake --build . --target install")
message(STATUS "")
