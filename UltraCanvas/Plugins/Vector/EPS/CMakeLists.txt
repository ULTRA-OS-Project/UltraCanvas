# UltraCanvas/Plugins/Vector/EPS/CMakeLists.txt
# Encapsulated PostScript (EPS) Plugin - Builds as separate static library.
# Self-contained: the embedded PostScript interpreter needs no external
# libraries beyond the UltraCanvas core.
# Version: 1.0.0
# Last Modified: 2026-08-26
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# ===== PLUGIN DEFINITION =====
set(PLUGIN_NAME "UltraCanvasEPSPlugin")
set(PLUGIN_VERSION "1.0.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== FIND DEPENDENCIES =====
# zlib decodes FlateDecode image data streams.
find_package(ZLIB REQUIRED)

# ===== SOURCE FILES =====
set(EPS_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasEPSPlugin.cpp
)

set(EPS_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasEPSPlugin.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${EPS_PLUGIN_SOURCES}
    ${EPS_PLUGIN_HEADERS}
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_eps_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/ultracanvas/plugins/eps>
)

target_link_libraries(${PLUGIN_NAME}
    PUBLIC
        ZLIB::ZLIB
    PRIVATE
        UltraCanvas   # core: declares the link edge so the static plugin's core
                      # symbol references resolve at final link (GNU/MinGW ld order).
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
        ULTRACANVAS_HAS_EPS_PLUGIN=1
    PRIVATE
        ULTRACANVAS_EPS_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_EPS_PLUGIN_FOUND TRUE PARENT_SCOPE)
set(ULTRACANVAS_EPS_PLUGIN_TARGET ${PLUGIN_NAME} PARENT_SCOPE)
set(ULTRACANVAS_EPS_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasEPSPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${EPS_PLUGIN_HEADERS}
    DESTINATION include/ultracanvas/plugins/eps
)

# ===== STATUS OUTPUT =====
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
