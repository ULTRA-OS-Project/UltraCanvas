# UltraCanvas/Plugins/UltraNet/imap/CMakeLists.txt
# Builds the IMAP/IMAPS fetch-only plug-in as a loadable DSO.
# Same model as the SMTP plug-in — the core is reached through the host table
# handed to UltraNet_PluginInit (see the host-table block below).

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetImapPlugin MODULE ImapPlugin.cpp)

set_target_properties(UltraNetImapPlugin PROPERTIES
    OUTPUT_NAME "ultranet_imap"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetImapPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetImapPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR})

# Reuse the curl target the core picked (system or vendored). UC_CURL_LINK is
# set by UltraCanvas/CMakeLists.txt before these plug-ins are added; the guard
# keeps the plug-in buildable standalone.
if(NOT DEFINED UC_CURL_LINK)
    find_package(CURL REQUIRED)
    set(UC_CURL_LINK CURL::libcurl)
endif()
target_link_libraries(UltraNetImapPlugin PRIVATE ${UC_CURL_LINK})

set_target_properties(UltraNetImapPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Core functions through the host table ------------------------------------
# What this plug-in needs from the core (UltraNet_ParseUrl, UltraNet_MimeBuild,
# ...) reaches the host through the UltraNetPluginHost table it is handed at
# load time: the shim defines those functions inside the plug-in and forwards
# each call there. So the DSO has no undefined core symbol and links against
# nothing from the core - it loads into an app on a static core, and a Windows
# DLL builds without the core's import library. Hidden visibility keeps the
# shim's definitions the plug-in's own.
set(_ultranet_host_shim ${CMAKE_CURRENT_SOURCE_DIR}/../common/UltraNetPluginHostShim.cpp)
target_sources(UltraNetImapPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetImapPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
