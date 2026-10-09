# Tests/UltraNet/CMakeLists.txt
# Builds the UltraNetTests binary (pass/fail unit + loopback tests) and the
# UltraNetApiStatus binary (per-function API status report).
# Enabled by ULTRACANVAS_BUILD_NET_TESTS.

cmake_minimum_required(VERSION 3.16)

# UltraNetMime routes base64/Trim through UltraCanvas core (UltraCanvasUtils),
# whose definitions live in libultracanvas — which in turn PUBLIC-links UltraNet.
# That makes the two static archives mutually dependent, so a consumer must link
# both and the linker must resolve the UltraNet -> UltraCanvas back-references
# regardless of scan order. GNU ld (Linux + Windows/MinGW CI) is single-pass, so
# wrap the pair in a rescanned link group; Apple ld64 / MSVC link.exe rescan
# archives natively AND reject CMake's LINK_GROUP RESCAN feature at generate time,
# so those get plain order. Linker family is checked first for exactly that
# reason. Guarded on the UltraCanvas target because the net tests can be built
# standalone. Mirrors the version-guarded WHOLE_ARCHIVE idiom in
# UltraCanvas/CMakeLists.txt.
function(ultranet_link_module target)
    if(TARGET UltraCanvas)
        if(APPLE OR MSVC)
            target_link_libraries(${target} PRIVATE UltraNet UltraCanvas)
        elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
            target_link_libraries(${target} PRIVATE
                "$<LINK_GROUP:RESCAN,UltraNet,UltraCanvas>")
        else()
            # GNU ld on CMake < 3.24 (no LINK_GROUP genex): emit the flags directly.
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group UltraNet UltraCanvas -Wl,--end-group)
        endif()
    else()
        target_link_libraries(${target} PRIVATE UltraNet)
    endif()
endfunction()

set(ULTRANET_TEST_SOURCES
    test_main.cpp
    test_url.cpp
    test_mime.cpp
    test_headers.cpp
    test_result.cpp
    test_plugins.cpp
    test_mailaddr.cpp
    test_dns_format.cpp
    test_dns_timeout.cpp
    test_dns_servers.cpp
    test_ftp_parser.cpp
    test_ftp_log.cpp
    test_curl_debug.cpp
    test_loopback.cpp
    test_session.cpp
    test_oauth2.cpp
    test_oauth2_apps.cpp
    test_smtp_plugin.cpp
    test_imap_plugin.cpp
    test_imap_mailbox.cpp
    test_pop3_plugin.cpp
    test_jmap_plugin.cpp
    test_extra_plugins.cpp
    test_sse.cpp
    test_websocket.cpp
    test_tls_pin.cpp
)

add_executable(UltraNetTests ${ULTRANET_TEST_SOURCES})

target_compile_features(UltraNetTests PRIVATE cxx_std_20)

ultranet_link_module(UltraNetTests)
target_include_directories(UltraNetTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# The plug-in tests dlopen the plug-in DSOs. Those reach the core only through
# the UltraNetPluginHost table and need nothing from this executable's symbol
# table; the exports are kept to match the apps (-rdynamic on Linux,
# -export_dynamic on macOS).
set_target_properties(UltraNetTests PROPERTIES ENABLE_EXPORTS ON)

# A plug-in in the retired v1 shape (only UltraNet_PluginRegister), in a
# directory of its own, so test_plugins.cpp can show the loader refuses it.
# POSIX only: a v1 plug-in resolves the host's UltraNet_RegisterPlugin at load
# time, which a Windows DLL cannot.
if(NOT WIN32)
    add_library(UltraNetV1OnlyTestPlugin MODULE v1only/V1OnlyPlugin.cpp)
    set_target_properties(UltraNetV1OnlyTestPlugin PROPERTIES
        PREFIX "" OUTPUT_NAME "ultranet_v1only"
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/v1only-plugins)
    target_compile_features(UltraNetV1OnlyTestPlugin PRIVATE cxx_std_20)
    target_include_directories(UltraNetV1OnlyTestPlugin PRIVATE ${CMAKE_SOURCE_DIR}/UltraCanvas/include)
    if(APPLE)
        set_property(TARGET UltraNetV1OnlyTestPlugin APPEND_STRING
            PROPERTY LINK_FLAGS " -undefined dynamic_lookup")
    endif()
    add_dependencies(UltraNetTests UltraNetV1OnlyTestPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_V1ONLY_PLUGIN_DIR_DEFINE="${CMAKE_CURRENT_BINARY_DIR}/v1only-plugins")
endif()

# Point the SMTP plug-in test at the built DSO when the plug-in is in this
# build (so it doesn't try to compile from source).
if(TARGET UltraNetSmtpPlugin)
    add_dependencies(UltraNetTests UltraNetSmtpPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_SMTP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetSmtpPlugin>")
endif()

if(TARGET UltraNetImapPlugin)
    add_dependencies(UltraNetTests UltraNetImapPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_IMAP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetImapPlugin>")
endif()

if(TARGET UltraNetPop3Plugin)
    add_dependencies(UltraNetTests UltraNetPop3Plugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_POP3_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetPop3Plugin>")
endif()

if(TARGET UltraNetJmapPlugin)
    add_dependencies(UltraNetTests UltraNetJmapPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_JMAP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetJmapPlugin>")
endif()

if(TARGET UltraNetWebDavPlugin)
    add_dependencies(UltraNetTests UltraNetWebDavPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetWebDavPlugin>")
endif()
if(TARGET UltraNetLdapPlugin)
    add_dependencies(UltraNetTests UltraNetLdapPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_LDAP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetLdapPlugin>")
endif()
if(TARGET UltraNetTelnetPlugin)
    add_dependencies(UltraNetTests UltraNetTelnetPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_TELNET_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetTelnetPlugin>")
endif()
if(TARGET UltraNetRtspPlugin)
    add_dependencies(UltraNetTests UltraNetRtspPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_RTSP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetRtspPlugin>")
endif()
if(TARGET UltraNetMqttPlugin)
    add_dependencies(UltraNetTests UltraNetMqttPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_MQTT_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetMqttPlugin>")
endif()
if(TARGET UltraNetAmqpPlugin)
    add_dependencies(UltraNetTests UltraNetAmqpPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_AMQP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetAmqpPlugin>")
endif()
if(TARGET UltraNetCoapPlugin)
    add_dependencies(UltraNetTests UltraNetCoapPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_COAP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetCoapPlugin>")
endif()
if(TARGET UltraNetSnmpPlugin)
    add_dependencies(UltraNetTests UltraNetSnmpPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_SNMP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetSnmpPlugin>")
endif()
if(TARGET UltraNetSshPlugin)
    add_dependencies(UltraNetTests UltraNetSshPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_SSH_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetSshPlugin>")
endif()
if(TARGET UltraNetMdnsPlugin)
    add_dependencies(UltraNetTests UltraNetMdnsPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_MDNS_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetMdnsPlugin>")
endif()
if(TARGET UltraNetRtmpPlugin)
    add_dependencies(UltraNetTests UltraNetRtmpPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_RTMP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetRtmpPlugin>")
endif()
if(TARGET UltraNetGrpcPlugin)
    add_dependencies(UltraNetTests UltraNetGrpcPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_GRPC_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetGrpcPlugin>")
endif()
if(TARGET UltraNetSipPlugin)
    add_dependencies(UltraNetTests UltraNetSipPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_SIP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetSipPlugin>")
endif()
if(TARGET UltraNetRtpPlugin)
    add_dependencies(UltraNetTests UltraNetRtpPlugin)
    target_compile_definitions(UltraNetTests PRIVATE
        ULTRANET_RTP_PLUGIN_PATH_DEFINE="$<TARGET_FILE:UltraNetRtpPlugin>")
endif()

# =============================================================================
# UltraNetApiStatus — per-function API status report.
#
# Complements UltraNetTests: where the test binary answers "does the code still
# behave?", this one answers "what is the state of every entry in the public
# surface on this machine?" — WORKING / IMPLEMENTED / NOT IMPLEMENTED / BROKEN.
# It brings its own HTTP + WebSocket origin (built on UltraNet's TCP API), so
# it needs neither Python nor internet access.
# =============================================================================
set(ULTRANET_APISTATUS_SOURCES
    ApiStatus/ApiStatusMain.cpp
    ApiStatus/ApiStatusReport.cpp
    ApiStatus/ProbeServer.cpp
    ApiStatus/ProbeCore.cpp
    ApiStatus/ProbeUrl.cpp
    ApiStatus/ProbeHttp.cpp
    ApiStatus/ProbeSession.cpp
    ApiStatus/ProbeSse.cpp
    ApiStatus/ProbeWebSocket.cpp
    ApiStatus/ProbeDns.cpp
    ApiStatus/ProbeSocket.cpp
    ApiStatus/ProbeTls.cpp
    ApiStatus/ProbeFtp.cpp
    ApiStatus/ProbeMime.cpp
    ApiStatus/ProbePlugins.cpp
)

add_executable(UltraNetApiStatus ${ULTRANET_APISTATUS_SOURCES})
target_compile_features(UltraNetApiStatus PRIVATE cxx_std_20)
ultranet_link_module(UltraNetApiStatus)
target_include_directories(UltraNetApiStatus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ApiStatus)

# The plug-in probe dlopen()s the plug-in DSOs. They need nothing from the
# host's symbol table any more (everything comes through the UltraNetPluginHost
# table); the probe keeps its symbols exported as UltraNetTests does.
set_target_properties(UltraNetApiStatus PROPERTIES ENABLE_EXPORTS ON)

# Where this build dropped its plug-in DSOs, so UltraNet_RefreshPlugins can be
# probed against real plug-ins regardless of the working directory.
target_compile_definitions(UltraNetApiStatus PRIVATE
    ULTRANET_PROBE_PLUGIN_DIR_DEFINE="${CMAKE_BINARY_DIR}/Plugins/UltraNet")

include(CTest)
add_test(NAME UltraNet COMMAND UltraNetTests)
# Fails only on BROKEN entries — IMPLEMENTED / NOT IMPLEMENTED are legitimate
# states for this environment, not test failures.
add_test(NAME UltraNetApiStatus COMMAND UltraNetApiStatus)

# A plug-in DSO must need no core symbol from its host: everything reaches the
# core through the UltraNetPluginHost table (Plugins/UltraNet/common/
# UltraNetPluginHostShim.cpp). A shared-core build would resolve a stray one
# anyway, so this reads the built plug-ins' undefined symbols instead of
# trusting a load to fail. On Linux and macOS it reads the undefined symbols
# (nm, c++filt); on Windows each DLL's import table (llvm-objdump / objdump),
# where a plug-in must import nothing from the core DLLs. --require when this
# build makes plug-ins, so an empty output folder fails instead of passing;
# the parser's own self-test runs alongside.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND (UNIX OR WIN32))
    set(_ultranet_plugin_require "")
    if(TARGET UltraNetImapPlugin OR TARGET UltraNetSmtpPlugin)
        set(_ultranet_plugin_require --require)
    endif()
    add_test(NAME UltraNetPluginHostImports
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/scripts/check_ultranet_plugin_imports.py
                     ${CMAKE_BINARY_DIR}/Plugins/UltraNet ${_ultranet_plugin_require})
    add_test(NAME UltraNetPluginHostImportsSelfTest
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/scripts/check_ultranet_plugin_imports.py --self-test)
endif()
