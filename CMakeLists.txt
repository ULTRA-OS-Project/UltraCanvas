cmake_minimum_required(VERSION 3.16)
project(UltraCanvasApp)

# ✅ REQUIRED: C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(BUILD_SHARED_LIBS OFF)
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

if(APPLE)
    set(ULTRACANVAS_PLATFORM_MACOS TRUE)
    message(STATUS "Building for macOS")
elseif(UNIX)
    set(ULTRACANVAS_PLATFORM_LINUX TRUE)
    message(STATUS "Building for Linux")
elseif(WIN32)
    set(ULTRACANVAS_PLATFORM_WINDOWS TRUE)
    message(STATUS "Building for Windows")
endif()

# ✅ REQUIRED: Find platform packages
find_package(PkgConfig REQUIRED)

#find_package(PNG REQUIRED)
#if(PNG_FOUND)
#    message(STATUS "Found PNG: ${PNG_VERSION_STRING}")
#    message(STATUS "PNG Include: ${PNG_INCLUDE_DIRS}")
#    message(STATUS "PNG Libraries: ${PNG_LIBRARIES}")
#else()
#    message(FATAL_ERROR "libpng is required for image support")
#endif()

pkg_check_modules(VIPS REQUIRED vips vips-cpp)
if(NOT VIPS_FOUND)
    message(FATAL_ERROR "libvips is required for image support.")
endif()

# ✅ REQUIRED: Check for required packages first
if(ULTRACANVAS_PLATFORM_LINUX)
    # Check individual packages with better error messages
    pkg_check_modules(X11 REQUIRED x11)
    if(NOT X11_FOUND)
        message(FATAL_ERROR "X11 development package not found. Install with: sudo apt-get install libx11-dev")
    endif()
    
    pkg_check_modules(CAIRO REQUIRED cairo)
    if(NOT CAIRO_FOUND)
        message(FATAL_ERROR "Cairo development package not found. Install with: sudo apt-get install libcairo2-dev")
    endif()
    
    # ✅ FIXED: Split Pango packages for better detection
    pkg_check_modules(PANGO REQUIRED pango)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
    if(NOT PANGO_FOUND OR NOT PANGOCAIRO_FOUND)
        message(FATAL_ERROR "Pango development packages not found. Install with: sudo apt-get install libpango1.0-dev")
    endif()
    
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    if(NOT FREETYPE_FOUND)
        message(FATAL_ERROR "FreeType development package not found. Install with: sudo apt-get install libfreetype6-dev")
    endif()

    pkg_check_modules(TINYXML2 REQUIRED tinyxml2)
    if(NOT TINYXML2_FOUND)
        message(FATAL_ERROR "libtinyxml2 is required")
    endif()

    # ✅ ADDED: Print found package info for debugging

elseif(ULTRACANVAS_PLATFORM_MACOS)
    # ===== MACOS DEPENDENCIES =====
    message(STATUS "Configuring macOS dependencies...")

    # Detect Homebrew installation
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        # Apple Silicon (M1/M2/M3)
        set(HOMEBREW_PREFIX "/opt/homebrew")
        message(STATUS "Detected Apple Silicon Mac")
    else()
        # Intel Mac
        set(HOMEBREW_PREFIX "/usr/local")
        message(STATUS "Detected Intel Mac")
    endif()

    # Set PKG_CONFIG_PATH for Homebrew
    set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

    # Check for Cairo with Quartz backend
    pkg_check_modules(CAIRO REQUIRED cairo)
    if(NOT CAIRO_FOUND)
        message(FATAL_ERROR "Cairo not found. Install with: brew install cairo")
    endif()

    # Check for Pango
    pkg_check_modules(PANGO REQUIRED pango)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
    if(NOT PANGO_FOUND OR NOT PANGOCAIRO_FOUND)
        message(FATAL_ERROR "Pango not found. Install with: brew install pango")
    endif()

    # Check for FreeType
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    if(NOT FREETYPE_FOUND)
        message(FATAL_ERROR "FreeType not found. Install with: brew install freetype")
    endif()

    # Check for HarfBuzz (required by Pango)
    pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
    if(NOT HARFBUZZ_FOUND)
        message(FATAL_ERROR "HarfBuzz not found. Install with: brew install harfbuzz")
    endif()

    # Check for GLib (required by Pango)
    pkg_check_modules(GLIB REQUIRED glib-2.0)
    if(NOT GLIB_FOUND)
        message(FATAL_ERROR "GLib not found. Install with: brew install glib")
    endif()

    # TinyXML2
    pkg_check_modules(TINYXML2 REQUIRED tinyxml2)
    if(NOT TINYXML2_FOUND)
        message(FATAL_ERROR "TinyXML2 not found. Install with: brew install tinyxml2")
    endif()

    # Find macOS frameworks
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZ_CORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(CORE_GRAPHICS_FRAMEWORK CoreGraphics REQUIRED)

    if(NOT COCOA_FRAMEWORK)
        message(FATAL_ERROR "Cocoa framework not found")
    endif()

    # Enable Objective-C++ for .mm files
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    # Set Objective-C++ flags
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc -fno-objc-exceptions")

    message(STATUS "Objective-C++ enabled with ARC")
endif()

# ✅ REQUIRED: Include directories - CORRECT ORDER
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # Your headers
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include  # Framework headers
)

# ✅ FIXED: Add system include directories for packages
if(ULTRACANVAS_PLATFORM_LINUX)
    include_directories(SYSTEM
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
elseif(ULTRACANVAS_PLATFORM_MACOS)
    include_directories(SYSTEM
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

# ✅ REQUIRED: Source files
set(ULTRACANVAS_CROSSPLATFORM_SOURCES
    # Platform-specific sources
        UltraCanvas/core/UltraCanvasUtils.cpp
        UltraCanvas/core/UltraCanvasApplication.cpp
        UltraCanvas/core/UltraCanvasRenderInterface.cpp
        UltraCanvas/core/UltraCanvasTooltipManager.cpp
        UltraCanvas/core/UltraCanvasWindow.cpp
        UltraCanvas/core/UltraCanvasMenu.cpp
        UltraCanvas/core/UltraCanvasUIElement.cpp
        UltraCanvas/core/UltraCanvasTextInput.cpp
        UltraCanvas/core/UltraCanvasClipboard.cpp
        UltraCanvas/core/UltraCanvasElementDebug.cpp
        UltraCanvas/core/UltraCanvasContainer.cpp
        UltraCanvas/core/UltraCanvasTextArea.cpp
        UltraCanvas/core/UltraCanvasDropdown.cpp
        UltraCanvas/core/UltraCanvasImageElement.cpp
#        UltraCanvas/core/UltraCanvasImageLoader.cpp
        UltraCanvas/core/UltraCanvasTreeView.cpp
        UltraCanvas/core/UltraCanvasTabbedContainer.cpp
        UltraCanvas/core/UltraCanvasButton.cpp
        UltraCanvas/core/UltraCanvasSyntaxTokenizer.cpp
        UltraCanvas/core/UltraCanvasCheckbox.cpp
        UltraCanvas/core/UltraCanvasGraphicsPluginSystem.cpp
        UltraCanvas/core/UltraCanvasSegmentedControl.cpp
        UltraCanvas/core/UltraCanvasPasswordStrengthMeter.cpp
        UltraCanvas/core/UltraCanvasPasswordRuleLegend.cpp
        UltraCanvas/core/UltraCanvasLayout.cpp
        UltraCanvas/core/UltraCanvasBoxLayout.cpp
        UltraCanvas/core/UltraCanvasGridLayout.cpp
        UltraCanvas/core/UltraCanvasFlexLayout.cpp
        UltraCanvas/core/UltraCanvasToolbar.cpp
        UltraCanvas/core/UltraCanvasScrollbar.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasChartElementBase.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasSpecificChartElements.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasChartDataStructures.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasFinancialChart.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasDivergingBarChart.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasWaterfallChart.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasPopulationChart.cpp
        UltraCanvas/Plugins/SVG/UltraCanvasSVGPlugin.cpp
        UltraCanvas/Plugins/Diagrams/UltraCanvasSankey.cpp
        UltraCanvas/Plugins/Text/UltraCanvasMarkdown.cpp
        UltraCanvas/PixelFX/PixelFX.cpp
        #        UltraCanvas/Plugins/Charts/UltraCanvasChartElement.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartCoreRendering.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartBasicTypes.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartRenderer.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasFinancialCharts.cpp
    # Add Windows/Mac sources as needed
#        UltraCanvas/OS/MSWindows/UltraCanvasWindowsDoubleBuffer.cpp
)

set(ULTRACANVAS_LIBSPECIFIC_SOURCES
        UltraCanvas/libspecific/Cairo/ImageCairo.cpp
        UltraCanvas/libspecific/Cairo/RenderContextCairo.cpp
)

set(ULTRACANVAS_PLATFORM_SOURCES "")

if(ULTRACANVAS_PLATFORM_LINUX)
    set(ULTRACANVAS_PLATFORM_SOURCES
            UltraCanvas/OS/Linux/UltraCanvasLinuxApplication.cpp
            UltraCanvas/OS/Linux/UltraCanvasLinuxWindow.cpp
            UltraCanvas/OS/Linux/UltraCanvasLinuxClipboard.cpp
    )

elseif(ULTRACANVAS_PLATFORM_MACOS)
    set(ULTRACANVAS_PLATFORM_SOURCES
            UltraCanvas/OS/MacOS/UltraCanvasMacOSClipboard.mm
            UltraCanvas/OS/MacOS/UltraCanvasMacOSApplication.mm
            UltraCanvas/OS/MacOS/UltraCanvasMacOSWindow.mm
            UltraCanvas/OS/MacOS/UltraCanvasSupport.cpp
    )

    # ✅ CRITICAL: Mark .mm files as OBJCXX language
    set_source_files_properties(
            UltraCanvas/OS/MacOS/UltraCanvasMacOSApplication.mm
            UltraCanvas/OS/MacOS/UltraCanvasMacOSWindow.mm
            UltraCanvas/OS/MacOS/UltraCanvasMacOSClipboard.mm
            PROPERTIES
            LANGUAGE OBJCXX
            COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
    )
endif()

set(ULTRACANVAS_SOURCES
        ${ULTRACANVAS_CROSSPLATFORM_SOURCES}
        ${ULTRACANVAS_LIBSPECIFIC_SOURCES}
        ${ULTRACANVAS_PLATFORM_SOURCES}
)
# ✅ FIXED: Corrected source file extension and added missing files
set(APP_SOURCES
#        clipboard app
#        Apps/ExampleClipboardApp.cpp
#        Apps/UltraCanvasClipboardUI.cpp

# formula app
#        Apps/GraphicFormulaApp.cpp
        Apps/UltraCanvasChartExample.cpp
        #Apps/UltraCanvasFinancialChartExample.cpp
        #Apps/TabbedContainerDemo.cpp
        #        Apps/DivergingBarChartExample.cpp
        #        Apps/Complete_IBM_Financial_Chart_Example.cpp
        #        Apps/IBM_StockChart_Example.cpp

        #image rendeing
#        Apps/ImageRenderingExample.cpp
        # Add your source files here
)

set(MAIN_DEMOAPP_SOURCES
#        Apps/UltraCanvasChartExample.cpp
        Apps/DemoApp/main.cpp
        Apps/DemoApp/UltraCanvasDemo.cpp
        Apps/DemoApp/UltraCanvasDemoExamples.cpp
        Apps/DemoApp/UltraCanvasBitmapExamples.cpp
        Apps/DemoApp/UltraCanvasButtonExamples.cpp
        Apps/DemoApp/UltraCanvasDropDownExamples.cpp
        Apps/DemoApp/UltraCanvasLabelExamples.cpp
        Apps/DemoApp/UltraCanvasMenuExamples.cpp
        Apps/DemoApp/UltraCanvasSliderExamples.cpp
        Apps/DemoApp/UltraCanvasSVGExamples.cpp
#        Apps/DemoApp/UltraCanvasTabExamples.cpp
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
        Apps/DemoApp/UltraCanvasTableDemo.cpp
        Apps/DemoApp/UltraCanvasToolbarExamples.cpp
        Apps/DemoApp/UltraCanvasTabExamples.cpp
)

# ✅ ADDED: Check that source files exist
foreach(source_file ${APP_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        message(WARNING "Source file not found: ${source_file}")
    endif()
endforeach()

foreach(source_file ${ULTRACANVAS_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        message(WARNING "UltraCanvas source file not found: ${source_file}")
    endif()
endforeach()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# ✅ REQUIRED: Create executable
add_executable(${PROJECT_NAME}
    ${MAIN_DEMOAPP_SOURCES}
    ${ULTRACANVAS_SOURCES}
)

# ✅ REQUIRED: Link libraries - Linux
if(ULTRACANVAS_PLATFORM_LINUX)
    target_link_libraries(${PROJECT_NAME}
        ${X11_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PANGO_LIBRARIES}
        ${PANGOCAIRO_LIBRARIES}  # ✅ FIXED: Added separate PangoCairo linking
        ${FREETYPE_LIBRARIES}
        ${TINYXML2_LIBRARIES}
        ${VIPS_LIBRARIES}
        pthread
    )

    # ✅ FIXED: Improved compile flags handling
    target_compile_options(${PROJECT_NAME} PRIVATE
        ${X11_CFLAGS_OTHER}
        ${CAIRO_CFLAGS_OTHER}
        ${PANGO_CFLAGS_OTHER}
        ${PANGOCAIRO_CFLAGS_OTHER}  # ✅ ADDED: PangoCairo flags
        ${FREETYPE_CFLAGS_OTHER}
        ${VIPS_CFLAGS_OTHER}
    )
    
    # ✅ ADDED: Define platform macro
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        ULTRACANVAS_PLATFORM_LINUX
    )
elseif(ULTRACANVAS_PLATFORM_MACOS)
    target_link_libraries(${PROJECT_NAME}
            ${CAIRO_LIBRARIES}
            ${PANGO_LIBRARIES}
            ${PANGOCAIRO_LIBRARIES}
            ${FREETYPE_LIBRARIES}
            ${HARFBUZZ_LIBRARIES}
            ${GLIB_LIBRARIES}
            ${TINYXML2_LIBRARIES}
            ${VIPS_LIBRARIES}
            ${COCOA_FRAMEWORK}
            ${QUARTZ_CORE_FRAMEWORK}
            ${CORE_GRAPHICS_FRAMEWORK}
            pthread
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
            ${CAIRO_CFLAGS_OTHER}
            ${PANGO_CFLAGS_OTHER}
            ${PANGOCAIRO_CFLAGS_OTHER}
            ${FREETYPE_CFLAGS_OTHER}
            ${HARFBUZZ_CFLAGS_OTHER}
            ${GLIB_CFLAGS_OTHER}
            ${VIPS_CFLAGS_OTHER}
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
            ULTRACANVAS_PLATFORM_MACOS
    )

    # ✅ CRITICAL: Set Objective-C++ specific flags for the target
    set_target_properties(${PROJECT_NAME} PROPERTIES
            OBJCXX_STANDARD 20
            OBJCXX_STANDARD_REQUIRED ON
    )
endif()


# ✅ OPTIONAL: Compiler warnings
target_compile_options(${PROJECT_NAME} PRIVATE
    -g -Wextra -Wpedantic
)

# ✅ OPTIONAL: Debug/Release configurations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        ULTRACANVAS_DEBUG_EVENTS
        ULTRACANVAS_DEBUG_RENDERING
    )
    message(STATUS "Debug build configured")
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        NDEBUG
    )
    message(STATUS "Release build configured")
endif()
