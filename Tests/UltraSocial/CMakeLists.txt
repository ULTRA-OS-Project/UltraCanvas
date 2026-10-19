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
    test_tier3_connectors.cpp
    test_outbox.cpp
    test_paths.cpp
)

add_executable(UltraSocialEngineTests ${ULTRASOCIAL_TEST_SOURCES})

target_compile_features(UltraSocialEngineTests PRIVATE cxx_std_20)
target_include_directories(UltraSocialEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The engine links UltraNet and the core through the UltraNet home: with the
# shared core that is libUltraCanvas alone (Tests/UltraNet/CMakeLists.txt says
# why). With a STATIC core, UltraNet's MIME helpers reference back into
# UltraCanvas - the same mutual static-archive dependency UltraNetTests
# resolves with a rescanned link group on GNU ld (Apple ld64 / MSVC rescan
# natively and reject the genex), with the archive named for exactly that.
set(_ultrasocial_core_shared FALSE)
if(TARGET UltraCanvas)
    get_target_property(_ultrasocial_core_type UltraCanvas TYPE)
    if(_ultrasocial_core_type STREQUAL "SHARED_LIBRARY")
        set(_ultrasocial_core_shared TRUE)
    endif()
endif()
if(NOT TARGET UltraCanvas OR _ultrasocial_core_shared)
    target_link_libraries(UltraSocialEngineTests PRIVATE UltraSocialEngine)
elseif(APPLE OR MSVC)
    target_link_libraries(UltraSocialEngineTests PRIVATE
        UltraSocialEngine uc-net UltraCanvas)
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(UltraSocialEngineTests PRIVATE
        "$<LINK_GROUP:RESCAN,UltraSocialEngine,uc-net,UltraCanvas>")
else()
    target_link_libraries(UltraSocialEngineTests PRIVATE
        -Wl,--start-group UltraSocialEngine uc-net UltraCanvas -Wl,--end-group)
endif()

include(CTest)
add_test(NAME UltraSocialEngine COMMAND UltraSocialEngineTests)
