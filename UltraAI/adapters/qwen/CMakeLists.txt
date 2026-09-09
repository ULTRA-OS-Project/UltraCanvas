# UltraAI/adapters/qwen/CMakeLists.txt
# Qwen adapter for locally served OpenAI-compatible endpoints (Ollama,
# vLLM, llama.cpp server, LM Studio). It delegates the wire protocol to the
# OpenAI adapter, so it is skipped when that adapter is not in the build.

if(NOT TARGET UltraAI_AdapterOpenAI)
    message(STATUS "UltraAI: Qwen adapter skipped — it delegates to the "
                   "OpenAI adapter, which is not enabled "
                   "(ULTRAAI_ADAPTER_OPENAI=OFF or its own probe failed).")
    return()
endif()

set(_ULTRAAI_NLOHMANN_DIR "")
foreach(_candidate
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../UltraCanvas/third_party")
    if(EXISTS "${_candidate}/nlohmann/json.hpp")
        set(_ULTRAAI_NLOHMANN_DIR "${_candidate}")
        break()
    endif()
endforeach()

if(NOT _ULTRAAI_NLOHMANN_DIR)
    message(STATUS "UltraAI: Qwen adapter skipped — vendored "
                   "nlohmann/json.hpp not found (monorepo layout required).")
    return()
endif()

add_library(UltraAI_AdapterQwen STATIC
    src/QwenAdapters.cpp
    src/QwenDiscovery.cpp
)
add_library(UltraAI::AdapterQwen ALIAS UltraAI_AdapterQwen)

target_include_directories(UltraAI_AdapterQwen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${_ULTRAAI_NLOHMANN_DIR}
)

target_link_libraries(UltraAI_AdapterQwen PUBLIC
    UltraAI
    UltraAI_AdapterShared
    UltraAI_AdapterOpenAI
)
target_compile_features(UltraAI_AdapterQwen PUBLIC cxx_std_20)

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
# Not in the UltraAITargets export set for the same reason as
# UltraAI_AdapterShared: its link interface reaches UltraNet, which has no
# export set of its own.
install(TARGETS UltraAI_AdapterQwen
    ARCHIVE DESTINATION lib
)
