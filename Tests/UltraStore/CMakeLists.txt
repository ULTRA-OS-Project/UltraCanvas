# Tests/UltraStore/CMakeLists.txt
# Builds the UltraStoreCoreTests binary. Enabled by
# ULTRACANVAS_BUILD_ULTRASTORE_TESTS.

cmake_minimum_required(VERSION 3.16)

add_executable(UltraStoreCoreTests
    test_main.cpp
    test_types.cpp
    test_platform.cpp
    test_catalog.cpp
    test_verifier.cpp
    test_resolver.cpp
    test_client.cpp
    test_registry.cpp
    test_installer.cpp
    test_engine.cpp
    test_query.cpp
    test_assets.cpp
    test_settings.cpp
    test_purchase.cpp
)
target_compile_features(UltraStoreCoreTests PRIVATE cxx_std_20)
target_include_directories(UltraStoreCoreTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraStoreCoreTests PRIVATE UltraStoreCore)

include(CTest)
add_test(NAME UltraStoreCore COMMAND UltraStoreCoreTests)
