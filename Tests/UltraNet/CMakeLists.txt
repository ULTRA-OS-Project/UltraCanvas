# Tests/UltraNet/CMakeLists.txt
# Builds the UltraNetTests binary. Enabled by ULTRACANVAS_BUILD_NET_TESTS.

cmake_minimum_required(VERSION 3.16)

set(ULTRANET_TEST_SOURCES
    test_main.cpp
    test_url.cpp
    test_headers.cpp
    test_result.cpp
    test_plugins.cpp
    test_dns_format.cpp
    test_ftp_parser.cpp
    test_loopback.cpp
    test_smtp_plugin.cpp
    test_imap_plugin.cpp
    test_imap_mailbox.cpp
    test_pop3_plugin.cpp
    test_extra_plugins.cpp
    test_sse.cpp
    test_websocket.cpp
)

add_executable(UltraNetTests ${ULTRANET_TEST_SOURCES})

target_compile_features(UltraNetTests PRIVATE cxx_std_20)
target_link_libraries(UltraNetTests PRIVATE UltraNet)
target_include_directories(UltraNetTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# Loopback test needs -rdynamic on Linux so a future DSO-based test can
# resolve UltraNet_RegisterPlugin from the main binary symbols.
if(UNIX AND NOT APPLE)
    target_link_options(UltraNetTests PRIVATE -rdynamic)
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

include(CTest)
add_test(NAME UltraNet COMMAND UltraNetTests)
