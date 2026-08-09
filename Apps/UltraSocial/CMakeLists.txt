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
    engine/connectors/UltraSocialMastodonConnector.cpp
    engine/connectors/UltraSocialBlueskyConnector.cpp
    engine/connectors/UltraSocialTelegramConnector.cpp
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
