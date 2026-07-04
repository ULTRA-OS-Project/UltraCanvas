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
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

if(WIN32)
    target_link_libraries(UltraNetSipPlugin PRIVATE ws2_32)
endif()

set_target_properties(UltraNetSipPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
