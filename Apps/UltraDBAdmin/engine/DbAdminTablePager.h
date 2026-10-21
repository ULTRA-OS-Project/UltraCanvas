// Apps/UltraDBAdmin/engine/DbAdminTablePager.h
// The Browse view's rows, a page at a time. A table of ten million rows is
// shown by asking for the page the user scrolled to, never by reading the
// table: each page is one bound query, ORDER BY the primary key (so a page
// holds the same rows when it is asked for again) or by the column the user
// sorted on, then the key.
//
// Every name in the generated SQL goes through UltraDb_QuoteQualifiedName /
// UltraDb_QuoteIdentifier, and the page bounds are bound parameters; the
// statement is kept so the SQL log can show exactly what ran.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace UltraDBAdmin {

class TablePager {
public:
    TablePager(std::string connection, UltraDbTableSchema table, size_t pageSize = 200);

    const UltraDbTableSchema& Table() const { return table_; }
    size_t PageSize() const { return pageSize_; }

    // Sort by a column (empty: the primary key alone). Ascending or not.
    void SetSort(const std::string& column, bool ascending);
    const std::string& SortColumn() const { return sortColumn_; }
    bool SortAscending() const { return ascending_; }

    // True when the rows have a stable order: the table has a primary key.
    // Without one, OFFSET paging can show a row twice or not at all while the
    // table changes, and the view says so.
    bool HasStableOrder() const { return !table_.primaryKey.empty(); }

    // SELECT count(*) - exact, and on a large table not instant: called off
    // the UI thread.
    UltraDbResult CountRows(int64_t& out) const;

    // Rows [index * pageSize, (index + 1) * pageSize).
    UltraDbResult LoadPage(size_t index, UltraDbResultSet& out) const;

    // The page query, with ? for its two bounds - what LoadPage runs.
    UltraDbResult PageSql(std::string& out) const;

private:
    std::string        connection_;
    UltraDbTableSchema table_;
    size_t             pageSize_;
    std::string        sortColumn_;
    bool               ascending_ = true;
};

} // namespace UltraDBAdmin
