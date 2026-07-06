# UltraCanvas/Plugins/UltraNet/smtp/CMakeLists.txt
# Builds the SMTP send-only plug-in as a loadable DSO.
#
# The plug-in does NOT statically link the UltraNet library — it imports
# UltraNet_RegisterPlugin from the host binary at load time. The host must
# export that symbol (Linux/macOS: build with -rdynamic; Windows: requires
# linking against an import library, currently unsupported — Windows plug-in
# loading is on the v1.1 roadmap).

cmake_minimum_required(VERSION 3.16)

add_library(UltraNetSmtpPlugin MODULE SmtpPlugin.cpp)

set_target_properties(UltraNetSmtpPlugin PROPERTIES
    OUTPUT_NAME "ultranet_smtp"
    PREFIX      ""           # produce ultranet_smtp.{so,dylib,dll}, no "lib" prefix
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetSmtpPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetSmtpPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include)

find_package(CURL REQUIRED)
target_link_libraries(UltraNetSmtpPlugin PRIVATE CURL::libcurl)

# Drop the binary next to where the host scans by default. The host's
# UltraNet_GetPluginDirectory() defaults to "Plugins/UltraNet"; mirror that
# in the build tree so a stock UltraNet_RefreshPlugins() finds it.
set_target_properties(UltraNetSmtpPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetSmtpPlugin PRIVATE UltraCanvas)
endif()
