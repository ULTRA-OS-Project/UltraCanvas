# UltraCanvas/Plugins/UltraNet/mqtt/CMakeLists.txt
# MQTT / MQTTS plug-in. Backend: Eclipse Paho's libpaho-mqtt3as (async +
# SSL). Skipped at configure time when paho-mqtt-c isn't installed.

cmake_minimum_required(VERSION 3.16)

find_library(PAHO_MQTT_LIB NAMES paho-mqtt3as paho-mqtt3a)
find_path(PAHO_MQTT_INCLUDE MQTTAsync.h)

if(NOT PAHO_MQTT_LIB OR NOT PAHO_MQTT_INCLUDE)
    message(STATUS "    paho-mqtt-c headers/lib missing — skipping MQTT plug-in")
    return()
endif()

add_library(UltraNetMqttPlugin MODULE MqttPlugin.cpp)

set_target_properties(UltraNetMqttPlugin PROPERTIES
    OUTPUT_NAME "ultranet_mqtt"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetMqttPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetMqttPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${PAHO_MQTT_INCLUDE})
target_link_libraries(UltraNetMqttPlugin PRIVATE ${PAHO_MQTT_LIB})

set_target_properties(UltraNetMqttPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetMqttPlugin PRIVATE UltraCanvas)
endif()
