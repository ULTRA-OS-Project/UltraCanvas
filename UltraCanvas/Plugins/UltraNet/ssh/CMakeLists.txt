# UltraCanvas/Plugins/UltraNet/ssh/CMakeLists.txt
# SSH plug-in backed by libssh (NOT libssh2 — libssh has proper
# interactive-shell exec semantics that fit IRemoteAccessPlugin).
# Self-disables when libssh isn't installed.

cmake_minimum_required(VERSION 3.16)

find_library(LIBSSH_LIB ssh)
find_path(LIBSSH_INCLUDE libssh/libssh.h)

if(NOT LIBSSH_LIB OR NOT LIBSSH_INCLUDE)
    message(STATUS "    libssh headers/lib missing - skipping SSH plug-in")
    return()
endif()

add_library(UltraNetSshPlugin MODULE SshPlugin.cpp)

set_target_properties(UltraNetSshPlugin PROPERTIES
    OUTPUT_NAME "ultranet_ssh"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetSshPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetSshPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${LIBSSH_INCLUDE})
target_link_libraries(UltraNetSshPlugin PRIVATE ${LIBSSH_LIB})

set_target_properties(UltraNetSshPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Core functions through the host table ------------------------------------
# What this plug-in needs from the core (UltraNet_ParseUrl, UltraNet_MimeBuild,
# ...) reaches the host through the UltraNetPluginHost table it is handed at
# load time: the shim defines those functions inside the plug-in and forwards
# each call there. So the DSO has no undefined core symbol and links against
# nothing from the core - it loads into an app on a static core, and a Windows
# DLL builds without the core's import library. Hidden visibility keeps the
# shim's definitions the plug-in's own.
set(_ultranet_host_shim ${CMAKE_CURRENT_SOURCE_DIR}/../common/UltraNetPluginHostShim.cpp)
target_sources(UltraNetSshPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetSshPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
