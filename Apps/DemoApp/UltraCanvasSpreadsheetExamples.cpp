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
#include "UltraCanvasFileLoader.h"

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

        // ===== TOOLBAR: Open button + status label =====
        auto openBtn = std::make_shared<UltraCanvasButton>("ssOpenBtn", 20, 50, 200, 30,
                                                           "Open Spreadsheet File…");
        root->AddChild(openBtn);

        auto status = std::make_shared<UltraCanvasLabel>("ssStatus", 235, 56, 760, 22);
        status->SetText("Loaded: (sample data) — supported formats: .ods, .csv");
        status->SetFontSize(12);
        status->SetTextColor(Color(90, 90, 90, 255));
        root->AddChild(status);

        // ===== SPREADSHEET ELEMENT =====
        auto sheet = CreateSpreadsheetElement("ssDemoGrid", 20, 92, kWidth - 40, kHeight - 104);
        SeedSampleData(sheet.get());
        root->AddChild(sheet);

        // ===== OPEN -> FileLoader -> LoadFromFile =====
        // Capture shared_ptrs so the grid/label stay valid for the async callback;
        // they are also owned by the container, so there is no ownership cycle
        // (the button does not own the container).
        openBtn->onClick = [sheet, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Open Spreadsheet File")
                .AddFilter("Spreadsheet files", std::vector<std::string>{ "ods", "csv", "tsv" })
                .AddFilter("OpenDocument Spreadsheet", "ods")
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
                        status->SetText("Failed to load: " + path +
                                        "  (only .ods and .csv/.tsv are supported)");
                    }
                    sheet->RequestRedraw();
                    status->RequestRedraw();
                });
        };

        return root;
    }

} // namespace UltraCanvas
