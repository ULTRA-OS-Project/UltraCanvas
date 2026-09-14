# Apps/UltraMail/CMakeLists.txt
# UltraMail — the ULTRA OS mail application. Builds the headless engine library
# (account / folder / message store on UltraDatabase) and, on top of it, the
# UltraMail GUI executable (Toolbox, account wizard, reading view, composer,
# contacts) linking the UltraCanvas UI library.
# Version: 0.5.0
# Last Modified: 2026-09-03
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
    engine/UltraMailOAuth.cpp
    engine/UltraMailLoginCheck.cpp
    engine/UltraMailComposer.cpp
    engine/UltraMailSender.cpp
    engine/UltraMailOutbox.cpp
    engine/UltraMailSyncService.cpp
    engine/UltraMailSyncScheduler.cpp
    engine/UltraMailContactCollector.cpp
)

# ── Built-in OAuth client credentials (compiled-in fallback) ─────────────────
# The Google / Microsoft OAuth *client* a shipped build signs in as, baked into
# the binary as the lowest-priority fallback (Set() / env / oauth.ini override
# it — see OAuthApps::Get). Empty by default, so a plain dev/CI/test build bakes
# in nothing and behaves exactly as before. To ship a preconfigured build,
# either -D these on the configure line (e.g. from CI secrets) or drop a local,
# gitignored cmake/oauth.local.cmake that set()s them — see
# Docs/UltraMail/AccountSetup.md and cmake/oauth.local.cmake.example.
set(ULTRAMAIL_GOOGLE_CLIENT_ID     "" CACHE STRING "Baked-in Google OAuth client id")
set(ULTRAMAIL_GOOGLE_CLIENT_SECRET "" CACHE STRING "Baked-in Google OAuth client secret")
set(ULTRAMAIL_MICROSOFT_CLIENT_ID  "" CACHE STRING "Baked-in Microsoft OAuth client id")
option(ULTRAMAIL_OAUTH_OBFUSCATE "XOR-obfuscate baked-in OAuth credentials" ON)
# High-bit key so ASCII credentials never XOR to a NUL (which truncates a C
# string). Anti-scraping friction only — the key ships in the same binary.
set(ULTRAMAIL_OAUTH_XOR_KEY "183" CACHE STRING "XOR key for baked-in credential obfuscation (1..255)")

# A build machine's real credentials, kept out of the repo. When present it
# set(... CACHE ... FORCE)s the three vars above.
set(_um_oauth_local "${CMAKE_SOURCE_DIR}/cmake/oauth.local.cmake")
if(EXISTS "${_um_oauth_local}")
    include("${_um_oauth_local}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_um_oauth_local}")
endif()

set(_um_oauth_tmpl "${CMAKE_CURRENT_SOURCE_DIR}/engine/UltraMailOAuthDefaults.h.in")
set(_um_oauth_hdr  "${CMAKE_CURRENT_BINARY_DIR}/generated/UltraMailOAuthDefaults.h")
set(_um_oauth_have OFF)
if(ULTRAMAIL_GOOGLE_CLIENT_ID OR ULTRAMAIL_GOOGLE_CLIENT_SECRET OR ULTRAMAIL_MICROSOFT_CLIENT_ID)
    set(_um_oauth_have ON)
endif()

if(_um_oauth_have AND ULTRAMAIL_OAUTH_OBFUSCATE)
    # Obfuscated: a Python helper does the per-byte XOR and writes the header.
    # Credentials pass through the environment, never the command line, so they
    # stay out of any process listing.
    find_program(ULTRAMAIL_PYTHON3 NAMES python3 python)
    if(NOT ULTRAMAIL_PYTHON3)
        message(FATAL_ERROR "UltraMail: baked-in OAuth credentials with obfuscation "
            "need python3. Install it, or reconfigure with -DULTRAMAIL_OAUTH_OBFUSCATE=OFF.")
    endif()
    set(_um_oauth_gen "${CMAKE_SOURCE_DIR}/scripts/generate_oauth_defaults.py")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
                "UM_GOOGLE_ID=${ULTRAMAIL_GOOGLE_CLIENT_ID}"
                "UM_GOOGLE_SECRET=${ULTRAMAIL_GOOGLE_CLIENT_SECRET}"
                "UM_MICROSOFT_ID=${ULTRAMAIL_MICROSOFT_CLIENT_ID}"
                "${ULTRAMAIL_PYTHON3}" "${_um_oauth_gen}"
                --template "${_um_oauth_tmpl}"
                --out "${_um_oauth_hdr}"
                --xor-key "${ULTRAMAIL_OAUTH_XOR_KEY}"
        RESULT_VARIABLE _um_oauth_rc)
    if(NOT _um_oauth_rc EQUAL 0)
        message(FATAL_ERROR "UltraMail: generating ${_um_oauth_hdr} failed (${_um_oauth_rc}).")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_um_oauth_tmpl}" "${_um_oauth_gen}")
    message(STATUS "  [✓] UltraMail OAuth client - BAKED IN (obfuscated)")
else()
    # Empty (dev/test) or plain: configure_file, no Python dependency.
    if(_um_oauth_have)
        set(UM_GOOGLE_ID_C     "\"${ULTRAMAIL_GOOGLE_CLIENT_ID}\"")
        set(UM_GOOGLE_SECRET_C "\"${ULTRAMAIL_GOOGLE_CLIENT_SECRET}\"")
        set(UM_MICROSOFT_ID_C  "\"${ULTRAMAIL_MICROSOFT_CLIENT_ID}\"")
        message(STATUS "  [✓] UltraMail OAuth client - BAKED IN (plain)")
    else()
        set(UM_GOOGLE_ID_C     "\"\"")
        set(UM_GOOGLE_SECRET_C "\"\"")
        set(UM_MICROSOFT_ID_C  "\"\"")
    endif()
    set(UM_OBFUSCATED "0")
    set(UM_XOR_KEY    "0")
    configure_file("${_um_oauth_tmpl}" "${_um_oauth_hdr}" @ONLY)
endif()

add_library(UltraMailEngine STATIC ${ULTRAMAIL_ENGINE_SOURCES})
set_target_properties(UltraMailEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultramail_engine")
target_compile_features(UltraMailEngine PUBLIC cxx_std_20)
target_include_directories(UltraMailEngine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/engine
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(UltraMailEngine PUBLIC UltraDatabase)
# Mail-account passwords live in UltraVault (Argon2id + XChaCha20-Poly1305 via
# UltraCrypt), not in a format of UltraMail's own.
if(TARGET UltraVault)
    target_link_libraries(UltraMailEngine PUBLIC UltraVault)
endif()
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
    ui/UltraMailAlerts.cpp
    ui/UltraMailStartPage.cpp
    ui/UltraMailAccountBar.cpp
    ui/UltraMailMessagePreview.cpp
    ui/UltraMailMailView.cpp
    ui/UltraMailAccountWizard.cpp
    ui/UltraMailAttachmentStrip.cpp
    ui/UltraMailContactsView.cpp
    ui/UltraMailComposeWindow.cpp
    ui/UltraMailPassphraseDialog.cpp
    ui/UltraMailWaitDialog.cpp
    ui/UltraMailServerSettingsDialog.cpp
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
# Cloud storage accounts + the shared dialogs behind "Attach cloud link".
# UltraCloud pulls UltraCanvasJSON out of the core library, so it must come
# before ${ULTRACANVAS_LIBRARY} on the link line for the same left-to-right
# reason as the engine.
if(TARGET UltraCloudUI)
    target_link_libraries(UltraMail PRIVATE UltraCloudUI)
endif()
target_link_libraries(UltraMail PRIVATE UltraMailEngine ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraMail PRIVATE fmt::fmt)
endif()
target_compile_features(UltraMail PRIVATE cxx_std_20)

# The app icon, in one place for every consumer of it. main.cpp asks for this
# same file by name; UCAPP_ICON_PATH is the fallback the core applies when an
# app sets no icon of its own (UltraCanvasApplicationBase::Initialize), so the
# two agreeing is what keeps a window from ever coming up unbranded.
target_compile_definitions(UltraMail PRIVATE
        UCAPP_ICON_PATH="media/appicon/UltraMail.png")

# Embed the app icon in the .exe (Windows only). This is the icon Explorer and
# the Windows taskbar read off the binary itself — neither of them looks at
# _NET_WM_ICON or at a desktop entry — so without it the window wears the
# UltraMail mark while its button on the taskbar wears the generic one.
ultracanvas_embed_app_icon(UltraMail
        "${CMAKE_SOURCE_DIR}/media/appicon/UltraMail.png")

# The freedesktop shortcut and the icon it names. `Icon=UltraMail` in the
# .desktop is resolved through the installed icon themes, so the PNG has to
# land in one: this is what lets UltraFiler (and the application menu, and a
# taskbar that matches windows to desktop entries) show UltraMail's own icon
# for the app rather than a generic executable glyph.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    # Exec=UltraMail / TryExec=UltraMail are resolved against PATH, so the
    # entry is only true if the binary is installed with it.
    install(TARGETS UltraMail RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraMail.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraMail.png
            DESTINATION share/icons/hicolor/256x256/apps)
    message(STATUS "  UltraMail desktop entry: share/applications/UltraMail.desktop")
endif()

message(STATUS "  [✓] UltraMail GUI app - ENABLED (target: UltraMail)")
