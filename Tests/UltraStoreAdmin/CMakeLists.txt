# Tests/UltraStoreAdmin/CMakeLists.txt
# Builds the UltraStoreAdminCoreTests binary. Enabled by
# ULTRACANVAS_BUILD_ULTRASTOREADMIN_TESTS.

cmake_minimum_required(VERSION 3.16)

add_executable(UltraStoreAdminCoreTests
    test_main.cpp
    test_database.cpp
    test_keys.cpp
    test_packages.cpp
    test_publish.cpp
    test_translation.cpp
    test_upload.cpp
    test_affiliates.cpp
    test_pages.cpp
    test_market.cpp
    test_finance.cpp
)
target_compile_features(UltraStoreAdminCoreTests PRIVATE cxx_std_20)
target_include_directories(UltraStoreAdminCoreTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../UltraStore
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraStoreAdminCoreTests PRIVATE UltraStoreAdminCore)
# The web store's browser-test fixture (a published store), which the HTML
# store's test publishes again as HTML + JavaScript, and the shipped
# translations its words come from.
target_compile_definitions(UltraStoreAdminCoreTests PRIVATE
    ULTRASTORE_WEB_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/Apps/UltraStore/web/test/fixture"
    ULTRASTORE_LOCALE_DIR="${CMAKE_SOURCE_DIR}/media/locale")

include(CTest)
add_test(NAME UltraStoreAdminCore COMMAND UltraStoreAdminCoreTests)
