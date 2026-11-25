// Apps/UltraOffice/UltraOfficeSpreadsheetHost.cpp
// A spreadsheet tab: the framework's grid (UltraCanvasSpreadsheet) - formula
// bar, sheet tabs, its own format menu - read and written through its
// LoadFromFile / SaveToFile (.ods, .xlsx, .csv, .tsv; .xls read-only), the
// file types and password prompts through UltraCanvasFileLoader. Command
// tabs - Home, Insert, Data, Page, View - and a status line with the sum,
// average and count of the selection.
//
// Version: 0.3.0 - Insert: a chart of the selection (columns, bars, lines, areas, pies, scatter ...),
//                  its properties and Delete chart
// Version: 0.2.0 - Export as PDF and Print, through the sheet's print pages
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeToolbar.h"

#include "UltraCanvasDropdown.h"
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"   // PrintDocumentWithDialog
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasSpreadsheetPrint.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasModalDialog.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

std::string LowerExtension(const std::string& path) {
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// A status-line number: up to four decimals, trailing zeros dropped.
std::string StatusNumber(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4) << value;
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    return text;
}

class SpreadsheetHost final : public OfficeHost {
public:
    explicit SpreadsheetHost(UltraCanvasWindowBase* window) : window_(window) {
        (void)window_;
        sheet_ = std::make_shared<UltraCanvasSpreadsheet>("office-spreadsheet", 0, 0, 800, 600);
        sheet_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        // A change anywhere marks the workbook changed; moving the selection
        // only refreshes the toolbar and the status line.
        auto changed = [this]() { modified_ = true; Sync(); if (onChanged) onChanged(); };
        sheet_->onCellChange = [changed](int, int) { changed(); };
        sheet_->onCellFormatChange = [changed](int, int) { changed(); };
        sheet_->onStructureChange = changed;
        sheet_->onSheetAdd = [changed](int) { changed(); };
        sheet_->onSheetRemove = [changed](int) { changed(); };
        sheet_->onSheetRename = [changed](int, const std::string&) { changed(); };
        sheet_->onSelectionChange = [this]() { Sync(); if (onChanged) onChanged(); };
        sheet_->onChartsChange = changed;
        sheet_->onChartSelectionChange = [this](int) { Sync(); };
        BuildHome();
        BuildInsert();
        BuildData();
        BuildPage();
        BuildView();
    }

    OfficeKind GetKind() const override { return OfficeKind::Spreadsheet; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return sheet_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return tabs_; }

    void NewBlank() override { modified_ = false; Sync(); }

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        if (!sheet_->LoadFromFile(path, password)) {
            error = sheet_->GetLastError();
            if (error.empty()) error = Tr("The spreadsheet could not be read.");
            return false;
        }
        modified_ = false;
        Sync();
        return true;
    }

    bool Write(const std::string& path, const std::string& password, std::string& error) override {
        const std::string ext = LowerExtension(path);
        if (ext == "xls") {
            error = Tr("Excel 97-2003 (.xls) files are read, not written. Save as .xlsx or .ods instead.");
            return false;
        }
        const bool ok = (ext == "csv" || ext == "tsv") ? sheet_->SaveCSV(path) : sheet_->SaveToFile(path, password);
        if (!ok) {
            error = sheet_->GetLastError();
            if (error.empty()) error = Tr("The spreadsheet could not be written.");
            return false;
        }
        return true;
    }

    void AddSaveTypes(FileDialogOptions& opts) const override {
        UltraCanvasFileLoader::AddSaveTypes(opts, MediaFormatCategory::Spreadsheet);
    }

    // The sheet on show, cut into pages by its print settings (print area,
    // paper, scale or fit, breaks, gridlines, header and footer).
    bool CanExportPdf() const override { return true; }
    bool ExportPdf(const std::string& path, std::string& error) override {
        auto pages = CreateSpreadsheetPrintPages(*sheet_);
        if (!pages) { error = Tr("There is no sheet to export."); return false; }
        return pages->ExportPdf(path, error);
    }
    bool Print(const std::string& name, UltraCanvasWindowBase* parent, std::string& error) override {
        auto pages = CreateSpreadsheetPrintPages(*sheet_);
        if (!pages) { error = Tr("There is no sheet to print."); return false; }
        std::vector<uint8_t> pdf;
        if (!pages->ExportPdf(pdf, error)) return false;
        const IODeviceResult printed = PrintDocumentWithDialog(name, pdf, "application/pdf", parent, pages);
        if (!printed.success && printed.code != IODeviceResultCode::Cancelled) {
            error = printed.message;
            return false;
        }
        return true;
    }
    std::string DefaultExtension(SaveFormat format) const override {
        return format == SaveFormat::OpenDocument ? "ods" : "xlsx";
    }

    bool IsModified() const override { return modified_; }
    void SetModified(bool modified) override { modified_ = modified; }
    void Undo() override { sheet_->Undo(); Sync(); }
    void Redo() override { sheet_->Redo(); Sync(); }
    void Focus() override { sheet_->SetFocus(true); }

    std::string GetStatusText() const override {
        const CellRange range = sheet_->GetSelection();
        double sum = 0;
        int count = 0;
        const long long cells = static_cast<long long>(range.RowCount()) * range.ColCount();
        if (cells > 1 && cells <= 100000) {
            for (int r = range.start.row; r <= range.end.row; ++r) {
                for (int c = range.start.col; c <= range.end.col; ++c) {
                    const SpreadsheetCell* cell = sheet_->GetCellIfExists(r, c);
                    if (!cell) continue;
                    if (const auto number = cell->TryGetNumber()) { sum += *number; ++count; }
                }
            }
        }
        const std::string where = range.ToString();
        if (count == 0) return where;
        return where + "    " + TrF("Sum {0}", StatusNumber(sum)) + "    " +
               TrF("Average {0}", StatusNumber(sum / count)) + "    " + TrF("Count {0}", count);
    }

private:
    UltraCanvasWindowBase* window_;
    std::shared_ptr<UltraCanvasSpreadsheet> sheet_;
    std::vector<CommandTab> tabs_;
    bool modified_ = false;
    bool filtered_ = false;
    UltraCanvasButton* bold_ = nullptr;
    UltraCanvasButton* italic_ = nullptr;
    UltraCanvasButton* wrap_ = nullptr;
    UltraCanvasButton* filter_ = nullptr;
    UltraCanvasButton* landscape_ = nullptr;
    UltraCanvasButton* fitWidth_ = nullptr;
    UltraCanvasButton* gridlines_ = nullptr;
    UltraCanvasButton* headings_ = nullptr;
    UltraCanvasDropdown* format_ = nullptr;
    UltraCanvasButton* chartProperties_ = nullptr;
    UltraCanvasButton* chartDelete_ = nullptr;

    std::function<void()> Act(std::function<void(UltraCanvasSpreadsheet&)> action) {
        UltraCanvasSpreadsheet* sheet = sheet_.get();
        return [this, sheet, action]() {
            if (sheet->IsEditing()) sheet->StopEditing(true);
            action(*sheet);
            Sync();
            sheet->SetFocus(true);
            if (onChanged) onChanged();
        };
    }

    void BuildHome() {
        auto bar = MakeToolbar("office-sheet-home");
        AddTool(*bar, "sheet-cut", "", "menu/cut.svg", Tr("Cut (Ctrl+X)"), Act([](auto& s) { s.Cut(); }));
        AddTool(*bar, "sheet-copy", "", "menu/copy.svg", Tr("Copy (Ctrl+C)"), Act([](auto& s) { s.Copy(); }));
        AddTool(*bar, "sheet-paste", "", "menu/paste.svg", Tr("Paste (Ctrl+V)"), Act([](auto& s) { s.Paste(); }));
        bar->AddSeparator();
        bold_ = AddToggleTool(*bar, "sheet-bold", "", "textformat/bold.svg", Tr("Bold (Ctrl+B)"),
                              Act([](auto& s) { s.SetSelectionBold(!s.GetActiveCellFont().bold); })).get();
        italic_ = AddToggleTool(*bar, "sheet-italic", "", "textformat/italic.svg", Tr("Italic (Ctrl+I)"),
                                Act([](auto& s) { s.SetSelectionItalic(!s.GetActiveCellFont().italic); })).get();
        bar->AddSeparator();
        AddTool(*bar, "sheet-left", "", "textformat/align-left.svg", Tr("Align left"),
                Act([](auto& s) { s.SetSelectionAlignment(HorizontalAlignment::Left, s.GetActiveCellVAlign()); }));
        AddTool(*bar, "sheet-centre", "", "textformat/align-centre.svg", Tr("Centre"),
                Act([](auto& s) { s.SetSelectionAlignment(HorizontalAlignment::Center, s.GetActiveCellVAlign()); }));
        AddTool(*bar, "sheet-right", "", "textformat/align-right.svg", Tr("Align right"),
                Act([](auto& s) { s.SetSelectionAlignment(HorizontalAlignment::Right, s.GetActiveCellVAlign()); }));
        wrap_ = AddToggleTool(*bar, "sheet-wrap", Tr("Wrap"), "", Tr("Wrap text in the cell"),
                              Act([](auto& s) { s.SetSelectionWrapText(!s.GetActiveCellWrapText()); })).get();
        bar->AddSeparator();
        UltraCanvasSpreadsheet* sheet = sheet_.get();
        auto format = bar->AddDropdownButton("sheet-format", "",
            {Tr("General"), Tr("Number"), Tr("Currency"), Tr("Percent"), Tr("Date"), Tr("Text")}, nullptr);
        format->SetTooltip(Tr("Number format"));
        format->onSelectionChanged = [this, sheet](int index, const DropdownItem&) {
            static const std::vector<std::function<NumberFormat()>> formats = {
                [] { return NumberFormat::General(); }, [] { return NumberFormat::Number(2, true); },
                [] { return NumberFormat::Currency(); }, [] { return NumberFormat::Percentage(0); },
                [] { return NumberFormat::Date(); },    [] { return NumberFormat::Text(); }};
            if (index < 0 || index >= static_cast<int>(formats.size())) return;
            sheet->SetSelectionNumberFormat(formats[static_cast<size_t>(index)]());
            sheet->SetFocus(true);
            if (onChanged) onChanged();
        };
        format_ = format.get();
        AddTool(*bar, "sheet-moredec", ".0+", "", Tr("More decimal places"), Act([](auto& s) { s.AdjustSelectionDecimalPlaces(1); }));
        AddTool(*bar, "sheet-lessdec", ".0-", "", Tr("Fewer decimal places"), Act([](auto& s) { s.AdjustSelectionDecimalPlaces(-1); }));
        tabs_.push_back({Tr("Home"), bar});
    }

    // A chart of the selected cells, of the kind picked; the selected
    // chart's properties, and deleting it.
    void BuildInsert() {
        auto bar = MakeToolbar("office-sheet-insert");
        auto chart = bar->AddDropdownButton("sheet-chart", "", {}, nullptr);
        for (size_t i = 0; i < SpreadsheetChartChoices().size(); ++i)
            chart->AddItem(SpreadsheetChartChoiceName(i), std::to_string(i), IconPath("chart.svg"));
        chart->SetIconsAsMask(true);
        chart->SetPlaceholderText(Tr("Chart"));
        chart->SetTooltip(Tr("A chart of the selected cells"));
        chart->SetSelectedIndex(-1, false);
        UltraCanvasSpreadsheet* sheet = sheet_.get();
        UltraCanvasDropdown* dropdown = chart.get();
        chart->onSelectionChanged = [this, sheet, dropdown](int index, const DropdownItem&) {
            const auto& choices = SpreadsheetChartChoices();
            if (index < 0 || index >= static_cast<int>(choices.size())) return;
            dropdown->SetSelectedIndex(-1, false);
            const SpreadsheetChartChoice choice = choices[static_cast<size_t>(index)];
            if (sheet->InsertChartFromSelection(choice.kind, choice.stacked) < 0) {
                UltraCanvasDialogManager::ShowInformation(Tr("Select cells holding numbers to make a chart of them."),
                                                          Tr("Chart"), nullptr, window_);
                return;
            }
            sheet->SetFocus(true);
            Sync();
            if (onChanged) onChanged();
        };
        chartProperties_ = AddTool(*bar, "sheet-chart-properties", Tr("Chart properties"), "chart.svg",
                                   Tr("The selected chart's type, title and legend (or double-click the chart)"),
                                   Act([](auto& s) { if (s.GetSelectedChart() >= 0) s.ShowChartPropertiesDialog(s.GetSelectedChart()); })).get();
        chartDelete_ = AddTool(*bar, "sheet-chart-delete", Tr("Delete chart"), "delete.svg", Tr("Remove the selected chart (Delete)"),
                               Act([](auto& s) { s.RemoveChart(s.GetSelectedChart()); })).get();
        tabs_.push_back({Tr("Insert"), bar});
    }

    void BuildData() {
        auto bar = MakeToolbar("office-sheet-data");
        AddTool(*bar, "sheet-sortup", Tr("Sort A to Z"), "sort-alpha-down.svg", Tr("Sort the selection, smallest first"),
                Act([](auto& s) { s.SortSelectionAscending(); }));
        AddTool(*bar, "sheet-sortdown", Tr("Sort Z to A"), "sort-alpha-up.svg", Tr("Sort the selection, largest first"),
                Act([](auto& s) { s.SortSelectionDescending(); }));
        filter_ = AddToggleTool(*bar, "sheet-filter", Tr("Filter"), "filter.svg", Tr("Filter buttons on the selection's columns"),
                                Act([this](auto& s) {
                                    if (filtered_) s.RemoveAutoFilter(); else s.SetAutoFilter();
                                    filtered_ = !filtered_;
                                })).get();
        bar->AddSeparator();
        AddTool(*bar, "sheet-filldown", Tr("Fill down"), "", Tr("Copy the top cells down the selection (Ctrl+D)"),
                Act([](auto& s) { s.FillSelectionDown(); }));
        AddTool(*bar, "sheet-fillright", Tr("Fill right"), "", Tr("Copy the left cells across the selection (Ctrl+R)"),
                Act([](auto& s) { s.FillSelectionRight(); }));
        bar->AddSeparator();
        AddTool(*bar, "sheet-sum", Tr("Sum"), "office/sum.svg", Tr("Insert the SUM function"),
                Act([](auto& s) { s.InsertFunction("SUM"); }));
        tabs_.push_back({Tr("Data"), bar});
    }

    // How the sheet prints: the print area, paper turned or not, fitted to
    // the page's width, gridlines and row and column headings.
    void BuildPage() {
        auto bar = MakeToolbar("office-sheet-page");
        AddTool(*bar, "sheet-print-area", Tr("Set print area"), "", Tr("Print only the selected cells"),
                Act([this](auto& s) { s.SetPrintArea(s.GetSelection()); modified_ = true; }));
        AddTool(*bar, "sheet-print-area-clear", Tr("Clear print area"), "", Tr("Print every cell in use"),
                Act([this](auto& s) { s.ClearPrintArea(); modified_ = true; }));
        bar->AddSeparator();
        auto toggle = [this](UltraCanvasSpreadsheet& s, auto change) {
            change(s.GetPrintSettings());
            modified_ = true;
        };
        landscape_ = AddToggleTool(*bar, "sheet-landscape", Tr("Landscape"), "", Tr("Turn the paper on its side"),
            Act([toggle](auto& s) { toggle(s, [](PrintSettings& p) {
                p.orientation = p.orientation == PageOrientation::Landscape ? PageOrientation::Portrait : PageOrientation::Landscape; }); })).get();
        fitWidth_ = AddToggleTool(*bar, "sheet-fit-width", Tr("Fit to width"), "", Tr("Shrink the sheet so every column fits across one page"),
            Act([toggle](auto& s) { toggle(s, [](PrintSettings& p) { p.fitToWidth = p.fitToWidth == 1 ? 0 : 1; p.fitToHeight = 0; }); })).get();
        gridlines_ = AddToggleTool(*bar, "sheet-print-gridlines", Tr("Gridlines"), "", Tr("Print the lines between the cells"),
            Act([toggle](auto& s) { toggle(s, [](PrintSettings& p) { p.printGridlines = !p.printGridlines; }); })).get();
        headings_ = AddToggleTool(*bar, "sheet-print-headings", Tr("Headings"), "", Tr("Print the column letters and row numbers"),
            Act([toggle](auto& s) { toggle(s, [](PrintSettings& p) { p.printRowColHeaders = !p.printRowColHeaders; }); })).get();
        tabs_.push_back({Tr("Page"), bar});
    }

    void BuildView() {
        auto bar = MakeToolbar("office-sheet-view");
        AddTool(*bar, "sheet-freeze", Tr("Freeze panes"), "", Tr("Keep the rows above and the columns left of the selection in view"),
                Act([](auto& s) { s.FreezePanesAtSelection(); }));
        AddTool(*bar, "sheet-unfreeze", Tr("Unfreeze"), "", Tr("Let every row and column scroll"),
                Act([](auto& s) { s.UnfreezePanes(); }));
        tabs_.push_back({Tr("View"), bar});
    }

    void Sync() {
        // (A file being read has taken every sheet away for a moment.)
        if (!sheet_->GetActiveSheet()) return;
        const CellFont font = sheet_->GetActiveCellFont();
        if (bold_) bold_->SetPressed(font.bold);
        if (italic_) italic_->SetPressed(font.italic);
        if (wrap_) wrap_->SetPressed(sheet_->GetActiveCellWrapText());
        if (filter_) filter_->SetPressed(filtered_);
        const PrintSettings& print = sheet_->GetPrintSettings();
        if (landscape_) landscape_->SetPressed(print.orientation == PageOrientation::Landscape);
        if (fitWidth_) fitWidth_->SetPressed(print.fitToWidth == 1);
        if (gridlines_) gridlines_->SetPressed(print.printGridlines);
        if (headings_) headings_->SetPressed(print.printRowColHeaders);
        const bool chartSelected = sheet_->GetSelectedChart() >= 0;
        if (chartProperties_) chartProperties_->SetDisabled(!chartSelected);
        if (chartDelete_) chartDelete_->SetDisabled(!chartSelected);
        if (format_) {
            const NumberFormatCategory category = sheet_->GetActiveCellNumberFormat().category;
            int index = 0;
            switch (category) {
                case NumberFormatCategory::Number: index = 1; break;
                case NumberFormatCategory::Currency: case NumberFormatCategory::Accounting: index = 2; break;
                case NumberFormatCategory::Percentage: index = 3; break;
                case NumberFormatCategory::Date: index = 4; break;
                case NumberFormatCategory::Text: index = 5; break;
                default: index = 0; break;
            }
            format_->SetSelectedIndex(index, false);
        }
    }
};

} // namespace

std::unique_ptr<OfficeHost> CreateSpreadsheetHost(UltraCanvasWindowBase* window) {
    return std::make_unique<SpreadsheetHost>(window);
}

} // namespace UltraOffice
