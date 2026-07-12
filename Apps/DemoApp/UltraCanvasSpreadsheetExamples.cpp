// Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp
// Demonstrates UltraCanvasSpreadsheet: an editable grid with an "Open" button
// that loads spreadsheet files (.ods / .csv) through UltraCanvasFileLoader.
// Version: 1.0.0
// Last Modified: 2026-05-30
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasCSVImportDialog.h"
#include "UltraCanvasCSVExportDialog.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include <algorithm>
#include <string>

namespace UltraCanvas {

    namespace {
        // Seed the grid with a little sample data so the element is meaningful
        // before the user loads a file of their own. It also exercises the
        // formatting features: per-column widths, a taller header row, currency
        // and percentage number formats, and a SUM whose covered area is
        // highlighted when the totals cell is selected.
        void SeedSampleData(UltraCanvasSpreadsheet* sheet) {
            if (!sheet) return;

            // Currency formatted like the European samples: "28,869.80 €".
            const NumberFormat euro = NumberFormat::Currency("€", 2, /*symbolAfter*/true);
            const NumberFormat pct  = NumberFormat::Percentage(2);

            // --- Column widths and a taller, wrapped header row ---
            const int widths[] = { 120, 90, 130, 110, 90 };
            for (int c = 0; c < 5; ++c) sheet->SetColumnWidth(c, widths[c]);
            sheet->SetRowHeight(0, 28);

            const char* headers[] = { "Month", "Refunds", "Sales", "Chargebacks", "CBK %" };
            for (int c = 0; c < 5; ++c) {
                sheet->SetCellValue(0, c, std::string(headers[c]));
                sheet->GetCell(0, c)->SetBold(true);
            }

            struct Row { const char* month; int refunds; double sales; double chargebacks; double cbkPct; };
            const Row rows[] = {
                { "06.2016", 22, 28869.80, 278.80, 0.0097 },
                { "07.2016", 21, 32788.00, 327.00, 0.0100 },
                { "08.2016", 42, 30913.90, 444.80, 0.0144 },
                { "09.2016", 27, 30877.25, 855.80, 0.0277 },
                { "10.2016", 36, 27515.40,  25.90, 0.0009 },
                { "11.2016", 27, 26412.90, 304.00, 0.0115 },
                { "12.2016",  8, 22282.30, 1035.0, 0.0464 },
            };

            int r = 1;
            for (const auto& row : rows) {
                sheet->SetCellValue(r, 0, std::string(row.month));
                sheet->GetCell(r, 1)->SetNumber(row.refunds);
                sheet->GetCell(r, 2)->SetNumber(row.sales);
                sheet->GetCell(r, 2)->SetNumberFormat(euro);
                sheet->GetCell(r, 3)->SetNumber(row.chargebacks);
                sheet->GetCell(r, 3)->SetNumberFormat(euro);
                sheet->GetCell(r, 4)->SetNumber(row.cbkPct);
                sheet->GetCell(r, 4)->SetNumberFormat(pct);
                ++r;
            }

            // Totals row: SUM formulas. Selecting one of these cells highlights
            // the range of cells it covers (e.g. C2:C8 for Sales).
            const int totalRow = r + 1;  // leave a blank spacer row
            sheet->SetCellValue(totalRow, 0, std::string("07 to 12/2016"));
            sheet->GetCell(totalRow, 0)->SetBold(true);

            sheet->SetCellFormula(totalRow, 1, "=SUM(B2:B8)");
            sheet->SetCellFormula(totalRow, 2, "=SUM(C2:C8)");
            sheet->GetCell(totalRow, 2)->SetNumberFormat(euro);
            sheet->SetCellFormula(totalRow, 3, "=SUM(D2:D8)");
            sheet->GetCell(totalRow, 3)->SetNumberFormat(euro);
            // Overall CBK %: chargebacks total over sales total.
            sheet->SetCellFormula(totalRow, 4, "=SUM(D2:D8)/SUM(C2:C8)");
            sheet->GetCell(totalRow, 4)->SetNumberFormat(pct);

            sheet->Recalculate();
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSpreadsheetExamples() {
        const int kWidth  = 1020;
        const int kHeight = 720;

        auto root = std::make_shared<UltraCanvasContainer>("SpreadsheetExamples", 0, 0, kWidth, kHeight);
        root->SetBackgroundColor(Colors::White);

        // ===== TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("ssTitle", 20, 12, 900, 28);
        title->SetText("UltraCanvas Spreadsheet");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        root->AddChild(title);

        // ===== TOOLBAR: Open / Import / Save buttons + status label =====
        auto openBtn = std::make_shared<UltraCanvasButton>("ssOpenBtn", 20, 50, 190, 30,
                                                           "Open Spreadsheet File…");
        root->AddChild(openBtn);

        auto importBtn = std::make_shared<UltraCanvasButton>("ssImportBtn", 218, 50, 150, 30,
                                                             "Import CSV…");
        root->AddChild(importBtn);

        auto saveBtn = std::make_shared<UltraCanvasButton>("ssSaveBtn", 376, 50, 110, 30,
                                                           "Save…");
        root->AddChild(saveBtn);

        auto status = std::make_shared<UltraCanvasLabel>("ssStatus", 496, 56, 510, 22);
        status->SetFontSize(12);
        status->SetTextColor(Color(90, 90, 90, 255));
        root->AddChild(status);

        // ===== SPREADSHEET ELEMENT =====
        auto sheet = CreateSpreadsheetElement("ssDemoGrid", 20, 92, kWidth - 40, kHeight - 104);

        // Load the bundled OpenDocument Spreadsheet demo file on entry so the
        // element opens on a real .ods document (monthly sales / chargeback
        // report with live SUM totals). If it cannot be found, fall back to the
        // programmatic sample data so the demo is still meaningful.
        const std::string samplePath =
            NormalizePath(GetResourcesDir() + "media/docs/spreadsheet.ods");
        if (sheet->LoadFromFile(samplePath)) {
            status->SetText("Loaded: " + samplePath
                            + " — supported formats: .ods, .xlsx, .csv");
        } else {
            SeedSampleData(sheet.get());
            status->SetText("Loaded: (sample data) — supported formats: .ods, .xlsx, .csv");
        }
        root->AddChild(sheet);

        // ===== OPEN -> FileLoader -> LoadFromFile =====
        // Capture shared_ptrs so the grid/label stay valid for the async callback;
        // they are also owned by the container, so there is no ownership cycle
        // (the button does not own the container).
        openBtn->onClick = [sheet, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Open Spreadsheet File")
                .AddFilter("Spreadsheet files", std::vector<std::string>{ "ods", "xlsx", "csv", "tsv" })
                .AddFilter("OpenDocument Spreadsheet", "ods")
                .AddFilter("Excel Workbook", "xlsx")
                .AddFilter("CSV / TSV", std::vector<std::string>{ "csv", "tsv" })
                .AddFilter("All files", "*");

            UltraCanvasFileLoader::OpenFileDialog(opts,
                [sheet, status](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Open cancelled.");
                        status->RequestRedraw();
                        return;
                    }
                    if (sheet->LoadFromFile(path)) {
                        status->SetText("Loaded: " + path);
                    } else {
                        status->SetText("Could not open file: " + sheet->GetLastError());
                    }
                    sheet->RequestRedraw();
                    status->RequestRedraw();
                });
        };

        // ===== IMPORT CSV -> file dialog -> manual "Text Import" options dialog =====
        // Pick a CSV/TSV file, then open the options dialog (charset, separators,
        // start row, number recognition) with a live preview. On accept the grid
        // is loaded with exactly the chosen settings.
        importBtn->onClick = [sheet, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Select CSV / TSV File")
                .AddFilter("CSV / TSV", std::vector<std::string>{ "csv", "tsv", "txt" })
                .AddFilter("All files", "*");

            UltraCanvasFileLoader::OpenFileDialog(opts,
                [sheet, status](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Import cancelled.");
                        status->RequestRedraw();
                        return;
                    }
                    // Surface a clear reason (locked / no permission / missing)
                    // instead of opening the dialog onto an empty preview.
                    std::string openError = CSVDescribeOpenError(path);
                    if (!openError.empty()) {
                        status->SetText("Could not open file: " + openError);
                        status->RequestRedraw();
                        return;
                    }
                    ShowCSVImportDialog(path,
                        [sheet, status, path](const CSVImportOptions& options) {
                            if (sheet->LoadCSVWithOptions(path, options)) {
                                status->SetText("Imported: " + path);
                            } else {
                                status->SetText("Could not import file: " + sheet->GetLastError());
                            }
                            sheet->RequestRedraw();
                            status->RequestRedraw();
                        });
                });
        };

        // ===== SAVE -> file dialog (all supported formats) =====
        // Offer every format the engine can write: OpenDocument (.ods), Excel
        // (.xlsx) and CSV / TSV. When the chosen name ends in .csv or .tsv we open the CSV
        // export options dialog (separator, quoting, charset, line ending) so the
        // text file is written exactly as the user wants; .ods saves directly.
        saveBtn->onClick = [sheet, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Save Spreadsheet As")
                .SetDefaultFileName("spreadsheet.ods")
                .AddFilter("OpenDocument Spreadsheet", "ods")
                .AddFilter("Excel Workbook", "xlsx")
                .AddFilter("CSV (comma separated)", "csv")
                .AddFilter("TSV (tab separated)", "tsv")
                .AddFilter("All files", "*");

            UltraCanvasFileLoader::SaveFileDialog(opts,
                [sheet, status](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Save cancelled.");
                        status->RequestRedraw();
                        return;
                    }

                    // Pick the format from the extension; default to .ods when none.
                    std::string ext;
                    size_t dot = path.find_last_of('.');
                    size_t sep = path.find_last_of("/\\");
                    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
                        ext = path.substr(dot);
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    }

                    if (ext == ".csv" || ext == ".tsv") {
                        // Seed the dialog with sensible defaults for the format.
                        CSVExportOptions initial;
                        if (ext == ".tsv") initial.fieldSeparator = '\t';

                        ShowCSVExportDialog(sheet.get(), initial,
                            [sheet, status, path](const CSVExportOptions& options) {
                                if (sheet->SaveCSVWithOptions(path, options)) {
                                    status->SetText("Saved: " + path);
                                } else {
                                    status->SetText("Could not save file: " + sheet->GetLastError());
                                }
                                status->RequestRedraw();
                            });
                        return;
                    }

                    // ODS (or no extension -> default to .ods).
                    std::string savePath = ext.empty() ? path + ".ods" : path;
                    if (sheet->SaveToFile(savePath)) {
                        status->SetText("Saved: " + savePath);
                    } else {
                        status->SetText("Could not save file: " + sheet->GetLastError());
                    }
                    status->RequestRedraw();
                });
        };

        return root;
    }

} // namespace UltraCanvas
