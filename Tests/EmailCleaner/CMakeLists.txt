# Tests/EmailCleaner/CMakeLists.txt
# Builds the EmailCleanerEngineTests binary. Enabled by
# ULTRACANVAS_BUILD_EMAILCLEANER_TESTS.

cmake_minimum_required(VERSION 3.16)

set(EMAILCLEANER_TEST_SOURCES
    test_main.cpp
    test_types.cpp
    test_text.cpp
    test_rules.cpp
    test_classifier.cpp
    test_store.cpp
    test_ingest.cpp
    test_analytics.cpp
    test_unsubscribe.cpp
    test_actions.cpp
    test_mailbackend.cpp
    test_overrides.cpp
    test_attachments.cpp
)

add_executable(EmailCleanerEngineTests ${EMAILCLEANER_TEST_SOURCES})

target_compile_features(EmailCleanerEngineTests PRIVATE cxx_std_20)
target_link_libraries(EmailCleanerEngineTests PRIVATE EmailCleanerEngine)
target_include_directories(EmailCleanerEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(CTest)
add_test(NAME EmailCleanerEngine COMMAND EmailCleanerEngineTests)

# UltraNet's MIME parser — which the ingest builds on — calls the UltraCanvas
# string and Base64 helpers (UltraCanvasUtils), so the core library has to
# follow UltraNet on the link line: GNU ld scans each archive once, in place.
# Naming the libraries explicitly fixes that order; CMake emits direct
# dependencies in declaration order. With a SHARED UltraCanvas, UltraNet is
# absorbed into the .so and the extra entries collapse to a no-op.
# The suite still runs headless — it never opens a window, so this is a
# link-time dependency only.
if(TARGET UltraNet)
    target_link_libraries(EmailCleanerEngineTests PRIVATE UltraNet)
endif()
if(ULTRACANVAS_LIBRARY)
    target_link_libraries(EmailCleanerEngineTests PRIVATE ${ULTRACANVAS_LIBRARY})
endif()
