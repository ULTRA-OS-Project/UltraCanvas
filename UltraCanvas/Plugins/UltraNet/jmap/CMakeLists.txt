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

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX the plug-in resolves core symbols (UltraNet_HttpRequest, UltraNet_ParseUrl,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetJmapPlugin PRIVATE UltraCanvas)
endif()
