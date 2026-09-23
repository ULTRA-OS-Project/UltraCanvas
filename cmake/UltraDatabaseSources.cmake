# cmake/UltraDatabaseSources.cmake
# The one list of UltraDatabase's source files. The in-tree build
# (UltraCanvas/CMakeLists.txt) and every standalone test tree that compiles
# the module against the system sqlite3 (Tests/UltraMessage) take it from
# here, so adding a driver is one edit and no tree drifts.
#
#   ultradatabase_sources(<out-var> <ultracanvas-dir>)
#
# `<ultracanvas-dir>` is the UltraCanvas/ directory of the checkout (the one
# holding core/ and include/). The Postgres files are always in the list:
# without ULTRADATABASE_HAS_POSTGRES they compile to the stub the manager's
# EnsureBuiltins() links against.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

function(ultradatabase_sources out_var ultracanvas_dir)
    set(${out_var}
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseValue.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseManager.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabaseSqliteDriver.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabasePostgresDriver.cpp
        ${ultracanvas_dir}/core/UltraDatabase/UltraDatabasePostgresSql.cpp
        PARENT_SCOPE)
endfunction()
