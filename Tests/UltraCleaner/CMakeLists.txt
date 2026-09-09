# Tests/UltraCleaner/CMakeLists.txt
# Builds the UltraCleanerEngineTests binary. Enabled by
# ULTRACANVAS_BUILD_ULTRACLEANER_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRACLEANER_TEST_SOURCES
    test_main.cpp
    test_paths.cpp
    test_safety.cpp
    test_rules.cpp
    test_scanner.cpp
    test_remover.cpp
)

# The album tests link the album engine, which needs the UltraCanvas core for
# image decoding. They are added only when that target exists.
if(TARGET UltraCleanerAlbum)
    list(APPEND ULTRACLEANER_TEST_SOURCES test_album.cpp)
endif()

add_executable(UltraCleanerEngineTests ${ULTRACLEANER_TEST_SOURCES})

target_compile_features(UltraCleanerEngineTests PRIVATE cxx_std_20)
target_include_directories(UltraCleanerEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The engine is self-contained — no UltraCanvas, no UltraNet — so the suite
# links it and nothing else.
target_link_libraries(UltraCleanerEngineTests PRIVATE UltraCleanerEngine)
if(TARGET UltraCleanerAlbum)
    target_link_libraries(UltraCleanerEngineTests PRIVATE UltraCleanerAlbum)
endif()

include(CTest)
add_test(NAME UltraCleanerEngine COMMAND UltraCleanerEngineTests)
