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

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetImapPlugin PRIVATE UltraCanvas)
endif()
