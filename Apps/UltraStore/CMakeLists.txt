# Apps/UltraStore/CMakeLists.txt
# UltraStore - the ULTRA OS app store: lists the apps from a signed catalog,
# downloads the right build for the machine, verifies it, installs it per user
# and keeps it updated (Docs/UltraStore/README.md).
#
# Two targets:
#   UltraStoreCore   a static library: the platform, the catalog, the verifier,
#                    the resolver and the catalog client. Links the UltraCanvas
#                    core for UltraCanvasJSON, UltraNet and UltraCrypt but opens
#                    no window, so the command line and the tests use it headless.
#   ultrastore       the program: the commands now, the window later (one
#                    program, since a case-insensitive file system cannot tell
#                    `ultrastore` from `UltraStore`).
#
# The store requires what it is built on: UltraCrypt with libsodium (the
# catalog signature) and UltraDatabase with SQLite (the registry of what is
# installed). Without either the configure stops, rather than building a store
# that cannot verify or cannot remember: -DBUILD_ULTRASTORE=OFF builds the
# rest of the tree without it.
#
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

add_library(UltraStoreCore STATIC
    core/UltraStoreTypes.cpp
    core/PlatformInfo.cpp
    core/Catalog.cpp
    core/Verifier.cpp
    core/PackageResolver.cpp
    core/CatalogClient.cpp
)
set_target_properties(UltraStoreCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrastore_core")
target_compile_features(UltraStoreCore PUBLIC cxx_std_20)
target_include_directories(UltraStoreCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_include_directories(UltraStoreCore PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
# The module homes resolve to the shared core, or to their archives under a
# static one; UltraCanvasJSON lives in the core either way.
target_link_libraries(UltraStoreCore PUBLIC ${ULTRACANVAS_LIBRARY} UltraNet UltraCrypt UltraDatabase)
message(STATUS "  [✓] UltraStore core - ENABLED")

add_executable(ultrastore cli/main.cpp)
target_include_directories(ultrastore PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(ultrastore PRIVATE UltraStoreCore)
target_compile_features(ultrastore PRIVATE cxx_std_20)
# The version comes from the first line of Docs/UltraStore/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake - never a literal of its own.
target_compile_definitions(ultrastore PRIVATE ULTRASTORE_VERSION="${ULTRASTORE_VERSION}")
set_target_properties(ultrastore PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ultrastore RUNTIME DESTINATION bin)
endif()
message(STATUS "  [✓] ultrastore (command line) - ENABLED")
