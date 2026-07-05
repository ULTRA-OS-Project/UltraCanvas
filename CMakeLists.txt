# CMakeLists.txt (Root)
# UltraCanvas Demo Application - Links against core library and plugins
# Version: 1.0.0
# Last Modified: 2025-12-14
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

project(UltraCanvasDemoApp
    VERSION 0.3.0
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

# Define Profile build type (based on Debug + gprof instrumentation)
set(CMAKE_C_FLAGS_PROFILE "${CMAKE_C_FLAGS_DEBUG} -pg" CACHE STRING "C flags for Profile build" FORCE)
set(CMAKE_CXX_FLAGS_PROFILE "${CMAKE_CXX_FLAGS_DEBUG} -pg" CACHE STRING "C++ flags for Profile build" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -pg" CACHE STRING "Linker flags for Profile build" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_PROFILE "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -pg" CACHE STRING "Shared linker flags for Profile build" FORCE)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ===== BUILD OPTIONS =====
option(BUILD_TEXTER_APP "Build the texter application" ON)
option(BUILD_FILEMANAGER_APP "Build the file manager application" OFF) # OFF: source files not yet committed
option(BUILD_DEMO_APP "Build the demo application" ON)
option(BUILD_CSSLAYOUT_TESTS "Build the CSSLayout non-UI test harness" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(ULTRACANVAS_BUILD_NET_TESTS "Build the UltraNet test suite" OFF)
option(ULTRACANVAS_PLUGIN_CDR "Build CDR plugin (requires libcdr)" ON)
option(ULTRACANVAS_PLUGIN_XAR "Build XARA plugin" ON)
option(ULTRACANVAS_ENABLE_AUDIO "Build audio backend" ON)
option(ULTRACANVAS_PLUGIN_VECTOR "Build Vector plugin" OFF)
option(ULTRACANVAS_PLUGIN_PDF "Build PDF plugin (requires MuPDF / libmupdf-dev)" ON)
option(ULTRACANVAS_PLUGIN_OCR "Build OCR plugin (requires tesseract + leptonica)" OFF)
option(ULTRACANVAS_PLUGIN_VECTORIZER "Build Vectorizer plugin (raster→SVG via VTracer; requires cargo)" OFF)
option(ULTRACANVAS_USE_VIRTUALFS "Build the VirtualFS module and the UltraCanvas bridge to it" ON)

include(FetchContent)
include(cmake/UltraCanvasAppIcon.cmake)

# ===== FIND REQUIRED PACKAGES =====
find_package(PkgConfig REQUIRED)

# ===== BUILD VIRTUALFS MODULE =====
# Independent sibling module; UltraCanvas consumes it through
# UltraCanvasVirtualFSBridge (added inside UltraCanvas/CMakeLists.txt
# when the VirtualFS target exists).
if(ULTRACANVAS_USE_VIRTUALFS)
    message(STATUS "")
    message(STATUS "── Building VirtualFS Module ──")
    add_subdirectory(VirtualFS)
endif()

# ===== BUILD ULTRACANVAS CORE LIBRARY =====
message(STATUS "")
message(STATUS "── Building UltraCanvas Core Library ──")
add_subdirectory(UltraCanvas)

# ===== ULTRAMAIL =====
# The mail application. Phase 1 builds the headless engine (LocalStore on
# UltraDatabase); UI targets are added in later phases. Enabled by default
# whenever UltraDatabase is available.
option(BUILD_ULTRAMAIL "Build the UltraMail application" ON)
if(BUILD_ULTRAMAIL AND TARGET UltraDatabase)
    add_subdirectory(Apps/UltraMail)
endif()

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
            Apps/DemoApp/UltraCanvasDatePickerExamples.cpp
            Apps/DemoApp/UltraCanvasAutoCompleteExamples.cpp
            Apps/DemoApp/UltraCanvasLabelExamples.cpp
            Apps/DemoApp/UltraCanvasMenuExamples.cpp
            Apps/DemoApp/UltraCanvasSliderExamples.cpp
            Apps/DemoApp/UltraCanvasColorPickerExamples.cpp
            Apps/DemoApp/UltraCanvasScrollbarExamples.cpp
            Apps/DemoApp/UltraCanvasSVGExamples.cpp
            Apps/DemoApp/UltraCanvasTabExamples.cpp
            Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp
            Apps/DemoApp/UltraCanvasGroupBoxExamples.cpp
            Apps/DemoApp/UltraCanvasTextAreaExamples.cpp
            Apps/DemoApp/UltraCanvasTextInputExamples.cpp
            Apps/DemoApp/UltraCanvasTreeViewExamples.cpp
            Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp
            Apps/DemoApp/UltraCanvasODTExamples.cpp
            Apps/DemoApp/UltraCanvasListViewExamples.cpp
            Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
            Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp
            Apps/DemoApp/UltraCanvasDivergingChartExamples.cpp
            Apps/DemoApp/UltraCanvasWatefallChartExamples.cpp
            Apps/DemoApp/UltraCanvasFinancialChartExamples.cpp
            Apps/DemoApp/UltraCanvasCheckboxExamples.cpp
            Apps/DemoApp/UltraCanvasDemoInfoWindow.cpp
            Apps/DemoApp/UltraCanvasSankeyExamples.cpp
            Apps/DemoApp/UltraCanvasTreeMapExamples.cpp
            Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasDendrogramExamples.cpp
            Apps/DemoApp/UltraCanvasBlockDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasNodeDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasFlowChartExamples.cpp
            Apps/DemoApp/UltraCanvasGourceTreeExamples.cpp
            Apps/DemoApp/UltraCanvasJitterPlotExamples.cpp
            Apps/DemoApp/UltraCanvasPopulationChartExamples.cpp
            Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp
            Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp
            Apps/DemoApp/UltraCanvasLayoutExamples.cpp
            Apps/DemoApp/UltraCanvasToolbarExamples.cpp
            Apps/DemoApp/UltraCanvasTabExamples.cpp
            Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp
            Apps/DemoApp/UltraCanvasTextRenderingExamples.cpp
            Apps/DemoApp/UltraCanvasPieChartExamples.cpp
            Apps/DemoApp/UltraCanvasRadarChartExamples.cpp
            Apps/DemoApp/UltraCanvasHeatmapExamples.cpp
            Apps/DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasArcDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasNetworkingExamples.cpp
            Apps/DemoApp/UltraCanvasSlideshowExamples.cpp
            Apps/DemoApp/UltraCanvasAlbumExamples.cpp
            Apps/DemoApp/UltraCanvasMediaViewerExamples.cpp
            Apps/DemoApp/UltraCanvasCompositorDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasGaugeExamples.cpp
            Apps/DemoApp/UltraCanvasQRCodeExamples.cpp
            Apps/DemoApp/UltraCanvasBarcodeExamples.cpp
            Apps/DemoApp/UltraCanvasAudioExamples.cpp
            Apps/DemoApp/UltraCanvasEBookExamples.cpp
            Apps/DemoApp/UltraCanvasWaveformExamples.cpp
            Apps/DemoApp/UltraCanvasPDFExamples.cpp
            Apps/DemoApp/UltraCanvasLaTeXExamples.cpp
            Apps/DemoApp/UltraCanvasFileLoaderExamples.cpp
            Apps/DemoApp/UltraCanvasVideoExamples.cpp
    )

    # These files depend on libvips
    if(LIBVIPS_FOUND)
        list(APPEND DEMO_SOURCES
            Apps/DemoApp/UltraCanvasBitmapExamples.cpp
            Apps/DemoApp/UltraCanvasBitmapFormatDemo.cpp
        )
    endif()

    # Only include GL examples if GL support is enabled
    if(ULTRACANVAS_ENABLE_GL)
        list(APPEND DEMO_SOURCES
            Apps/DemoApp/UltraCanvasGLSurfaceExamples.cpp
            Apps/DemoApp/UltraCanvasGLModelsTab.cpp
            Apps/DemoApp/UltraCanvasGLShaderTab.cpp
            Apps/DemoApp/UltraCanvasGLZarchTab.cpp
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

        if(PLUGIN_TARGET MATCHES "OCR")
            list(APPEND DEMO_SOURCES Apps/DemoApp/UltraCanvasOCRExamples.cpp)
        endif()

        if(PLUGIN_TARGET MATCHES "Vectorizer")
            list(APPEND DEMO_SOURCES Apps/DemoApp/UltraCanvasVectorizerExamples.cpp)
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
    # Export the executable's dynamic symbols so on-demand plugin modules
    # (dlopen'ed via the loader, e.g. libUltraCanvasLaTeX) can resolve
    # UltraCanvas core symbols when the core is linked statically.
    set_target_properties(UltraCanvasDemo PROPERTIES ENABLE_EXPORTS ON)

    # Include directories
    target_include_directories(UltraCanvasDemo PRIVATE
        ${ULTRACANVAS_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/Apps/DemoApp
    )
    
    # Link against UltraCanvas core library
    target_link_libraries(UltraCanvasDemo PRIVATE
        ${ULTRACANVAS_LIBRARY}
        ${JSONCPP_LIBRARIES}
    )

    # The core library resolves fmt too, but its imported target is scoped to
    # that directory — resolve again here (cheap) before falling back to a fetch.
    if(NOT TARGET fmt::fmt)
        find_package(fmt CONFIG QUIET)
    endif()
    if(NOT TARGET fmt::fmt)
        FetchContent_Declare(
                fmt
                GIT_REPOSITORY https://github.com/fmtlib/fmt
                GIT_TAG        123913715afeb8a437e6388b4473fcc4753e1c9a) # 11.1.4
        FetchContent_MakeAvailable(fmt)
    endif()
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
        UCAPP_ICON_PATH="media/appicon/Demo.png"
        $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
        $<$<BOOL:${ULTRACANVAS_ENABLE_GL}>:ULTRACANVAS_ENABLE_GL=1>
        # macOS deprecated OpenGL in 10.14; silence per-call warnings in the GL demo code.
        $<$<AND:$<BOOL:${ULTRACANVAS_ENABLE_GL}>,$<BOOL:${APPLE}>>:GL_SILENCE_DEPRECATION=1>
    )

    # Embed app icon in .exe (Windows only)
    ultracanvas_embed_app_icon(UltraCanvasDemo
        "${CMAKE_CURRENT_SOURCE_DIR}/media/appicon/Demo.png")
    
    # Compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(UltraCanvasDemo PRIVATE
#            -Wall -Wextra
        )
    endif()
    
    # Set executable properties
#    set_target_properties(UltraCanvasDemo PROPERTIES
#        OUTPUT_NAME "UltraCanvasDemo"
#        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/..
#    )
    
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

    # The tests are self-contained (no test framework dependency).
    add_subdirectory(Tests)
endif()

# ===== ULTRANET TESTS =====
# Independent of BUILD_TESTS / Catch2: uses a tiny in-tree framework so it
# can run wherever UltraNet is built.
if(ULTRACANVAS_BUILD_NET_TESTS AND ULTRACANVAS_ENABLE_NET)
    enable_testing()
    add_subdirectory(Tests/UltraNet)
    message(STATUS "")
    message(STATUS "── UltraNet test suite enabled (target: UltraNetTests) ──")
endif()

# ===== ULTRADATABASE TESTS =====
# Independent of BUILD_TESTS / Catch2: uses a tiny in-tree framework so it
# can run wherever UltraDatabase is built.
option(ULTRACANVAS_BUILD_DATABASE_TESTS "Build the UltraDatabase test suite" OFF)
if(ULTRACANVAS_BUILD_DATABASE_TESTS AND TARGET UltraDatabase)
    enable_testing()
    add_subdirectory(Tests/UltraDatabase)
    message(STATUS "")
    message(STATUS "── UltraDatabase test suite enabled (target: UltraDatabaseTests) ──")
endif()

# ===== ULTRAMAIL ENGINE TESTS =====
# Headless engine tests (LocalStore on UltraDatabase); in-tree framework.
option(ULTRACANVAS_BUILD_ULTRAMAIL_TESTS "Build the UltraMail engine test suite" OFF)
if(ULTRACANVAS_BUILD_ULTRAMAIL_TESTS AND TARGET UltraMailEngine)
    enable_testing()
    add_subdirectory(Tests/UltraMail)
    message(STATUS "")
    message(STATUS "── UltraMail engine test suite enabled (target: UltraMailEngineTests) ──")
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
            Apps/Texter/UltraCanvasTextEditorInfoFileStatistics.cpp
            Apps/Texter/UltraCanvasEncoding.cpp
            Apps/Texter/main.cpp
            Apps/Texter/UltraCanvasSearchBar.cpp
    )
    # Export dynamic symbols so on-demand plugin modules can resolve core
    # symbols when the core is linked statically (see UltraCanvasDemo).
    set_target_properties(UltraCanvasTexter PROPERTIES ENABLE_EXPORTS ON)

    # Include directories
    target_include_directories(UltraCanvasTexter PRIVATE
            ${ULTRACANVAS_INCLUDE_DIRS}
    )

    # Link against UltraCanvas core library
    target_link_libraries(UltraCanvasTexter PRIVATE
            ${ULTRACANVAS_LIBRARY}
            ${JSONCPP_LIBRARIES}
    )
    target_link_libraries(UltraCanvasTexter PRIVATE fmt::fmt)

    # iconv - on Linux it's part of glibc, on macOS it needs explicit linking
if(APPLE)
    target_link_libraries(UltraCanvasTexter PRIVATE iconv)
endif()

if(WIN32)
    target_link_libraries(UltraCanvasTexter PRIVATE iconv)
endif()

    # Compile features
    target_compile_features(UltraCanvasTexter PRIVATE cxx_std_20)

    # Compile definitions
    target_compile_definitions(UltraCanvasTexter PRIVATE
            ULTRACANVAS_DEMO_VERSION="${PROJECT_VERSION}"
            UCAPP_ICON_PATH="media/appicon/Texter.png"
            $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )

    # Embed app icon in .exe (Windows only)
    ultracanvas_embed_app_icon(UltraCanvasTexter
        "${CMAKE_CURRENT_SOURCE_DIR}/media/appicon/Texter.png")

    # Compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(UltraCanvasTexter PRIVATE
                #            -Wall -Wextra
        )
    endif()

    # Set executable properties
#    set_target_properties(UltraCanvasTexter PROPERTIES
#            OUTPUT_NAME "UltraCanvasTexter"
#            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/..
#    )
endif ()

if (BUILD_CSSLAYOUT_TESTS)
    add_executable(CSSLayoutTests Apps/CSSLayoutTests/main.cpp)
    target_include_directories(CSSLayoutTests PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
    target_link_libraries(CSSLayoutTests PRIVATE ${ULTRACANVAS_LIBRARY})
    target_compile_features(CSSLayoutTests PRIVATE cxx_std_20)
endif()

# ===== ULTRAMAIL GUI APPLICATION =====
# The full UI executable (Toolbox, account info-tile bar, setup wizard) on top
# of the UltraMail engine. Links the whole UltraCanvas UI library, so it is
# OFF by default; enable with -DBUILD_ULTRAMAIL_APP=ON.
option(BUILD_ULTRAMAIL_APP "Build the UltraMail GUI application" OFF)
if (BUILD_ULTRAMAIL_APP AND TARGET UltraMailEngine)
    add_executable(UltraMail
            Apps/UltraMail/main.cpp
            Apps/UltraMail/ui/UltraMailApp.cpp
            Apps/UltraMail/ui/UltraMailToolbox.cpp
            Apps/UltraMail/ui/UltraMailInfoTileBar.cpp
            Apps/UltraMail/ui/UltraMailAccountWizard.cpp
            Apps/UltraMail/ui/UltraMailAttachmentStrip.cpp
            Apps/UltraMail/ui/UltraMailContactsView.cpp
            Apps/UltraMail/ui/UltraMailReadingView.cpp
    )
    set_target_properties(UltraMail PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(UltraMail PRIVATE
            ${ULTRACANVAS_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraMail
    )
    target_link_libraries(UltraMail PRIVATE
            ${ULTRACANVAS_LIBRARY}
            UltraMailEngine
            ${JSONCPP_LIBRARIES}
    )
    if(TARGET fmt::fmt)
        target_link_libraries(UltraMail PRIVATE fmt::fmt)
    endif()
    target_compile_features(UltraMail PRIVATE cxx_std_20)
    message(STATUS "  [✓] UltraMail GUI app - ENABLED (target: UltraMail)")
endif()

if (BUILD_TESTS)
    # Headless eBook viewer smoke test (links the full library).
    add_executable(EBookViewerTest Tests/EBookViewerTest.cpp)
    target_include_directories(EBookViewerTest PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
    target_link_libraries(EBookViewerTest PRIVATE ${ULTRACANVAS_LIBRARY})
    target_compile_features(EBookViewerTest PRIVATE cxx_std_20)
    set_target_properties(EBookViewerTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME EBookViewerTest COMMAND EBookViewerTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if (BUILD_FILEMANAGER_APP)
    set(UCFILEMANAGER_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/ListView/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/ListView/DemoListViewFileManager.cpp
            a.cpp
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
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/..
    )
endif()

# Hide console window on Windows Release builds
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(BUILD_DEMO_APP)
        set_target_properties(UltraCanvasDemo PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_TEXTER_APP)
        set_target_properties(UltraCanvasTexter PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_FILEMANAGER_APP)
        set_target_properties(UCFileManager PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

add_custom_target(copy_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/media ${CMAKE_CURRENT_BINARY_DIR}/share/UltraCanvas/media
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Docs ${CMAKE_CURRENT_BINARY_DIR}/share/UltraCanvas/Docs
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Apps/DemoApp ${CMAKE_CURRENT_BINARY_DIR}/share/UltraCanvas/DemoApp
)
if(BUILD_TEXTER_APP)
    add_dependencies(UltraCanvasTexter copy_assets)
endif()
if(BUILD_DEMO_APP)
    add_dependencies(UltraCanvasDemo copy_assets)
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
