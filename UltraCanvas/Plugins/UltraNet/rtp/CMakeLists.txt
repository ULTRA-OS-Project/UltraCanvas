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
