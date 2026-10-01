# UltraCanvas/Plugins/UltraNet/mdns/CMakeLists.txt
# mDNS / DNS-SD service discovery. Per-platform backend:
#   Linux   - Avahi (libavahi-client)
#   macOS   - Bonjour (system framework, no extra lib needed)
#   Windows - DnsServiceBrowse / DnsServiceResolve, bound at run time from
#             dnsapi.dll so the module still loads on pre-1703 Windows
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

add_library(UltraNetMdnsPlugin MODULE MdnsPlugin.cpp MdnsNames.cpp)

set_target_properties(UltraNetMdnsPlugin PROPERTIES
    OUTPUT_NAME "ultranet_mdns"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetMdnsPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetMdnsPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${MDNS_BACKEND_INCLUDES})
target_link_libraries(UltraNetMdnsPlugin PRIVATE ${MDNS_BACKEND_LIBS})

set_target_properties(UltraNetMdnsPlugin PROPERTIES
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
target_sources(UltraNetMdnsPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetMdnsPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
