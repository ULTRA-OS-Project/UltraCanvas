// Apps/UltraDBAdmin/engine/DbAdminCompare.h
// Data compare (Docs/Research/UltraDBAdminDesignProposal.md, stage 5): a
// table's rows beside another table's - on another connection, another
// engine even (a staging copy, a replica, last month's dump restored) -
// matched by the first table's primary key, the columns both have compared.
// What comes back is how many rows match, which are only on one side and
// which differ in which columns; and the SQL that makes the second table
// hold what the first holds (an INSERT, UPDATE or DELETE a row), to preview
// and run in one transaction.
//
// Values are compared as text the same way on both sides, so the same value
// from two engines reads alike: a number by its digits (10.50 is 10.5), true
// and false as 1 and 0, NULL as nothing else is; a date or a time as each
// server writes it. Both tables are read whole through cursors; the second's
// rows are held while the first streams past them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEngine.h"
#include "DbAdminOperations.h"

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace UltraDBAdmin {

// One table to compare.
struct CompareSide {
    std::string connection;        // as registered with UltraDatabase
    SqlEngine engine = SqlEngine::Sqlite;
    std::string schema;
    std::string table;
};

struct CompareLimits {
    size_t maxRows = 500000;           // in the second table, which is held
    size_t maxDifferences = 100000;    // kept to show and to write SQL for
    const std::atomic<bool>* stop = nullptr;   // set: the compare ends, "stopped"
};

struct RowDifference {
    enum class Kind { OnlyLeft, OnlyRight, Changed };
    Kind kind = Kind::Changed;
    std::vector<UltraDbValue> left;    // the compared columns' values; empty for OnlyRight
    std::vector<UltraDbValue> right;   // empty for OnlyLeft
    std::vector<size_t> changed;       // Changed: the columns that differ
};

struct CompareResult {
    std::vector<std::string> key;          // the primary key's columns
    std::vector<std::string> columns;      // compared, the key's first
    std::vector<std::string> onlyLeftColumns, onlyRightColumns;   // not compared
    size_t leftRows = 0, rightRows = 0;
    size_t same = 0, onlyLeft = 0, onlyRight = 0, changed = 0;
    std::vector<RowDifference> differences;   // at most CompareLimits::maxDifferences
    bool truncated = false;                    // more differences than kept

    size_t Differences() const { return onlyLeft + onlyRight + changed; }
};

// Compare `left` with `right`. Fails when the left table has no primary key,
// the right one lacks a column of it, the right one holds more rows than
// `limits.maxRows`, or a read fails. Call it on a worker.
UltraDbResult CompareTables(const CompareSide& left, const CompareSide& right, const CompareLimits& limits,
                            CompareResult& out);

// The statements that make `right` hold what `left` held when compared: the
// rows only left has inserted, the changed ones updated (their compared
// columns), those only right has deleted - one transaction. False, with
// `error`, when the differences were not all kept (truncated).
bool SyncPlan(const CompareSide& right, const CompareResult& result, OperationPlan& out, std::string& error);

// The differences as a grid shows them, at most `max`: a first column that
// says which ("changed", "only here", "only there") and then the compared
// columns - a changed one as "here -> there".
UltraDbResultSet DifferenceRows(const CompareResult& result, size_t max = 2000);

// What the compare found, in a sentence or two; `here` and `there` say where
// each table is, as the sentence reads them: "here", "in orders on Shop".
std::string CompareSummary(const CompareResult& result, const std::string& here, const std::string& there);

// How the compare reads a value: the same text for the same value from any
// engine (see above); NULL is "\x01" alone.
std::string CompareText(const UltraDbValue& value);

} // namespace UltraDBAdmin
