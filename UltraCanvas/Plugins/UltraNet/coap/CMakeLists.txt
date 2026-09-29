# UltraCanvas/Plugins/UltraNet/coap/CMakeLists.txt
# CoAP plug-in backed by libcoap3. Picks whichever TLS variant of
# libcoap3 is installed (openssl / gnutls / mbedtls), falling back to
# the no-TLS build if that's all that's available.

cmake_minimum_required(VERSION 3.16)

find_path(COAP3_INCLUDE coap3/coap.h)
# Try each TLS-flavoured libcoap3 in turn; -openssl is the most common.
find_library(COAP3_LIB
    NAMES coap-3-openssl coap-3-gnutls coap-3-mbedtls coap-3-notls coap-3)

if(NOT COAP3_LIB OR NOT COAP3_INCLUDE)
    message(STATUS "    libcoap3 headers/lib missing - skipping CoAP plug-in")
    return()
endif()

add_library(UltraNetCoapPlugin MODULE CoapPlugin.cpp)

set_target_properties(UltraNetCoapPlugin PROPERTIES
    OUTPUT_NAME "ultranet_coap"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetCoapPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetCoapPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${COAP3_INCLUDE})
target_link_libraries(UltraNetCoapPlugin PRIVATE ${COAP3_LIB})

set_target_properties(UltraNetCoapPlugin PROPERTIES
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
target_sources(UltraNetCoapPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetCoapPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
