# Tests/UltraDatabase/CMakeLists.txt
# Builds the UltraDatabaseTests binary. Enabled by ULTRACANVAS_BUILD_DATABASE_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRADATABASE_TEST_SOURCES
    test_main.cpp
    test_ultradatabase.cpp
    test_numbers.cpp
    test_schema.cpp
    test_batch_cursor.cpp
    test_lifetime.cpp
    test_observer.cpp
)

add_executable(UltraDatabaseTests ${ULTRADATABASE_TEST_SOURCES})

target_compile_features(UltraDatabaseTests PRIVATE cxx_std_20)
target_link_libraries(UltraDatabaseTests PRIVATE UltraDatabase)
target_include_directories(UltraDatabaseTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(CTest)
add_test(NAME UltraDatabase COMMAND UltraDatabaseTests)

# A connection closed at exit, by a handler registered before the first close
# (test_exit_close.cpp): its own program, since the fault is in the exit.
# MALLOC_CHECK_ / MALLOC_PERTURB_ make glibc abort on a write into freed
# memory instead of exiting by luck; other C libraries ignore them.
add_executable(UltraDatabaseExitClose test_exit_close.cpp)
target_compile_features(UltraDatabaseExitClose PRIVATE cxx_std_20)
target_link_libraries(UltraDatabaseExitClose PRIVATE UltraDatabase)
add_test(NAME UltraDatabaseExitClose COMMAND UltraDatabaseExitClose)
set_tests_properties(UltraDatabaseExitClose PROPERTIES
    ENVIRONMENT "MALLOC_CHECK_=3;MALLOC_PERTURB_=165"
    PASS_REGULAR_EXPRESSION "closed at exit")
