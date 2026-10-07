# Apps/UltraSocial/CMakeLists.txt
# UltraSocial — cross-posting to social networks (Docs/UltraSocial/Concept.md).
# Phase 1 builds the headless engine library: account store on UltraDatabase,
# credential vault, compose-once/adapt-per-network composer, and the
# Mastodon / Bluesky / Telegram connectors over UltraNet (+ UltraNetOAuth2).
# The GUI application arrives in a later phase on top of this target.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

# The engine needs UltraDatabase (store), UltraNet (HTTP/OAuth2), and the
# UltraCanvas core for UltraCanvasJSON.
if(NOT TARGET UltraDatabase)
    message(STATUS "  [−] UltraSocial engine - SKIPPED (UltraDatabase not built)")
    return()
endif()
if(NOT TARGET UltraNet)
    message(STATUS "  [−] UltraSocial engine - SKIPPED (UltraNet not built)")
    return()
endif()

set(ULTRASOCIAL_ENGINE_SOURCES
    engine/UltraSocialTypes.cpp
    engine/UltraSocialComposer.cpp
    engine/UltraSocialStore.cpp
    engine/UltraSocialWebUtil.cpp
    engine/UltraSocialConnector.cpp
    engine/UltraSocialPublisher.cpp
    engine/UltraSocialPaths.cpp
    engine/connectors/UltraSocialMastodonConnector.cpp
    engine/connectors/UltraSocialBlueskyConnector.cpp
    engine/connectors/UltraSocialTelegramConnector.cpp
    engine/connectors/UltraSocialRedditConnector.cpp
    engine/connectors/UltraSocialXConnector.cpp
    engine/connectors/UltraSocialLinkedInConnector.cpp
    engine/connectors/UltraSocialFacebookConnector.cpp
)

add_library(UltraSocialEngine STATIC ${ULTRASOCIAL_ENGINE_SOURCES})
set_target_properties(UltraSocialEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrasocial_engine")
target_compile_features(UltraSocialEngine PUBLIC cxx_std_20)
target_include_directories(UltraSocialEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(UltraSocialEngine PUBLIC UltraDatabase)
# Account credentials live in UltraVault's device-key vault (Argon2id +
# XChaCha20-Poly1305 via UltraCrypt), not in a format of UltraSocial's own.
target_link_libraries(UltraSocialEngine PUBLIC UltraVault)

# UltraCanvasJSON lives in the UltraCanvas core library; UltraNet supplies
# HTTP/OAuth2/sockets. With a SHARED core, UltraNet is absorbed into (and
# exported by) libultracanvas — link that alone and take UltraNet's headers
# via its interface, mirroring UltraMailEngine's link logic.
set(_uc_core_shared FALSE)
if(TARGET ${ULTRACANVAS_LIBRARY})
    get_target_property(_uc_core_type ${ULTRACANVAS_LIBRARY} TYPE)
    if(_uc_core_type STREQUAL "SHARED_LIBRARY")
        set(_uc_core_shared TRUE)
    endif()
endif()
if(_uc_core_shared)
    target_link_libraries(UltraSocialEngine PUBLIC ${ULTRACANVAS_LIBRARY})
    target_include_directories(UltraSocialEngine PUBLIC
        $<TARGET_PROPERTY:UltraNet,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    target_link_libraries(UltraSocialEngine PUBLIC UltraNet)
    if(TARGET ${ULTRACANVAS_LIBRARY})
        target_link_libraries(UltraSocialEngine PUBLIC ${ULTRACANVAS_LIBRARY})
    endif()
endif()
# DataFormats/UltraCanvasJSON.h include root.
target_include_directories(UltraSocialEngine PUBLIC
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

message(STATUS "  [✓] UltraSocial engine - ENABLED (target: UltraSocialEngine)")

# ── UltraSocial GUI application ──────────────────────────────────────────────
# The compose window + account wizard on top of the engine. Needs the
# UltraCanvas UI library, built by add_subdirectory(UltraCanvas) before this
# directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS} are in
# scope and carry the platform deps as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraSocial GUI app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

set(ULTRASOCIAL_UI_SOURCES
    ui/UltraSocialApp.cpp
    ui/UltraSocialAccountWizard.cpp
    ui/UltraSocialComposeView.cpp
    ui/UltraSocialStartPage.cpp
)

add_executable(UltraSocial main.cpp ${ULTRASOCIAL_UI_SOURCES})
target_include_directories(UltraSocial PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
# Engine before the core: the engine (static) pulls UltraNet_* / JSON symbols
# from the core library, and GNU ld resolves left-to-right, so the provider
# must follow the consumer on the link line.
target_link_libraries(UltraSocial PRIVATE UltraSocialEngine ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraSocial PRIVATE fmt::fmt)
endif()
target_compile_features(UltraSocial PRIVATE cxx_std_20)
# The version shown in the window title: the first line of
# Docs/UltraSocial/CHANGELOG.md (cmake/UltraCanvasVersion.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasVersion.cmake")
# One icon everywhere the app is drawn: main.cpp names this same file for the
# window and taskbar icon; UCAPP_ICON_PATH is the fallback the core applies
# when an app sets no icon of its own. The PNG is the 256 px render of
# media/appicon/UltraSocial.svg.
target_compile_definitions(UltraSocial PRIVATE
        ULTRASOCIAL_VERSION="${ULTRASOCIAL_VERSION}"
        UCAPP_ICON_PATH="media/appicon/UltraSocial.png")

# Embed the app icon in the .exe (Windows only): Explorer and the Windows
# taskbar read it off the binary, not from the window.
ultracanvas_embed_app_icon(UltraSocial
        "${CMAKE_SOURCE_DIR}/media/appicon/UltraSocial.png")

# The freedesktop shortcut and the icon it names (`Icon=UltraSocial` is
# resolved through the installed icon themes).
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraSocial RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraSocial.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraSocial.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraSocial.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UltraSocial GUI app - ENABLED (target: UltraSocial)")
