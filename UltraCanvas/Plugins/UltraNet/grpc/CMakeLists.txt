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
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetGrpcPlugin PRIVATE CURL::libcurl)

set_target_properties(UltraNetGrpcPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetGrpcPlugin PRIVATE UltraCanvas)
endif()
