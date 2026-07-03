# UltraCanvas/CMakeLists.txt
# UltraCanvas Core Library - Builds as separate static library with optional plugins
# Version: 1.4.2
# Last Modified: 2026-06-21
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# Enable the C language: third_party/miniz/miniz.c is C (the only C source in the
# project) and provides ZIP support for ODS/XLSX File I/O. Without this, a CXX-only
# project silently skips compiling .c files, causing undefined references to mz_zip_*.
enable_language(C)

# ===== LIBRARY DEFINITION =====
set(ULTRACANVAS_VERSION "0.2.31")
set(ULTRACANVAS_LIB_NAME "UltraCanvas")

message(STATUS "")
message(STATUS "========================================")
message(STATUS "Configuring ${ULTRACANVAS_LIB_NAME} v${ULTRACANVAS_VERSION}")
message(STATUS "========================================")

# ===== OPTIONS =====
if (BUILD_DEMO_APP)
else()
    option(ULTRACANVAS_BUILD_SHARED "Build shared library instead of static" OFF)
endif ()
option(ULTRACANVAS_PLUGIN_CDR "Build CorelDRAW CDR plugin" ON)
option(ULTRACANVAS_PLUGIN_VECTOR, "Build Vector plugin" OFF)
option(ULTRACANVAS_PLUGIN_XAR "Build XARA plugin" ON)
option(ULTRACANVAS_PLUGIN_SVG "Build SVG plugin" OFF)
option(ULTRACANVAS_PLUGIN_PDF "Build PDF plugin" ON)
option(ULTRACANVAS_PLUGIN_OCR "Build OCR plugin (requires tesseract + leptonica)" OFF)
option(ULTRACANVAS_PLUGIN_VECTORIZER "Build Vectorizer plugin (raster→SVG via VTracer; requires cargo)" OFF)
option(ULTRACANVAS_PLUGIN_LATEX "Build LaTeX math plugin (embedded MicroTeX)" ON)
option(ULTRACANVAS_ENABLE_AUDIO "Enable audio playback & recording (requires backend lib)" ON)
option(ULTRACANVAS_ENABLE_VIDEO "Enable video playback & recording (requires platform media framework)" ON)
option(ULTRACANVAS_ENABLE_GL "Enable OpenGL 3D surface support" ON)

# OpenGL surface support (Linux, macOS and Windows)
# On Windows the WGL backend additionally requires GLEW to load modern GL
# entry points; if GLEW is not found below, GL support is disabled there.
#if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE OR WIN32)
#    option(ULTRACANVAS_ENABLE_GL "Enable OpenGL 3D surface support" ON)
#else()
#    set(ULTRACANVAS_ENABLE_GL OFF CACHE BOOL "Enable OpenGL 3D surface support" FORCE)
#endif()

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


# The core library links fmt::fmt, so it must resolve the dependency itself
# rather than relying on the demo app's FetchContent. Prefer an installed fmt
# (works offline); fall back to fetching the pinned revision.
if(NOT TARGET fmt::fmt)
    find_package(fmt CONFIG QUIET)
    if(NOT fmt_FOUND)
        include(FetchContent)
        FetchContent_Declare(
                fmt
                GIT_REPOSITORY https://github.com/fmtlib/fmt
                GIT_TAG        123913715afeb8a437e6388b4473fcc4753e1c9a) # 11.1.4
        FetchContent_MakeAvailable(fmt)
    endif()
endif()

message(STATUS "Target Platform: ${ULTRACANVAS_PLATFORM}")

# ===== FIND CORE DEPENDENCIES =====
find_package(PkgConfig REQUIRED)

# Cairo - 2D Graphics
pkg_check_modules(CAIRO REQUIRED cairo)

# Pango - Text Layout
pkg_check_modules(PANGO REQUIRED pango pangocairo)

# FontConfig - bundled-font registration (Linux always; Windows when Pango is built with the FontConfig backend)
if(NOT APPLE)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()

pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(TINYXML2 REQUIRED tinyxml2)

# libvips - Image processing
pkg_check_modules(LIBVIPS REQUIRED vips vips-cpp)

# zbar - QR/barcode decoder (optional)
pkg_check_modules(ZBAR zbar)
if(ZBAR_FOUND)
    message(STATUS "  zbar: ${ZBAR_VERSION} (QR decoder enabled)")
else()
    message(STATUS "  zbar: NOT FOUND (QR decoder disabled)")
endif()

# FreeType - Font Rendering
# On Windows with vcpkg, use pkg-config since FindFreetype may not locate vcpkg's install
if(WIN32 AND PKG_CONFIG_FOUND)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
else()
    find_package(Freetype REQUIRED)
    set(FREETYPE_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIRS})
    set(FREETYPE_LIBRARIES ${FREETYPE_LIBRARIES})
endif()

# Platform-specific dependencies
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    pkg_check_modules(X11 REQUIRED x11 xcursor)
    find_package(OpenGL REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0 gtk+-unix-print-3.0)

    # OpenGL/EGL support for GL surfaces
    if(ULTRACANVAS_ENABLE_GL)
        pkg_check_modules(EGL egl)
        if(EGL_FOUND)
            set(ULTRACANVAS_HAS_EGL TRUE)
            message(STATUS "  EGL: ${EGL_VERSION} (primary GL context backend)")
        else()
            message(STATUS "  EGL: NOT FOUND (will use GLX fallback)")
        endif()
        # GLX is always available with OpenGL on Linux
        set(ULTRACANVAS_HAS_GLX TRUE)
    endif()
elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
    # Windows SDK provides most dependencies
    # WGL needs GLEW to load modern (GL 3.x/4.x) entry points, since
    # opengl32.dll only exports OpenGL 1.1.
    if(ULTRACANVAS_ENABLE_GL)
        pkg_check_modules(GLEW glew)
        if(GLEW_FOUND)
            set(ULTRACANVAS_HAS_WGL TRUE)
            message(STATUS "  GLEW: ${GLEW_VERSION} (WGL GL function loader)")
        else()
            message(WARNING "  GLEW NOT FOUND - disabling OpenGL surface support on Windows "
                            "(install mingw-w64-x86_64-glew)")
            # Force the cache so the parent scope (demo GL sources) sees it too.
            set(ULTRACANVAS_ENABLE_GL OFF CACHE BOOL "Enable OpenGL 3D surface support" FORCE)
        endif()
    endif()
elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZ_FRAMEWORK QuartzCore REQUIRED)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    find_library(CORETEXT_FRAMEWORK CoreText REQUIRED)
    if(ULTRACANVAS_ENABLE_GL)
        set(ULTRACANVAS_HAS_CGL TRUE)
    endif()
endif()

# ===== DEFINE INCLUDE DIRECTORY =====
set(ULTRACANVAS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(ULTRACANVAS_INCLUDE_DIR ${ULTRACANVAS_INCLUDE_DIR} PARENT_SCOPE)
set(ULTRACANVAS_PLATFORM ${ULTRACANVAS_PLATFORM} PARENT_SCOPE)

# ===== COLLECT SOURCE FILES =====

# Core sources
set(ULTRACANVAS_CORE_SOURCES
        # Platform-specific sources
        #${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasString.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasCommonTypes.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasEvent.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasConfig.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasUtils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasUtilsUtf8.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasGroupBox.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSplitPane.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasDirtyRectManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTextArea.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTextArea_Markdown.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTextArea_Hex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasDropdown.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasDatePicker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAutoComplete.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasImageElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasImageViewer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasMediaViewer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTreeView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasListModel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasListView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasListDelegate.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasListSelection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTabbedContainer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasButton.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasBreadcrumb.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasLabel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSyntaxTokenizer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasLabeledToggleBase.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasCheckbox.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasRadio.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSwitch.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasGraphicsPluginSystem.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSegmentedControl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasPasswordStrengthMeter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasPasswordRuleLegend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasToolbar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasScrollbar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSlider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasColorPicker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSplashScreen.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSlideshow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAlbum.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasModalDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasFileLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasNewDocumentDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/Dimension.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/Layout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/LayoutUtils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/Element.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/AbsoluteLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/FlexLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/CSSLayout/GridLayout.cpp

        # HTMLReader: HTML/XHTML parser + CSS subset + DOM→element builder
        # on top of the CSSLayout engine (used by the eBook subsystem).
        ${CMAKE_CURRENT_SOURCE_DIR}/core/HTMLReader/HTMLDocument.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/HTMLReader/HTMLParser.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/HTMLReader/CSSStyleSheet.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/HTMLReader/HTMLStyleResolver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/HTMLReader/HTMLElementBuilder.cpp

        # eBook foundation: chapter-oriented engine interface + ZIP/DEFLATE
        # container access (format engines register themselves here).
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasEBookTypes.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/eBook/IEBookEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/eBook/EBookArchive.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/eBook/EPUBEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/eBook/FB2Engine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/eBook/MOBIEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/eBook/TXTEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasEBookViewer.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAudio.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAudioDevices.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAudioPlayer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAudioRecorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAudioPlayerElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAudioRecorderElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasWaveformElement.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoDevices.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoPlayer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoThumbnail.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoRecorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoPlayerElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoRecorderElement.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartElementBase.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasSpecificChartElements.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartDataStructures.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasFinancialChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasDivergingBarChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasWaterfallChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasPopulationChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasJitterPlotElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasPieChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasRadarChartElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasColormap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasHeatmapChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasCalendarHeatmap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasHexbinChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasSTFT.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasSpectrogram.cpp
        # Vendored KissFFT (BSD-3-Clause) - real-input FFT for the spectrogram
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/FFT/kiss_fft.c
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/FFT/kiss_fftr.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasArcDiagram.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasAdjacencyDiagram.cpp

        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/SVG/UltraCanvasSVGPlugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasSankey.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasVennDiagram.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasDendrogram.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasDendrogramLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasBlockDiagram.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasFlowChart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasNodeDiagram.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasGourceTree.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasTreeMapElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasFlowChartPalette.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasGaugeDiagramElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Diagrams/UltraCanvasCompositorDiagram.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/UltraCanvasBarcodeElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/UltraCanvasBarcodeEncoders.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/CodabarEncoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/Code128Encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/Code39Encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/Code93Encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/EANUPCEncoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/ITFEncoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/MSIEncoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/PharmacodeEncoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Barcode/Encoders/Standard25Encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/QRCode/UltraCanvasQRCode.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/QRCode/third_party/qrcodegen.cpp

        # STL 3D model loader/saver (self-contained, no external deps).
        # The GL-backed viewer in UltraCanvasSTLElement.cpp compiles to a 2D
        # fallback automatically when ULTRACANVAS_ENABLE_GL is off.
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Models/STL/UltraCanvasSTLLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Models/STL/UltraCanvasSTLElement.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Models/STL/UltraCanvasSTLPlugin.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Text/UltraCanvasMarkdown.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartElement.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartCoreRendering.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartBasicTypes.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasChartRenderer.cpp
        #        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Charts/UltraCanvasFinancialCharts.cpp
        # Spreadsheet component (Cell + Types are header-only)
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheet.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheetComponent.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheetSheet.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheetFormula.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheetFileIO.cpp
        # CSV/TSV "Text Import" options dialog (depends on the spreadsheet element)
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/UltraCanvasCSVImportDialog.cpp
        # CSV/TSV "Text Export" options dialog (depends on the spreadsheet element)
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/UltraCanvasCSVExportDialog.cpp
        # Vendored miniz (ZIP support for ODS/XLSX File I/O)
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c

        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheetFileIO.cpp
        # Add Windows/Mac sources as needed
        #        ${CMAKE_CURRENT_SOURCE_DIR}/OS/MSWindows/UltraCanvasWindowsDoubleBuffer.cpp
)

# libvips-dependent sources
if(LIBVIPS_FOUND)
    list(APPEND ULTRACANVAS_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/PixelFX/PixelFX.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/PixelFX/PixelFXImageInfo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/UltraCanvasImageExportDialog.cpp
    )
endif()

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
    # The CGL context manager is added conditionally by the OpenGL section below
    # (guarded by ULTRACANVAS_HAS_CGL); remove it here to avoid a duplicate source.
    list(REMOVE_ITEM ULTRACANVAS_OBJCPP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/GLContextManagerCGL_MacOS.mm
    )
    list(APPEND ULTRACANVAS_PLATFORM_SOURCES ${ULTRACANVAS_OBJCPP_SOURCES})

    set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/*.mm
            PROPERTIES
            LANGUAGE OBJCXX
            COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
    )
endif()

set(ULTRACANVAS_LIBSPECIFIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/ImageCairo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/RenderContextCairo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/UCTextLayout.cpp
        # Standalone QOI codec — dependency free, always compiled so qoi_encode/
        # qoi_decode/qoi_read/qoi_write are available without libvips.
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/qoi.cpp
)
if(LIBVIPS_FOUND)
    # vips foreign loader for QOI — consumes the codec symbols above.
    list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/VipsQOILoader.cpp
    )
endif()

# Audio sources (always-compiled API surface; backend is a stub unless
# ULTRACANVAS_ENABLE_AUDIO is ON, in which case the miniaudio backend replaces
# the null one).
list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Audio/AudioBackendNull.cpp
)
if(ULTRACANVAS_ENABLE_AUDIO)
    # Fetch miniaudio (single-header, MIT-0 / public-domain, cross-platform).
    # Pinned to a release tag for reproducibility.
#    FetchContent_Declare(
#        miniaudio
#        GIT_REPOSITORY https://github.com/mackron/miniaudio
#        GIT_TAG        0.11.21
#        GIT_SHALLOW    TRUE
#    )
#    FetchContent_MakeAvailable(miniaudio)

    list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Audio/AudioBackendMiniaudio.cpp
    )
    message(STATUS "Audio backend: miniaudio (${miniaudio_SOURCE_DIR})")
endif()

# Video sources (always-compiled API surface; backend is a stub unless
# ULTRACANVAS_ENABLE_VIDEO is ON and a platform media framework is found, in
# which case the platform-native backend replaces the null one).
#   Linux  : GStreamer (which dispatches to V4L2 / VA-API under the hood)
#   macOS  : AVFoundation   (TODO)
#   Windows: Media Foundation (TODO)
list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Video/VideoBackendNull.cpp
)
set(ULTRACANVAS_VIDEO_BACKEND_FOUND OFF)
if(ULTRACANVAS_ENABLE_VIDEO)
    message(STATUS "Video enabled in ${ULTRACANVAS_PLATFORM}")
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(GSTREAMER
                gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-pbutils-1.0)
        endif()
        if(GSTREAMER_FOUND)
            list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Video/VideoBackendGStreamer.cpp
            )
            set(ULTRACANVAS_VIDEO_BACKEND_FOUND ON)
            message(STATUS "Video backend: GStreamer (${GSTREAMER_VERSION})")
        else()
            message(STATUS "Video backend: null (GStreamer dev packages not found; "
                           "install gstreamer-1.0 + app/video/pbutils plugins for playback & capture)")
        endif()
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Video/VideoBackendMediaFoundation.cpp
        )
        set(ULTRACANVAS_VIDEO_BACKEND_FOUND ON)
        message(STATUS "Video backend: Media Foundation")
    elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
        list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Video/VideoBackendAVFoundation.mm
        )
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Video/VideoBackendAVFoundation.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        set(ULTRACANVAS_VIDEO_BACKEND_FOUND ON)
        message(STATUS "Video backend: AVFoundation")
    endif()
endif()

# OpenGL surface sources
if(ULTRACANVAS_ENABLE_GL)
    list(APPEND ULTRACANVAS_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasGLSurface.cpp
    )
    list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/GL/GLContextManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/GL/GLFramebuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/GL/ICompositeStrategy.cpp
    )

    # Platform-specific GL context managers
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        if(ULTRACANVAS_HAS_EGL)
            list(APPEND ULTRACANVAS_PLATFORM_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/Linux/GLContextManagerEGL_Linux.cpp
            )
        endif()
        if(ULTRACANVAS_HAS_GLX)
            list(APPEND ULTRACANVAS_PLATFORM_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/Linux/GLContextManagerGLX_Linux.cpp
            )
        endif()
    elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
        if(ULTRACANVAS_HAS_CGL)
            list(APPEND ULTRACANVAS_PLATFORM_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/GLContextManagerCGL_MacOS.mm
            )
        endif()
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        if(ULTRACANVAS_HAS_WGL)
            list(APPEND ULTRACANVAS_PLATFORM_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/MSWindows/GLContextManagerWGL_MSWindows.cpp
            )
        endif()
    endif()
endif()

# Combine all core sources
set(ULTRACANVAS_ALL_SOURCES
    ${ULTRACANVAS_CORE_SOURCES}
    ${ULTRACANVAS_PLATFORM_SOURCES}
    ${ULTRACANVAS_LIBSPECIFIC_SOURCES}
        libspecific/Audio/miniaudio.h
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
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
        ${FREETYPE_INCLUDE_DIRS}
        ${HARFBUZZ_INCLUDE_DIRS}
)
# Public headers reference cairo, glib, tinyxml2 — consumers need these include paths.
# Wrap in BUILD_INTERFACE to avoid CMake errors when paths are inside the source tree (vcpkg on CI).
foreach(_inc_dir ${CAIRO_INCLUDE_DIRS} ${PANGO_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${TINYXML2_INCLUDE_DIRS})
    target_include_directories(${ULTRACANVAS_LIB_NAME} PUBLIC $<BUILD_INTERFACE:${_inc_dir}>)
endforeach()
if(NOT APPLE AND FONTCONFIG_FOUND)
    foreach(_inc_dir ${FONTCONFIG_INCLUDE_DIRS})
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE ${_inc_dir})
    endforeach()
endif()
if(LIBVIPS_FOUND)
    foreach(_inc_dir ${LIBVIPS_INCLUDE_DIRS})
        target_include_directories(${ULTRACANVAS_LIB_NAME} PUBLIC $<BUILD_INTERFACE:${_inc_dir}>)
    endforeach()
endif()

if(ZBAR_FOUND)
    foreach(_inc_dir ${ZBAR_INCLUDE_DIRS})
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE ${_inc_dir})
    endforeach()
endif()

# Platform-specific includes
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
        ${X11_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIRS}
        ${GTK3_INCLUDE_DIRS}
    )
    if(ULTRACANVAS_ENABLE_GL AND EGL_FOUND)
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
            ${EGL_INCLUDE_DIRS}
        )
    endif()
elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
    if(ULTRACANVAS_ENABLE_GL AND GLEW_FOUND)
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
            ${GLEW_INCLUDE_DIRS}
        )
        target_link_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
            ${GLEW_LIBRARY_DIRS}
        )
    endif()
endif()

# ===== LINK LIBRARIES =====
target_link_libraries(${ULTRACANVAS_LIB_NAME}
    PUBLIC
        ${CAIRO_LIBRARIES}
        ${PANGO_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${LIBVIPS_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${TINYXML2_LIBRARIES}
)

# Platform-specific libraries
if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${X11_LIBRARIES}
            ${GTK3_LIBRARIES}
            ${FONTCONFIG_LIBRARIES}
            #        ${OPENGL_LIBRARIES}
            pthread
            dl
    )
    # Link OpenGL/EGL for GL surfaces
    if(ULTRACANVAS_ENABLE_GL)
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${OPENGL_LIBRARIES}
        )
        if(EGL_FOUND)
            target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
                ${EGL_LIBRARIES}
            )
        endif()
    endif()
elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            gdi32
            user32
            ole32
            oleaut32
            uuid
            shell32
            comdlg32
            comctl32
            imm32
            dwmapi
            ${FONTCONFIG_LIBRARIES}
    )
    if(ULTRACANVAS_ENABLE_GL)
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${GLEW_LIBRARIES}
            opengl32
        )
    endif()
elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${COCOA_FRAMEWORK}
            ${QUARTZ_FRAMEWORK}
            ${CORETEXT_FRAMEWORK}
            #        ${OPENGL_FRAMEWORK}
    )
    if(ULTRACANVAS_ENABLE_GL)
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${OPENGL_FRAMEWORK}
        )
    endif()
endif()

# ===== LINK LIBRARIES =====
target_link_libraries(${ULTRACANVAS_LIB_NAME}
    PUBLIC
        ${CAIRO_LIBRARIES}
        ${PANGO_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${LIBVIPS_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${TINYXML2_LIBRARIES}
        ${ZBAR_LIBRARIES}
        fmt::fmt
    # PRIVATE
    #     grapheme
)

# ===== COMPILER SETTINGS =====
target_compile_features(${ULTRACANVAS_LIB_NAME} PUBLIC cxx_std_20)

target_compile_definitions(${ULTRACANVAS_LIB_NAME}
    PUBLIC
        ULTRACANVAS_VERSION="${ULTRACANVAS_VERSION}"
        ULTRACANVAS_PLATFORM_${ULTRACANVAS_PLATFORM}=1
        $<$<BOOL:${LIBVIPS_FOUND}>:HAS_LIBVIPS=1>
        $<$<BOOL:${ZBAR_FOUND}>:ULTRACANVAS_QRCODE_HAS_DECODER=1>
        $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>
        $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>
        $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<BOOL:${WIN32}>:UNICODE>
        $<$<BOOL:${WIN32}>:_UNICODE>
    PRIVATE
        $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# GL surface compile definitions
if(ULTRACANVAS_ENABLE_GL)
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_ENABLE_GL=1)
    if(ULTRACANVAS_HAS_EGL)
        target_compile_definitions(${ULTRACANVAS_LIB_NAME} PRIVATE ULTRACANVAS_HAS_EGL=1)
    endif()
    if(ULTRACANVAS_HAS_GLX)
        target_compile_definitions(${ULTRACANVAS_LIB_NAME} PRIVATE ULTRACANVAS_HAS_GLX=1)
    endif()
    if(ULTRACANVAS_HAS_CGL)
        target_compile_definitions(${ULTRACANVAS_LIB_NAME} PRIVATE ULTRACANVAS_HAS_CGL=1)
        # macOS deprecated OpenGL in 10.14; silence the per-call warnings so the
        # GL surface code (which intentionally still uses it) builds cleanly.
        target_compile_definitions(${ULTRACANVAS_LIB_NAME} PRIVATE GL_SILENCE_DEPRECATION=1)
    endif()
    if(ULTRACANVAS_HAS_WGL)
        target_compile_definitions(${ULTRACANVAS_LIB_NAME} PRIVATE ULTRACANVAS_HAS_WGL=1)
    endif()
endif()

# Audio compile definitions + include path + libs
if(ULTRACANVAS_ENABLE_AUDIO)
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_ENABLE_AUDIO=1)
    target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
        $<BUILD_INTERFACE:${miniaudio_SOURCE_DIR}>
    )
    # miniaudio links dl + pthread on Linux, and uses AudioToolbox/CoreFoundation
    # on macOS. On Windows it's header-only and links the SDK that's already
    # pulled in via gdi32/ole32/etc.
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC dl pthread m)
    elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
        find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
        find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${COREAUDIO_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
        )
    endif()
endif()

# Video compile definitions + include path + libs. The API surface always
# compiles; ULTRACANVAS_ENABLE_VIDEO=1 is only defined when a real platform
# backend was found, so the null backend's GetVideoBackend() is used otherwise.
if(ULTRACANVAS_ENABLE_VIDEO AND ULTRACANVAS_VIDEO_BACKEND_FOUND)
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_ENABLE_VIDEO=1)
    if(GSTREAMER_FOUND)
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE ${GSTREAMER_INCLUDE_DIRS})
        target_link_directories(${ULTRACANVAS_LIB_NAME} PUBLIC ${GSTREAMER_LIBRARY_DIRS})
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC ${GSTREAMER_LIBRARIES})
        target_compile_options(${ULTRACANVAS_LIB_NAME} PRIVATE ${GSTREAMER_CFLAGS_OTHER})
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        # Media Foundation playback + capture + encode
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            mf mfplat mfreadwrite mfuuid ole32 shlwapi)
    elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
        find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
        find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
            ${AVFOUNDATION_FRAMEWORK} ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
    endif()
endif()

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${ULTRACANVAS_LIB_NAME} PRIVATE
        -Werror=return-type
#        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
endif()

# ===== BUILD PLUGINS AS SEPARATE LIBRARIES =====
set(ULTRACANVAS_PLUGIN_TARGETS "")
set(ULTRACANVAS_PLUGIN_INCLUDE_DIRS "")

# CDR Plugin
if(ULTRACANVAS_PLUGIN_CDR)
    if(NOT WIN32)
        pkg_check_modules(LIBCDR libcdr-0.1)
        pkg_check_modules(LIBREVENGE librevenge-0.0)
    else()
        # On Windows, these would need to be found differently
        # For now, mark as not found
        set(LIBCDR_FOUND FALSE)
        set(LIBREVENGE_FOUND FALSE)
    endif()

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

# PDF Plugin (MuPDF-backed)
# MuPDF ships as a static library on most distros and has no .pc file, so
# probe via find_path / find_library. On Windows the vcpkg port exposes the
# same names.
if(ULTRACANVAS_PLUGIN_PDF)
    find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h)
    find_library(MUPDF_LIBRARY      NAMES mupdf libmupdf)
    find_library(MUPDF_THIRD_LIBRARY NAMES mupdf-third libmupdf-third)
    # Companion libs MuPDF statically depends on. Most distros bundle them
    # into libmupdf-third, but some (Ubuntu 24.04+) require explicit linkage.
    find_library(JBIG2DEC_LIBRARY    NAMES jbig2dec)
    find_library(OPENJP2_LIBRARY     NAMES openjp2)
    find_library(GUMBO_LIBRARY       NAMES gumbo)
    find_library(MUJS_LIBRARY        NAMES mujs)
    find_library(HARFBUZZ_LIB        NAMES harfbuzz)
    # MuPDF on MSYS2/MinGW is built against the system FreeType/JPEG/zlib
    # (USE_SYSTEM_LIBS), so these must be linked explicitly AFTER libmupdf.a.
    # Full paths from find_library (not "-lfreetype") avoid CMake de-duplicating
    # them back to their earlier, pre-mupdf position on the link line.
    find_library(FREETYPE_LIB NAMES freetype)
    find_library(JPEG_LIB     NAMES jpeg libjpeg)
    find_library(ZLIB_LIB     NAMES z zlib)

    # MUPDF_THIRD_LIBRARY is intentionally NOT required here: it is absent for
    # DLL/import-lib MuPDF builds (MSYS2/MinGW), where the third-party deps live
    # inside the DLL. It is still linked below when present (static Linux builds).
    if(MUPDF_INCLUDE_DIR AND MUPDF_LIBRARY)
        set(MUPDF_FOUND TRUE)
    else()
        set(MUPDF_FOUND FALSE)
    endif()

    list(APPEND ULTRACANVAS_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/UltraCanvasPDF_MuPDF.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/UltraCanvasPDFView.cpp
    )
    # Rebuild the library source list since we just appended.
    target_sources(${ULTRACANVAS_LIB_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/UltraCanvasPDF_MuPDF.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/UltraCanvasPDFView.cpp
    )
    target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE
        ${MUPDF_INCLUDE_DIR}
    )
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC
        ULTRACANVAS_PLUGIN_PDF=1
        ULTRACANVAS_PDF_MUPDF=1
    )
    # Linking the static archives in the right order matters: dependents
    # before dependencies.
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC
        ${MUPDF_LIBRARY}
    )
    # libmupdf-third only exists for static MuPDF builds (Linux). A DLL/import-lib
    # build (MSYS2/MinGW) folds the third-party deps into the DLL, so it is absent
    # there — link it only when present.
    if(MUPDF_THIRD_LIBRARY)
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC ${MUPDF_THIRD_LIBRARY})
    endif()
    foreach(_extra_lib
            MUJS_LIBRARY GUMBO_LIBRARY JBIG2DEC_LIBRARY OPENJP2_LIBRARY
            HARFBUZZ_LIB FREETYPE_LIB JPEG_LIB ZLIB_LIB)
        if(${_extra_lib})
            target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC ${${_extra_lib}})
        endif()
    endforeach()
    message(STATUS "  [✓] PDF Plugin - ENABLED (MuPDF at ${MUPDF_LIBRARY})")
else()
    message(STATUS "  [−] PDF Plugin - SKIPPED (disabled by option)")
endif()

# OCR Plugin
if(ULTRACANVAS_PLUGIN_OCR)
    pkg_check_modules(TESSERACT tesseract)
    pkg_check_modules(LEPTONICA lept)
    if(TESSERACT_FOUND AND LEPTONICA_FOUND)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/OCR/CMakeLists.txt)
            add_subdirectory(Plugins/Documents/OCR)
            if(ULTRACANVAS_OCR_PLUGIN_FOUND)
                list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_OCR_PLUGIN_TARGET})
                list(APPEND ULTRACANVAS_PLUGIN_INCLUDE_DIRS ${ULTRACANVAS_OCR_PLUGIN_INCLUDE_DIR})
                message(STATUS "  [✓] OCR Plugin - ENABLED")
            endif()
        else()
            message(STATUS "  [−] OCR Plugin - NOT FOUND (CMakeLists.txt missing)")
        endif()
    else()
        message(STATUS "  [✗] OCR Plugin - DISABLED (missing tesseract/leptonica)")
    endif()
else()
    message(STATUS "  [−] OCR Plugin - SKIPPED (disabled by option)")
endif()

# Vectorizer Plugin (raster → SVG via VTracer through a Rust shim)
if(ULTRACANVAS_PLUGIN_VECTORIZER)
    find_program(CARGO_EXECUTABLE cargo)
    if(CARGO_EXECUTABLE)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Vectorizer/CMakeLists.txt)
            add_subdirectory(Plugins/Vectorizer)
            if(ULTRACANVAS_VECTORIZER_PLUGIN_FOUND)
                list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_VECTORIZER_PLUGIN_TARGET})
                list(APPEND ULTRACANVAS_PLUGIN_INCLUDE_DIRS ${ULTRACANVAS_VECTORIZER_PLUGIN_INCLUDE_DIR})
                message(STATUS "  [✓] Vectorizer Plugin - ENABLED (cargo: ${CARGO_EXECUTABLE})")
            endif()
        else()
            message(STATUS "  [−] Vectorizer Plugin - NOT FOUND (CMakeLists.txt missing)")
        endif()
    else()
        message(STATUS "  [✗] Vectorizer Plugin - DISABLED (cargo not found; install via https://rustup.rs/)")
    endif()
else()
    message(STATUS "  [−] Vectorizer Plugin - SKIPPED (disabled by option)")
endif()

# LaTeX Plugin (embedded MicroTeX) - ON-DEMAND dynamic module
#
# LaTeX support is a load-on-demand module, in line with UltraCanvas' goal of
# being an OS module whose plugins/file-format support are only mapped in when
# actually used. Nothing here is linked into the core library except a small
# loader: the core dlopen()s libUltraCanvasLaTeX the first time CreateLaTeXView()
# is called.
#
#   * microtex_core  : the vendored MicroTeX engine (static, path-glyph mode,
#                      GLYPH_RENDER_TYPE=1) - linked PRIVATELY into the module.
#   * UltraCanvasLaTeX: the SHARED module = backend adapter + concrete view +
#                      C ABI entry points + microtex_core. Built into the
#                      "plugins" output dir; NOT linked by the core.
#   * loader         : core/UltraCanvasLaTeXModuleLoader.cpp compiled into the
#                      core lib (the only LaTeX code the core contains).
#
# The Latin Modern Math font + its .clm2 metrics ship in media/microtex.
if(ULTRACANVAS_PLUGIN_LATEX)
    set(MICROTEX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/microtex)

    # --- MicroTeX engine (static, private to the module) ---
    file(GLOB_RECURSE MICROTEX_CORE_SOURCES ${MICROTEX_DIR}/lib/*.cpp)
    # Exclude the C-wrapper API (not used) and the auto-font-find feature - the
    # only std::filesystem consumer. The engine is initialised with an explicit
    # FontSrcFile and driven through the C++ API.
    list(FILTER MICROTEX_CORE_SOURCES EXCLUDE REGEX "/lib/wrapper/")
    list(FILTER MICROTEX_CORE_SOURCES EXCLUDE REGEX "/otf/fontsense\\.cpp$")

    add_library(microtex_core STATIC ${MICROTEX_CORE_SOURCES})
    target_include_directories(microtex_core PUBLIC ${MICROTEX_DIR}/lib)
    target_compile_features(microtex_core PUBLIC cxx_std_17)
    target_compile_definitions(microtex_core
        PRIVATE MICROTEX_LIBRARY
        PUBLIC  GLYPH_RENDER_TYPE=1)
    set_target_properties(microtex_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT MSVC)
        target_compile_options(microtex_core PRIVATE -fexceptions)
    endif()

    # --- The on-demand shared module ---
    add_library(UltraCanvasLaTeX MODULE
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/LaTeX/UltraCanvasLaTeXBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/LaTeX/UltraCanvasLaTeXViewImpl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/LaTeX/UltraCanvasLaTeXModule.cpp
    )
    set_target_properties(UltraCanvasLaTeX PROPERTIES
        PREFIX "lib"                     # libUltraCanvasLaTeX.so on all UNIX
        CXX_VISIBILITY_PRESET hidden
        # Co-locate with the core lib so the loader's "<exe>/../lib" and bare-name
        # searches find it during dev/test; installers can relocate it.
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    target_include_directories(UltraCanvasLaTeX PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/CSSLayout
        ${MICROTEX_DIR}/lib
        ${CAIRO_INCLUDE_DIRS} ${PANGO_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS}
        ${HARFBUZZ_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS}
    )
    target_compile_definitions(UltraCanvasLaTeX PRIVATE ULTRACANVAS_PLUGIN_LATEX=1)
    target_link_libraries(UltraCanvasLaTeX PRIVATE microtex_core)
    # The module resolves UltraCanvas core symbols (IRenderContext, UIElement,
    # GetExecutableDir, ...) at load time. When the core is a shared library we
    # record a link dependency on it; for a static core the host executable must
    # export its dynamic symbols (ENABLE_EXPORTS / -rdynamic) so RTLD_GLOBAL can
    # satisfy them - undefined references in the module are expected and allowed.
    if(ULTRACANVAS_BUILD_SHARED)
        target_link_libraries(UltraCanvasLaTeX PRIVATE ${ULTRACANVAS_LIB_NAME})
    elseif(APPLE)
        set_target_properties(UltraCanvasLaTeX PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup")
    endif()
    # NB: the module is dlopen()ed at runtime, never linked - so it is NOT added
    # to ULTRACANVAS_PLUGIN_TARGETS (which apps link against). It builds as part
    # of the default ("all") target.

    # --- Loader (the only LaTeX code linked into the core) ---
    target_sources(${ULTRACANVAS_LIB_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasLaTeXModuleLoader.cpp
    )
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_PLUGIN_LATEX=1)
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC ${CMAKE_DL_LIBS})

    message(STATUS "  [✓] LaTeX Plugin - ENABLED (on-demand module: libUltraCanvasLaTeX)")
else()
    message(STATUS "  [−] LaTeX Plugin - SKIPPED (disabled by option)")
endif()

# Add more plugins here...

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_LIBRARY ${ULTRACANVAS_LIB_NAME} PARENT_SCOPE)
set(ULTRACANVAS_LIBRARIES ${ULTRACANVAS_LIB_NAME} ${ULTRACANVAS_PLUGIN_TARGETS} PARENT_SCOPE)
set(LIBVIPS_FOUND ${LIBVIPS_FOUND} PARENT_SCOPE)
set(ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_PLUGIN_TARGETS} PARENT_SCOPE)
set(ULTRACANVAS_INCLUDE_DIRS 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ULTRACANVAS_PLUGIN_INCLUDE_DIRS}
    PARENT_SCOPE
)

# ===== INSTALLATION =====
# install(TARGETS ${ULTRACANVAS_LIB_NAME} grapheme
#     EXPORT UltraCanvasTargets
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib
#     RUNTIME DESTINATION bin
# )

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include/ultracanvas
    FILES_MATCHING PATTERN "*.h"
)

# Install bundled libgrapheme header
# install(FILES ${LIBGRAPHEME_DIR}/grapheme.h
#     DESTINATION include/ultracanvas/grapheme
# )

# Export CMake config for find_package support
# install(EXPORT UltraCanvasTargets
#     FILE UltraCanvasTargets.cmake
#     NAMESPACE UltraCanvas::
#     DESTINATION lib/cmake/UltraCanvas
# )

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
# message(STATUS "  libgrapheme: 3.0.0 (bundled, Unicode 17.0.0)")
message(STATUS "  Plugins: ${ULTRACANVAS_PLUGIN_TARGETS}")
if(ULTRACANVAS_ENABLE_GL)
    message(STATUS "  OpenGL Surface: ENABLED")
    if(ULTRACANVAS_HAS_EGL)
        message(STATUS "    - EGL backend: YES")
    endif()
    if(ULTRACANVAS_HAS_GLX)
        message(STATUS "    - GLX backend: YES")
    endif()
    if(ULTRACANVAS_HAS_CGL)
        message(STATUS "    - CGL backend: YES")
    endif()
    if(ULTRACANVAS_HAS_WGL)
        message(STATUS "    - WGL backend: YES")
    endif()
else()
    message(STATUS "  OpenGL Surface: DISABLED")
endif()
message(STATUS "========================================")
message(STATUS "")