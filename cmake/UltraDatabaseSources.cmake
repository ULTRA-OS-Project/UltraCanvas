# cmake/UltraDatabaseSources.cmake
# The one list of UltraDatabase's source files. The in-tree build
# (UltraCanvas/CMakeLists.txt) and every standalone test tree that compiles
# the module against the system sqlite3 (Tests/UltraMessage) take it from
# here, so adding a driver is one edit and no tree drifts.
#
#   ultradatabase_sources(<out-var> <ultracanvas-dir>)
#
# `<ultracanvas-dir>` is the UltraCanvas/ directory of the checkout (the one
# holding core/ and include/). The Postgres and MySQL files are always in the
# list: without ULTRADATABASE_HAS_POSTGRES / ULTRADATABASE_HAS_MYSQL they
# compile to the stubs the manager's EnsureBuiltins() links against.
# Version: 0.4.0 - the encrypting SQLite VFS (UltraDatabaseEncryption.h)
# Version: 0.3.0 - the relay (UltraDatabaseRelay.h): its wire format, driver and server
# Version: 0.2.0 - the MySQL / MariaDB driver, the server views
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

function(ultradatabase_sources out_var ultracanvas_dir)
    set(${out_var}
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseValue.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseManager.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseSqliteDriver.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseSqliteSchema.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseSqliteCipher.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabasePostgresDriver.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabasePostgresSql.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabasePostgresSchema.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseMysqlDriver.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseMysqlSql.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseMysqlSchema.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseServerViews.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseRelayJson.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseRelayClient.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseRelayServer.cpp
        PARENT_SCOPE)
endfunction()

# SQLite's column-origin functions (sqlite3_column_table_name and friends)
# are declared by every sqlite3.h but exist in the library only when it was
# built with SQLITE_ENABLE_COLUMN_METADATA - which Debian / Ubuntu, Homebrew
# and MSYS2 do, and a minimal build may not. Linking a call to them is the
# only reliable test, so this tries exactly that and defines
# ULTRADATABASE_SQLITE_COLUMN_METADATA on `target` when it links. Without it
# result columns still carry their declared type, only not their source table.
#
#   ultradatabase_sqlite_features(<target> "<include-dirs>" "<libraries>" "<library-dirs>")
function(ultradatabase_sqlite_features target include_dirs libraries library_dirs)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${include_dirs})
    set(CMAKE_REQUIRED_LIBRARIES ${libraries})
    if(library_dirs)
        foreach(_dir ${library_dirs})
            list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-L${_dir}")
        endforeach()
    endif()
    check_cxx_source_compiles("
        #include <sqlite3.h>
        int main() {
            sqlite3_stmt* stmt = nullptr;
            return sqlite3_column_table_name(stmt, 0) != nullptr;
        }" ULTRADATABASE_SQLITE_COLUMN_METADATA)
    if(ULTRADATABASE_SQLITE_COLUMN_METADATA)
        target_compile_definitions(${target} PRIVATE ULTRADATABASE_SQLITE_COLUMN_METADATA=1)
    endif()
endfunction()
