cmake_minimum_required(VERSION 3.16)
project(UltraCanvasClipboardApp)

# ✅ REQUIRED: C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ REQUIRED: Find platform packages
find_package(PkgConfig REQUIRED)

find_package(PNG REQUIRED)
if(PNG_FOUND)
    message(STATUS "Found PNG: ${PNG_VERSION_STRING}")
    message(STATUS "PNG Include: ${PNG_INCLUDE_DIRS}")
    message(STATUS "PNG Libraries: ${PNG_LIBRARIES}")
else()
    message(FATAL_ERROR "libpng is required for image support")
endif()

# Find JPEG library
find_package(JPEG REQUIRED)
if(JPEG_FOUND)
    message(STATUS "Found JPEG library")
    message(STATUS "JPEG Include: ${JPEG_INCLUDE_DIRS}")
    message(STATUS "JPEG Libraries: ${JPEG_LIBRARIES}")
else()
    message(FATAL_ERROR "libjpeg is required for image support")
endif()

# ✅ REQUIRED: Check for required packages first
if(UNIX AND NOT APPLE)
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
    
    # ✅ ADDED: Print found package info for debugging
    message(STATUS "Found X11: ${X11_VERSION}")
    message(STATUS "Found Cairo: ${CAIRO_VERSION}")
    message(STATUS "Found Pango: ${PANGO_VERSION}")
    message(STATUS "Found PangoCairo: ${PANGOCAIRO_VERSION}")
    message(STATUS "Found1 FreeType: ${FREETYPE_VERSION}")
endif()

# ✅ REQUIRED: Include directories - CORRECT ORDER
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # Your headers
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include  # Framework headers
)

# ✅ FIXED: Add system include directories for packages
if(UNIX AND NOT APPLE)
    include_directories(SYSTEM
        ${X11_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${PANGO_INCLUDE_DIRS}
        ${PNG_INCLUDE_DIRS}
        ${JPEG_INCLUDE_DIRS}
        ${PANGOCAIRO_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
    )
    
    # ✅ ADDED: Set link directories
    link_directories(
        ${X11_LIBRARY_DIRS}
        ${CAIRO_LIBRARY_DIRS}
        ${PANGO_LIBRARY_DIRS}
        ${PANGOCAIRO_LIBRARY_DIRS}
        ${FREETYPE_LIBRARY_DIRS}
    )
endif()

# ✅ REQUIRED: Source files
set(ULTRACANVAS_SOURCES
    # Platform-specific sources
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
        #UltraCanvas/core/UltraCanvasScrollbar.cpp
        UltraCanvas/OS/Linux/UltraCanvasLinuxRenderContext.cpp
        UltraCanvas/OS/Linux/UltraCanvasLinuxImageLoader.cpp
        UltraCanvas/OS/Linux/UltraCanvasLinuxApplication.cpp
        UltraCanvas/OS/Linux/UltraCanvasLinuxWindow.cpp
        UltraCanvas/OS/Linux/UltraCanvasLinuxClipboard.cpp
        UltraCanvas/OS/Linux/UltraCanvasLinuxDoubleBuffer.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasChartElementBase.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasSpecificChartElements.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasChartDataStructures.cpp
        UltraCanvas/Plugins/Charts/UltraCanvasFinancialChart.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartElement.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartCoreRendering.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartBasicTypes.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasChartRenderer.cpp
#        UltraCanvas/Plugins/Charts/UltraCanvasFinancialCharts.cpp
    # Add Windows/Mac sources as needed
#        UltraCanvas/OS/MSWindows/UltraCanvasWindowsDoubleBuffer.cpp
)

# ✅ FIXED: Corrected source file extension and added missing files
set(APP_SOURCES
#        clipboard app
#        Apps/ExampleClipboardApp.cpp
#        Apps/UltraCanvasClipboardUI.cpp

# formula app
        #Apps/GraphicFormulaApp.cpp
#        Apps/UltraCanvasChartExample.cpp
        Apps/UltraCanvasFinancialChartExample.cpp
        #        Apps/Complete_IBM_Financial_Chart_Example.cpp
        #        Apps/IBM_StockChart_Example.cpp

        #image rendeing
#        Apps/ImageRenderingExample.cpp
        # Add your source files here
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

# ✅ REQUIRED: Create executable
add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${ULTRACANVAS_SOURCES}
)

# ✅ REQUIRED: Link libraries - Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME}
        ${X11_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PANGO_LIBRARIES}
        ${PANGOCAIRO_LIBRARIES}  # ✅ FIXED: Added separate PangoCairo linking
        ${FREETYPE_LIBRARIES}
        ${PNG_LIBRARIES}
        ${JPEG_LIBRARIES}
        pthread
    )

    # ✅ FIXED: Improved compile flags handling
    target_compile_options(${PROJECT_NAME} PRIVATE
        ${X11_CFLAGS_OTHER}
        ${CAIRO_CFLAGS_OTHER}
        ${PANGO_CFLAGS_OTHER}
        ${PANGOCAIRO_CFLAGS_OTHER}  # ✅ ADDED: PangoCairo flags
        ${FREETYPE_CFLAGS_OTHER}
    )
    
    # ✅ ADDED: Define platform macro
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        ULTRACANVAS_PLATFORM_LINUX
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

# ✅ ADDED: Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Project source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
