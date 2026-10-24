// Apps/UltraDBAdmin/engine/DbAdminSearch.cpp
// Version: 0.2.0 - LikeEscaped shared with the Server tab
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminSearch.h"

#include "DbAdminExport.h"       // QuotedName
#include "DbAdminOperations.h"   // QualifiedName

namespace UltraDBAdmin {
namespace {

std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

} // namespace

std::string LikeEscaped(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::vector<std::string> SearchColumns(const UltraDbTableSchema& table) {
    std::vector<std::string> names;
    for (const auto& column : table.columns) {
        const std::string type = Lower(column.declaredType);
        if (type.find("blob") != std::string::npos || type == "bytea" || column.valueType == UltraDbType::Blob) continue;
        names.push_back(column.name);
    }
    return names;
}

bool SearchQuery(SqlEngine engine, const UltraDbTableSchema& table, const std::string& text, SearchMode mode,
                 std::string& sql, UltraDbParams& params) {
    sql.clear();
    params.clear();
    const std::vector<std::string> columns = SearchColumns(table);
    if (columns.empty()) return false;
    const std::string like = engine == SqlEngine::Postgres ? "ILIKE" : "LIKE";
    const std::string pattern = "%" + LikeEscaped(text) + "%";
    sql = "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) {
        const std::string value = "CAST(" + QuotedName(columns[i]) + " AS TEXT)";
        const std::string test = mode == SearchMode::Contains ? value + " " + like + " ? ESCAPE '\\'" : value + " = ?";
        // The columns are named c0, c1, ...: a column's own name could be
        // anything, and the answer is read by position anyway.
        sql += (i == 0 ? "" : ", ") + std::string("COALESCE(SUM(CASE WHEN ") + test + " THEN 1 ELSE 0 END), 0) AS c" +
               std::to_string(i);
        params.push_back(UltraDbValue(mode == SearchMode::Contains ? pattern : text));
    }
    sql += " FROM " + QualifiedName(table.schema, table.name);
    return true;
}

UltraDbResult SearchTable(const std::string& connection, SqlEngine engine, const UltraDbTableSchema& table,
                          const std::string& text, SearchMode mode, std::vector<SearchHit>& out) {
    std::string sql;
    UltraDbParams params;
    if (!SearchQuery(engine, table, text, mode, sql, params)) return UltraDbResult::Ok();
    UltraDbResultSet rows;
    const UltraDbResult result = UltraDb_Query(connection, sql, params, rows);
    if (!result) return result;
    if (rows.Size() != 1) return UltraDbResult::Ok();
    const std::vector<std::string> columns = SearchColumns(table);
    const UltraDbRow& counts = rows.Row(0);
    for (size_t i = 0; i < columns.size() && i < counts.Size(); ++i) {
        const int64_t n = counts[i].AsInt64();
        if (n <= 0) continue;
        SearchHit hit;
        hit.schema = table.schema;
        hit.table = table.name;
        hit.kind = table.kind;
        hit.column = columns[i];
        hit.rows = n;
        out.push_back(hit);
    }
    return UltraDbResult::Ok();
}

UltraDbResult SearchTargets(const std::string& connection, const std::string& schema,
                            std::vector<UltraDbObjectSummary>& out) {
    out.clear();
    std::vector<UltraDbObjectSummary> objects;
    const UltraDbResult result = UltraDb_ListObjects(connection, schema, objects);
    if (!result) return result;
    for (auto& object : objects)
        if (object.kind != UltraDbObjectKind::Sequence) out.push_back(std::move(object));
    return UltraDbResult::Ok();
}

} // namespace UltraDBAdmin
