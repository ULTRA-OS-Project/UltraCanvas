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
# UltraNet provides the MIME/HTTP/Base64 helpers the engine builds on.
# With a STATIC UltraCanvas we link the UltraNet archive directly. With a SHARED
# UltraCanvas, UltraNet is absorbed into (and exported by) libultracanvas — link
# THAT instead, for two reasons:
#   * linking the static archive too would put a second copy of the UltraNet
#     symbols on consumers' link lines and collide with the DLL's exports;
#   * the engine's UltraNet_* references must resolve from the ultracanvas import
#     lib, and GNU ld resolves left-to-right, so libultracanvas has to appear
#     AFTER libultramail_engine. Expressing the dependency here lets CMake order
#     them for every consumer (the UltraMail app and the headless engine test).
# (ULTRACANVAS_BUILD_SHARED can be force-enabled inside the UltraCanvas
# subdirectory scope, so query the resolved target type instead of the option.)
if(TARGET UltraNet)
    set(_uc_core_shared FALSE)
    if(TARGET ${ULTRACANVAS_LIBRARY})
        get_target_property(_uc_core_type ${ULTRACANVAS_LIBRARY} TYPE)
        if(_uc_core_type STREQUAL "SHARED_LIBRARY")
            set(_uc_core_shared TRUE)
        endif()
    endif()
    if(_uc_core_shared)
        target_link_libraries(UltraMailEngine PUBLIC ${ULTRACANVAS_LIBRARY})
        target_include_directories(UltraMailEngine PUBLIC
            $<TARGET_PROPERTY:UltraNet,INTERFACE_INCLUDE_DIRECTORIES>)
    else()
        target_link_libraries(UltraMailEngine PUBLIC UltraNet)
    endif()
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
# Engine before the core: the engine (static) pulls UltraNet_* from the shared
# libultracanvas import lib, and GNU ld resolves left-to-right, so the provider
# must follow the consumer on the link line.
target_link_libraries(UltraMail PRIVATE UltraMailEngine ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraMail PRIVATE fmt::fmt)
endif()
target_compile_features(UltraMail PRIVATE cxx_std_20)
message(STATUS "  [✓] UltraMail GUI app - ENABLED (target: UltraMail)")
