// Apps/UltraDBAdmin/ui/DbAdminModels.h
// The list models the window shows, all plain IListModels over what
// UltraDatabase returns, so UltraCanvasListView draws them:
//   ResultSetModel    - the rows of a result set: a Browse page, or the rows
//                       of one statement in the SQL console;
//   ColumnSchemaModel - a table's columns, for Structure;
//   ConstraintModel   - its key, unique constraints, indexes, foreign keys
//                       and triggers, one row each, for Structure;
//   StatementModel    - what each statement of a console script did.
// A cell is CellText (engine/DbAdminFormat.h): NULL is not an empty string, a
// blob says how big it is. Its tooltip is the longer text.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEditBuffer.h"

#include <UltraDatabase/UltraDatabase.h>

#include "UltraCanvasListModel.h"

#include <string>
#include <vector>

namespace UltraDBAdmin {

class ResultSetModel : public UltraCanvas::IListModel {
public:
    int GetRowCount() const override;
    int GetColumnCount() const override;
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    // Replaces the rows (and the columns: a new result can have others).
    // `columnTypes`, when given, are the declared types the header tooltips
    // name - a Browse page knows them from the table's schema.
    void Replace(UltraDbResultSet rows, std::vector<std::string> columnTypes = {});
    void Clear() { Replace(UltraDbResultSet()); }
    const UltraDbResultSet& Rows() const { return rows_; }

    // Browse's staged edits, shown over the rows: a changed cell reads its
    // new value followed by " *", a row to be deleted starts with "DEL ".
    // Null: none. The buffer belongs to the window; call
    // Refresh() after it changes.
    void SetPending(const EditBuffer* pending) { pending_ = pending; }
    void Refresh() { NotifyDataChanged(); }

private:
    const EditBuffer* pending_ = nullptr;
    UltraDbResultSet rows_;
    std::vector<std::string> columns_;
    std::vector<std::string> columnTypes_;
    std::vector<int> widths_;            // from the header and the rows' text
};

class ColumnSchemaModel : public UltraCanvas::IListModel {
public:
    enum Column { Name = 0, Type, Null, Default, Key, Extra, Collation, Comment, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override { return ColumnCount; }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void Replace(const UltraDbTableSchema& table);

private:
    std::vector<UltraDbColumnSchema> columns_;
    std::vector<std::string> primaryKey_;
};

class ConstraintModel : public UltraCanvas::IListModel {
public:
    enum Column { Kind = 0, Name, Columns, Details, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override { return ColumnCount; }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void Replace(const UltraDbTableSchema& table);

private:
    struct Entry {
        std::string kind, name, columns, details, tooltip;
    };
    std::vector<Entry> entries_;
};

class StatementModel : public UltraCanvas::IListModel {
public:
    enum Column { Number = 0, Statement, Outcome, Time, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override { return ColumnCount; }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void Replace(std::vector<UltraDbStatementResult> statements);
    const UltraDbStatementResult* At(int row) const;

private:
    std::vector<UltraDbStatementResult> statements_;
};

// What one statement did, in a few words: "12 rows", "3 rows affected",
// "error: no such table: x", "cancelled".
std::string OutcomeText(const UltraDbStatementResult& statement);

} // namespace UltraDBAdmin
