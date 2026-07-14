# UltraCanvas/CMakeLists.txt
# UltraCanvas Core Library - Builds as separate static library with optional plugins
# Version: 1.4.5
# Last Modified: 2026-07-05
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# Enable the C language: third_party/miniz/miniz.c is C (the only C source in the
# project) and provides ZIP support for ODS/XLSX File I/O. Without this, a CXX-only
# project silently skips compiling .c files, causing undefined references to mz_zip_*.
enable_language(C)

# Enable Objective-C++ on Apple platforms: the macOS backend (GL context managers,
# AVFoundation video, UltraNet TLS/DNS support) ships .mm sources tagged
# LANGUAGE OBJCXX below. The root project() only declares CXX, so without this the
# generate step fails with "The language OBJCXX was requested ... but was not enabled".
if(APPLE)
    enable_language(OBJCXX)
endif()

# ===== LIBRARY DEFINITION =====
set(ULTRACANVAS_VERSION "0.3.7")
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
option(ULTRACANVAS_PLUGIN_OCR "Build OCR plugin (requires tesseract + leptonica)" ON)
option(ULTRACANVAS_PLUGIN_VECTORIZER "Build Vectorizer plugin (raster→SVG via VTracer; requires cargo)" OFF)
option(ULTRACANVAS_PLUGIN_LATEX "Build LaTeX math plugin (embedded MicroTeX)" ON)
option(ULTRACANVAS_ENABLE_AUDIO "Enable audio playback & recording (requires backend lib)" ON)
option(ULTRACANVAS_ENABLE_VIDEO "Enable video playback & recording (requires platform media framework)" ON)
option(ULTRACANVAS_ENABLE_GL "Enable OpenGL 3D surface support" ON)

# The LaTeX plugin is a runtime-loaded module that subclasses core UI classes.
# On Unix its undefined core symbols are resolved from the host executable at
# dlopen() time (ENABLE_EXPORTS + RTLD_GLOBAL). Windows PE has no equivalent, so
# the shared code must live in a DLL that both the apps and the plugin import.
if(WIN32 AND ULTRACANVAS_PLUGIN_LATEX AND NOT ULTRACANVAS_BUILD_SHARED)
    message(STATUS "Windows + LaTeX plugin: forcing shared core (ULTRACANVAS_BUILD_SHARED=ON)")
    set(ULTRACANVAS_BUILD_SHARED ON)
endif()

# UltraNet — networking module (HTTP/HTTPS, FTP, WebSocket, TCP/UDP, TLS, DNS).
# Stage 1 ships HTTP/HTTPS + URL utilities on libcurl. Built as a separate
# library target so apps that don't need networking pay no cost.
option(ULTRACANVAS_ENABLE_NET "Build the UltraNet networking library" ON)
option(ULTRACANVAS_PLUGIN_SMTP   "Build the UltraNet SMTP plug-in"   ON)
option(ULTRACANVAS_PLUGIN_IMAP   "Build the UltraNet IMAP plug-in"   ON)
option(ULTRACANVAS_PLUGIN_POP3   "Build the UltraNet POP3 plug-in"   ON)
option(ULTRACANVAS_PLUGIN_WEBDAV "Build the UltraNet WebDAV plug-in" ON)
option(ULTRACANVAS_PLUGIN_LDAP   "Build the UltraNet LDAP plug-in"   ON)
option(ULTRACANVAS_PLUGIN_TELNET "Build the UltraNet Telnet plug-in" ON)
option(ULTRACANVAS_PLUGIN_RTSP   "Build the UltraNet RTSP plug-in"   ON)
option(ULTRACANVAS_PLUGIN_MQTT   "Build the UltraNet MQTT plug-in (requires paho-mqtt-c)" ON)
option(ULTRACANVAS_PLUGIN_AMQP   "Build the UltraNet AMQP plug-in (requires rabbitmq-c)" ON)
option(ULTRACANVAS_PLUGIN_COAP   "Build the UltraNet CoAP plug-in (requires libcoap3)"   ON)
option(ULTRACANVAS_PLUGIN_SNMP   "Build the UltraNet SNMP plug-in (requires net-snmp)"   ON)
option(ULTRACANVAS_PLUGIN_SSH    "Build the UltraNet SSH plug-in (requires libssh)"    ON)
option(ULTRACANVAS_PLUGIN_MDNS   "Build the UltraNet mDNS plug-in (Avahi/Bonjour/Win32 DNS-SD)" ON)
option(ULTRACANVAS_PLUGIN_RTMP   "Build the UltraNet RTMP plug-in (libcurl-native)"    ON)
option(ULTRACANVAS_PLUGIN_GRPC   "Build the UltraNet gRPC plug-in (libcurl HTTP/2)"    ON)
option(ULTRACANVAS_PLUGIN_SIP    "Build the UltraNet SIP plug-in (UDP-native)"        ON)
option(ULTRACANVAS_PLUGIN_RTP    "Build the UltraNet RTP receiver plug-in (UDP-native)" ON)

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

# librsvg - parse-once SVG documents (optional). With it, SVG files are parsed
# a single time into a retained document that every rasterization (zoom level,
# HiDPI, layout size) renders from; without it SVG falls back to libvips'
# svgload, which re-reads and re-parses the XML for every requested size.
pkg_check_modules(LIBRSVG librsvg-2.0)
if(LIBRSVG_FOUND)
    message(STATUS "  librsvg: ${LIBRSVG_VERSION} (parse-once SVG documents enabled)")
else()
    message(STATUS "  librsvg: NOT FOUND (SVG re-parses per rasterization via libvips)")
endif()

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
    # xrandr: per-monitor DPI query for HiDPI scaling (XRRGetMonitors).
    pkg_check_modules(X11 REQUIRED x11 xcursor xrandr)
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
set(ULTRACANVAS_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
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
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasMenuRegistry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasMenuLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasMenuConfigWidget.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasImageAnimation.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpinner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasTimePicker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasPagination.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasRating.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasStepper.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasChip.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasBadge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasColorPicker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSplashScreen.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSlideshow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasAlbum.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasFilerWidget.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasModalDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasFileLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSupportedFormats.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVideoHoverPreview.cpp

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
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasSpreadsheetXlsxIO.cpp
        # CSV/TSV "Text Import" options dialog (depends on the spreadsheet element)
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/UltraCanvasCSVImportDialog.cpp
        # CSV/TSV "Text Export" options dialog (depends on the spreadsheet element)
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/UltraCanvasCSVExportDialog.cpp
        # Vendored miniz (ZIP support for ODS/XLSX File I/O)
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c

        # Reusable ZIP package layer (ODF/OPC containers)
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasZipPackage.cpp
        # Word-processing document module: UCRichDocument + ODT/DOCX readers
        # and writers (used by FileLoader::OpenTextDocument and Texter)
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/Word/UltraCanvasRichDocument.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/Word/UltraCanvasWordDocumentIO.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/Word/UltraCanvasOdtFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/Word/UltraCanvasDocxFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Documents/Word/UltraCanvasMathToLatex.cpp

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

# The OS-level UltraNet back-ends (UltraNetSupport / UltraNetTlsImpl /
# UltraNetDnsImpl) live under the platform dir but belong to the UltraNet static
# library (they are listed explicitly in ULTRANET_SOURCES). The platform glob
# above also matches them, which compiled them into BOTH libultracanvas and
# libultranet. That was silently tolerated only because a plain link never
# extracted the archive's copies; once UltraNet is absorbed whole-archive into a
# shared libultracanvas the two copies collide ("multiple definition of
# ultranet_tls_platform::...", "UltraNet_DetectSystemProxy", ...). Drop them from
# the core sources so they are compiled exactly once — into libultranet — which
# the core then links.
file(GLOB _ultracanvas_ultranet_platform_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/UltraNet*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/${ULTRACANVAS_PLATFORM_DIR}/UltraNet*.mm
)
if(_ultracanvas_ultranet_platform_srcs)
    list(REMOVE_ITEM ULTRACANVAS_PLATFORM_SOURCES ${_ultracanvas_ultranet_platform_srcs})
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
if(LIBRSVG_FOUND)
    # Parse-once SVG document cache (retained RsvgHandle).
    list(APPEND ULTRACANVAS_LIBSPECIFIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libspecific/Cairo/SvgDocumentCairo.cpp
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

# VirtualFS bridge (only when the sibling VirtualFS module is part of the build)
if(TARGET VirtualFS)
    list(APPEND ULTRACANVAS_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraCanvasVirtualFSBridge.cpp
    )
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

if(WIN32)
    # No __declspec(dllexport) in the sources: export everything (functions AND
    # data, incl. vtables/typeinfo the LaTeX plugin needs) and emit an import lib.
    set_target_properties(${ULTRACANVAS_LIB_NAME} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

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
if(LIBRSVG_FOUND)
    # Only libspecific/Cairo internals include librsvg headers.
    foreach(_inc_dir ${LIBRSVG_INCLUDE_DIRS})
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE ${_inc_dir})
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

# VirtualFS module (brings VIRTUALFS_HAS_* defines and headers for the bridge;
# ULTRACANVAS_HAS_VIRTUALFS gates FileLoader's transparent decompression)
if(TARGET VirtualFS)
    target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC VirtualFS::VirtualFS)
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_HAS_VIRTUALFS)
    # The bridge calls zlib directly, so it needs the headers/library itself
    # (VirtualFS's find results are scoped to its own directory)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_include_directories(${ULTRACANVAS_LIB_NAME} PRIVATE ${ZLIB_INCLUDE_DIRS})
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PRIVATE ${ZLIB_LIBRARIES})
    endif()
endif()

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
        ${LIBRSVG_LIBRARIES}
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
        $<$<BOOL:${LIBRSVG_FOUND}>:HAS_LIBRSVG=1>
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

# ===== BUILD ULTRANET LIBRARY (Stage 1: HTTP/HTTPS + URL on libcurl) =====
if(ULTRACANVAS_ENABLE_NET)
    find_package(Threads REQUIRED)

    # --- libcurl selection: system vs vendored (WebSockets) -------------------
    # UltraNet's WebSocket transport needs libcurl >= 7.86 built WITH WebSocket
    # support (curl_ws_*). Some machines ship a libcurl without it, or have a
    # locally built one that lacks the distro's versioned symbols (e.g.
    # CURL_OPENSSL_4) that OTHER already-installed libraries (libvips ->
    # libhdf5) reference — linking such a libcurl next to them fails with
    #   "undefined reference to curl_global_init@CURL_OPENSSL_4".
    # When the found libcurl can't satisfy both needs we fall back to building
    # the vendored copy in third_party/curl and (Linux) stamp it with the
    # distro's symbol version node, making it a drop-in superset for everything.
    # CI runners all ship a WebSocket-capable, correctly-versioned system
    # libcurl, so this fallback never activates there (asserted in build.yml).
    #
    # NOTE: the probe below deliberately uses find_library/find_path rather than
    # find_package(CURL) — find_package creates the imported CURL::libcurl
    # target, which would clash with the ALIAS of the same name that the
    # vendored curl's own CMake defines. find_package is called only in the
    # system branch, where no vendored target exists.
    set(_uc_use_vendored OFF)
    set(UC_CURL_HAS_WS FALSE)
    set(_uc_found_curl_versioned FALSE)
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        find_library(_uc_sys_curl_lib NAMES curl)
        find_path(_uc_sys_curl_inc NAMES curl/curl.h)
        if(_uc_sys_curl_lib AND _uc_sys_curl_inc)
            include(CheckSymbolExists)
            set(CMAKE_REQUIRED_INCLUDES  ${_uc_sys_curl_inc})
            set(CMAKE_REQUIRED_LIBRARIES ${_uc_sys_curl_lib})
            # Result goes to a dedicated cache var: check_symbol_exists SKIPS the
            # test when its result variable is already set, so it must not reuse
            # a name we pre-initialised (UC_CURL_HAS_WS).
            check_symbol_exists(curl_ws_send "curl/curl.h;curl/websockets.h" UC_SYS_CURL_HAS_WS)
            unset(CMAKE_REQUIRED_INCLUDES)
            unset(CMAKE_REQUIRED_LIBRARIES)
            if(UC_SYS_CURL_HAS_WS)
                set(UC_CURL_HAS_WS TRUE)
            endif()
            # Versioned symbols (CURL_*) are required to coexist with libhdf5/libvips.
            execute_process(COMMAND objdump -T "${_uc_sys_curl_lib}"
                            OUTPUT_VARIABLE _uc_curl_dump ERROR_QUIET)
            if(_uc_curl_dump MATCHES "CURL_[A-Z0-9_]+")
                set(_uc_found_curl_versioned TRUE)
            endif()
        endif()

        set(_uc_vendored_default OFF)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/curl/CMakeLists.txt"
           AND (NOT UC_CURL_HAS_WS OR NOT _uc_found_curl_versioned))
            set(_uc_vendored_default ON)
        endif()
        option(ULTRACANVAS_USE_VENDORED_CURL
            "Build libcurl from third_party/curl (WebSockets, distro-versioned)"
            ${_uc_vendored_default})
        if(ULTRACANVAS_USE_VENDORED_CURL)
            set(_uc_use_vendored ON)
        endif()
    endif()

    if(_uc_use_vendored)
        # Builds libcurl_shared (+ curl's own CURL::libcurl alias) and, on Linux,
        # stamps it with the distro's symbol version node.
        include(${CMAKE_CURRENT_LIST_DIR}/cmake/VendoredCurl.cmake)
        set(UC_CURL_LINK libcurl_shared)
        set(CURL_VERSION_STRING "8.21.0 (vendored)")
        message(STATUS "  [i] UltraNet libcurl: VENDORED third_party/curl "
                       "(system curl unusable: WS=${UC_CURL_HAS_WS} versioned=${_uc_found_curl_versioned})")
    else()
        find_package(CURL REQUIRED)
        set(UC_CURL_LINK CURL::libcurl)
        message(STATUS "  [i] UltraNet libcurl: SYSTEM ${CURL_VERSION_STRING} (WebSocket: OK)")
    endif()
    # OpenSSL is the TLS backend for UltraNet_TlsWrap on Linux. Windows and
    # macOS use Schannel / SecureTransport (no extra dependency).
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        find_package(OpenSSL REQUIRED)
    endif()

    # c-ares: optional async DNS backend. When present, replaces the
    # system getaddrinfo + libresolv path with a fully non-blocking
    # resolver, fixes UltraNet_DnsSetServers (which was a no-op without
    # c-ares), and unifies record-type handling across platforms.
    pkg_check_modules(CARES libcares)
    if(CARES_FOUND)
        message(STATUS "  [✓] c-ares ${CARES_VERSION} - ENABLED (async DNS)")
        set(ULTRANET_HAS_CARES 1)
    else()
        message(STATUS "  [−] c-ares - not found (using thread-per-call async DNS fallback)")
    endif()

    set(ULTRANET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetCore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetHttp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetHttpEasy.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetHttpAsync.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetCookies.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetDns.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetUrl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetMime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetWebSocket.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetFtp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetSocket.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetTls.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetPlugins.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetSse.cpp
    )
    if(ULTRANET_HAS_CARES)
        list(APPEND ULTRANET_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraNet/UltraNetDnsCares.cpp)
    endif()
    # Per-platform DNS impl supplies ultranet_dns_platform::Resolve. When
    # c-ares is enabled, UltraNetDnsCares.cpp supplies that same symbol —
    # we'd get a duplicate-definition link error if we kept both. Exclude
    # the per-platform DNS impl when c-ares wins.
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        list(APPEND ULTRANET_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/Linux/UltraNetSupport.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/Linux/UltraNetTlsImpl.cpp)
        if(NOT ULTRANET_HAS_CARES)
            list(APPEND ULTRANET_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/Linux/UltraNetDnsImpl.cpp)
        endif()
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        list(APPEND ULTRANET_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MSWindows/UltraNetSupport.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MSWindows/UltraNetTlsImpl.cpp)
        if(NOT ULTRANET_HAS_CARES)
            list(APPEND ULTRANET_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/MSWindows/UltraNetDnsImpl.cpp)
        endif()
    elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
        list(APPEND ULTRANET_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/UltraNetSupport.mm
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/UltraNetTlsImpl.mm)
        if(NOT ULTRANET_HAS_CARES)
            list(APPEND ULTRANET_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/UltraNetDnsImpl.mm)
        endif()
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/UltraNetSupport.mm
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/UltraNetTlsImpl.mm
            ${CMAKE_CURRENT_SOURCE_DIR}/OS/MacOS/UltraNetDnsImpl.mm
            PROPERTIES LANGUAGE OBJCXX COMPILE_FLAGS "-x objective-c++ -fobjc-arc")
    endif()

    add_library(UltraNet STATIC ${ULTRANET_SOURCES})
    set_target_properties(UltraNet PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "ultranet")
    target_compile_features(UltraNet PUBLIC cxx_std_20)
    target_include_directories(UltraNet
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/ultracanvas>)
    target_link_libraries(UltraNet PUBLIC ${UC_CURL_LINK} Threads::Threads)
    if(ULTRANET_HAS_CARES)
        target_compile_definitions(UltraNet PUBLIC ULTRANET_HAS_CARES=1)
        target_include_directories(UltraNet PRIVATE ${CARES_INCLUDE_DIRS})
        target_link_libraries(UltraNet PUBLIC ${CARES_LIBRARIES})
    endif()
    if(ULTRACANVAS_PLATFORM STREQUAL "Linux")
        target_link_libraries(UltraNet PUBLIC OpenSSL::SSL OpenSSL::Crypto resolv)
    elseif(ULTRACANVAS_PLATFORM STREQUAL "Windows")
        # winhttp: system-proxy detection; ws2_32: getaddrinfo/getnameinfo/recv/send;
        # secur32: Schannel (AcquireCredentialsHandle etc.); crypt32: cert parsing;
        # dnsapi: DnsQuery for non-A/AAAA DNS records.
        target_link_libraries(UltraNet PUBLIC
            winhttp ws2_32 secur32 crypt32 dnsapi)
    elseif(ULTRACANVAS_PLATFORM STREQUAL "MacOS")
        find_library(CFNETWORK_FRAMEWORK CFNetwork REQUIRED)
        find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        find_library(SYSTEM_CONFIG_FRAMEWORK SystemConfiguration REQUIRED)
        find_library(SECURITY_FRAMEWORK Security REQUIRED)
        target_link_libraries(UltraNet PUBLIC
            ${CFNETWORK_FRAMEWORK}
            ${CORE_FOUNDATION_FRAMEWORK}
            ${SYSTEM_CONFIG_FRAMEWORK}
            ${SECURITY_FRAMEWORK}
            resolv)
    endif()
    target_compile_definitions(UltraNet PUBLIC ULTRACANVAS_HAS_NET=1)

    # Let UltraCanvas itself link UltraNet so that components like FileLoader
    # can call UltraNet_HttpDownloadFile when ULTRACANVAS_ENABLE_NET is on.
    #
    # In a shared build, libultracanvas is meant to be the single host that
    # exports the whole UltraNet API: plug-ins (SMTP/IMAP/...) and apps import
    # every UltraNet_* symbol from it and never link the static archive, so there
    # is exactly one copy of UltraNet (and its global state) at runtime.
    #
    # A plain link only pulls the UltraNet objects that libultracanvas's own code
    # references into the DLL, so WINDOWS_EXPORT_ALL_SYMBOLS would re-export only
    # that arbitrary subset (e.g. UltraNet_Initialize but not UltraNet_MimeBuild /
    # UltraNet_ParseUrl) — leaving consumers with undefined references. Force the
    # ENTIRE archive in with --whole-archive so the complete API is present and
    # exported. PRIVATE keeps the static archive off consumers' link lines (they
    # import from the DLL); UltraNet's transitive deps/includes still propagate.
    # A static ultracanvas keeps PUBLIC so consumers link the archive themselves.
    if(ULTRACANVAS_BUILD_SHARED)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
            target_link_libraries(${ULTRACANVAS_LIB_NAME} PRIVATE
                "$<LINK_LIBRARY:WHOLE_ARCHIVE,UltraNet>")
        else()
            # Fallback for CMake < 3.24 (no $<LINK_LIBRARY:WHOLE_ARCHIVE>): force
            # the archive in by file, plus the target for its usage requirements.
            target_link_libraries(${ULTRACANVAS_LIB_NAME} PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:UltraNet> -Wl,--no-whole-archive
                UltraNet)
        endif()
    else()
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC UltraNet)
    endif()
    target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_HAS_NET=1)
    message(STATUS "  [✓] UltraNet - ENABLED (libcurl ${CURL_VERSION_STRING})")

    # Mail plug-ins (SMTP/IMAP/POP3) — load via dlopen/LoadLibrary on all
    # three platforms. Plug-in DSOs use the v2 host-vtable contract so
    # they don't depend on the host's import library at load time.
    if(ULTRACANVAS_PLUGIN_SMTP)
        add_subdirectory(Plugins/UltraNet/smtp)
        message(STATUS "  [✓] UltraNet SMTP plug-in - ENABLED")
    endif()
    if(ULTRACANVAS_PLUGIN_IMAP)
        add_subdirectory(Plugins/UltraNet/imap)
        message(STATUS "  [✓] UltraNet IMAP plug-in - ENABLED")
    endif()
    if(ULTRACANVAS_PLUGIN_POP3)
        add_subdirectory(Plugins/UltraNet/pop3)
        message(STATUS "  [✓] UltraNet POP3 plug-in - ENABLED")
    endif()

    # Quick-win plug-ins: all libcurl-native, single-method-ish, same
    # plug-in DSO shape as the mail trio. Cover four more spec
    # categories (Web Modern / Directory / Remote Access / Streaming).
    if(ULTRACANVAS_PLUGIN_WEBDAV)
        add_subdirectory(Plugins/UltraNet/webdav)
        message(STATUS "  [✓] UltraNet WebDAV plug-in - ENABLED")
    endif()
    if(ULTRACANVAS_PLUGIN_LDAP)
        add_subdirectory(Plugins/UltraNet/ldap)
        message(STATUS "  [✓] UltraNet LDAP plug-in - ENABLED")
    endif()
    if(ULTRACANVAS_PLUGIN_TELNET)
        add_subdirectory(Plugins/UltraNet/telnet)
        message(STATUS "  [✓] UltraNet Telnet plug-in - ENABLED")
    endif()
    if(ULTRACANVAS_PLUGIN_RTSP)
        add_subdirectory(Plugins/UltraNet/rtsp)
        message(STATUS "  [✓] UltraNet RTSP plug-in - ENABLED")
    endif()

    # MQTT — first plug-in NOT backed by libcurl. Needs Eclipse Paho's
    # libpaho-mqtt3as (async + SSL). The plug-in CMakeLists self-disables
    # when paho-mqtt-c isn't installed, so this is safe to leave ON.
    if(ULTRACANVAS_PLUGIN_MQTT)
        add_subdirectory(Plugins/UltraNet/mqtt)
        if(TARGET UltraNetMqttPlugin)
            message(STATUS "  [✓] UltraNet MQTT plug-in - ENABLED")
        else()
            message(STATUS "  [−] UltraNet MQTT plug-in - SKIPPED (paho-mqtt-c not found)")
        endif()
    endif()

    # Three more third-party-library plug-ins. Each self-disables when
    # its backend library isn't present, so leaving these ON is safe.
    if(ULTRACANVAS_PLUGIN_AMQP)
        add_subdirectory(Plugins/UltraNet/amqp)
        if(TARGET UltraNetAmqpPlugin)
            message(STATUS "  [✓] UltraNet AMQP plug-in - ENABLED")
        else()
            message(STATUS "  [−] UltraNet AMQP plug-in - SKIPPED (rabbitmq-c not found)")
        endif()
    endif()
    if(ULTRACANVAS_PLUGIN_COAP)
        add_subdirectory(Plugins/UltraNet/coap)
        if(TARGET UltraNetCoapPlugin)
            message(STATUS "  [✓] UltraNet CoAP plug-in - ENABLED")
        else()
            message(STATUS "  [−] UltraNet CoAP plug-in - SKIPPED (libcoap3 not found)")
        endif()
    endif()
    if(ULTRACANVAS_PLUGIN_SNMP)
        add_subdirectory(Plugins/UltraNet/snmp)
        if(TARGET UltraNetSnmpPlugin)
            message(STATUS "  [✓] UltraNet SNMP plug-in - ENABLED")
        else()
            message(STATUS "  [−] UltraNet SNMP plug-in - SKIPPED (net-snmp not found)")
        endif()
    endif()
    if(ULTRACANVAS_PLUGIN_SSH)
        add_subdirectory(Plugins/UltraNet/ssh)
        if(TARGET UltraNetSshPlugin)
            message(STATUS "  [✓] UltraNet SSH plug-in - ENABLED")
        else()
            message(STATUS "  [−] UltraNet SSH plug-in - SKIPPED (libssh not found)")
        endif()
    endif()
    if(ULTRACANVAS_PLUGIN_MDNS)
        add_subdirectory(Plugins/UltraNet/mdns)
        if(TARGET UltraNetMdnsPlugin)
            message(STATUS "  [✓] UltraNet mDNS plug-in - ENABLED")
        else()
            message(STATUS "  [−] UltraNet mDNS plug-in - SKIPPED (no platform backend)")
        endif()
    endif()
    if(ULTRACANVAS_PLUGIN_RTMP)
        add_subdirectory(Plugins/UltraNet/rtmp)
        message(STATUS "  [✓] UltraNet RTMP plug-in - ENABLED (runtime check for libcurl RTMP support)")
    endif()
    if(ULTRACANVAS_PLUGIN_GRPC)
        add_subdirectory(Plugins/UltraNet/grpc)
        message(STATUS "  [✓] UltraNet gRPC plug-in - ENABLED (libcurl HTTP/2 backend)")
    endif()
    if(ULTRACANVAS_PLUGIN_SIP)
        add_subdirectory(Plugins/UltraNet/sip)
        message(STATUS "  [✓] UltraNet SIP plug-in - ENABLED (UDP-native OPTIONS + MESSAGE)")
    endif()
    if(ULTRACANVAS_PLUGIN_RTP)
        add_subdirectory(Plugins/UltraNet/rtp)
        message(STATUS "  [✓] UltraNet RTP plug-in - ENABLED (UDP receiver, RFC 3550)")
    endif()

    # macOS: each UltraNet plug-in is a MODULE that resolves core symbols
    # (UltraNet_RegisterPlugin, UltraNet_ParseUrl, ...) from the host binary at
    # dlopen() time. Unlike Linux's ld, the Mach-O linker rejects undefined
    # symbols in a module unless it is linked with -undefined dynamic_lookup
    # (same mechanism the LaTeX plug-in uses below). Applied here in one place so
    # a new plug-in only needs its target name added to this list.
    # NB: when a new UltraNet plug-in is added above, add its target here too.
    if(APPLE)
        foreach(_ultranet_plugin IN ITEMS
                UltraNetSmtpPlugin UltraNetImapPlugin UltraNetPop3Plugin
                UltraNetWebDavPlugin UltraNetLdapPlugin UltraNetTelnetPlugin
                UltraNetRtspPlugin UltraNetMqttPlugin UltraNetAmqpPlugin
                UltraNetCoapPlugin UltraNetSnmpPlugin UltraNetSshPlugin
                UltraNetMdnsPlugin UltraNetRtmpPlugin UltraNetGrpcPlugin
                UltraNetSipPlugin UltraNetRtpPlugin)
            if(TARGET ${_ultranet_plugin})
                set_property(TARGET ${_ultranet_plugin} APPEND_STRING
                    PROPERTY LINK_FLAGS " -undefined dynamic_lookup")
            endif()
        endforeach()
    endif()
else()
    message(STATUS "  [−] UltraNet - DISABLED")
endif()

# ===== BUILD ULTRADATABASE LIBRARY (Stage 1: SQLite core on libsqlite3) =====
option(ULTRACANVAS_ENABLE_DATABASE "Build the UltraDatabase data-access library" ON)
if(ULTRACANVAS_ENABLE_DATABASE)
    find_package(Threads REQUIRED)
    # SQLite is the bundled/default engine. Prefer the system library; the
    # amalgamation can be vendored later without touching callers.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 sqlite3)
    endif()
    if(NOT SQLITE3_FOUND)
        find_path(SQLITE3_INCLUDE_DIRS sqlite3.h)
        find_library(SQLITE3_LIBRARIES NAMES sqlite3)
    endif()

    if(NOT SQLITE3_LIBRARIES)
        message(STATUS "  [−] UltraDatabase - DISABLED (libsqlite3 not found)")
    else()
        set(ULTRADATABASE_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraDatabase/UltraDatabaseValue.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraDatabase/UltraDatabaseManager.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/core/UltraDatabase/UltraDatabaseSqliteDriver.cpp
        )
        add_library(UltraDatabase STATIC ${ULTRADATABASE_SOURCES})
        set_target_properties(UltraDatabase PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME "ultradatabase")
        target_compile_features(UltraDatabase PUBLIC cxx_std_20)
        target_include_directories(UltraDatabase
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include/ultracanvas>)
        target_include_directories(UltraDatabase PRIVATE ${SQLITE3_INCLUDE_DIRS})
        target_link_libraries(UltraDatabase PUBLIC Threads::Threads)
        target_link_libraries(UltraDatabase PRIVATE ${SQLITE3_LIBRARIES})
        target_compile_definitions(UltraDatabase PUBLIC ULTRACANVAS_HAS_DATABASE=1)

        # Let UltraCanvas link UltraDatabase so components/apps can persist
        # data through it when the module is enabled.
        target_link_libraries(${ULTRACANVAS_LIB_NAME} PUBLIC UltraDatabase)
        target_compile_definitions(${ULTRACANVAS_LIB_NAME} PUBLIC ULTRACANVAS_HAS_DATABASE=1)
        message(STATUS "  [✓] UltraDatabase - ENABLED (SQLite ${SQLITE3_VERSION})")
    endif()
else()
    message(STATUS "  [−] UltraDatabase - DISABLED")
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
    # The vendored engine uses a u8"" literal (formula_def.cpp) that is char[]
    # under C++17 but char8_t[] under C++20 (which this project forces globally),
    # breaking std::string map initialisation. Keep u8"" as char[].
    if(MSVC)
        target_compile_options(microtex_core PRIVATE /Zc:char8_t-)
    else()
        target_compile_options(microtex_core PRIVATE -fexceptions -fno-char8_t)
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
    # macOS/Homebrew: cairo.pc only emits -I<prefix>/include/cairo, so the
    # `#include <cairo/cairo.h>` form (reached transitively via the core's
    # UltraCanvasRenderContext.h -> ImageCairo.h) needs the parent <prefix>/include
    # on the path. The core lib gets this incidentally from libvips' include dirs;
    # this MODULE doesn't link the (static) core on macOS, so add it explicitly.
    if(APPLE)
        execute_process(COMMAND brew --prefix
            OUTPUT_VARIABLE _uc_brew_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        target_include_directories(UltraCanvasLaTeX PRIVATE ${_uc_brew_prefix}/include)
    endif()
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
if(LIBRSVG_FOUND)
    message(STATUS "  librsvg: ${LIBRSVG_VERSION} (parse-once SVG documents)")
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