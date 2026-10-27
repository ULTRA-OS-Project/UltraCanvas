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
    engine/UltraMailFolderNames.cpp
    engine/UltraMailMimeCodec.cpp
    engine/UltraMailAttachmentCache.cpp
    engine/UltraMailContacts.cpp
    engine/UltraMailContactStore.cpp
    engine/UltraMailSenderBrands.cpp
    engine/UltraMailSenderBrandTable.cpp
    engine/UltraMailSenderIconCache.cpp
    engine/UltraMailSenderTrust.cpp
    engine/UltraMailThreatScan.cpp
    engine/UltraMailUnsubscribe.cpp
    engine/UltraMailRecipientComplete.cpp
    engine/UltraMailInlineImages.cpp
    engine/UltraMailMessageFilter.cpp
    engine/UltraMailMessageSort.cpp
    engine/UltraMailSyncEngine.cpp
    engine/UltraMailDiscovery.cpp
    engine/UltraMailOAuth.cpp
    engine/UltraMailLoginCheck.cpp
    engine/UltraMailComposer.cpp
    engine/UltraMailRichComposer.cpp
    engine/UltraMailSignature.cpp
    engine/UltraMailSender.cpp
    engine/UltraMailOutbox.cpp
    engine/UltraMailSyncService.cpp
    engine/UltraMailSyncScheduler.cpp
    engine/UltraMailContactCollector.cpp
    engine/UltraMailFeedPublisher.cpp
    engine/UltraMailNewMail.cpp
    engine/UltraMailTrace.cpp
    engine/UltraMailOneTimeCode.cpp
    engine/UltraMailPaths.cpp
    engine/UltraMailStorage.cpp
    engine/UltraMailSecureMime.cpp
    engine/UltraMailSmime.cpp
    engine/UltraMailOpenPgp.cpp
    engine/UltraMailSecureKeys.cpp
    engine/UltraMailResultText.cpp
)

add_library(UltraMailEngine STATIC ${ULTRAMAIL_ENGINE_SOURCES})
set_target_properties(UltraMailEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultramail_engine")
target_compile_features(UltraMailEngine PUBLIC cxx_std_20)
target_include_directories(UltraMailEngine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(UltraMailEngine PUBLIC UltraDatabase)
# Mail-account passwords live in UltraVault (Argon2id + XChaCha20-Poly1305 via
# UltraCrypt), not in a format of UltraMail's own.
if(TARGET UltraVault)
    target_link_libraries(UltraMailEngine PUBLIC UltraVault)
endif()
# Signed and encrypted mail (UltraMailSecureMime.h): S/MIME on OpenSSL 3's
# CMS, OpenPGP on GPGME (the user's GnuPG). Each is optional and soft-failing:
# without its library the backend compiles to stubs that say so, and a
# message in that form is shown as one this build cannot check.
find_package(OpenSSL 3 QUIET COMPONENTS Crypto)
if(OpenSSL_FOUND)
    target_link_libraries(UltraMailEngine PUBLIC OpenSSL::Crypto)
    target_compile_definitions(UltraMailEngine PUBLIC ULTRAMAIL_HAVE_SMIME=1)
    message(STATUS "  [✓] UltraMail S/MIME (OpenSSL ${OPENSSL_VERSION})")
else()
    message(STATUS "  [−] UltraMail S/MIME - no OpenSSL 3")
endif()
# Not on Windows on Arm: there the first GPGME operation ended the process
# (abort, no message) in CI's windows-11-arm leg (MSYS2 CLANGARM64), where
# x64 Windows runs the same code - UltraMail would quit on opening an OpenPGP
# message. OpenPGP mail is shown as one this build cannot check instead,
# until GPGME on that platform is known to work.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _ultramail_cpu)
if(WIN32 AND _ultramail_cpu MATCHES "^(arm64|aarch64)$")
    set(_ultramail_gpgme_unsafe TRUE)
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT _ultramail_gpgme_unsafe)
    pkg_check_modules(GPGME QUIET IMPORTED_TARGET gpgme)
endif()
if(GPGME_FOUND)
    target_link_libraries(UltraMailEngine PUBLIC PkgConfig::GPGME)
    target_compile_definitions(UltraMailEngine PUBLIC ULTRAMAIL_HAVE_OPENPGP=1)
    message(STATUS "  [✓] UltraMail OpenPGP (GPGME ${GPGME_VERSION})")
elseif(_ultramail_gpgme_unsafe)
    message(STATUS "  [−] UltraMail OpenPGP - not on Windows on Arm (GPGME ends the process there)")
else()
    message(STATUS "  [−] UltraMail OpenPGP - no GPGME")
endif()
# The mail kept on the computer is encrypted at rest (UltraMailStorage.h):
# the message files with UltraCrypt, the databases by UltraDatabase.
if(TARGET UltraCrypt)
    target_link_libraries(UltraMailEngine PUBLIC UltraCrypt)
endif()
# New mail goes to the desktop feed over UltraMessage (mail.message); the
# publisher compiles to a no-op when the module is not in the build.
if(TARGET UltraMessage)
    target_link_libraries(UltraMailEngine PUBLIC UltraMessage)
    target_compile_definitions(UltraMailEngine PUBLIC ULTRAMAIL_HAVE_ULTRAMESSAGE=1)
endif()
# UltraNet provides the MIME/HTTP/Base64 helpers the engine builds on. Its
# home resolves to the shared core, which has absorbed the module and exports
# its API, or to the archive under a static core (UltraCanvas/CMakeLists.txt,
# "MODULE HOMES"); either way the engine names the module and CMake puts the
# library after libultramail_engine on every consumer's link line.
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
    ui/UltraMailAlerts.cpp
    ui/UltraMailPreferences.cpp
    ui/UltraMailStartPage.cpp
    ui/UltraMailAccountBar.cpp
    ui/UltraMailMessagePreview.cpp
    ui/UltraMailSenderBadge.cpp
    ui/UltraMailMailView.cpp
    ui/UltraMailAccountWizard.cpp
    ui/UltraMailAttachmentStrip.cpp
    ui/UltraMailContactsView.cpp
    ui/UltraMailComposeWindow.cpp
    ui/UltraMailFormatBar.cpp
    ui/UltraMailOutboxView.cpp
    ui/UltraMailPassphraseDialog.cpp
    ui/UltraMailWaitDialog.cpp
    ui/UltraMailOAuthCodeDialog.cpp
    ui/UltraMailServerSettingsDialog.cpp
    ui/UltraMailSettingsDialog.cpp
    ui/UltraMailSignatureDialog.cpp
)

add_executable(UltraMail main.cpp ${ULTRAMAIL_UI_SOURCES})
# One UltraMail per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraMail)
# Windows Release builds open no console window (the GUI subsystem), like the
# other apps' (root CMakeLists.txt, "Hide console window on Windows Release
# builds"). The timing trace still goes to trace.log in the data folder, and
# into the console of a prompt UltraMail is started from. Debug builds keep
# their console.
ultracanvas_windows_gui_app(UltraMail)
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

# UltraMail dlopens these UltraNet mailbox plug-ins at runtime (see ENABLE_EXPORTS
# above). As MODULE libraries they have no link relationship to the executable, so
# a build that only targets UltraMail would leave a stale .so behind whenever the
# shared plug-in interface (UltraNetPlugins.h) changes — and the first virtual
# call into a method the old .so lacks crashes (SIGSEGV), or a reworked method
# silently runs old code. Force them to rebuild whenever UltraMail does so the
# loaded .so is never older than the app.
foreach(_mail_plugin UltraNetImapPlugin UltraNetSmtpPlugin
                     UltraNetPop3Plugin UltraNetJmapPlugin)
    if(TARGET ${_mail_plugin})
        add_dependencies(UltraMail ${_mail_plugin})
    endif()
endforeach()

# The app icon, in one place for every consumer of it. main.cpp asks for this
# same file by name; UCAPP_ICON_PATH is the fallback the core applies when an
# app sets no icon of its own (UltraCanvasApplicationBase::Initialize), so the
# two agreeing is what keeps a window from ever coming up unbranded.
# The version shown in the window title: the first line of
# Docs/UltraMail/CHANGELOG.md (cmake/UltraCanvasVersion.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasVersion.cmake")
target_compile_definitions(UltraMail PRIVATE
        ULTRAMAIL_VERSION="${ULTRAMAIL_VERSION}"
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
