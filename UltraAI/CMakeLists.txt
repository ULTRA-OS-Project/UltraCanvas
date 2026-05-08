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
option(ULTRAAI_BUILD_ADAPTERS    "Build provider adapter implementations" OFF)
option(ULTRAAI_ADAPTER_OPENAI    "Build OpenAI adapter"                   OFF)
option(ULTRAAI_ADAPTER_ANTHROPIC "Build Anthropic adapter"                OFF)
option(ULTRAAI_ADAPTER_LLAMACPP  "Build local llama.cpp adapter"          OFF)
option(ULTRAAI_ADAPTER_WHISPER   "Build local whisper.cpp STT adapter"    OFF)
option(ULTRAAI_ADAPTER_PIPER     "Build local Piper TTS adapter"          OFF)
option(ULTRAAI_BUILD_TESTS       "Build UltraAI unit tests"               OFF)

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
    include/UltraAITextLLM.h
    include/UltraAIEmbeddings.h
    include/UltraAISpeechToText.h
    include/UltraAITextToSpeech.h
    include/UltraAIImageGen.h
    include/UltraAIVisionAnalyzer.h
)

# ===== Adapters (placeholders — implementations to follow) =====
if(ULTRAAI_BUILD_ADAPTERS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/adapters/CMakeLists.txt")
        add_subdirectory(adapters)
    else()
        message(STATUS "UltraAI: ULTRAAI_BUILD_ADAPTERS=ON but adapters/ not present yet.")
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

install(TARGETS UltraAI
    EXPORT UltraAITargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT UltraAITargets
    FILE UltraAITargets.cmake
    NAMESPACE UltraAI::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UltraAI
)

message(STATUS "UltraAI ${PROJECT_VERSION} configured (header-only interface)")
