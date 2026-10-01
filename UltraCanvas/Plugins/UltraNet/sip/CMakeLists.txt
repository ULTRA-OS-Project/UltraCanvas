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
    ${ULTRACANVAS_INCLUDE_DIR})

if(WIN32)
    target_link_libraries(UltraNetSipPlugin PRIVATE ws2_32)
endif()

set_target_properties(UltraNetSipPlugin PROPERTIES
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
target_sources(UltraNetSipPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetSipPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
