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
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetRtmpPlugin PRIVATE CURL::libcurl)

set_target_properties(UltraNetRtmpPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
