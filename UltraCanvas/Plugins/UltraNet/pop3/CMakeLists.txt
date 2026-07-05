# UltraCanvas/Plugins/UltraNet/pop3/CMakeLists.txt
# POP3/POP3S fetch-only plug-in. Same DSO model as the SMTP and IMAP
# plug-ins — runtime-loaded via dlopen; Windows plug-in loading is on
# the v1.1 roadmap.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetPop3Plugin MODULE Pop3Plugin.cpp)

set_target_properties(UltraNetPop3Plugin PROPERTIES
    OUTPUT_NAME "ultranet_pop3"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetPop3Plugin PRIVATE cxx_std_20)
target_include_directories(UltraNetPop3Plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetPop3Plugin PRIVATE CURL::libcurl)

set_target_properties(UltraNetPop3Plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
