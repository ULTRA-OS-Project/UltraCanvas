# CMakeLists.txt (Root)
# UltraCanvas Demo Application - Links against core library and plugins
# Version: 1.0.0
# Last Modified: 2025-12-14
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# Version comes from the first line of Docs/UltraCanvas/CHANGELOG.md — the same
# line the packaging scripts read for the artefact file names. Must be included
# before project(), which needs ULTRACANVAS_VERSION.
include(cmake/UltraCanvasVersion.cmake)

project(UltraCanvasDemoApp
    VERSION ${ULTRACANVAS_VERSION}
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

# Build every target position-independent so the static sublibs (UltraNet,
# UltraDatabase, microtex_core, UltraMailEngine, VirtualFS) can be linked into
# the shared libUltraCanvas.so (ULTRACANVAS_BUILD_SHARED). Negligible cost on
# x86-64 and harmless for the static build.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# Executables go to the build root so the portable Linux package can place them
# alongside lib/ and share/ (see package-linux.sh).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ===== ANDROID DEFAULTS (phase 1: core library only) =====
# Cache defaults only (no FORCE): -D on the command line still wins, so any of
# these can be opted back in once its dependency exists in the NDK sysroot.
# The desktop app executables are meaningless in an APK (an Android app is a
# shared library loaded by the activity); packaging DemoApp via Gradle is a
# later phase (Docs/UltraCanvas/AndroidPortInvestigation.md §5, §7).
if(ANDROID)
    set(BUILD_TEXTER_APP OFF CACHE BOOL "Build the texter application")
    set(BUILD_ULTRAFILER_APP OFF CACHE BOOL "Build the UltraFiler file manager application")
    set(BUILD_ULTRAVIEWER_APP OFF CACHE BOOL "Build the UltraViewer universal media viewer application")
    set(BUILD_DEMO_APP OFF CACHE BOOL "Build the demo application")
    set(BUILD_ULTRAAI_APP OFF CACHE BOOL "Build the UltraAI dashboard demo application")
    set(BUILD_ULTRAMAIL OFF CACHE BOOL "Build the UltraMail application")
    set(BUILD_CSSLAYOUT_TESTS OFF CACHE BOOL "Build the CSSLayout non-UI test harness")
    # Image processing: libvips' ~10-library transitive stack is not in a
    # minimal sysroot; the HAS_LIBVIPS fallbacks cover phase 1.
    set(ULTRACANVAS_ENABLE_VIPS OFF CACHE BOOL "Enable libvips-backed raster image processing")
    # Runtime-dlopen'ed plugin DSOs don't load from outside the APK on Android
    # (investigation §4.3); miniaudio/GStreamer/MuPDF/tesseract/cargo builds are
    # all post-phase-1.
    set(ULTRACANVAS_PLUGIN_LATEX OFF CACHE BOOL "Build LaTeX math plugin (embedded MicroTeX)")
    set(ULTRACANVAS_PLUGIN_CDR OFF CACHE BOOL "Build CDR plugin (requires libcdr)")
    set(ULTRACANVAS_PLUGIN_XAR OFF CACHE BOOL "Build XARA plugin")
    set(ULTRACANVAS_PLUGIN_VECTOR OFF CACHE BOOL "Build Vector plugin")
    set(ULTRACANVAS_PLUGIN_PDF OFF CACHE BOOL "Build PDF plugin (requires MuPDF / libmupdf-dev)")
    set(ULTRACANVAS_PLUGIN_OCR OFF CACHE BOOL "Build OCR plugin (requires tesseract + leptonica)")
    set(ULTRACANVAS_PLUGIN_VECTORIZER OFF CACHE BOOL "Build Vectorizer plugin (raster→SVG via VTracer; requires cargo)")
    set(ULTRACANVAS_ENABLE_AUDIO OFF CACHE BOOL "Build audio backend")
    set(ULTRACANVAS_ENABLE_VIDEO OFF CACHE BOOL "Enable video playback & recording (requires platform media framework)")
    set(ULTRACANVAS_ENABLE_DATABASE OFF CACHE BOOL "Build the UltraDatabase data-access library")
    set(ULTRACANVAS_USE_VIRTUALFS OFF CACHE BOOL "Build the VirtualFS module and the UltraCanvas bridge to it")
    # UltraNet works on Android (Linux TLS/proxy sources + mandatory c-ares)
    # but needs curl/openssl/c-ares in the sysroot; opt in explicitly.
    set(ULTRACANVAS_ENABLE_NET OFF CACHE BOOL "Build the UltraNet networking library")
endif()

# ===== BUILD OPTIONS =====
option(BUILD_TEXTER_APP "Build the texter application" ON)
option(BUILD_ULTRAFILER_APP "Build the UltraFiler file manager application" ON)
option(BUILD_ULTRAVIEWER_APP "Build the UltraViewer universal media viewer application" ON)
option(BUILD_FILEMANAGER_APP "Build the file manager application" OFF) # OFF: source files not yet committed
option(BUILD_DEMO_APP "Build the demo application" ON)
option(BUILD_ULTRAAI_APP "Build the UltraAI dashboard demo application" ON)
option(BUILD_ULTRAAI_MODULE "Build the UltraAI module (interfaces, registry, adapters)" ON)
option(BUILD_CSSLAYOUT_TESTS "Build the CSSLayout non-UI test harness" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(ULTRACANVAS_BUILD_NET_TESTS "Build the UltraNet test suite" OFF)
option(ULTRACANVAS_PLUGIN_CDR "Build CDR plugin (requires libcdr)" ON)
option(ULTRACANVAS_PLUGIN_XAR "Build XARA plugin" ON)
option(ULTRACANVAS_ENABLE_AUDIO "Build audio backend" ON)
option(ULTRACANVAS_PLUGIN_VECTOR "Build Vector plugin" OFF)
option(ULTRACANVAS_PLUGIN_PDF "Build PDF plugin (requires MuPDF / libmupdf-dev)" ON)
option(ULTRACANVAS_PLUGIN_OCR "Build OCR plugin (requires tesseract + leptonica)" ON)
option(ULTRACANVAS_PLUGIN_VECTORIZER "Build Vectorizer plugin (raster→SVG via VTracer; requires cargo)" ON)
option(ULTRACANVAS_USE_VIRTUALFS "Build the VirtualFS module and the UltraCanvas bridge to it" ON)

include(FetchContent)
include(cmake/UltraCanvasAppIcon.cmake)
include(cmake/UltraCanvasWinManifest.cmake)

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
# The demo's chart pages use the typed factories of these selectable charts,
# so the demo build declares its chart set (chart engine proposal §12.11).
# A -DULTRACANVAS_CHARTS=... on the command line still wins (cache-first).
if(BUILD_DEMO_APP)
    set(ULTRACANVAS_CHARTS "ParallelCoordinate" CACHE STRING
        "Selectable charts linked into the library: 'all', empty, or a ;-list of keys")
endif()

# Which platforms ship a native folder-watch backend (UltraCanvasFolderWatcher:
# inotify / ReadDirectoryChangesW). Decided HERE, above every add_subdirectory,
# because the library and Tests/ are sibling scopes and both must agree: the
# core file compiles a null-returning fallback unless this is set, so a
# disagreement is either a duplicate symbol or a missing one at link time.
# Adding a platform means dropping its backend under OS/<Platform>/ and adding
# it to the condition below.
if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN))
    set(ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH ON)
endif()

add_subdirectory(UltraCanvas)

# ===== BUILD ULTRAAI MODULE =====
# Sibling module — capability interfaces + registry + adapters. Built on
# its own switch so libraries and apps can link UltraAI_Core without
# dragging in the dashboard demo app (and vice versa).
if(BUILD_ULTRAAI_MODULE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/UltraAI/CMakeLists.txt")
    message(STATUS "")
    message(STATUS "── Building UltraAI Module ──")
    # In-tree builds hand UltraAI the UltraNet target by default so the
    # adapter-shared library gets its UltraNetTransport; -DULTRAAI_USE_ULTRANET
    # on the command line still wins (cache-first).
    if(TARGET UltraNet)
        set(ULTRAAI_USE_ULTRANET ON CACHE BOOL
            "Enable adapters that depend on UltraNet")
    endif()
    if(TARGET UltraVault)
        set(ULTRAAI_USE_ULTRAVAULT ON CACHE BOOL
            "Enable UltraVault credential lookup")
    endif()
    add_subdirectory(UltraAI)
endif()
    
# ===== ULTRAMAIL =====
# The mail application. Phase 1 builds the headless engine (LocalStore on
# UltraDatabase); UI targets are added in later phases. Enabled by default
# whenever UltraDatabase is available.
option(BUILD_ULTRAMAIL "Build the UltraMail application" ON)
if(BUILD_ULTRAMAIL AND TARGET UltraDatabase)
    add_subdirectory(Apps/UltraMail)
endif()

# ===== ULTRASOCIAL =====
# Cross-posting to social networks. Phase 1 builds the headless engine
# (connectors + store + composer); UI targets are added in later phases.
option(BUILD_ULTRASOCIAL "Build the UltraSocial application" ON)
if(BUILD_ULTRASOCIAL AND TARGET UltraDatabase)
    add_subdirectory(Apps/UltraSocial)
endif()

# ===== ULTRACLEANER =====
# Removes the files macOS, Windows and Linux leave behind: temporary files,
# application and browser caches, logs, crash reports, thumbnail databases,
# package-manager downloads, build leftovers and the trash. Builds the
# headless engine plus the GUI application.
option(BUILD_ULTRACLEANER "Build the UltraCleaner application" ON)
if(BUILD_ULTRACLEANER)
    message(STATUS "")
    message(STATUS "── Building UltraCleaner ──")
    add_subdirectory(Apps/UltraCleaner)
endif()

# At this point, the following variables are available from UltraCanvas/CMakeLists.txt:
#   ULTRACANVAS_LIBRARY        - Main library target name
#   ULTRACANVAS_LIBRARIES      - All libraries (core + plugins)
#   ULTRACANVAS_PLUGIN_TARGETS - List of plugin targets

#   ULTRACANVAS_INCLUDE_DIRS   - All include directories

# ===== ULTRAAUTHENTICATOR =====
# TOTP/HOTP authenticator. The core (OTP engine, otpauth:// parser, encrypted
# vault, account layer) needs only UltraCrypt and builds headless; the GUI is
# added on top when the framework is available.
option(BUILD_ULTRAAUTHENTICATOR "Build the UltraAuthenticator application" ON)
if(BUILD_ULTRAAUTHENTICATOR AND TARGET UltraCrypt)
    add_subdirectory(Apps/UltraAuthenticator)
endif()

# ===== DEMO APPLICATION =====
if(BUILD_DEMO_APP)
    message(STATUS "")
    message(STATUS "── Building Demo Application ──")

    # Demo app source files
    if(NOT ULTRACANVAS_ENABLE_CHARTS)
        message(FATAL_ERROR "BUILD_DEMO_APP requires ULTRACANVAS_ENABLE_CHARTS=ON (the demo's chart pages need the bundled charts)")
    endif()
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
            Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp
            Apps/DemoApp/UltraCanvasSliderExamples.cpp
            Apps/DemoApp/UltraCanvasSpinnerExamples.cpp
            Apps/DemoApp/UltraCanvasTimePickerExamples.cpp
            Apps/DemoApp/UltraCanvasAlertExamples.cpp
            Apps/DemoApp/UltraCanvasPaginationExamples.cpp
            Apps/DemoApp/UltraCanvasRatingExamples.cpp
            Apps/DemoApp/UltraCanvasStepperExamples.cpp
            Apps/DemoApp/UltraCanvasChipExamples.cpp
            Apps/DemoApp/UltraCanvasBadgeExamples.cpp
            Apps/DemoApp/UltraCanvasColorPickerExamples.cpp
            Apps/DemoApp/UltraCanvasScrollbarExamples.cpp
            Apps/DemoApp/UltraCanvasSVGExamples.cpp
            Apps/DemoApp/UltraCanvasTabExamples.cpp
            Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp
            Apps/DemoApp/UltraCanvasGroupBoxExamples.cpp
            Apps/DemoApp/UltraCanvasTooltipExamples.cpp
            Apps/DemoApp/UltraCanvasTextAreaExamples.cpp
            Apps/DemoApp/UltraCanvasTextInputExamples.cpp
            Apps/DemoApp/UltraCanvasTreeViewExamples.cpp
            Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp
            Apps/DemoApp/UltraCanvasODTExamples.cpp
            Apps/DemoApp/UltraCanvasListViewExamples.cpp
            Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
            Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp
            Apps/DemoApp/UltraCanvasBarChartExamples.cpp
            Apps/DemoApp/UltraCanvasDivergingChartExamples.cpp
            Apps/DemoApp/UltraCanvasDumbbellChartExamples.cpp
            Apps/DemoApp/UltraCanvasFunnelChartExamples.cpp
            Apps/DemoApp/UltraCanvasPyramidChartExamples.cpp
            Apps/DemoApp/UltraCanvasWatefallChartExamples.cpp
            Apps/DemoApp/UltraCanvasMekkoChartExamples.cpp
            Apps/DemoApp/UltraCanvasNestedAreaChartExamples.cpp
            Apps/DemoApp/UltraCanvasFinancialChartExamples.cpp
            Apps/DemoApp/UltraCanvasCheckboxExamples.cpp
            Apps/DemoApp/UltraCanvasDemoInfoWindow.cpp
            Apps/DemoApp/UltraCanvasSankeyExamples.cpp
            Apps/DemoApp/UltraCanvasTreeMapExamples.cpp
            Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasWordCloudExamples.cpp
            Apps/DemoApp/UltraCanvasDendrogramExamples.cpp
            Apps/DemoApp/UltraCanvasBlockDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasClassDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasNodeDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasERDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasFlowChartExamples.cpp
            Apps/DemoApp/UltraCanvasPertChartExamples.cpp
            Apps/DemoApp/UltraCanvasGourceTreeExamples.cpp
            Apps/DemoApp/UltraCanvasGitGraphExamples.cpp
            Apps/DemoApp/UltraCanvasJitterPlotExamples.cpp
            Apps/DemoApp/UltraCanvasBubbleChartExamples.cpp
            Apps/DemoApp/UltraCanvasQuadrantChartExamples.cpp
            Apps/DemoApp/UltraCanvasSWOTDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasCircleDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasFishboneDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasMatrixDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasSequenceDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasTimelineDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasTimelineChartExamples.cpp
            Apps/DemoApp/UltraCanvasGanttChartExamples.cpp
            Apps/DemoApp/UltraCanvasKanbanBoardExamples.cpp
            Apps/DemoApp/UltraCanvasPopulationChartExamples.cpp
            Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp
            Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp
            Apps/DemoApp/UltraCanvasLayoutExamples.cpp
            Apps/DemoApp/UltraCanvasLabelPlacementExamples.cpp
            Apps/DemoApp/UltraCanvasToolbarExamples.cpp
            Apps/DemoApp/UltraCanvasTabExamples.cpp
            Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp
            Apps/DemoApp/UltraCanvasTextRenderingExamples.cpp
            Apps/DemoApp/UltraCanvasPieChartExamples.cpp
            Apps/DemoApp/UltraCanvasSunburstChartExamples.cpp
            Apps/DemoApp/UltraCanvasChordChartExamples.cpp
            Apps/DemoApp/UltraCanvasCircularInfoGraphicExamples.cpp
            Apps/DemoApp/UltraCanvasRadialBarChartExamples.cpp
            Apps/DemoApp/UltraCanvasCircularProgressChartExamples.cpp
            Apps/DemoApp/UltraCanvasPolarChartExamples.cpp
            Apps/DemoApp/UltraCanvasChartEngineExamples.cpp
            Apps/DemoApp/UltraCanvasRadarChartExamples.cpp
            Apps/DemoApp/UltraCanvasParallelCoordinateChartExamples.cpp
            Apps/DemoApp/UltraCanvasHeatmapExamples.cpp
            Apps/DemoApp/UltraCanvasContourChartExamples.cpp
            Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasArcDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasRequirementDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasMindMapExamples.cpp
            Apps/DemoApp/UltraCanvasNetworkingExamples.cpp
            Apps/DemoApp/UltraCanvasSlideshowExamples.cpp
            Apps/DemoApp/UltraCanvasAlbumExamples.cpp
            Apps/DemoApp/UltraCanvasFilerExamples.cpp
            Apps/DemoApp/UltraCanvasMediaViewerExamples.cpp
            Apps/DemoApp/UltraCanvasCompositorDiagramExamples.cpp
            Apps/DemoApp/UltraCanvasGaugeExamples.cpp
            Apps/DemoApp/UltraCanvasQRCodeExamples.cpp
            Apps/DemoApp/UltraCanvasBarcodeExamples.cpp
            Apps/DemoApp/UltraCanvasAudioExamples.cpp
            Apps/DemoApp/UltraCanvasAudioFXExamples.cpp
            Apps/DemoApp/UltraCanvasEBookExamples.cpp
            Apps/DemoApp/UltraCanvasWaveformExamples.cpp
            Apps/DemoApp/UltraCanvasSpectrogramExamples.cpp
            Apps/DemoApp/UltraCanvasLevelMeterExamples.cpp
            Apps/DemoApp/UltraCanvasAudioAnalysisExamples.cpp
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
            Apps/DemoApp/UltraCanvasPixelFXExamples.cpp
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
                ${HOMEBREW_PREFIX}/lib
                ${HOMEBREW_PREFIX}/opt/vips/lib
        )
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        link_directories(
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

    # Embed manifest with UTF-8 active code page (Windows only) so paths with
    # characters outside the legacy ANSI code page work in all narrow APIs
    ultracanvas_embed_windows_manifest(UltraCanvasDemo)
    
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

# ===== ULTRAWIN MANAGER =====
# UltraWin Manager: the graphical front-end for Windows applications on
# ULTRA OS — environments, installed programs, and the Windows VM.
option(BUILD_ULTRAWIN_MANAGER "Build the UltraWin Manager application" ON)
if(BUILD_ULTRAWIN_MANAGER AND TARGET UltraWin)
    add_executable(UltraWinManager
        Apps/UltraWinManager/main.cpp
        Apps/UltraWinManager/UltraWinManagerWindow.cpp
    )
    target_include_directories(UltraWinManager PRIVATE
        ${ULTRACANVAS_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraWinManager
    )
    target_link_libraries(UltraWinManager PRIVATE
        ${ULTRACANVAS_LIBRARY}
        UltraWin
    )
    message(STATUS "  [✓] UltraWinManager - ENABLED")
endif()

# ===== ULTRAWIN SETUP CLI =====
# ultrawin-setup: the console driver for the VM tier's real-hardware
# validation (provision from install media, boot, watch, launch). Plain
# CLI over the UltraWin API — built wherever UltraWin is.
option(BUILD_ULTRAWIN_SETUP "Build the ultrawin-setup CLI tool" ON)
if(BUILD_ULTRAWIN_SETUP AND TARGET UltraWin)
    add_executable(ultrawin-setup Apps/UltraWinSetup/main.cpp)
    target_compile_features(ultrawin-setup PRIVATE cxx_std_20)
    target_link_libraries(ultrawin-setup PRIVATE UltraWin)
    message(STATUS "  [✓] ultrawin-setup CLI - ENABLED")
endif()

# ===== ULTRAWIN TESTS =====
# Independent of BUILD_TESTS / Catch2: uses the same tiny in-tree framework
# as the UltraNet suite. Wine itself is NOT required — wine-dependent tests
# skip themselves when no binary is found.
option(ULTRACANVAS_BUILD_ULTRAWIN_TESTS "Build the UltraWin test suite" OFF)
if(ULTRACANVAS_BUILD_ULTRAWIN_TESTS AND TARGET UltraWin)
    enable_testing()
    add_subdirectory(Tests/UltraWin)
    message(STATUS "")
    message(STATUS "── UltraWin test suite enabled (target: UltraWinTests) ──")
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

# ===== ULTRASOCIAL ENGINE TESTS =====
# Headless engine tests (composer, vault, store, connectors against
# loopback fake servers); in-tree framework.
option(ULTRACANVAS_BUILD_ULTRASOCIAL_TESTS "Build the UltraSocial engine test suite" OFF)
if(ULTRACANVAS_BUILD_ULTRASOCIAL_TESTS AND TARGET UltraSocialEngine)
    enable_testing()
    add_subdirectory(Tests/UltraSocial)
    message(STATUS "")
    message(STATUS "── UltraSocial engine test suite enabled (target: UltraSocialEngineTests) ──")
endif()

# ===== ULTRACLEANER ENGINE TESTS =====
# Headless engine tests: the path guard, the rule table, the scanner and the
# remover, all driven over temporary trees. Enabled on its own switch.
option(ULTRACANVAS_BUILD_ULTRACLEANER_TESTS "Build the UltraCleaner engine test suite" OFF)
if(ULTRACANVAS_BUILD_ULTRACLEANER_TESTS AND TARGET UltraCleanerEngine)
    enable_testing()
    add_subdirectory(Tests/UltraCleaner)
    message(STATUS "")
    message(STATUS "── UltraCleaner engine test suite enabled (target: UltraCleanerEngineTests) ──")
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
            # UltraTexter has its own changelog and its own version line; this
            # target must define it too, or the editor built from here would
            # report a different version than the one built from
            # Apps/Texter/CMakeLists.txt.
            ULTRATEXTER_VERSION="${ULTRATEXTER_VERSION}"
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
endif()

# ===== ULTRAFILER APPLICATION =====
# Windows Explorer style file manager: folder tree (UltraCanvasTreeView) +
# folder content (UltraCanvasFilerWidget) + media preview
# (UltraCanvasMediaViewer) in a three-pane split window.
if (BUILD_ULTRAFILER_APP)
    message(STATUS "")
    message(STATUS "── Building UltraFiler Application ──")

    add_executable(UltraFiler
            Apps/UltraFiler/main.cpp
            Apps/UltraFiler/UltraFilerWindow.cpp
            Apps/UltraFiler/UltraFilerSettingsDialog.cpp
            Apps/UltraFiler/UltraFilerPrompt.cpp
            Apps/UltraFiler/UltraFilerPropertiesDialogs.cpp
            Apps/UltraFiler/UltraFilerShare.cpp
    )
    # Export dynamic symbols so on-demand plugin modules can resolve core
    # symbols when the core is linked statically (see UltraCanvasDemo).
    set_target_properties(UltraFiler PROPERTIES ENABLE_EXPORTS ON)

    target_include_directories(UltraFiler PRIVATE
            ${ULTRACANVAS_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraFiler
    )

    target_link_libraries(UltraFiler PRIVATE
            ${ULTRACANVAS_LIBRARY}
    )
    target_link_libraries(UltraFiler PRIVATE fmt::fmt)

    target_compile_features(UltraFiler PRIVATE cxx_std_20)

    target_compile_definitions(UltraFiler PRIVATE
            # UltraFiler's own version, not the framework's — keep in sync
            # with UltraFiler.rc and UltraFiler.manifest.
            ULTRAFILER_VERSION="0.8.0"
            UCAPP_ICON_PATH="media/appicon/UltraFiler.png"
            $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )

    # Embed app icon in .exe (Windows only)
    ultracanvas_embed_app_icon(UltraFiler
        "${CMAKE_CURRENT_SOURCE_DIR}/media/appicon/UltraFiler.png")

    # Embed the application manifest + version info (Windows only). The .rc
    # ships its own manifest, so do NOT also call
    # ultracanvas_embed_windows_manifest here (two RT_MANIFEST id=1 resources
    # would collide at resource link time).
    if(WIN32)
        target_sources(UltraFiler PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraFiler/UltraFiler.rc
        )
        # windres/rc.exe resolves the manifest path relative to the .rc file's
        # directory.
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraFiler/UltraFiler.rc
            PROPERTIES
                COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraFiler"
        )
    endif()

    # No ultracanvas_embed_windows_manifest(UltraFiler) here: UltraFiler.rc
    # above already embeds Apps/UltraFiler/UltraFiler.manifest, which is a
    # superset of the generated one (UTF-8 code page + long paths, plus DPI
    # awareness, common controls v6 and segment heap). A second RT_MANIFEST
    # id=1 resource is a hard error with lld (CLANGARM64), even though GNU ld
    # let the duplicate through silently.

    message(STATUS "  UltraFiler executable: UltraFiler")
endif()

# ===== ULTRAVIEWER APPLICATION =====
# Universal media viewer: one full-window UltraCanvasMediaViewer displaying
# bitmaps, vector graphics, video/audio (with player transport controls),
# documents (PDF), e-books, spreadsheets, 3D models, text and UltraCanvas
# Document containers (*.ucd).
if (BUILD_ULTRAVIEWER_APP)
    message(STATUS "")
    message(STATUS "── Building UltraViewer Application ──")

    add_executable(UltraViewer
            Apps/UltraViewer/main.cpp
            Apps/UltraViewer/UltraViewerWindow.cpp
    )
    # Export dynamic symbols so on-demand plugin modules can resolve core
    # symbols when the core is linked statically (see UltraCanvasDemo).
    set_target_properties(UltraViewer PROPERTIES ENABLE_EXPORTS ON)

    target_include_directories(UltraViewer PRIVATE
            ${ULTRACANVAS_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraViewer
    )

    target_link_libraries(UltraViewer PRIVATE
            ${ULTRACANVAS_LIBRARY}
    )
    target_link_libraries(UltraViewer PRIVATE fmt::fmt)

    target_compile_features(UltraViewer PRIVATE cxx_std_20)

    target_compile_definitions(UltraViewer PRIVATE
            ULTRAVIEWER_VERSION="${PROJECT_VERSION}"
            UCAPP_ICON_PATH="media/appicon/UltraViewer.png"
            $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )

    # Embed app icon in .exe (Windows only)
    ultracanvas_embed_app_icon(UltraViewer
        "${CMAKE_CURRENT_SOURCE_DIR}/media/appicon/UltraViewer.png")

    # Embed manifest with UTF-8 active code page (Windows only) - the viewer
    # opens arbitrary paths, so narrow Win32/CRT calls must read them as UTF-8.
    ultracanvas_embed_windows_manifest(UltraViewer)

    message(STATUS "  UltraViewer executable: UltraViewer")
endif()

# ===== ULTRAAI APPLICATION =====
# Dashboard demo for the UltraAI module: grid of icon-buttons, modal
# dialog per capability, backed by the in-process mock adapters.
if(BUILD_ULTRAAI_APP AND NOT TARGET UltraAI_Core)
    message(STATUS "UltraAIApp skipped: requires the UltraAI module (BUILD_ULTRAAI_MODULE=ON)")
endif()
if(BUILD_ULTRAAI_APP AND TARGET UltraAI_Core)
    message(STATUS "")
    message(STATUS "── Building UltraAI Dashboard Application ──")

    add_executable(UltraAIApp
        Apps/UltraAIApp/main.cpp
        Apps/UltraAIApp/UltraAIDashboard.cpp
        Apps/UltraAIApp/UltraAIServiceDialog.cpp
        Apps/UltraAIApp/UltraAIDialogs.cpp
    )

    target_include_directories(UltraAIApp PRIVATE
        ${ULTRACANVAS_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraAIApp
    )

    target_link_libraries(UltraAIApp PRIVATE
        ${ULTRACANVAS_LIBRARY}
        UltraAI_Core
    )

    if(TARGET fmt::fmt)
        target_link_libraries(UltraAIApp PRIVATE fmt::fmt)
    endif()

    target_compile_features(UltraAIApp PRIVATE cxx_std_20)

    target_compile_definitions(UltraAIApp PRIVATE
        ULTRAAI_APP_VERSION="0.1.0"
        $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(UltraAIApp PRIVATE)
    endif()

    message(STATUS "  UltraAI executable: UltraAIApp")
endif()

if (BUILD_CSSLAYOUT_TESTS)
    add_executable(CSSLayoutTests Apps/CSSLayoutTests/main.cpp)
    target_include_directories(CSSLayoutTests PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
    target_link_libraries(CSSLayoutTests PRIVATE ${ULTRACANVAS_LIBRARY})
    target_compile_features(CSSLayoutTests PRIVATE cxx_std_20)
endif()

# ===== ULTRAMAIL GUI APPLICATION =====
# The full UI executable is defined alongside the engine in
# Apps/UltraMail/CMakeLists.txt (target: UltraMail), built whenever
# BUILD_ULTRAMAIL is ON and the UltraCanvas UI library is available.

if (BUILD_TESTS)
    # JSONTests below compiles vendored C (yyjson.c); the project() call only
    # declares CXX, so C must be enabled here - the enable_language(C) inside
    # Tests/CMakeLists.txt does not propagate up to this scope.
    enable_language(C)

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

    # UltraCanvasJSON unit tests (DataFormats section). Compiles only the JSON
    # module and its vendored engine, so it runs headless without the UI stack.
    add_executable(JSONTests
        Tests/DataFormats/JSONTests.cpp
        UltraCanvas/core/DataFormats/UltraCanvasJSON.cpp
        UltraCanvas/third_party/yyjson/yyjson.c
    )
    target_include_directories(JSONTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/third_party/yyjson
    )
    target_compile_features(JSONTests PRIVATE cxx_std_20)
    set_target_properties(JSONTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME JSONTests COMMAND JSONTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # UltraCrypt unit tests: published vectors (FIPS 180-4, RFC 4231), AEAD
    # round-trip plus the mandated negative cases, KDF behaviour and secure
    # buffer semantics. Compiles only UltraCrypt, so it runs headless.
    add_executable(UltraCryptTests Tests/UltraCryptTests.cpp)
    target_link_libraries(UltraCryptTests PRIVATE UltraCrypt)
    target_compile_features(UltraCryptTests PRIVATE cxx_std_20)
    set_target_properties(UltraCryptTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UltraCryptTests COMMAND UltraCryptTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # Authenticator vault: confidentiality on disk, wrong-password refusal,
    # tamper detection over the whole container, and durability of each
    # mutation. Links only UltraCrypt, so it runs headless.
    add_executable(UltraAuthenticatorStoreTests
        Tests/UltraAuthenticatorStoreTests.cpp
        Apps/UltraAuthenticator/store/EncryptedFileStore.cpp
    )
    target_link_libraries(UltraAuthenticatorStoreTests PRIVATE UltraCrypt)
    target_compile_features(UltraAuthenticatorStoreTests PRIVATE cxx_std_20)
    set_target_properties(UltraAuthenticatorStoreTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UltraAuthenticatorStoreTests COMMAND UltraAuthenticatorStoreTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # Encrypted backup file: round trip, refusal to reuse the master password,
    # tamper detection, and that a restore never overwrites a live account.
    # Links only UltraCrypt, so it runs headless.
    add_executable(UltraAuthenticatorExportTests
        Tests/UltraAuthenticatorExportTests.cpp
        Apps/UltraAuthenticator/AccountStore.cpp
        Apps/UltraAuthenticator/AccountExport.cpp
        Apps/UltraAuthenticator/store/EncryptedFileStore.cpp
        Apps/UltraAuthenticator/otp/UltraOtp.cpp
        Apps/UltraAuthenticator/otp/OtpAuthUri.cpp
        UltraCanvas/Plugins/Documents/UCDCryptoEnvelope.cpp
    )
    target_include_directories(UltraAuthenticatorExportTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraAuthenticator
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include)
    target_link_libraries(UltraAuthenticatorExportTests PRIVATE UltraCrypt)
    target_compile_features(UltraAuthenticatorExportTests PRIVATE cxx_std_20)
    set_target_properties(UltraAuthenticatorExportTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UltraAuthenticatorExportTests
             COMMAND UltraAuthenticatorExportTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # In-memory QR decode. Unlike the suites around it this one links the whole
    # framework, because the QR plugin is compiled into the UltraCanvas library
    # rather than standing alone. It skips itself cleanly when the library was
    # built without libzbar.
    add_executable(QRCodeScanImageTests Tests/QRCodeScanImageTests.cpp)
    target_link_libraries(QRCodeScanImageTests PRIVATE ${ULTRACANVAS_LIBRARY})
    target_include_directories(QRCodeScanImageTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include)
    target_compile_features(QRCodeScanImageTests PRIVATE cxx_std_20)
    set_target_properties(QRCodeScanImageTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME QRCodeScanImageTests COMMAND QRCodeScanImageTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # HOTP/TOTP engine and otpauth:// parser: RFC 4226 App. D and RFC 6238
    # App. B vectors, plus the provisioning-URI rejection cases. Links only
    # UltraCrypt, so it runs headless.
    add_executable(UltraOtpTests
        Tests/UltraOtpTests.cpp
        Apps/UltraAuthenticator/otp/UltraOtp.cpp
        Apps/UltraAuthenticator/otp/OtpAuthUri.cpp
    )
    target_link_libraries(UltraOtpTests PRIVATE UltraCrypt)
    target_compile_features(UltraOtpTests PRIVATE cxx_std_20)
    set_target_properties(UltraOtpTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UltraOtpTests COMMAND UltraOtpTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # Account layer: the boundary the UI talks to. Covers duplicate refusal,
    # HOTP counter durability across a reopen, type-mismatch refusal, and that
    # no seed, URI or label reaches the disk in the clear. Links only
    # UltraCrypt, so it runs headless.
    add_executable(UltraAuthenticatorAccountTests
        Tests/UltraAuthenticatorAccountTests.cpp
        Apps/UltraAuthenticator/AccountStore.cpp
        # AccountStore now exports and imports backups, so it needs the export
        # container and the envelope underneath it.
        Apps/UltraAuthenticator/AccountExport.cpp
        UltraCanvas/Plugins/Documents/UCDCryptoEnvelope.cpp
        Apps/UltraAuthenticator/store/EncryptedFileStore.cpp
        Apps/UltraAuthenticator/otp/UltraOtp.cpp
        Apps/UltraAuthenticator/otp/OtpAuthUri.cpp
    )
    target_include_directories(UltraAuthenticatorAccountTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Apps/UltraAuthenticator
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include)
    target_link_libraries(UltraAuthenticatorAccountTests PRIVATE UltraCrypt)
    target_compile_features(UltraAuthenticatorAccountTests PRIVATE cxx_std_20)
    set_target_properties(UltraAuthenticatorAccountTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UltraAuthenticatorAccountTests
             COMMAND UltraAuthenticatorAccountTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # UltraVault unit tests: memory-backend CRUD and lifecycle, encrypted-file
    # persistence, and the no-oracle property (wrong passphrase and a tampered
    # file are indistinguishable). Compiles only UltraVault + UltraCrypt.
    add_executable(UltraVaultTests Tests/UltraVaultTests.cpp)
    target_link_libraries(UltraVaultTests PRIVATE UltraVault)
    target_compile_features(UltraVaultTests PRIVATE cxx_std_20)
    set_target_properties(UltraVaultTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UltraVaultTests COMMAND UltraVaultTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # Cross-module: UltraAI's ResolveApiKey resolving apiKeyVaultRef through
    # UltraVault (the path enabled by ULTRAAI_USE_ULTRAVAULT).
    if(TARGET UltraAI_AdapterShared)
        add_executable(UltraAIVaultIntegrationTests
            Tests/UltraAIVaultIntegrationTests.cpp)
        target_link_libraries(UltraAIVaultIntegrationTests PRIVATE
            UltraAI_AdapterShared UltraVault)
        target_compile_features(UltraAIVaultIntegrationTests PRIVATE cxx_std_20)
        set_target_properties(UltraAIVaultIntegrationTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
        add_test(NAME UltraAIVaultIntegrationTests
                 COMMAND UltraAIVaultIntegrationTests
                 WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()

    # UCD document encryption envelope: round-trip, wrong password, tamper
    # detection and associated-data binding. Regression cover for the silently
    # broken encryption this replaced (Docs/Modules/UltraCrypt/README.md §1.1).
    add_executable(UCDCryptoEnvelopeTests
        Tests/UCDCryptoEnvelopeTests.cpp
        UltraCanvas/Plugins/Documents/UCDCryptoEnvelope.cpp
    )
    target_link_libraries(UCDCryptoEnvelopeTests PRIVATE UltraCrypt)
    target_include_directories(UCDCryptoEnvelopeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvas/include)
    target_compile_features(UCDCryptoEnvelopeTests PRIVATE cxx_std_20)
    set_target_properties(UCDCryptoEnvelopeTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME UCDCryptoEnvelopeTests COMMAND UCDCryptoEnvelopeTests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # Audio encode/decode roundtrip across every save-capable format the
    # build reports (WAV always; FLAC/OGG/Opus/MP3 with their codec libs).
    # Links the full library but runs headless - no window or device needed.
    add_executable(AudioCodecTests Tests/AudioCodecTests.cpp)
    target_include_directories(AudioCodecTests PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
    target_link_libraries(AudioCodecTests PRIVATE ${ULTRACANVAS_LIBRARY})
    target_compile_features(AudioCodecTests PRIVATE cxx_std_20)
    set_target_properties(AudioCodecTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME AudioCodecTests COMMAND AudioCodecTests
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

    # Embed manifest with UTF-8 active code page (Windows only)
    ultracanvas_embed_windows_manifest(UCFileManager)
endif()

# Hide console window on Windows Release builds
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(BUILD_DEMO_APP)
        set_target_properties(UltraCanvasDemo PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_TEXTER_APP)
        set_target_properties(UltraCanvasTexter PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_ULTRAFILER_APP)
        set_target_properties(UltraFiler PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_ULTRAVIEWER_APP)
        set_target_properties(UltraViewer PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_FILEMANAGER_APP)
        set_target_properties(UCFileManager PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(BUILD_ULTRAFILER_APP)
        set_target_properties(UltraFiler PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

add_custom_target(copy_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/media ${CMAKE_CURRENT_BINARY_DIR}/share/media
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Docs ${CMAKE_CURRENT_BINARY_DIR}/share/Docs
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Apps/DemoApp ${CMAKE_CURRENT_BINARY_DIR}/share/DemoApp
)
if(BUILD_TEXTER_APP)
    add_dependencies(UltraCanvasTexter copy_assets)
endif()
if(BUILD_DEMO_APP)
    add_dependencies(UltraCanvasDemo copy_assets)
endif()
if(BUILD_ULTRAFILER_APP)
    add_dependencies(UltraFiler copy_assets)
endif()
if(BUILD_ULTRAVIEWER_APP)
    add_dependencies(UltraViewer copy_assets)
endif()

# The LaTeX plugin is a dlopen()ed CMake MODULE - never linked by any app, so
# nothing forces it to rebuild when a core interface header (e.g. IRenderContext)
# changes. A single-target IDE build then rebuilds the core but leaves a stale
# libUltraCanvasLaTeX.so whose baked-in vtable indices no longer match the core,
# crashing on the first cross-module virtual call. Take an explicit build-order
# dependency (NOT a link) so any app that can load the module always rebuilds it.
if(TARGET UltraCanvasLaTeX)
    foreach(_uc_app UltraCanvasDemo UltraCanvasTexter UltraFiler UltraViewer UltraAIApp UCFileManager EBookViewerTest)
        if(TARGET ${_uc_app})
            add_dependencies(${_uc_app} UltraCanvasLaTeX)
        endif()
    endforeach()
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
