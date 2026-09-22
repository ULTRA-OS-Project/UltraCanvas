# Tests/UltraMessage/CMakeLists.txt
# Builds UltraMessageTests. In-tree it is enabled by
# ULTRACANVAS_BUILD_ULTRAMESSAGE_TESTS and links the UltraMessage archive.
#
# It also configures on its own (cmake -S Tests/UltraMessage -B build), where
# it compiles the module, UltraCanvasJSON, the vendored yyjson and the
# UltraDatabase sources directly against the system sqlite3 — the same
# arrangement Tests/UltraFIBU uses, so the channel can be checked on a machine
# that cannot build the UI library (no pango, no vips).

cmake_minimum_required(VERSION 3.16)

if(NOT TARGET UltraMessage)
    project(UltraMessageTestsStandalone CXX C)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(_um_root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    get_filename_component(_um_root "${_um_root}" ABSOLUTE)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SQLITE3 REQUIRED sqlite3)

    add_library(UltraDatabase STATIC
        ${_um_root}/UltraCanvas/core/UltraDatabase/UltraDatabaseValue.cpp
        ${_um_root}/UltraCanvas/core/UltraDatabase/UltraDatabaseManager.cpp
        ${_um_root}/UltraCanvas/core/UltraDatabase/UltraDatabaseSqliteDriver.cpp
        # Without ULTRADATABASE_HAS_POSTGRES these compile to the stub the
        # manager's EnsureBuiltins() links against.
        ${_um_root}/UltraCanvas/core/UltraDatabase/UltraDatabasePostgresDriver.cpp
        ${_um_root}/UltraCanvas/core/UltraDatabase/UltraDatabasePostgresSql.cpp)
    target_include_directories(UltraDatabase PUBLIC ${_um_root}/UltraCanvas/include)
    target_include_directories(UltraDatabase PRIVATE ${SQLITE3_INCLUDE_DIRS})
    target_link_libraries(UltraDatabase PUBLIC Threads::Threads ${SQLITE3_LIBRARIES})
    target_link_directories(UltraDatabase PUBLIC ${SQLITE3_LIBRARY_DIRS})

    add_library(uc-yyjson STATIC ${_um_root}/UltraCanvas/third_party/yyjson/yyjson.c)
    target_include_directories(uc-yyjson PUBLIC ${_um_root}/UltraCanvas/third_party/yyjson)

    file(GLOB _um_sources ${_um_root}/UltraCanvas/core/UltraMessage/*.cpp)
    set(_um_gio_found FALSE)
    if(UNIX AND NOT APPLE)
        pkg_check_modules(UM_GIO QUIET gio-2.0)
        if(UM_GIO_FOUND)
            set(_um_gio_found TRUE)
            list(APPEND _um_sources
                ${_um_root}/UltraCanvas/OS/Linux/UltraMessage/UltraMessageFreedesktopNotifications.cpp)
        endif()
    endif()
    add_library(UltraMessage STATIC
        ${_um_sources}
        ${_um_root}/UltraCanvas/core/DataFormats/UltraCanvasJSON.cpp)
    target_include_directories(UltraMessage PUBLIC ${_um_root}/UltraCanvas/include)
    target_link_libraries(UltraMessage PUBLIC UltraDatabase Threads::Threads PRIVATE uc-yyjson)
    if(_um_gio_found)
        target_include_directories(UltraMessage PUBLIC ${UM_GIO_INCLUDE_DIRS})
        target_link_directories(UltraMessage PUBLIC ${UM_GIO_LIBRARY_DIRS})
        target_link_libraries(UltraMessage PUBLIC ${UM_GIO_LIBRARIES})
        target_compile_definitions(UltraMessage PUBLIC ULTRAMESSAGE_HAVE_GIO=1)
    endif()

    add_executable(ultramsg ${_um_root}/Apps/UltraMessageCli/main.cpp)
    target_link_libraries(ultramsg PRIVATE UltraMessage)
    set(_um_output_dir ${CMAKE_BINARY_DIR})
else()
    set(_um_output_dir ${CMAKE_BINARY_DIR}/bin)
endif()

# test_adapters.cpp compiles to nothing without ULTRAMESSAGE_HAVE_GIO, which
# the UltraMessage target exports wherever the notification adapter is built.
set(ULTRAMESSAGE_TEST_SOURCES
    test_main.cpp
    test_ultramessage.cpp
    test_adapters.cpp
)

add_executable(UltraMessageTests ${ULTRAMESSAGE_TEST_SOURCES})
target_compile_features(UltraMessageTests PRIVATE cxx_std_20)
target_link_libraries(UltraMessageTests PRIVATE UltraMessage)
target_include_directories(UltraMessageTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(UltraMessageTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${_um_output_dir})

include(CTest)
add_test(NAME UltraMessage COMMAND UltraMessageTests
         WORKING_DIRECTORY ${_um_output_dir})
