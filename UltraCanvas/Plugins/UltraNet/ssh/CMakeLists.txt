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

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetSshPlugin PRIVATE UltraCanvas)
endif()
