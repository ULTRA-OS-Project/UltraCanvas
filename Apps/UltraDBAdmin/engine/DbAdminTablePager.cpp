// Apps/UltraDBAdmin/engine/DbAdminTablePager.cpp
// Version: 0.2.0 - filters per column
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminTablePager.h"

#include <algorithm>
#include <utility>

namespace UltraDBAdmin {
namespace {

// A LIKE pattern matching `text` literally: %, _ and the escape itself
// escaped with a backslash (the statement says ESCAPE '\').
std::string LikeLiteral(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

bool HasColumn(const UltraDbTableSchema& table, const std::string& name) {
    return std::any_of(table.columns.begin(), table.columns.end(),
                       [&](const UltraDbColumnSchema& c) { return c.name == name; });
}

} // namespace

const std::vector<FilterOp>& FilterOps() {
    static const std::vector<FilterOp> ops = {
        FilterOp::Contains, FilterOp::StartsWith, FilterOp::Equals, FilterOp::NotEquals,
        FilterOp::Less, FilterOp::LessOrEqual, FilterOp::Greater, FilterOp::GreaterOrEqual,
        FilterOp::IsNull, FilterOp::IsNotNull};
    return ops;
}

const char* FilterOpText(FilterOp op) {
    switch (op) {
        case FilterOp::Contains:       return "contains";
        case FilterOp::StartsWith:     return "starts with";
        case FilterOp::Equals:         return "=";
        case FilterOp::NotEquals:      return "\u2260";
        case FilterOp::Less:           return "<";
        case FilterOp::LessOrEqual:    return "\u2264";
        case FilterOp::Greater:        return ">";
        case FilterOp::GreaterOrEqual: return "\u2265";
        case FilterOp::IsNull:         return "is NULL";
        case FilterOp::IsNotNull:      return "is not NULL";
    }
    return "=";
}

bool FilterTakesValue(FilterOp op) {
    return op != FilterOp::IsNull && op != FilterOp::IsNotNull;
}

std::string FilterText(const ColumnFilter& filter) {
    std::string text = filter.column + " " + FilterOpText(filter.op);
    if (FilterTakesValue(filter.op)) text += " '" + filter.value + "'";
    return text;
}

TablePager::TablePager(std::string connection, UltraDbTableSchema table, size_t pageSize, SqlEngine engine)
    : connection_(std::move(connection)), table_(std::move(table)),
      pageSize_(pageSize == 0 ? 200 : pageSize), engine_(engine) {}

bool TablePager::SetFilter(const ColumnFilter& filter, std::string& error) {
    error.clear();
    if (!HasColumn(table_, filter.column)) {
        error = "The table has no column named " + filter.column + ".";
        return false;
    }
    // "= nothing" would match the empty text only; "contains nothing" all
    // rows. Neither is what an empty field means: ask for a value.
    if (FilterTakesValue(filter.op) && filter.value.empty()) {
        error = "Type a value to compare " + filter.column + " with, or choose \"is NULL\".";
        return false;
    }
    RemoveFilter(filter.column);
    filters_.push_back(filter);
    return true;
}

void TablePager::RemoveFilter(const std::string& column) {
    filters_.erase(std::remove_if(filters_.begin(), filters_.end(),
                                  [&](const ColumnFilter& f) { return f.column == column; }),
                   filters_.end());
}

UltraDbResult TablePager::WhereSql(std::string& sql, UltraDbParams& params) const {
    sql.clear();
    params.clear();
    for (const auto& filter : filters_) {
        std::string column;
        UltraDbResult q = UltraDb_QuoteIdentifier(connection_, filter.column, column);
        if (!q) return q;
        // A text match reads the column as text, so it finds digits in a
        // number or a date; PostgreSQL's LIKE heeds case, its ILIKE does
        // not, as SQLite's LIKE does not for ASCII.
        const std::string asText = "CAST(" + column + " AS TEXT)";
        const char* like = engine_ == SqlEngine::Postgres ? " ILIKE ? ESCAPE '\\'" : " LIKE ? ESCAPE '\\'";
        std::string term;
        switch (filter.op) {
            case FilterOp::Contains:
                term = asText + like;
                params.push_back(UltraDbValue("%" + LikeLiteral(filter.value) + "%"));
                break;
            case FilterOp::StartsWith:
                term = asText + like;
                params.push_back(UltraDbValue(LikeLiteral(filter.value) + "%"));
                break;
            case FilterOp::Equals:         term = column + " = ?";  break;
            case FilterOp::NotEquals:      term = column + " <> ?"; break;
            case FilterOp::Less:           term = column + " < ?";  break;
            case FilterOp::LessOrEqual:    term = column + " <= ?"; break;
            case FilterOp::Greater:        term = column + " > ?";  break;
            case FilterOp::GreaterOrEqual: term = column + " >= ?"; break;
            case FilterOp::IsNull:         term = column + " IS NULL";     break;
            case FilterOp::IsNotNull:      term = column + " IS NOT NULL"; break;
        }
        const bool compares = filter.op != FilterOp::Contains && filter.op != FilterOp::StartsWith &&
                              FilterTakesValue(filter.op);
        if (compares) params.push_back(UltraDbValue(filter.value));
        sql += (sql.empty() ? " WHERE " : " AND ") + term;
    }
    return UltraDbResult::Ok();
}

void TablePager::SetSort(const std::string& column, bool ascending) {
    sortColumn_ = column;
    ascending_ = ascending;
}

UltraDbResult TablePager::CountRows(int64_t& out) const {
    out = 0;
    std::string name;
    UltraDbResult quoted = UltraDb_QuoteQualifiedName(connection_, table_.schema, table_.name, name);
    if (!quoted) return quoted;
    std::string where;
    UltraDbParams params;
    quoted = WhereSql(where, params);
    if (!quoted) return quoted;
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_, "SELECT count(*) AS n FROM " + name + where, params, rs);
    if (r && !rs.Empty()) out = rs.Row(0)[static_cast<size_t>(0)].AsInt64();
    return r;
}

UltraDbResult TablePager::PageSql(std::string& out) const {
    UltraDbParams params;
    UltraDbResult r = QuerySql(out, params);
    if (r) out += " LIMIT ? OFFSET ?";
    return r;
}

UltraDbResult TablePager::QuerySql(std::string& out, UltraDbParams& params) const {
    out.clear();
    params.clear();
    std::string name;
    UltraDbResult r = UltraDb_QuoteQualifiedName(connection_, table_.schema, table_.name, name);
    if (!r) return r;

    // ORDER BY: the sort column first, then the key, so rows that tie on the
    // sort column still come in one order every time.
    std::string order;
    auto add = [&](const std::string& column, bool asc) -> UltraDbResult {
        std::string q;
        UltraDbResult qr = UltraDb_QuoteIdentifier(connection_, column, q);
        if (!qr) return qr;
        if (!order.empty()) order += ", ";
        order += q + (asc ? " ASC" : " DESC");
        return UltraDbResult::Ok();
    };
    if (!sortColumn_.empty()) {
        r = add(sortColumn_, ascending_);
        if (!r) return r;
    }
    for (const auto& key : table_.primaryKey) {
        if (key == sortColumn_) continue;
        r = add(key, true);
        if (!r) return r;
    }

    std::string where;
    r = WhereSql(where, params);
    if (!r) return r;
    out = "SELECT * FROM " + name + where;
    if (!order.empty()) out += " ORDER BY " + order;
    return UltraDbResult::Ok();
}

UltraDbResult TablePager::LoadPage(size_t index, UltraDbResultSet& out) const {
    out.Clear();
    std::string sql;
    UltraDbParams params;
    UltraDbResult r = QuerySql(sql, params);
    if (!r) return r;
    sql += " LIMIT ? OFFSET ?";
    params.push_back(static_cast<int64_t>(pageSize_));
    params.push_back(static_cast<int64_t>(index * pageSize_));
    return UltraDb_Query(connection_, sql, params, out);
}

} // namespace UltraDBAdmin
