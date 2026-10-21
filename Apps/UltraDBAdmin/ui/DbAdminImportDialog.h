// Apps/UltraDBAdmin/ui/DbAdminImportDialog.h
// The two import forms, and the workbook reader and writer the table import
// and export need:
//   the SQL import - a file's name, size and statements, what of it changes
//                    or removes data (DROP, DELETE...), and "all or nothing",
//                    before anything runs; a file only psql can run says so
//                    and offers no Run;
//   the row import - which column of the file (or sheet) each column of the
//                    table takes, whether the first row names them, whether
//                    an empty cell is NULL, and the first rows as they will
//                    be inserted.
// Both are non-blocking. The row import stays open on a mistake: Import asks
// the caller, and closes only when it reports no error.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminImport.h"
#include "DbAdminSqlScript.h"

#include <UltraDatabase/UltraDatabase.h>

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraDBAdmin {

struct SqlImportInfo {
    std::string fileName;
    std::string sizeText;          // "12.3 KB", "1.2 MB, unpacked from GZIP"
    ScriptSummary summary;
    std::string connectionName;
    bool production = false;
};

// `onRun` gets "all or nothing" as ticked.
void ShowSqlImportDialog(UltraCanvas::UltraCanvasWindowBase* parent, const SqlImportInfo& info,
                         std::function<void(bool oneTransaction)> onRun);

// Returns an error to show, or "" to close.
using ImportRowsFn = std::function<std::string(const ImportGrid& grid, const ImportMapping& mapping)>;

// `grids`: one per sheet of a workbook, one for a CSV file.
void ShowRowImportDialog(UltraCanvas::UltraCanvasWindowBase* parent, const UltraDbTableSchema& table,
                         std::vector<ImportGrid> grids, bool production, ImportRowsFn onImport);

// Every sheet of a workbook (ODS, XLSX, XLS - what the framework's
// spreadsheet reads) as grids. A number keeps its value, written with a dot;
// a date or time is ISO 8601 (2026-10-21, 2026-10-21 08:30:00, 08:30:00);
// TRUE / FALSE are 1 / 0 for a number column and true / false for text.
// On the UI thread: the reader is the spreadsheet element.
bool GridsFromWorkbook(const std::string& path, std::vector<ImportGrid>& out, std::string& error);

// The other way: rows as a workbook (ODS, XLSX - by the path's extension),
// written by the framework's spreadsheet: the column names in bold on the
// first row, numbers as numbers - also an exact numeric that comes back as
// text, by its declared type - TRUE / FALSE as such, the rest as text, never
// as a formula. On the UI thread.
bool WriteWorkbook(const std::string& path, const std::string& sheetName, const UltraDbResultSet& rows,
                   std::string& error);

} // namespace UltraDBAdmin
