# Apps/UltraPassword/CMakeLists.txt
# UltraPassword — the password vault for ULTRA OS.
#
# Two targets:
#   UltraPasswordCore  static lib: the group/entry model, the encrypted vault
#                      file (also the export format), CSV exchange and the
#                      password generator. Depends only on UltraCrypt, so the
#                      whole security-relevant surface builds and tests headless
#                      (Tests/UltraPasswordTests.cpp).
#   UltraPassword      the GUI, built only when the framework is available.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT TARGET UltraCrypt)
    message(STATUS "  [−] UltraPassword - SKIPPED (UltraCrypt not built)")
    return()
endif()

add_library(UltraPasswordCore STATIC
    core/PasswordVault.cpp
    core/VaultFile.cpp
    core/PasswordGenerator.cpp
    core/CsvExchange.cpp
)
set_target_properties(UltraPasswordCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrapassword_core")
target_compile_features(UltraPasswordCore PUBLIC cxx_std_20)
# UltraCanvasPathUtf8.h is header-only and has no link dependency.
target_include_directories(UltraPasswordCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)
target_link_libraries(UltraPasswordCore PUBLIC UltraCrypt)

# ===========================================================================
# GUI
# ===========================================================================
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraPassword GUI - SKIPPED (framework not built)")
    return()
endif()

add_executable(UltraPassword
    main.cpp
    ui/PasswordApp.cpp
    ui/StartPage.cpp
    ui/CreateVaultWizard.cpp
    ui/EntryDialog.cpp
    ui/EntryCard.cpp
    ui/ExportDialog.cpp
)

# The window title and --version read the release recorded in
# Docs/UltraPassword/CHANGELOG.md.
target_compile_definitions(UltraPassword PRIVATE
    ULTRAPASSWORD_VERSION="${ULTRAPASSWORD_VERSION}")

target_include_directories(UltraPassword PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(UltraPassword PRIVATE
    UltraPasswordCore
    ${ULTRACANVAS_LIBRARY})

if(TARGET fmt::fmt)
    target_link_libraries(UltraPassword PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraPassword PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()

target_compile_features(UltraPassword PRIVATE cxx_std_20)
set_target_properties(UltraPassword PROPERTIES
    OUTPUT_NAME "UltraPassword"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# One icon everywhere the app is drawn: main.cpp names the same file for the
# window, and the core falls back to UCAPP_ICON_PATH when an app sets none.
target_compile_definitions(UltraPassword PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraPassword.png")

ultracanvas_embed_app_icon(UltraPassword
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraPassword.png")

install(TARGETS UltraPassword RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraPassword.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraPassword.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraPassword.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraPassword - GUI + core")
