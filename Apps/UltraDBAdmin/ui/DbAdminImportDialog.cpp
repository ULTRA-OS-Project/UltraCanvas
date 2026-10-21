// Apps/UltraDBAdmin/ui/DbAdminImportDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminImportDialog.h"

#include "DbAdminModels.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

constexpr float kRowHeight = 28.0f;
constexpr size_t kPreviewRows = 50;
const Color kErrorColor(176, 32, 32);

std::shared_ptr<UltraCanvasModalDialog> MakeDialog(const std::string& title, int width, int height) {
    DialogConfig config;
    config.title      = title;
    config.width      = width;
    config.height     = height;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    config.position   = DialogPosition::CenterParent;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return nullptr;
    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);
    return dialog;
}

std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id, const std::string& text, float height = 24.0f) {
    auto label = CreateLabel(id, 0, 0, 0, height, text);
    label->SetWrap(TextWrap::WrapWord);
    label->layoutItem.SetFlexShrink(0);
    return label;
}

// Cancel and the primary button, right-aligned; Cancel closes the dialog.
UltraCanvasButton* AddButtons(UltraCanvasModalDialog* dialog, const std::string& id, const std::string& primary,
                              std::function<void()> onPrimary) {
    auto buttons = CreateContainer(id, 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    buttons->AddStretchSpacer(1);
    auto cancel = CreateButton(id + "-cancel", 0, 0, 90, kRowHeight, "Cancel");
    cancel->SetOnClick([dialog]() { dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    UltraCanvasButton* raw = nullptr;
    if (onPrimary) {
        auto run = CreateButton(id + "-run", 0, 0, 150, kRowHeight, primary);
        run->SetStyle(ButtonStyles::PrimaryStyle());
        run->SetOnClick(std::move(onPrimary));
        raw = run.get();
        buttons->AddChild(run);
    }
    dialog->AddChild(buttons);
    return raw;
}

// "4 512" for a count read at a glance.
std::string Count(size_t n, const char* one, const char* many) {
    std::string digits = std::to_string(n), grouped;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) grouped += " ";   // thin space
        grouped += digits[i];
    }
    return grouped + " " + (n == 1 ? one : many);
}

// ----- the row import's state -----

struct RowForm {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasLabel* intro = nullptr;
    UltraCanvasLabel* error = nullptr;
    UltraCanvasCheckbox* header = nullptr;
    UltraCanvasCheckbox* emptyIsNull = nullptr;
    UltraCanvasButton* import = nullptr;
    std::vector<UltraCanvasDropdown*> sources;      // per table column; null for a computed one
    std::shared_ptr<ResultSetModel> preview;
    UltraDbTableSchema table;
    std::vector<ImportGrid> grids;
    size_t grid = 0;
    ImportMapping mapping;
    bool production = false;
    bool filling = false;          // the form is being filled: no change events
    ImportRowsFn onImport;
};

void ShowPreview(RowForm& form) {
    const ImportGrid& grid = form.grids[form.grid];
    form.preview->Replace(ImportPreview(form.table, grid, form.mapping, kPreviewRows));
    const size_t rows = DataRows(grid, form.mapping);
    form.intro->SetText(Count(rows, "row", "rows") + " to import from " + grid.name + " into " + form.table.name +
                        ". Each column of the table takes a column of the file, or is left to its default." +
                        (form.production ? " This is a production connection." : ""));
    form.import->SetText("Import " + Count(rows, "row", "rows"));
    form.error->SetText(CheckMapping(form.table, grid, form.mapping));
}

// The dropdowns from the mapping, with the file's column names as they now are.
void FillSources(RowForm& form) {
    form.filling = true;
    const std::vector<std::string> names = FileColumnNames(form.grids[form.grid], form.mapping.header);
    for (size_t t = 0; t < form.sources.size(); ++t) {
        UltraCanvasDropdown* dropdown = form.sources[t];
        if (!dropdown) continue;
        dropdown->ClearItems();
        dropdown->AddItem("(not imported - its default)");
        for (const auto& name : names) dropdown->AddItem(name);
        dropdown->SetSelectedIndex(form.mapping.source[t] + 1, false);
    }
    form.filling = false;
    ShowPreview(form);
}

void Remap(RowForm& form) {
    form.mapping = AutoMapping(form.table, form.grids[form.grid], form.mapping.header);
    form.mapping.emptyIsNull = form.emptyIsNull->IsChecked();
    FillSources(form);
}

void Import(RowForm& form) {
    const std::string problem = CheckMapping(form.table, form.grids[form.grid], form.mapping);
    if (!problem.empty()) { form.error->SetText(problem); return; }
    const std::string error = form.onImport ? form.onImport(form.grids[form.grid], form.mapping) : std::string();
    if (!error.empty()) { form.error->SetText(error); return; }
    form.dialog->CloseDialog(DialogResult::OK);
}

std::string Two(int n) {
    char text[8];
    std::snprintf(text, sizeof text, "%02d", n);
    return text;
}

// One cell of a workbook as the import reads it.
ImportCell WorkbookCell(const SpreadsheetCell* cell) {
    if (!cell) return ImportCell();
    switch (cell->GetValueType()) {
        case CellValueType::Empty:
            return ImportCell();
        case CellValueType::Number:
        case CellValueType::Currency:
        case CellValueType::Percentage:
            return ImportCell::Number(FormatFloatClassic(cell->GetNumber(), 15));
        case CellValueType::Boolean: {
            ImportCell value = ImportCell::Text(cell->GetBoolean() ? "true" : "false");
            value.number = cell->GetBoolean() ? "1" : "0";
            return value;
        }
        case CellValueType::Date:
        case CellValueType::Time:
        case CellValueType::DateTime: {
            const DateTimeValue when = cell->GetDateTime();
            int y = 0, m = 0, d = 0, h = 0, mi = 0, s = 0;
            when.ToDate(y, m, d);
            when.ToTime(h, mi, s);
            const std::string date = std::to_string(y) + "-" + Two(m) + "-" + Two(d);
            const std::string time = Two(h) + ":" + Two(mi) + ":" + Two(s);
            const CellValueType type = cell->GetValueType();
            return ImportCell::Text(type == CellValueType::Date ? date
                                    : type == CellValueType::Time ? time
                                                                  : date + " " + time);
        }
        default: {
            // Text, a formula's result, an error value: as the cell shows it,
            // and its number where it has one.
            ImportCell value = ImportCell::Text(cell->GetDisplayValue());
            if (cell->GetValueType() == CellValueType::Formula) {
                if (const auto number = cell->TryGetNumber()) value.number = FormatFloatClassic(*number, 15);
            }
            return value;
        }
    }
}

} // namespace

void ShowSqlImportDialog(UltraCanvasWindowBase* parent, const SqlImportInfo& info,
                         std::function<void(bool oneTransaction)> onRun) {
    auto dialog = MakeDialog("Import SQL into " + info.connectionName, 560, 330);
    if (!dialog) return;
    UltraCanvasModalDialog* raw = dialog.get();
    const ScriptSummary& summary = info.summary;
    const bool runnable = summary.unsupported.empty() && summary.statements > 0;

    dialog->AddChild(MakeText("dbsFile", info.fileName + " - " + info.sizeText +
                                             (runnable ? ", " + Count(summary.statements, "statement", "statements")
                                                       : std::string()) + "."));
    if (!summary.unsupported.empty()) {
        auto why = MakeText("dbsWhy", "It cannot run here: " + summary.unsupported + ".", 60);
        why->SetTextColor(kErrorColor);
        dialog->AddChild(why);
    } else if (summary.statements == 0) {
        dialog->AddChild(MakeText("dbsWhy", "It holds no statements."));
    }

    UltraCanvasCheckbox* rawAtomic = nullptr;
    if (runnable) {
        const std::string risky = RiskyText(summary);
        if (!risky.empty()) {
            auto warning = MakeText("dbsRisky", "It changes or removes what is there: " + risky + ".");
            warning->SetTextColor(kErrorColor);
            dialog->AddChild(warning);
        }
        if (info.production) {
            auto warning = MakeText("dbsProduction", info.connectionName + " is a production connection.");
            warning->SetTextColor(kErrorColor);
            dialog->AddChild(warning);
        }
        auto atomic = UltraCanvasCheckbox::CreateCheckbox(
            "dbsAtomic", 0, 0, 0, 24, "All or nothing: run it as one transaction", !summary.ownTransactions);
        atomic->layoutItem.SetFlexShrink(0);
        atomic->SetDisabled(summary.ownTransactions);
        rawAtomic = atomic.get();
        dialog->AddChild(atomic);
        dialog->AddChild(MakeText(
            "dbsAtomicNote",
            summary.ownTransactions
                ? "The file begins and commits its transactions itself, so it runs as written: what it commits "
                  "before a failure stays."
                : "If a statement fails, or you press Stop, nothing of the file stays. Without it, what ran "
                  "before the failure stays.",
            44));
    }
    dialog->AddStretchSpacer(1);
    std::function<void()> run;
    if (runnable) {
        run = [raw, rawAtomic, onRun = std::move(onRun)]() {
            const bool atomic = rawAtomic && rawAtomic->IsChecked() && !rawAtomic->IsDisabled();
            raw->CloseDialog(DialogResult::OK);
            if (onRun) onRun(atomic);
        };
    }
    AddButtons(raw, "dbsButtons", "Run", std::move(run));
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowRowImportDialog(UltraCanvasWindowBase* parent, const UltraDbTableSchema& table,
                         std::vector<ImportGrid> grids, bool production, ImportRowsFn onImport) {
    if (grids.empty()) return;
    auto form = std::make_shared<RowForm>();
    form->table = table;
    form->grids = std::move(grids);
    form->production = production;
    form->onImport = std::move(onImport);
    form->mapping.header = GuessHeader(table, form->grids[0]);

    auto dialog = MakeDialog("Import rows into " + table.name, 780, 620);
    if (!dialog) return;
    form->dialog = dialog.get();

    auto intro = MakeText("dbiIntro", "", 40);
    form->intro = intro.get();
    if (production) intro->SetTextColor(kErrorColor);
    dialog->AddChild(intro);

    auto options = CreateContainer("dbiOptions", 0, 0, 0, 28);
    options->layout.SetFlexRow().SetFlexGap(14).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    options->layoutItem.SetFlexShrink(0);
    if (form->grids.size() > 1) {
        auto sheet = CreateDropdown("dbiSheet", 0, 0, 200, kRowHeight);
        sheet->layoutItem.SetFlexShrink(0);
        for (const auto& grid : form->grids) sheet->AddItem(grid.name);
        sheet->SetSelectedIndex(0, false);
        sheet->SetTooltip("The sheet to import");
        sheet->onSelectionChanged = [form](int index, const DropdownItem&) {
            if (form->filling || index < 0 || static_cast<size_t>(index) >= form->grids.size()) return;
            form->grid = static_cast<size_t>(index);
            form->mapping.header = GuessHeader(form->table, form->grids[form->grid]);
            form->header->SetChecked(form->mapping.header);
            Remap(*form);
        };
        options->AddChild(sheet);
    }
    auto header = UltraCanvasCheckbox::CreateCheckbox("dbiHeader", 0, 0, 290, 24,
                                                      "The first row names the columns", form->mapping.header);
    header->layoutItem.SetFlexShrink(0);
    form->header = header.get();
    header->onChecked   = [form]() { if (!form->filling) { form->mapping.header = true; Remap(*form); } };
    header->onUnchecked = [form]() { if (!form->filling) { form->mapping.header = false; Remap(*form); } };
    options->AddChild(header);
    auto empty = UltraCanvasCheckbox::CreateCheckbox("dbiEmpty", 0, 0, 230, 24, "An empty cell is NULL", true);
    empty->layoutItem.SetFlexShrink(0);
    empty->SetTooltip("Else it is empty text.");
    form->emptyIsNull = empty.get();
    empty->onChecked   = [form]() { form->mapping.emptyIsNull = true; ShowPreview(*form); };
    empty->onUnchecked = [form]() { form->mapping.emptyIsNull = false; ShowPreview(*form); };
    options->AddChild(empty);
    dialog->AddChild(options);

    // One line per column of the table; they scroll when there are many.
    auto columns = CreateContainer("dbiColumns", 0, 0, 0, 0);
    columns->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    columns->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    auto grid = CreateFormGrid("dbiForm", 6.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);
    int index = 0;
    for (size_t t = 0; t < table.columns.size(); ++t) {
        const UltraDbColumnSchema& column = table.columns[t];
        const std::string id = "dbiCol" + std::to_string(index++);
        auto caption = CreateFormCaption(id + "-caption", column.name);
        std::string type = column.declaredType.empty() ? std::string("any type") : column.declaredType;
        if (!column.nullable && !column.hasDefault && !column.autoIncrement) type += ", must have a value";
        caption->SetTooltip(type);
        auto source = CreateDropdown(id + "-source", 0, 0, 0, kRowHeight);
        if (column.generated) {
            source->AddItem("(computed by the database)");
            source->SetSelectedIndex(0, false);
            source->SetDisabled(true);
            form->sources.push_back(nullptr);
        } else {
            source->SetTooltip(type);
            source->onSelectionChanged = [form, t](int selected, const DropdownItem&) {
                if (form->filling || t >= form->mapping.source.size()) return;
                form->mapping.source[t] = selected - 1;
                ShowPreview(*form);
            };
            form->sources.push_back(source.get());
        }
        AddFormRow(grid, caption, source);
    }
    columns->AddChild(grid);
    dialog->AddChild(columns);

    dialog->AddChild(MakeText("dbiPreviewCaption", "The first rows, as they will be inserted:"));
    form->preview = std::make_shared<ResultSetModel>();
    auto preview = std::make_shared<UltraCanvasListView>("dbiPreview", -1, -1, 400, 160);
    preview->SetModel(form->preview);
    preview->SetShowHeader(true);
    preview->SetShowItemTooltips(true);
    preview->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1)
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    dialog->AddChild(preview);

    auto error = MakeText("dbiError", "", 36);
    error->SetTextColor(kErrorColor);
    form->error = error.get();
    dialog->AddChild(error);
    form->import = AddButtons(dialog.get(), "dbiButtons", "Import", [form]() { Import(*form); });

    Remap(*form);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

bool GridsFromWorkbook(const std::string& path, std::vector<ImportGrid>& out, std::string& error) {
    out.clear();
    UltraCanvasSpreadsheet book("dbaImportBook", 0, 0, 10, 10);
    if (!book.LoadFromFile(path)) {
        error = book.GetLastError().empty() ? std::string("it cannot be read") : book.GetLastError();
        return false;
    }
    for (int s = 0; s < book.GetSheetCount(); ++s) {
        const SpreadsheetSheet* sheet = book.GetSheet(s);
        if (!sheet || sheet->GetUsedRowCount() == 0) continue;
        const CellRange used = sheet->GetUsedRange();
        ImportGrid grid;
        grid.name = sheet->GetName();
        for (int r = used.start.row; r <= used.end.row; ++r) {
            std::vector<ImportCell> row;
            bool any = false;
            for (int c = 0; c <= used.end.col; ++c) {
                row.push_back(WorkbookCell(sheet->GetCellIfExists(r, c)));
                any = any || !row.back().empty;
            }
            if (any) grid.AddRow(std::move(row), static_cast<size_t>(r) + 1);
        }
        if (!grid.rows.empty()) out.push_back(std::move(grid));
    }
    if (out.empty()) { error = "it holds no rows"; return false; }
    return true;
}

} // namespace UltraDBAdmin
