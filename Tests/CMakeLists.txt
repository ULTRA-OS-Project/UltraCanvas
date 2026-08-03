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

# ===== FUTURE TESTS =====
# Add more test executables here as needed:
#
# Example:
# add_executable(AnotherTest
#     ${CMAKE_CURRENT_SOURCE_DIR}/AnotherTest.cpp
#     ${ULTRACANVAS_CORE_DIR}/SomeSource.cpp
# )
# target_include_directories(AnotherTest PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
# target_compile_features(AnotherTest PRIVATE cxx_std_20)
# add_test(NAME AnotherTest COMMAND AnotherTest)

message(STATUS "")