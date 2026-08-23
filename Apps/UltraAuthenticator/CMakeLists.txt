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
)

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

install(TARGETS UltraAuthenticator RUNTIME DESTINATION bin)
message(STATUS "  [+] UltraAuthenticator - GUI + core")
