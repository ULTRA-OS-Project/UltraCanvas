# UltraAI/adapters/comfyui/CMakeLists.txt
# ComfyUI image- and video-generation adapter. ComfyUI itself is a separate,
# user-installed GPL-3.0 program: nothing is vendored, fetched or linked
# here — this target is an HTTP/WebSocket client of a server the user runs.
# JSON handling uses the repo's vendored nlohmann/json (already licensed in
# THIRD_PARTY_LICENSES.md); when the monorepo layout is not present the
# adapter is skipped.

set(_ULTRAAI_NLOHMANN_DIR "")
foreach(_candidate
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../UltraCanvas/third_party")
    if(EXISTS "${_candidate}/nlohmann/json.hpp")
        set(_ULTRAAI_NLOHMANN_DIR "${_candidate}")
        break()
    endif()
endforeach()

if(NOT _ULTRAAI_NLOHMANN_DIR)
    message(STATUS "UltraAI: ComfyUI adapter skipped — vendored "
                   "nlohmann/json.hpp not found (monorepo layout required).")
    return()
endif()

add_library(UltraAI_AdapterComfyUI STATIC
    src/ComfyUIImageGen.cpp
    src/ComfyUIInternal.cpp
    src/ComfyUIRun.cpp
    src/ComfyUIVideoGen.cpp
    src/ComfyUIWorkflows.cpp
)
add_library(UltraAI::AdapterComfyUI ALIAS UltraAI_AdapterComfyUI)

target_include_directories(UltraAI_AdapterComfyUI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${_ULTRAAI_NLOHMANN_DIR}
)

target_link_libraries(UltraAI_AdapterComfyUI PUBLIC
    UltraAI
    UltraAI_AdapterShared
)
target_compile_features(UltraAI_AdapterComfyUI PUBLIC cxx_std_20)

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
# Not in the UltraAITargets export set for the same reason as
# UltraAI_AdapterShared: its link interface reaches UltraNet, which has no
# export set of its own.
install(TARGETS UltraAI_AdapterComfyUI
    ARCHIVE DESTINATION lib
)
