# Tests/UltraDBAdmin/CMakeLists.txt
# UltraDBAdmin's engine - the connection store and its vault, cell
# formatting, the Browse pager, the SQL runner - without a window. Compiles
# the engine sources directly, as Tests/UltraFIBU does, so it runs where the
# UI library cannot be built. Enabled by ULTRACANVAS_BUILD_ULTRADBADMIN_TESTS.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

cmake_minimum_required(VERSION 3.16)
enable_language(C)

set(_dbadmin ${CMAKE_SOURCE_DIR}/Apps/UltraDBAdmin)
add_executable(UltraDBAdminEngineTests
    UltraDBAdminEngineTests.cpp
    ${_dbadmin}/engine/DbAdminConnections.cpp
    ${_dbadmin}/engine/DbAdminFormat.cpp
    ${_dbadmin}/engine/DbAdminJobQueue.cpp
    ${_dbadmin}/engine/DbAdminPaths.cpp
    ${_dbadmin}/engine/DbAdminSqlRunner.cpp
    ${_dbadmin}/engine/DbAdminTablePager.cpp
    # The connection list is JSON; the wrapper and the parser it wraps.
    ${CMAKE_SOURCE_DIR}/UltraCanvas/core/DataFormats/UltraCanvasJSON.cpp
    ${CMAKE_SOURCE_DIR}/UltraCanvas/third_party/yyjson/yyjson.c
)
target_compile_features(UltraDBAdminEngineTests PRIVATE cxx_std_20)
target_include_directories(UltraDBAdminEngineTests PRIVATE
    ${_dbadmin}/engine
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${CMAKE_SOURCE_DIR}/UltraCanvas/third_party/yyjson)
target_link_libraries(UltraDBAdminEngineTests PRIVATE UltraDatabase UltraVault)
find_package(Threads REQUIRED)
target_link_libraries(UltraDBAdminEngineTests PRIVATE Threads::Threads)
set_target_properties(UltraDBAdminEngineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(CTest)
add_test(NAME UltraDBAdminEngine COMMAND UltraDBAdminEngineTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
