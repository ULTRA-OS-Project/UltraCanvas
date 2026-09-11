# UltraCanvas/Plugins/Models/CMakeLists.txt
# 3D Model Formats Plugin - builds as a separate static library, like the
# Vector, CDR, XAR and EPS plugins.
#
# Every converter here reads into ModelStorage::ModelDocument, which lives in
# the core library (DataFormats) because it is a framework-wide structure. Only
# the format readers and writers are plugin-side, so an application that does
# not open 3D files does not compile or link them.
#
# Three formats are optional within the plugin, because each pulls a dependency
# the rest does not need:
#   COLLADA -> tinyxml2
#   X3D     -> tinyxml2
#   .blend  -> zlib
# All are detected here and reported; the plugin builds without any of them.
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
option(ULTRACANVAS_MODELS_X3D     "Build the X3D (.x3d) reader (needs tinyxml2)" ON)
option(ULTRACANVAS_MODELS_BLEND   "Build the Blender (.blend) inspector (needs zlib)" ON)

find_package(PkgConfig QUIET)

# One probe for both XML readers - they need the same library, and a second
# pkg_check_modules for it would only be a second chance to spell it wrong.
if(PkgConfig_FOUND AND (ULTRACANVAS_MODELS_COLLADA OR ULTRACANVAS_MODELS_X3D))
    pkg_check_modules(MODELS_TINYXML2 QUIET tinyxml2)
endif()

set(MODELS_COLLADA_ENABLED FALSE)
if(ULTRACANVAS_MODELS_COLLADA AND MODELS_TINYXML2_FOUND)
    set(MODELS_COLLADA_ENABLED TRUE)
endif()

set(MODELS_X3D_ENABLED FALSE)
if(ULTRACANVAS_MODELS_X3D AND MODELS_TINYXML2_FOUND)
    set(MODELS_X3D_ENABLED TRUE)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/STEP/UltraCanvasStepFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/STEP/UltraCanvasStepConverter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Alembic/UltraCanvasOgawaFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Alembic/UltraCanvasAlembicConverter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PLY/UltraCanvasPLYConverter.cpp
)

if(MODELS_COLLADA_ENABLED)
    list(APPEND MODELS_PLUGIN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/COLLADA/UltraCanvasColladaConverter.cpp)
endif()

if(MODELS_X3D_ENABLED)
    list(APPEND MODELS_PLUGIN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/X3D/UltraCanvasX3DConverter.cpp)
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

# The core library, for its include paths rather than its symbols - the same
# edge every other plugin declares.
#
# UltraCanvasModelFormatsPlugin.h reaches UltraCanvasImage.h, which includes
# <cairo/cairo.h>, and the path to Cairo is carried on the core target as a
# PUBLIC include directory. Without this edge the plugin compiles anywhere
# Cairo happens to sit on the compiler's default search path (/usr/include on
# Linux) and fails everywhere it does not - which is exactly what macOS did,
# where Homebrew puts it under /opt/homebrew/include.
#
# Guarded because this directory also configures standalone, for building and
# testing the converters without the rest of the framework; there the core
# target does not exist and the headers that need Cairo are not reached.
if(TARGET UltraCanvas)
    target_link_libraries(${PLUGIN_NAME} PRIVATE UltraCanvas)
endif()

if(DEFINED ULTRACANVAS_INCLUDE_DIR)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${ULTRACANVAS_INCLUDE_DIR})
endif()

if(MODELS_COLLADA_ENABLED OR MODELS_X3D_ENABLED)
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
        $<$<BOOL:${MODELS_X3D_ENABLED}>:ULTRACANVAS_HAS_X3D_CONVERTER=1>
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
message(STATUS "  Formats: 3DS, OBJ (read+write), DXF 3D entities, STEP B-rep (read+write), Alembic, PLY (read+write)")
if(MODELS_COLLADA_ENABLED)
    message(STATUS "  [✓] COLLADA (.dae) - ENABLED")
else()
    message(STATUS "  [✗] COLLADA (.dae) - DISABLED (tinyxml2 not found or option off)")
endif()
if(MODELS_X3D_ENABLED)
    message(STATUS "  [✓] X3D (.x3d) - ENABLED")
else()
    message(STATUS "  [✗] X3D (.x3d) - DISABLED (tinyxml2 not found or option off)")
endif()
if(MODELS_BLEND_ENABLED)
    message(STATUS "  [✓] Blender (.blend) inspection - ENABLED")
else()
    message(STATUS "  [✗] Blender (.blend) inspection - DISABLED (zlib not found or option off)")
endif()
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
