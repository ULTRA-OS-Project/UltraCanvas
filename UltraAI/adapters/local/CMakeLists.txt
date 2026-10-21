# UltraAI/adapters/local/CMakeLists.txt
# The `local` provider: ITextLLM and IEmbeddings served by an engine the
# local runtime downloads, starts and supervises. The wire protocol is the
# OpenAI adapter's, so it needs that adapter and the runtime.

if(NOT TARGET UltraAI_Runtime)
    message(STATUS "UltraAI: local adapter skipped — the local runtime is not in the build.")
    return()
endif()
if(NOT TARGET UltraAI_AdapterOpenAI)
    message(STATUS "UltraAI: local adapter skipped — it delegates to the OpenAI adapter, "
                   "which is not enabled.")
    return()
endif()

add_library(UltraAI_AdapterLocal STATIC
    src/LocalAdapters.cpp
)
add_library(UltraAI::AdapterLocal ALIAS UltraAI_AdapterLocal)

target_include_directories(UltraAI_AdapterLocal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(UltraAI_AdapterLocal PUBLIC
    UltraAI
    UltraAI_AdapterShared
    UltraAI_AdapterOpenAI
    UltraAI_Runtime
)
target_compile_features(UltraAI_AdapterLocal PUBLIC cxx_std_20)

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(TARGETS UltraAI_AdapterLocal ARCHIVE DESTINATION lib)
