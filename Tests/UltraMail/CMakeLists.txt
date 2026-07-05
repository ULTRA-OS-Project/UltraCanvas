# Tests/UltraMail/CMakeLists.txt
# Builds the UltraMailEngineTests binary. Enabled by ULTRACANVAS_BUILD_ULTRAMAIL_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRAMAIL_TEST_SOURCES
    test_main.cpp
    test_localstore.cpp
)

add_executable(UltraMailEngineTests ${ULTRAMAIL_TEST_SOURCES})

target_compile_features(UltraMailEngineTests PRIVATE cxx_std_20)
target_link_libraries(UltraMailEngineTests PRIVATE UltraMailEngine)
target_include_directories(UltraMailEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(CTest)
add_test(NAME UltraMailEngine COMMAND UltraMailEngineTests)
