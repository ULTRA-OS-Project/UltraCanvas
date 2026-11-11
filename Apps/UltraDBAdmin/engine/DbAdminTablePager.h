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
//
// Filters narrow the rows: one per column, all of them true (AND), each a
// comparison with a bound value - never a value written into the SQL. The
// row count is of the filtered rows. A value is bound as text and the
// database converts it to the column's type, as an edit's value is
// (DbAdminEditBuffer.h): "100" compares as a number with a number column.
// Version: 0.2.0 - filters per column
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminStructureEdit.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraDBAdmin {

// How a filter compares a column with its value.
enum class FilterOp {
    Contains,        // the value anywhere in the column's text, any case
    StartsWith,      // the column's text begins with it, any case
    Equals, NotEquals, Less, LessOrEqual, Greater, GreaterOrEqual,
    IsNull, IsNotNull   // take no value
};

struct ColumnFilter {
    std::string column;
    FilterOp op = FilterOp::Contains;
    std::string value;   // as typed; unused by IsNull / IsNotNull
};

// Every operator, in the order a list offers them, and how each reads
// ("contains", "=", "is NULL").
const std::vector<FilterOp>& FilterOps();
const char* FilterOpText(FilterOp op);
bool FilterTakesValue(FilterOp op);
// One filter as it reads to the user: name contains 'pen', total > 100.
std::string FilterText(const ColumnFilter& filter);

class TablePager {
public:
    TablePager(std::string connection, UltraDbTableSchema table, size_t pageSize = 200,
               SqlEngine engine = SqlEngine::Sqlite);

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

    // The filters, one per column: a second for a column replaces the first.
    // False, with `error`, for a column the table does not have or a
    // comparison without a value.
    bool SetFilter(const ColumnFilter& filter, std::string& error);
    void RemoveFilter(const std::string& column);
    void ClearFilters() { filters_.clear(); }
    const std::vector<ColumnFilter>& Filters() const { return filters_; }

    // " WHERE ..." for the filters with their parameters, or "" when there
    // are none. Quoting asks the connection: call it where a query may run.
    UltraDbResult WhereSql(std::string& sql, UltraDbParams& params) const;

    // SELECT count(*) - exact, and on a large table not instant: called off
    // the UI thread.
    UltraDbResult CountRows(int64_t& out) const;

    // Rows [index * pageSize, (index + 1) * pageSize).
    UltraDbResult LoadPage(size_t index, UltraDbResultSet& out) const;

    // The page query, with ? for the filters' values and its two bounds -
    // what LoadPage runs.
    UltraDbResult PageSql(std::string& out) const;

    // Every row the filters let through, in the view's order, and the
    // filters' values - what an export of the view reads, through a cursor.
    UltraDbResult QuerySql(std::string& sql, UltraDbParams& params) const;

private:
    std::string        connection_;
    UltraDbTableSchema table_;
    size_t             pageSize_;
    SqlEngine          engine_;
    std::vector<ColumnFilter> filters_;
    std::string        sortColumn_;
    bool               ascending_ = true;
};

} // namespace UltraDBAdmin
