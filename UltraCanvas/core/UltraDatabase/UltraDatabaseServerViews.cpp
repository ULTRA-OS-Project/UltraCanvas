// core/UltraDatabase/UltraDatabaseServerViews.cpp
// Server administration for the server drivers (UltraDatabaseServer.h): the
// sessions, settings and sizes of a PostgreSQL and of a MySQL / MariaDB
// server, and stopping a session's query or the session, each in the
// engine-neutral columns the header fixes.
//
// PostgreSQL from pg_stat_activity, pg_settings and its size functions;
// MySQL and MariaDB from information_schema.PROCESSLIST and TABLES, and the
// system variables where each keeps them - MariaDB's
// information_schema.SYSTEM_VARIABLES with a description for each, MySQL's
// performance_schema.global_variables with where each value came from.
//
// The status a monitor samples: PostgreSQL's pg_stat_activity and
// pg_stat_database summed over the databases; MySQL's and MariaDB's SHOW
// GLOBAL STATUS (performance_schema may be off), its counters added up.
//
// Plain SQL over the connection interface, every value bound, so this file
// compiles - and stays compiled - in a build with neither client library.
// Version: 0.2.0 - the server's status (UltraDb_ServerStatus)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace ultradb_internal {

namespace {

// `text` for LIKE ... ESCAPE '!', contained anywhere: %, _ and ! matched as
// written. '!' rather than a backslash, which a MySQL server running with
// NO_BACKSLASH_ESCAPES would not take as the escape in the statement's text.
std::string ContainsPattern(const std::string& text) {
    std::string out = "%";
    for (char c : text) {
        if (c == '%' || c == '_' || c == '!') out.push_back('!');
        out.push_back(c);
    }
    out.push_back('%');
    return out;
}

bool IsMariaDb(IUltraDbConnection& conn) {
    UltraDbResultSet rs;
    if (!conn.ExecuteDirect("SELECT VERSION() AS v", {}, rs) || rs.Empty()) return false;
    std::string v = rs.Row(0)["v"].AsString();
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v.find("mariadb") != std::string::npos;
}

UltraDbResult IsThisSession(int64_t id, int64_t own) {
    if (id == own)
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "session " + std::to_string(id) +
                                        " is the one asking: it is not stopped from itself");
    return UltraDbResult::Ok();
}

} // namespace

// ===== POSTGRESQL =====

UltraDbResult PostgresServerSessions(IUltraDbConnection& conn, UltraDbResultSet& out) {
    return conn.ExecuteDirect(
        "SELECT pid AS id, COALESCE(usename, '') AS \"user\", COALESCE(datname, '') AS database,"
        "       COALESCE(application_name, '') AS application, COALESCE(client_addr::text, 'local') AS client,"
        "       COALESCE(state, '') AS state,"
        "       EXTRACT(EPOCH FROM now() - COALESCE(query_start, backend_start))::bigint AS running,"
        "       COALESCE(wait_event_type, '') AS waiting, COALESCE(query, '') AS query,"
        "       pid = pg_backend_pid() AS this_session"
        "  FROM pg_catalog.pg_stat_activity"
        " WHERE backend_type = 'client backend'"
        " ORDER BY state = 'active' DESC, query_start",
        {}, out);
}

UltraDbResult PostgresServerSettings(IUltraDbConnection& conn, const std::string& filter, UltraDbResultSet& out) {
    const std::string pattern = ContainsPattern(filter);
    return conn.ExecuteDirect(
        "SELECT name, setting AS value, COALESCE(unit, '') AS unit, category,"
        "       COALESCE(short_desc, '') AS description, source"
        "  FROM pg_catalog.pg_settings"
        " WHERE name ILIKE ? ESCAPE '!' OR short_desc ILIKE ? ESCAPE '!'"
        " ORDER BY category, name",
        { pattern, pattern }, out);
}

UltraDbResult PostgresServerSizes(IUltraDbConnection& conn, const std::string& schema, UltraDbResultSet& out) {
    return conn.ExecuteDirect(
        "SELECT 'database' AS kind, datname AS name, pg_database_size(oid) AS bytes,"
        "       NULL::bigint AS data_bytes, NULL::bigint AS index_bytes"
        "  FROM pg_catalog.pg_database WHERE datallowconn"
        " UNION ALL "
        "SELECT 'table', n.nspname || '.' || c.relname, pg_total_relation_size(c.oid),"
        "       pg_relation_size(c.oid), pg_indexes_size(c.oid)"
        "  FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = ? AND c.relkind IN ('r', 'm', 'p')"
        " ORDER BY kind, bytes DESC",
        { schema }, out);
}

UltraDbResult PostgresServerStatus(IUltraDbConnection& conn, UltraDbResultSet& out) {
    return conn.ExecuteDirect(
        "WITH a AS (SELECT COUNT(*) AS n,"
        "                  COUNT(*) FILTER (WHERE state = 'active' AND pid <> pg_backend_pid()) AS running"
        "             FROM pg_catalog.pg_stat_activity WHERE backend_type = 'client backend'),"
        "     d AS (SELECT COALESCE(SUM(xact_commit + xact_rollback), 0) AS xacts,"
        "                  COALESCE(SUM(tup_returned), 0) AS rows_read,"
        "                  COALESCE(SUM(tup_inserted + tup_updated + tup_deleted), 0) AS rows_written,"
        "                  COALESCE(SUM(blks_hit + blks_read), 0) AS cache_reads,"
        "                  COALESCE(SUM(blks_read), 0) AS disk_reads"
        "             FROM pg_catalog.pg_stat_database)"
        "SELECT v.name, v.value::bigint AS value, v.counter"
        "  FROM a, d, LATERAL (VALUES ('connections', a.n::numeric, false), ('active', a.running::numeric, false),"
        "                             ('transactions', d.xacts, true), ('rows_read', d.rows_read, true),"
        "                             ('rows_written', d.rows_written, true), ('cache_reads', d.cache_reads, true),"
        "                             ('disk_reads', d.disk_reads, true)) AS v(name, value, counter)",
        {}, out);
}

std::string PostgresStopSessionSql(int64_t id, bool endSession) {
    return std::string("SELECT ") + (endSession ? "pg_terminate_backend(" : "pg_cancel_backend(") +
           std::to_string(id) + ")";
}

UltraDbResult PostgresStopSession(IUltraDbConnection& conn, int64_t id, bool endSession, bool& stopped) {
    stopped = false;
    UltraDbResultSet rows;
    UltraDbResult r = conn.ExecuteDirect("SELECT pg_backend_pid() AS pid", {}, rows);
    if (!r) return r;
    r = IsThisSession(id, rows.Empty() ? 0 : rows.Row(0)["pid"].AsInt64());
    if (!r) return r;
    r = conn.ExecuteDirect(endSession ? "SELECT pg_terminate_backend(?) AS done" : "SELECT pg_cancel_backend(?) AS done",
                           { id }, rows);
    // Another role's session, without the right to signal it: not stopped.
    if (!r && r.sqlState == "42501") return UltraDbResult::Ok();
    if (!r) return r;
    stopped = rows.Size() == 1 && rows.Row(0)["done"].AsBool();
    return UltraDbResult::Ok();
}

// ===== MYSQL / MARIADB =====

UltraDbResult MysqlServerSessions(IUltraDbConnection& conn, UltraDbResultSet& out) {
    // Without the PROCESS privilege the list holds the user's own sessions.
    return conn.ExecuteDirect(
        "SELECT ID AS `id`, USER AS `user`, IFNULL(DB, '') AS `database`, '' AS `application`,"
        "       HOST AS `client`,"
        "       CASE COMMAND WHEN 'Sleep' THEN 'idle' WHEN 'Query' THEN 'active' ELSE LOWER(COMMAND) END AS `state`,"
        "       TIME AS `running`, IFNULL(STATE, '') AS `waiting`, IFNULL(INFO, '') AS `query`,"
        "       ID = CONNECTION_ID() AS `this_session`"
        "  FROM information_schema.PROCESSLIST"
        " WHERE COMMAND NOT IN ('Daemon', 'Binlog Dump', 'Binlog Dump GTID')"
        "   AND USER NOT IN ('system user', 'event_scheduler')"
        " ORDER BY COMMAND = 'Query' DESC, TIME DESC",
        {}, out);
}

UltraDbResult MysqlServerSettings(IUltraDbConnection& conn, const std::string& filter, UltraDbResultSet& out) {
    const std::string pattern = ContainsPattern(filter);
    if (IsMariaDb(conn))
        return conn.ExecuteDirect(
            "SELECT LOWER(VARIABLE_NAME) AS `name`, IFNULL(GLOBAL_VALUE, IFNULL(SESSION_VALUE, '')) AS `value`,"
            "       '' AS `unit`, VARIABLE_SCOPE AS `category`, IFNULL(VARIABLE_COMMENT, '') AS `description`,"
            "       IFNULL(GLOBAL_VALUE_ORIGIN, '') AS `source`"
            "  FROM information_schema.SYSTEM_VARIABLES"
            " WHERE VARIABLE_NAME LIKE ? ESCAPE '!' OR VARIABLE_COMMENT LIKE ? ESCAPE '!'"
            " ORDER BY VARIABLE_NAME",
            { pattern, pattern }, out);
    UltraDbResult r = conn.ExecuteDirect(
        "SELECT v.VARIABLE_NAME AS `name`, IFNULL(v.VARIABLE_VALUE, '') AS `value`, '' AS `unit`,"
        "       '' AS `category`, '' AS `description`, IFNULL(i.VARIABLE_SOURCE, '') AS `source`"
        "  FROM performance_schema.global_variables v"
        "  LEFT JOIN performance_schema.variables_info i ON i.VARIABLE_NAME = v.VARIABLE_NAME"
        " WHERE v.VARIABLE_NAME LIKE ? ESCAPE '!'"
        " ORDER BY v.VARIABLE_NAME",
        { pattern }, out);
    if (r) return r;
    // performance_schema switched off: the names and values are all there is.
    UltraDbResultSet shown;
    r = conn.ExecuteDirect("SHOW GLOBAL VARIABLES WHERE Variable_name LIKE ? ESCAPE '!'", { pattern }, shown);
    if (!r) return r;
    out.Clear();
    out.SetColumns({ "name", "value", "unit", "category", "description", "source" });
    for (const auto& row : shown)
        out.AddRow({ row[size_t(0)], row[size_t(1)], UltraDbValue(""), UltraDbValue(""), UltraDbValue(""), UltraDbValue("") });
    return UltraDbResult::Ok();
}

UltraDbResult MysqlServerSizes(IUltraDbConnection& conn, const std::string& schema, UltraDbResultSet& out) {
    return conn.ExecuteDirect(
        "SELECT 'database' AS `kind`, TABLE_SCHEMA AS `name`,"
        "       CAST(SUM(DATA_LENGTH + INDEX_LENGTH) AS SIGNED) AS `bytes`,"
        "       CAST(SUM(DATA_LENGTH) AS SIGNED) AS `data_bytes`, CAST(SUM(INDEX_LENGTH) AS SIGNED) AS `index_bytes`"
        "  FROM information_schema.TABLES GROUP BY TABLE_SCHEMA"
        " UNION ALL "
        "SELECT 'table', CONCAT(TABLE_SCHEMA, '.', TABLE_NAME), CAST(DATA_LENGTH + INDEX_LENGTH AS SIGNED),"
        "       CAST(DATA_LENGTH AS SIGNED), CAST(INDEX_LENGTH AS SIGNED)"
        "  FROM information_schema.TABLES WHERE TABLE_SCHEMA = ? AND TABLE_TYPE = 'BASE TABLE'"
        " ORDER BY `kind`, `bytes` DESC",
        { schema }, out);
}

UltraDbResult MysqlServerStatus(IUltraDbConnection& conn, UltraDbResultSet& out) {
    UltraDbResultSet shown;
    UltraDbResult r = conn.ExecuteDirect(
        "SHOW GLOBAL STATUS WHERE Variable_name IN ('Threads_connected', 'Threads_running', 'Questions',"
        " 'Handler_commit', 'Handler_rollback', 'Innodb_rows_read', 'Innodb_rows_inserted', 'Innodb_rows_updated',"
        " 'Innodb_rows_deleted', 'Rows_read', 'Handler_write', 'Handler_update', 'Handler_delete',"
        " 'Innodb_buffer_pool_read_requests', 'Innodb_buffer_pool_reads', 'Bytes_received', 'Bytes_sent')",
        {}, shown);
    if (!r) return r;
    // The values come as text; a name the server does not keep is left out.
    std::vector<std::pair<std::string, int64_t>> status;
    for (const auto& row : shown) {
        const std::string text = row[size_t(1)].AsString();
        // Within int64_t; a counter past that would have taken centuries.
        if (text.empty() || text.size() > 18 || text.find_first_not_of("0123456789") != std::string::npos) continue;
        status.emplace_back(row[size_t(0)].AsString(), std::stoll(text));
    }
    // The sum of `names`, when the server keeps the first of them.
    auto sum = [&status](std::initializer_list<const char*> names, int64_t& value) {
        value = 0;
        bool first = false;
        for (const char* name : names)
            for (const auto& [key, v] : status)
                if (key == name) {
                    value += v;
                    first = first || name == *names.begin();
                }
        return first;
    };
    out.Clear();
    out.SetColumns({ "name", "value", "counter" });
    // `name` from the first of `sources` the server keeps.
    auto add = [&out, &sum](const char* name, std::initializer_list<std::initializer_list<const char*>> sources,
                            bool counter, int64_t less = 0) {
        for (const auto& from : sources) {
            int64_t value = 0;
            if (!sum(from, value)) continue;
            out.AddRow({ UltraDbValue(name), UltraDbValue(std::max<int64_t>(0, value - less)), UltraDbValue(counter) });
            return;
        }
    };
    add("connections", { { "Threads_connected" } }, false);
    add("active", { { "Threads_running" } }, false, 1);        // the thread asking runs too
    add("queries", { { "Questions" } }, true);
    add("transactions", { { "Handler_commit", "Handler_rollback" } }, true);
    // InnoDB's own counts; MariaDB 10.6 and later keep only the handler's.
    add("rows_read", { { "Innodb_rows_read" }, { "Rows_read" } }, true);
    add("rows_written", { { "Innodb_rows_inserted", "Innodb_rows_updated", "Innodb_rows_deleted" },
                          { "Handler_write", "Handler_update", "Handler_delete" } }, true);
    add("cache_reads", { { "Innodb_buffer_pool_read_requests" } }, true);
    add("disk_reads", { { "Innodb_buffer_pool_reads" } }, true);
    add("bytes_received", { { "Bytes_received" } }, true);
    add("bytes_sent", { { "Bytes_sent" } }, true);
    return UltraDbResult::Ok();
}

std::string MysqlStopSessionSql(int64_t id, bool endSession) {
    return std::string(endSession ? "KILL " : "KILL QUERY ") + std::to_string(id);
}

UltraDbResult MysqlStopSession(IUltraDbConnection& conn, int64_t id, bool endSession, int64_t ownSession,
                               bool& stopped) {
    stopped = false;
    UltraDbResult r = IsThisSession(id, ownSession);
    if (!r) return r;
    // A session gone, or out of this user's sight, is not stopped - an
    // answer, not an error.
    UltraDbResultSet rows;
    r = conn.ExecuteDirect("SELECT COUNT(*) AS n FROM information_schema.PROCESSLIST WHERE ID = ?", { id }, rows);
    if (!r) return r;
    if (rows.Empty() || rows.Row(0)["n"].AsInt64() == 0) return UltraDbResult::Ok();
    r = conn.ExecuteDirect(MysqlStopSessionSql(id, endSession), {}, rows);
    if (!r) return r;
    stopped = true;
    return UltraDbResult::Ok();
}

} // namespace ultradb_internal
