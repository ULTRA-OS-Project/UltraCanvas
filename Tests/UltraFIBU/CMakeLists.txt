# Tests/UltraFIBU/CMakeLists.txt
# Builds UltraFIBUEngineTests. Enabled by ULTRACANVAS_BUILD_ULTRAFIBU_TESTS.
#
# The suite compiles the engine sources - plus UltraCanvasJSON and its vendored
# parser - directly, exactly as Tests/DataFormats/JSONTests does, instead of
# linking the UltraFIBUEngine archive. That is deliberate: it keeps the tests
# runnable where the UI library cannot be built (no pango, no vips), which is
# the common case on a CI runner and on a contributor's machine that only wants
# to check the bookkeeping logic. The engine is headless by design and this is
# what proves it.

cmake_minimum_required(VERSION 3.16)

# yyjson is C, and the top-level project() declares CXX only. This directory is
# processed before the JSONTests block that enables C further down the root
# CMakeLists, so it has to enable it here too - otherwise CMake emits a rule
# for yyjson.c.o with no compile command in it, and the link then fails on an
# object that was announced but never built.
enable_language(C)

set(ULTRAFIBU_TEST_SOURCES
    UltraFIBUEngineTests.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUDate.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUGeschaeftsjahr.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUTypes.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUUstIdNr.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUUstIdNrOnline.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUKontenrahmen.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine/UltraFIBUStore.cpp
    ${CMAKE_SOURCE_DIR}/UltraCanvas/core/DataFormats/UltraCanvasJSON.cpp
    ${CMAKE_SOURCE_DIR}/UltraCanvas/third_party/yyjson/yyjson.c
)

add_executable(UltraFIBUEngineTests ${ULTRAFIBU_TEST_SOURCES})
target_compile_features(UltraFIBUEngineTests PRIVATE cxx_std_20)
target_include_directories(UltraFIBUEngineTests PRIVATE
    ${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/engine
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${CMAKE_SOURCE_DIR}/UltraCanvas/third_party/yyjson)
target_link_libraries(UltraFIBUEngineTests PRIVATE UltraDatabase)
if(TARGET UltraCrypt)
    target_link_libraries(UltraFIBUEngineTests PRIVATE UltraCrypt)
endif()
set_target_properties(UltraFIBUEngineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(CTest)
add_test(NAME UltraFIBUEngine COMMAND UltraFIBUEngineTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The data-file tests read the shipped chart of accounts and tax keys. Pointing
# them at the source tree means they run before anything has been installed,
# and they say SKIP rather than fail if the files are ever missing.
set_tests_properties(UltraFIBUEngine PROPERTIES
    ENVIRONMENT "ULTRAFIBU_DATA_DIR=${CMAKE_SOURCE_DIR}/Apps/UltraFIBU/data")
