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
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${RABBITMQ_INCLUDE})
target_link_libraries(UltraNetAmqpPlugin PRIVATE ${RABBITMQ_LIB})

set_target_properties(UltraNetAmqpPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
