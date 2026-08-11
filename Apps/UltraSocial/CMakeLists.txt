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
    engine/UltraSocialCredentialVault.cpp
    engine/UltraSocialStore.cpp
    engine/UltraSocialWebUtil.cpp
    engine/UltraSocialConnector.cpp
    engine/UltraSocialPublisher.cpp
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
message(STATUS "  [✓] UltraSocial GUI app - ENABLED (target: UltraSocial)")
