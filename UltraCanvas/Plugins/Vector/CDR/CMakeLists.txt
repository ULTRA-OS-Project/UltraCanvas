# UltraCanvas/Plugins/Vector/CDR/CMakeLists.txt
# CorelDRAW CDR Plugin - Builds as separate static library
# Version: 1.1.0
# Last Modified: 2026-09-26
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)

# ===== PLUGIN DEFINITION =====
set(PLUGIN_NAME "UltraCanvasCDRPlugin")
set(PLUGIN_VERSION "1.0.0")

message(STATUS "Configuring ${PLUGIN_NAME} v${PLUGIN_VERSION}")

# ===== FIND DEPENDENCIES =====
# Only use PkgConfig on non-Windows platforms
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
endif()

# librevenge - Document conversion framework
pkg_check_modules(LIBREVENGE REQUIRED librevenge-0.0)
pkg_check_modules(LIBREVENGE_STREAM REQUIRED librevenge-stream-0.0)
if(NOT LIBREVENGE_FOUND)
    message(FATAL_ERROR "librevenge not found. Install with: sudo apt install librevenge-dev")
endif()

if(APPLE)
    # ICU is keg-only on Homebrew, find it explicitly
    execute_process(
            COMMAND brew --prefix icu4c
            OUTPUT_VARIABLE ICU_HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if(ICU_HOMEBREW_PREFIX)
        set(ENV{PKG_CONFIG_PATH} "${ICU_HOMEBREW_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        link_directories(${ICU_HOMEBREW_PREFIX}/lib)
    endif()
endif()

# Optional dependencies
pkg_check_modules(LCMS2 lcms2)
pkg_check_modules(ICU icu-uc icu-i18n)
pkg_check_modules(CDR_ZLIB zlib)

# libcdr - CorelDRAW file format parser. The patched copy in
# third_party/libcdr (bitmap transparency, PowerClip - see its README) is
# built when its dependencies are here; the system package is the fallback.
option(ULTRACANVAS_USE_VENDORED_LIBCDR "Build the patched libcdr in third_party/libcdr" ON)
set(ULTRACANVAS_LIBCDR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/libcdr")
find_path(ULTRACANVAS_BOOST_INCLUDE_DIR boost/optional.hpp
          HINTS "$ENV{HOMEBREW_PREFIX}/include" /opt/homebrew/include /usr/local/include)
set(CDR_USE_VENDORED FALSE)
if(ULTRACANVAS_USE_VENDORED_LIBCDR AND EXISTS "${ULTRACANVAS_LIBCDR_DIR}/src/CDRParser.cpp"
   AND LCMS2_FOUND AND ICU_FOUND AND CDR_ZLIB_FOUND AND ULTRACANVAS_BOOST_INCLUDE_DIR)
    set(CDR_USE_VENDORED TRUE)
    file(GLOB ULTRACANVAS_LIBCDR_SOURCES "${ULTRACANVAS_LIBCDR_DIR}/src/*.cpp")
    add_library(ultracanvas_libcdr STATIC ${ULTRACANVAS_LIBCDR_SOURCES})
    set_target_properties(ultracanvas_libcdr PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_VISIBILITY_PRESET hidden)
    target_compile_definitions(ultracanvas_libcdr PRIVATE LIBCDR_BUILD=1)
    target_include_directories(ultracanvas_libcdr
        PUBLIC  "${ULTRACANVAS_LIBCDR_DIR}/inc" ${LIBREVENGE_INCLUDE_DIRS}
        PRIVATE "${ULTRACANVAS_LIBCDR_DIR}/src" ${LIBREVENGE_STREAM_INCLUDE_DIRS}
                ${LCMS2_INCLUDE_DIRS} ${ICU_INCLUDE_DIRS} ${CDR_ZLIB_INCLUDE_DIRS}
                ${ULTRACANVAS_BOOST_INCLUDE_DIR})
    target_link_directories(ultracanvas_libcdr PUBLIC
        ${LIBREVENGE_LIBRARY_DIRS} ${LIBREVENGE_STREAM_LIBRARY_DIRS}
        ${LCMS2_LIBRARY_DIRS} ${ICU_LIBRARY_DIRS} ${CDR_ZLIB_LIBRARY_DIRS})
    target_link_libraries(ultracanvas_libcdr PUBLIC
        ${LIBREVENGE_LIBRARIES} ${LIBREVENGE_STREAM_LIBRARIES}
        ${LCMS2_LIBRARIES} ${ICU_LIBRARIES} ${CDR_ZLIB_LIBRARIES})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ultracanvas_libcdr PRIVATE -w)   # third-party code: its warnings are upstream's
    endif()
    set(LIBCDR_LIBRARIES ultracanvas_libcdr)
    set(LIBCDR_INCLUDE_DIRS "${ULTRACANVAS_LIBCDR_DIR}/inc")
    set(LIBCDR_VERSION "0.1.7+ (vendored, patched)")
else()
    pkg_check_modules(LIBCDR REQUIRED libcdr-0.1)
    if(NOT LIBCDR_FOUND)
        message(FATAL_ERROR "libcdr not found. Install with: sudo apt install libcdr-dev")
    endif()
    if(ULTRACANVAS_USE_VENDORED_LIBCDR)
        message(STATUS "  Vendored libcdr needs lcms2, ICU, zlib and the Boost headers; using the system libcdr "
                       "(no PowerClip, bitmap masks restored by the Vector plugin)")
    endif()
endif()

# ===== SOURCE FILES =====
set(CDR_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasCDRPlugin.cpp
)

set(CDR_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasCDRPlugin.h
)

# ===== BUILD STATIC LIBRARY =====
add_library(${PLUGIN_NAME} STATIC
    ${CDR_PLUGIN_SOURCES}
    ${CDR_PLUGIN_HEADERS}
)

# Set library properties
set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvas_cdr_plugin"
)

# ===== INCLUDE DIRECTORIES =====
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/ultracanvas/plugins/cdr>
    PRIVATE
        ${LIBCDR_INCLUDE_DIRS}
        ${LIBREVENGE_INCLUDE_DIRS}
        ${LIBREVENGE_STREAM_INCLUDE_DIRS}
)

# Include UltraCanvas core headers (passed from parent)
if(DEFINED ULTRACANVAS_INCLUDE_DIR)
    target_include_directories(${PLUGIN_NAME} PRIVATE
        ${ULTRACANVAS_INCLUDE_DIR}
    )
endif()

# Optional includes
if(LCMS2_FOUND)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${LCMS2_INCLUDE_DIRS})
endif()

if(ICU_FOUND)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${ICU_INCLUDE_DIRS})
endif()

# ===== LINK LIBRARIES =====
target_link_libraries(${PLUGIN_NAME}
    PUBLIC
        ${LIBCDR_LIBRARIES}
        ${LIBREVENGE_LIBRARIES}
        ${LIBREVENGE_STREAM_LIBRARIES}
    PRIVATE
        UltraCanvas   # core: declares the link edge so the static plugin's core
                      # symbol references resolve at final link (GNU/MinGW ld order).
)

if(LCMS2_FOUND)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${LCMS2_LIBRARIES})
endif()

if(ICU_FOUND)
    target_link_directories(${PLUGIN_NAME} PUBLIC ${ICU_LIBRARY_DIRS})
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${ICU_LIBRARIES})
endif()

# ===== COMPILER SETTINGS =====
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)

target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        ULTRACANVAS_HAS_CDR_PLUGIN=1
        $<$<BOOL:${CDR_USE_VENDORED}>:ULTRACANVAS_VENDORED_LIBCDR=1>
    PRIVATE
        ULTRACANVAS_CDR_PLUGIN_VERSION="${PLUGIN_VERSION}"
)

# Warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PLUGIN_NAME} PRIVATE
#        -Wall -Wextra -Wpedantic
    )
endif()

# ===== EXPORT VARIABLES TO PARENT =====
set(ULTRACANVAS_CDR_PLUGIN_FOUND TRUE PARENT_SCOPE)
set(ULTRACANVAS_CDR_PLUGIN_TARGET ${PLUGIN_NAME} PARENT_SCOPE)
set(ULTRACANVAS_CDR_PLUGIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

# ===== INSTALLATION =====
install(TARGETS ${PLUGIN_NAME}
    EXPORT UltraCanvasCDRPluginTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${CDR_PLUGIN_HEADERS}
    DESTINATION include/ultracanvas/plugins/cdr
)

# ===== STATUS OUTPUT =====
message(STATUS "  libcdr version: ${LIBCDR_VERSION}")
message(STATUS "  librevenge version: ${LIBREVENGE_VERSION}")
message(STATUS "  Output library: lib${PLUGIN_NAME}.a")
