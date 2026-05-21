# UltraCanvas/Plugins/Documents/OCR/CMakeLists.txt
# OCR Plugin (Tesseract) - Builds as separate static library
# Version: 0.1.0
# Last Modified: 2026-05-21
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

set(PLUGIN_NAME "UltraCanvasOCRPlugin")
set(PLUGIN_VERSION "0.1.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== FIND DEPENDENCIES =====
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
endif()

pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)
pkg_check_modules(LIBVIPS  REQUIRED vips vips-cpp)

if(NOT TESSERACT_FOUND)
    message(FATAL_ERROR "Tesseract not found. Install with: sudo apt install libtesseract-dev libleptonica-dev")
endif()

# ===== SOURCE FILES =====
set(OCR_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UltraCanvasOCRPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TesseractOCREngine.cpp
)

set(OCR_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/UltraCanvasOCRPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TesseractOCREngine.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${OCR_PLUGIN_SOURCES}
    ${OCR_PLUGIN_HEADERS}
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_ocr_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/ultracanvas/plugins/ocr>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${TESSERACT_INCLUDE_DIRS}
        ${LEPTONICA_INCLUDE_DIRS}
        ${LIBVIPS_INCLUDE_DIRS}
)

if(DEFINED ULTRACANVAS_INCLUDE_DIR)
    target_include_directories(${PLUGIN_NAME} PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
    )
endif()

# ===== LINK LIBRARIES =====
target_link_directories(${PLUGIN_NAME}
    PUBLIC
        ${TESSERACT_LIBRARY_DIRS}
        ${LEPTONICA_LIBRARY_DIRS}
)

target_link_libraries(${PLUGIN_NAME}
    PUBLIC
        ${TESSERACT_LIBRARIES}
        ${LEPTONICA_LIBRARIES}
)

# ===== COMPILER SETTINGS =====
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)

target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        ULTRACANVAS_HAS_OCR_PLUGIN=1
        ULTRACANVAS_OCR_SUPPORT=1
        ULTRACANVAS_OCR_TESSERACT_SUPPORT=1
    PRIVATE
        ULTRACANVAS_OCR_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_OCR_PLUGIN_FOUND TRUE PARENT_SCOPE)
set(ULTRACANVAS_OCR_PLUGIN_TARGET ${PLUGIN_NAME} PARENT_SCOPE)
set(ULTRACANVAS_OCR_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasOCRPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/UltraCanvasOCRPlugin.h
    DESTINATION include/ultracanvas/plugins/ocr
)

# ===== STATUS OUTPUT =====
message(STATUS "  Tesseract version: ${TESSERACT_VERSION}")
message(STATUS "  Leptonica version: ${LEPTONICA_VERSION}")
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
