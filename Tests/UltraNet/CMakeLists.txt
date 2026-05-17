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
    test_pop3_plugin.cpp
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

include(CTest)
add_test(NAME UltraNet COMMAND UltraNetTests)
