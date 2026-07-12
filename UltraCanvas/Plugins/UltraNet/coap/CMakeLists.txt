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

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetCoapPlugin PRIVATE UltraCanvas)
endif()
