# UltraCanvas/Plugins/Vector/CDR/CMakeLists.txt
# CorelDRAW CDR Plugin - Builds as separate static library
# Version: 1.0.0
# Last Modified: 2025-12-14
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# ===== PLUGIN DEFINITION =====
set(PLUGIN_NAME "UltraCanvasVectorlugin")
set(PLUGIN_VERSION "1.0.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== FIND DEPENDENCIES =====
# Only use PkgConfig on non-Windows platforms
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)

    # libcdr - CorelDRAW file format parser
    pkg_check_modules(LIBVIPS REQUIRED vips vips-cpp)
    pkg_check_modules(LIBZLIB REQUIRED zlib)
else()
    # On Windows, use find_package with vcpkg
    find_package(ZLIB REQUIRED)

    # libvips is not available on Windows vcpkg, mark as not found
    set(LIBVIPS_FOUND FALSE)

    if(ZLIB_FOUND)
        set(LIBZLIB_FOUND TRUE)
        set(LIBZLIB_LIBRARIES ZLIB::ZLIB)
    endif()
endif()

# ===== SOURCE FILES =====
set(VECTOR_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasVectorElement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasVectorRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasVectorStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasXARConverter.cpp
)

set(VECTOR_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasVectorElement.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${VECTOR_PLUGIN_SOURCES}
    ${VECTOR_PLUGIN_HEADERS}
)

# Set library properties
set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_vector_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/ultracanvas/plugins/vector>
    PRIVATE
        ${LIBVIPS_INCLUDE_DIRS}
        ${LIBZLIB_INCLUDE_DIRS}
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
        ULTRACANVAS_HAS_VECTOR_PLUGIN=1
    PRIVATE
        ULTRACANVAS_VECTOR_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PLUGIN_NAME} PRIVATE
#        -Wall -Wextra -Wpedantic
    )
endif()

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_VECTOR_PLUGIN_FOUND TRUE PARENT_SCOPE)
set(ULTRACANVAS_VECTOR_PLUGIN_TARGET ${PLUGIN_NAME} PARENT_SCOPE)
set(ULTRACANVAS_VECTOR_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasVectorPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${VECTOR_PLUGIN_HEADERS}
    DESTINATION include/ultracanvas/plugins/vector
)

# ===== STATUS OUTPUT =====
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
