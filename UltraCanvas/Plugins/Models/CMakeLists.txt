# UltraCanvas/Plugins/Models/CMakeLists.txt
# 3D Model Formats Plugin - builds as a separate static library, like the
# Vector, CDR, XAR and EPS plugins.
#
# Every converter here reads into ModelStorage::ModelDocument, which lives in
# the core library (DataFormats) because it is a framework-wide structure. Only
# the format readers and writers are plugin-side, so an application that does
# not open 3D files does not compile or link them.
#
# Two formats are optional within the plugin, because each pulls a dependency
# the rest does not need:
#   COLLADA -> tinyxml2
#   .blend  -> zlib
# Both are detected here and reported; the plugin builds without either.
#
# Version: 1.0.0
# Last Modified: 2026-09-10
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# ===== PLUGIN DEFINITION =====
set(PLUGIN_NAME "UltraCanvasModelsPlugin")
set(PLUGIN_VERSION "1.0.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== OPTIONAL FORMAT DEPENDENCIES =====
option(ULTRACANVAS_MODELS_COLLADA "Build the COLLADA (.dae) reader (needs tinyxml2)" ON)
option(ULTRACANVAS_MODELS_BLEND   "Build the Blender (.blend) inspector (needs zlib)" ON)

find_package(PkgConfig QUIET)

set(MODELS_COLLADA_ENABLED FALSE)
if(ULTRACANVAS_MODELS_COLLADA AND PkgConfig_FOUND)
    pkg_check_modules(MODELS_TINYXML2 QUIET tinyxml2)
    if(MODELS_TINYXML2_FOUND)
        set(MODELS_COLLADA_ENABLED TRUE)
    endif()
endif()

set(MODELS_BLEND_ENABLED FALSE)
if(ULTRACANVAS_MODELS_BLEND)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        set(MODELS_BLEND_ENABLED TRUE)
    endif()
endif()

# ===== SOURCE FILES =====
# The formats with no dependency beyond the standard library.
set(MODELS_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasModelFormatsDispatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasModelFormatsPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasModelMesh3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3DS/UltraCanvas3DSConverter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OBJ/UltraCanvasOBJConverter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DXF/UltraCanvasDXFModelConverter.cpp
)

if(MODELS_COLLADA_ENABLED)
    list(APPEND MODELS_PLUGIN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/COLLADA/UltraCanvasColladaConverter.cpp)
endif()

if(MODELS_BLEND_ENABLED)
    list(APPEND MODELS_PLUGIN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Blend/UltraCanvasBlendFile.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Blend/UltraCanvasBlendConverter.cpp)
endif()

set(MODELS_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasModelFormatsPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasModelMesh3D.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${MODELS_PLUGIN_SOURCES}
    ${MODELS_PLUGIN_HEADERS}
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_models_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..          # "Models/<Format>/..." includes
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

if(MODELS_COLLADA_ENABLED)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${MODELS_TINYXML2_INCLUDE_DIRS})
    target_link_directories(${PLUGIN_NAME} PRIVATE ${MODELS_TINYXML2_LIBRARY_DIRS})
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${MODELS_TINYXML2_LIBRARIES})
endif()

if(MODELS_BLEND_ENABLED)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${ZLIB_LIBRARIES})
endif()

# ===== COMPILER SETTINGS =====
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)

target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        ULTRACANVAS_HAS_MODELS_PLUGIN=1
        # Per-format, so a caller can #ifdef on exactly what was built rather
        # than on the plugin as a whole.
        $<$<BOOL:${MODELS_COLLADA_ENABLED}>:ULTRACANVAS_HAS_COLLADA_CONVERTER=1>
        $<$<BOOL:${MODELS_BLEND_ENABLED}>:ULTRACANVAS_HAS_BLEND_CONVERTER=1>
    PRIVATE
        ULTRACANVAS_MODELS_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PLUGIN_NAME} PRIVATE -Wall -Wextra)
endif()

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_MODELS_PLUGIN_FOUND TRUE PARENT_SCOPE)
set(ULTRACANVAS_MODELS_PLUGIN_TARGET ${PLUGIN_NAME} PARENT_SCOPE)
set(ULTRACANVAS_MODELS_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasModelsPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${MODELS_PLUGIN_HEADERS}
    DESTINATION include/ultracanvas/plugins/models
)

# ===== STATUS OUTPUT =====
message(STATUS "  Formats: 3DS, OBJ (read+write), DXF 3D entities")
if(MODELS_COLLADA_ENABLED)
    message(STATUS "  [✓] COLLADA (.dae) - ENABLED")
else()
    message(STATUS "  [✗] COLLADA (.dae) - DISABLED (tinyxml2 not found or option off)")
endif()
if(MODELS_BLEND_ENABLED)
    message(STATUS "  [✓] Blender (.blend) inspection - ENABLED")
else()
    message(STATUS "  [✗] Blender (.blend) inspection - DISABLED (zlib not found or option off)")
endif()
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
