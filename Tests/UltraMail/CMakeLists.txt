# Tests/UltraMail/CMakeLists.txt
# Builds the UltraMailEngineTests binary. Enabled by ULTRACANVAS_BUILD_ULTRAMAIL_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRAMAIL_TEST_SOURCES
    test_main.cpp
    test_localstore.cpp
    test_attachments.cpp
    test_contacts.cpp
    test_senderidentity.cpp
    test_threatscan.cpp
    test_unsubscribe.cpp
    test_recipientcomplete.cpp
    test_inlineimages.cpp
    test_messagefilter.cpp
    test_messagesort.cpp
    test_foldernames.cpp
    test_syncengine.cpp
    test_discovery.cpp
    test_composer.cpp
    test_richcomposer.cpp
    test_signature.cpp
    test_outbox.cpp
    test_draftscopy.cpp
    test_scheduler.cpp
    test_oauth.cpp
    test_logincheck.cpp
    test_feedpublisher.cpp
    test_newmail.cpp
    test_preferences.cpp
    test_headertext.cpp
    test_trace.cpp
    test_onetimecode.cpp
    # The Settings window's preferences file: plain C++, no UI.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Apps/UltraMail/ui/UltraMailPreferences.cpp
)

add_executable(UltraMailEngineTests ${ULTRAMAIL_TEST_SOURCES})

target_compile_features(UltraMailEngineTests PRIVATE cxx_std_20)
target_link_libraries(UltraMailEngineTests PRIVATE UltraMailEngine)
target_include_directories(UltraMailEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../Apps/UltraMail/ui)

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

# The message pane's body reaches the pane's bottom: an attachment strip with
# nothing in it takes no room. Shows real .eml messages in a window, so it
# runs under Xvfb and skips itself without a DISPLAY. Linked like the UltraMail
# app: the engine (MIME, trace, inline images) before the core.
if(ULTRACANVAS_LIBRARY)
    add_executable(UltraMailMessagePreviewTest
        MessagePreviewLayoutTest.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Apps/UltraMail/ui/UltraMailMessagePreview.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Apps/UltraMail/ui/UltraMailSenderBadge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Apps/UltraMail/ui/UltraMailAttachmentStrip.cpp
    )
    target_compile_features(UltraMailMessagePreviewTest PRIVATE cxx_std_20)
    target_include_directories(UltraMailMessagePreviewTest PRIVATE
                               ${ULTRACANVAS_INCLUDE_DIRS}
                               ${CMAKE_CURRENT_SOURCE_DIR}/../../Apps/UltraMail/ui)
    target_link_libraries(UltraMailMessagePreviewTest PRIVATE UltraMailEngine ${ULTRACANVAS_LIBRARY})
    if(TARGET fmt::fmt)
        target_link_libraries(UltraMailMessagePreviewTest PRIVATE fmt::fmt)
    endif()
    add_test(NAME UltraMailMessagePreview COMMAND UltraMailMessagePreviewTest)
endif()
