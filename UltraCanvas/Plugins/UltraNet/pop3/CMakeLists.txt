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
    ${ULTRACANVAS_INCLUDE_DIR})

# Reuse the curl target the core picked (system or vendored). UC_CURL_LINK is
# set by UltraCanvas/CMakeLists.txt before these plug-ins are added; the guard
# keeps the plug-in buildable standalone.
if(NOT DEFINED UC_CURL_LINK)
    find_package(CURL REQUIRED)
    set(UC_CURL_LINK CURL::libcurl)
endif()
target_link_libraries(UltraNetPop3Plugin PRIVATE ${UC_CURL_LINK})

set_target_properties(UltraNetPop3Plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetPop3Plugin PRIVATE UltraCanvas)
endif()
