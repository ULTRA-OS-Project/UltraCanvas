# Tests/UltraSocial/CMakeLists.txt
# Builds the UltraSocialEngineTests binary. Enabled by
# ULTRACANVAS_BUILD_ULTRASOCIAL_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRASOCIAL_TEST_SOURCES
    test_main.cpp
    test_composer.cpp
    test_store_vault.cpp
    test_connectors.cpp
    test_tier2_connectors.cpp
    test_outbox.cpp
)

add_executable(UltraSocialEngineTests ${ULTRASOCIAL_TEST_SOURCES})

target_compile_features(UltraSocialEngineTests PRIVATE cxx_std_20)
target_include_directories(UltraSocialEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The engine references UltraNet (HTTP/OAuth2) and UltraCanvas (JSON), and
# UltraNet's MIME helpers reference back into UltraCanvas — the same mutual
# static-archive dependency UltraNetTests resolves with a rescanned link
# group on GNU ld (Apple ld64 / MSVC rescan natively and reject the genex).
if(TARGET UltraCanvas)
    if(APPLE OR MSVC)
        target_link_libraries(UltraSocialEngineTests PRIVATE
            UltraSocialEngine UltraNet UltraCanvas)
    elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        target_link_libraries(UltraSocialEngineTests PRIVATE
            "$<LINK_GROUP:RESCAN,UltraSocialEngine,UltraNet,UltraCanvas>")
    else()
        target_link_libraries(UltraSocialEngineTests PRIVATE
            -Wl,--start-group UltraSocialEngine UltraNet UltraCanvas -Wl,--end-group)
    endif()
else()
    target_link_libraries(UltraSocialEngineTests PRIVATE UltraSocialEngine)
endif()

include(CTest)
add_test(NAME UltraSocialEngine COMMAND UltraSocialEngineTests)
