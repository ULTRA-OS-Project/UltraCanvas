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
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${LIBSSH_INCLUDE})
target_link_libraries(UltraNetSshPlugin PRIVATE ${LIBSSH_LIB})

set_target_properties(UltraNetSshPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
