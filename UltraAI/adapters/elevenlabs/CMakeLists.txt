# UltraAI/adapters/elevenlabs/CMakeLists.txt
# ElevenLabs speech adapter. JSON handling uses the repo's
# vendored nlohmann/json (already licensed in THIRD_PARTY_LICENSES.md);
# when the monorepo layout is not present the adapter is skipped.

set(_ULTRAAI_NLOHMANN_DIR "")
foreach(_candidate
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../UltraCanvas/third_party")
    if(EXISTS "${_candidate}/nlohmann/json.hpp")
        set(_ULTRAAI_NLOHMANN_DIR "${_candidate}")
        break()
    endif()
endforeach()

if(NOT _ULTRAAI_NLOHMANN_DIR)
    message(STATUS "UltraAI: ElevenLabs adapter skipped — vendored "
                   "nlohmann/json.hpp not found (monorepo layout required).")
    return()
endif()

add_library(UltraAI_AdapterElevenLabs STATIC
    src/ElevenLabsTextToSpeech.cpp
)
add_library(UltraAI::AdapterElevenLabs ALIAS UltraAI_AdapterElevenLabs)

target_include_directories(UltraAI_AdapterElevenLabs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${_ULTRAAI_NLOHMANN_DIR}
)

target_link_libraries(UltraAI_AdapterElevenLabs PUBLIC
    UltraAI
    UltraAI_AdapterShared
)
target_compile_features(UltraAI_AdapterElevenLabs PUBLIC cxx_std_20)

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
# Not in the UltraAITargets export set for the same reason as
# UltraAI_AdapterShared: its link interface reaches UltraNet, which has no
# export set of its own.
install(TARGETS UltraAI_AdapterElevenLabs
    ARCHIVE DESTINATION lib
)
