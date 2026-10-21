// Tests/UltraDatabase/test_pg.h
// A live PostgreSQL connection for the tests that need a real server, from the
// same variables CI sets for UltraFIBU's multi-user tests
// (ULTRAFIBU_TEST_PG_HOST / _PORT / _USER / _DB; .github/workflows/build.yml).
// Without them a test reports SKIP and passes - unless
// ULTRAFIBU_TEST_PG_REQUIRED is set, as it is in CI, which turns "could not
// connect" into a failure, so a server that silently went missing cannot make
// the suite pass by not testing anything.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "test_framework.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ultradb_test {

inline const char* PgEnv(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

// Registers and opens a PostgreSQL connection called `name`. Returns false
// when the test should be skipped (it prints why); fails the test when a
// server is required and cannot be reached.
inline bool OpenPgTestConnection(const std::string& name) {
    const bool required = std::getenv("ULTRAFIBU_TEST_PG_REQUIRED") != nullptr;
    const std::string database = PgEnv("ULTRAFIBU_TEST_PG_DB", "");
    if (database.empty()) {
        REQUIRE(!required);   // CI promised a server and did not say where
        std::printf("         [SKIP] no PostgreSQL configured (ULTRAFIBU_TEST_PG_DB)\n");
        return false;
    }
    UltraDbConnectionConfig cfg;
    cfg.name     = name;
    cfg.driver   = "postgresql";
    cfg.database = database;
    cfg.host     = PgEnv("ULTRAFIBU_TEST_PG_HOST", "/var/run/postgresql");
    cfg.port     = std::atoi(PgEnv("ULTRAFIBU_TEST_PG_PORT", "5432"));
    cfg.user     = PgEnv("ULTRAFIBU_TEST_PG_USER", "");
    // A local test server, not a production connection: the driver defaults
    // to verify-full and the tests deliberately do not.
    cfg.tls      = UltraDbTls::Disable;
    UltraDb_CloseConnection(name);
    UltraDbResult registered = UltraDb_RegisterConnection(cfg);
    if (!registered) {
        // Built without libpq: there is no driver to test.
        REQUIRE(!required);
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
