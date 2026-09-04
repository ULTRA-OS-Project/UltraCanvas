# UltraCloud/CMakeLists.txt
# UltraCloud — cloud storage accounts, providers and share links for every
# ULTRA OS app. Builds the headless engine library (account store on
# UltraDatabase, secret stores, the provider registry, the WebDAV and
# Nextcloud providers over UltraNet HTTP) and, when the UltraCanvas UI library
# is available, the UltraCloudUI library with the shared dialogs (add account,
# pick a file / insert a link).
# Version: 0.2.0
# Last Modified: 2026-09-04
# Author: UltraCanvas Framework / ULTRA OS

if(NOT TARGET UltraDatabase)
    message(STATUS "  [−] UltraCloud - SKIPPED (UltraDatabase not built)")
    return()
endif()

set(ULTRACLOUD_SOURCES
    core/UltraCloudAccountStore.cpp
    core/UltraCloudSecrets.cpp
    core/UltraCloudRegistry.cpp
    core/UltraCloudService.cpp
    core/UltraCloudHttp.cpp
    core/UltraCloudOAuth.cpp
    providers/UltraCloudWebDav.cpp
    providers/UltraCloudNextcloud.cpp
    providers/UltraCloudMemory.cpp
    providers/UltraCloudDropbox.cpp
    providers/UltraCloudOneDrive.cpp
    providers/UltraCloudGoogleDrive.cpp
)

add_library(UltraCloud STATIC ${ULTRACLOUD_SOURCES})
set_target_properties(UltraCloud PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracloud")
target_compile_features(UltraCloud PUBLIC cxx_std_20)
target_include_directories(UltraCloud PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/include)
target_include_directories(UltraCloud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UltraCloud PUBLIC UltraDatabase)
if(NOT WIN32)
    target_link_libraries(UltraCloud PUBLIC ${CMAKE_DL_LIBS})   # provider plug-ins (dlopen)
endif()

# Secrets go to UltraVault when it is built; the file store is always there.
if(TARGET UltraVault)
    target_compile_definitions(UltraCloud PUBLIC ULTRACLOUD_USE_ULTRAVAULT=1)
    target_link_libraries(UltraCloud PUBLIC UltraVault)
endif()

# UltraNet (HTTP, URL and Base64 helpers) and UltraCanvasJSON: with a STATIC
# UltraCanvas we link the UltraNet archive directly and consumers add the core
# library; with a SHARED UltraCanvas both are exported by libultracanvas, so
# link that instead (same reasoning as Apps/UltraMail/CMakeLists.txt).
if(TARGET UltraNet)
    set(_uc_core_shared FALSE)
    if(TARGET ${ULTRACANVAS_LIBRARY})
        get_target_property(_uc_core_type ${ULTRACANVAS_LIBRARY} TYPE)
        if(_uc_core_type STREQUAL "SHARED_LIBRARY")
            set(_uc_core_shared TRUE)
        endif()
    endif()
    if(_uc_core_shared)
        target_link_libraries(UltraCloud PUBLIC ${ULTRACANVAS_LIBRARY})
        target_include_directories(UltraCloud PUBLIC
            $<TARGET_PROPERTY:UltraNet,INTERFACE_INCLUDE_DIRECTORIES>)
    else()
        # The core library follows UltraNet and UltraCloud on every consumer's
        # link line (UltraCanvasJSON lives there); naming it here makes CMake
        # repeat it after libultracloud.a even when the consumer already
        # linked it earlier.
        target_link_libraries(UltraCloud PUBLIC UltraNet)
        if(TARGET ${ULTRACANVAS_LIBRARY})
            target_link_libraries(UltraCloud PUBLIC ${ULTRACANVAS_LIBRARY})
        endif()
    endif()
endif()

message(STATUS "  [✓] UltraCloud - ENABLED (target: UltraCloud)")

# ── UltraCloudUI: the shared dialogs ─────────────────────────────────────────
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraCloudUI - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_library(UltraCloudUI STATIC
    ui/UltraCloudAccountDialog.cpp
    ui/UltraCloudPickerDialog.cpp)
set_target_properties(UltraCloudUI PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracloud_ui")
target_compile_features(UltraCloudUI PUBLIC cxx_std_20)
target_include_directories(UltraCloudUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraCloudUI PUBLIC UltraCloud ${ULTRACANVAS_LIBRARY})
message(STATUS "  [✓] UltraCloudUI - ENABLED (target: UltraCloudUI)")
