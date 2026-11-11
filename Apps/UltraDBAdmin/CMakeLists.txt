# Apps/UltraDBAdmin/CMakeLists.txt
# UltraDBAdmin - browse and query the databases UltraDatabase reaches
# (Docs/Research/UltraDBAdminDesignProposal.md).
#
# One target, the GUI application. engine/ holds what needs no window (the
# connection store, the Browse pager, the SQL runner) and is what
# Tests/UltraDBAdmin compiles headless; ui/ is the window.
#
# Version: 0.2.0 - ultradbadmin-relay; the browser build (UltraDBAdmin.html)
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

# Needs the UltraCanvas UI library, built by add_subdirectory(UltraCanvas)
# before this directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS}
# are in scope and carry the platform dependencies as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraDBAdmin app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()
if(EMSCRIPTEN)
    # The browser build: no vault (nothing to keep - the relay holds the
    # passwords), only UltraDatabase with its relay driver.
    if(NOT TARGET UltraDatabase)
        message(STATUS "  [−] UltraDBAdmin page - SKIPPED (UltraDatabase not built)")
        return()
    endif()
elseif(NOT TARGET UltraDatabase OR NOT TARGET UltraVault)
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
if(EMSCRIPTEN)
    target_sources(UltraDBAdmin PRIVATE web/DbAdminRelayWeb.cpp)
else()
    # One copy per user (UltraCanvasSingleInstance.h).
    ultracanvas_single_instance(UltraDBAdmin)
endif()

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
if(NOT EMSCRIPTEN)
    set_target_properties(UltraDBAdmin PROPERTIES ENABLE_EXPORTS ON)
endif()

target_include_directories(UltraDBAdmin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# Modules before the core on the link line: GNU ld resolves left to right.
if(EMSCRIPTEN)
    target_link_libraries(UltraDBAdmin PRIVATE UltraDatabase ${ULTRACANVAS_LIBRARY})
    # The icons are SVG, and the browser's libvips reads none: the Vector
    # plugin's preview seam does (UltraCanvas/OS/WASM/README.md). Linking it
    # defines ULTRACANVAS_HAS_VECTOR_PLUGIN, and main.cpp registers it.
    if(TARGET UltraCanvasVectorPlugin)
        target_link_libraries(UltraDBAdmin PRIVATE UltraCanvasVectorPlugin)
    endif()
    target_compile_definitions(UltraDBAdmin PRIVATE ULTRADBADMIN_WEB=1 ULTRADBADMIN_NO_VAULT=1)
    # The page: UltraDBAdmin.html, its fonts, icons and app icon preloaded at
    # /share/ (GetResourcesDir() in the browser), the browser's WebSocket for
    # the relay, and a shell that fills the viewport.
    set_target_properties(UltraDBAdmin PROPERTIES SUFFIX ".html")
    # (and the one framework icon it shows: the tab row's overflow arrow)
    foreach(_dba_preload media/fonts media/icons/dbadmin media/icons/arrow_down_solid.svg
                         media/appicon/UltraDBAdmin.png)
        target_link_options(UltraDBAdmin PRIVATE
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/${_dba_preload}@/share/${_dba_preload}")
    endforeach()
    unset(_dba_preload)
    target_link_options(UltraDBAdmin PRIVATE
        -lwebsocket.js
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    set_property(TARGET UltraDBAdmin APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
else()
    target_link_libraries(UltraDBAdmin PRIVATE UltraDatabase UltraVault ${ULTRACANVAS_LIBRARY})
endif()
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
    # The OpenID Connect sign-in ([signin], relay/RelaySignIn.h): UltraNet
    # for the provider's endpoints and its OAuth2 helpers, UltraCrypt for the
    # ID token's RS256 / ES256 signature (UltraCryptPublicKey.h) - which it
    # checks only where UltraCrypt has its OpenSSL 3 backend. Without them the
    # relay takes tokens only.
    if(TARGET UltraNet AND ULTRACANVAS_CRYPT_HAS_OPENSSL AND TARGET uc-yyjson)
        target_sources(ultradbadmin-relay PRIVATE relay/RelaySignIn.cpp)
        target_link_libraries(ultradbadmin-relay PRIVATE UltraNet UltraCrypt uc-yyjson)
        target_compile_definitions(ultradbadmin-relay PRIVATE ULTRADBADMIN_RELAY_SIGNIN=1)
        message(STATUS "  [✓] ultradbadmin-relay sign-in (OpenID Connect) - ENABLED")
    else()
        message(STATUS "  [−] ultradbadmin-relay sign-in - DISABLED (needs UltraNet and UltraCrypt with OpenSSL 3)")
    endif()
    install(TARGETS ultradbadmin-relay RUNTIME DESTINATION bin)
    message(STATUS "  [✓] ultradbadmin-relay - ENABLED (target: ultradbadmin-relay)")
endif()

message(STATUS "  [✓] UltraDBAdmin app - ENABLED (target: UltraDBAdmin)")
message(STATUS "      Version ${ULTRADBADMIN_VERSION} (Docs/UltraDBAdmin/CHANGELOG.md)")
