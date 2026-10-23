// Apps/UltraDBAdmin/engine/DbAdminEditBuffer.h
// Editing rows in Browse, staged: an edit, a new row or a deletion becomes a
// pending change, shown in the grid and as the SQL it will run, and nothing
// reaches the database until Commit runs every change in one transaction
// (Docs/Research/UltraDBAdminDesignProposal.md, 5.3). Revert drops them.
//
// A row is addressed by its primary key, as it was read - never by a WHERE
// over all columns guessing at a unique row - so a table without a key, a
// view and a sequence are not editable here, and say why. Every value is a
// bound parameter and every name goes through UltraDb_QuoteIdentifier. A
// value the user typed is bound as text, and the database converts it to the
// column's type (SQLite by the column's affinity, PostgreSQL by inferring the
// parameter's type), so "12.5" or "2026-10-21" needs no parsing here and a
// value the column cannot take is the database's error, shown at Commit.
//
// A plain value class: the window keeps one for the table it shows, and hands
// a copy to the worker that builds or commits the statements.
//
// A blob is the one value not typed: a file's bytes, loaded into the cell,
// are bound as a blob (SQLite's BLOB, PostgreSQL's bytea). And a value
// function - CURRENT_TIMESTAMP, gen_random_uuid() - is the one value written
// into the statement rather than bound: only one of the functions
// ValueFunctions lists (DbAdminValueTypes.h), checked when it is staged.
// Version: 0.3.0 - a value function (CURRENT_TIMESTAMP, gen_random_uuid()) for a cell
// Version: 0.2.0 - a blob value: a file's bytes for a cell
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraDBAdmin {

// What one column of a change is set to.
struct CellValue {
    enum class Kind { Text, Null, Default, Blob, Function };
    Kind kind = Kind::Text;
    std::string text;             // Kind::Text: as typed; Kind::Function: its SQL
    std::vector<uint8_t> bytes;   // Kind::Blob: the bytes

    static CellValue Of(std::string value) { return CellValue{Kind::Text, std::move(value), {}}; }
    static CellValue NullValue() { return CellValue{Kind::Null, {}, {}}; }
    static CellValue DefaultValue() { return CellValue{Kind::Default, {}, {}}; }
    static CellValue BlobValue(std::vector<uint8_t> value) { return CellValue{Kind::Blob, {}, std::move(value)}; }
    static CellValue FunctionValue(std::string sql) { return CellValue{Kind::Function, std::move(sql), {}}; }
    bool operator==(const CellValue& other) const {
        return kind == other.kind && text == other.text && bytes == other.bytes;
    }
};

struct RowChange {
    enum class Kind { Update, Insert, Delete };
    Kind kind = Kind::Update;
    std::vector<UltraDbValue> key;              // Update / Delete: the primary key as read, in key order
    std::map<std::string, CellValue> values;    // Update: the changed columns; Insert: the given ones
};

// One statement to run, with its parameters, and the same statement with the
// values written in - for the preview only, never executed.
struct GeneratedStatement {
    RowChange::Kind kind = RowChange::Kind::Update;
    std::string sql;
    UltraDbParams params;
    std::string preview;
};

class EditBuffer {
public:
    EditBuffer() = default;
    explicit EditBuffer(UltraDbTableSchema table) : table_(std::move(table)) {}

    const UltraDbTableSchema& Table() const { return table_; }

    // Why rows of this table cannot be edited here, or "" when they can.
    std::string ReadOnlyReason() const;

    // The row's primary key as read, in key order. False when the row lacks
    // a key column (a result that is not the whole row).
    bool KeyOf(const UltraDbRow& row, std::vector<UltraDbValue>& key) const;

    // Stage a change. `values` holds only the columns that change. An update
    // merges into the row's pending update; one that leaves nothing changed
    // drops it. A deletion replaces a pending update of the row. False, with
    // `error`, when refused (the table is read-only, the row has no key, the
    // row is already to be deleted).
    bool UpdateRow(const UltraDbRow& row, const std::map<std::string, CellValue>& values, std::string& error);
    bool InsertRow(const std::map<std::string, CellValue>& values, std::string& error);
    bool DeleteRow(const UltraDbRow& row, std::string& error);
    void Revert() { changes_.clear(); }

    bool Empty() const { return changes_.empty(); }
    size_t Count() const { return changes_.size(); }
    size_t Count(RowChange::Kind kind) const;
    const std::vector<RowChange>& Changes() const { return changes_; }

    // For the grid: the staged value of a cell, or null; whether the row is
    // to be deleted.
    const CellValue* PendingCell(const UltraDbRow& row, const std::string& column) const;
    bool PendingDelete(const UltraDbRow& row) const;

    // The statements the changes become, in the order they were staged.
    // Quoting asks the connection, so call it where a query may run (a
    // worker), not on the UI thread.
    UltraDbResult Statements(const std::string& connection, std::vector<GeneratedStatement>& out) const;

    // Run every statement in one transaction. Each UPDATE and DELETE must
    // touch exactly one row - none means the row was changed or deleted since
    // it was read - or everything is rolled back. On failure nothing has
    // changed in the database; `failed` is the index of the statement that
    // failed. The buffer itself is not changed: the caller empties it after a
    // success.
    UltraDbResult Commit(const std::string& connection, size_t& failed) const;

private:
    RowChange* Find(RowChange::Kind kind, const std::vector<UltraDbValue>& key);
    const RowChange* Find(const std::vector<UltraDbValue>& key) const;

    UltraDbTableSchema table_;
    std::vector<RowChange> changes_;
};

// A value as it reads in the SQL preview: 'text' (quotes doubled), 42, NULL,
// DEFAULT, X'0a1b…' (a blob: its first 32 bytes), CURRENT_TIMESTAMP (a
// function, as it is). For showing only.
std::string PreviewLiteral(const UltraDbValue& value);
std::string PreviewLiteral(const CellValue& value);

} // namespace UltraDBAdmin
