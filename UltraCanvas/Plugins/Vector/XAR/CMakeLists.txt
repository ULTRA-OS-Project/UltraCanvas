# UltraCanvas/Plugins/Vector/CDR/CMakeLists.txt
# CorelDRAW CDR Plugin - Builds as separate static library
# Version: 1.0.0
# Last Modified: 2025-12-14
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# ===== PLUGIN DEFINITION =====
set(PLUGIN_NAME "UltraCanvasXARPlugin")
set(PLUGIN_VERSION "1.0.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== FIND DEPENDENCIES =====
find_package(PkgConfig REQUIRED)

# libcdr - CorelDRAW file format parser
pkg_check_modules(LIBVIPS REQUIRED vips vips-cpp)
pkg_check_modules(LIBZLIB REQUIRED zlib)

# ===== SOURCE FILES =====
set(XAR_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasXARPlugin.cpp
)

set(XAR_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasXARPlugin.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${XAR_PLUGIN_SOURCES}
    ${XAR_PLUGIN_HEADERS}
)

# Set library properties
set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_xar_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/ultracanvas/plugins/xar>
    PRIVATE
        ${LIBVIPS_INCLUDE_DIRS}
        ${LIBZLIB_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(${PLUGIN_NAME}
        PUBLIC
        ${LIBZLIB_LIBRARIES}
)

# Include UltraCanvas core headers (passed from parent)
if(DEFINED ULTRACANVAS_INCLUDE_DIR)
    target_include_directories(${PLUGIN_NAME} PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
    )
endif()

# ===== COMPILER SETTINGS =====
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)

target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        ULTRACANVAS_HAS_XAR_PLUGIN=1
    PRIVATE
        ULTRACANVAS_XAR_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PLUGIN_NAME} PRIVATE
#        -Wall -Wextra -Wpedantic
    )
endif()

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_XAR_PLUGIN_FOUND TRUE PARENT_SCOPE)
set(ULTRACANVAS_XAR_PLUGIN_TARGET ${PLUGIN_NAME} PARENT_SCOPE)
set(ULTRACANVAS_XAR_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasXARPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${XAR_PLUGIN_HEADERS}
    DESTINATION include/ultracanvas/plugins/xar
)

# ===== STATUS OUTPUT =====
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
