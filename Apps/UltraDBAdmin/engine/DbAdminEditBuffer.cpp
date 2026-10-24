// Apps/UltraDBAdmin/engine/DbAdminEditBuffer.cpp
// Version: 0.4.0 - InsertAt, UpdateInsert, RemoveInsert: staged new rows, shown and changed in the grid
// Version: 0.3.0 - CellValue::Kind::Function: a listed value function, written in
// Version: 0.2.0 - CellValue::Kind::Blob
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminEditBuffer.h"

#include "DbAdminValueTypes.h"

#include <algorithm>
#include <cstdio>

namespace UltraDBAdmin {
namespace {

bool SameValue(const UltraDbValue& a, const UltraDbValue& b) {
    if (a.Type() != b.Type()) return false;
    switch (a.Type()) {
        case UltraDbType::Null: return true;
        case UltraDbType::Blob: return a.AsBlob() == b.AsBlob();
        default:                return a.AsString() == b.AsString();
    }
}

bool SameKey(const std::vector<UltraDbValue>& a, const std::vector<UltraDbValue>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!SameValue(a[i], b[i])) return false;
    return true;
}

// X'0a1b...', the first 32 bytes and "…" for the rest.
std::string HexLiteral(const std::vector<uint8_t>& bytes) {
    std::string hex = "X'";
    char pair[3];
    for (size_t i = 0; i < bytes.size() && i < 32; ++i) {
        std::snprintf(pair, sizeof pair, "%02x", bytes[i]);
        hex += pair;
    }
    if (bytes.size() > 32) hex += "…";
    return hex + "'";
}

std::string QuotedText(const std::string& text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}

const UltraDbColumnSchema* ColumnNamed(const UltraDbTableSchema& table, const std::string& name) {
    for (const auto& column : table.columns)
        if (column.name == name) return &column;
    return nullptr;
}

// What a staged value binds as, or false for DEFAULT and a function, which
// are written into the statement and bind nothing.
bool Bound(const CellValue& value, UltraDbValue& out) {
    switch (value.kind) {
        case CellValue::Kind::Text:     out = UltraDbValue(value.text); return true;
        case CellValue::Kind::Null:     out = UltraDbValue::Null();     return true;
        case CellValue::Kind::Blob:     out = UltraDbValue::Blob(value.bytes); return true;
        case CellValue::Kind::Default:
        case CellValue::Kind::Function: return false;
    }
    return false;
}

// Where the value goes in the statement: "?" when it binds, else the SQL it is.
std::string Placeholder(const CellValue& value) {
    if (value.kind == CellValue::Kind::Function) return value.text;
    if (value.kind == CellValue::Kind::Default) return "DEFAULT";
    return "?";
}

} // namespace

std::string PreviewLiteral(const UltraDbValue& value) {
    switch (value.Type()) {
        case UltraDbType::Null:   return "NULL";
        case UltraDbType::Boolean:   return value.AsBool() ? "TRUE" : "FALSE";
        case UltraDbType::Int:
        case UltraDbType::Double: return value.AsString();
        case UltraDbType::Text:   return QuotedText(value.AsString());
        case UltraDbType::Blob:   return HexLiteral(value.AsBlob());
    }
    return "NULL";
}

std::string PreviewLiteral(const CellValue& value) {
    switch (value.kind) {
        case CellValue::Kind::Text:    return QuotedText(value.text);
        case CellValue::Kind::Null:    return "NULL";
        case CellValue::Kind::Default: return "DEFAULT";
        case CellValue::Kind::Blob:    return HexLiteral(value.bytes);
        case CellValue::Kind::Function: return value.text;
    }
    return "NULL";
}

std::string EditBuffer::ReadOnlyReason() const {
    if (table_.name.empty()) return "Pick a table.";
    switch (table_.kind) {
        case UltraDbObjectKind::Table:
        case UltraDbObjectKind::ForeignTable:
            break;
        case UltraDbObjectKind::View:
        case UltraDbObjectKind::MaterializedView:
            return "A view shows rows of other tables: edit them there.";
        case UltraDbObjectKind::Sequence:
            return "A sequence is changed with its own functions, in the SQL tab.";
    }
    if (table_.primaryKey.empty())
        return "This table has no primary key, so a row cannot be told apart from an identical one; "
               "edit it with SQL in the SQL tab.";
    return std::string();
}

bool EditBuffer::KeyOf(const UltraDbRow& row, std::vector<UltraDbValue>& key) const {
    key.clear();
    for (const auto& column : table_.primaryKey) {
        if (!row.Has(column)) return false;
        key.push_back(row[column]);
    }
    return !key.empty();
}

size_t EditBuffer::Count(RowChange::Kind kind) const {
    return static_cast<size_t>(std::count_if(changes_.begin(), changes_.end(),
                                             [kind](const RowChange& c) { return c.kind == kind; }));
}

RowChange* EditBuffer::Find(RowChange::Kind kind, const std::vector<UltraDbValue>& key) {
    for (auto& change : changes_)
        if (change.kind == kind && SameKey(change.key, key)) return &change;
    return nullptr;
}

const RowChange* EditBuffer::Find(const std::vector<UltraDbValue>& key) const {
    for (const auto& change : changes_)
        if (change.kind != RowChange::Kind::Insert && SameKey(change.key, key)) return &change;
    return nullptr;
}

bool EditBuffer::UpdateRow(const UltraDbRow& row, const std::map<std::string, CellValue>& values,
                           std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    std::vector<UltraDbValue> key;
    if (!KeyOf(row, key)) { error = "The row has no primary-key value to address it by."; return false; }
    if (Find(RowChange::Kind::Delete, key)) { error = "This row is already to be deleted."; return false; }
    for (const auto& [column, value] : values) {
        const UltraDbColumnSchema* schema = ColumnNamed(table_, column);
        if (!schema) { error = "No column named " + column + "."; return false; }
        if (schema->generated) { error = column + " is computed by the database and cannot be set."; return false; }
        if (value.kind == CellValue::Kind::Default) { error = "DEFAULT applies to new rows only."; return false; }
        if (value.kind == CellValue::Kind::Function && !IsValueFunction(value.text)) {
            error = value.text + " is not one of the value functions offered.";
            return false;
        }
    }
    RowChange* change = Find(RowChange::Kind::Update, key);
    if (!change) {
        if (values.empty()) return true;
        changes_.push_back(RowChange{RowChange::Kind::Update, key, {}});
        change = &changes_.back();
    }
    for (const auto& [column, value] : values) {
        // Back to what was read: no longer a change.
        const UltraDbValue& original = row[column];
        bool unchanged = false;
        switch (value.kind) {
            case CellValue::Kind::Null: unchanged = original.IsNull(); break;
            case CellValue::Kind::Function: unchanged = false; break;   // its value is the database's, when it runs
            case CellValue::Kind::Blob:
                unchanged = original.Type() == UltraDbType::Blob && original.AsBlob() == value.bytes;
                break;
            default:
                unchanged = !original.IsNull() && original.Type() != UltraDbType::Blob &&
                            original.AsString() == value.text;
                break;
        }
        if (unchanged) change->values.erase(column);
        else change->values[column] = value;
    }
    if (change->values.empty()) {
        changes_.erase(changes_.begin() + (change - changes_.data()));
    }
    return true;
}

bool EditBuffer::InsertValues(const std::map<std::string, CellValue>& values, std::map<std::string, CellValue>& given,
                              std::string& error) const {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    given.clear();
    for (const auto& [column, value] : values) {
        const UltraDbColumnSchema* schema = ColumnNamed(table_, column);
        if (!schema) { error = "No column named " + column + "."; return false; }
        if (schema->generated) continue;                       // the database computes it
        if (value.kind == CellValue::Kind::Default) continue;  // left out: the column's default
        if (value.kind == CellValue::Kind::Function && !IsValueFunction(value.text)) {
            error = value.text + " is not one of the value functions offered.";
            return false;
        }
        given[column] = value;
    }
    return true;
}

bool EditBuffer::InsertRow(const std::map<std::string, CellValue>& values, std::string& error) {
    std::map<std::string, CellValue> given;
    if (!InsertValues(values, given, error)) return false;
    changes_.push_back(RowChange{RowChange::Kind::Insert, {}, std::move(given)});
    return true;
}

const RowChange* EditBuffer::InsertAt(size_t index) const {
    for (const auto& change : changes_) {
        if (change.kind != RowChange::Kind::Insert) continue;
        if (index-- == 0) return &change;
    }
    return nullptr;
}

bool EditBuffer::UpdateInsert(size_t index, const std::map<std::string, CellValue>& values, std::string& error) {
    std::map<std::string, CellValue> given;
    if (!InsertValues(values, given, error)) return false;
    for (auto& change : changes_) {
        if (change.kind != RowChange::Kind::Insert) continue;
        if (index-- == 0) {
            change.values = std::move(given);
            return true;
        }
    }
    error = "That new row is no longer staged.";
    return false;
}

void EditBuffer::RemoveInsert(size_t index) {
    for (auto it = changes_.begin(); it != changes_.end(); ++it) {
        if (it->kind != RowChange::Kind::Insert) continue;
        if (index-- == 0) {
            changes_.erase(it);
            return;
        }
    }
}

bool EditBuffer::DeleteRow(const UltraDbRow& row, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    std::vector<UltraDbValue> key;
    if (!KeyOf(row, key)) { error = "The row has no primary-key value to address it by."; return false; }
    if (Find(RowChange::Kind::Delete, key)) return true;
    if (RowChange* update = Find(RowChange::Kind::Update, key)) {
        update->kind = RowChange::Kind::Delete;     // its edits no longer matter
        update->values.clear();
        return true;
    }
    changes_.push_back(RowChange{RowChange::Kind::Delete, key, {}});
    return true;
}

const CellValue* EditBuffer::PendingCell(const UltraDbRow& row, const std::string& column) const {
    if (changes_.empty()) return nullptr;
    std::vector<UltraDbValue> key;
    if (!KeyOf(row, key)) return nullptr;
    const RowChange* change = Find(key);
    if (!change || change->kind != RowChange::Kind::Update) return nullptr;
    auto it = change->values.find(column);
    return it == change->values.end() ? nullptr : &it->second;
}

bool EditBuffer::PendingDelete(const UltraDbRow& row) const {
    if (changes_.empty()) return false;
    std::vector<UltraDbValue> key;
    if (!KeyOf(row, key)) return false;
    const RowChange* change = Find(key);
    return change && change->kind == RowChange::Kind::Delete;
}

UltraDbResult EditBuffer::Statements(const std::string& connection, std::vector<GeneratedStatement>& out) const {
    out.clear();
    std::string target;
    UltraDbResult result = UltraDb_QuoteQualifiedName(connection, table_.schema, table_.name, target);
    if (!result) return result;
    auto quote = [&](const std::string& name, std::string& quoted) {
        UltraDbResult q = UltraDb_QuoteIdentifier(connection, name, quoted);
        if (!q) result = q;
        return q.success;
    };
    // WHERE "k1" = ? AND "k2" = ?, over the key as read.
    auto whereKey = [&](const RowChange& change, GeneratedStatement& statement) {
        statement.sql += " WHERE ";
        statement.preview += " WHERE ";
        for (size_t i = 0; i < table_.primaryKey.size(); ++i) {
            std::string column;
            if (!quote(table_.primaryKey[i], column)) return false;
            const char* glue = i == 0 ? "" : " AND ";
            statement.sql += glue + column + " = ?";
            statement.preview += glue + column + " = " + PreviewLiteral(change.key[i]);
            statement.params.push_back(change.key[i]);
        }
        return true;
    };

    for (const auto& change : changes_) {
        GeneratedStatement statement;
        statement.kind = change.kind;
        switch (change.kind) {
            case RowChange::Kind::Update: {
                statement.sql = "UPDATE " + target + " SET ";
                statement.preview = statement.sql;
                bool first = true;
                for (const auto& [name, value] : change.values) {
                    std::string column;
                    if (!quote(name, column)) return result;
                    UltraDbValue bound;
                    const bool binds = Bound(value, bound);
                    const char* glue = first ? "" : ", ";
                    statement.sql += glue + column + " = " + Placeholder(value);
                    statement.preview += glue + column + " = " + PreviewLiteral(value);
                    if (binds) statement.params.push_back(bound);
                    first = false;
                }
                if (!whereKey(change, statement)) return result;
                break;
            }
            case RowChange::Kind::Insert: {
                statement.sql = "INSERT INTO " + target;
                statement.preview = statement.sql;
                if (change.values.empty()) {
                    statement.sql += " DEFAULT VALUES";
                    statement.preview += " DEFAULT VALUES";
                    break;
                }
                std::string names, marks, literals;
                for (const auto& [name, value] : change.values) {
                    std::string column;
                    if (!quote(name, column)) return result;
                    UltraDbValue bound;
                    const bool binds = Bound(value, bound);
                    const char* glue = names.empty() ? "" : ", ";
                    names += glue + column;
                    marks += std::string(glue) + Placeholder(value);
                    literals += glue + PreviewLiteral(value);
                    if (binds) statement.params.push_back(bound);
                }
                statement.sql += " (" + names + ") VALUES (" + marks + ")";
                statement.preview += " (" + names + ") VALUES (" + literals + ")";
                break;
            }
            case RowChange::Kind::Delete:
                statement.sql = "DELETE FROM " + target;
                statement.preview = statement.sql;
                if (!whereKey(change, statement)) return result;
                break;
        }
        out.push_back(std::move(statement));
    }
    return UltraDbResult::Ok();
}

UltraDbResult EditBuffer::Commit(const std::string& connection, size_t& failed) const {
    failed = 0;
    std::vector<GeneratedStatement> statements;
    UltraDbResult result = Statements(connection, statements);
    if (!result) return result;
    UltraDbResult begun;
    const UltraDbHandle tx = UltraDb_Begin(connection, &begun);
    if (tx == UltraDbInvalidHandle) return begun;
    for (size_t i = 0; i < statements.size(); ++i) {
        const GeneratedStatement& statement = statements[i];
        result = UltraDb_ExecInTx(tx, statement.sql, statement.params);
        if (result && statement.kind != RowChange::Kind::Insert && result.affectedRows != 1) {
            result = UltraDbResult::Error(
                UltraDbResultCode::NotFound,
                result.affectedRows == 0
                    ? "no row has this key any more - it was changed or deleted since it was read"
                    : "the key matched " + std::to_string(result.affectedRows) + " rows, not one");
        }
        if (!result) {
            failed = i;
            UltraDb_Rollback(tx);
            return result;
        }
    }
    return UltraDb_Commit(tx);
}

} // namespace UltraDBAdmin
