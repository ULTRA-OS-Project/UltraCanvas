# UltraAI/runtime/CMakeLists.txt
# The local runtime (UltraAILocalRuntime.h): engines and models downloaded
# on demand, run as supervised loopback processes. It needs the siblings
# that live in the shared core - UltraNet (downloads, health checks),
# UltraCrypt (SHA-256, the API key) and VirtualFS (unpacking a bundle) -
# so it is built in-tree only; a standalone UltraAI build skips it and the
# `local` provider with it.

if(NOT ULTRAAI_HAS_ULTRANET)
    message(STATUS "UltraAI: local runtime skipped — UltraNet is not in the build.")
    return()
endif()
if(NOT TARGET UltraCrypt OR NOT TARGET VirtualFS)
    message(STATUS "UltraAI: local runtime skipped — UltraCrypt and VirtualFS are not both in the build.")
    return()
endif()

set(_ULTRAAI_RUNTIME_NLOHMANN_DIR "")
foreach(_candidate "${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/third_party")
    if(EXISTS "${_candidate}/nlohmann/json.hpp")
        set(_ULTRAAI_RUNTIME_NLOHMANN_DIR "${_candidate}")
        break()
    endif()
endforeach()
if(NOT _ULTRAAI_RUNTIME_NLOHMANN_DIR)
    message(STATUS "UltraAI: local runtime skipped — vendored nlohmann/json.hpp not found.")
    return()
endif()

add_library(UltraAI_Runtime STATIC
    src/LocalRuntime.cpp
    src/LocalRuntimeBuiltinData.cpp
    src/LocalRuntimeCatalogue.cpp
    src/LocalRuntimeHardware.cpp
    src/LocalRuntimeStorage.cpp
)
add_library(UltraAI::Runtime ALIAS UltraAI_Runtime)

target_include_directories(UltraAI_Runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${_ULTRAAI_RUNTIME_NLOHMANN_DIR}
)

# UltraNet carries UltraCanvas/include (the header-only path, data-folder
# and settings-folder helpers come from there); UltraCrypt and VirtualFS
# their own headers. All three resolve to the shared core in a shared
# build.
target_link_libraries(UltraAI_Runtime
    PUBLIC  UltraAI UltraAI_AdapterShared
    PRIVATE UltraNet UltraCrypt VirtualFS
)
target_compile_features(UltraAI_Runtime PUBLIC cxx_std_20)
target_compile_definitions(UltraAI_Runtime PUBLIC ULTRAAI_HAS_LOCAL_RUNTIME=1)

# The hardware inventory (GPUs, memory, CPU topology) lives in the core
# library itself, not in a module archive: use it when the core is in the
# build, and fall back to the standard library's view otherwise.
if(ULTRACANVAS_LIBRARY AND TARGET ${ULTRACANVAS_LIBRARY})
    target_link_libraries(UltraAI_Runtime PRIVATE ${ULTRACANVAS_LIBRARY})
    target_compile_definitions(UltraAI_Runtime PRIVATE ULTRAAI_RUNTIME_HAS_HARDWARE_INFO=1)
    message(STATUS "UltraAI: local runtime detects hardware through UltraCanvasHardwareInfo.")
endif()

if(WIN32)
    target_link_libraries(UltraAI_Runtime PRIVATE ws2_32)
endif()

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(TARGETS UltraAI_Runtime ARCHIVE DESTINATION lib)
