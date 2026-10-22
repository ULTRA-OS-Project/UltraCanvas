// Apps/UltraDBAdmin/ui/DbAdminStructureDialog.h
// The forms Structure stages its edits with:
//   the column form - name, type (typed, or picked from the engine's common
//                     types), NOT NULL, and a default, which is SQL: 0,
//                     'text', CURRENT_TIMESTAMP, now();
//   the index form  - a name, UNIQUE, and the columns, in the order they are
//                     ticked, which is the key's order.
// What they return is staged (StructureEdit), not written: Apply does that.
// Each stays open on a mistake: Save asks the caller, and closes only when it
// reports no error.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminStructureEdit.h"

#include <functional>
#include <string>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraDBAdmin {

// Returns an error to show, or "" to close.
using SaveColumnFn = std::function<std::string(const ColumnDraft& draft)>;
using SaveIndexFn = std::function<std::string(const IndexDraft& draft)>;

// `at` is the column's place in edit.Columns(), or -1 for a new column.
// Non-blocking.
void ShowColumnDialog(UltraCanvas::UltraCanvasWindowBase* parent, const StructureEdit& edit, int at,
                      SaveColumnFn onSave);

// A new index on the edit's columns as they are to be; `firstColumn` (may be
// "") is ticked to begin with.
void ShowIndexDialog(UltraCanvas::UltraCanvasWindowBase* parent, const StructureEdit& edit,
                     const std::string& firstColumn, SaveIndexFn onSave);

} // namespace UltraDBAdmin
