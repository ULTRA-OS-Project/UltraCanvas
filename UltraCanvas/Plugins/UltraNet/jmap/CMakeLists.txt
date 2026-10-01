# UltraCanvas/Plugins/UltraNet/jmap/CMakeLists.txt
# Builds the JMAP (RFC 8620/8621) plug-in as a loadable DSO. Unlike the mail
# trio this plug-in has no libcurl code of its own — it calls the host's
# UltraNet_Http* surface (resolved at dlopen time on POSIX; linked against the
# shared UltraCanvas core on Windows). Only extra dependency: the vendored
# nlohmann/json single header in third_party/.

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetJmapPlugin MODULE JmapPlugin.cpp)

set_target_properties(UltraNetJmapPlugin PROPERTIES
    OUTPUT_NAME "ultranet_jmap"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetJmapPlugin PRIVATE cxx_std_20)

# Third-party dir for <nlohmann/json.hpp>; derive it from the include dir so
# the plug-in stays buildable standalone.
if(NOT DEFINED ULTRACANVAS_INCLUDE_DIR)
    get_filename_component(ULTRACANVAS_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../include" ABSOLUTE)
endif()
get_filename_component(UC_THIRD_PARTY_DIR
    "${ULTRACANVAS_INCLUDE_DIR}/../third_party" ABSOLUTE)

target_include_directories(UltraNetJmapPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${UC_THIRD_PARTY_DIR})

set_target_properties(UltraNetJmapPlugin PROPERTIES
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
target_sources(UltraNetJmapPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetJmapPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
