# UltraCanvas/Plugins/UltraNet/rtsp/CMakeLists.txt
# RTSP plug-in. Same loadable-DSO model as the mail plug-ins.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetRtspPlugin MODULE RtspPlugin.cpp)

set_target_properties(UltraNetRtspPlugin PROPERTIES
    OUTPUT_NAME "ultranet_rtsp"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetRtspPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetRtspPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR})

# Reuse the curl target the core picked (system or vendored). UC_CURL_LINK is
# set by UltraCanvas/CMakeLists.txt before these plug-ins are added; the guard
# keeps the plug-in buildable standalone.
if(NOT DEFINED UC_CURL_LINK)
    find_package(CURL REQUIRED)
    set(UC_CURL_LINK CURL::libcurl)
endif()
target_link_libraries(UltraNetRtspPlugin PRIVATE ${UC_CURL_LINK})

set_target_properties(UltraNetRtspPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetRtspPlugin PRIVATE UltraCanvas)
endif()
