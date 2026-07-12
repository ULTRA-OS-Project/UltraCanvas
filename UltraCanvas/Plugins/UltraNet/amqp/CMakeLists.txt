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

# --- Windows: import host-provided UltraNet symbols at link time ---------------
# On POSIX these plug-ins resolve core symbols (UltraNet_ParseUrl, UltraNet_UrlDecode,
# ...) from the host binary at dlopen() time (RTLD_GLOBAL + -rdynamic). Windows DLLs
# must bind every symbol at link time, so link the shared UltraCanvas core, which
# exports them (WINDOWS_EXPORT_ALL_SYMBOLS). No-op on POSIX and on static builds.
if(WIN32 AND ULTRACANVAS_BUILD_SHARED)
    target_link_libraries(UltraNetAmqpPlugin PRIVATE UltraCanvas)
endif()
