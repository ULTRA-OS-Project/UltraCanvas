// Tests/UltraDBAdmin/UltraDBAdminTunnelTests.cpp
// UltraDBAdmin's connections through a real SSH tunnel: the app's own
// connection store (an in-memory vault) with a bastion, the private key in
// the vault, UltraDatabase opening the tunnel through UltraNet's SSH plug-in
// (UltraDatabaseSshTunnel.h), and a PostgreSQL and a MySQL / MariaDB server
// reached through it over TCP.
//
// The first connection meets the bastion's key unknown and is refused with
// the key's fingerprint; trusted, the connection opens and queries run - a
// cursor's pages, the second session of the SQL console; a key that differs
// is refused; MySQL asked to verify a certificate through the tunnel is
// refused before connecting.
//
// The bastion: ULTRANET_TEST_SSH_URL (ssh://user@host:port) and
// ULTRANET_TEST_SSH_KEY (a private key file that signs in there). The
// databases, as the bastion reaches them on 127.0.0.1:
// ULTRADB_TEST_TUNNEL_PG_USER / _PG_PASSWORD / _PG_DB and
// ULTRADB_TEST_TUNNEL_MYSQL_USER / _MYSQL_PASSWORD / _MYSQL_DB. Without them
// the suite skips; ULTRADB_TEST_TUNNEL_REQUIRED, as in CI, makes that a
// failure.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraDatabase/UltraDatabaseSshTunnel.h>
#include <UltraNet/UltraNetPlugins.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace UltraDBAdmin;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) {
        std::printf("  ok    %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string ReadFile(const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buffer[4096];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) text.append(buffer, n);
        std::fclose(f);
    }
    return text;
}

struct Bastion {
    std::string host, user, key;
    int port = 22;
};

// ssh://user@host:port
bool ParseBastion(const std::string& url, Bastion& out) {
    const std::string scheme = "ssh://";
    if (url.rfind(scheme, 0) != 0) return false;
    std::string rest = url.substr(scheme.size());
    const size_t at = rest.find('@');
    if (at != std::string::npos) {
        out.user = rest.substr(0, at);
        rest = rest.substr(at + 1);
    }
    const size_t colon = rest.rfind(':');
    if (colon != std::string::npos) {
        out.port = std::atoi(rest.substr(colon + 1).c_str());
        rest = rest.substr(0, colon);
    }
    out.host = rest;
    return !out.host.empty();
}

// The digits 0 to 9 as a derived table `name` with one column, a.
std::string Digits(const char* name) {
    std::string sql = "(SELECT 0 AS a";
    for (int i = 1; i < 10; ++i) sql += " UNION ALL SELECT " + std::to_string(i);
    return sql + ") " + name;
}

int64_t Count(const std::string& c, const std::string& sql) {
    UltraDbResultSet rows;
    if (!UltraDb_Query(c, sql, {}, rows) || rows.Size() != 1) return -1;
    return rows.Row(0)[size_t(0)].AsInt64();
}

// One engine through the tunnel: refused, trusted, used, refused again with
// another key trusted. Returns the bastion's key.
std::string ThroughTunnel(ConnectionStore& store, const Bastion& bastion, const char* engine, const std::string& driver,
                   int port, const std::string& user, const std::string& password, const std::string& database,
                   const std::string& rowsSql) {
    std::printf("%s through the tunnel\n", engine);
    ConnectionSpec spec;
    spec.name = std::string(engine) + " via bastion";
    spec.driver = driver;
    spec.host = "127.0.0.1";
    spec.port = port;
    spec.user = user;
    spec.database = database;
    spec.tls = driver == "mysql" ? UltraDbTls::Prefer : UltraDbTls::Disable;
    spec.sshHost = bastion.host;
    spec.sshPort = bastion.port;
    spec.sshUser = bastion.user;
    spec.sshAuth = "key";
    ConnectionStore::SecretChanges secrets;
    secrets.password = password;
    secrets.sshSecret = bastion.key;
    std::string error;
    const std::string id = store.Put(spec, secrets, error);
    Check(!id.empty(), "kept, with the key in the vault: " + error);
    if (id.empty()) return std::string();
    spec = *store.Find(id);
    Check(spec.hasSshSecret && spec.hasPassword, "both secrets are in the vault, neither in the list");
    store.Register(spec);
    const std::string c = RegisteredName(spec);

    // An unknown bastion: refused, its key handed back.
    UltraDbResult opened = UltraDb_OpenConnection(c);
    Check(opened.code == UltraDbResultCode::HostKeyUnknown && opened.hostKeyFingerprint.rfind("SHA256:", 0) == 0,
          "the bastion's key not known yet: refused, with its fingerprint (" + opened.message + ")");
    const std::string fingerprint = opened.hostKeyFingerprint;

    // Trusted, as the window does when the user agrees.
    spec.sshHostKey = fingerprint;
    store.Put(spec, ConnectionStore::SecretChanges(), error);
    store.Register(*store.Find(id));
    opened = UltraDb_OpenConnection(c);
    Check(opened.success, std::string("trusted: the connection opens (") + opened.message + ")");
    Check(Count(c, "SELECT 7") == 7, "a query runs through the tunnel");
    // A cursor reads its pages - on MySQL from a session of its own, which
    // goes through the same tunnel.
    UltraDbResult cursorError;
    const UltraDbHandle cursor = UltraDb_OpenCursor(c, rowsSql, {}, &cursorError);
    size_t rows = 0;
    bool exhausted = false;
    while (cursor != UltraDbInvalidHandle && !exhausted) {
        UltraDbResultSet page;
        if (!UltraDb_FetchRows(cursor, 500, page, exhausted)) break;
        rows += page.Size();
    }
    if (cursor != UltraDbInvalidHandle) UltraDb_CloseCursor(cursor);
    Check(rows == 2000, "a cursor's 2000 rows, page by page (" + std::to_string(rows) + ")");
    Check(Count(ConsoleName(spec), "SELECT 8") == 8, "the SQL console's own session goes through a tunnel too");
    UltraDb_CloseConnection(c);
    UltraDb_CloseConnection(ConsoleName(spec));

    // Another key trusted than the bastion's: refused.
    ConnectionSpec wrong = *store.Find(id);
    wrong.sshHostKey = "SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    store.Put(wrong, ConnectionStore::SecretChanges(), error);
    store.Register(wrong);
    opened = UltraDb_OpenConnection(c);
    Check(opened.code == UltraDbResultCode::HostKeyChanged, "a bastion presenting another key is refused");
    UltraDb_CloseConnection(c);
    UltraDb_CloseConnection(ConsoleName(wrong));
    store.Remove(id, error);
    return fingerprint;
}

} // namespace

int main() {
    std::printf("UltraDBAdmin SSH tunnel tests\n\n");
    const bool required = !Env("ULTRADB_TEST_TUNNEL_REQUIRED").empty();
    Bastion bastion;
    const bool haveBastion = ParseBastion(Env("ULTRANET_TEST_SSH_URL"), bastion);
    bastion.key = ReadFile(Env("ULTRANET_TEST_SSH_KEY"));
    const bool havePg = !Env("ULTRADB_TEST_TUNNEL_PG_USER").empty();
    const bool haveMysql = !Env("ULTRADB_TEST_TUNNEL_MYSQL_USER").empty();
    if (!haveBastion || bastion.key.empty() || (!havePg && !haveMysql)) {
        std::printf("  [SKIP] no bastion or no database configured (ULTRANET_TEST_SSH_URL, ULTRANET_TEST_SSH_KEY, "
                    "ULTRADB_TEST_TUNNEL_*)\n");
        Check(!required, "a bastion and a database are configured");
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }
#ifdef ULTRADBADMIN_TEST_SSH_PLUGIN_DIR
    UltraNet_SetPluginDirectory(ULTRADBADMIN_TEST_SSH_PLUGIN_DIR);
#endif
    UltraDb_UseUltraNetSshTunnels();
    ConnectionStore store;
    store.OpenInMemory();

    if (havePg)
        ThroughTunnel(store, bastion, "PostgreSQL", "postgresql", 5432, Env("ULTRADB_TEST_TUNNEL_PG_USER"),
                      Env("ULTRADB_TEST_TUNNEL_PG_PASSWORD"), Env("ULTRADB_TEST_TUNNEL_PG_DB"),
                      "SELECT g FROM generate_series(1, 2000) AS g");
    else
        Check(!required, "a PostgreSQL server is configured for the tunnel");
    if (haveMysql) {
        const std::string fingerprint = ThroughTunnel(store, bastion, "MySQL / MariaDB", "mysql", 3306, Env("ULTRADB_TEST_TUNNEL_MYSQL_USER"),
                      Env("ULTRADB_TEST_TUNNEL_MYSQL_PASSWORD"), Env("ULTRADB_TEST_TUNNEL_MYSQL_DB"),
                      // Digits crossed, as a recursive query stops at MySQL's
                      // cte_max_recursion_depth (1000).
                      "SELECT u.a + 10 * t.a + 100 * h.a + 1000 * k.a AS n FROM " + Digits("u") + ", " +
                          Digits("t") + ", " + Digits("h") + ", " + Digits("k") + " WHERE k.a < 2");

        // Through a tunnel MySQL can check no certificate against the
        // database's name: Verify is refused before anything connects.
        ConnectionSpec verify;
        verify.name = "verify";
        verify.driver = "mysql";
        verify.host = "db.internal";
        verify.database = "x";
        verify.tls = UltraDbTls::VerifyFull;
        verify.sshHost = bastion.host;
        verify.sshPort = bastion.port;
        verify.sshUser = bastion.user;
        verify.sshAuth = "key";
        verify.sshHostKey = fingerprint;   // the tunnel itself opens
        std::string error;
        ConnectionStore::SecretChanges key;
        key.sshSecret = bastion.key;
        const std::string id = store.Put(verify, key, error);
        store.Register(*store.Find(id));
        const UltraDbResult opened = UltraDb_OpenConnection(RegisteredName(*store.Find(id)));
        Check(opened.code == UltraDbResultCode::Unsupported,
              "MySQL with certificate checks through a tunnel is not attempted (" + opened.message + ")");
        store.Remove(id, error);
    } else {
        Check(!required, "a MySQL server is configured for the tunnel");
    }
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
