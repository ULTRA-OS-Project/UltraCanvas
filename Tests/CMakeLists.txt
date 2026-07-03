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
message(STATUS "  Building UCStringTest...")

# Source files for UCString (standalone - doesn't need full UltraCanvas)
set(UCSTRING_SOURCES
    ${ULTRACANVAS_CORE_DIR}/UltraCanvasString.cpp
)

# Check if UltraCanvasString.cpp exists, if not use local copy
if(NOT EXISTS "${ULTRACANVAS_CORE_DIR}/UltraCanvasString.cpp")
    message(STATUS "    Note: UltraCanvasString.cpp not in core/, checking Tests/")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasString.cpp")
        set(UCSTRING_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasString.cpp)
    else()
        message(FATAL_ERROR "UltraCanvasString.cpp not found!")
    endif()
endif()

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

# ===== ENABLE CTEST =====
enable_testing()

# Add UCStringTest as a CTest
add_test(
    NAME UCStringTest
    COMMAND UCStringTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "    Test registered: UCStringTest")

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