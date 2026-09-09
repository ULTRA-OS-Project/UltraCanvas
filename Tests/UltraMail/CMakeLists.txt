# Tests/UltraMail/CMakeLists.txt
# Builds the UltraMailEngineTests binary. Enabled by ULTRACANVAS_BUILD_ULTRAMAIL_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRAMAIL_TEST_SOURCES
    test_main.cpp
    test_localstore.cpp
    test_attachments.cpp
    test_contacts.cpp
    test_syncengine.cpp
    test_discovery.cpp
    test_composer.cpp
    test_outbox.cpp
    test_scheduler.cpp
)

add_executable(UltraMailEngineTests ${ULTRAMAIL_TEST_SOURCES})

target_compile_features(UltraMailEngineTests PRIVATE cxx_std_20)
target_link_libraries(UltraMailEngineTests PRIVATE UltraMailEngine)
target_include_directories(UltraMailEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# UltraNet's MIME parser — which the composer and the sync engine build on —
# calls the UltraCanvas string and Base64 helpers (UltraCanvasUtils), so the
# core library has to follow UltraNet on the link line: GNU ld scans each
# archive once, in place, and libultranet.a comes first through UltraMailEngine.
# Naming the libraries explicitly fixes that order; CMake emits direct
# dependencies in declaration order. With a SHARED UltraCanvas, UltraNet is
# absorbed into the .so and the extra entries collapse to a no-op.
# The suite still runs headless — it never opens a window, so this is a
# link-time dependency only.
if(TARGET UltraNet)
    target_link_libraries(UltraMailEngineTests PRIVATE UltraNet)
endif()
if(ULTRACANVAS_LIBRARY)
    target_link_libraries(UltraMailEngineTests PRIVATE ${ULTRACANVAS_LIBRARY})
endif()

include(CTest)
add_test(NAME UltraMailEngine COMMAND UltraMailEngineTests)
