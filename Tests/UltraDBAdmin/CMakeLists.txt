# Tests/UltraDBAdmin/CMakeLists.txt
# UltraDBAdmin's engine - the connection store and its vault, cell
# formatting, the Browse pager, the SQL runner - without a window. Compiles
# the engine sources directly, as Tests/UltraFIBU does, so it runs where the
# UI library cannot be built. Enabled by ULTRACANVAS_BUILD_ULTRADBADMIN_TESTS.
# Version: 0.3.0 - the schema suite (relations, routines, server), a third
# Version: 0.2.0 - the operations suite, a second program
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

cmake_minimum_required(VERSION 3.16)
enable_language(C)

set(_dbadmin ${CMAKE_SOURCE_DIR}/Apps/UltraDBAdmin)
set(_dbadmin_engine
    ${_dbadmin}/engine/DbAdminBlob.cpp
    ${_dbadmin}/engine/DbAdminConnections.cpp
    ${_dbadmin}/engine/DbAdminDump.cpp
    ${_dbadmin}/engine/DbAdminEditBuffer.cpp
    ${_dbadmin}/engine/DbAdminExport.cpp
    ${_dbadmin}/engine/DbAdminFormat.cpp
    ${_dbadmin}/engine/DbAdminImport.cpp
    ${_dbadmin}/engine/DbAdminJobQueue.cpp
    ${_dbadmin}/engine/DbAdminOperations.cpp
    ${_dbadmin}/engine/DbAdminPaths.cpp
    ${_dbadmin}/engine/DbAdminRelations.cpp
    ${_dbadmin}/engine/DbAdminRoutines.cpp
    ${_dbadmin}/engine/DbAdminSearch.cpp
    ${_dbadmin}/engine/DbAdminServer.cpp
    ${_dbadmin}/engine/DbAdminSnippets.cpp
    ${_dbadmin}/engine/DbAdminSqlLog.cpp
    ${_dbadmin}/engine/DbAdminSqlRunner.cpp
    ${_dbadmin}/engine/DbAdminSqlScript.cpp
    ${_dbadmin}/engine/DbAdminSqliteTable.cpp
    ${_dbadmin}/engine/DbAdminStructureEdit.cpp
    ${_dbadmin}/engine/DbAdminTablePager.cpp
    ${_dbadmin}/engine/DbAdminValueTypes.cpp
    # The connection list is JSON; the wrapper and the parser it wraps.
    ${CMAKE_SOURCE_DIR}/UltraCanvas/core/DataFormats/UltraCanvasJSON.cpp
    ${CMAKE_SOURCE_DIR}/UltraCanvas/third_party/yyjson/yyjson.c
)
find_package(Threads REQUIRED)
include(CTest)

# The engine suite, and the operations suite (the SQL log, operations,
# search, the schema dump, snippets, the console's helpers, the row form's
# types), and the schema suite (relations, routines, the server views):
# three programs over the same sources.
foreach(_suite UltraDBAdminEngineTests UltraDBAdminOperationsTests UltraDBAdminSchemaTests)
    add_executable(${_suite} ${_suite}.cpp ${_dbadmin_engine})
    target_compile_features(${_suite} PRIVATE cxx_std_20)
    target_include_directories(${_suite} PRIVATE
        ${_dbadmin}/engine
        ${CMAKE_SOURCE_DIR}/UltraCanvas/include
        ${CMAKE_SOURCE_DIR}/UltraCanvas/third_party/yyjson)
    target_link_libraries(${_suite} PRIVATE UltraDatabase UltraVault Threads::Threads)
    set_target_properties(${_suite} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()

add_test(NAME UltraDBAdminEngine COMMAND UltraDBAdminEngineTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME UltraDBAdminOperations COMMAND UltraDBAdminOperationsTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME UltraDBAdminSchema COMMAND UltraDBAdminSchemaTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
