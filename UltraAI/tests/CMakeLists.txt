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

add_executable(ultraai_test_textllm_translator test_textllm_translator.cpp)
target_link_libraries(ultraai_test_textllm_translator PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_textllm_translator PRIVATE cxx_std_20)
add_test(NAME ultraai.textllm_translator COMMAND ultraai_test_textllm_translator)

add_executable(ultraai_test_agent test_agent.cpp)
target_link_libraries(ultraai_test_agent PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_agent PRIVATE cxx_std_20)
add_test(NAME ultraai.agent COMMAND ultraai_test_agent)

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
    add_test(NAME ultraai.llamacpp_adapter COMMAND ultraai_test_llamacpp_adapter)

    # The inference checks run on a model fixture make_tiny_llama_gguf.py
    # writes at build time with the gguf-py of the llama.cpp checkout the
    # adapter was built from (a random one-layer llama with a chat
    # template, ~115 KB), so `ctest` exercises the adapter for real with no
    # download. The script needs Python with numpy; without them the fixture
    # is not written and the test self-skips its inference checks, saying
    # so. ULTRAAI_TEST_LLAMA_MODEL, when set in the test's environment,
    # points at the fixture; run the binary by hand with the variable set
    # to try another GGUF.
    get_property(_ultraai_llama_src GLOBAL PROPERTY ULTRAAI_LLAMACPP_RESOLVED_SOURCE_DIR)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(_ultraai_llama_fixture_ok FALSE)
    if(Python3_Interpreter_FOUND AND EXISTS "${_ultraai_llama_src}/gguf-py/gguf/gguf_writer.py")
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy"
                        RESULT_VARIABLE _ultraai_numpy_rc OUTPUT_QUIET ERROR_QUIET)
        if(_ultraai_numpy_rc EQUAL 0)
            set(_ultraai_llama_fixture_ok TRUE)
        endif()
    endif()
    if(_ultraai_llama_fixture_ok)
        set(_ultraai_llama_fixture "${CMAKE_CURRENT_BINARY_DIR}/tiny-llama.gguf")
        add_custom_command(
            OUTPUT "${_ultraai_llama_fixture}"
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/make_tiny_llama_gguf.py"
                    --gguf-py "${_ultraai_llama_src}/gguf-py"
                    --output "${_ultraai_llama_fixture}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_tiny_llama_gguf.py"
            COMMENT "Writing the llama.cpp adapter test's model fixture"
            VERBATIM)
        add_custom_target(ultraai_tiny_llama_gguf ALL DEPENDS "${_ultraai_llama_fixture}")
        add_dependencies(ultraai_test_llamacpp_adapter ultraai_tiny_llama_gguf)
        set_tests_properties(ultraai.llamacpp_adapter PROPERTIES
            ENVIRONMENT "ULTRAAI_TEST_LLAMA_MODEL=${_ultraai_llama_fixture}")
    else()
        message(STATUS "UltraAI: no Python with numpy (or no gguf-py next to "
                       "the llama.cpp sources) - the llama.cpp adapter test "
                       "skips its inference checks")
    endif()
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

    # The OCR vision adapter on top of it, offline the same way.
    add_executable(ultraai_test_anthropic_vision test_anthropic_vision.cpp)
    target_link_libraries(ultraai_test_anthropic_vision PRIVATE
        UltraAI_Core UltraAI_AdapterAnthropic)
    target_include_directories(ultraai_test_anthropic_vision PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_anthropic_vision PRIVATE cxx_std_20)
    add_test(NAME ultraai.anthropic_vision COMMAND ultraai_test_anthropic_vision)
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

    # Speech to text over the audio API, offline the same way.
    add_executable(ultraai_test_openai_speech test_openai_speech.cpp)
    target_link_libraries(ultraai_test_openai_speech PRIVATE
        UltraAI_Core UltraAI_AdapterOpenAI)
    target_include_directories(ultraai_test_openai_speech PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_openai_speech PRIVATE cxx_std_20)
    add_test(NAME ultraai.openai_speech COMMAND ultraai_test_openai_speech)
endif()

# Batches: the fallback through the core, the native Anthropic and OpenAI
# batch formats through ScriptedTransport.
if(TARGET UltraAI_AdapterAnthropic AND TARGET UltraAI_AdapterOpenAI)
    add_executable(ultraai_test_batch test_batch.cpp)
    target_link_libraries(ultraai_test_batch PRIVATE
        UltraAI_Core UltraAI_AdapterAnthropic UltraAI_AdapterOpenAI)
    target_include_directories(ultraai_test_batch PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_batch PRIVATE cxx_std_20)
    add_test(NAME ultraai.batch COMMAND ultraai_test_batch)
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

if(TARGET UltraAI_AdapterElevenLabs)
    add_executable(ultraai_test_elevenlabs_adapter test_elevenlabs_adapter.cpp)
    target_link_libraries(ultraai_test_elevenlabs_adapter PRIVATE
        UltraAI_Core UltraAI_AdapterElevenLabs)
    # The test inspects recorded request bodies with the same vendored
    # nlohmann/json the adapter serializes with.
    target_include_directories(ultraai_test_elevenlabs_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_test_elevenlabs_adapter PRIVATE cxx_std_20)
    add_test(NAME ultraai.elevenlabs_adapter COMMAND ultraai_test_elevenlabs_adapter)
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

# The local runtime and the `local` provider, tested against a fake engine
# (fake_engine.cpp) over real loopback HTTP. In-tree only, like the runtime.
if(TARGET UltraAI_AdapterLocal AND TARGET VirtualFS)
    add_executable(ultraai_fake_engine fake_engine.cpp)
    target_include_directories(ultraai_fake_engine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    target_compile_features(ultraai_fake_engine PRIVATE cxx_std_20)
    if(WIN32)
        target_link_libraries(ultraai_fake_engine PRIVATE ws2_32)
    endif()

    add_executable(ultraai_test_local_runtime test_local_runtime.cpp)
    target_link_libraries(ultraai_test_local_runtime PRIVATE
        UltraAI_Core UltraAI_AdapterLocal UltraAI_Runtime VirtualFS)
    # The test reaches the runtime's internal parts (the pure fit and
    # command-line functions) through the module's private header.
    target_include_directories(ultraai_test_local_runtime PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src")
    target_compile_features(ultraai_test_local_runtime PRIVATE cxx_std_20)
    add_test(NAME ultraai.local_runtime
             COMMAND ultraai_test_local_runtime $<TARGET_FILE:ultraai_fake_engine>)
endif()
