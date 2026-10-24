# Apps/UltraDBAdmin/CMakeLists.txt
# UltraDBAdmin - browse and query the databases UltraDatabase reaches
# (Docs/Research/UltraDBAdminDesignProposal.md).
#
# One target, the GUI application. engine/ holds what needs no window (the
# connection store, the Browse pager, the SQL runner) and is what
# Tests/UltraDBAdmin compiles headless; ui/ is the window.
#
# Version: 0.2.0 - ultradbadmin-relay
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

# Needs the UltraCanvas UI library, built by add_subdirectory(UltraCanvas)
# before this directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS}
# are in scope and carry the platform dependencies as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraDBAdmin app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()
if(NOT TARGET UltraDatabase OR NOT TARGET UltraVault)
    message(STATUS "  [−] UltraDBAdmin app - SKIPPED (UltraDatabase or UltraVault not built)")
    return()
endif()

add_executable(UltraDBAdmin
    main.cpp
    engine/DbAdminBlob.cpp
    engine/DbAdminCompletion.cpp
    engine/DbAdminCompare.cpp
    engine/DbAdminConnections.cpp
    engine/DbAdminDump.cpp
    engine/DbAdminEditBuffer.cpp
    engine/DbAdminExport.cpp
    engine/DbAdminFormat.cpp
    engine/DbAdminImport.cpp
    engine/DbAdminJobQueue.cpp
    engine/DbAdminMonitor.cpp
    engine/DbAdminOperations.cpp
    engine/DbAdminPaths.cpp
    engine/DbAdminRelations.cpp
    engine/DbAdminRoutines.cpp
    engine/DbAdminSearch.cpp
    engine/DbAdminServer.cpp
    engine/DbAdminSnippets.cpp
    engine/DbAdminSqlLog.cpp
    engine/DbAdminSqlRunner.cpp
    engine/DbAdminSqlScript.cpp
    engine/DbAdminSqliteTable.cpp
    engine/DbAdminStructureEdit.cpp
    engine/DbAdminTablePager.cpp
    engine/DbAdminValueTypes.cpp
    ui/DbAdminBlobDialog.cpp
    ui/DbAdminConnectionDialog.cpp
    ui/DbAdminImportDialog.cpp
    ui/DbAdminModels.cpp
    ui/DbAdminOperationDialogs.cpp
    ui/DbAdminRowDialog.cpp
    ui/DbAdminStructureDialog.cpp
    ui/DbAdminTheme.cpp
    ui/DbAdminWindow.cpp
    ui/DbAdminWindowObjects.cpp
    ui/DbAdminWindowSchema.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraDBAdmin)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraDBAdmin PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(UltraDBAdmin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# Modules before the core on the link line: GNU ld resolves left to right.
target_link_libraries(UltraDBAdmin PRIVATE UltraDatabase UltraVault ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraDBAdmin PRIVATE fmt::fmt)
endif()
find_package(Threads REQUIRED)
target_link_libraries(UltraDBAdmin PRIVATE Threads::Threads)
target_compile_features(UltraDBAdmin PRIVATE cxx_std_20)

# UltraDBAdmin versions itself: the number comes from the first line of
# Docs/UltraDBAdmin/CHANGELOG.md through cmake/UltraCanvasVersion.cmake.
target_compile_definitions(UltraDBAdmin PRIVATE
    ULTRADBADMIN_VERSION="${ULTRADBADMIN_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Embed the application manifest + version info (Windows only).
ultracanvas_embed_windows_manifest(UltraDBAdmin)

# One icon, everywhere the app is drawn: main.cpp names this file for the
# window and taskbar icon, and UCAPP_ICON_PATH is the core's fallback. The
# PNG is the 256 px render of media/appicon/UltraDBAdmin.svg.
target_compile_definitions(UltraDBAdmin PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraDBAdmin.png")
ultracanvas_embed_app_icon(UltraDBAdmin
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraDBAdmin.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraDBAdmin RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraDBAdmin.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraDBAdmin.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraDBAdmin.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    message(STATUS "  UltraDBAdmin desktop entry: share/applications/UltraDBAdmin.desktop")
endif()

# ===== THE RELAY =====
# ultradbadmin-relay: the server half of the browser build (relay/main.cpp,
# Docs/Research/UltraDBAdminDesignProposal.md 8.1) - headless, UltraDatabase
# and the vault, no window. A POSIX server program.
if(UNIX AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(ultradbadmin-relay
        relay/main.cpp
        relay/RelayConfig.cpp
        relay/RelayWebSocket.cpp)
    target_link_libraries(ultradbadmin-relay PRIVATE UltraDatabase UltraVault Threads::Threads)
    target_compile_features(ultradbadmin-relay PRIVATE cxx_std_20)
    install(TARGETS ultradbadmin-relay RUNTIME DESTINATION bin)
    message(STATUS "  [✓] ultradbadmin-relay - ENABLED (target: ultradbadmin-relay)")
endif()

message(STATUS "  [✓] UltraDBAdmin app - ENABLED (target: UltraDBAdmin)")
message(STATUS "      Version ${ULTRADBADMIN_VERSION} (Docs/UltraDBAdmin/CHANGELOG.md)")
