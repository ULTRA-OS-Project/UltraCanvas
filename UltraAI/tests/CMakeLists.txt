# UltraAI/tests/CMakeLists.txt
# Lightweight assert-based tests for the UltraAI module. No external test
# framework required so the module stays self-contained.

add_executable(ultraai_test_mock_textllm test_mock_textllm.cpp)
target_link_libraries(ultraai_test_mock_textllm PRIVATE UltraAI_Core)
target_compile_features(ultraai_test_mock_textllm PRIVATE cxx_std_20)

add_test(NAME ultraai.mock_textllm COMMAND ultraai_test_mock_textllm)
