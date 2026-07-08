# Apps/UltraMail/CMakeLists.txt
# UltraMail — the ULTRA OS mail application. Builds the headless engine library
# (account / folder / message store on UltraDatabase) and, on top of it, the
# UltraMail GUI executable (Toolbox, account wizard, reading view, composer,
# contacts) linking the UltraCanvas UI library.
# Version: 0.2.0
# Last Modified: 2026-07-08
# Author: UltraCanvas Framework / ULTRA OS

# The engine depends only on UltraDatabase, so it builds and tests headlessly.
if(NOT TARGET UltraDatabase)
    message(STATUS "  [−] UltraMail engine - SKIPPED (UltraDatabase not built)")
    return()
endif()

set(ULTRAMAIL_ENGINE_SOURCES
    engine/UltraMailTypes.cpp
    engine/UltraMailLocalStore.cpp
    engine/UltraMailMimeCodec.cpp
    engine/UltraMailAttachmentCache.cpp
    engine/UltraMailContacts.cpp
    engine/UltraMailContactStore.cpp
    engine/UltraMailSyncEngine.cpp
    engine/UltraMailDiscovery.cpp
    engine/UltraMailCredentialVault.cpp
    engine/UltraMailComposer.cpp
    engine/UltraMailSender.cpp
    engine/UltraMailOutbox.cpp
    engine/UltraMailSyncService.cpp
    engine/UltraMailSyncScheduler.cpp
    engine/UltraMailContactCollector.cpp
)

add_library(UltraMailEngine STATIC ${ULTRAMAIL_ENGINE_SOURCES})
set_target_properties(UltraMailEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultramail_engine")
target_compile_features(UltraMailEngine PUBLIC cxx_std_20)
target_include_directories(UltraMailEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(UltraMailEngine PUBLIC UltraDatabase)
# UltraNet provides the MIME codec (UltraNet_MimeParse) the engine builds on.
if(TARGET UltraNet)
    target_link_libraries(UltraMailEngine PUBLIC UltraNet)
endif()

message(STATUS "  [✓] UltraMail engine - ENABLED (target: UltraMailEngine)")

# ── UltraMail GUI application ────────────────────────────────────────────────
# The full UI executable on top of the engine. Needs the UltraCanvas UI library,
# which is built by add_subdirectory(UltraCanvas) before this directory, so
# ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS} are already in scope and
# carry the platform deps (X11/Cairo/Pango/UltraNet/UltraDatabase) as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraMail GUI app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

set(ULTRAMAIL_UI_SOURCES
    ui/UltraMailApp.cpp
    ui/UltraMailToolbox.cpp
    ui/UltraMailInfoTileBar.cpp
    ui/UltraMailAccountWizard.cpp
    ui/UltraMailAttachmentStrip.cpp
    ui/UltraMailContactsView.cpp
    ui/UltraMailReadingView.cpp
    ui/UltraMailComposeWindow.cpp
)

add_executable(UltraMail main.cpp ${ULTRAMAIL_UI_SOURCES})
# Plugins (SMTP/IMAP DSOs) resolve symbols back from the executable at runtime.
set_target_properties(UltraMail PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(UltraMail PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(UltraMail PRIVATE ${ULTRACANVAS_LIBRARY} UltraMailEngine)
if(TARGET fmt::fmt)
    target_link_libraries(UltraMail PRIVATE fmt::fmt)
endif()
target_compile_features(UltraMail PRIVATE cxx_std_20)
message(STATUS "  [✓] UltraMail GUI app - ENABLED (target: UltraMail)")
