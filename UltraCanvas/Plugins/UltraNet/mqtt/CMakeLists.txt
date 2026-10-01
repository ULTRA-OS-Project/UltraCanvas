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
    ${ULTRACANVAS_INCLUDE_DIR}
    ${PAHO_MQTT_INCLUDE})
target_link_libraries(UltraNetMqttPlugin PRIVATE ${PAHO_MQTT_LIB})

set_target_properties(UltraNetMqttPlugin PROPERTIES
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
target_sources(UltraNetMqttPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetMqttPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
