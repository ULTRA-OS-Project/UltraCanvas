// Apps/UltraDBAdmin/engine/DbAdminValueTypes.h
// What kind of value a column takes, read from its declared type, so the row
// form can offer the right field - a choice of true / false for a boolean, a
// pattern to follow for a date - and catch a typing slip before Commit does:
// "12,5" in a number column, "31.01.2026" in a date column. The database
// still converts and has the last word; this only says early what it would.
//
// And the value functions a cell can be set to instead of a value - the
// database's CURRENT_TIMESTAMP, PostgreSQL's gen_random_uuid(), MySQL's UUID() - per engine
// and kind (Docs/Research/UltraDBAdminDesignProposal.md, 5.2 Insert). A
// function is written into the statement as SQL, so only the ones listed
// here are ever written (IsValueFunction).
// Version: 0.2.0 - MySQL / MariaDB
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEngine.h"

#include <UltraDatabase/UltraDatabase.h>

#include <string>
#include <vector>

namespace UltraDBAdmin {

enum class ValueKind { Boolean, Integer, Decimal, Date, Time, Timestamp, Uuid, Json, Text, Blob, Any };

// From the declared type ("boolean", "INTEGER", "numeric(10,2)",
// "timestamp with time zone", "uuid", "jsonb", "varchar(80)", "BLOB", "")
// and, where that says nothing (SQLite's untyped columns), from how the
// column's values come back.
ValueKind KindOfColumn(const UltraDbColumnSchema& column);

// What to type: "a whole number", "YYYY-MM-DD"; "" for text.
std::string KindHint(ValueKind kind);

// Whether `text` reads as a value of `kind` - empty `error` when it does,
// else what is wrong, naming the column. A Text, Json, Any or Blob column
// takes anything (JSON is the database's to check).
bool CheckValue(ValueKind kind, const std::string& column, const std::string& text, std::string& error);

struct ValueFunction {
    std::string label;      // "Now (CURRENT_TIMESTAMP)"
    std::string sql;        // "CURRENT_TIMESTAMP" - written into the statement as it is
};

// The functions a column of `kind` can be set to on `engine`; none for a
// boolean, a number or a blob.
std::vector<ValueFunction> ValueFunctions(SqlEngine engine, ValueKind kind);

// One of the functions above, for either engine: nothing else is written
// into a statement as SQL.
bool IsValueFunction(const std::string& sql);

} // namespace UltraDBAdmin
