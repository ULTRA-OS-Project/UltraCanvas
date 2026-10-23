// Apps/UltraDBAdmin/engine/DbAdminServer.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminServer.h"

#include "DbAdminExport.h"   // QuotedName
#include "DbAdminFormat.h"   // HumanSize
#include "DbAdminSearch.h"   // LikeEscaped

namespace UltraDBAdmin {
namespace {

// One PRAGMA's single value, as text; "" when it has none.
std::string Pragma(const std::string& connection, const std::string& schema, const char* name) {
    UltraDbResultSet rows;
    const std::string target = schema.empty() ? std::string() : QuotedName(schema) + ".";
    if (!UltraDb_Query(connection, "PRAGMA " + target + name, {}, rows) || rows.Size() == 0 || rows.Row(0).Size() == 0)
        return std::string();
    return rows.Row(0)[0].IsNull() ? std::string() : rows.Row(0)[0].AsString();
}

UltraDbResult SqliteFileInfo(const std::string& connection, const std::string& schema, UltraDbResultSet& out) {
    out.Clear();
    out.SetColumns({"Property", "Value", "What it is"});
    const std::string s = schema.empty() ? std::string("main") : schema;
    auto add = [&](const std::string& item, const std::string& value, const std::string& about) {
        out.AddRow({UltraDbValue(item), UltraDbValue(value), UltraDbValue(about)});
    };
    const std::string pageSize = Pragma(connection, s, "page_size");
    const std::string pageCount = Pragma(connection, s, "page_count");
    const std::string freePages = Pragma(connection, s, "freelist_count");
    if (pageSize.empty()) {
        // A file that cannot be read answers nothing: say why, not a blank table.
        UltraDbResultSet probe;
        const UltraDbResult result = UltraDb_Query(connection, "PRAGMA " + QuotedName(s) + ".page_size", {}, probe);
        if (!result) return result;
    }
    add("Page size", pageSize + " bytes", "The unit SQLite reads and writes");
    add("Pages", pageCount, "Pages in the file");
    add("Free pages", freePages, "Pages deleted rows left empty; VACUUM gives them back");
    if (!pageSize.empty() && !pageCount.empty()) {
        const int64_t bytes = std::stoll(pageSize) * std::stoll(pageCount);
        add("Size", HumanSize(bytes), "Page size times pages");
    }
    UltraDbResultSet files;
    if (UltraDb_Query(connection, "PRAGMA database_list", {}, files)) {
        for (size_t i = 0; i < files.Size(); ++i)
            if (files.Row(i)["name"].AsString() == s) add("File", files.Row(i)["file"].AsString(), "Where it is");
    }
    add("Journal mode", Pragma(connection, s, "journal_mode"), "How a write is made safe: delete, wal ...");
    add("Encoding", Pragma(connection, s, "encoding"), "The text encoding, chosen when the file was made");
    const std::string autoVacuum = Pragma(connection, s, "auto_vacuum");
    add("Auto-vacuum", autoVacuum == "1" ? "full" : autoVacuum == "2" ? "incremental" : "none",
        "Whether free pages are given back as they appear");
    add("User version", Pragma(connection, s, "user_version"), "A number the application keeps for its schema");
    add("Application id", Pragma(connection, s, "application_id"), "Which program the file belongs to, if it says");
    add("Foreign keys", Pragma(connection, "", "foreign_keys") == "1" ? "on" : "off",
        "Whether this session checks foreign keys");
    UltraDbResultSet version;
    if (UltraDb_Query(connection, "SELECT sqlite_version()", {}, version) && version.Size() == 1)
        add("SQLite", version.Row(0)[0].AsString(), "The library's version");
    return UltraDbResult::Ok();
}

} // namespace

std::vector<ServerView> ServerViews(SqlEngine engine) {
    if (engine == SqlEngine::Sqlite) return {ServerView::FileInfo};
    return {ServerView::Sessions, ServerView::Settings, ServerView::Sizes};
}

const char* ServerViewText(ServerView view) {
    switch (view) {
        case ServerView::Sessions: return "Sessions";
        case ServerView::Settings: return "Settings";
        case ServerView::Sizes:    return "Sizes";
        case ServerView::FileInfo: return "The file";
    }
    return "";
}

const char* SessionPidColumn() { return "pid"; }

UltraDbResult ReadServerView(const std::string& connection, SqlEngine engine, ServerView view,
                             const std::string& schema, const std::string& filter, UltraDbResultSet& out) {
    out.Clear();
    if (engine == SqlEngine::Sqlite || view == ServerView::FileInfo) return SqliteFileInfo(connection, schema, out);
    const std::string pattern = "%" + LikeEscaped(filter) + "%";
    switch (view) {
        case ServerView::Sessions:
            return UltraDb_Query(
                connection,
                "SELECT pid, usename AS \"user\", datname AS database, application_name AS application,"
                "       COALESCE(client_addr::text, 'local') AS client, state,"
                "       to_char(now() - COALESCE(query_start, backend_start), 'HH24:MI:SS') AS running,"
                "       wait_event_type AS waiting, query,"
                "       pid = pg_backend_pid() AS this_session"
                "  FROM pg_catalog.pg_stat_activity"
                " WHERE backend_type = 'client backend'"
                " ORDER BY state = 'active' DESC, query_start",
                {}, out);
        case ServerView::Settings:
            return UltraDb_Query(
                connection,
                "SELECT name, setting, unit, category, short_desc AS description, source,"
                "       pending_restart AS restart_pending"
                "  FROM pg_catalog.pg_settings"
                " WHERE name ILIKE ? ESCAPE '\\' OR short_desc ILIKE ? ESCAPE '\\'"
                " ORDER BY category, name",
                {UltraDbValue(pattern), UltraDbValue(pattern)}, out);
        case ServerView::Sizes:
            return UltraDb_Query(
                connection,
                "SELECT 'database' AS kind, datname AS name, pg_size_pretty(pg_database_size(oid)) AS total,"
                "       '' AS data, '' AS indexes, pg_database_size(oid) AS bytes"
                "  FROM pg_catalog.pg_database WHERE datallowconn"
                " UNION ALL "
                "SELECT 'table', n.nspname || '.' || c.relname, pg_size_pretty(pg_total_relation_size(c.oid)),"
                "       pg_size_pretty(pg_relation_size(c.oid)), pg_size_pretty(pg_indexes_size(c.oid)),"
                "       pg_total_relation_size(c.oid)"
                "  FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace"
                " WHERE n.nspname = ? AND c.relkind IN ('r', 'm', 'p')"
                " ORDER BY kind, bytes DESC",
                {UltraDbValue(schema)}, out);
        case ServerView::FileInfo:
            break;
    }
    return UltraDbResult::Ok();
}

OperationPlan StopSessionPlan(int64_t pid, bool terminate) {
    OperationPlan plan;
    plan.transaction = false;
    plan.statements.push_back(std::string("SELECT ") + (terminate ? "pg_terminate_backend(" : "pg_cancel_backend(") +
                              std::to_string(pid) + ")");
    plan.notes.push_back(terminate ? "The session ends: what it had not committed is rolled back."
                                   : "The query stops with an error; the session stays.");
    return plan;
}

} // namespace UltraDBAdmin
