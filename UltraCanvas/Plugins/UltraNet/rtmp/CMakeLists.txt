# UltraCanvas/Plugins/UltraNet/rtmp/CMakeLists.txt
# RTMP plug-in. libcurl-native — requires libcurl to be built with RTMP
# support enabled (Ubuntu / brew / MSYS2 all ship RTMP-enabled curls by
# default). Runtime detection in the plug-in catches the rare libcurl
# builds without RTMP.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetRtmpPlugin MODULE RtmpPlugin.cpp)

set_target_properties(UltraNetRtmpPlugin PROPERTIES
    OUTPUT_NAME "ultranet_rtmp"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetRtmpPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetRtmpPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR})

# Reuse the curl target the core picked (system or vendored). UC_CURL_LINK is
# set by UltraCanvas/CMakeLists.txt before these plug-ins are added; the guard
# keeps the plug-in buildable standalone.
if(NOT DEFINED UC_CURL_LINK)
    find_package(CURL REQUIRED)
    set(UC_CURL_LINK CURL::libcurl)
endif()
target_link_libraries(UltraNetRtmpPlugin PRIVATE ${UC_CURL_LINK})

set_target_properties(UltraNetRtmpPlugin PROPERTIES
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
target_sources(UltraNetRtmpPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetRtmpPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
