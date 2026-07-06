# UltraCanvas/Plugins/UltraNet/rtp/CMakeLists.txt
# RTP receiver plug-in — no external library dependency. BSD sockets
# on Linux/macOS, Winsock on Windows.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetRtpPlugin MODULE RtpPlugin.cpp)

set_target_properties(UltraNetRtpPlugin PROPERTIES
    OUTPUT_NAME "ultranet_rtp"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetRtpPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetRtpPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

if(WIN32)
    target_link_libraries(UltraNetRtpPlugin PRIVATE ws2_32)
endif()

set_target_properties(UltraNetRtpPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetRtpPlugin PRIVATE UltraCanvas)
endif()
