// Apps/UltraDBAdmin/engine/DbAdminStructureEdit.cpp
// Version: 0.2.0 - MySQL / MariaDB: MODIFY COLUMN, DROP INDEX ... ON, each step committing on its own
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminStructureEdit.h"

#include "DbAdminEditBuffer.h"
#include "DbAdminExport.h"   // SqlLiteral

#include <algorithm>

namespace UltraDBAdmin {
namespace {

const char* const kSqliteAutoIndex = "sqlite_autoindex_";
const char* const kRebuildPrefix = "_dbadmin_new_";

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool Contains(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

// SQLite's rowid alias: a lone INTEGER PRIMARY KEY column.
bool RowidAlias(const std::string& type, bool soleKey) {
    std::string upper = Trim(type);
    for (auto& c : upper)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return soleKey && upper == "INTEGER";
}

std::string Join(const std::vector<std::string>& items) {
    std::string text;
    for (const auto& item : items) text += (text.empty() ? "" : ", ") + item;
    return text;
}

} // namespace

ColumnDraft DraftOf(const UltraDbColumnSchema& column) {
    ColumnDraft draft;
    draft.origin = column.name;
    draft.name = column.name;
    draft.type = column.declaredType;
    draft.notNull = !column.nullable;
    draft.hasDefault = column.hasDefault;
    draft.defaultExpression = column.hasDefault ? column.defaultExpression : std::string();
    return draft;
}

StructureEdit::StructureEdit(UltraDbTableSchema table, SqlEngine engine)
    : table_(std::move(table)), engine_(engine) {
    Revert();
}

void StructureEdit::Revert() {
    columns_.clear();
    for (const auto& column : table_.columns) columns_.push_back(DraftOf(column));
    newIndexes_.clear();
    droppedIndexes_.clear();
    newName_.clear();
}

std::string StructureEdit::ReadOnlyReason() const {
    if (table_.name.empty()) return "Pick a table.";
    switch (table_.kind) {
        case UltraDbObjectKind::Table:
            return std::string();
        case UltraDbObjectKind::View:
        case UltraDbObjectKind::MaterializedView:
            return "A view's structure is its query: change it with CREATE OR REPLACE VIEW in the SQL tab.";
        case UltraDbObjectKind::ForeignTable:
            return "A foreign table is changed with ALTER FOREIGN TABLE, in the SQL tab.";
        case UltraDbObjectKind::Sequence:
            return "A sequence is changed with ALTER SEQUENCE, in the SQL tab.";
    }
    return std::string();
}

bool StructureEdit::SameName(const std::string& a, const std::string& b) const {
    return engine_ == SqlEngine::Sqlite ? SameSqliteName(a, b) : a == b;
}

const UltraDbColumnSchema* StructureEdit::Original(size_t at) const {
    if (at >= columns_.size() || columns_[at].origin.empty()) return nullptr;
    for (const auto& column : table_.columns)
        if (column.name == columns_[at].origin) return &column;
    return nullptr;
}

bool StructureEdit::DefinitionChanged(size_t at) const {
    const UltraDbColumnSchema* original = Original(at);
    if (!original) return false;
    const ColumnDraft& draft = columns_[at];
    const ColumnDraft was = DraftOf(*original);
    return draft.type != was.type || draft.notNull != was.notNull || draft.hasDefault != was.hasDefault ||
           (draft.hasDefault && draft.defaultExpression != was.defaultExpression);
}

StructureEdit::ColumnState StructureEdit::StateOf(size_t at) const {
    if (at >= columns_.size()) return ColumnState::Unchanged;
    const ColumnDraft& draft = columns_[at];
    if (draft.origin.empty()) return ColumnState::New;
    if (draft.dropped) return ColumnState::Dropped;
    if (draft.name != draft.origin || DefinitionChanged(at)) return ColumnState::Changed;
    return ColumnState::Unchanged;
}

bool StructureEdit::IndexDropped(const std::string& name) const { return Contains(droppedIndexes_, name); }

size_t StructureEdit::Count(ColumnState state) const {
    size_t n = 0;
    for (size_t i = 0; i < columns_.size(); ++i) n += StateOf(i) == state;
    return n;
}

size_t StructureEdit::Count() const {
    return Count(ColumnState::Changed) + Count(ColumnState::New) + Count(ColumnState::Dropped) +
           newIndexes_.size() + droppedIndexes_.size() + (newName_.empty() ? 0 : 1);
}

bool StructureEdit::NeedsRebuild() const {
    if (engine_ != SqlEngine::Sqlite) return false;
    for (size_t i = 0; i < columns_.size(); ++i)
        if (!columns_[i].dropped && DefinitionChanged(i)) return true;
    return false;
}

bool StructureEdit::Destructive() const {
    return Count(ColumnState::Dropped) > 0 || !droppedIndexes_.empty();
}

bool StructureEdit::CheckColumn(const ColumnDraft& draft, const ColumnDraft* replacing, std::string& error) const {
    if (draft.name.empty()) { error = "A column needs a name."; return false; }
    if (draft.name.find('\0') != std::string::npos) { error = "A name cannot contain a NUL byte."; return false; }
    for (const auto& other : columns_) {
        if (&other == replacing) continue;
        if (!other.dropped && SameName(other.name, draft.name)) {
            error = "There is already a column " + draft.name + ".";
            return false;
        }
        // Renames run before drops and adds: until then the old name is taken.
        if (!draft.origin.empty() && draft.name != draft.origin && !other.origin.empty() &&
            SameName(other.origin, draft.name)) {
            error = draft.name + " is still the name of another column when the rename runs; "
                    "apply that change first.";
            return false;
        }
    }
    if (engine_ == SqlEngine::Postgres && draft.type.empty()) {
        error = "PostgreSQL needs a type: text, integer, numeric(12,2), timestamptz…";
        return false;
    }
    if (engine_ == SqlEngine::MySql && draft.type.empty()) {
        error = "MySQL needs a type: varchar(80), int, decimal(12,2), datetime…";
        return false;
    }
    if (!draft.type.empty() && !CheckSqlFragment(draft.type, "The type", error)) return false;
    if (draft.hasDefault) {
        if (draft.defaultExpression.empty()) {
            error = "Default is ticked but empty: write a value such as 0, 'text' or CURRENT_TIMESTAMP.";
            return false;
        }
        if (!CheckSqlFragment(draft.defaultExpression, "The default", error)) return false;
    }
    if (draft.origin.empty() && engine_ == SqlEngine::Sqlite && draft.notNull && !draft.hasDefault) {
        error = "SQLite adds a NOT NULL column only with a default, which the rows it already has receive.";
        return false;
    }
    return true;
}

bool StructureEdit::AddColumn(const ColumnDraft& given, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    ColumnDraft draft = given;
    draft.origin.clear();
    draft.dropped = false;
    draft.name = Trim(draft.name);
    draft.type = Trim(draft.type);
    draft.defaultExpression = draft.hasDefault ? Trim(draft.defaultExpression) : std::string();
    if (!CheckColumn(draft, nullptr, error)) return false;
    columns_.push_back(std::move(draft));
    return true;
}

bool StructureEdit::UpdateColumn(size_t at, const ColumnDraft& given, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    if (at >= columns_.size()) { error = "No such column."; return false; }
    ColumnDraft& current = columns_[at];
    if (current.dropped) { error = current.name + " is to be dropped; restore it to change it."; return false; }
    ColumnDraft draft = given;
    draft.origin = current.origin;
    draft.dropped = false;
    draft.name = Trim(draft.name);
    draft.type = Trim(draft.type);
    draft.defaultExpression = draft.hasDefault ? Trim(draft.defaultExpression) : std::string();
    if (const UltraDbColumnSchema* original = Original(at); original && original->generated) {
        const ColumnDraft was = DraftOf(*original);
        if (draft.type != was.type || draft.notNull != was.notNull || draft.hasDefault != was.hasDefault ||
            draft.defaultExpression != was.defaultExpression) {
            error = current.name + " is computed by the database: its definition is changed with SQL.";
            return false;
        }
    }
    if (!CheckColumn(draft, &current, error)) return false;
    // A new index on the column follows it to its new name.
    for (auto& index : newIndexes_)
        for (auto& column : index.columns)
            if (column == current.name) column = draft.name;
    current = std::move(draft);
    return true;
}

bool StructureEdit::DropColumn(size_t at, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    if (at >= columns_.size()) { error = "No such column."; return false; }
    const ColumnDraft& draft = columns_[at];
    if (draft.dropped) return true;
    for (const auto& index : newIndexes_) {
        if (Contains(index.columns, draft.name)) {
            error = "The new index " + index.name + " uses " + draft.name + ": drop the index first.";
            return false;
        }
    }
    if (draft.origin.empty()) {
        columns_.erase(columns_.begin() + static_cast<std::ptrdiff_t>(at));
        return true;
    }
    size_t remaining = 0;
    for (const auto& column : columns_) remaining += !column.dropped;
    if (remaining <= 1) { error = "A table needs at least one column."; return false; }
    if (engine_ == SqlEngine::Sqlite) {
        // What SQLite's DROP COLUMN refuses, said before Apply rather than at it.
        if (Contains(table_.primaryKey, draft.origin)) {
            error = "SQLite cannot drop a column of the primary key.";
            return false;
        }
        for (const auto& fk : table_.foreignKeys) {
            if (Contains(fk.columns, draft.origin)) {
                error = "SQLite cannot drop a column of a foreign key (to " + fk.refTable + ").";
                return false;
            }
        }
        for (const auto& index : table_.indexes) {
            if (!Contains(index.columns, draft.origin) || IndexDropped(index.name)) continue;
            error = index.name.rfind(kSqliteAutoIndex, 0) == 0
                        ? "SQLite cannot drop a column of a UNIQUE constraint."
                        : "SQLite cannot drop a column an index uses: drop the index " + index.name + " first.";
            return false;
        }
    }
    columns_[at].dropped = true;
    return true;
}

bool StructureEdit::RestoreColumn(size_t at, std::string& error) {
    if (at >= columns_.size() || !columns_[at].dropped) return true;
    ColumnDraft restored = columns_[at];
    restored.dropped = false;
    if (!CheckColumn(restored, &columns_[at], error)) return false;
    columns_[at].dropped = false;
    return true;
}

bool StructureEdit::AddIndex(const IndexDraft& given, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    IndexDraft draft = given;
    draft.name = Trim(draft.name);
    if (draft.name.empty()) { error = "An index needs a name."; return false; }
    if (draft.name.find('\0') != std::string::npos) { error = "A name cannot contain a NUL byte."; return false; }
    for (const auto& index : table_.indexes) {
        if (SameName(index.name, draft.name) && !IndexDropped(index.name)) {
            error = "There is already an index " + draft.name + ".";
            return false;
        }
    }
    for (const auto& index : newIndexes_) {
        if (SameName(index.name, draft.name)) { error = "There is already an index " + draft.name + "."; return false; }
    }
    if (draft.columns.empty()) { error = "Tick the columns the index is on."; return false; }
    for (size_t i = 0; i < draft.columns.size(); ++i) {
        const std::string& name = draft.columns[i];
        const bool exists = std::any_of(columns_.begin(), columns_.end(),
                                        [&](const ColumnDraft& c) { return !c.dropped && c.name == name; });
        if (!exists) { error = "No column " + name + " to index."; return false; }
        if (std::find(draft.columns.begin(), draft.columns.begin() + static_cast<std::ptrdiff_t>(i), name) !=
            draft.columns.begin() + static_cast<std::ptrdiff_t>(i)) {
            error = name + " is in the index twice.";
            return false;
        }
    }
    newIndexes_.push_back(std::move(draft));
    return true;
}

bool StructureEdit::DropIndex(const std::string& name, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    for (auto it = newIndexes_.begin(); it != newIndexes_.end(); ++it) {
        if (it->name == name) { newIndexes_.erase(it); return true; }
    }
    for (const auto& index : table_.indexes) {
        if (index.name != name) continue;
        if (index.primary) { error = "The primary key is part of the table's definition, not an index of its own."; return false; }
        if (engine_ == SqlEngine::Sqlite && name.rfind(kSqliteAutoIndex, 0) == 0) {
            error = "This index belongs to a UNIQUE constraint of the table's definition; SQLite cannot drop it.";
            return false;
        }
        if (!IndexDropped(name)) droppedIndexes_.push_back(name);
        return true;
    }
    error = "No index " + name + ".";
    return false;
}

bool StructureEdit::RenameTable(const std::string& given, std::string& error) {
    error = ReadOnlyReason();
    if (!error.empty()) return false;
    const std::string name = Trim(given);
    if (name.find('\0') != std::string::npos) { error = "A name cannot contain a NUL byte."; return false; }
    newName_ = name == table_.name ? std::string() : name;
    return true;
}

std::string StructureEdit::ColumnDefinition(const ColumnDraft& draft, const std::string& quotedName) const {
    std::string text = quotedName;
    if (!draft.type.empty()) text += " " + draft.type;
    if (draft.notNull) text += " NOT NULL";
    if (draft.hasDefault) {
        text += " " + (engine_ == SqlEngine::Sqlite ? SqliteDefaultClause(draft.defaultExpression)
                                                     : "DEFAULT " + draft.defaultExpression);
    }
    return text;
}

UltraDbResult StructureEdit::Plan(const std::string& connection, const SqliteTableSource& source,
                                  std::vector<DdlStep>& out) const {
    out.clear();
    UltraDbResult result = UltraDbResult::Ok();
    auto quote = [&](const std::string& name) {
        std::string quoted;
        if (result) {
            const UltraDbResult q = UltraDb_QuoteIdentifier(connection, name, quoted);
            if (!q) result = q;
        }
        return quoted;
    };
    const std::string& schema = table_.schema;
    auto qualify = [&](const std::string& name) {
        return schema.empty() ? quote(name) : quote(schema) + "." + quote(name);
    };
    auto add = [&](DdlStep::Phase phase, const std::string& sql, const std::string& note = std::string()) {
        DdlStep step;
        step.phase = phase;
        step.sql = sql;
        step.preview = sql;
        step.note = note;
        out.push_back(std::move(step));
        return &out.back();
    };
    const std::string target = qualify(table_.name);
    const std::string alter = "ALTER TABLE " + target;
    const bool sqlite = engine_ == SqlEngine::Sqlite;
    const bool mysql = engine_ == SqlEngine::MySql;
    // MySQL commits before and after each ALTER TABLE: no transaction can
    // hold them, so its steps run one by one, outside one.
    const DdlStep::Phase inside = mysql ? DdlStep::Phase::Before : DdlStep::Phase::Inside;

    // 1. Indexes to drop, first: a rebuild does not put them back. MySQL
    // names an index's table, not its schema.
    for (const auto& name : droppedIndexes_)
        add(inside, mysql ? "DROP INDEX " + quote(name) + " ON " + target : "DROP INDEX " + qualify(name));

    // 2. Definitions, under the old names.
    if (sqlite && NeedsRebuild()) {
        SqliteCreateTable parsed;
        std::string why;
        if (!ParseCreateTable(source.createSql, parsed, why)) {
            return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                        "SQLite changes a column's type, NOT NULL or default only by rebuilding the "
                                        "table from its definition, and this definition cannot be read here: " +
                                            why + ". Change it with SQL in the SQL tab.");
        }
        std::vector<ColumnRewrite> rewrites;
        bool newAlias = false;
        for (size_t i = 0; i < columns_.size(); ++i) {
            const ColumnDraft& draft = columns_[i];
            const UltraDbColumnSchema* original = Original(i);
            if (!original) continue;
            const ColumnDraft was = DraftOf(*original);
            const bool soleKey = table_.primaryKey.size() == 1 && table_.primaryKey[0] == draft.origin;
            if (RowidAlias(draft.dropped ? was.type : draft.type, soleKey)) newAlias = true;
            if (draft.dropped || !DefinitionChanged(i)) continue;
            ColumnRewrite rewrite;
            rewrite.column = draft.origin;
            rewrite.setType = draft.type != was.type;
            rewrite.type = draft.type;
            rewrite.setNotNull = draft.notNull != was.notNull;
            rewrite.notNull = draft.notNull;
            rewrite.setDefault = draft.hasDefault != was.hasDefault ||
                                 (draft.hasDefault && draft.defaultExpression != was.defaultExpression);
            rewrite.hasDefault = draft.hasDefault;
            rewrite.defaultExpression = draft.defaultExpression;
            rewrites.push_back(std::move(rewrite));
        }
        const std::string temporary = qualify(kRebuildPrefix + table_.name);
        std::string create;
        if (!RewriteCreateTable(source.createSql, temporary, rewrites, create, why))
            return UltraDbResult::Error(UltraDbResultCode::Unsupported, "The table cannot be rebuilt here: " + why + ".");

        // Every stored column, by name; the rowid too where no column is it,
        // so the rows keep the numbers they had.
        std::vector<std::string> copied;
        bool oldAlias = false;
        for (const auto& column : table_.columns) {
            if (column.autoIncrement && table_.primaryKey.size() == 1) oldAlias = true;
            if (!column.generated) copied.push_back(quote(column.name));
        }
        if (!parsed.withoutRowid && !oldAlias && !newAlias) copied.insert(copied.begin(), "rowid");
        const std::string list = Join(copied);

        if (source.foreignKeys) {
            add(DdlStep::Phase::Before, "PRAGMA foreign_keys = OFF",
                "Before the transaction (SQLite ignores it inside one): the old table is dropped below, "
                "which must not cascade to the rows that refer to it.");
        }
        add(DdlStep::Phase::Inside, create,
            "Rebuild " + table_.name + ": SQLite cannot change a column's type, NOT NULL or default in place. "
            "The new table is the old definition with only those columns rewritten.");
        add(DdlStep::Phase::Inside, "INSERT INTO " + temporary + " (" + list + ") SELECT " + list + " FROM " + target);
        add(DdlStep::Phase::Inside, "DROP TABLE " + target);
        add(DdlStep::Phase::Inside, "PRAGMA legacy_alter_table = ON",
            "So the rename leaves the views and triggers that name " + table_.name + " as they are.");
        add(DdlStep::Phase::Inside, "ALTER TABLE " + temporary + " RENAME TO " + quote(table_.name));
        if (!source.legacyAlterTable) add(DdlStep::Phase::Inside, "PRAGMA legacy_alter_table = OFF");
        if (source.sequence >= 0) {
            const std::string sequence = qualify("sqlite_sequence");
            DdlStep* clear = add(DdlStep::Phase::Inside, "DELETE FROM " + sequence + " WHERE name = ?",
                                 "AUTOINCREMENT goes on from where it was.");
            clear->params = {UltraDbValue(table_.name)};
            clear->preview = "DELETE FROM " + sequence + " WHERE name = " + PreviewLiteral(UltraDbValue(table_.name));
            DdlStep* keep = add(DdlStep::Phase::Inside, "INSERT INTO " + sequence +
                                                            " (name, seq) SELECT ?, MAX(?, COALESCE(MAX(rowid), 0)) FROM " +
                                                            target);
            keep->params = {UltraDbValue(table_.name), UltraDbValue(source.sequence)};
            keep->preview = "INSERT INTO " + sequence + " (name, seq) SELECT " +
                            PreviewLiteral(UltraDbValue(table_.name)) + ", MAX(" + std::to_string(source.sequence) +
                            ", COALESCE(MAX(rowid), 0)) FROM " + target;
        }
        bool first = true;
        for (const auto& [name, sql] : source.indexes) {
            if (IndexDropped(name)) continue;
            add(DdlStep::Phase::Inside, sql, first ? "The indexes and triggers the drop took with it." : "");
            first = false;
        }
        for (const auto& sql : source.triggers) {
            add(DdlStep::Phase::Inside, sql, first ? "The triggers the drop took with it." : "");
            first = false;
        }
    } else if (mysql) {
        // MODIFY COLUMN states the whole column anew: what it does not say is
        // gone. So AUTO_INCREMENT, the collation and the comment are said
        // again as they were.
        bool first = true;
        for (size_t i = 0; i < columns_.size(); ++i) {
            const ColumnDraft& draft = columns_[i];
            const UltraDbColumnSchema* original = Original(i);
            if (!original || draft.dropped || !DefinitionChanged(i)) continue;
            std::string definition = ColumnDefinition(draft, quote(draft.origin));
            if (original->autoIncrement) definition += " AUTO_INCREMENT";
            if (!original->collation.empty() && draft.type == DraftOf(*original).type)
                definition += " COLLATE " + original->collation;
            if (!original->comment.empty())
                definition += " COMMENT " + SqlLiteral(UltraDbValue(original->comment), SqlEngine::MySql);
            add(inside, alter + " MODIFY COLUMN " + definition,
                first ? "MODIFY states the whole column: its AUTO_INCREMENT, collation and comment are kept, "
                        "an ON UPDATE clause is not."
                      : "");
            first = false;
        }
    } else if (!sqlite) {
        for (size_t i = 0; i < columns_.size(); ++i) {
            const ColumnDraft& draft = columns_[i];
            const UltraDbColumnSchema* original = Original(i);
            if (!original || draft.dropped || !DefinitionChanged(i)) continue;
            const ColumnDraft was = DraftOf(*original);
            const std::string column = alter + " ALTER COLUMN " + quote(draft.origin);
            const bool typeChanged = draft.type != was.type;
            // A default is dropped before the type changes and set again
            // after: PostgreSQL does not convert it with USING, and one
            // written for the old type ('1'::text) fails there rather than
            // at SET DEFAULT, where its error says what to rewrite.
            const bool defaultChanged = typeChanged || draft.hasDefault != was.hasDefault ||
                                        (draft.hasDefault && draft.defaultExpression != was.defaultExpression);
            if (defaultChanged && was.hasDefault) add(DdlStep::Phase::Inside, column + " DROP DEFAULT");
            if (typeChanged) {
                add(DdlStep::Phase::Inside, column + " TYPE " + draft.type + " USING CAST(" + quote(draft.origin) +
                                                " AS " + draft.type + ")");
            }
            if (draft.notNull != was.notNull)
                add(DdlStep::Phase::Inside, column + (draft.notNull ? " SET NOT NULL" : " DROP NOT NULL"));
            if (defaultChanged && draft.hasDefault)
                add(DdlStep::Phase::Inside, column + " SET DEFAULT " + draft.defaultExpression);
        }
    }

    // 3. Renames, 4. drops, 5. new columns.
    for (const auto& draft : columns_) {
        if (!draft.origin.empty() && !draft.dropped && draft.name != draft.origin)
            add(inside, alter + " RENAME COLUMN " + quote(draft.origin) + " TO " + quote(draft.name));
    }
    for (const auto& draft : columns_) {
        if (!draft.origin.empty() && draft.dropped)
            add(inside, alter + " DROP COLUMN " + quote(draft.origin));
    }
    for (const auto& draft : columns_) {
        if (draft.origin.empty())
            add(inside, alter + " ADD COLUMN " + ColumnDefinition(draft, quote(draft.name)));
    }

    // 6. New indexes. SQLite puts the schema on the index's name, PostgreSQL
    // on the table's (an index lives in its table's schema).
    for (const auto& index : newIndexes_) {
        std::vector<std::string> columns;
        for (const auto& column : index.columns) columns.push_back(quote(column));
        const std::string head = index.unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX ";
        add(inside, sqlite ? head + qualify(index.name) + " ON " + quote(table_.name) + " (" +
                                                 Join(columns) + ")"
                                           : head + quote(index.name) + " ON " + target + " (" + Join(columns) + ")");
    }

    // 7. The table's own name, last.
    const std::string finalName = newName_.empty() ? table_.name : newName_;
    if (!newName_.empty()) add(inside, alter + " RENAME TO " + quote(newName_));

    // The rebuild ran with foreign keys off: nothing may refer to a row that is not there.
    if (sqlite && NeedsRebuild() && source.foreignKeys) {
        std::vector<std::string> tables = {finalName};
        for (const auto& name : source.referencedBy)
            if (name != table_.name) tables.push_back(name);
        bool first = true;
        for (const auto& name : tables) {
            DdlStep* check = add(DdlStep::Phase::Inside,
                                 "PRAGMA " + (schema.empty() ? std::string() : quote(schema) + ".") +
                                     "foreign_key_check(" + quote(name) + ")",
                                 first ? "Must find nothing, or everything above is rolled back." : "");
            check->check = true;
            check->checkFailure = "rows of " + name + " refer to rows that are not there";
            first = false;
        }
        add(DdlStep::Phase::After, "PRAGMA foreign_keys = ON", "After the transaction, whatever its outcome.");
    }
    if (mysql && !out.empty()) {
        const std::string note = "MySQL commits each statement as it runs: should one fail, those before it stay done.";
        out.front().note = out.front().note.empty() ? note : note + " " + out.front().note;
    }
    if (!result) out.clear();
    return result;
}

std::string StructureEdit::PlanText(const std::vector<DdlStep>& steps) {
    std::string text;
    bool inside = false;
    for (const auto& step : steps) {
        const bool stepInside = step.phase == DdlStep::Phase::Inside;
        if (stepInside && !inside) text += "BEGIN;\n";
        if (!stepInside && inside) text += "COMMIT;\n";
        inside = stepInside;
        if (!step.note.empty()) text += (text.empty() ? "" : "\n") + std::string("-- ") + step.note + "\n";
        text += step.preview + ";\n";
    }
    if (inside) text += "COMMIT;\n";
    return text;
}

UltraDbResult StructureEdit::Apply(const std::string& connection, const std::vector<DdlStep>& steps, size_t& failed) {
    failed = 0;
    auto after = [&]() {
        for (const auto& step : steps)
            if (step.phase == DdlStep::Phase::After) UltraDb_Exec(connection, step.sql, step.params);
    };
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].phase != DdlStep::Phase::Before) continue;
        const UltraDbResult result = UltraDb_Exec(connection, steps[i].sql, steps[i].params);
        if (!result) { failed = i; after(); return result; }
    }
    // Nothing inside (MySQL's steps all run on their own): no transaction.
    if (std::none_of(steps.begin(), steps.end(), [](const DdlStep& s) { return s.phase == DdlStep::Phase::Inside; })) {
        after();
        return UltraDbResult::Ok();
    }
    UltraDbResult begun;
    const UltraDbHandle tx = UltraDb_Begin(connection, &begun);
    if (tx == UltraDbInvalidHandle) { after(); return begun; }
    for (size_t i = 0; i < steps.size(); ++i) {
        const DdlStep& step = steps[i];
        if (step.phase != DdlStep::Phase::Inside) continue;
        UltraDbResult result;
        if (step.check) {
            UltraDbResultSet rows;
            result = UltraDb_QueryInTx(tx, step.sql, step.params, rows);
            if (result && !rows.Empty()) {
                result = UltraDbResult::Error(UltraDbResultCode::ConstraintViolation,
                                              std::to_string(rows.Size()) + " " + step.checkFailure);
            }
        } else {
            result = UltraDb_ExecInTx(tx, step.sql, step.params);
        }
        if (!result) {
            failed = i;
            UltraDb_Rollback(tx);
            after();
            return result;
        }
    }
    const UltraDbResult committed = UltraDb_Commit(tx);
    after();
    return committed;
}

} // namespace UltraDBAdmin
