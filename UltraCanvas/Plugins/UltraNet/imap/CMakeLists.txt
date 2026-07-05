# UltraCanvas/Plugins/UltraNet/imap/CMakeLists.txt
# Builds the IMAP/IMAPS fetch-only plug-in as a loadable DSO.
# Same model as the SMTP plug-in — symbols are resolved at dlopen time
# against the host binary; Windows plug-in loading is on the v1.1 roadmap.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetImapPlugin MODULE ImapPlugin.cpp)

set_target_properties(UltraNetImapPlugin PROPERTIES
    OUTPUT_NAME "ultranet_imap"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetImapPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetImapPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetImapPlugin PRIVATE CURL::libcurl)

set_target_properties(UltraNetImapPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
