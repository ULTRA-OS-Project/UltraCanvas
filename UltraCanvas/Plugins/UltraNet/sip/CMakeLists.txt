# UltraCanvas/Plugins/UltraNet/sip/CMakeLists.txt
# SIP plug-in — UDP-native, no external SIP library dependency. Uses
# BSD sockets on Linux/macOS and Winsock on Windows.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetSipPlugin MODULE SipPlugin.cpp)

set_target_properties(UltraNetSipPlugin PROPERTIES
    OUTPUT_NAME "ultranet_sip"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetSipPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetSipPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR})

if(WIN32)
    target_link_libraries(UltraNetSipPlugin PRIVATE ws2_32)
endif()

set_target_properties(UltraNetSipPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetSipPlugin PRIVATE UltraCanvas)
endif()
