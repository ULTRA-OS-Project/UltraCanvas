# Apps/UltraMail/CMakeLists.txt
# UltraMail — the ULTRA OS mail application. Phase 1 builds the headless engine
# library (account / folder / message store on UltraDatabase); the UI targets
# (Toolbox, account wizard, main window) are added on top in later phases.
# Version: 0.1.0 (Phase 1)
# Author: UltraCanvas Framework / ULTRA OS

# The engine depends only on UltraDatabase, so it builds and tests headlessly.
if(NOT TARGET UltraDatabase)
    message(STATUS "  [−] UltraMail engine - SKIPPED (UltraDatabase not built)")
    return()
endif()

set(ULTRAMAIL_ENGINE_SOURCES
    engine/UltraMailTypes.cpp
    engine/UltraMailLocalStore.cpp
    engine/UltraMailMimeCodec.cpp
    engine/UltraMailAttachmentCache.cpp
    engine/UltraMailContacts.cpp
    engine/UltraMailContactStore.cpp
    engine/UltraMailSyncEngine.cpp
    engine/UltraMailDiscovery.cpp
    engine/UltraMailCredentialVault.cpp
)

add_library(UltraMailEngine STATIC ${ULTRAMAIL_ENGINE_SOURCES})
set_target_properties(UltraMailEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultramail_engine")
target_compile_features(UltraMailEngine PUBLIC cxx_std_20)
target_include_directories(UltraMailEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(UltraMailEngine PUBLIC UltraDatabase)
# UltraNet provides the MIME codec (UltraNet_MimeParse) the engine builds on.
if(TARGET UltraNet)
    target_link_libraries(UltraMailEngine PUBLIC UltraNet)
endif()

message(STATUS "  [✓] UltraMail engine - ENABLED (target: UltraMailEngine)")
