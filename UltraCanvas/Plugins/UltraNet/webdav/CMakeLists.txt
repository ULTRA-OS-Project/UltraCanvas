# UltraCanvas/Plugins/UltraNet/webdav/CMakeLists.txt
# WebDAV / WebDAVS plug-in. Same loadable-DSO model as the mail plug-ins.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetWebDavPlugin MODULE WebDavPlugin.cpp)

set_target_properties(UltraNetWebDavPlugin PROPERTIES
    OUTPUT_NAME "ultranet_webdav"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetWebDavPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetWebDavPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetWebDavPlugin PRIVATE CURL::libcurl)

set_target_properties(UltraNetWebDavPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
