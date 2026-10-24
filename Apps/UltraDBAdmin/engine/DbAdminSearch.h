// Apps/UltraDBAdmin/engine/DbAdminSearch.h
// "Find this text in every table" (Docs/Research/UltraDBAdminDesignProposal.md,
// 5.2 Search): for each table and view of a schema, how many rows hold the
// text in each column. One query per table - SUM(CASE WHEN ... THEN 1 ELSE 0
// END) per column, so the table is read once - with the text a bound value
// in every comparison, never written into the SQL.
//
//   Contains  - the column read as text holds it, in any case: LIKE on
//               SQLite (ASCII case), ILIKE on PostgreSQL, LIKE on MySQL (the
//               connection's collation ignores case); %, _ and the escape in
//               the text matched as themselves (ESCAPE '\', MySQL '!').
//   Exactly   - the column read as text is it, case and all.
//
// Blob columns are not searched. The window runs one table per job, so the
// search can be stopped between tables and never holds up a Browse page for
// longer than one table takes. A hit opens the table filtered on that
// column (Browse's filter: contains, or =).
// Version: 0.3.0 - MySQL: LIKE with '!' as the escape; LikeEscaped takes the escape
// Version: 0.2.0 - LikeEscaped, for any LIKE ... ESCAPE '\' with typed text
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminStructureEdit.h"   // SqlEngine

#include <UltraDatabase/UltraDatabase.h>

#include <cstdint>
#include <string>
#include <vector>

namespace UltraDBAdmin {

enum class SearchMode { Contains, Exactly };

// `text` for a LIKE pattern with ESCAPE `escape`: %, _ and the escape itself
// matched as written.
std::string LikeEscaped(const std::string& text, char escape = '\\');

struct SearchHit {
    std::string schema;
    std::string table;
    UltraDbObjectKind kind = UltraDbObjectKind::Table;
    std::string column;
    int64_t rows = 0;
};

// The columns of `table` a search reads: every one but a blob column (by its
// declared type, BLOB or bytea).
std::vector<std::string> SearchColumns(const UltraDbTableSchema& table);

// The one query for `table`, and its values: a count per searched column.
// False when the table has no column to search.
bool SearchQuery(SqlEngine engine, const UltraDbTableSchema& table, const std::string& text, SearchMode mode,
                 std::string& sql, UltraDbParams& params);

// Searches one table: a hit per column that has the text in at least one
// row, appended to `out`. Call it on a worker.
UltraDbResult SearchTable(const std::string& connection, SqlEngine engine, const UltraDbTableSchema& table,
                          const std::string& text, SearchMode mode, std::vector<SearchHit>& out);

// The tables and views of `schema` a search goes through, by name: not its
// sequences.
UltraDbResult SearchTargets(const std::string& connection, const std::string& schema,
                            std::vector<UltraDbObjectSummary>& out);

} // namespace UltraDBAdmin
