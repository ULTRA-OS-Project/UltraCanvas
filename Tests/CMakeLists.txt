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
    # Trim() lives in UltraCanvasUtils.cpp; without it this target fails to
    # link, which is why BUILD_TESTS could not be switched on.
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasUtils.cpp
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
    # Trim() lives in UltraCanvasUtils.cpp; see HTMLReaderTest above.
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasUtils.cpp
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
        ${ULTRACANVAS_SRC_DIR}/third_party/miniz/miniz.c
        # Trim() lives in UltraCanvasUtils.cpp; see HTMLReaderTest above.
        ${ULTRACANVAS_SRC_DIR}/core/UltraCanvasUtils.cpp
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
# Parses the shipped media/eps samples through the EPS plugin and reports
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
        EPS_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/media/eps"
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
        XAR_SAMPLES_DIR="${ULTRACANVAS_ROOT}/media/xar"
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