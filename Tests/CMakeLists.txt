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