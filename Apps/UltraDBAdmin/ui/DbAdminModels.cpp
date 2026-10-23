// Apps/UltraDBAdmin/ui/DbAdminModels.cpp
// Version: 0.5.0 - staged new rows shown after the rows read
// Version: 0.4.0 - LogModel, SearchHitModel
// Version: 0.3.0 - a column's width is the view's to measure (SetColumnFitting);
//                  the model gives the header's estimate only
// Version: 0.2.0 - a staged blob shows as its size
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminModels.h"

#include "DbAdminFormat.h"

#include <algorithm>
#include <cstdio>
#include <utility>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

// How much of a value a tooltip shows: enough to read a paragraph, not a
// whole document.
constexpr size_t kTooltipChars = 2000;

std::string Join(const std::vector<std::string>& items, const char* separator = ", ") {
    std::string text;
    for (const auto& item : items) {
        if (!text.empty()) text += separator;
        text += item;
    }
    return text;
}

// Characters, not bytes: "Größe" is five wide.
size_t Utf8Length(const std::string& text) {
    size_t n = 0;
    for (const unsigned char c : text) n += (c & 0xC0) != 0x80;
    return n;
}

// A column's width before the view has measured it (FitResultColumns in
// DbAdminTheme fits every column to its text at the first paint): wide
// enough for the title, from the character count.
int TextWidth(size_t characters) {
    return std::clamp(static_cast<int>(characters) * 9 + 24, 60, 360);
}

// The whole value for a tooltip: every line, up to kTooltipChars.
std::string TooltipText(const UltraDbValue& value) {
    if (value.IsNull() || value.Type() == UltraDbType::Blob) return CellText(value);
    std::string text = value.AsString();
    if (text.size() > kTooltipChars) text = CellText(UltraDbValue(text), kTooltipChars);
    return text;
}

} // namespace

// ===== ResultSetModel =====

int ResultSetModel::GetRowCount() const { return static_cast<int>(rows_.Size() + StagedRows()); }
int ResultSetModel::GetColumnCount() const { return static_cast<int>(columns_.size()); }

size_t ResultSetModel::StagedRows() const {
    if (!pending_ || columns_.empty()) return 0;
    size_t n = 0;
    while (pending_->InsertAt(n)) ++n;
    return n;
}

ListDataValue ResultSetModel::StagedData(size_t insert, int column, ListDataRole role) const {
    const RowChange* change = pending_ ? pending_->InsertAt(insert) : nullptr;
    if (!change) return {};
    const std::string& name = columns_[static_cast<size_t>(column)];
    const auto it = change->values.find(name);
    std::string text;
    if (it == change->values.end()) {
        text = "(default)";              // left to the column's default, or computed
    } else {
        switch (it->second.kind) {
            case CellValue::Kind::Null:     text = NullText(); break;
            case CellValue::Kind::Blob:     text = CellText(UltraDbValue::Blob(it->second.bytes)); break;
            case CellValue::Kind::Default:  text = "(default)"; break;
            case CellValue::Kind::Function: text = it->second.text; break;
            case CellValue::Kind::Text:     text = CellText(UltraDbValue(it->second.text)); break;
        }
    }
    switch (role) {
        case ListDataRole::DisplayRole:
            return column == 0 ? "NEW " + text : text;
        case ListDataRole::ToolTipRole:
            return std::string("A new row, not written yet: Commit inserts it, Revert drops it. Double-click to "
                               "change it.");
        default:
            return {};
    }
}

ListDataValue ResultSetModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount() || index.column < 0 || index.column >= GetColumnCount())
        return {};
    // The rows read first; after them, the new rows staged for the table.
    if (static_cast<size_t>(index.row) >= rows_.Size())
        return StagedData(static_cast<size_t>(index.row) - rows_.Size(), index.column, role);
    const UltraDbRow& row = rows_.Row(static_cast<size_t>(index.row));
    if (static_cast<size_t>(index.column) >= row.Size()) return {};
    const UltraDbValue& value = row[static_cast<size_t>(index.column)];
    const CellValue* staged = nullptr;
    bool deleted = false;
    if (pending_ && (role == ListDataRole::DisplayRole || role == ListDataRole::ToolTipRole)) {
        staged = pending_->PendingCell(row, columns_[static_cast<size_t>(index.column)]);
        deleted = pending_->PendingDelete(row);
    }
    switch (role) {
        case ListDataRole::DisplayRole:
            if (deleted && index.column == 0) return "DEL " + CellText(value);
            if (staged) {
                switch (staged->kind) {
                    case CellValue::Kind::Null: return std::string(NullText()) + " *";
                    case CellValue::Kind::Blob: return CellText(UltraDbValue::Blob(staged->bytes)) + " *";
                    default:                    return CellText(UltraDbValue(staged->text)) + " *";
                }
            }
            return CellText(value);
        case ListDataRole::ToolTipRole:
            if (deleted) return std::string("Deleted when you commit; Revert keeps it.");
            if (staged) return "Was: " + TooltipText(value) + "\nStaged: " + PreviewLiteral(*staged);
            return TooltipText(value);
        case ListDataRole::SortRole:
            // Numbers sort as numbers; everything else by its text. NULL
            // answers nothing, so it sorts with the empty values.
            if (value.Type() == UltraDbType::Int || value.Type() == UltraDbType::Double)
                return static_cast<float>(value.AsDouble());
            return {};
        default:
            return {};
    }
}

ListColumnDef ResultSetModel::GetColumnDef(int column) const {
    if (column < 0 || column >= GetColumnCount()) return ListColumnDef("", 80);
    const std::string& title = columns_[static_cast<size_t>(column)];
    std::string tip;
    if (static_cast<size_t>(column) < columnTypes_.size()) tip = columnTypes_[static_cast<size_t>(column)];
    const auto& info = rows_.ColumnInfo();
    if (tip.empty() && static_cast<size_t>(column) < info.size()) tip = info[static_cast<size_t>(column)].declaredType;
    return ListColumnDef(title, TextWidth(Utf8Length(title) + 2), TextAlignment::Left, tip);   // + the sort mark
}

void ResultSetModel::Replace(UltraDbResultSet rows, std::vector<std::string> columnTypes) {
    rows_ = std::move(rows);
    columns_ = rows_.Columns();
    columnTypes_ = std::move(columnTypes);
    NotifyDataChanged();
}

// ===== ColumnSchemaModel =====

int ColumnSchemaModel::GetRowCount() const {
    return static_cast<int>(pending_ ? pending_->Columns().size() : columns_.size());
}

ListDataValue ColumnSchemaModel::DraftData(const ListIndex& index, ListDataRole role) const {
    const size_t at = static_cast<size_t>(index.row);
    const ColumnDraft& draft = pending_->Columns()[at];
    const UltraDbColumnSchema* original = pending_->Original(at);
    const StructureEdit::ColumnState state = pending_->StateOf(at);
    const ColumnDraft was = original ? DraftOf(*original) : ColumnDraft();
    // " *" on what this column changes, so the eye finds it.
    auto mark = [&](std::string text, bool changed) {
        return state == StructureEdit::ColumnState::Changed && changed ? text + " *" : text;
    };
    if (role == ListDataRole::ToolTipRole) {
        switch (state) {
            case StructureEdit::ColumnState::New:     return std::string("Added when you apply; Revert drops it.");
            case StructureEdit::ColumnState::Dropped: return std::string("Dropped when you apply, with its data; Drop column again restores it.");
            case StructureEdit::ColumnState::Changed:
                return draft.name != draft.origin ? "Was " + draft.origin + ". Changed when you apply."
                                                  : std::string("Changed when you apply.");
            default: break;
        }
        if (index.column == Default && draft.hasDefault) return draft.defaultExpression;
        if (index.column == Comment && original) return original->comment;
        return {};
    }
    if (role != ListDataRole::DisplayRole) return {};
    switch (index.column) {
        case Name:
            if (state == StructureEdit::ColumnState::New) return "NEW " + draft.name;
            if (state == StructureEdit::ColumnState::Dropped) return "DROP " + draft.name;
            return mark(draft.name, draft.name != draft.origin);
        case Type:    return mark(draft.type.empty() ? std::string("(any)") : draft.type, draft.type != was.type);
        case Null:    return mark(draft.notNull ? "no" : "yes", draft.notNull != was.notNull);
        case Default:
            return mark(draft.hasDefault ? draft.defaultExpression : std::string(),
                        draft.hasDefault != was.hasDefault || draft.defaultExpression != was.defaultExpression);
        case Key: {
            if (!original) return std::string();
            const auto it = std::find(primaryKey_.begin(), primaryKey_.end(), original->name);
            if (it == primaryKey_.end()) return std::string();
            return primaryKey_.size() == 1 ? std::string("PRIMARY")
                                           : "PRIMARY " + std::to_string(it - primaryKey_.begin() + 1);
        }
        case Extra: {
            if (!original) return std::string();
            std::string extra;
            if (original->autoIncrement) extra = "identity";
            if (original->generated) extra += extra.empty() ? "generated" : ", generated";
            return extra;
        }
        case Collation: return original ? original->collation : std::string();
        case Comment:   return original ? original->comment : std::string();
        default:        return {};
    }
}

ListDataValue ColumnSchemaModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount()) return {};
    if (pending_) return DraftData(index, role);
    const UltraDbColumnSchema& c = columns_[static_cast<size_t>(index.row)];
    if (role == ListDataRole::ToolTipRole) {
        if (index.column == Default && c.hasDefault) return c.defaultExpression;
        if (index.column == Comment) return c.comment;
        return {};
    }
    if (role != ListDataRole::DisplayRole) return {};
    switch (index.column) {
        case Name:    return c.name;
        case Type:    return c.declaredType.empty() ? std::string("(any)") : c.declaredType;
        case Null:    return std::string(c.nullable ? "yes" : "no");
        case Default: return c.hasDefault ? c.defaultExpression : std::string();
        case Key: {
            const auto it = std::find(primaryKey_.begin(), primaryKey_.end(), c.name);
            if (it == primaryKey_.end()) return std::string();
            // The position in a composite key matters: (tenant, id) is not (id, tenant).
            return primaryKey_.size() == 1 ? std::string("PRIMARY")
                                           : "PRIMARY " + std::to_string(it - primaryKey_.begin() + 1);
        }
        case Extra: {
            std::string extra;
            if (c.autoIncrement) extra = "identity";
            if (c.generated) extra += extra.empty() ? "generated" : ", generated";
            return extra;
        }
        case Collation: return c.collation;
        case Comment:   return c.comment;
        default:        return {};
    }
}

ListColumnDef ColumnSchemaModel::GetColumnDef(int column) const {
    switch (column) {
        case Name:      return ListColumnDef("Column", 180);
        case Type:      return ListColumnDef("Type", 150, TextAlignment::Left, "As the database spells it");
        case Null:      return ListColumnDef("Null", 50);
        case Default:   return ListColumnDef("Default", 150);
        case Key:       return ListColumnDef("Key", 90, TextAlignment::Left,
                                             "Part of the primary key, and where in it");
        case Extra:     return ListColumnDef("Extra", 110, TextAlignment::Left,
                                             "Identity: numbered by the database; generated: computed, not writable");
        case Collation: return ListColumnDef("Collation", 110, TextAlignment::Left,
                                             "Shown only where it differs from the type's default");
        case Comment:   return ListColumnDef("Comment", 220);
        default:        return ListColumnDef("", 80);
    }
}

void ColumnSchemaModel::Replace(const UltraDbTableSchema& table) {
    columns_ = table.columns;
    primaryKey_ = table.primaryKey;
    NotifyDataChanged();
}

// ===== ConstraintModel =====

int ConstraintModel::GetRowCount() const { return static_cast<int>(entries_.size()); }

ListDataValue ConstraintModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount()) return {};
    const Entry& e = entries_[static_cast<size_t>(index.row)];
    if (role == ListDataRole::ToolTipRole) return e.tooltip;
    if (role != ListDataRole::DisplayRole) return {};
    switch (index.column) {
        case Kind:    return e.kind;
        case Name:    return e.name;
        case Columns: return e.columns;
        case Details: return e.details;
        default:      return {};
    }
}

ListColumnDef ConstraintModel::GetColumnDef(int column) const {
    switch (column) {
        case Kind:    return ListColumnDef("Kind", 130);
        case Name:    return ListColumnDef("Name", 200);
        case Columns: return ListColumnDef("Columns", 200);
        case Details: return ListColumnDef("Details", 320);
        default:      return ListColumnDef("", 80);
    }
}

std::string ConstraintModel::IndexAt(int row) const {
    if (row < 0 || row >= GetRowCount()) return std::string();
    const Entry& e = entries_[static_cast<size_t>(row)];
    return e.index ? e.name : std::string();
}

void ConstraintModel::Replace(const UltraDbTableSchema& table) {
    table_ = table;
    Refresh();
}

void ConstraintModel::Refresh() {
    const UltraDbTableSchema& table = table_;
    entries_.clear();
    bool keyListed = false;
    for (const auto& index : table.indexes) {
        Entry e;
        e.kind = index.primary ? "PRIMARY KEY" : index.unique ? "UNIQUE" : "INDEX";
        e.name = index.name;
        e.columns = Join(index.columns);
        e.index = !index.primary;
        if (pending_ && pending_->IndexDropped(index.name)) {
            e.kind = "DROP " + e.kind;
            e.tooltip = "Dropped when you apply; Revert keeps it.";
        }
        keyListed = keyListed || index.primary;
        entries_.push_back(std::move(e));
    }
    if (pending_) {
        for (const auto& index : pending_->NewIndexes()) {
            Entry e;
            e.kind = index.unique ? "NEW UNIQUE" : "NEW INDEX";
            e.name = index.name;
            e.columns = Join(index.columns);
            e.index = true;
            e.tooltip = "Created when you apply; Drop index removes it again.";
            entries_.push_back(std::move(e));
        }
    }
    // SQLite's rowid key has no index of its own; it is still the key.
    if (!keyListed && !table.primaryKey.empty()) {
        Entry e;
        e.kind = "PRIMARY KEY";
        e.columns = Join(table.primaryKey);
        entries_.insert(entries_.begin(), std::move(e));
    }
    for (const auto& fk : table.foreignKeys) {
        Entry e;
        e.kind = "FOREIGN KEY";
        e.name = fk.name;
        e.columns = Join(fk.columns);
        const std::string target = (fk.refSchema.empty() ? std::string() : fk.refSchema + ".") + fk.refTable;
        e.details = "references " + target + " (" + Join(fk.refColumns) + ")";
        e.tooltip = e.details + "\nON UPDATE " + fk.onUpdate + "\nON DELETE " + fk.onDelete;
        if (fk.onDelete != "NO ACTION") e.details += ", on delete " + fk.onDelete;
        entries_.push_back(std::move(e));
    }
    for (const auto& trigger : table.triggers) {
        Entry e;
        e.kind = "TRIGGER";
        e.name = trigger.name;
        e.details = CellText(UltraDbValue(trigger.definition), 120);
        e.tooltip = trigger.definition;
        entries_.push_back(std::move(e));
    }
    NotifyDataChanged();
}

// ===== StatementModel =====

std::string OutcomeText(const UltraDbStatementResult& statement) {
    const UltraDbResult& r = statement.result;
    if (r.code == UltraDbResultCode::Cancelled) return "cancelled";
    if (!r.success) return "error: " + CellText(UltraDbValue(r.message), 160);
    if (statement.returnsRows) {
        const size_t n = statement.rows.Size();
        return std::to_string(n) + (n == 1 ? " row" : " rows");
    }
    if (r.affectedRows > 0) {
        return std::to_string(r.affectedRows) + (r.affectedRows == 1 ? " row affected" : " rows affected");
    }
    return "done";
}

int StatementModel::GetRowCount() const { return static_cast<int>(statements_.size()); }

ListDataValue StatementModel::GetData(const ListIndex& index, ListDataRole role) const {
    const UltraDbStatementResult* s = At(index.row);
    if (!s) return {};
    if (role == ListDataRole::ToolTipRole) {
        if (index.column == Statement) return s->sql;
        if (index.column == Outcome && !s->result.success) return s->result.message;
        return {};
    }
    if (role == ListDataRole::SortRole) {
        if (index.column == Number) return index.row + 1;
        if (index.column == Time) return static_cast<float>(s->result.processingTimeMs);
        return {};
    }
    if (role != ListDataRole::DisplayRole) return {};
    switch (index.column) {
        case Number:    return std::to_string(index.row + 1);
        case Statement: return CellText(UltraDbValue(s->sql), 120);
        case Outcome:   return OutcomeText(*s);
        case Time: {
            // locale-ok: a duration for the reader, in the desktop's own decimal mark.
            char text[32];
            std::snprintf(text, sizeof text, "%.1f ms", s->result.processingTimeMs);
            return std::string(text);
        }
        default: return {};
    }
}

ListColumnDef StatementModel::GetColumnDef(int column) const {
    switch (column) {
        case Number:    return ListColumnDef("#", 36, TextAlignment::Right);
        case Statement: return ListColumnDef("Statement", 220);
        case Outcome:   return ListColumnDef("Result", 200);
        case Time:      return ListColumnDef("Time", 80, TextAlignment::Right);
        default:        return ListColumnDef("", 80);
    }
}

void StatementModel::Replace(std::vector<UltraDbStatementResult> statements) {
    statements_ = std::move(statements);
    NotifyDataChanged();
}

const UltraDbStatementResult* StatementModel::At(int row) const {
    if (row < 0 || row >= GetRowCount()) return nullptr;
    return &statements_[static_cast<size_t>(row)];
}

// ===== LogModel =====

ListDataValue LogModel::GetData(const ListIndex& index, ListDataRole role) const {
    const LogEntry* entry = At(index.row);
    if (!entry) return {};
    if (role == ListDataRole::ToolTipRole) {
        std::string tip = entry->sql;
        if (tip.size() > kTooltipChars) tip = CellText(UltraDbValue(tip), kTooltipChars);
        if (entry->boundValues > 0)
            tip += "\n\n" + std::to_string(entry->boundValues) +
                   (entry->boundValues == 1 ? " value bound (not shown)" : " values bound (not shown)");
        if (entry->inTransaction) tip += "\nIn a transaction.";
        if (!entry->ok) tip += "\n\n" + entry->message;
        return tip;
    }
    if (role != ListDataRole::DisplayRole) return {};
    switch (index.column) {
        case Time:      return LogTime(*entry);
        case Where:     return where_ ? where_(entry->connection) : entry->connection;
        case Took:      return LogDuration(*entry);
        case Result:    return entry->ok ? LogOutcome(*entry) : "failed: " + CellText(UltraDbValue(entry->message), 80);
        case Statement: return CellText(UltraDbValue(entry->sql), 300);
        default:        return {};
    }
}

ListColumnDef LogModel::GetColumnDef(int column) const {
    switch (column) {
        case Time:      return ListColumnDef("Time", 112);
        case Where:     return ListColumnDef("Connection", 160);
        case Took:      return ListColumnDef("Took", 64, TextAlignment::Right);
        case Result:    return ListColumnDef("Result", 140);
        case Statement: return ListColumnDef("Statement", 560);
        default:        return ListColumnDef("", 80);
    }
}

void LogModel::Append(const std::vector<LogEntry>& entries) {
    if (entries.empty()) return;
    entries_.insert(entries_.end(), entries.begin(), entries.end());
    NotifyDataChanged();
}

void LogModel::Clear() {
    entries_.clear();
    NotifyDataChanged();
}

void LogModel::Trim(size_t keep) {
    if (entries_.size() <= keep) return;
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - keep));
    NotifyDataChanged();
}

const LogEntry* LogModel::At(int row) const {
    if (row < 0 || row >= GetRowCount()) return nullptr;
    return &entries_[static_cast<size_t>(row)];
}

// ===== SearchHitModel =====

ListDataValue SearchHitModel::GetData(const ListIndex& index, ListDataRole role) const {
    const SearchHit* hit = At(index.row);
    if (!hit) return {};
    if (role == ListDataRole::ToolTipRole) return std::string("Double-click: open ") + hit->table +
                                                  " showing only these rows";
    if (role == ListDataRole::SortRole && index.column == RowsField) return static_cast<float>(hit->rows);
    if (role != ListDataRole::DisplayRole) return {};
    switch (index.column) {
        case TableField:  return hit->kind == UltraDbObjectKind::Table ? hit->table : hit->table + " (view)";
        case ColumnField: return hit->column;
        case RowsField:   return std::to_string(hit->rows);
        default:     return {};
    }
}

ListColumnDef SearchHitModel::GetColumnDef(int column) const {
    switch (column) {
        case TableField:  return ListColumnDef("Table", 220);
        case ColumnField: return ListColumnDef("Column", 200);
        case RowsField:   return ListColumnDef("Rows", 80, TextAlignment::Right);
        default:     return ListColumnDef("", 80);
    }
}

void SearchHitModel::Append(const std::vector<SearchHit>& hits) {
    if (hits.empty()) return;
    hits_.insert(hits_.end(), hits.begin(), hits.end());
    NotifyDataChanged();
}

void SearchHitModel::Clear() {
    hits_.clear();
    NotifyDataChanged();
}

const SearchHit* SearchHitModel::At(int row) const {
    if (row < 0 || row >= GetRowCount()) return nullptr;
    return &hits_[static_cast<size_t>(row)];
}

} // namespace UltraDBAdmin
