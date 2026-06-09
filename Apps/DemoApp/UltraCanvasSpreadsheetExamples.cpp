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
        // before the user loads a file of their own.
        void SeedSampleData(UltraCanvasSpreadsheet* sheet) {
            if (!sheet) return;

            const char* headers[] = { "Class", "Assignment", "Status", "Grade", "Weight" };
            for (int c = 0; c < 5; ++c) {
                sheet->SetCellValue(0, c, std::string(headers[c]));
                sheet->GetCell(0, c)->SetBold(true);
            }

            struct Row { const char* cls; const char* asg; const char* status; const char* grade; double weight; };
            const Row rows[] = {
                { "Calculus 1",        "Module 03 content",  "In Progress", "A",  0.10 },
                { "Quantum Mechanics", "Final project",      "Not Started", "B+", 0.25 },
                { "Algebra",           "Midterm Exam",       "In Progress", "C",  0.15 },
                { "History + Theory",  "Presentation",       "On Hold",     "A+", 0.10 },
                { "Fluid Dynamics",    "Module 02 content",  "In Progress", "B",  0.50 },
            };

            int r = 1;
            for (const auto& row : rows) {
                sheet->SetCellValue(r, 0, std::string(row.cls));
                sheet->SetCellValue(r, 1, std::string(row.asg));
                sheet->SetCellValue(r, 2, std::string(row.status));
                sheet->SetCellValue(r, 3, std::string(row.grade));
                // Percentage weight (0.10 -> 10%)
                sheet->GetCell(r, 4)->SetPercentage(row.weight);
                ++r;
            }

            // A simple formula cell demonstrating the engine.
            sheet->SetCellValue(7, 0, std::string("Total weight"));
            sheet->GetCell(7, 0)->SetBold(true);
            sheet->SetCellFormula(7, 4, "=SUM(E2:E6)");

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
