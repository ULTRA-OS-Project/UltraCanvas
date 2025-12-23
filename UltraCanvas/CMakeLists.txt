# UltraCanvas/CMakeLists.txt
# UltraCanvas Core Library - Builds as separate static library with optional plugins
# Version: 1.2.0
# Last Modified: 2025-12-14
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# ===== LIBRARY DEFINITION =====
set(ULTRACANVAS_VERSION "1.2.0")
set(ULTRACANVAS_LIB_NAME "UltraCanvas")

message(STATUS "")
message(STATUS "========================================")
message(STATUS "Configuring ${ULTRACANVAS_LIB_NAME} v${ULTRACANVAS_VERSION}")
message(STATUS "========================================")

# ===== OPTIONS =====
option(ULTRACANVAS_BUILD_SHARED "Build shared library instead of static" OFF)
option(ULTRACANVAS_PLUGIN_CDR "Build CorelDRAW CDR plugin" ON)
option(ULTRACANVAS_PLUGIN_VECTOR, "Build Vector plugin" ON)
option(ULTRACANVAS_PLUGIN_XAR "Build XARA plugin" OFF)
option(ULTRACANVAS_PLUGIN_SVG "Build SVG plugin" OFF)
option(ULTRACANVAS_PLUGIN_PDF "Build PDF plugin" OFF)

# ===== DETECT PLATFORM =====
if(WIN32)
    set(ULTRACANVAS_PLATFORM "Windows")
    set(ULTRACANVAS_PLATFORM_DIR "OS/MSWindows")
elseif(APPLE)
    set(ULTRACANVAS_PLATFORM "MacOS")
    set(ULTRACANVAS_PLATFORM_DIR "OS/MacOS")
elseif(UNIX)
    set(ULTRACANVAS_PLATFORM "Linux")
    set(ULTRACANVAS_PLATFORM_DIR "OS/Linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

message(STATUS "Target Platform: ${ULTRACANVAS_PLATFORM}")

# ===== FIND CORE DEPENDENCIES =====
find_package(PkgConfig REQUIRED)

# Cairo - 2D Graphics
pkg_check_modules(CAIRO REQUIRED cairo)

# Pango - Text Layout
pkg_check_modules(PANGO REQUIRED pango pangocairo)

pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(LIBVIPS REQUIRED vips vips-cpp)
pkg_check_modules(GLIB REQUIRED glib-2.0)

# FreeType - Font Rendering
find_package(Freetype REQUIRED)

# Platform-specific dependencies
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    pkg_check_modules(X11 REQUIRED x11)
    find_package(OpenGL REQUIRED)
elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
    # Windows SDK provides most dependencies
elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZ_FRAMEWORK QuartzCore REQUIRED)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
endif()

# ===== DEFINE INCLUDE DIRECTORY =====
set(ULTRACANVAS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(ULTRACANVAS_INCLUDE_DIR ${ULTRACANVAS_INCLUDE_DIR} PARENT_SCOPE)
set(ULTRACANVAS_PLATFORM ${ULTRACANVAS_PLATFORM} PARENT_SCOPE)

# ===== COLLECT SOURCE FILES =====

# Core sources
set(ULTRACANVAS_CORE_SOURCES
        # Platform-specific sources
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasUtils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasApplication.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasRenderInterface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTooltipManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasWindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasMenu.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasUIElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTextInput.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasClipboard.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasElementDebug.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasContainer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTextArea.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasDropdown.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasImageElement.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasImageLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTreeView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTabbedContainer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasButton.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSyntaxTokenizer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasCheckbox.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasGraphicsPluginSystem.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSegmentedControl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasPasswordStrengthMeter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasPasswordRuleLegend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasBoxLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasGridLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasFlexLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasToolbar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasScrollbar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartElementBase.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasSpecificChartElements.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartDataStructures.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasFinancialChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasDivergingBarChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasWaterfallChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasPopulationChart.cpp
#        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/SVG/UltraCanvasSVGPlugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasSankey.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Text/UltraCanvasMarkdown.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/PixelFX/PixelFX.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartElement.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartCoreRendering.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartBasicTypes.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartRenderer.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasFinancialCharts.cpp
        # Add Windows/Mac sources as needed
        #        ${CMAKE_CURRENT_SOURCE_DIR}/OS/MSWindows/UltraCanvasWindowsDoubleBuffer.cpp
)

# Include headers (for IDE support)
file(GLOB ULTRACANVAS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/**/*.h
)

# Platform-specific sources
file(GLOB ULTRACANVAS_PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/*.cpp
)

# macOS needs Objective-C++ files
if(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
    file(GLOB ULTRACANVAS_OBJCPP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/*.mm
    )
    list(APPEND ULTRACANVAS_PLATFORM_SOURCES ${ULTRACANVAS_OBJCPP_SOURCES})

    set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/*.mm
            PROPERTIES
            LANGUAGE OBJCXX
            COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
    )
endif()

# Built-in plugins (always included)
file(GLOB ULTRACANVAS_TEXT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Text/*.cpp
)

file(GLOB ULTRACANVAS_CHARTS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/*.cpp
)

set(ULTRACANVAS_LIBSPECIFIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/ImageCairo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/RenderContextCairo.cpp
)

# Combine all core sources
set(ULTRACANVAS_ALL_SOURCES
    ${ULTRACANVAS_CORE_SOURCES}
    ${ULTRACANVAS_PLATFORM_SOURCES}
    ${ULTRACANVAS_LIBSPECIFIC_SOURCES}
    ${ULTRACANVAS_TEXT_SOURCES}
    ${ULTRACANVAS_CHARTS_SOURCES}
        Plugins/Vector/UltraCanvasXARConverter.h
        Plugins/Vector/UltraCanvasVectorElement.h
        Plugins/Vector/UltraCanvasVectorElement.cpp
)

# ===== BUILD CORE LIBRARY =====
if(ULTRACANVAS_BUILD_SHARED)
    add_library(${ULTRACANVAS_LIB_NAME} SHARED ${ULTRACANVAS_ALL_SOURCES})
else()
    add_library(${ULTRACANVAS_LIB_NAME} STATIC ${ULTRACANVAS_ALL_SOURCES})
endif()

# Set library properties
set_target_properties(${ULTRACANVAS_LIB_NAME} PROPERTIES
    VERSION ${ULTRACANVAS_VERSION}
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${ULTRACANVAS_LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Plugins>
        $<INSTALL_INTERFACE:include/ultracanvas>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}
        ${CAIRO_INCLUDE_DIRS}
        ${PANGO_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
        ${LIBVIPS_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
)

# Platform-specific includes
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
        ${X11_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIRS}
    )
endif()

# ===== LINK LIBRARIES =====
target_link_libraries(${ULTRACANVAS_LIB_NAME}
    PUBLIC
        ${CAIRO_LIBRARIES}
        ${PANGO_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${LIBVIPS_LIBRARIES}
        ${GLIB_LIBRARIES}
)

# Platform-specific libraries
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
        ${X11_LIBRARIES}
#        ${OPENGL_LIBRARIES}
        pthread
        dl
    )
elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
        d2d1
        dwrite
        windowscodecs
        gdi32
        user32
        ole32
        uuid
    )
elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
        ${COCOA_FRAMEWORK}
        ${QUARTZ_FRAMEWORK}
#        ${OPENGL_FRAMEWORK}
    )
endif()

# ===== COMPILER SETTINGS =====
target_compile_features(${ULTRACANVAS_LIB_NAME} PUBLIC cxx_std_20)

target_compile_definitions(${ULTRACANVAS_LIB_NAME}
    PUBLIC
        ULTRACANVAS_VERSION="${ULTRACANVAS_VERSION}"
        ULTRACANVAS_PLATFORM_${ULTRACANVAS_PLATFORM}=1
    PRIVATE
        $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${ULTRACANVAS_LIB_NAME} PRIVATE
#        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
endif()

# ===== BUILD PLUGINS AS SEPARATE LIBRARIES =====
set(ULTRACANVAS_PLUGIN_TARGETS "")
set(ULTRACANVAS_PLUGIN_INCLUDE_DIRS "")

# CDR Plugin
if(ULTRACANVAS_PLUGIN_CDR)
    pkg_check_modules(LIBCDR libcdr-0.1)
    pkg_check_modules(LIBREVENGE librevenge-0.0)

    if(LIBCDR_FOUND AND LIBREVENGE_FOUND)
        add_subdirectory(Plugins/Vector/CDR)
        if(ULTRACANVAS_CDR_PLUGIN_FOUND)
            list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_CDR_PLUGIN_TARGET})
            list(APPEND ULTRACANVAS_PLUGIN_INCLUDE_DIRS ${ULTRACANVAS_CDR_PLUGIN_INCLUDE_DIR})
            message(STATUS "  [✓] CDR Plugin - ENABLED")
        endif()
    else()
        message(STATUS "  [✗] CDR Plugin - DISABLED (missing libcdr/librevenge)")
    endif()
else()
    message(STATUS "  [−] CDR Plugin - SKIPPED (disabled by option)")
endif()

if(ULTRACANVAS_PLUGIN_XAR)
    add_subdirectory(Plugins/Vector/XAR)
    if(ULTRACANVAS_XAR_PLUGIN_FOUND)
        list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_XAR_PLUGIN_TARGET})
        list(APPEND ULTRACANVAS_PLUGIN_INCLUDE_DIRS ${ULTRACANVAS_XAR_PLUGIN_INCLUDE_DIR})
        message(STATUS "  [✓] XAR Plugin - ENABLED")
    endif()
else()
    message(STATUS "  [−] XAR Plugin - SKIPPED (disabled by option)")
endif()

if(ULTRACANVAS_PLUGIN_VECTOR)
    add_subdirectory(Plugins/Vector)
    if(ULTRACANVAS_VECTOR_PLUGIN_FOUND)
        list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_VECTOR_PLUGIN_TARGET})
        list(APPEND ULTRACANVAS_PLUGIN_INCLUDE_DIRS ${ULTRACANVAS_VECTOR_PLUGIN_INCLUDE_DIR})
        message(STATUS "  [✓] VECTOR Plugin - ENABLED")
    endif()
else()
    message(STATUS "  [−] VECTOR Plugin - SKIPPED (disabled by option)")
endif()

# PDF Plugin
if(ULTRACANVAS_PLUGIN_PDF)
    pkg_check_modules(POPPLER poppler-cpp)
    if(POPPLER_FOUND)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/PDF/CMakeLists.txt)
            add_subdirectory(Plugins/Documents/PDF)
            if(ULTRACANVAS_PDF_PLUGIN_FOUND)
                list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_PDF_PLUGIN_TARGET})
                message(STATUS "  [✓] PDF Plugin - ENABLED")
            endif()
        else()
            message(STATUS "  [−] PDF Plugin - NOT FOUND (CMakeLists.txt missing)")
        endif()
    else()
        message(STATUS "  [✗] PDF Plugin - DISABLED (missing poppler)")
    endif()
else()
    message(STATUS "  [−] PDF Plugin - SKIPPED (disabled by option)")
endif()

# Add more plugins here...

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_LIBRARY ${ULTRACANVAS_LIB_NAME} PARENT_SCOPE)
set(ULTRACANVAS_LIBRARIES ${ULTRACANVAS_LIB_NAME} ${ULTRACANVAS_PLUGIN_TARGETS} PARENT_SCOPE)
set(ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_PLUGIN_TARGETS} PARENT_SCOPE)
set(ULTRACANVAS_INCLUDE_DIRS 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ULTRACANVAS_PLUGIN_INCLUDE_DIRS}
    PARENT_SCOPE
)

# ===== INSTALLATION =====
install(TARGETS ${ULTRACANVAS_LIB_NAME}
    EXPORT UltraCanvasTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include/ultracanvas
    FILES_MATCHING PATTERN "*.h"
)

# Export CMake config for find_package support
install(EXPORT UltraCanvasTargets
    FILE UltraCanvasTargets.cmake
    NAMESPACE UltraCanvas::
    DESTINATION lib/cmake/UltraCanvas
)

# ===== CONFIGURATION SUMMARY =====
message(STATUS "")
message(STATUS "UltraCanvas Core Configuration Summary:")
if(ULTRACANVAS_BUILD_SHARED)
    message(STATUS "  Library Type: SHARED")
else()
    message(STATUS "  Library Type: STATIC")
endif ()
message(STATUS "  Platform: ${ULTRACANVAS_PLATFORM}")
message(STATUS "  Cairo: ${CAIRO_VERSION}")
message(STATUS "  Pango: ${PANGO_VERSION}")
message(STATUS "  FreeType: ${FREETYPE_VERSION_STRING}")
if(HARFBUZZ_FOUND)
    message(STATUS "  HarfBuzz: ${HARFBUZZ_VERSION}")
endif()
if(LIBVIPS_FOUND)
    message(STATUS "  libvips: ${LIBVIPS_VERSION}")
endif()
message(STATUS "  Plugins: ${ULTRACANVAS_PLUGIN_TARGETS}")
message(STATUS "========================================")
message(STATUS "")
