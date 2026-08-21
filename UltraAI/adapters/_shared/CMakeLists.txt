# UltraAI/adapters/_shared/CMakeLists.txt
# Infrastructure shared by network-using adapters: credential resolution,
# HTTP error mapping, retry policy, stream-handle base, and the transport
# seam (ScriptedTransport for tests; UltraNetTransport when UltraNet is in
# the build). Not an adapter itself — always built with the adapters tree.

add_library(UltraAI_AdapterShared STATIC
    src/Credentials.cpp
    src/HttpError.cpp
    src/RetryPolicy.cpp
    src/ScriptedTransport.cpp
)
add_library(UltraAI::AdapterShared ALIAS UltraAI_AdapterShared)

target_include_directories(UltraAI_AdapterShared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Depends only on the interface headers, like the mock adapter, so the
# dependency graph stays acyclic.
target_link_libraries(UltraAI_AdapterShared PUBLIC UltraAI)
target_compile_features(UltraAI_AdapterShared PUBLIC cxx_std_20)

if(ULTRAAI_HAS_ULTRANET)
    target_sources(UltraAI_AdapterShared PRIVATE src/UltraNetTransport.cpp)
    target_link_libraries(UltraAI_AdapterShared PUBLIC UltraNet)
    target_compile_definitions(UltraAI_AdapterShared PUBLIC ULTRAAI_HAS_ULTRANET=1)
    message(STATUS "UltraAI: adapter-shared library includes UltraNetTransport.")
endif()

if(ULTRAAI_HAS_ULTRAVAULT)
    target_link_libraries(UltraAI_AdapterShared PUBLIC UltraVault)
    target_compile_definitions(UltraAI_AdapterShared PUBLIC ULTRAAI_HAS_ULTRAVAULT=1)
endif()

install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(TARGETS UltraAI_AdapterShared
    EXPORT UltraAITargets
    ARCHIVE DESTINATION lib
)
