// Apps/UltraDBAdmin/engine/DbAdminTablePager.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminTablePager.h"

#include <utility>

namespace UltraDBAdmin {

TablePager::TablePager(std::string connection, UltraDbTableSchema table, size_t pageSize)
    : connection_(std::move(connection)), table_(std::move(table)),
      pageSize_(pageSize == 0 ? 200 : pageSize) {}

void TablePager::SetSort(const std::string& column, bool ascending) {
    sortColumn_ = column;
    ascending_ = ascending;
}

UltraDbResult TablePager::CountRows(int64_t& out) const {
    out = 0;
    std::string name;
    UltraDbResult quoted = UltraDb_QuoteQualifiedName(connection_, table_.schema, table_.name, name);
    if (!quoted) return quoted;
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_, "SELECT count(*) AS n FROM " + name, rs);
    if (r && !rs.Empty()) out = rs.Row(0)[static_cast<size_t>(0)].AsInt64();
    return r;
}

UltraDbResult TablePager::PageSql(std::string& out) const {
    out.clear();
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

    out = "SELECT * FROM " + name;
    if (!order.empty()) out += " ORDER BY " + order;
    out += " LIMIT ? OFFSET ?";
    return UltraDbResult::Ok();
}

UltraDbResult TablePager::LoadPage(size_t index, UltraDbResultSet& out) const {
    out.Clear();
    std::string sql;
    UltraDbResult r = PageSql(sql);
    if (!r) return r;
    return UltraDb_Query(connection_, sql,
                         { static_cast<int64_t>(pageSize_),
                           static_cast<int64_t>(index * pageSize_) }, out);
}

} // namespace UltraDBAdmin
