# Apps/UltraStore/CMakeLists.txt
# UltraStore - the ULTRA OS app store: lists the apps from a signed catalog,
# downloads the right build for the machine, verifies it, installs it per user
# and keeps it updated (Docs/UltraStore/README.md).
#
# Two targets:
#   UltraStoreCore   a static library: the platform, the catalog, the verifier,
#                    the resolver, the catalog client, and UltraInstaller (the
#                    downloader, the registry on UltraDatabase, the archive
#                    installer, the engine). Links the UltraCanvas core for
#                    UltraCanvasJSON and the VirtualFS bridge, UltraNet,
#                    UltraCrypt and UltraDatabase, but opens no window, so the
#                    command line and the tests use it headless.
#   ultrastore       the program: the window when started plainly, the
#                    commands when given one (one program, since a
#                    case-insensitive file system cannot tell `ultrastore`
#                    from `UltraStore`).
#
# The store requires what it is built on: UltraCrypt with libsodium (the
# catalog signature) and UltraDatabase with SQLite (the registry of what is
# installed). Without either the configure stops, rather than building a store
# that cannot verify or cannot remember: -DBUILD_ULTRASTORE=OFF builds the
# rest of the tree without it.
#
# Version: 0.3.0 - the window; built-in keys and servers (UltraStoreBuiltin.h)
# Version: 0.2.0 - UltraInstaller
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraStore - SKIPPED (UltraCanvas core library not built)")
    return()
endif()

if(NOT TARGET UltraCrypt OR NOT TARGET uc-crypt)
    message(FATAL_ERROR "UltraStore requires UltraCrypt; configure the core with it or pass -DBUILD_ULTRASTORE=OFF")
endif()
get_target_property(_ultrastore_crypt_defs uc-crypt INTERFACE_COMPILE_DEFINITIONS)
if(NOT _ultrastore_crypt_defs MATCHES "ULTRACRYPT_HAVE_SODIUM")
    message(FATAL_ERROR
        "UltraStore requires UltraCrypt with libsodium (the catalog signature is checked with it). "
        "Install libsodium's development package (libsodium-dev / libsodium / mingw-w64-clang-x86_64-libsodium) "
        "and configure again, or pass -DBUILD_ULTRASTORE=OFF.")
endif()
if(NOT TARGET UltraDatabase)
    message(FATAL_ERROR
        "UltraStore requires UltraDatabase with SQLite (the registry of installed apps). "
        "Install SQLite's development package (libsqlite3-dev / sqlite / mingw-w64-clang-x86_64-sqlite3) "
        "and configure again, or pass -DBUILD_ULTRASTORE=OFF.")
endif()

# What the build compiles in: every public key under keys/ (one per *.pub
# file, 64 hex digits) and the catalog servers named in ULTRASTORE_SERVERS,
# as initializer lists in a generated header. Both are empty until the admin
# backend publishes the first catalog; the window says so.
set(ULTRASTORE_SERVERS "" CACHE STRING
    "Catalog base URLs compiled into ultrastore, semicolon-separated (empty: the user enters one)")
file(GLOB _ultrastore_key_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/keys/*.pub")
list(SORT _ultrastore_key_files)
set(ULTRASTORE_BUILTIN_KEYS_INIT "")
foreach(_key_file IN LISTS _ultrastore_key_files)
    file(STRINGS "${_key_file}" _key_lines REGEX "^[0-9a-fA-F]+$")
    list(LENGTH _key_lines _key_count)
    if(_key_count EQUAL 0)
        message(FATAL_ERROR "UltraStore: ${_key_file} holds no hex key line")
    endif()
    list(GET _key_lines 0 _key_hex)
    string(TOLOWER "${_key_hex}" _key_hex)
    string(LENGTH "${_key_hex}" _key_len)
    if(NOT _key_len EQUAL 64)
        message(FATAL_ERROR "UltraStore: ${_key_file} is not a 64-digit hex public key")
    endif()
    string(APPEND ULTRASTORE_BUILTIN_KEYS_INIT "\"${_key_hex}\", ")
endforeach()
set(ULTRASTORE_BUILTIN_SERVERS_INIT "")
foreach(_server IN LISTS ULTRASTORE_SERVERS)
    string(APPEND ULTRASTORE_BUILTIN_SERVERS_INIT "\"${_server}\", ")
endforeach()
configure_file(core/UltraStoreBuiltin.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/UltraStoreBuiltin.h" @ONLY)

add_library(UltraStoreCore STATIC
    core/UltraStoreTypes.cpp
    core/PlatformInfo.cpp
    core/Catalog.cpp
    core/Verifier.cpp
    core/PackageResolver.cpp
    core/CatalogClient.cpp
    core/Registry.cpp
    core/Downloader.cpp
    core/Installer.cpp
    core/StoreEngine.cpp
    core/StoreQuery.cpp
    core/AssetCache.cpp
    core/StoreSettings.cpp
    core/Purchase.cpp
)
set_target_properties(UltraStoreCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrastore_core")
target_compile_features(UltraStoreCore PUBLIC cxx_std_20)
target_include_directories(UltraStoreCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_include_directories(UltraStoreCore PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
# The module homes resolve to the shared core, or to their archives under a
# static one; UltraCanvasJSON lives in the core either way.
target_link_libraries(UltraStoreCore PUBLIC ${ULTRACANVAS_LIBRARY} UltraNet UltraCrypt UltraDatabase)
message(STATUS "  [✓] UltraStore core - ENABLED")

# The program: the window (ui/) when started plainly, the commands (cli/)
# when given one. The window needs the UI library; the core requires it anyway
# (UltraCanvasJSON, the VirtualFS bridge), so there is no commands-only build.
add_executable(ultrastore
    ui/main.cpp
    ui/StoreWindow.cpp
    ui/StoreJobs.cpp
    ui/StoreSettingsPage.cpp
    cli/Commands.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(ultrastore)
target_include_directories(ultrastore PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(ultrastore PRIVATE UltraStoreCore ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(ultrastore PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(ultrastore PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(ultrastore PRIVATE cxx_std_20)
# The version comes from the first line of Docs/UltraStore/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake - never a literal of its own. One icon
# everywhere the app is drawn: main.cpp names the same file for the window,
# and the core falls back to UCAPP_ICON_PATH when an app sets none.
target_compile_definitions(ultrastore PRIVATE
    ULTRASTORE_VERSION="${ULTRASTORE_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraStore.png")
ultracanvas_embed_app_icon(ultrastore "${CMAKE_SOURCE_DIR}/media/appicon/UltraStore.png")
# A GUI program on Windows (no console beside the window), with ultrastore.com
# beside it so a prompt waits for `ultrastore installed` and sees its exit
# code (cmake/UltraCanvasWindowsGuiApp.cmake). Debug builds keep the console.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasWindowsGuiApp.cmake")
ultracanvas_windows_console_companion(ultrastore)
# Beside UltraCanvasStart in the build root (CMAKE_RUNTIME_OUTPUT_DIRECTORY),
# where the core's DLL is on Windows: under bin/ the program does not start
# there (exit 127 in CI, the DLL not found).
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ultrastore RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraStore.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraStore.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraStore.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()
message(STATUS "  [✓] ultrastore (window and command line) - ENABLED")
