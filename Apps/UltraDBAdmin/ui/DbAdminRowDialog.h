// Apps/UltraDBAdmin/ui/DbAdminRowDialog.h
// The row form: one line per column - its name and type, a field for its
// value (a choice of true / false for a boolean, the pattern to follow for a
// date or a number), and what the column is set to: the value, NULL, its
// default (a new row), or one of the database's functions for its kind
// (CURRENT_TIMESTAMP, gen_random_uuid() - DbAdminValueTypes.h) - to edit a
// row of Browse or add one. A value the column cannot take ("12,5" for a
// number) is said in the form, before Commit. What it returns is staged
// (EditBuffer), not written: Commit does that.
//
// A column the database computes is shown and cannot be set; a blob column
// is shown by its size and is not edited here. Values are typed as text and
// converted by the database (see DbAdminEditBuffer.h). The form stays open on
// a mistake: Save asks the caller, and closes only when it reports no error.
// Version: 0.3.0 - a staged new row opened again
// Version: 0.2.0 - a field per type; NULL / Default / a value function per column
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEditBuffer.h"
#include "DbAdminStructureEdit.h"   // SqlEngine

#include <UltraDatabase/UltraDatabase.h>

#include <functional>
#include <map>
#include <string>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraDBAdmin {

// The columns that changed (editing) or were given (a new row). Returns an
// error to show, or "" to close.
using SaveRowFn = std::function<std::string(const std::map<std::string, CellValue>& values)>;

// `row` null: a new row. `pending` (may be null) gives the values already
// staged for the row, which the form starts from; `stagedNew`, for a new row
// staged earlier and opened again, the values it was staged with (a column
// it leaves out is at its default). Non-blocking.
void ShowRowDialog(UltraCanvas::UltraCanvasWindowBase* parent, const UltraDbTableSchema& table, SqlEngine engine,
                   const UltraDbRow* row, const EditBuffer* pending, SaveRowFn onSave,
                   const std::map<std::string, CellValue>* stagedNew = nullptr);

// SQL to read, not run: `intro` above a read-only, highlighted SQL view of
// `sql`, and an OK button. The Commit preview.
void ShowSqlPreview(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& title,
                    const std::string& intro, const std::string& sql);

} // namespace UltraDBAdmin
