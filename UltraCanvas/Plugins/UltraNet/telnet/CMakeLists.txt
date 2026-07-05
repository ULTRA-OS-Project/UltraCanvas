# UltraCanvas/Plugins/UltraNet/telnet/CMakeLists.txt
# Telnet plug-in. Same loadable-DSO model as the mail plug-ins.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetTelnetPlugin MODULE TelnetPlugin.cpp)

set_target_properties(UltraNetTelnetPlugin PROPERTIES
    OUTPUT_NAME "ultranet_telnet"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetTelnetPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetTelnetPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetTelnetPlugin PRIVATE CURL::libcurl)

set_target_properties(UltraNetTelnetPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
