# Apps/UltraStoreAdmin/CMakeLists.txt
# UltraStoreAdmin - the store's admin backend: the apps and their settings in
# a database, the files behind the packages, the signing key sealed with a
# passphrase, the texts translated into every official language, and the
# catalog generated, signed and laid out for the web server
# (Docs/UltraStoreAdmin/README.md).
#
# Two targets:
#   UltraStoreAdminCore  a static library on UltraStoreCore (the catalog's
#                        types and writer, the verifier), UltraDatabase and
#                        UltraCrypt; UltraAI when the module is built. Opens
#                        no window, so the command line and the tests run
#                        headless.
#   ultrastoreadmin      the window when started plainly, the commands when
#                        given one.
#
# Version: 0.2.0 - the window
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT TARGET UltraStoreCore)
    message(STATUS "  [−] UltraStoreAdmin - SKIPPED (UltraStoreCore not built)")
    return()
endif()

add_library(UltraStoreAdminCore STATIC
    core/AdminDatabase.cpp
    core/KeyVault.cpp
    core/PackageStore.cpp
    core/CatalogPublisher.cpp
    core/Translation.cpp
)
set_target_properties(UltraStoreAdminCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrastoreadmin_core")
target_compile_features(UltraStoreAdminCore PUBLIC cxx_std_20)
target_include_directories(UltraStoreAdminCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_include_directories(UltraStoreAdminCore PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraStoreAdminCore PUBLIC UltraStoreCore)
# The VirtualFS bridge lists an archive's entries (the entry to start is
# suggested from them); the core defines ULTRACANVAS_HAS_VIRTUALFS on its
# interface when it has a provider.
if(TARGET UltraAI_Core)
    target_link_libraries(UltraStoreAdminCore PUBLIC UltraAI_Core)
    target_include_directories(UltraStoreAdminCore PRIVATE ${CMAKE_SOURCE_DIR}/UltraAI/include)
    target_compile_definitions(UltraStoreAdminCore PRIVATE ULTRASTOREADMIN_HAVE_ULTRAAI=1)
    message(STATUS "  [✓] UltraStoreAdmin core - ENABLED (with UltraAI translation)")
else()
    message(STATUS "  [✓] UltraStoreAdmin core - ENABLED (no UltraAI: translation needs the module)")
endif()

# The program: the window (ui/) when started plainly, the commands (cli/)
# when given one.
add_executable(ultrastoreadmin
    ui/main.cpp
    ui/AdminWindow.cpp
    ui/AdminComponentPage.cpp
    cli/Commands.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(ultrastoreadmin)
target_include_directories(ultrastoreadmin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(ultrastoreadmin PRIVATE UltraStoreAdminCore ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(ultrastoreadmin PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(ultrastoreadmin PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(ultrastoreadmin PRIVATE cxx_std_20)
# The version comes from the first line of Docs/UltraStoreAdmin/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake - never a literal of its own. One
# icon everywhere the app is drawn (UCAPP_ICON_PATH is the core's fallback).
target_compile_definitions(ultrastoreadmin PRIVATE
    ULTRASTOREADMIN_VERSION="${ULTRASTOREADMIN_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraStoreAdmin.png")
ultracanvas_embed_app_icon(ultrastoreadmin "${CMAKE_SOURCE_DIR}/media/appicon/UltraStoreAdmin.png")
# A GUI program on Windows with ultrastoreadmin.com beside it, so a prompt
# waits for `ultrastoreadmin publish` and sees its exit code.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasWindowsGuiApp.cmake")
ultracanvas_windows_console_companion(ultrastoreadmin)
# In the build root beside ultrastore, where the core's DLL is on Windows.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ultrastoreadmin RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraStoreAdmin.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraStoreAdmin.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraStoreAdmin.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()
message(STATUS "  [✓] ultrastoreadmin (window and command line) - ENABLED")
