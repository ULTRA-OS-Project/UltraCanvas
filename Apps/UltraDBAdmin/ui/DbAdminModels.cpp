// Apps/UltraDBAdmin/ui/DbAdminModels.cpp
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

// A column as wide as its widest text, within reason; the user drags it
// wider. An estimate from the character count: measuring every cell would
// need a render context, and the first rows say enough.
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

int ResultSetModel::GetRowCount() const { return static_cast<int>(rows_.Size()); }
int ResultSetModel::GetColumnCount() const { return static_cast<int>(columns_.size()); }

ListDataValue ResultSetModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount() || index.column < 0 || index.column >= GetColumnCount())
        return {};
    const UltraDbRow& row = rows_.Row(static_cast<size_t>(index.row));
    if (static_cast<size_t>(index.column) >= row.Size()) return {};
    const UltraDbValue& value = row[static_cast<size_t>(index.column)];
    switch (role) {
        case ListDataRole::DisplayRole:
            return CellText(value);
        case ListDataRole::ToolTipRole:
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
    const int width = static_cast<size_t>(column) < widths_.size() ? widths_[static_cast<size_t>(column)]
                                                                     : TextWidth(Utf8Length(title));
    return ListColumnDef(title, width, TextAlignment::Left, tip);
}

void ResultSetModel::Replace(UltraDbResultSet rows, std::vector<std::string> columnTypes) {
    rows_ = std::move(rows);
    columns_ = rows_.Columns();
    columnTypes_ = std::move(columnTypes);
    // Widths from the header and the first rows; a cell is cut at 60
    // characters for this, so one long text does not take the whole grid.
    constexpr size_t kSampleRows = 200;
    constexpr size_t kCellChars = 60;
    widths_.assign(columns_.size(), 0);
    for (size_t c = 0; c < columns_.size(); ++c) {
        size_t widest = Utf8Length(columns_[c]) + 2;   // room for the sort mark
        for (size_t r = 0; r < rows_.Size() && r < kSampleRows; ++r) {
            const UltraDbRow& row = rows_.Row(r);
            if (c < row.Size()) widest = std::max(widest, Utf8Length(CellText(row[c], kCellChars)));
        }
        widths_[c] = TextWidth(widest);
    }
    NotifyDataChanged();
}

// ===== ColumnSchemaModel =====

int ColumnSchemaModel::GetRowCount() const { return static_cast<int>(columns_.size()); }

ListDataValue ColumnSchemaModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount()) return {};
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

void ConstraintModel::Replace(const UltraDbTableSchema& table) {
    entries_.clear();
    bool keyListed = false;
    for (const auto& index : table.indexes) {
        Entry e;
        e.kind = index.primary ? "PRIMARY KEY" : index.unique ? "UNIQUE" : "INDEX";
        e.name = index.name;
        e.columns = Join(index.columns);
        keyListed = keyListed || index.primary;
        entries_.push_back(std::move(e));
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

} // namespace UltraDBAdmin
