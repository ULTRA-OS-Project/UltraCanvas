# Tests/Fuzz/CMakeLists.txt
# Fuzzing the HTMLReader's parser and CSS (Tests/Fuzz/README.md).
#
# Two ways to build it:
#   - from Tests/CMakeLists.txt (BUILD_TESTS): the smoke runs, registered in
#     ctest, on the shared HTMLReaderTestCore objects - any compiler;
#   - on its own, for fuzzing without the rest of the project's dependencies:
#       cmake -S Tests/Fuzz -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ \
#             -DULTRACANVAS_BUILD_FUZZERS=ON
#     (the CI workflow html-fuzz.yml does this).
# ULTRACANVAS_BUILD_FUZZERS adds the libFuzzer targets, with AddressSanitizer
# and UndefinedBehaviorSanitizer, compiled from the sources again so the
# reader itself is instrumented for coverage.
# Version: 1.0.0
# Last Modified: 2026-10-08
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

if(NOT TARGET HTMLReaderTestCore)
    project(HTMLReaderFuzz CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    get_filename_component(ULTRACANVAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(ULTRACANVAS_INCLUDE_DIR "${ULTRACANVAS_ROOT}/UltraCanvas/include")
    set(ULTRACANVAS_CORE_DIR "${ULTRACANVAS_ROOT}/UltraCanvas/core")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../HTMLReaderCoreSources.cmake)
    add_library(HTMLReaderTestCore OBJECT ${HTMLREADER_CORE_SOURCES})
    target_include_directories(HTMLReaderTestCore PUBLIC ${ULTRACANVAS_INCLUDE_DIR})
    target_compile_features(HTMLReaderTestCore PUBLIC cxx_std_20)
    enable_testing()
endif()

option(ULTRACANVAS_BUILD_FUZZERS "Build the HTMLReader libFuzzer targets (clang)" OFF)

set(FUZZ_DIR ${CMAKE_CURRENT_SOURCE_DIR})
# Mutations per smoke run: a few seconds in a debug build.
set(ULTRACANVAS_FUZZ_SMOKE_RUNS 3000 CACHE STRING "Mutations per fuzz smoke test")

# name: target stem; source: the LLVMFuzzerTestOneInput file; corpus: the
# seed directory and dictionary under Tests/Fuzz.
function(ultracanvas_add_fuzz_target name source corpus)
    add_executable(${name}FuzzSmoke ${FUZZ_DIR}/${source} ${FUZZ_DIR}/FuzzSmokeMain.cpp)
    target_link_libraries(${name}FuzzSmoke PRIVATE HTMLReaderTestCore)
    target_include_directories(${name}FuzzSmoke PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
    set_target_properties(${name}FuzzSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME ${name}FuzzSmoke
             COMMAND ${name}FuzzSmoke -runs=${ULTRACANVAS_FUZZ_SMOKE_RUNS}
                     -dict=${FUZZ_DIR}/${corpus}.dict ${FUZZ_DIR}/corpus/${corpus}
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set_tests_properties(${name}FuzzSmoke PROPERTIES TIMEOUT 300)
    message(STATUS "    Test registered: ${name}FuzzSmoke")

    if(ULTRACANVAS_BUILD_FUZZERS)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "ULTRACANVAS_BUILD_FUZZERS needs clang (libFuzzer)")
        endif()
        add_executable(${name}Fuzzer ${FUZZ_DIR}/${source} ${HTMLREADER_CORE_SOURCES})
        target_include_directories(${name}Fuzzer PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
        target_compile_features(${name}Fuzzer PRIVATE cxx_std_20)
        target_compile_options(${name}Fuzzer PRIVATE
            -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined
            -fno-sanitize-recover=undefined)
        target_link_options(${name}Fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
        set_target_properties(${name}Fuzzer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()
endfunction()

ultracanvas_add_fuzz_target(HTMLParser HTMLParserFuzz.cpp html)
ultracanvas_add_fuzz_target(CSS CSSFuzz.cpp css)
