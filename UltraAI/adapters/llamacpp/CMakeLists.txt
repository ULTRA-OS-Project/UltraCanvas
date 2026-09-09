# UltraAI/adapters/llamacpp/CMakeLists.txt
# Local llama.cpp adapter. Opt-in (ULTRAAI_ADAPTER_LLAMACPP) because it
# vendors and builds the llama.cpp/ggml inference engine.
#
# Source resolution:
#   ULTRAAI_LLAMACPP_SOURCE_DIR  — use an existing llama.cpp checkout
#   otherwise                    — FetchContent from GitHub, pinned below.
#
# The pin is a specific upstream commit (llama.cpp does not keep a stable
# API); bump it deliberately and re-run the adapter smoke test.

set(ULTRAAI_LLAMACPP_SOURCE_DIR "" CACHE PATH
    "Existing llama.cpp source tree to build instead of fetching")
set(ULTRAAI_LLAMACPP_GIT_TAG "5b6ddc9675a9c2d10c4458a510b09674a3decec1" CACHE STRING
    "llama.cpp commit to fetch when no source dir is given")

# Keep the engine build lean: library only, CPU by default. `common` is
# kept for its JSON-schema→GBNF compiler (structured output).
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON   ON  CACHE BOOL "" FORCE)
set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

if(ULTRAAI_LLAMACPP_SOURCE_DIR)
    add_subdirectory("${ULTRAAI_LLAMACPP_SOURCE_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/llama.cpp" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(llama_cpp
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp
        GIT_TAG        ${ULTRAAI_LLAMACPP_GIT_TAG}
    )
    FetchContent_MakeAvailable(llama_cpp)
endif()

if(NOT TARGET llama)
    message(FATAL_ERROR "UltraAI: llama.cpp sources did not define the "
                        "'llama' target; check ULTRAAI_LLAMACPP_SOURCE_DIR / "
                        "ULTRAAI_LLAMACPP_GIT_TAG.")
endif()

add_library(UltraAI_AdapterLlama STATIC
    src/LlamaTextLLM.cpp
    src/LlamaEmbeddings.cpp
)
add_library(UltraAI::AdapterLlama ALIAS UltraAI_AdapterLlama)

target_include_directories(UltraAI_AdapterLlama PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(UltraAI_AdapterLlama
    PUBLIC UltraAI
    PRIVATE UltraAI_AdapterShared llama
)
target_compile_features(UltraAI_AdapterLlama PUBLIC cxx_std_20)

# llama.cpp `common` carries json_schema_to_grammar (and, PUBLIC, the
# vendored nlohmann it needs). The target is `llama-common` at the pinned
# commit, plain `common` in older checkouts; with neither available the
# adapter falls back to the universal JSON grammar for structured output.
set(_ULTRAAI_LLAMA_COMMON "")
if(TARGET llama-common)
    set(_ULTRAAI_LLAMA_COMMON llama-common)
elseif(TARGET common)
    set(_ULTRAAI_LLAMA_COMMON common)
endif()
if(_ULTRAAI_LLAMA_COMMON)
    target_link_libraries(UltraAI_AdapterLlama PRIVATE ${_ULTRAAI_LLAMA_COMMON})
    target_compile_definitions(UltraAI_AdapterLlama PRIVATE
        ULTRAAI_LLAMA_HAS_SCHEMA_GRAMMAR=1)
else()
    message(STATUS "UltraAI: llama.cpp common target not available — "
                   "JSON-schema grammars fall back to plain JSON.")
endif()

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
# Plain archive, no export set — see the install() note in the module's
# top-level CMakeLists.
install(TARGETS UltraAI_AdapterLlama
    ARCHIVE DESTINATION lib
)
