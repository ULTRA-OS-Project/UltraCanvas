// Tests/UltraDatabase/test_mysql.h
// A live MySQL or MariaDB connection for the tests that need a real server,
// from ULTRADB_TEST_MYSQL_HOST (a host, or the server's socket file),
// _PORT, _USER and _DB. Without them a test reports SKIP and passes - unless
// ULTRADB_TEST_MYSQL_REQUIRED is set, as it is in CI, which turns "could not
// connect" into a failure, so a server that silently went missing cannot make
// the suite pass by not testing anything. No password: the tests log in as
// the socket's own user (MariaDB's unix_socket, MySQL's auth_socket).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "test_framework.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ultradb_test {

inline const char* MysqlEnv(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

inline bool MysqlRequired() { return std::getenv("ULTRADB_TEST_MYSQL_REQUIRED") != nullptr; }

// Registers and opens a MySQL connection called `name`. Returns false when the
// test should be skipped (it prints why); fails the test when a server is
// required and cannot be reached.
inline bool OpenMysqlTestConnection(const std::string& name, bool readOnly = false) {
    const bool required = MysqlRequired();
    const std::string database = MysqlEnv("ULTRADB_TEST_MYSQL_DB", "");
    if (database.empty()) {
        REQUIRE(!required);
        std::printf("         [SKIP] no MySQL configured (ULTRADB_TEST_MYSQL_DB)\n");
        return false;
    }
    UltraDbConnectionConfig cfg;
    cfg.name     = name;
    cfg.driver   = "mysql";
    cfg.database = database;
    cfg.host     = MysqlEnv("ULTRADB_TEST_MYSQL_HOST", "/run/mysqld/mysqld.sock");
    cfg.port     = std::atoi(MysqlEnv("ULTRADB_TEST_MYSQL_PORT", "0"));
    cfg.user     = MysqlEnv("ULTRADB_TEST_MYSQL_USER", "");
    cfg.tls      = UltraDbTls::Disable;   // the local test server
    cfg.readOnly = readOnly;
    UltraDb_CloseConnection(name);
    UltraDbResult registered = UltraDb_RegisterConnection(cfg);
    if (!registered) {
        REQUIRE(!required);   // built without libmariadb
        std::printf("         [SKIP] %s\n", registered.message.c_str());
        return false;
    }
    UltraDbResult opened = UltraDb_OpenConnection(name);
    if (!opened) {
        if (required) {
            std::printf("         %s\n", opened.message.c_str());
            REQUIRE(opened.success);
        }
        std::printf("         [SKIP] %s\n", opened.message.c_str());
        return false;
    }
    return true;
}

} // namespace ultradb_test
