# UltraCanvas/Plugins/UltraNet/grpc/CMakeLists.txt
# gRPC plug-in — libcurl-native (NOT grpc-c++). Requires libcurl with
# HTTP/2 enabled (nghttp2 backend); the plug-in does a runtime probe
# via curl_version_info and fails Connect() cleanly when HTTP/2 isn't
# available.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetGrpcPlugin MODULE GrpcPlugin.cpp)

set_target_properties(UltraNetGrpcPlugin PROPERTIES
    OUTPUT_NAME "ultranet_grpc"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetGrpcPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetGrpcPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR})

# Reuse the curl target the core picked (system or vendored). UC_CURL_LINK is
# set by UltraCanvas/CMakeLists.txt before these plug-ins are added; the guard
# keeps the plug-in buildable standalone.
if(NOT DEFINED UC_CURL_LINK)
    find_package(CURL REQUIRED)
    set(UC_CURL_LINK CURL::libcurl)
endif()
target_link_libraries(UltraNetGrpcPlugin PRIVATE ${UC_CURL_LINK})

set_target_properties(UltraNetGrpcPlugin PROPERTIES
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
target_sources(UltraNetGrpcPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetGrpcPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
