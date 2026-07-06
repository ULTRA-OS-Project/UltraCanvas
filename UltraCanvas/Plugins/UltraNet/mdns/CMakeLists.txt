# UltraCanvas/Plugins/UltraNet/mdns/CMakeLists.txt
# mDNS / DNS-SD service discovery. Per-platform backend:
#   Linux   - Avahi (libavahi-client)
#   macOS   - Bonjour (system framework, no extra lib needed)
#   Windows - DnsServiceBrowse via dnsapi.dll (system)
# Self-disables when its platform backend isn't available.

cmake_minimum_required(VERSION 3.16)

set(MDNS_BACKEND_OK FALSE)
set(MDNS_BACKEND_LIBS "")
set(MDNS_BACKEND_INCLUDES "")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(AVAHI_CLIENT_LIB avahi-client)
    find_library(AVAHI_COMMON_LIB avahi-common)
    find_path(AVAHI_INCLUDE avahi-client/client.h)
    if(AVAHI_CLIENT_LIB AND AVAHI_COMMON_LIB AND AVAHI_INCLUDE)
        set(MDNS_BACKEND_OK TRUE)
        set(MDNS_BACKEND_LIBS ${AVAHI_CLIENT_LIB} ${AVAHI_COMMON_LIB})
        set(MDNS_BACKEND_INCLUDES ${AVAHI_INCLUDE})
    endif()
elseif(APPLE)
    # Bonjour is part of the system; <dns_sd.h> is in the SDK.
    find_library(BONJOUR_FRAMEWORK CoreServices)
    if(BONJOUR_FRAMEWORK)
        set(MDNS_BACKEND_OK TRUE)
        set(MDNS_BACKEND_LIBS ${BONJOUR_FRAMEWORK})
    endif()
elseif(WIN32)
    set(MDNS_BACKEND_OK TRUE)
    set(MDNS_BACKEND_LIBS dnsapi)
endif()

if(NOT MDNS_BACKEND_OK)
    message(STATUS "    mDNS backend not available - skipping mDNS plug-in")
    return()
endif()

add_library(UltraNetMdnsPlugin MODULE MdnsPlugin.cpp)

set_target_properties(UltraNetMdnsPlugin PROPERTIES
    OUTPUT_NAME "ultranet_mdns"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetMdnsPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetMdnsPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${MDNS_BACKEND_INCLUDES})
target_link_libraries(UltraNetMdnsPlugin PRIVATE ${MDNS_BACKEND_LIBS})

set_target_properties(UltraNetMdnsPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetMdnsPlugin PRIVATE UltraCanvas)
endif()
