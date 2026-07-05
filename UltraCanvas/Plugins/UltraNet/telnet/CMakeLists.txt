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

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetTelnetPlugin PRIVATE UltraCanvas)
endif()
