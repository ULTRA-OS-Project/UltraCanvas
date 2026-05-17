# UltraCanvas/Plugins/UltraNet/snmp/CMakeLists.txt
# SNMP plug-in backed by net-snmp (libsnmp). Self-disables when
# libsnmp-dev / net-snmp isn't installed.

cmake_minimum_required(VERSION 3.16)

find_library(NETSNMP_LIB NAMES snmp netsnmp)
find_path(NETSNMP_INCLUDE net-snmp/net-snmp-config.h)

if(NOT NETSNMP_LIB OR NOT NETSNMP_INCLUDE)
    message(STATUS "    net-snmp headers/lib missing - skipping SNMP plug-in")
    return()
endif()

add_library(UltraNetSnmpPlugin MODULE SnmpPlugin.cpp)

set_target_properties(UltraNetSnmpPlugin PROPERTIES
    OUTPUT_NAME "ultranet_snmp"
    PREFIX      ""
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(UltraNetSnmpPlugin PRIVATE cxx_std_20)
target_include_directories(UltraNetSnmpPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${NETSNMP_INCLUDE})
target_link_libraries(UltraNetSnmpPlugin PRIVATE ${NETSNMP_LIB})

set_target_properties(UltraNetSnmpPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/UltraNet)
