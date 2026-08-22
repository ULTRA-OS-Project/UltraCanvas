# UltraAI/CMakeLists.txt
# UltraAI module — a sibling of UltraCanvas providing AI capabilities
# (LLM, embeddings, speech-to-text, text-to-speech, image generation,
# vision analysis) behind provider-agnostic interfaces.
# Version: 0.1.0
# Last Modified: 2026-05-08
# Author: UltraAI Module

cmake_minimum_required(VERSION 3.16)

project(UltraAI
    VERSION 0.1.0
    DESCRIPTION "UltraAI — provider-agnostic AI capability module for UltraOS"
    LANGUAGES CXX
)

# ===== Build options =====
option(ULTRAAI_BUILD_ADAPTERS    "Build provider adapter implementations" ON)
option(ULTRAAI_ADAPTER_MOCK      "Build in-process mock adapter"          ON)
option(ULTRAAI_ADAPTER_OPENAI    "Build OpenAI adapter"                   ON)
option(ULTRAAI_ADAPTER_ANTHROPIC "Build Anthropic adapter"                ON)
option(ULTRAAI_ADAPTER_LLAMACPP  "Build local llama.cpp adapter"          OFF)
option(ULTRAAI_ADAPTER_WHISPER   "Build local whisper.cpp STT adapter"    OFF)
option(ULTRAAI_ADAPTER_PIPER     "Build local Piper TTS adapter"          OFF)
option(ULTRAAI_BUILD_TESTS       "Build UltraAI unit tests"               OFF)

# Sibling-module integration (see Docs/UltraNetIntegration.md, Docs/UltraVault.md).
# Default OFF so the module builds cleanly when only mock adapters are present.
option(ULTRAAI_USE_ULTRANET   "Enable adapters that depend on UltraNet"   OFF)
option(ULTRAAI_USE_ULTRAVAULT "Enable UltraVault credential lookup"       OFF)

# ===== Header-only interface library =====
# The module ships its capability interfaces as a header-only INTERFACE
# target. Adapters depend on this target and add their own translation units.
add_library(UltraAI INTERFACE)
add_library(UltraAI::UltraAI ALIAS UltraAI)

target_include_directories(UltraAI INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(UltraAI INTERFACE cxx_std_20)

set(ULTRAAI_PUBLIC_HEADERS
    include/UltraAI.h
    include/UltraAICommon.h
    include/UltraAIRouting.h
    include/UltraAITextLLM.h
    include/UltraAIEmbeddings.h
    include/UltraAISpeechToText.h
    include/UltraAITextToSpeech.h
    include/UltraAIImageGen.h
    include/UltraAIVisionAnalyzer.h
    include/UltraAITranslator.h
    include/UltraAIVideoGen.h
    include/UltraAIMusicGen.h
    include/UltraAICodeAssist.h
)

# ===== Core static library =====
# Holds the factory / registry for capability interfaces. Adapters extend
# the registry by linking into this library (or registering at runtime).
add_library(UltraAI_Core STATIC
    src/TextLLMFactory.cpp
    src/CapabilityFactories.cpp
    src/Routing.cpp
)
add_library(UltraAI::Core ALIAS UltraAI_Core)

target_link_libraries(UltraAI_Core PUBLIC UltraAI)
target_compile_features(UltraAI_Core PUBLIC cxx_std_20)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(UltraAI_Core PUBLIC Threads::Threads)
endif()

# ===== Sibling-module probes (do not fail the build if missing) =====
if(ULTRAAI_USE_ULTRANET)
    if(NOT TARGET UltraNet)
        find_package(UltraNet QUIET)
    endif()
    if(TARGET UltraNet)
        message(STATUS "UltraAI: UltraNet target found; network adapters may link against it.")
        set(ULTRAAI_HAS_ULTRANET TRUE)
    else()
        message(STATUS "UltraAI: ULTRAAI_USE_ULTRANET=ON but no 'UltraNet' target visible. "
                       "Network adapters that need it will be skipped. The parent project "
                       "is expected to add_subdirectory(UltraNet) before add_subdirectory(UltraAI).")
        set(ULTRAAI_HAS_ULTRANET FALSE)
    endif()
endif()

if(ULTRAAI_USE_ULTRAVAULT)
    if(NOT TARGET UltraVault)
        find_package(UltraVault QUIET)
    endif()
    if(TARGET UltraVault)
        message(STATUS "UltraAI: UltraVault target found; credential lookup enabled.")
        target_link_libraries(UltraAI_Core PUBLIC UltraVault)
        target_compile_definitions(UltraAI_Core PUBLIC ULTRAAI_HAS_ULTRAVAULT=1)
        set(ULTRAAI_HAS_ULTRAVAULT TRUE)
    else()
        message(STATUS "UltraAI: ULTRAAI_USE_ULTRAVAULT=ON but no 'UltraVault' target visible. "
                       "Credential resolution falls back to ProviderConfig::apiKey only.")
    endif()
endif()

# ===== Adapters =====
if(ULTRAAI_BUILD_ADAPTERS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/adapters/CMakeLists.txt")
        add_subdirectory(adapters)
    else()
        message(STATUS "UltraAI: ULTRAAI_BUILD_ADAPTERS=ON but adapters/ not present yet.")
    endif()

    # Adapters declared above register themselves with UltraAI_Core via
    # ULTRAAI_HAS_<NAME>_ADAPTER compile definitions.
    if(TARGET UltraAI_AdapterMock)
        target_link_libraries(UltraAI_Core PUBLIC UltraAI_AdapterMock)
        target_compile_definitions(UltraAI_Core PRIVATE ULTRAAI_HAS_MOCK_ADAPTER=1)
    endif()
    if(TARGET UltraAI_AdapterAnthropic)
        target_link_libraries(UltraAI_Core PUBLIC UltraAI_AdapterAnthropic)
        target_compile_definitions(UltraAI_Core PRIVATE ULTRAAI_HAS_ANTHROPIC_ADAPTER=1)
    endif()
    if(TARGET UltraAI_AdapterOpenAI)
        target_link_libraries(UltraAI_Core PUBLIC UltraAI_AdapterOpenAI)
        target_compile_definitions(UltraAI_Core PRIVATE ULTRAAI_HAS_OPENAI_ADAPTER=1)
    endif()
    if(TARGET UltraAI_AdapterLlama)
        target_link_libraries(UltraAI_Core PUBLIC UltraAI_AdapterLlama)
        target_compile_definitions(UltraAI_Core PRIVATE ULTRAAI_HAS_LLAMACPP_ADAPTER=1)
    endif()
endif()

# ===== Tests =====
if(ULTRAAI_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

# ===== Install =====
include(GNUInstallDirs)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Only the header-only interface target is exported: UltraAI_Core's link
# interface reaches adapter libraries (and through them possibly UltraNet),
# none of which have export sets — CMake rejects an export referencing a
# non-exported target. Core and the adapters install as plain archives and
# are consumed in-tree.
install(TARGETS UltraAI
    EXPORT UltraAITargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(TARGETS UltraAI_Core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(EXPORT UltraAITargets
    FILE UltraAITargets.cmake
    NAMESPACE UltraAI::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UltraAI
)

message(STATUS "UltraAI ${PROJECT_VERSION} configured (header-only interface)")
