# Tests/UltraWin/CMakeLists.txt
# Builds the UltraWinTests binary. Enabled by ULTRACANVAS_BUILD_ULTRAWIN_TESTS
# (Linux only — the UltraWin target itself is Linux-only). Wine is NOT
# required: wine-dependent tests skip themselves when no binary is found.

cmake_minimum_required(VERSION 3.16)

set(ULTRAWIN_TEST_SOURCES
    test_main.cpp
    test_validation.cpp
    test_manifest.cpp
    test_lifecycle.cpp
    test_supervision.cpp
    test_components.cpp
)

add_executable(UltraWinTests ${ULTRAWIN_TEST_SOURCES})
target_compile_features(UltraWinTests PRIVATE cxx_std_20)

# The suite exercises the public API plus the pure internals
# (ultrawin_internal::*), so the core directory goes on the include path.
target_include_directories(UltraWinTests PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/core/UltraWin)
target_link_libraries(UltraWinTests PRIVATE UltraWin)

add_test(NAME UltraWinTests COMMAND UltraWinTests)
