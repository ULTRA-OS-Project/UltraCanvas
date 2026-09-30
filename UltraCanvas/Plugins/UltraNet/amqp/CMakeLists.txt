# UltraCanvas/Plugins/UltraNet/amqp/CMakeLists.txt
# AMQP / AMQPS plug-in backed by rabbitmq-c (librabbitmq).
# Self-disables when the library isn't installed.

cmake_minimum_required(VERSION 3.16)

find_library(RABBITMQ_LIB rabbitmq)
find_path(RABBITMQ_INCLUDE amqp.h)

if(NOT RABBITMQ_LIB OR NOT RABBITMQ_INCLUDE)
    message(STATUS "    rabbitmq-c headers/lib missing - skipping AMQP plug-in")
    return()
endif()

add_library(UltraNetAmqpPlugin MODULE AmqpPlugin.cpp)

set_target_properties(UltraNetAmqpPlugin PROPERTIES
    OUTPUT_NAME "ultranet_amqp"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetAmqpPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetAmqpPlugin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIR}
    ${RABBITMQ_INCLUDE})
target_link_libraries(UltraNetAmqpPlugin PRIVATE ${RABBITMQ_LIB})

set_target_properties(UltraNetAmqpPlugin PROPERTIES
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
target_sources(UltraNetAmqpPlugin PRIVATE ${_ultranet_host_shim})
target_include_directories(UltraNetAmqpPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(NOT MSVC)
    set_source_files_properties(${_ultranet_host_shim} PROPERTIES
        COMPILE_OPTIONS "-fvisibility=hidden")
endif()
