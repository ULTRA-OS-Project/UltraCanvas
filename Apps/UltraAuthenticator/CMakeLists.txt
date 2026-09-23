# Apps/UltraAuthenticator/CMakeLists.txt
# UltraAuthenticator — TOTP/HOTP codes for ULTRA OS.
#
# Two targets:
#   UltraAuthenticatorCore  static lib: OTP engine, otpauth:// parser, encrypted
#                           vault and the account layer. Depends only on
#                           UltraCrypt, so the whole security-relevant surface
#                           builds and tests without the UI stack — which is why
#                           the test suites in Tests/ can run headless in CI.
#   UltraAuthenticator      the GUI, built only when the framework is available.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT TARGET UltraCrypt)
    message(STATUS "  [−] UltraAuthenticator - SKIPPED (UltraCrypt not built)")
    return()
endif()

set(ULTRAAUTHENTICATOR_CORE_SOURCES
    otp/UltraOtp.cpp
    otp/OtpAuthUri.cpp
    store/EncryptedFileStore.cpp
    AccountStore.cpp
    AccountExport.cpp
    LockPolicy.cpp
    Preferences.cpp
    # The backup file is sealed with UCDCrypto::Seal, the framework's
    # password envelope. It is compiled here rather than linked because
    # nothing else builds it: the source lives under Plugins/Documents but the
    # UltraCanvas library does not link UltraCrypt, so the envelope is not part
    # of it, and until now only test targets compiled it. Worth relocating to a
    # shared crypto target one day; compiling one file is the smaller change.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/Plugins/Documents/UCDCryptoEnvelope.cpp
)

add_library(UltraAuthenticatorCore STATIC ${ULTRAAUTHENTICATOR_CORE_SOURCES})
set_target_properties(UltraAuthenticatorCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultraauthenticator_core")
target_compile_features(UltraAuthenticatorCore PUBLIC cxx_std_20)
target_include_directories(UltraAuthenticatorCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UltraAuthenticatorCore PUBLIC UltraCrypt)

# ===========================================================================
# GUI
# ===========================================================================
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraAuthenticator GUI - SKIPPED (framework not built)")
    return()
endif()

add_executable(UltraAuthenticator
    main.cpp
    AuthenticatorWindow.cpp
    AddAccountDialog.cpp
    EditAccountDialog.cpp
    ChangePasswordDialog.cpp
    RevealSecretDialog.cpp
    ScanAccountDialog.cpp
    BackupDialog.cpp
    LockScreenDialog.cpp
    SettingsDialog.cpp
    NewVaultDialog.cpp
)

# --version reports the release recorded in Docs/UltraAuthenticator/CHANGELOG.md.
target_compile_definitions(UltraAuthenticator PRIVATE
    ULTRAAUTHENTICATOR_VERSION="${ULTRAAUTHENTICATOR_VERSION}")

target_include_directories(UltraAuthenticator PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(UltraAuthenticator PRIVATE
    UltraAuthenticatorCore
    ${ULTRACANVAS_LIBRARY})

if(TARGET fmt::fmt)
    target_link_libraries(UltraAuthenticator PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraAuthenticator PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()

target_compile_features(UltraAuthenticator PRIVATE cxx_std_20)
set_target_properties(UltraAuthenticator PROPERTIES
    OUTPUT_NAME "UltraAuthenticator"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# One icon, everywhere the app is drawn. main.cpp names this same file for the
# window and taskbar icon; UCAPP_ICON_PATH is the fallback the core applies
# when an app sets no icon of its own (UltraCanvasApplicationBase::Initialize),
# so the two agreeing is what keeps a window from ever coming up unbranded. The
# PNG is the 256 px render of media/appicon/UltraAuthenticator.svg, the
# scalable half of the pair.
target_compile_definitions(UltraAuthenticator PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraAuthenticator.png")

# Embed the app icon in the .exe (Windows only). This is the icon Explorer and
# the Windows taskbar read off the binary itself — neither of them looks at
# _NET_WM_ICON or at a desktop entry.
ultracanvas_embed_app_icon(UltraAuthenticator
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraAuthenticator.png")

install(TARGETS UltraAuthenticator RUNTIME DESTINATION bin)

# The freedesktop shortcut and the icon it names. `Icon=UltraAuthenticator` in
# the .desktop is resolved through the installed icon themes, so the files have
# to land in one: this is what lets UltraFiler (and the application menu, and a
# taskbar that matches windows to desktop entries) show UltraAuthenticator's
# own icon for the app rather than a generic executable glyph.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraAuthenticator.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraAuthenticator.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraAuthenticator.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    message(STATUS "  UltraAuthenticator desktop entry: share/applications/UltraAuthenticator.desktop")
endif()

message(STATUS "  [+] UltraAuthenticator - GUI + core")
