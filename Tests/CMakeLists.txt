# Tests/CMakeLists.txt
# UltraCanvas Test Applications
# Version: 1.0.0
# Last Modified: 2026-01-30
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

message(STATUS "")
message(STATUS "── Building UltraCanvas Tests ──")

# ===== COMPILER SETTINGS =====
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== FIND ULTRACANVAS =====
# Check if we're being built as part of the main project
if(NOT DEFINED ULTRACANVAS_ROOT)
    # Standalone build - find UltraCanvas
    set(ULTRACANVAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "UltraCanvas root directory")
endif()

# Include directories
set(ULTRACANVAS_INCLUDE_DIR "${ULTRACANVAS_ROOT}/UltraCanvas/include")
set(ULTRACANVAS_CORE_DIR "${ULTRACANVAS_ROOT}/UltraCanvas/core")

# Verify paths exist
if(NOT EXISTS "${ULTRACANVAS_INCLUDE_DIR}")
    message(WARNING "UltraCanvas include directory not found: ${ULTRACANVAS_INCLUDE_DIR}")
endif()

# ===== UCSTRING TEST =====
# UltraCanvasString.cpp is not currently part of the tree; skip the test when
# the source is absent instead of failing the configure.
set(UCSTRING_SOURCES "")
if(EXISTS "${ULTRACANVAS_CORE_DIR}/UltraCanvasString.cpp")
    set(UCSTRING_SOURCES ${ULTRACANVAS_CORE_DIR}/UltraCanvasString.cpp)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasString.cpp")
    set(UCSTRING_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasString.cpp)
endif()

if(UCSTRING_SOURCES)
message(STATUS "  Building UCStringTest...")

# Create UCStringTest executable
add_executable(UCStringTest
    ${CMAKE_CURRENT_SOURCE_DIR}/UCStringTest.cpp
    ${UCSTRING_SOURCES}
)

# Include directories for UCStringTest
target_include_directories(UCStringTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compile features
target_compile_features(UCStringTest PRIVATE cxx_std_20)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(UCStringTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

# Set executable properties
set_target_properties(UCStringTest PROPERTIES
    OUTPUT_NAME "UCStringTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "    Executable: UCStringTest")

# Add UCStringTest as a CTest
add_test(
    NAME UCStringTest
    COMMAND UCStringTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "    Test registered: UCStringTest")
else()
    message(STATUS "  UCStringTest skipped (UltraCanvasString.cpp not present)")
endif()

# ===== ENABLE CTEST =====
enable_testing()

# ===== FOCUS HISTORY TEST =====
# UCWeakMRUList (jump-to-last-window focus history) is header-only and
# framework-independent, so the test builds without linking UltraCanvas.
message(STATUS "  Building FocusHistoryTest...")

add_executable(FocusHistoryTest
    ${CMAKE_CURRENT_SOURCE_DIR}/FocusHistoryTest.cpp
)
target_include_directories(FocusHistoryTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(FocusHistoryTest PRIVATE cxx_std_20)
set_target_properties(FocusHistoryTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME FocusHistoryTest COMMAND FocusHistoryTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: FocusHistoryTest")

# ===== RECENT FILES TEST =====
# UltraTexter's recent-files store lives in the header-only
# Apps/Texter/UltraCanvasTextEditorConfig.h, so the test builds without
# linking UltraCanvas or opening a display.
message(STATUS "  Building RecentFilesTest...")

add_executable(RecentFilesTest
    ${CMAKE_CURRENT_SOURCE_DIR}/RecentFilesTest.cpp
)
target_include_directories(RecentFilesTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/Apps/Texter
)
target_compile_features(RecentFilesTest PRIVATE cxx_std_20)
set_target_properties(RecentFilesTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME RecentFilesTest COMMAND RecentFilesTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: RecentFilesTest")

# ===== TEXT WRAP TEST =====
# UltraCanvasTextWrapping.h is header-only and measures text through a callable
# instead of a render context, so the caption wrapper behind the Filer's tile
# names is tested against a synthetic font: no display, no framework link.
message(STATUS "  Building TextWrapTest...")

add_executable(TextWrapTest
    ${CMAKE_CURRENT_SOURCE_DIR}/TextWrapTest.cpp
)
target_include_directories(TextWrapTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(TextWrapTest PRIVATE cxx_std_20)
set_target_properties(TextWrapTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME TextWrapTest COMMAND TextWrapTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: TextWrapTest")

# ===== FOLDER WATCHER TEST =====
# UltraCanvasFolderWatcher is built from its own sources - the core file plus
# this platform's backend - so the test needs no display and does not link
# UltraCanvas. Platforms without a backend still build and run it: there every
# Watch() fails by design and the test checks exactly that.
message(STATUS "  Building FolderWatcherTest...")

set(FOLDER_WATCHER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/FolderWatcherTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasFolderWatcher.cpp
)
# The backend source and the define must be chosen together - the core file
# emits its fallback only when the define is absent - so both follow the single
# ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH decision made in the top-level CMakeLists.
if(ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH)
    if(WIN32)
        list(APPEND FOLDER_WATCHER_SOURCES
            ${ULTRACANVAS_ROOT}/UltraCanvas/OS/MSWindows/UltraCanvasWindowsFolderWatcher.cpp)
    else()
        list(APPEND FOLDER_WATCHER_SOURCES
            ${ULTRACANVAS_ROOT}/UltraCanvas/OS/Linux/UltraCanvasLinuxFolderWatcher.cpp)
    endif()
endif()

add_executable(FolderWatcherTest ${FOLDER_WATCHER_SOURCES})
target_include_directories(FolderWatcherTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(FolderWatcherTest PRIVATE cxx_std_20)
if(ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH)
    target_compile_definitions(FolderWatcherTest PRIVATE ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH)
endif()
find_package(Threads REQUIRED)
target_link_libraries(FolderWatcherTest PRIVATE Threads::Threads)
set_target_properties(FolderWatcherTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME FolderWatcherTest COMMAND FolderWatcherTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: FolderWatcherTest")

# ===== FILE LOCK TEST =====
# Same arrangement as the folder watcher above: UltraCanvasFileLock is built
# from its own sources (core plus this platform's backend), needs no display
# and does not link UltraCanvas. Platforms without a backend still build and
# run it: there every probe answers Unknown by design and the test checks that.
message(STATUS "  Building FileLockTest...")

set(FILE_LOCK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLockTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasFileLock.cpp
)
# Backend source and define chosen together, from the single
# ULTRACANVAS_HAS_NATIVE_FILE_LOCK decision in the top-level CMakeLists - the
# core file emits its fallback only when the define is absent.
if(ULTRACANVAS_HAS_NATIVE_FILE_LOCK)
    if(WIN32)
        list(APPEND FILE_LOCK_SOURCES
            ${ULTRACANVAS_ROOT}/UltraCanvas/OS/MSWindows/UltraCanvasWindowsFileLock.cpp)
    else()
        list(APPEND FILE_LOCK_SOURCES
            ${ULTRACANVAS_ROOT}/UltraCanvas/OS/Linux/UltraCanvasLinuxFileLock.cpp)
    endif()
endif()

add_executable(FileLockTest ${FILE_LOCK_SOURCES})
target_include_directories(FileLockTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(FileLockTest PRIVATE cxx_std_20)
if(ULTRACANVAS_HAS_NATIVE_FILE_LOCK)
    target_compile_definitions(FileLockTest PRIVATE ULTRACANVAS_HAS_NATIVE_FILE_LOCK)
endif()
set_target_properties(FileLockTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME FileLockTest COMMAND FileLockTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: FileLockTest")

# ===== VOLUME MONITOR TEST =====
# Same arrangement as the folder watcher above: UltraCanvasVolumeMonitor is
# built from its own sources, needs no display and does not link UltraCanvas.
# Mounting a filesystem needs privileges a test does not have, so the test
# checks the volume list and the monitor's lifetime rather than a real
# insertion - on the polling path as well as the native one.
message(STATUS "  Building VolumeMonitorTest...")

set(VOLUME_MONITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/VolumeMonitorTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasVolumeMonitor.cpp
)
# macOS is deliberately left on the polling path HERE only: its backend is
# Objective-C++ and linking AppKit into a standalone test buys nothing the
# library build does not already cover. Everywhere else the backend source and
# the define are chosen together, because the core file emits its fallback
# definitions only when the define is absent.
set(VOLUME_MONITOR_NATIVE OFF)
if(ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR AND NOT APPLE)
    set(VOLUME_MONITOR_NATIVE ON)
    if(WIN32)
        list(APPEND VOLUME_MONITOR_SOURCES
            ${ULTRACANVAS_ROOT}/UltraCanvas/OS/MSWindows/UltraCanvasWindowsVolumeMonitor.cpp)
    else()
        list(APPEND VOLUME_MONITOR_SOURCES
            ${ULTRACANVAS_ROOT}/UltraCanvas/OS/Linux/UltraCanvasLinuxVolumeMonitor.cpp)
    endif()
endif()

add_executable(VolumeMonitorTest ${VOLUME_MONITOR_SOURCES})
target_include_directories(VolumeMonitorTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(VolumeMonitorTest PRIVATE cxx_std_20)
if(VOLUME_MONITOR_NATIVE)
    target_compile_definitions(VolumeMonitorTest PRIVATE ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR)
endif()
target_link_libraries(VolumeMonitorTest PRIVATE Threads::Threads)
set_target_properties(VolumeMonitorTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME VolumeMonitorTest COMMAND VolumeMonitorTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: VolumeMonitorTest")

# ===== SPELL CHECKER TEST =====
# The spell service, tokenizer and Hunspell backend are framework-independent,
# so the test builds straight from the sources. The WASM backend stub supplies
# CreateNativeSpellCheckBackend(), which keeps the portable Hunspell path under
# test on every platform rather than whatever the host OS happens to provide.
message(STATUS "  Building SpellCheckerTest...")

add_executable(SpellCheckerTest
    ${CMAKE_CURRENT_SOURCE_DIR}/SpellCheckerTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasSpellChecker.cpp
    ${ULTRACANVAS_CORE_DIR}/SpellCheckLanguageNames.cpp
    ${ULTRACANVAS_CORE_DIR}/SpellCheckBackendHunspell.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/OS/WASM/UltraCanvasSpellCheckSupport.cpp
)
target_include_directories(SpellCheckerTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(SpellCheckerTest PRIVATE cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(SpellCheckerTest PRIVATE Threads::Threads)

# HUNSPELL_FOUND is set in the UltraCanvas subdirectory scope, so it is not
# visible here - probe again rather than silently building the stub.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HUNSPELL QUIET hunspell)
endif()

# Optional, exactly as in the framework build: without it the backend compiles
# to a stub, reports zero dictionaries, and the test skips those checks.
if(HUNSPELL_FOUND)
    target_compile_definitions(SpellCheckerTest PRIVATE ULTRACANVAS_HAS_HUNSPELL)
    target_include_directories(SpellCheckerTest PRIVATE ${HUNSPELL_INCLUDE_DIRS})
    target_link_directories(SpellCheckerTest PRIVATE ${HUNSPELL_LIBRARY_DIRS})
    target_link_libraries(SpellCheckerTest PRIVATE ${HUNSPELL_LIBRARIES})
endif()

set_target_properties(SpellCheckerTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME SpellCheckerTest COMMAND SpellCheckerTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: SpellCheckerTest")

# ===== TEXTAREA SPELL CHECK RUNTIME TEST =====
# Needs the real framework: the byte-offset to screen-rectangle mapping runs
# through live line layouts, scroll offsets and the gutter. Skips itself when
# no DISPLAY or no dictionary is present.
if(TARGET UltraCanvas)
    message(STATUS "  Building TextAreaSpellCheckTest...")
    add_executable(TextAreaSpellCheckTest
        ${CMAKE_CURRENT_SOURCE_DIR}/TextAreaSpellCheckTest.cpp
    )
    target_include_directories(TextAreaSpellCheckTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(TextAreaSpellCheckTest PRIVATE cxx_std_20)
    target_link_libraries(TextAreaSpellCheckTest PRIVATE UltraCanvas)
    set_target_properties(TextAreaSpellCheckTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME TextAreaSpellCheckTest COMMAND TextAreaSpellCheckTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: TextAreaSpellCheckTest")
else()
    message(STATUS "  TextAreaSpellCheckTest skipped (UltraCanvas target not present)")
endif()

# ===== ULTRATEXTER MARKDOWN SPELL RANGE TEST =====
# The scanner that keeps fenced code, link targets and math out of UltraTexter's
# spell check has no framework or editor dependency, so the test builds from
# that one source file.
message(STATUS "  Building TexterMarkdownSpellRangesTest...")

add_executable(TexterMarkdownSpellRangesTest
    ${CMAKE_CURRENT_SOURCE_DIR}/TexterMarkdownSpellRangesTest.cpp
    ${ULTRACANVAS_ROOT}/Apps/Texter/UltraCanvasMarkdownSpellRanges.cpp
)
target_include_directories(TexterMarkdownSpellRangesTest PRIVATE
    ${ULTRACANVAS_ROOT}/Apps/Texter)
target_compile_features(TexterMarkdownSpellRangesTest PRIVATE cxx_std_20)
set_target_properties(TexterMarkdownSpellRangesTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME TexterMarkdownSpellRangesTest COMMAND TexterMarkdownSpellRangesTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: TexterMarkdownSpellRangesTest")

# ===== HTMLREADER TEST =====
# The HTMLReader parse/CSS/resolve layer is framework-independent, so the
# test builds straight from the sources without linking UltraCanvas.
message(STATUS "  Building HTMLReaderTest...")

add_executable(HTMLReaderTest
    ${CMAKE_CURRENT_SOURCE_DIR}/HTMLReaderTest.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLDocument.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLParser.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/CSSStyleSheet.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLStyleResolver.cpp
    # Trim() lives in UltraCanvasTextUtils.cpp (platform-free); without it
    # this target fails to link.
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasTextUtils.cpp
)
target_include_directories(HTMLReaderTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
target_compile_features(HTMLReaderTest PRIVATE cxx_std_20)
set_target_properties(HTMLReaderTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME HTMLReaderTest COMMAND HTMLReaderTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: HTMLReaderTest")

# ===== EBOOK ARCHIVE TEST =====
message(STATUS "  Building EBookArchiveTest...")

# miniz is C; the top-level project only declares C++.
enable_language(C)

add_executable(EBookArchiveTest
    ${CMAKE_CURRENT_SOURCE_DIR}/EBookArchiveTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/EBookArchive.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz/miniz.c
)
target_include_directories(EBookArchiveTest PRIVATE
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook
    ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz
)
target_compile_features(EBookArchiveTest PRIVATE cxx_std_20)
set_target_properties(EBookArchiveTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME EBookArchiveTest COMMAND EBookArchiveTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: EBookArchiveTest")

# ===== EBOOK ENGINE TEST =====
message(STATUS "  Building EBookEngineTest...")

add_executable(EBookEngineTest
    ${CMAKE_CURRENT_SOURCE_DIR}/EBookEngineTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/EPUBEngine.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/FB2Engine.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/MOBIEngine.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/TXTEngine.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/IEBookEngine.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook/EBookArchive.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLDocument.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/HTMLParser.cpp
    ${ULTRACANVAS_CORE_DIR}/HTMLReader/CSSStyleSheet.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz/miniz.c
    # Trim() lives in UltraCanvasTextUtils.cpp; see HTMLReaderTest above.
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasTextUtils.cpp
)
target_include_directories(EBookEngineTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/eBook
    ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz
)
target_compile_features(EBookEngineTest PRIVATE cxx_std_20)
set_target_properties(EBookEngineTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME EBookEngineTest COMMAND EBookEngineTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: EBookEngineTest")

# ===== WORD FORMATS TEST =====
# Round-trip test for the ODT/DOCX document module. Needs only the module
# sources + vendored miniz + system tinyxml2 — not the full UltraCanvas lib.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TINYXML2_TEST tinyxml2)
endif()

if(TINYXML2_TEST_FOUND)
    message(STATUS "  Building WordFormatsTest...")

    set(ULTRACANVAS_SRC_DIR "${ULTRACANVAS_ROOT}/UltraCanvas")
    add_executable(WordFormatsTest
        ${CMAKE_CURRENT_SOURCE_DIR}/WordFormatsTest.cpp
        ${ULTRACANVAS_SRC_DIR}/core/UltraCanvasZipPackage.cpp
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/Word/UltraCanvasRichDocument.cpp
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/Word/UltraCanvasWordDocumentIO.cpp
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/Word/UltraCanvasOdtFormat.cpp
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/Word/UltraCanvasDocxFormat.cpp
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/Word/UltraCanvasMathToLatex.cpp
        # UCWordDocumentIO dispatches .tex to the LaTeX document reader
        ${ULTRACANVAS_SRC_DIR}/Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.cpp
        ${ULTRACANVAS_SRC_DIR}/third_party/miniz/miniz.c
        # Trim() lives in UltraCanvasTextUtils.cpp; see HTMLReaderTest above.
        ${ULTRACANVAS_SRC_DIR}/core/UltraCanvasTextUtils.cpp
    )
    target_include_directories(WordFormatsTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_SRC_DIR}/third_party/miniz
        ${TINYXML2_TEST_INCLUDE_DIRS}
    )
    target_link_libraries(WordFormatsTest PRIVATE ${TINYXML2_TEST_LIBRARIES})
    target_compile_definitions(WordFormatsTest PRIVATE
        WORDTEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_compile_features(WordFormatsTest PRIVATE cxx_std_20)
    set_target_properties(WordFormatsTest PROPERTIES
        OUTPUT_NAME "WordFormatsTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME WordFormatsTest
        COMMAND WordFormatsTest ${CMAKE_BINARY_DIR}/wordformats-test-out
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: WordFormatsTest")
else()
    message(STATUS "  tinyxml2 not found - WordFormatsTest disabled")
endif()

# ===== MIND MAP IO TEST =====
# ===== LATEX DOCUMENT TEST =====
# The LaTeX document-subset importer (UltraCanvasLaTeXDocumentReader →
# UCRichDocument): structure, text commands, macros, tables, references,
# math runs/blocks, diagnostics, Markdown/ODT round trips of the math model
# parts, format detection, and the shipped media/LaTex corpus. Needs the Word
# module sources (for UCWordDocumentIO) like WordFormatsTest, not the UI stack.
if(TINYXML2_TEST_FOUND)
    message(STATUS "  Building LaTeXDocumentTest...")
    add_executable(LaTeXDocumentTest
        ${CMAKE_CURRENT_SOURCE_DIR}/LaTeXDocumentTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/core/UltraCanvasZipPackage.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/Word/UltraCanvasRichDocument.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/Word/UltraCanvasWordDocumentIO.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/Word/UltraCanvasOdtFormat.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/Word/UltraCanvasDocxFormat.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Documents/Word/UltraCanvasMathToLatex.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz/miniz.c
        ${ULTRACANVAS_ROOT}/UltraCanvas/core/UltraCanvasTextUtils.cpp
    )
    target_include_directories(LaTeXDocumentTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz
        ${TINYXML2_TEST_INCLUDE_DIRS}
    )
    target_link_libraries(LaTeXDocumentTest PRIVATE ${TINYXML2_TEST_LIBRARIES})
    target_compile_features(LaTeXDocumentTest PRIVATE cxx_std_20)
    set_target_properties(LaTeXDocumentTest PROPERTIES
        OUTPUT_NAME "LaTeXDocumentTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME LaTeXDocumentTest
        COMMAND LaTeXDocumentTest ${CMAKE_BINARY_DIR}/latexdocument-test-out ${ULTRACANVAS_ROOT}/media/LaTex
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: LaTeXDocumentTest")
else()
    message(STATUS "  tinyxml2 not found - LaTeXDocumentTest disabled")
endif()

# Interchange: Mermaid, FreeMind/Freeplane .mm, OPML, indented text, CSV, XMind
# content.json, SVG export and format sniffing. Checks round-trip fidelity and
# that a failed import leaves the caller's model untouched. Needs the same
# system tinyxml2 + vendored miniz/yyjson as WordFormatsTest, not the UI stack.
if(TINYXML2_TEST_FOUND)
    message(STATUS "  Building MindMapIOTest...")
    add_executable(MindMapIOTest
        ${CMAKE_CURRENT_SOURCE_DIR}/MindMapIOTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasMindMapLayout.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasMindMapIO.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/core/DataFormats/UltraCanvasJSON.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/core/UltraCanvasZipPackage.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/yyjson/yyjson.c
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz/miniz.c
    )
    target_include_directories(MindMapIOTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/yyjson
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz
        ${TINYXML2_TEST_INCLUDE_DIRS}
    )
    target_link_libraries(MindMapIOTest PRIVATE ${TINYXML2_TEST_LIBRARIES})
    target_compile_features(MindMapIOTest PRIVATE cxx_std_20)
    set_target_properties(MindMapIOTest PROPERTIES
        OUTPUT_NAME "MindMapIOTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME MindMapIOTest
        COMMAND MindMapIOTest
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: MindMapIOTest")
else()
    message(STATUS "  tinyxml2 not found - MindMapIOTest disabled")
endif()


# ===== SPREADSHEET ODS FORMULA TEST =====
# Regression test for translating OpenDocument (ODS) formula references into the
# engine's native A1 syntax and tokenizing them. Header-only: exercises the real
# OdsFormulaToNative() and FormulaTokenizer with no UI stack and no link deps.
message(STATUS "  Building SpreadsheetOdsFormulaTest...")
add_executable(SpreadsheetOdsFormulaTest
    ${CMAKE_CURRENT_SOURCE_DIR}/SpreadsheetOdsFormulaTest.cpp
)
target_include_directories(SpreadsheetOdsFormulaTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(SpreadsheetOdsFormulaTest PRIVATE cxx_std_20)
set_target_properties(SpreadsheetOdsFormulaTest PROPERTIES
    OUTPUT_NAME "SpreadsheetOdsFormulaTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME SpreadsheetOdsFormulaTest
    COMMAND SpreadsheetOdsFormulaTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: SpreadsheetOdsFormulaTest")

# ===== SPREADSHEET NUMBER FORMAT TEST =====
# Regression test for currency/percentage/date display formatting and the
# DateTimeValue date round-trip. Header-only: exercises the real SpreadsheetCell
# / NumberFormat / DateTimeValue with no UI stack and no link deps.
message(STATUS "  Building SpreadsheetNumberFormatTest...")
add_executable(SpreadsheetNumberFormatTest
    ${CMAKE_CURRENT_SOURCE_DIR}/SpreadsheetNumberFormatTest.cpp
)
target_include_directories(SpreadsheetNumberFormatTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(SpreadsheetNumberFormatTest PRIVATE cxx_std_20)
set_target_properties(SpreadsheetNumberFormatTest PROPERTIES
    OUTPUT_NAME "SpreadsheetNumberFormatTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME SpreadsheetNumberFormatTest
    COMMAND SpreadsheetNumberFormatTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: SpreadsheetNumberFormatTest")

# ===== SPREADSHEET COLUMN SIZING TEST =====
# Regression test for column widths taken from an imported document (ODF/OOXML
# length conversion) and for the auto-width pass that fits the columns a file
# did not size. Exercises the real SpreadsheetSheet with no UI stack.
message(STATUS "  Building SpreadsheetColumnSizingTest...")
add_executable(SpreadsheetColumnSizingTest
    ${CMAKE_CURRENT_SOURCE_DIR}/SpreadsheetColumnSizingTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/core/UltraCanvasSpreadsheetSheet.cpp
)
target_include_directories(SpreadsheetColumnSizingTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(SpreadsheetColumnSizingTest PRIVATE cxx_std_20)
set_target_properties(SpreadsheetColumnSizingTest PROPERTIES
    OUTPUT_NAME "SpreadsheetColumnSizingTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME SpreadsheetColumnSizingTest
    COMMAND SpreadsheetColumnSizingTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: SpreadsheetColumnSizingTest")

# ===== CONTOUR GEOMETRY TEST =====
# Marching-squares isolines (including saddle disambiguation), isoband cell
# clipping, level generation, NaN holes, point gridding and polyline
# post-processing. No UI stack - only the two contour algorithm sources.
message(STATUS "  Building ContourGeometryTest...")
add_executable(ContourGeometryTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ContourGeometryTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/UltraCanvasContourGrid.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/UltraCanvasMarchingSquares.cpp
)
target_include_directories(ContourGeometryTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(ContourGeometryTest PRIVATE cxx_std_20)
set_target_properties(ContourGeometryTest PROPERTIES
    OUTPUT_NAME "ContourGeometryTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ContourGeometryTest
    COMMAND ContourGeometryTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ContourGeometryTest")

# ===== MODEL STORAGE TEST =====
# ModelStorage::ModelDocument, the universal 3D scene structure every 3D file
# format reads into and writes out of. Pure geometry and data - no UI stack and
# no GL - so it runs anywhere. Pass an .stl path as argv[1] to also exercise the
# Mesh3D bridge against a real file.
message(STATUS "  Building ModelStorageTest...")
add_executable(ModelStorageTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelStorageTest.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/UltraCanvasModelMesh3D.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/STL/UltraCanvasSTLLoader.cpp
)
target_include_directories(ModelStorageTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(ModelStorageTest PRIVATE cxx_std_20)
set_target_properties(ModelStorageTest PROPERTIES
    OUTPUT_NAME "ModelStorageTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ModelStorageTest
    COMMAND ModelStorageTest "${ULTRACANVAS_ROOT}/media/vector/STL/Toy airplane model for children B0011994 3d model file for 3d printer.stl"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ModelStorageTest")

# ===== ALEMBIC READER TEST =====
# The Ogawa container and AbcGeom, against the same E-45 aircraft the 3DS, OBJ,
# DXF and COLLADA suites read - exported from the same .blend, and like the
# .dae exported without applying the mirror modifier. The suite pins that
# difference rather than letting a later change quietly "fix" it, and checks
# the face winding against the per-corner normals the file itself carries,
# which is the one decision in an Alembic reader that is invisible until a
# model renders inside out.
message(STATUS "  Building ModelAlembicTest...")
add_executable(ModelAlembicTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelAlembicTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Alembic/UltraCanvasOgawaFile.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Alembic/UltraCanvasAlembicConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/OBJ/UltraCanvasOBJConverter.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
)
target_include_directories(ModelAlembicTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(ModelAlembicTest PRIVATE cxx_std_20)
set_target_properties(ModelAlembicTest PROPERTIES
    OUTPUT_NAME "ModelAlembicTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ModelAlembicTest
    COMMAND ModelAlembicTest "${ULTRACANVAS_ROOT}/media/models"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ModelAlembicTest")

# ===== STEP READER / WRITER TEST =====
# The first B-rep reader: ISO 10303-21 syntax as unit tests, and AP203/214
# entity meanings against the hand-authored samples in media/models/STEP. The
# samples are hand-authored on purpose - a reader tested only against files its
# own writer produced proves nothing - and the geometric assertions (a meshed
# block's signed volume, a pin's volume against pi*r^2*h, a rational patch's
# radius) cannot come out right by accident.
message(STATUS "  Building ModelStepTest...")
add_executable(ModelStepTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelStepTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/STEP/UltraCanvasStepFile.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/STEP/UltraCanvasStepConverter.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
)
target_include_directories(ModelStepTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(ModelStepTest PRIVATE cxx_std_20)
set_target_properties(ModelStepTest PROPERTIES
    OUTPUT_NAME "ModelStepTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ModelStepTest
    COMMAND ModelStepTest "${ULTRACANVAS_ROOT}/media/models/STEP"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ModelStepTest")

# ===== B-REP STORAGE TEST =====
# ModelStorage::BrepData - exact trimmed surfaces and the topology that closes
# them into solids, plus the tessellator that approximates them. The assertions
# are geometric (a meshed box's signed volume, a meshed cylinder's radial
# error, a plate-with-hole's area) because a B-rep that holds the right numbers
# and meshes them wrongly produces a model that looks plausible and is not the
# part. No sample file: the bodies are built in the test, which is the point -
# the structure has to be constructible from a reader's point of view.
message(STATUS "  Building BrepStorageTest...")
add_executable(BrepStorageTest
    ${CMAKE_CURRENT_SOURCE_DIR}/BrepStorageTest.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
)
target_include_directories(BrepStorageTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(BrepStorageTest PRIVATE cxx_std_20)
set_target_properties(BrepStorageTest PROPERTIES
    OUTPUT_NAME "BrepStorageTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME BrepStorageTest
    COMMAND BrepStorageTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: BrepStorageTest")

# ===== 3DS MODEL READER TEST =====
# The 3DS chunk reader against the real E-45 aircraft in media/models/3DS -
# the first scene format read into ModelDocument, so also the first end-to-end
# check that the structure holds meshes, object matrices, materials and texture
# maps together. Malformed-input cases run with or without the sample.
message(STATUS "  Building Model3DSTest...")
add_executable(Model3DSTest
    ${CMAKE_CURRENT_SOURCE_DIR}/Model3DSTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/3DS/UltraCanvas3DSConverter.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/UltraCanvasModelMesh3D.cpp
)
target_include_directories(Model3DSTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(Model3DSTest PRIVATE cxx_std_20)
set_target_properties(Model3DSTest PROPERTIES
    OUTPUT_NAME "Model3DSTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME Model3DSTest
    COMMAND Model3DSTest ${ULTRACANVAS_ROOT}/media/models/3DS/E-45-Aircraft.3ds
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: Model3DSTest")

# ===== OBJ MODEL READER/WRITER TEST =====
# OBJ is the first format on ModelDocument with both a reader and a writer, so
# this is where the round trip lives - and where the same aircraft, exported
# once as OBJ (Y-up, quads) and once as 3DS (Z-up, triangles), is checked to
# land on the same geometry through two independent readers.
message(STATUS "  Building ModelOBJTest...")
add_executable(ModelOBJTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelOBJTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/OBJ/UltraCanvasOBJConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/3DS/UltraCanvas3DSConverter.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
)
target_include_directories(ModelOBJTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(ModelOBJTest PRIVATE cxx_std_20)
set_target_properties(ModelOBJTest PROPERTIES
    OUTPUT_NAME "ModelOBJTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ModelOBJTest
    COMMAND ModelOBJTest
            "${ULTRACANVAS_ROOT}/media/models/OBJ/E-45-Aircraft.obj"
            "${ULTRACANVAS_ROOT}/media/models/3DS/E-45-Aircraft.3ds"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ModelOBJTest")

# ===== DXF 3D MODEL READER TEST =====
# DXF read as geometry rather than as a drawing: the 3D entities the Vector
# plugin's DXF reader has to flatten. Checks both kinds of DXF - the exported
# model in media/models/DXF, and a drawing from media/vector/DXF that must be
# refused with an explanation instead of returned empty.
message(STATUS "  Building ModelDXFTest...")
add_executable(ModelDXFTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelDXFTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/DXF/UltraCanvasDXFModelConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/3DS/UltraCanvas3DSConverter.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasCADPalette.cpp
)
target_include_directories(ModelDXFTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(ModelDXFTest PRIVATE cxx_std_20)
set_target_properties(ModelDXFTest PROPERTIES
    OUTPUT_NAME "ModelDXFTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ModelDXFTest
    COMMAND ModelDXFTest
            "${ULTRACANVAS_ROOT}/media/models/DXF/E-45-Aircraft.dxf"
            "${ULTRACANVAS_ROOT}/media/models/3DS/E-45-Aircraft.3ds"
            "${ULTRACANVAS_ROOT}/media/vector/DXF/women-body.dxf"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ModelDXFTest")

# ===== COLLADA MODEL READER TEST =====
# COLLADA is the first format that states unit, up axis, a node hierarchy and
# animation, so this is where those paths meet a real file. Needs tinyxml2.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TEST_TINYXML2 QUIET tinyxml2)
endif()
if(TEST_TINYXML2_FOUND)
    message(STATUS "  Building ModelColladaTest...")
    add_executable(ModelColladaTest
        ${CMAKE_CURRENT_SOURCE_DIR}/ModelColladaTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/COLLADA/UltraCanvasColladaConverter.cpp
        ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    )
    target_include_directories(ModelColladaTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
        ${TEST_TINYXML2_INCLUDE_DIRS}
    )
    target_link_libraries(ModelColladaTest PRIVATE ${TEST_TINYXML2_LIBRARIES})
    target_link_directories(ModelColladaTest PRIVATE ${TEST_TINYXML2_LIBRARY_DIRS})
    target_compile_features(ModelColladaTest PRIVATE cxx_std_20)
    set_target_properties(ModelColladaTest PROPERTIES
        OUTPUT_NAME "ModelColladaTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME ModelColladaTest
        COMMAND ModelColladaTest "${ULTRACANVAS_ROOT}/media/models/COLLADA/E-45-Aircraft.dae"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: ModelColladaTest")
else()
    message(STATUS "  Skipping ModelColladaTest (tinyxml2 not found)")
endif()

# ===== BLENDER FILE INSPECTOR TEST =====
# The .blend inspector and the converter that deliberately declines to import
# geometry. Needs zlib to inflate a compressed save file.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    message(STATUS "  Building ModelBlendTest...")
    add_executable(ModelBlendTest
        ${CMAKE_CURRENT_SOURCE_DIR}/ModelBlendTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Blend/UltraCanvasBlendFile.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Blend/UltraCanvasBlendConverter.cpp
        ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    )
    target_include_directories(ModelBlendTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(ModelBlendTest PRIVATE ${ZLIB_LIBRARIES})
    target_compile_features(ModelBlendTest PRIVATE cxx_std_20)
    set_target_properties(ModelBlendTest PROPERTIES
        OUTPUT_NAME "ModelBlendTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME ModelBlendTest
        COMMAND ModelBlendTest "${ULTRACANVAS_ROOT}/media/models/Blend/E-45-Aircraft.blend"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: ModelBlendTest")
else()
    message(STATUS "  Skipping ModelBlendTest (zlib not found)")
endif()

# ===== FBX MODEL READER TEST =====
# FBX is the one format whose scene is a connection graph rather than a tree,
# and the one with a real transform chain. Its container is deflate-compressed,
# so this needs zlib - and the suite builds binary FBX by hand, because nothing
# about the container is reachable from an exported sample alone. Two samples,
# because .fbx names two formats: a 7.4 binary and a 6.1 ASCII, which agree on
# neither their byte layout nor their object model.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    message(STATUS "  Building ModelFbxTest...")
    add_executable(ModelFbxTest
        ${CMAKE_CURRENT_SOURCE_DIR}/ModelFbxTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/FBX/UltraCanvasFbxFile.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/FBX/UltraCanvasFbxConverter.cpp
        ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
        ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
        ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
        ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    )
    target_include_directories(ModelFbxTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(ModelFbxTest PRIVATE ${ZLIB_LIBRARIES})
    target_compile_features(ModelFbxTest PRIVATE cxx_std_20)
    set_target_properties(ModelFbxTest PROPERTIES
        OUTPUT_NAME "ModelFbxTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME ModelFbxTest
        COMMAND ModelFbxTest "${ULTRACANVAS_ROOT}/media/models/FBX/E-45-Aircraft.fbx"
                             "${ULTRACANVAS_ROOT}/media/models/FBX/E-45-Aircraft-6.1-ascii.fbx"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: ModelFbxTest")
else()
    message(STATUS "  Skipping ModelFbxTest (zlib not found)")
endif()

# ===== MODEL FORMATS DISPATCH TEST =====
# The seam that makes the converters a plugin matrix rather than five classes:
# extension dispatch, the claimed-extension lists, and loading every sample
# through one call. It compiles the dispatch TU rather than the IGraphicsPlugin
# facade, which is why it needs no UI *symbols* - but the facade's header still
# reaches UltraCanvasImage.h and so needs the core's include paths, Cairo's
# among them. The link edge below is how this repository carries those; without
# it the target builds only where Cairo happens to sit on the compiler's
# default search path, which is Linux and not macOS.
set(MODEL_DISPATCH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelFormatsPluginTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/UltraCanvasModelFormatsDispatch.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/3DS/UltraCanvas3DSConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/OBJ/UltraCanvasOBJConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/DXF/UltraCanvasDXFModelConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/STEP/UltraCanvasStepFile.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/STEP/UltraCanvasStepConverter.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Alembic/UltraCanvasOgawaFile.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Alembic/UltraCanvasAlembicConverter.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelMath.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasPolygonTriangulation.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasBrepStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasModelStorage.cpp
    ${ULTRACANVAS_CORE_DIR}/DataFormats/UltraCanvasCADPalette.cpp
)
set(MODEL_DISPATCH_DEFINES "")
set(MODEL_DISPATCH_INCLUDES "")
set(MODEL_DISPATCH_LIBS "")
if(TARGET UltraCanvas)
    list(APPEND MODEL_DISPATCH_LIBS UltraCanvas)
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(DISPATCH_TINYXML2 QUIET tinyxml2)
endif()
if(DISPATCH_TINYXML2_FOUND)
    list(APPEND MODEL_DISPATCH_SOURCES
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/COLLADA/UltraCanvasColladaConverter.cpp)
    list(APPEND MODEL_DISPATCH_DEFINES ULTRACANVAS_HAS_COLLADA_CONVERTER=1)
    list(APPEND MODEL_DISPATCH_INCLUDES ${DISPATCH_TINYXML2_INCLUDE_DIRS})
    list(APPEND MODEL_DISPATCH_LIBS ${DISPATCH_TINYXML2_LIBRARIES})
endif()

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    list(APPEND MODEL_DISPATCH_SOURCES
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Blend/UltraCanvasBlendFile.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/Blend/UltraCanvasBlendConverter.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/FBX/UltraCanvasFbxFile.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Models/FBX/UltraCanvasFbxConverter.cpp)
    list(APPEND MODEL_DISPATCH_DEFINES ULTRACANVAS_HAS_BLEND_CONVERTER=1
                                       ULTRACANVAS_HAS_FBX_CONVERTER=1)
    list(APPEND MODEL_DISPATCH_INCLUDES ${ZLIB_INCLUDE_DIRS})
    list(APPEND MODEL_DISPATCH_LIBS ${ZLIB_LIBRARIES})
endif()

message(STATUS "  Building ModelFormatsPluginTest...")
add_executable(ModelFormatsPluginTest ${MODEL_DISPATCH_SOURCES})
target_include_directories(ModelFormatsPluginTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
    ${MODEL_DISPATCH_INCLUDES}
)
target_compile_definitions(ModelFormatsPluginTest PRIVATE ${MODEL_DISPATCH_DEFINES})
target_link_libraries(ModelFormatsPluginTest PRIVATE ${MODEL_DISPATCH_LIBS})
target_compile_features(ModelFormatsPluginTest PRIVATE cxx_std_20)
set_target_properties(ModelFormatsPluginTest PROPERTIES
    OUTPUT_NAME "ModelFormatsPluginTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ModelFormatsPluginTest
    COMMAND ModelFormatsPluginTest "${ULTRACANVAS_ROOT}/media/models"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ModelFormatsPluginTest")

# ===== CSS LAYOUT AUTO-EXPAND TEST =====
# The auto-expanding, centred card pattern (UltraMail's account tile) on the
# real CSSLayout engine: an auto-width flex column grows with an inner row of
# auto-sized counters, keeps every row centred, and honours its minimum width.
# Pure geometry - the layout engine has no UI-stack dependencies.
message(STATUS "  Building CSSLayoutAutoExpandTest...")
add_executable(CSSLayoutAutoExpandTest
    ${CMAKE_CURRENT_SOURCE_DIR}/CSSLayoutAutoExpandTest.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/Element.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/FlexLayout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/GridLayout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/AbsoluteLayout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/Dimension.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/Layout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/LayoutUtils.cpp
)
target_include_directories(CSSLayoutAutoExpandTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_CORE_DIR}
)
target_compile_features(CSSLayoutAutoExpandTest PRIVATE cxx_std_20)
set_target_properties(CSSLayoutAutoExpandTest PROPERTIES
    OUTPUT_NAME "CSSLayoutAutoExpandTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME CSSLayoutAutoExpandTest
    COMMAND CSSLayoutAutoExpandTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: CSSLayoutAutoExpandTest")

# ===== DIALOG ICON LAYOUT TEST =====
# The modal dialog's content row with and without the severity icon: hiding it
# must drop its column and the flex gap after it, so an element centred in the
# message column (the progress dialog's ring) lands on the window's centre.
# Pure geometry - the layout engine has no UI-stack dependencies.
message(STATUS "  Building DialogIconLayoutTest...")
add_executable(DialogIconLayoutTest
    ${CMAKE_CURRENT_SOURCE_DIR}/DialogIconLayoutTest.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/Element.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/FlexLayout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/GridLayout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/AbsoluteLayout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/Dimension.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/Layout.cpp
    ${ULTRACANVAS_CORE_DIR}/CSSLayout/LayoutUtils.cpp
)
target_include_directories(DialogIconLayoutTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_CORE_DIR}
)
target_compile_features(DialogIconLayoutTest PRIVATE cxx_std_20)
set_target_properties(DialogIconLayoutTest PROPERTIES
    OUTPUT_NAME "DialogIconLayoutTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME DialogIconLayoutTest
    COMMAND DialogIconLayoutTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: DialogIconLayoutTest")

# ===== PACKET LAYOUT TEST =====
# Packet structure model, layout engine (row splitting with continuation
# edges, proportional clamping, lanes), validators, and the RFC-pinning
# template tests. No UI stack - the layout engine is pure geometry.
message(STATUS "  Building PacketLayoutTest...")
add_executable(PacketLayoutTest
    ${CMAKE_CURRENT_SOURCE_DIR}/PacketLayoutTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasPacketLayout.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasPacketTemplates.cpp
)
target_include_directories(PacketLayoutTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(PacketLayoutTest PRIVATE cxx_std_20)
set_target_properties(PacketLayoutTest PROPERTIES
    OUTPUT_NAME "PacketLayoutTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME PacketLayoutTest
    COMMAND PacketLayoutTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: PacketLayoutTest")

# ===== GIT GRAPH LAYOUT TEST =====
# Commit ordering, lane assignment, merge/octopus handling, swimlane banding
# and edge routing. Pure geometry - no UI stack, no link dependencies beyond
# the layout source itself.
message(STATUS "  Building GitGraphLayoutTest...")
add_executable(GitGraphLayoutTest
    ${CMAKE_CURRENT_SOURCE_DIR}/GitGraphLayoutTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasGitGraphLayout.cpp
)
target_include_directories(GitGraphLayoutTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(GitGraphLayoutTest PRIVATE cxx_std_20)
set_target_properties(GitGraphLayoutTest PROPERTIES
    OUTPUT_NAME "GitGraphLayoutTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME GitGraphLayoutTest
    COMMAND GitGraphLayoutTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: GitGraphLayoutTest")

# ===== GIT GRAPH MERMAID TEST =====
# Mermaid gitGraph import/export: statements, attributes, the init header,
# error reporting and an import -> export -> import round trip.
message(STATUS "  Building GitGraphMermaidTest...")
add_executable(GitGraphMermaidTest
    ${CMAKE_CURRENT_SOURCE_DIR}/GitGraphMermaidTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasGitGraphMermaid.cpp
)
target_include_directories(GitGraphMermaidTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(GitGraphMermaidTest PRIVATE cxx_std_20)
set_target_properties(GitGraphMermaidTest PROPERTIES
    OUTPUT_NAME "GitGraphMermaidTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME GitGraphMermaidTest
    COMMAND GitGraphMermaidTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: GitGraphMermaidTest")

# ===== GIT REPOSITORY TEST =====
# Reads a real repository (this checkout by default): refs, loose and packed
# objects, delta chains and the newest-first walk. Needs the vendored miniz for
# zlib inflate; skips cleanly when no .git directory is present.
message(STATUS "  Building GitRepositoryTest...")
add_executable(GitRepositoryTest
    ${CMAKE_CURRENT_SOURCE_DIR}/GitRepositoryTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/core/UltraCanvasGitRepository.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz/miniz.c
)
target_include_directories(GitRepositoryTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz
)
target_compile_definitions(GitRepositoryTest PRIVATE
    ULTRACANVAS_REPO_ROOT="${ULTRACANVAS_ROOT}"
)
target_compile_features(GitRepositoryTest PRIVATE cxx_std_20)
set_target_properties(GitRepositoryTest PROPERTIES
    OUTPUT_NAME "GitRepositoryTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME GitRepositoryTest
    COMMAND GitRepositoryTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: GitRepositoryTest")

# ===== MIND MAP LAYOUT TEST =====
# Topic tree model (add/remove/reparent/cycle rejection, outline build, outline
# numbering, relationships) and the layout engine (balanced, logic, org chart and
# radial structures, balance policies, collapse, floating topics, spacing) with a
# non-overlap stress case over a deep ragged tree. Pure geometry - no UI stack.
message(STATUS "  Building MindMapLayoutTest...")
add_executable(MindMapLayoutTest
        ${CMAKE_CURRENT_SOURCE_DIR}/MindMapLayoutTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasMindMapLayout.cpp
)
target_include_directories(MindMapLayoutTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(MindMapLayoutTest PRIVATE cxx_std_20)
set_target_properties(MindMapLayoutTest PROPERTIES
        OUTPUT_NAME "MindMapLayoutTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
        NAME MindMapLayoutTest
        COMMAND MindMapLayoutTest
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: MindMapLayoutTest")

# ===== DIAGRAM ROUTING TEST =====
# Unit tests for the shared orthogonal router (cardinal L/Z paths, A* obstacle
# avoidance, label anchors, arrow angles). UI-free - links only the one source
# file under test.
message(STATUS "  Building DiagramRoutingTest...")
add_executable(DiagramRoutingTest
    ${CMAKE_CURRENT_SOURCE_DIR}/DiagramRoutingTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasDiagramRouting.cpp
)
target_include_directories(DiagramRoutingTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(DiagramRoutingTest PRIVATE cxx_std_20)
set_target_properties(DiagramRoutingTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME DiagramRoutingTest COMMAND DiagramRoutingTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: DiagramRoutingTest")

# ===== CLASS LAYOUT TEST =====
# Unit tests for the class-diagram layout engine (ranking, directions, tree
# centring, grid, pinned nodes, no-overlap property). UI-free.
message(STATUS "  Building ClassLayoutTest...")
add_executable(ClassLayoutTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ClassLayoutTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasClassLayout.cpp
)
target_include_directories(ClassLayoutTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(ClassLayoutTest PRIVATE cxx_std_20)
set_target_properties(ClassLayoutTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME ClassLayoutTest COMMAND ClassLayoutTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: ClassLayoutTest")

# ===== UML MODEL TEST =====
# Unit tests for the UML static-structure model, the member-notation grammar,
# the heuristic C++ reverse engineer and PlantUML/Mermaid export. UI-free.
message(STATUS "  Building UMLModelTest...")
add_executable(UMLModelTest
    ${CMAKE_CURRENT_SOURCE_DIR}/UMLModelTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasUMLModel.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasUMLNotation.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasUMLTextExport.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasCppReverseEngineer.cpp
)
target_include_directories(UMLModelTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(UMLModelTest PRIVATE cxx_std_20)
set_target_properties(UMLModelTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME UMLModelTest COMMAND UMLModelTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: UMLModelTest")

# ===== SEQUENCE MODEL TEST =====
# Unit tests for the sequence diagram model: activation computation, message
# numbering, fragment/note bookkeeping, validation and PlantUML/Mermaid
# export. UI-free.
message(STATUS "  Building SequenceModelTest...")
add_executable(SequenceModelTest
    ${CMAKE_CURRENT_SOURCE_DIR}/SequenceModelTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasSequenceModel.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasSequenceTextExport.cpp
)
target_include_directories(SequenceModelTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(SequenceModelTest PRIVATE cxx_std_20)
set_target_properties(SequenceModelTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME SequenceModelTest COMMAND SequenceModelTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: SequenceModelTest")

# ===== MATRIX MODEL TEST =====
# Unit tests for the matrix diagram model: scale presets and their weights,
# sparse cell storage, the weighted row/column roll-ups, stable column ranking,
# validation, the CSV codec in both directions and the shipped samples. UI-free.
message(STATUS "  Building MatrixModelTest...")
add_executable(MatrixModelTest
    ${CMAKE_CURRENT_SOURCE_DIR}/MatrixModelTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasMatrixModel.cpp
)
target_include_directories(MatrixModelTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(MatrixModelTest PRIVATE cxx_std_20)
set_target_properties(MatrixModelTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME MatrixModelTest COMMAND MatrixModelTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: MatrixModelTest")

# ===== FISHBONE MODEL TEST =====
# Unit tests for the fishbone (Ishikawa) model: category presets, cause and
# sub-cause counting, depth, validation, the indented-outline codec and the
# Mermaid ishikawa-beta codec in both directions. UI-free.
message(STATUS "  Building FishboneModelTest...")
add_executable(FishboneModelTest
    ${CMAKE_CURRENT_SOURCE_DIR}/FishboneModelTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasFishboneModel.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasFishboneText.cpp
)
target_include_directories(FishboneModelTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(FishboneModelTest PRIVATE cxx_std_20)
set_target_properties(FishboneModelTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME FishboneModelTest COMMAND FishboneModelTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: FishboneModelTest")

# ===== REQUIREMENT MODEL TEST =====
# Unit tests for the SysML requirement model: endpoint semantics, containment
# cycle detection, hierarchical id assignment, derived-element compartments,
# coverage analysis, trace chains and the Mermaid/CSV interchange. The model
# is deliberately free of UI dependencies, so only its own source is compiled.
message(STATUS "  Building RequirementModelTests...")
add_executable(RequirementModelTests
    ${CMAKE_CURRENT_SOURCE_DIR}/RequirementModelTests.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Diagrams/UltraCanvasRequirementModel.cpp
)
target_include_directories(RequirementModelTests PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins
)
target_compile_features(RequirementModelTests PRIVATE cxx_std_20)
set_target_properties(RequirementModelTests PROPERTIES
    OUTPUT_NAME "RequirementModelTests"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME RequirementModelTests
    COMMAND RequirementModelTests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: RequirementModelTests")

# ===== VIRTUALFS PATH TEST =====
# Regression test for VirtualFSPath archive-path resolution (Unix, relative
# and Windows drive-letter paths). Header-only - no link dependencies, so it
# runs on every platform regardless of libarchive availability.
message(STATUS "  Building VirtualFSPathTest...")
add_executable(VirtualFSPathTest
    ${CMAKE_CURRENT_SOURCE_DIR}/VirtualFSPathTest.cpp
)
target_include_directories(VirtualFSPathTest PRIVATE
    ${ULTRACANVAS_ROOT}/VirtualFS/include
)
target_compile_features(VirtualFSPathTest PRIVATE cxx_std_17)
set_target_properties(VirtualFSPathTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME VirtualFSPathTest COMMAND VirtualFSPathTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: VirtualFSPathTest")

# ===== VIRTUALFS DELETE TEST =====
# Regression test for batched archive-entry deletion (single rewrite pass).
# Needs libarchive; the vendored miniz additionally enables the ZIP raw-copy
# fast path, matching the flags the VirtualFS module itself is built with.
if(PkgConfig_FOUND)
    pkg_check_modules(LIBARCHIVE_TEST libarchive)
endif()

if(LIBARCHIVE_TEST_FOUND)
    message(STATUS "  Building VirtualFSDeleteTest...")

    add_executable(VirtualFSDeleteTest
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualFSDeleteTest.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSManager.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSCompression.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/providers/VirtualFSLibArchiveProvider.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz/miniz.c
    )
    target_include_directories(VirtualFSDeleteTest PRIVATE
        ${ULTRACANVAS_ROOT}/VirtualFS/include
        ${ULTRACANVAS_ROOT}/VirtualFS/include/VirtualFS
        ${ULTRACANVAS_ROOT}/VirtualFS/providers
        ${ULTRACANVAS_ROOT}/UltraCanvas/third_party/miniz
        ${LIBARCHIVE_TEST_INCLUDE_DIRS}
    )
    target_compile_definitions(VirtualFSDeleteTest PRIVATE
        VIRTUALFS_HAS_LIBARCHIVE
        VIRTUALFS_HAS_MINIZ
    )
    target_link_libraries(VirtualFSDeleteTest PRIVATE
        ${LIBARCHIVE_TEST_LIBRARIES}
        pthread
    )
    target_compile_features(VirtualFSDeleteTest PRIVATE cxx_std_17)
    set_target_properties(VirtualFSDeleteTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME VirtualFSDeleteTest
        COMMAND VirtualFSDeleteTest ${CMAKE_BINARY_DIR}/vfsdelete-test-out
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: VirtualFSDeleteTest")
else()
    message(STATUS "  libarchive not found - VirtualFSDeleteTest disabled")
endif()

# ===== VIRTUALFS NESTED MEMORY TEST =====
# Regression test for memory-backed nested archive traversal: an archive
# inside an archive must be opened straight from its bytes, leaving nothing
# in the temp directory. Reuses the libarchive probe from the delete test.
if(LIBARCHIVE_TEST_FOUND)
    message(STATUS "  Building VirtualFSNestedMemoryTest...")

    add_executable(VirtualFSNestedMemoryTest
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualFSNestedMemoryTest.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSManager.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSCompression.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/providers/VirtualFSLibArchiveProvider.cpp
    )
    target_include_directories(VirtualFSNestedMemoryTest PRIVATE
        ${ULTRACANVAS_ROOT}/VirtualFS/include
        ${ULTRACANVAS_ROOT}/VirtualFS/include/VirtualFS
        ${ULTRACANVAS_ROOT}/VirtualFS/providers
        ${LIBARCHIVE_TEST_INCLUDE_DIRS}
    )
    target_compile_definitions(VirtualFSNestedMemoryTest PRIVATE
        VIRTUALFS_HAS_LIBARCHIVE
    )
    target_link_libraries(VirtualFSNestedMemoryTest PRIVATE
        ${LIBARCHIVE_TEST_LIBRARIES}
        pthread
    )
    target_compile_features(VirtualFSNestedMemoryTest PRIVATE cxx_std_17)
    set_target_properties(VirtualFSNestedMemoryTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME VirtualFSNestedMemoryTest
        COMMAND VirtualFSNestedMemoryTest ${CMAKE_BINARY_DIR}/vfsnested-test-out
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: VirtualFSNestedMemoryTest")
else()
    message(STATUS "  libarchive not found - VirtualFSNestedMemoryTest disabled")
endif()

# ===== VIRTUALFS RAM DISC TEST =====
# OS-visible RAM disc provisioning. Needs no libarchive - the disc is a
# platform mount, not an archive - but does need the platform back end, so
# the source is picked the same way VirtualFS/CMakeLists.txt picks it.
if(WIN32)
    set(VIRTUALFS_RAMDISK_BACKEND ${ULTRACANVAS_ROOT}/VirtualFS/OS/MSWindows/VirtualFSRamDiskPlatform.cpp)
elseif(APPLE)
    set(VIRTUALFS_RAMDISK_BACKEND ${ULTRACANVAS_ROOT}/VirtualFS/OS/MacOS/VirtualFSRamDiskPlatform.cpp)
elseif(UNIX)
    set(VIRTUALFS_RAMDISK_BACKEND ${ULTRACANVAS_ROOT}/VirtualFS/OS/Linux/VirtualFSRamDiskPlatform.cpp)
endif()

if(VIRTUALFS_RAMDISK_BACKEND)
    message(STATUS "  Building VirtualFSRamDiskTest...")

    add_executable(VirtualFSRamDiskTest
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualFSRamDiskTest.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSRamDisk.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSManager.cpp
        ${ULTRACANVAS_ROOT}/VirtualFS/core/VirtualFSCompression.cpp
        ${VIRTUALFS_RAMDISK_BACKEND}
    )
    target_include_directories(VirtualFSRamDiskTest PRIVATE
        ${ULTRACANVAS_ROOT}/VirtualFS/include
        ${ULTRACANVAS_ROOT}/VirtualFS/include/VirtualFS
    )
    target_link_libraries(VirtualFSRamDiskTest PRIVATE pthread)
    target_compile_features(VirtualFSRamDiskTest PRIVATE cxx_std_17)
    set_target_properties(VirtualFSRamDiskTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(
        NAME VirtualFSRamDiskTest
        COMMAND VirtualFSRamDiskTest
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    message(STATUS "    Test registered: VirtualFSRamDiskTest")
else()
    message(STATUS "  No RAM disc back end for this platform - VirtualFSRamDiskTest disabled")
endif()

# ===== LABEL PLACEMENT TEST =====
# Session reuse across passes, the spatial index, rotated labels, polyline
# obstacles, priority, suppression, minimum separation, leader lines, point
# anchors and the 1-D axis tick declutter. Pure geometry - no UI stack, no link
# dependencies beyond the solver itself.
message(STATUS "  Building LabelPlacementTest...")
add_executable(LabelPlacementTest
    ${CMAKE_CURRENT_SOURCE_DIR}/LabelPlacementTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasLabelPlacement.cpp
)
target_include_directories(LabelPlacementTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(LabelPlacementTest PRIVATE cxx_std_20)
set_target_properties(LabelPlacementTest PROPERTIES
    OUTPUT_NAME "LabelPlacementTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME LabelPlacementTest
    COMMAND LabelPlacementTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: LabelPlacementTest")

# ===== TONE CURVE TEST =====
# The tone curve model behind the Curves dialog: point management, monotone
# interpolation, lookup tables and the four-curve set. Pure model code - no UI
# stack, no image library.
message(STATUS "  Building ToneCurveTest...")
add_executable(ToneCurveTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ToneCurveTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasToneCurve.cpp
)
target_include_directories(ToneCurveTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(ToneCurveTest PRIVATE cxx_std_20)
set_target_properties(ToneCurveTest PROPERTIES
    OUTPUT_NAME "ToneCurveTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ToneCurveTest
    COMMAND ToneCurveTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ToneCurveTest")

# ===== CHART ENGINE TEST =====
# The engine's model layer: axis ranges and scales, tick generation and
# formatting, layout negotiation, the vertical / horizontal / polar projections
# and the label policy and plan. Pure model code - no UI stack.
message(STATUS "  Building ChartEngineTest...")
add_executable(ChartEngineTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ChartEngineTest.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/Engine/UltraCanvasChartAxis.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/Engine/UltraCanvasChartProjection.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/Engine/UltraCanvasChartLabels.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/Engine/UltraCanvasChartSeries.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/Engine/UltraCanvasChartTheme.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/Engine/UltraCanvasChartHighlights.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/UltraCanvasParallelAxisModel.cpp
    ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/Charts/UltraCanvasColormap.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasLabelPlacement.cpp
)
target_include_directories(ChartEngineTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
)
target_compile_features(ChartEngineTest PRIVATE cxx_std_20)
set_target_properties(ChartEngineTest PROPERTIES
    OUTPUT_NAME "ChartEngineTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ChartEngineTest
    COMMAND ChartEngineTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ChartEngineTest")

# ===== ELEMENT PLUGIN TEST =====
# The element plugin registry, create-by-name, keyword text dispatch, the named
# property surface, the ABI handshake, and a real end-to-end DSO load: two test
# plugins are built as MODULE libraries and loaded through the host vtable.
# The test supplies its own minimal UltraCanvasUIElement stand-in (see
# ElementPluginTestSupport.h) so no UI stack is needed.
message(STATUS "  Building ElementPluginTest...")
add_library(TestElementPlugin MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/TestElementPlugin.cpp
)
add_library(TestRefusingPlugin MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/TestRefusingPlugin.cpp
)
foreach(plugin TestElementPlugin TestRefusingPlugin)
    target_include_directories(${plugin} PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_features(${plugin} PRIVATE cxx_std_20)
    set_target_properties(${plugin} PROPERTIES
        PREFIX ""
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/TestElements
    )
endforeach()

add_executable(ElementPluginTest
    ${CMAKE_CURRENT_SOURCE_DIR}/ElementPluginTest.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasElementPlugins.cpp
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasPluginLoader.cpp
)
target_include_directories(ElementPluginTest PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(ElementPluginTest PRIVATE cxx_std_20)
target_link_libraries(ElementPluginTest PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(ElementPluginTest TestElementPlugin TestRefusingPlugin)
set_target_properties(ElementPluginTest PROPERTIES
    OUTPUT_NAME "ElementPluginTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME ElementPluginTest
    COMMAND ElementPluginTest ${CMAKE_BINARY_DIR}/Plugins/TestElements
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
message(STATUS "    Test registered: ElementPluginTest")

# ===== EPS PROBE TEST =====
# Parses the shipped media/vector/EPS samples through the EPS plugin and reports
# the interpreter's diagnostics; with cairo it also rasterizes to PNG for
# comparison against a ghostscript rendering of the same file.
if(TARGET UltraCanvasEPSPlugin)
    message(STATUS "  Building EPSProbeTest...")
    add_executable(EPSProbeTest
        ${CMAKE_CURRENT_SOURCE_DIR}/EPSProbeTest.cpp
    )
    target_include_directories(EPSProbeTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
    )
    target_compile_features(EPSProbeTest PRIVATE cxx_std_20)
    target_compile_definitions(EPSProbeTest PRIVATE
        EPS_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/media/vector/EPS"
    )
    target_link_libraries(EPSProbeTest PRIVATE UltraCanvasEPSPlugin UltraCanvas)
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(EPSPROBE_CAIRO cairo)
            if(EPSPROBE_CAIRO_FOUND)
                target_compile_definitions(EPSProbeTest PRIVATE EPSPROBE_HAVE_CAIRO=1)
                target_include_directories(EPSProbeTest PRIVATE ${EPSPROBE_CAIRO_INCLUDE_DIRS})
                target_link_libraries(EPSProbeTest PRIVATE ${EPSPROBE_CAIRO_LIBRARIES})
            endif()
        endif()
    endif()
    set_target_properties(EPSProbeTest PROPERTIES
        OUTPUT_NAME "EPSProbeTest"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME EPSProbeTest COMMAND EPSProbeTest)
    message(STATUS "    Test registered: EPSProbeTest")
endif()

# Round-trips a VectorDocument through the Vector plugin's EPS writer and the
# EPS plugin's PostScript interpreter: DSC header, every operator understood,
# no interpreter warnings, and (with cairo) pixel placement. Needs both
# plugins.
if(TARGET UltraCanvasEPSPlugin AND TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building EPSWriterTest...")
    add_executable(EPSWriterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/EPSWriterTest.cpp
    )
    target_include_directories(EPSWriterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(EPSWriterTest PRIVATE cxx_std_20)
    target_link_libraries(EPSWriterTest PRIVATE
        UltraCanvasVectorPlugin UltraCanvasEPSPlugin UltraCanvas)
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(EPSWRITER_CAIRO cairo)
            if(EPSWRITER_CAIRO_FOUND)
                target_compile_definitions(EPSWriterTest PRIVATE EPSWRITER_HAVE_CAIRO=1)
                target_include_directories(EPSWriterTest PRIVATE ${EPSWRITER_CAIRO_INCLUDE_DIRS})
                target_link_libraries(EPSWriterTest PRIVATE ${EPSWRITER_CAIRO_LIBRARIES})
            endif()
        endif()
    endif()
    set_target_properties(EPSWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME EPSWriterTest COMMAND EPSWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: EPSWriterTest")
else()
    message(STATUS "  EPSWriterTest skipped (needs UltraCanvasEPSPlugin and UltraCanvasVectorPlugin)")
endif()

# Round-trips a VectorDocument through the Vector plugin's CDR writer and the
# CDR plugin (libcdr): the file must parse, carry the right page, and render
# the objects at their places. Needs both plugins.
if(TARGET UltraCanvasCDRPlugin AND TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building CDRWriterTest...")
    add_executable(CDRWriterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/CDRWriterTest.cpp
    )
    target_include_directories(CDRWriterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(CDRWriterTest PRIVATE cxx_std_20)
    target_link_libraries(CDRWriterTest PRIVATE
        UltraCanvasVectorPlugin UltraCanvasCDRPlugin UltraCanvas)
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(CDRWRITER_CAIRO cairo)
            if(CDRWRITER_CAIRO_FOUND)
                target_compile_definitions(CDRWriterTest PRIVATE CDRWRITER_HAVE_CAIRO=1)
                target_include_directories(CDRWriterTest PRIVATE ${CDRWRITER_CAIRO_INCLUDE_DIRS})
                target_link_libraries(CDRWriterTest PRIVATE ${CDRWRITER_CAIRO_LIBRARIES})
            endif()
        endif()
    endif()
    set_target_properties(CDRWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME CDRWriterTest COMMAND CDRWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: CDRWriterTest")
else()
    message(STATUS "  CDRWriterTest skipped (needs UltraCanvasCDRPlugin and UltraCanvasVectorPlugin)")
endif()

# Round-trips a VectorDocument through the Vector plugin's SVG converter in
# both directions (export -> import, structural comparison), rasterizes the
# export through the framework's real SVG pipeline, and parses a hand-written
# snippet for importer robustness.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building SVGConverterTest...")
    add_executable(SVGConverterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SVGConverterTest.cpp
    )
    target_include_directories(SVGConverterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(SVGConverterTest PRIVATE cxx_std_20)
    target_link_libraries(SVGConverterTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(SVGConverterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME SVGConverterTest COMMAND SVGConverterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: SVGConverterTest")
else()
    message(STATUS "  SVGConverterTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# Validates the Vector plugin's PDF writer: structural checks (an xref table
# whose offsets really point at their objects, resources, operators) plus a
# ghostscript rasterization with pixel checks when gs is installed.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building PDFVectorWriterTest...")
    add_executable(PDFVectorWriterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/PDFVectorWriterTest.cpp
    )
    target_include_directories(PDFVectorWriterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(PDFVectorWriterTest PRIVATE cxx_std_20)
    target_link_libraries(PDFVectorWriterTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(PDFVectorWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME PDFVectorWriterTest COMMAND PDFVectorWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: PDFVectorWriterTest")
else()
    message(STATUS "  PDFVectorWriterTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# Validates the Vector plugin's EMF, WMF and AI writers: structural record
# walks, plus rasterization through LibreOffice (EMF/WMF) and ghostscript
# (AI) with pixel checks when those tools are installed.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building MetafileWriterTest...")
    add_executable(MetafileWriterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/MetafileWriterTest.cpp
    )
    target_include_directories(MetafileWriterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(MetafileWriterTest PRIVATE cxx_std_20)
    target_link_libraries(MetafileWriterTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(MetafileWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME MetafileWriterTest COMMAND MetafileWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: MetafileWriterTest")
else()
    message(STATUS "  MetafileWriterTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# Validates the Vector plugin's DXF and DWG writers: structural checks, then
# ezdxf (strict read + audit + structure), LibreOffice rasterization, and -
# when LibreDWG's dxf2dwg is available - the real DWG conversion round-trip.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building CADWriterTest...")
    add_executable(CADWriterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/CADWriterTest.cpp
    )
    target_include_directories(CADWriterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(CADWriterTest PRIVATE cxx_std_20)
    target_link_libraries(CADWriterTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(CADWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME CADWriterTest COMMAND CADWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: CADWriterTest")
else()
    message(STATUS "  CADWriterTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# ===== VECTOR MODEL TEST =====
# The shared vector document model: Matrix3x3 at double precision, group
# and document bounds through nested transforms, and hit-testing that
# carries the point through ancestor transforms.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building VectorModelTest...")
    add_executable(VectorModelTest
        ${CMAKE_CURRENT_SOURCE_DIR}/VectorModelTest.cpp
    )
    target_include_directories(VectorModelTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(VectorModelTest PRIVATE cxx_std_20)
    target_link_libraries(VectorModelTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(VectorModelTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME VectorModelTest COMMAND VectorModelTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: VectorModelTest")
else()
    message(STATUS "  VectorModelTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# ===== VECTOR FORMATS PLUGIN TEST =====
# The whole converter matrix through the graphics plugin registry: saving
# all ten formats via SaveGraphicsFile, loading the formats with readers
# via LoadGraphicsFile, round-trip structure checks, and the supported-
# format inventory's per-extension load/save capabilities.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building VectorFormatsPluginTest...")
    add_executable(VectorFormatsPluginTest
        ${CMAKE_CURRENT_SOURCE_DIR}/VectorFormatsPluginTest.cpp
    )
    target_include_directories(VectorFormatsPluginTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(VectorFormatsPluginTest PRIVATE cxx_std_20)
    target_link_libraries(VectorFormatsPluginTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(VectorFormatsPluginTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME VectorFormatsPluginTest COMMAND VectorFormatsPluginTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: VectorFormatsPluginTest")
else()
    message(STATUS "  VectorFormatsPluginTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# ===== DWG READER TEST =====
# The CAD import path: the DXF reader's block machinery (BLOCKS, nested/
# arrayed/mirrored INSERTs, DIMENSION blocks, visibility, geometry-derived
# extents) on a synthetic drawing, and the native DWG decoder on the R2000
# fixture in DataFormats. Extra DWG files on the command line are decoded
# and reported (DWG_TEST_SVG_DIR exports them as SVG).
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building DWGReaderTest...")
    add_executable(DWGReaderTest
        ${CMAKE_CURRENT_SOURCE_DIR}/DWGReaderTest.cpp
    )
    target_include_directories(DWGReaderTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(DWGReaderTest PRIVATE cxx_std_20)
    target_compile_definitions(DWGReaderTest PRIVATE
        DWG_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/DataFormats"
    )
    target_link_libraries(DWGReaderTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(DWGReaderTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME DWGReaderTest COMMAND DWGReaderTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: DWGReaderTest")
else()
    message(STATUS "  DWGReaderTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# ===== FUTURE TESTS =====
# Add more test executables here as needed:
#
# ===== XAR PROBE TEST =====
# Parses the repo's XAR samples through the XAR plugin's XARDocument and
# reports node-tree statistics, unhandled record tags and parse warnings —
# the triage tool for XAR files that display wrong. Needs the plugin (and
# through it the framework), so it only builds when the plugin does.
if(TARGET UltraCanvasXARPlugin)
    message(STATUS "  Building XARProbeTest...")
    add_executable(XARProbeTest
        ${CMAKE_CURRENT_SOURCE_DIR}/XARProbeTest.cpp
    )
    target_include_directories(XARProbeTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(XARProbeTest PRIVATE cxx_std_20)
    target_link_libraries(XARProbeTest PRIVATE UltraCanvasXARPlugin UltraCanvas)
    target_compile_definitions(XARProbeTest PRIVATE
        XAR_SAMPLES_DIR="${ULTRACANVAS_ROOT}/media/vector/XAR"
    )
    # --render mode writes PNGs through cairo; probing works without it.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XARPROBE_CAIRO QUIET cairo)
    endif()
    if(XARPROBE_CAIRO_FOUND)
        target_compile_definitions(XARProbeTest PRIVATE XARPROBE_HAVE_CAIRO)
        target_include_directories(XARProbeTest PRIVATE ${XARPROBE_CAIRO_INCLUDE_DIRS})
        target_link_libraries(XARProbeTest PRIVATE ${XARPROBE_CAIRO_LIBRARIES})
    endif()
    set_target_properties(XARProbeTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME XARProbeTest COMMAND XARProbeTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: XARProbeTest")
else()
    message(STATUS "  XARProbeTest skipped (UltraCanvasXARPlugin target not present)")
endif()

# Round-trips a VectorDocument through the Vector plugin's XAR writer and the
# XAR plugin's spec-verified reader: page size, node counts, millipoint
# placement (Y flip), colour references, text spans. Needs both plugins.
if(TARGET UltraCanvasXARPlugin AND TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building XARWriterTest...")
    add_executable(XARWriterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/XARWriterTest.cpp
    )
    target_include_directories(XARWriterTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(XARWriterTest PRIVATE cxx_std_20)
    target_link_libraries(XARWriterTest PRIVATE
        UltraCanvasVectorPlugin UltraCanvasXARPlugin UltraCanvas)
    set_target_properties(XARWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME XARWriterTest COMMAND XARWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: XARWriterTest")
else()
    message(STATUS "  XARWriterTest skipped (needs UltraCanvasXARPlugin and UltraCanvasVectorPlugin)")
endif()

# Example:
# add_executable(AnotherTest
#     ${CMAKE_CURRENT_SOURCE_DIR}/AnotherTest.cpp
#     ${ULTRACANVAS_CORE_DIR}/SomeSource.cpp
# )
# target_include_directories(AnotherTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
# target_compile_features(AnotherTest PRIVATE cxx_std_20)
# add_test(NAME AnotherTest COMMAND AnotherTest)

message(STATUS "")

# ===== EMBEDDED PREVIEW TEST =====
# The preview bitmap Xara and ZIP-based CorelDRAW documents carry inside
# themselves - what the filer thumbnails them from - plus the image
# pipeline's own "can this extension load" answer, whose collapse to "no"
# is what silently strips every thumbnail in a folder.
if(TARGET UltraCanvas)
    message(STATUS "  Building EmbeddedPreviewTest...")
    add_executable(EmbeddedPreviewTest
        ${CMAKE_CURRENT_SOURCE_DIR}/EmbeddedPreviewTest.cpp
    )
    target_include_directories(EmbeddedPreviewTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(EmbeddedPreviewTest PRIVATE cxx_std_20)
    target_compile_definitions(EmbeddedPreviewTest PRIVATE
        UC_MEDIA_DIR="${ULTRACANVAS_ROOT}/media")
    target_link_libraries(EmbeddedPreviewTest PRIVATE UltraCanvas)
    set_target_properties(EmbeddedPreviewTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME EmbeddedPreviewTest COMMAND EmbeddedPreviewTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: EmbeddedPreviewTest")
else()
    message(STATUS "  EmbeddedPreviewTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== FILER VOLUME SPACE TEST =====
# The Computer page's drive cards: the used share the pie chart is cut by
# and the "free of" line under it (Apps/UltraFiler/UltraFilerVolumeSpace.h).
# Header-only, so the test needs no library - it runs on any build.
message(STATUS "  Building FilerVolumeSpaceTest...")
add_executable(FilerVolumeSpaceTest
    ${CMAKE_CURRENT_SOURCE_DIR}/FilerVolumeSpaceTest.cpp
)
target_include_directories(FilerVolumeSpaceTest PRIVATE
    ${ULTRACANVAS_ROOT}/Apps/UltraFiler
)
target_compile_features(FilerVolumeSpaceTest PRIVATE cxx_std_20)
set_target_properties(FilerVolumeSpaceTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME FilerVolumeSpaceTest COMMAND FilerVolumeSpaceTest
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "    Test registered: FilerVolumeSpaceTest")

message(STATUS "")

# ===== FILER FORMAT LIST TEST =====
# The list of files behind Display > Thumbnails and Display > Detail view has
# to hold every format the FileLoader inventory reports for this build, filed
# under the preview kind of its media category - a format the file manager can
# open but cannot list is one whose thumbnail nobody can switch on.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerFormatListTest...")
    add_executable(FilerFormatListTest
        ${CMAKE_CURRENT_SOURCE_DIR}/FilerFormatListTest.cpp
    )
    target_include_directories(FilerFormatListTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(FilerFormatListTest PRIVATE cxx_std_20)
    target_link_libraries(FilerFormatListTest PRIVATE UltraCanvas)
    set_target_properties(FilerFormatListTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME FilerFormatListTest COMMAND FilerFormatListTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: FilerFormatListTest")
else()
    message(STATUS "  FilerFormatListTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== FILER FILE-EXTENSION DISPLAY TEST =====
# Display > File extensions: the name the file display draws for an entry and
# the type tag its thumbnail tiles carry. Hiding an extension must never
# invent a file type out of a version number or an architecture tag.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerExtensionDisplayTest...")
    add_executable(FilerExtensionDisplayTest
        ${CMAKE_CURRENT_SOURCE_DIR}/FilerExtensionDisplayTest.cpp
    )
    target_include_directories(FilerExtensionDisplayTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(FilerExtensionDisplayTest PRIVATE cxx_std_20)
    target_link_libraries(FilerExtensionDisplayTest PRIVATE UltraCanvas)
    set_target_properties(FilerExtensionDisplayTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME FilerExtensionDisplayTest COMMAND FilerExtensionDisplayTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: FilerExtensionDisplayTest")
else()
    message(STATUS "  FilerExtensionDisplayTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== SHELL LINK TEST =====
# Reading a Windows shortcut (.lnk) on any platform: what it points at, the
# icon it names, and how its Windows path maps onto the host filesystem. The
# links are built byte by byte inside the test, so it needs no fixtures.
if(TARGET UltraCanvas)
    message(STATUS "  Building ShellLinkTest...")
    add_executable(ShellLinkTest
        ${CMAKE_CURRENT_SOURCE_DIR}/ShellLinkTest.cpp
    )
    target_include_directories(ShellLinkTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(ShellLinkTest PRIVATE cxx_std_20)
    target_link_libraries(ShellLinkTest PRIVATE UltraCanvas)
    set_target_properties(ShellLinkTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME ShellLinkTest COMMAND ShellLinkTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: ShellLinkTest")
else()
    message(STATUS "  ShellLinkTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== DESKTOP ENTRY TEST =====
# The freedesktop desktop-entry reader: what a .desktop says, the icon its
# Icon= name resolves to in the installed themes, and the argument vector its
# Exec= expands into. Builds a throwaway icon theme and points XDG_DATA_HOME
# at it, so it never depends on what the machine has installed.
if(TARGET UltraCanvas)
    message(STATUS "  Building DesktopEntryTest...")
    add_executable(DesktopEntryTest
        ${CMAKE_CURRENT_SOURCE_DIR}/DesktopEntryTest.cpp
    )
    target_include_directories(DesktopEntryTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(DesktopEntryTest PRIVATE cxx_std_20)
    target_link_libraries(DesktopEntryTest PRIVATE UltraCanvas)
    set_target_properties(DesktopEntryTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME DesktopEntryTest COMMAND DesktopEntryTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: DesktopEntryTest")
else()
    message(STATUS "  DesktopEntryTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== MAC BUNDLE TEST =====
# The property-list reader (both encodings) and the macOS application bundles
# and web locations built on it. Builds a bundle directory in a temp folder,
# so it runs — and has to pass — on every platform, not only on macOS.
if(TARGET UltraCanvas)
    message(STATUS "  Building MacBundleTest...")
    add_executable(MacBundleTest
        ${CMAKE_CURRENT_SOURCE_DIR}/MacBundleTest.cpp
    )
    target_include_directories(MacBundleTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(MacBundleTest PRIVATE cxx_std_20)
    target_link_libraries(MacBundleTest PRIVATE UltraCanvas)
    set_target_properties(MacBundleTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME MacBundleTest COMMAND MacBundleTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: MacBundleTest")
else()
    message(STATUS "  MacBundleTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== FILER SHORTCUT ENTRY TEST =====
# What the file display makes of a .lnk it lists: type, category, info column
# and the resolved target on the entry. Builds its own shortcuts (and its own
# Wine-prefix-shaped tree) inside the test.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerShortcutEntryTest...")
    add_executable(FilerShortcutEntryTest
        ${CMAKE_CURRENT_SOURCE_DIR}/FilerShortcutEntryTest.cpp
    )
    target_include_directories(FilerShortcutEntryTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(FilerShortcutEntryTest PRIVATE cxx_std_20)
    target_link_libraries(FilerShortcutEntryTest PRIVATE UltraCanvas)
    set_target_properties(FilerShortcutEntryTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME FilerShortcutEntryTest COMMAND FilerShortcutEntryTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: FilerShortcutEntryTest")
else()
    message(STATUS "  FilerShortcutEntryTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== ICON RESOURCE TEST =====
# The portable icon reader: an .ico file and the RT_GROUP_ICON / RT_ICON
# resources of a PE binary, both assembled inside the test rather than
# checked in as binaries.
if(TARGET UltraCanvas)
    message(STATUS "  Building IconResourceTest...")
    add_executable(IconResourceTest
        ${CMAKE_CURRENT_SOURCE_DIR}/IconResourceTest.cpp
    )
    target_include_directories(IconResourceTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(IconResourceTest PRIVATE cxx_std_20)
    target_link_libraries(IconResourceTest PRIVATE UltraCanvas)
    set_target_properties(IconResourceTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME IconResourceTest COMMAND IconResourceTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: IconResourceTest")
else()
    message(STATUS "  IconResourceTest skipped (UltraCanvas target not present)")
endif()

message(STATUS "")

# ===== FONT FILE TEST =====
# Reading a font definition file as a document: its name records, and the
# line of its own glyphs the filer thumbnails it with. Runs against the
# framework's own bundled Ubuntu faces, so it needs no installed font.
if(TARGET UltraCanvas)
    message(STATUS "  Building FontFileTest...")
    add_executable(FontFileTest
        ${CMAKE_CURRENT_SOURCE_DIR}/FontFileTest.cpp
    )
    target_include_directories(FontFileTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(FontFileTest PRIVATE cxx_std_20)
    target_compile_definitions(FontFileTest PRIVATE
        UC_MEDIA_DIR="${ULTRACANVAS_ROOT}/media")
    target_link_libraries(FontFileTest PRIVATE UltraCanvas)
    set_target_properties(FontFileTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME FontFileTest COMMAND FontFileTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: FontFileTest")
else()
    message(STATUS "  FontFileTest skipped (UltraCanvas target not present)")
endif()

# ===== MATH FONT TEST =====
# The OpenType math font reader behind the native LaTeX engine (Phase 0 of
# Docs/UltraCanvas/UltraCanvasLaTeXEngineProposal.md), checked against the
# .clm2 the vendored MicroTeX engine reads for the same font - so the test
# links microtex_core as its oracle and needs the LaTeX plugin enabled.
if(TARGET UltraCanvas AND TARGET microtex_core)
    message(STATUS "  Building MathFontTest...")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(MATHFONT_FREETYPE QUIET freetype2)
    endif()
    add_executable(MathFontTest
        ${CMAKE_CURRENT_SOURCE_DIR}/MathFontTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/LaTeX/UltraCanvasMathFont.cpp
    )
    target_include_directories(MathFontTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${MATHFONT_FREETYPE_INCLUDE_DIRS})
    target_compile_features(MathFontTest PRIVATE cxx_std_20)
    target_compile_definitions(MathFontTest PRIVATE
        UC_MEDIA_DIR="${ULTRACANVAS_ROOT}/media")
    target_link_directories(MathFontTest PRIVATE ${MATHFONT_FREETYPE_LIBRARY_DIRS})
    target_link_libraries(MathFontTest PRIVATE UltraCanvas microtex_core ${MATHFONT_FREETYPE_LIBRARIES})
    set_target_properties(MathFontTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME MathFontTest COMMAND MathFontTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: MathFontTest")
else()
    message(STATUS "  MathFontTest skipped (UltraCanvas or microtex_core target not present)")
endif()

# ===== MATH ENGINE TEST =====
# The native math engine (parser, layout, facade) with the vendored MicroTeX
# as its oracle - Phase 1 of Docs/UltraCanvas/UltraCanvasLaTeXEngineProposal.md.
if(TARGET UltraCanvas AND TARGET microtex_core)
    message(STATUS "  Building MathEngineTest...")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(MATHENGINE_FREETYPE QUIET freetype2)
    endif()
    add_executable(MathEngineTest
        ${CMAKE_CURRENT_SOURCE_DIR}/MathEngineTest.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/LaTeX/UltraCanvasMathFont.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/LaTeX/UltraCanvasMathSymbols.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/LaTeX/UltraCanvasMathParser.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/LaTeX/UltraCanvasMathLayout.cpp
        ${ULTRACANVAS_ROOT}/UltraCanvas/Plugins/LaTeX/UltraCanvasMathEngine.cpp
    )
    target_include_directories(MathEngineTest PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
        ${MATHENGINE_FREETYPE_INCLUDE_DIRS})
    target_compile_features(MathEngineTest PRIVATE cxx_std_20)
    target_compile_definitions(MathEngineTest PRIVATE
        UC_MEDIA_DIR="${ULTRACANVAS_ROOT}/media")
    target_link_directories(MathEngineTest PRIVATE ${MATHENGINE_FREETYPE_LIBRARY_DIRS})
    target_link_libraries(MathEngineTest PRIVATE UltraCanvas microtex_core ${MATHENGINE_FREETYPE_LIBRARIES})
    set_target_properties(MathEngineTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME MathEngineTest COMMAND MathEngineTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: MathEngineTest")
else()
    message(STATUS "  MathEngineTest skipped (UltraCanvas or microtex_core target not present)")
endif()

# ===== INLINE MATH TEST =====
# The core's handle on the LaTeX module for formulas inside text (Phase 2 of
# Docs/UltraCanvas/UltraCanvasLaTeXEngineProposal.md): the module is loaded
# with dlopen from <build>/lib, so the module target must be built first.
if(TARGET UltraCanvas AND TARGET UltraCanvasLaTeX)
    message(STATUS "  Building InlineMathTest...")
    add_executable(InlineMathTest ${CMAKE_CURRENT_SOURCE_DIR}/InlineMathTest.cpp)
    target_include_directories(InlineMathTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(InlineMathTest PRIVATE cxx_std_20)
    target_compile_definitions(InlineMathTest PRIVATE UC_MEDIA_DIR="${ULTRACANVAS_ROOT}/media")
    target_link_libraries(InlineMathTest PRIVATE UltraCanvas)
    add_dependencies(InlineMathTest UltraCanvasLaTeX)
    # The module resolves the core's symbols against the executable that
    # dlopens it, so the executable has to export them - the same
    # ENABLE_EXPORTS every other module host sets (DemoApp, EmailCleaner,
    # UltraCleaner, UltraMail, UltraNetTests). Without it the module loads with
    # "undefined symbol: typeinfo for UltraCanvas::UltraCanvasUIElement" and
    # the test reports the module as unavailable.
    set_target_properties(InlineMathTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        ENABLE_EXPORTS ON)
    add_test(NAME InlineMathTest COMMAND InlineMathTest WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: InlineMathTest")
else()
    message(STATUS "  InlineMathTest skipped (UltraCanvas or UltraCanvasLaTeX target not present)")
endif()

# ===== FONT VIEWER TEST =====
# The glyph browser's geometry - columns, rows, scroll range, hit testing.
# The painting needs a window; the layout does not, and it is where the bugs
# that hide part of a font live.
if(TARGET UltraCanvas)
    message(STATUS "  Building FontViewerTest...")
    add_executable(FontViewerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/FontViewerTest.cpp
    )
    target_include_directories(FontViewerTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(FontViewerTest PRIVATE cxx_std_20)
    target_compile_definitions(FontViewerTest PRIVATE
        UC_MEDIA_DIR="${ULTRACANVAS_ROOT}/media")
    target_link_libraries(FontViewerTest PRIVATE UltraCanvas)
    set_target_properties(FontViewerTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME FontViewerTest COMMAND FontViewerTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: FontViewerTest")
else()
    message(STATUS "  FontViewerTest skipped (UltraCanvas target not present)")
endif()

# ===== OFFSCREEN RENDER TEST =====
# Widgets drawn into a surface with no window behind it. Everything a widget
# needs at paint time has to come from the context it was handed, and a widget
# that cannot get it must draw less rather than crash.
if(TARGET UltraCanvas)
    message(STATUS "  Building OffscreenRenderTest...")
    add_executable(OffscreenRenderTest
        ${CMAKE_CURRENT_SOURCE_DIR}/OffscreenRenderTest.cpp
    )
    target_include_directories(OffscreenRenderTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(OffscreenRenderTest PRIVATE cxx_std_20)
    target_link_libraries(OffscreenRenderTest PRIVATE UltraCanvas)
    set_target_properties(OffscreenRenderTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME OffscreenRenderTest COMMAND OffscreenRenderTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: OffscreenRenderTest")
else()
    message(STATUS "  OffscreenRenderTest skipped (UltraCanvas target not present)")
endif()
