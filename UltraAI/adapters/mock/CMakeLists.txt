# UltraAI/adapters/mock/CMakeLists.txt
# In-process mock ITextLLM adapter — no network, no external dependencies.

add_library(UltraAI_AdapterMock STATIC
    src/MockTextLLM.cpp
)
add_library(UltraAI::AdapterMock ALIAS UltraAI_AdapterMock)

target_include_directories(UltraAI_AdapterMock PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Mock depends only on the interface headers, never on UltraAI_Core,
# so we keep the dependency graph acyclic (Core -> Mock -> Interface).
target_link_libraries(UltraAI_AdapterMock PUBLIC UltraAI)
target_compile_features(UltraAI_AdapterMock PUBLIC cxx_std_20)

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(TARGETS UltraAI_AdapterMock
    EXPORT UltraAITargets
    ARCHIVE DESTINATION lib
)
