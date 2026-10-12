# Tests/HTMLReaderCoreSources.cmake
# The framework-independent part of the HTMLReader - parser, DOM, CSS parser,
# selector matcher and cascade, plus Trim() from UltraCanvasTextUtils.cpp,
# which is platform-free too - as one list, for every build that compiles it
# without the UI library: the unit tests' object library (Tests/CMakeLists.txt)
# and the fuzz targets (Tests/Fuzz/CMakeLists.txt), which compile it again
# with coverage instrumentation. A new HTMLReader source is added here.
# Expects ULTRACANVAS_CORE_DIR.
# Version: 1.0.0
# Last Modified: 2026-10-08
# Author: UltraCanvas Framework

set(HTMLREADER_CORE_SOURCES
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLDocument.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLParser.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/CSSStyleSheet.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLStyleResolver.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasTextUtils.cpp
)
