# UltraAI/adapters/CMakeLists.txt
# Aggregator for capability adapters. Each adapter is independently
# opt-in via its ULTRAAI_ADAPTER_<NAME> option.

# Shared infrastructure for network adapters — always built.
add_subdirectory(_shared)

if(ULTRAAI_ADAPTER_MOCK)
    add_subdirectory(mock)
endif()

if(ULTRAAI_ADAPTER_ANTHROPIC)
    add_subdirectory(anthropic)
endif()

if(ULTRAAI_ADAPTER_OPENAI)
    add_subdirectory(openai)
endif()

if(ULTRAAI_ADAPTER_LLAMACPP)
    add_subdirectory(llamacpp)
endif()

if(ULTRAAI_ADAPTER_MINIMAX)
    add_subdirectory(minimax)
endif()

if(ULTRAAI_ADAPTER_ELEVENLABS)
    add_subdirectory(elevenlabs)
endif()

if(ULTRAAI_ADAPTER_QWEN)
    add_subdirectory(qwen)
endif()

if(ULTRAAI_ADAPTER_COMFYUI)
    add_subdirectory(comfyui)
endif()

# The local runtime (engines and models downloaded on demand, run as
# supervised processes) and the `local` provider on it. The runtime needs
# UltraAI_AdapterShared and the siblings of the shared core; the adapter
# needs the OpenAI one. Each skips itself with a message when it cannot
# build.
if(ULTRAAI_LOCAL_RUNTIME)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../runtime" "${CMAKE_CURRENT_BINARY_DIR}/runtime")
    add_subdirectory(local)
endif()

# Real adapters are added here as they land:
# if(ULTRAAI_ADAPTER_WHISPER)   add_subdirectory(whisper)   endif()
# if(ULTRAAI_ADAPTER_PIPER)     add_subdirectory(piper)     endif()
