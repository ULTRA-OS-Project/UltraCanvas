# UltraAI/tests/CMakeLists.txt
# Lightweight assert-based tests for the UltraAI module. No external test
# framework required so the module stays self-contained.

add_executable(ultraai_test_mock_textllm test_mock_textllm.cpp)
target_link_libraries(ultraai_test_mock_textllm PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_mock_textllm PRIVATE cxx_std_20)
add_test(NAME ultraai.mock_textllm COMMAND ultraai_test_mock_textllm)

add_executable(ultraai_test_mock_capabilities test_mock_capabilities.cpp)
target_link_libraries(ultraai_test_mock_capabilities PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_mock_capabilities PRIVATE cxx_std_20)
add_test(NAME ultraai.mock_capabilities COMMAND ultraai_test_mock_capabilities)

add_executable(ultraai_test_mock_more_capabilities test_mock_more_capabilities.cpp)
target_link_libraries(ultraai_test_mock_more_capabilities PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_mock_more_capabilities PRIVATE cxx_std_20)
add_test(NAME ultraai.mock_more_capabilities COMMAND ultraai_test_mock_more_capabilities)

add_executable(ultraai_test_routing test_routing.cpp)
target_link_libraries(ultraai_test_routing PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_routing PRIVATE cxx_std_20)
add_test(NAME ultraai.routing COMMAND ultraai_test_routing)

add_executable(ultraai_test_adapter_shared test_adapter_shared.cpp)
target_link_libraries(ultraai_test_adapter_shared PRIVATE UltraAI_AdapterShared)
target_compile_features(ultraai_test_adapter_shared PRIVATE cxx_std_20)
add_test(NAME ultraai.adapter_shared COMMAND ultraai_test_adapter_shared)

# Cassette support exists only when the vendored nlohmann/json was found
# (same probe as _shared/CMakeLists.txt).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party/nlohmann/json.hpp")
    add_executable(ultraai_test_cassette test_cassette.cpp)
    target_link_libraries(ultraai_test_cassette PRIVATE UltraAI_AdapterShared)
    target_compile_features(ultraai_test_cassette PRIVATE cxx_std_20)
    add_test(NAME ultraai.cassette COMMAND ultraai_test_cassette)
endif()

if(TARGET UltraAI_AdapterLlama)
    add_executable(ultraai_test_llamacpp_adapter test_llamacpp_adapter.cpp)
    target_link_libraries(ultraai_test_llamacpp_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterLlama)
    target_compile_features(ultraai_test_llamacpp_adapter PRIVATE cxx_std_20)
    # Inference checks self-skip unless ULTRAAI_TEST_LLAMA_MODEL points at a
    # GGUF file.
    add_test(NAME ultraai.llamacpp_adapter COMMAND ultraai_test_llamacpp_adapter)
endif()

if(TARGET UltraAI_AdapterAnthropic)
    add_executable(ultraai_test_anthropic_adapter test_anthropic_adapter.cpp)
    target_link_libraries(ultraai_test_anthropic_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterAnthropic)
    # The test inspects recorded request bodies with the same vendored
    # nlohmann/json the adapter serializes with.
    target_include_directories(ultraai_test_anthropic_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_anthropic_adapter PRIVATE cxx_std_20)
    add_test(NAME ultraai.anthropic_adapter COMMAND ultraai_test_anthropic_adapter)
endif()

if(TARGET UltraAI_AdapterOpenAI)
    add_executable(ultraai_test_openai_adapter test_openai_adapter.cpp)
    target_link_libraries(ultraai_test_openai_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterOpenAI)
    # The test inspects recorded request bodies with the same vendored
    # nlohmann/json the adapter serializes with.
    target_include_directories(ultraai_test_openai_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_openai_adapter PRIVATE cxx_std_20)
    add_test(NAME ultraai.openai_adapter COMMAND ultraai_test_openai_adapter)
endif()

if(TARGET UltraAI_AdapterMiniMax)
    add_executable(ultraai_test_minimax_adapter test_minimax_adapter.cpp)
    target_link_libraries(ultraai_test_minimax_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterMiniMax)
    # The test inspects recorded request bodies with the same vendored
    # nlohmann/json the adapter serializes with.
    target_include_directories(ultraai_test_minimax_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_minimax_adapter PRIVATE cxx_std_20)
    add_test(NAME ultraai.minimax_adapter COMMAND ultraai_test_minimax_adapter)
endif()

if(TARGET UltraAI_AdapterQwen)
    add_executable(ultraai_test_qwen_adapter test_qwen_adapter.cpp)
    target_link_libraries(ultraai_test_qwen_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterQwen)
    target_include_directories(ultraai_test_qwen_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_qwen_adapter PRIVATE cxx_std_20)
    add_test(NAME ultraai.qwen_adapter COMMAND ultraai_test_qwen_adapter)
endif()

if(TARGET UltraAI_AdapterComfyUI)
    add_executable(ultraai_test_comfyui_adapter test_comfyui_adapter.cpp)
    target_link_libraries(ultraai_test_comfyui_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterComfyUI)
    target_include_directories(ultraai_test_comfyui_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_comfyui_adapter PRIVATE cxx_std_20)
    add_test(NAME ultraai.comfyui_adapter COMMAND ultraai_test_comfyui_adapter)
endif()
