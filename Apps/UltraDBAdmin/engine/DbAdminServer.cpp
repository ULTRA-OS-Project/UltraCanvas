// Apps/UltraDBAdmin/engine/DbAdminServer.cpp
// Version: 0.2.0 - the server engines through UltraDatabase
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminServer.h"

#include "DbAdminExport.h"   // QuotedName
#include "DbAdminFormat.h"   // HumanSize

#include <cstdio>

namespace UltraDBAdmin {
namespace {

// One PRAGMA's single value, as text; "" when it has none.
std::string Pragma(const std::string& connection, const std::string& schema, const char* name) {
    UltraDbResultSet rows;
    const std::string target = schema.empty() ? std::string() : QuotedName(SqlEngine::Sqlite, schema) + ".";
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
        const UltraDbResult result = UltraDb_Query(connection, "PRAGMA " + QuotedName(SqlEngine::Sqlite, s) + ".page_size", {}, probe);
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

const char* SessionIdColumn() { return "id"; }

namespace {

// The module's rows as the grid shows them: a time as h:mm:ss, a size
// readable (the bytes kept for sorting). Other columns as they come.
void Shown(const UltraDbResultSet& rows, ServerView view, UltraDbResultSet& out) {
    out.Clear();
    if (view == ServerView::Settings) { out = rows; return; }
    std::vector<std::string> columns = rows.Columns();
    if (view == ServerView::Sizes)
        columns = {"kind", "name", "total", "data", "indexes", "bytes"};
    out.SetColumns(columns);
    for (const auto& row : rows) {
        std::vector<UltraDbValue> values;
        if (view == ServerView::Sizes) {
            auto size = [&row](const char* column) {
                return row[column].IsNull() ? UltraDbValue() : UltraDbValue(HumanSize(row[column].AsInt64()));
            };
            values = {row["kind"], row["name"], size("bytes"), size("data_bytes"), size("index_bytes"), row["bytes"]};
        } else {
            for (size_t i = 0; i < row.Size(); ++i) {
                if (columns[i] == "running" && !row[i].IsNull()) {
                    const int64_t seconds = row[i].AsInt64();
                    char text[32];
                    std::snprintf(text, sizeof text, "%lld:%02lld:%02lld", static_cast<long long>(seconds / 3600),
                                  static_cast<long long>(seconds / 60 % 60), static_cast<long long>(seconds % 60));
                    values.push_back(UltraDbValue(std::string(text)));
                } else {
                    values.push_back(row[i]);
                }
            }
        }
        out.AddRow(std::move(values));
    }
}

} // namespace

UltraDbResult ReadServerView(const std::string& connection, SqlEngine engine, ServerView view,
                             const std::string& schema, const std::string& filter, UltraDbResultSet& out) {
    out.Clear();
    if (engine == SqlEngine::Sqlite || view == ServerView::FileInfo) return SqliteFileInfo(connection, schema, out);
    UltraDbResultSet rows;
    UltraDbResult result = UltraDbResult::Ok();
    switch (view) {
        case ServerView::Sessions: result = UltraDb_ServerSessions(connection, rows); break;
        case ServerView::Settings: result = UltraDb_ServerSettings(connection, filter, rows); break;
        case ServerView::Sizes:    result = UltraDb_ServerSizes(connection, schema, rows); break;
        case ServerView::FileInfo: break;
    }
    if (!result) return result;
    Shown(rows, view, out);
    return UltraDbResult::Ok();
}

} // namespace UltraDBAdmin
