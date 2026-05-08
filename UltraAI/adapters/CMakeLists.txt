# UltraAI/adapters/CMakeLists.txt
# Aggregator for capability adapters. Each adapter is independently
# opt-in via its ULTRAAI_ADAPTER_<NAME> option.

if(ULTRAAI_ADAPTER_MOCK)
    add_subdirectory(mock)
endif()

# Real adapters are added here as they land:
# if(ULTRAAI_ADAPTER_ANTHROPIC) add_subdirectory(anthropic) endif()
# if(ULTRAAI_ADAPTER_OPENAI)    add_subdirectory(openai)    endif()
# if(ULTRAAI_ADAPTER_LLAMACPP)  add_subdirectory(llamacpp)  endif()
# if(ULTRAAI_ADAPTER_WHISPER)   add_subdirectory(whisper)   endif()
# if(ULTRAAI_ADAPTER_PIPER)     add_subdirectory(piper)     endif()
