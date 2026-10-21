// Apps/UltraDBAdmin/ui/DbAdminRowDialog.h
// The row form: one line per column - its name and type, a text field, a NULL
// box and, for a new row, a Default box - to edit a row of Browse or add one.
// What it returns is staged (EditBuffer), not written: Commit does that.
//
// A column the database computes is shown and cannot be set; a blob column
// is shown by its size and is not edited here. Values are typed as text and
// converted by the database (see DbAdminEditBuffer.h). The form stays open on
// a mistake: Save asks the caller, and closes only when it reports no error.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEditBuffer.h"

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
// staged for the row, which the form starts from. Non-blocking.
void ShowRowDialog(UltraCanvas::UltraCanvasWindowBase* parent, const UltraDbTableSchema& table,
                   const UltraDbRow* row, const EditBuffer* pending, SaveRowFn onSave);

// SQL to read, not run: `intro` above a read-only, highlighted SQL view of
// `sql`, and an OK button. The Commit preview.
void ShowSqlPreview(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& title,
                    const std::string& intro, const std::string& sql);

} // namespace UltraDBAdmin
