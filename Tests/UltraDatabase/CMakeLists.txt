# Tests/UltraDatabase/CMakeLists.txt
# Builds the UltraDatabaseTests binary. Enabled by ULTRACANVAS_BUILD_DATABASE_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRADATABASE_TEST_SOURCES
    test_main.cpp
    test_ultradatabase.cpp
)

add_executable(UltraDatabaseTests ${ULTRADATABASE_TEST_SOURCES})

target_compile_features(UltraDatabaseTests PRIVATE cxx_std_20)
target_link_libraries(UltraDatabaseTests PRIVATE UltraDatabase)
target_include_directories(UltraDatabaseTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(CTest)
add_test(NAME UltraDatabase COMMAND UltraDatabaseTests)
