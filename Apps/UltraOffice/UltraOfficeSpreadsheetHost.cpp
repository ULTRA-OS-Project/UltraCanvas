// Apps/UltraOffice/UltraOfficeSpreadsheetHost.cpp
// A spreadsheet tab: the framework's grid (UltraCanvasSpreadsheet) - formula
// bar, sheet tabs, its own format menu - read and written through its
// LoadFromFile / SaveToFile (.ods, .xlsx, .csv, .tsv; .xls read-only), the
// file types and password prompts through UltraCanvasFileLoader. Command
// tabs - Home, Insert, Data, Review, Page - with the view group at the
// toolbar's right end (freeze panes, unfreeze, gridlines, the zoom), and a
// status line with the sum, average and count of the selection. A pivot table's field list
// (UltraCanvasSpreadsheetPivotPanel) while one of its cells is active - in the Pivot table panel
// with the tool panels, beside the grid with the toolbar; each change in it is applied to the
// pivot at once.
//
// Version: 0.10.0 - the pivot field list moves into the Pivot table panel while the tools are panels
// Version: 0.9.0 - the tools are one table (UltraOfficeTools.h) the command tabs and the view group are
//                  built from, with no change to what they show; the panels' own tools in it too: the
//                  cell's font, size, text and fill colour, borders, vertical alignment and merging
// Version: 0.8.0 - Data fits beside the view group at 1280 px: sorting, filter, SUM and the pivot
//                  table's tools as icons
// Version: 0.7.0 - the view group (freeze panes, unfreeze, gridlines, zoom out, the zoom level, zoom in)
//                  replaces the View tab; the gridlines from the settings
// Version: 0.6.0 - pivot tables: Insert > Pivot table (the field list in a dialog, on a new sheet or a cell);
//                  Data: Refresh, Change source, Field list, Delete pivot; the field list beside the grid
// Version: 0.5.0 - Insert: Chart opens the chart gallery (every chart of the framework, by category, drawn
//                  from the selection); Chart type changes the selected chart's through it
// Version: 0.4.0 - Data: conditional formatting and data validation dialogs; Review: comments and links
// Version: 0.3.0 - Insert: a chart of the selection (columns, bars, lines, areas, pies, scatter ...),
//                  its properties and Delete chart
// Version: 0.2.0 - Export as PDF and Print, through the sheet's print pages
// Version: 0.1.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeSettings.h"
#include "UltraOfficeToolbar.h"
#include "UltraOfficeTools.h"

#include "UltraCanvasDropdown.h"
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"   // PrintDocumentWithDialog
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasSpreadsheetPrint.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasSpreadsheetPivot.h"
#include "UltraCanvasSpreadsheetPivotPanel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
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
        // The grid, and the field list of the pivot whose cell is active beside it.
        root_ = std::make_shared<UltraCanvasContainer>("office-spreadsheet-root", 0, 0, 800, 600);
        root_->layout.SetFlex(CSSLayout::FlexDirection::Row);
        root_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        sheet_ = std::make_shared<UltraCanvasSpreadsheet>("office-spreadsheet", 0, 0, 800, 600);
        sheet_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root_->AddChild(sheet_);
        pivotPanel_ = std::make_shared<UltraCanvasSpreadsheetPivotPanel>("office-pivot-fields", 0, 0, 340, 600, false);
        pivotPanel_->size.width = CSSLayout::Dimension::Px(340.0f);
        pivotPanel_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        pivotPanel_->SetPadding(8, 8, 8, 8);
        pivotPanel_->SetVisible(false);
        root_->AddChild(pivotPanel_);
        pivotPanel_->onPivotChanged = [this](const SpreadsheetPivotTable& changed) { ApplyPanelPivot(changed); };
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
        sheet_->onPivotTablesChange = changed;
        BuildTools();
        bars_.Build(tools_, window_);
    }

    OfficeKind GetKind() const override { return OfficeKind::Spreadsheet; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return root_; }
    const ToolTable& GetTools() const override { return tools_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return bars_.Tabs(); }
    std::shared_ptr<UltraCanvasToolbar> GetViewToolbar() override { return bars_.ViewBar(); }

    void AttachSettings(UltraCanvasAppSettings* settings) override {
        settings_ = settings;
        ApplySettings();
    }
    // The gridlines on screen follow the settings at once (printing them is
    // the Page tab's, and the file's).
    void ApplySettings() override {
        if (!settings_) return;
        sheet_->SetShowGridlines(OfficeSettings(*settings_).GetShowGridlines());
        SyncView();
    }

    void ZoomIn() override { SetZoom(sheet_->GetZoomLevel() + 10); }
    void ZoomOut() override { SetZoom(sheet_->GetZoomLevel() - 10); }
    void ZoomToActualSize() override { SetZoom(100); }

    void NewBlank() override { modified_ = false; panelPivot_ = -1; Sync(); }

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        if (!sheet_->LoadFromFile(path, password)) {
            error = sheet_->GetLastError();
            if (error.empty()) error = Tr("The spreadsheet could not be read.");
            return false;
        }
        modified_ = false;
        panelPivot_ = -1;   // the field list shows the new workbook's pivots
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
        UltraCanvasFileLoader::AddSpreadsheetSaveTypes(opts);   // the templates are "Save as template"'s
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
    std::shared_ptr<UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvasSpreadsheet> sheet_;
    std::shared_ptr<UltraCanvasSpreadsheetPivotPanel> pivotPanel_;
    // The pivot the field list shows (sheet, index), -1 for none; whether
    // the reader closed it; whether a change of its own is being applied.
    int panelSheet_ = -1;
    int panelPivot_ = -1;
    bool panelClosed_ = false;
    bool applyingPanel_ = false;
    bool modified_ = false;
    bool filtered_ = false;
    UltraCanvas::UltraCanvasAppSettings* settings_ = nullptr;   // the window's
    // The tools, and the command tabs' bars and the view group built from them.
    ToolTable tools_;
    ToolBars bars_;

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

    // ===== THE TOOLS =====
    // Every tool of a spreadsheet, once (UltraOfficeTools.h): the command
    // tabs' bars and the view group are built from it, and the tool panels
    // will be.
    void BuildTools() {
        using P = ToolPanel;
        tools_.kind = OfficeKind::Spreadsheet;
        UltraCanvasSpreadsheet* sheet = sheet_.get();
        tools_.inContext = [sheet](ToolContext context) {
            switch (context) {
                case ToolContext::ChartSelected: return sheet->GetSelectedChart() >= 0;
                case ToolContext::PivotCell: return sheet->GetActivePivotTable() >= 0;
                default: return false;
            }
        };
        auto chartSelected = [sheet]() { return sheet->GetSelectedChart() >= 0; };
        auto print = [sheet]() -> const PrintSettings& { return sheet->GetPrintSettings(); };
        ToolWriter w(tools_);

        // Home: the clipboard | B I | alignment and wrap | the number format.
        w.Tab("home", Tr("Home"), "office-sheet-home");
        w.Button("sheet-cut", "menu/cut.svg", Tr("Cut"), Tr("Cut (Ctrl+X)"), P::Clipboard, Act([](auto& s) { s.Cut(); }));
        w.Button("sheet-copy", "menu/copy.svg", Tr("Copy"), Tr("Copy (Ctrl+C)"), P::Clipboard, Act([](auto& s) { s.Copy(); }));
        w.Button("sheet-paste", "menu/paste.svg", Tr("Paste"), Tr("Paste (Ctrl+V)"), P::Clipboard, Act([](auto& s) { s.Paste(); }));
        w.NextGroup();
        w.Toggle("sheet-bold", "textformat/bold.svg", Tr("Bold"), Tr("Bold (Ctrl+B)"), P::Cell,
                 Act([](auto& s) { s.SetSelectionBold(!s.GetActiveCellFont().bold); }),
                 [sheet]() { return sheet->GetActiveCellFont().bold; });
        w.Toggle("sheet-italic", "textformat/italic.svg", Tr("Italic"), Tr("Italic (Ctrl+I)"), P::Cell,
                 Act([](auto& s) { s.SetSelectionItalic(!s.GetActiveCellFont().italic); }),
                 [sheet]() { return sheet->GetActiveCellFont().italic; });
        w.NextGroup();
        w.Button("sheet-left", "textformat/align-left.svg", Tr("Align left"), Tr("Align left"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionAlignment(HorizontalAlignment::Left, s.GetActiveCellVAlign()); }));
        w.Button("sheet-centre", "textformat/align-centre.svg", Tr("Centre"), Tr("Centre"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionAlignment(HorizontalAlignment::Center, s.GetActiveCellVAlign()); }));
        w.Button("sheet-right", "textformat/align-right.svg", Tr("Align right"), Tr("Align right"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionAlignment(HorizontalAlignment::Right, s.GetActiveCellVAlign()); }));
        w.Toggle("sheet-wrap", "", Tr("Wrap"), Tr("Wrap text in the cell"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionWrapText(!s.GetActiveCellWrapText()); }),
                 [sheet]() { return sheet->GetActiveCellWrapText(); }).labelOnBar = true;
        w.NextGroup();
        {
            ToolEntry& format = w.List("sheet-format", Tr("Number format"), Tr("Number format"), P::Number);
            format.list.items = [] {
                return std::vector<std::string>{Tr("General"), Tr("Number"), Tr("Currency"), Tr("Percent"), Tr("Date"), Tr("Text")};
            };
            format.list.choose = [this, sheet](int index) {
                static const std::vector<std::function<NumberFormat()>> formats = {
                    [] { return NumberFormat::General(); }, [] { return NumberFormat::Number(2, true); },
                    [] { return NumberFormat::Currency(); }, [] { return NumberFormat::Percentage(0); },
                    [] { return NumberFormat::Date(); },    [] { return NumberFormat::Text(); }};
                if (index >= static_cast<int>(formats.size())) return;
                sheet->SetSelectionNumberFormat(formats[static_cast<size_t>(index)]());
                sheet->SetFocus(true);
                if (onChanged) onChanged();
            };
            format.list.selected = [sheet]() {
                switch (sheet->GetActiveCellNumberFormat().category) {
                    case NumberFormatCategory::Number: return 1;
                    case NumberFormatCategory::Currency: case NumberFormatCategory::Accounting: return 2;
                    case NumberFormatCategory::Percentage: return 3;
                    case NumberFormatCategory::Date: return 4;
                    case NumberFormatCategory::Text: return 5;
                    default: return 0;
                }
            };
        }
        w.Button("sheet-moredec", "", ".0+", Tr("More decimal places"), P::Number,
                 Act([](auto& s) { s.AdjustSelectionDecimalPlaces(1); })).labelOnBar = true;
        w.Button("sheet-lessdec", "", ".0-", Tr("Fewer decimal places"), P::Number,
                 Act([](auto& s) { s.AdjustSelectionDecimalPlaces(-1); })).labelOnBar = true;

        // Insert: a chart of the selected cells, of the type picked in the
        // chart gallery; the selected chart's type and properties, and
        // deleting it | a pivot table.
        w.Tab("insert", Tr("Insert"), "office-sheet-insert");
        w.Button("sheet-chart", "chart.svg", Tr("Chart"), Tr("A chart of the selected cells: every chart type, by category, drawn from them"),
                 P::Insert, Act([](auto& s) { s.ShowInsertChartDialog(); })).labelOnBar = true;
        {
            ToolEntry& type = w.Button("sheet-chart-type", "chart.svg", Tr("Chart type"), Tr("Change the selected chart's type in the chart gallery"),
                                       P::Chart, Act([](auto& s) { if (s.GetSelectedChart() >= 0) s.ShowChartTypeDialog(s.GetSelectedChart()); }));
            type.labelOnBar = true;
            type.context = ToolContext::ChartSelected;
            type.isEnabled = chartSelected;
        }
        {
            ToolEntry& properties = w.Button("sheet-chart-properties", "chart.svg", Tr("Chart properties"),
                                             Tr("The selected chart's type, title and legend (or double-click the chart)"), P::Chart,
                                             Act([](auto& s) { if (s.GetSelectedChart() >= 0) s.ShowChartPropertiesDialog(s.GetSelectedChart()); }));
            properties.labelOnBar = true;
            properties.context = ToolContext::ChartSelected;
            properties.isEnabled = chartSelected;
        }
        {
            ToolEntry& remove = w.Button("sheet-chart-delete", "delete.svg", Tr("Delete chart"), Tr("Remove the selected chart (Delete)"),
                                         P::Chart, Act([](auto& s) { s.RemoveChart(s.GetSelectedChart()); }));
            remove.labelOnBar = true;
            remove.context = ToolContext::ChartSelected;
            remove.isEnabled = chartSelected;
        }
        w.NextGroup();
        w.Button("sheet-pivot", "table.svg", Tr("Pivot table"),
                 Tr("Summarise the selected cells in a pivot table: their fields down the side, across the top and in the values"),
                 P::Insert, Act([this](auto& s) { panelClosed_ = false; s.ShowInsertPivotTableDialog(); })).labelOnBar = true;

        // Data: sorting and the filter | filling | SUM | the rules of the
        // selection, in the grid's own dialogs | the pivot table whose cell
        // is active.
        w.Tab("data", Tr("Data"), "office-sheet-data");
        w.Button("sheet-sortup", "sort-alpha-down.svg", Tr("Sort A to Z"), Tr("Sort A to Z: sort the selection, smallest first"),
                 P::Data, Act([](auto& s) { s.SortSelectionAscending(); }));
        w.Button("sheet-sortdown", "sort-alpha-up.svg", Tr("Sort Z to A"), Tr("Sort Z to A: sort the selection, largest first"),
                 P::Data, Act([](auto& s) { s.SortSelectionDescending(); }));
        w.Toggle("sheet-filter", "filter.svg", Tr("Filter"), Tr("Filter: filter buttons on the selection's columns"), P::Data,
                 Act([this](auto& s) {
                     if (filtered_) s.RemoveAutoFilter(); else s.SetAutoFilter();
                     filtered_ = !filtered_;
                 }),
                 [this]() { return filtered_; });
        w.NextGroup();
        w.Button("sheet-filldown", "", Tr("Fill down"), Tr("Copy the top cells down the selection (Ctrl+D)"), P::Data,
                 Act([](auto& s) { s.FillSelectionDown(); })).labelOnBar = true;
        w.Button("sheet-fillright", "", Tr("Fill right"), Tr("Copy the left cells across the selection (Ctrl+R)"), P::Data,
                 Act([](auto& s) { s.FillSelectionRight(); })).labelOnBar = true;
        w.NextGroup();
        w.Button("sheet-sum", "office/sum.svg", Tr("Sum"), Tr("Sum: insert the SUM function"), P::Data,
                 Act([](auto& s) { s.InsertFunction("SUM"); }));
        w.NextGroup();
        w.Button("sheet-conditional", "menu/palette.svg", Tr("Conditional formatting"),
                 Tr("Colour the selected cells by their values: rules, colour scales, data bars"), P::Data,
                 Act([](auto& s) { s.ShowConditionalFormatDialog(); })).labelOnBar = true;
        w.Button("sheet-validation", "list-check.svg", Tr("Data validation"),
                 Tr("Limit what may be typed into the selected cells, or offer a list"), P::Data,
                 Act([](auto& s) { s.ShowDataValidationDialog(); })).labelOnBar = true;
        w.NextGroup();
        auto pivotTool = [](ToolEntry& entry, std::function<bool()> enabled) {
            entry.context = ToolContext::PivotCell;
            entry.isEnabled = std::move(enabled);
        };
        pivotTool(w.Button("sheet-pivot-refresh", "menu/refresh.svg", Tr("Refresh"),
                           Tr("Refresh: compute the pivot table from its source again (every pivot table when none is picked)"),
                           P::Pivot, Act([this](auto& s) { RefreshPivot(s); })),
                  nullptr);
        pivotTool(w.Button("sheet-pivot-source", "menu/dataset.svg", Tr("Change source"),
                           Tr("Change source: the cells the pivot table summarises"), P::Pivot,
                           Act([](auto& s) { if (s.GetActivePivotTable() >= 0) s.ShowPivotSourceDialog(s.GetActivePivotTable()); })),
                  [this]() { return PivotSupported(); });
        pivotTool(w.Toggle("sheet-pivot-fields", "menu/sidebar.svg", Tr("Field list"),
                           Tr("Field list: show or hide the pivot table's fields beside the sheet"), P::Pivot,
                           Act([this](auto&) { panelClosed_ = !panelClosed_; }),
                           [this]() { return PivotSupported() && !panelClosed_; }),
                  [this]() { return PivotSupported(); });
        pivotTool(w.Button("sheet-pivot-delete", "delete.svg", Tr("Delete pivot"), Tr("Delete pivot: remove the pivot table and its cells"),
                           P::Pivot, Act([](auto& s) { s.RemovePivotTable(s.GetActiveSheetIndex(), s.GetActivePivotTable()); })),
                  [sheet]() { return sheet->GetActivePivotTable() >= 0; });

        // Review: comments | links of the active cell.
        w.Tab("review", Tr("Review"), "office-sheet-review");
        w.Button("sheet-comment", "quote.svg", Tr("Comment"), Tr("Add or change the active cell's comment"), P::Insert,
                 Act([](auto& s) { s.ShowCommentDialog(); })).labelOnBar = true;
        w.Button("sheet-comment-delete", "delete.svg", Tr("Delete comment"), Tr("Delete the active cell's comment"), P::Insert,
                 Act([](auto& s) { s.RemoveCellComment(s.GetActiveCell().row, s.GetActiveCell().col); })).labelOnBar = true;
        w.NextGroup();
        w.Button("sheet-link", "link.svg", Tr("Link"), Tr("Link the active cell to a web or mail address, or to a cell"), P::Insert,
                 Act([](auto& s) { s.ShowHyperlinkDialog(); })).labelOnBar = true;
        w.Button("sheet-link-open", "", Tr("Open link"), Tr("Open the active cell's link (Ctrl+click)"), P::Insert,
                 Act([](auto& s) { s.OpenHyperlink(s.GetActiveCell().row, s.GetActiveCell().col); })).labelOnBar = true;
        w.Button("sheet-link-remove", "link-slash.svg", Tr("Remove link"), Tr("Remove the active cell's link"), P::Insert,
                 Act([](auto& s) { s.RemoveCellHyperlink(s.GetActiveCell().row, s.GetActiveCell().col); })).labelOnBar = true;

        // Page: how the sheet prints - the print area | paper turned or not,
        // fitted to the page's width, gridlines and row and column headings.
        w.Tab("page", Tr("Page"), "office-sheet-page");
        w.Button("sheet-print-area", "", Tr("Set print area"), Tr("Print only the selected cells"), P::Page,
                 Act([this](auto& s) { s.SetPrintArea(s.GetSelection()); modified_ = true; })).labelOnBar = true;
        w.Button("sheet-print-area-clear", "", Tr("Clear print area"), Tr("Print every cell in use"), P::Page,
                 Act([this](auto& s) { s.ClearPrintArea(); modified_ = true; })).labelOnBar = true;
        w.NextGroup();
        auto printToggle = [this](std::function<void(PrintSettings&)> change) {
            return Act([this, change](auto& s) { change(s.GetPrintSettings()); modified_ = true; });
        };
        w.Toggle("sheet-landscape", "", Tr("Landscape"), Tr("Turn the paper on its side"), P::Page,
                 printToggle([](PrintSettings& p) {
                     p.orientation = p.orientation == PageOrientation::Landscape ? PageOrientation::Portrait : PageOrientation::Landscape;
                 }),
                 [print]() { return print().orientation == PageOrientation::Landscape; }).labelOnBar = true;
        w.Toggle("sheet-fit-width", "", Tr("Fit to width"), Tr("Shrink the sheet so every column fits across one page"), P::Page,
                 printToggle([](PrintSettings& p) { p.fitToWidth = p.fitToWidth == 1 ? 0 : 1; p.fitToHeight = 0; }),
                 [print]() { return print().fitToWidth == 1; }).labelOnBar = true;
        w.Toggle("sheet-print-gridlines", "", Tr("Gridlines"), Tr("Print the lines between the cells"), P::Page,
                 printToggle([](PrintSettings& p) { p.printGridlines = !p.printGridlines; }),
                 [print]() { return print().printGridlines; }).labelOnBar = true;
        w.Toggle("sheet-print-headings", "", Tr("Headings"), Tr("Print the column letters and row numbers"), P::Page,
                 printToggle([](PrintSettings& p) { p.printRowColHeaders = !p.printRowColHeaders; }),
                 [print]() { return print().printRowColHeaders; }).labelOnBar = true;

        // The view group: freeze panes, unfreeze, gridlines | zoom out,
        // [zoom], zoom in.
        w.ViewGroup("office-sheet-view");
        w.Button("sheet-freeze", "office/freeze-panes.svg", Tr("Freeze panes"),
                 Tr("Freeze panes: keep the rows above and the columns left of the selection in view"), P::ViewGroup,
                 Act([](auto& s) { s.FreezePanesAtSelection(); }));
        w.Button("sheet-unfreeze", "office/unfreeze-panes.svg", Tr("Unfreeze"), Tr("Unfreeze: let every row and column scroll"),
                 P::ViewGroup, Act([](auto& s) { s.UnfreezePanes(); }));
        w.Toggle("sheet-gridlines", "office/gridlines.svg", Tr("Gridlines"), Tr("Gridlines: show the lines between the cells"),
                 P::ViewGroup, Act([](auto& s) { s.SetShowGridlines(!s.GetShowGridlines()); }),
                 [sheet]() { return sheet->GetShowGridlines(); });
        w.NextGroup();
        w.Button("sheet-zoomout", "menu/zoom-out.svg", Tr("Zoom out"), Tr("Zoom out (Ctrl+-)"), P::ViewGroup, [this]() { ZoomOut(); });
        {
            ToolEntry& zoom = w.List("sheet-zoom", Tr("Zoom"), Tr("Zoom level (Ctrl+0: 100 %)"), P::ViewGroup, ToolListStyle::Percent);
            zoom.list.percents = ZoomLevelChoices();
            zoom.list.minimum = 10;
            zoom.list.maximum = 400;
            zoom.list.askTitle = Tr("Zoom");
            zoom.list.choosePercent = [this](int percent) { SetZoom(percent); };
            zoom.list.currentPercent = [sheet]() { return sheet->GetZoomLevel(); };
        }
        w.Button("sheet-zoomin", "menu/zoom-in.svg", Tr("Zoom in"), Tr("Zoom in (Ctrl++)"), P::ViewGroup, [this]() { ZoomIn(); });

        // The panels' own: the cell's font, size, colours and borders, its
        // vertical alignment, merging.
        w.PanelsOnly();
        {
            ToolEntry& font = w.List("sheet-font", Tr("Font"), Tr("Font"), P::Cell, ToolListStyle::Font);
            font.list.settings = [this]() { return settings_; };
            font.list.chooseFont = [this, sheet](const FontPickerChoice& choice) {
                sheet->SetSelectionFontFamily(FontChoiceForRun(choice).family);
                Sync();
                if (onChanged) onChanged();
            };
            font.list.currentFont = [sheet]() -> std::optional<std::string> { return sheet->GetActiveCellFont().family; };
        }
        w.Number("sheet-fontsize", Tr("Font size"), Tr("pt"), 1.0, 409.0, 1.0, 1, P::Cell,
                 [sheet]() -> std::optional<double> { return sheet->GetActiveCellFont().size; },
                 [this, sheet](double to) { sheet->SetSelectionFontSize(static_cast<float>(to)); Sync(); });
        w.Colour("sheet-textcolour", Tr("Text colour"), P::Cell,
                 [sheet]() -> std::optional<Color> { return sheet->GetActiveCellFont().color; },
                 [this, sheet](const Color& colour) { sheet->SetSelectionFontColor(colour); Sync(); });
        w.Colour("sheet-fillcolour", Tr("Fill colour"), P::Cell,
                 [sheet]() -> std::optional<Color> {
                     const CellAddress active = sheet->GetActiveCell();
                     const SpreadsheetCell* cell = sheet->GetCellIfExists(active.row, active.col);
                     if (!cell || !cell->GetStyle().fill.HasFill()) return std::nullopt;   // no fill
                     return cell->GetStyle().fill.foregroundColor;
                 },
                 [this, sheet](const Color& colour) { sheet->SetSelectionBackgroundColor(colour); Sync(); });
        {
            ToolEntry& borders = w.List("sheet-borders", Tr("Borders"), Tr("The lines round the selected cells"), P::Cell);
            borders.list.items = [] { return std::vector<std::string>{Tr("No borders"), Tr("All borders"), Tr("Bottom border")}; };
            borders.list.choose = [this, sheet](int index) {
                CellBorders lines;
                CellBorder thin;
                thin.style = BorderStyle::Thin;
                if (index == 1) lines.SetAll(thin);
                else if (index == 2) lines.bottom = thin;
                sheet->SetSelectionBorders(lines);
                Sync();
                if (onChanged) onChanged();
            };
            borders.list.selected = [sheet]() {
                const CellAddress active = sheet->GetActiveCell();
                const SpreadsheetCell* cell = sheet->GetCellIfExists(active.row, active.col);
                if (!cell || !cell->GetStyle().borders.HasAnyBorder()) return 0;
                const CellBorders& lines = cell->GetStyle().borders;
                if (lines.left.IsVisible() && lines.right.IsVisible() && lines.top.IsVisible() && lines.bottom.IsVisible()) return 1;
                if (lines.bottom.IsVisible() && !lines.left.IsVisible() && !lines.right.IsVisible() && !lines.top.IsVisible()) return 2;
                return -1;   // lines of another kind
            };
        }
        w.Button("sheet-top", "artcreator/align-top.svg", Tr("Align top"), Tr("Align top"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionAlignment(s.GetActiveCellHAlign(), VerticalAlignment::Top); }));
        w.Button("sheet-middle", "artcreator/align-vcentre.svg", Tr("Align middle"), Tr("Align middle"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionAlignment(s.GetActiveCellHAlign(), VerticalAlignment::Middle); }));
        w.Button("sheet-bottom", "artcreator/align-bottom.svg", Tr("Align bottom"), Tr("Align bottom"), P::Alignment,
                 Act([](auto& s) { s.SetSelectionAlignment(s.GetActiveCellHAlign(), VerticalAlignment::Bottom); }));
        // The pivot table's field list, in the Pivot table panel while the
        // tools are panels; beside the grid with the toolbar.
        w.Custom("sheet-pivot-field-list", Tr("Pivot table fields"),
                 Tr("The pivot table's fields: what goes down the side, across the top, into the values and the filters"),
                 P::Pivot, [this](bool inPanel) { return HandOverPivotPanel(inPanel); })
            .context = ToolContext::PivotCell;
        w.Toggle("sheet-merge", "", Tr("Merge cells"), Tr("Merge the selected cells into one, or split a merged cell"), P::Alignment,
                 Act([](auto& s) {
                     const CellAddress active = s.GetActiveCell();
                     if (s.IsCellMerged(active.row, active.col)) s.UnmergeSelection();
                     else s.MergeSelection();
                 }),
                 [sheet]() {
                     const CellAddress active = sheet->GetActiveCell();
                     return sheet->IsCellMerged(active.row, active.col);
                 });
    }

    // The field list into a tool panel (out of the row beside the grid), or
    // back beside the grid.
    std::shared_ptr<UltraCanvasUIElement> HandOverPivotPanel(bool inPanel) {
        if (auto* parent = pivotPanel_->GetParentContainer()) parent->RemoveChild(pivotPanel_);
        // In a panel the column scrolls: the list is as tall as it is; beside
        // the grid it is the grid's height and scrolls itself.
        ContainerStyle scroll = pivotPanel_->GetContainerStyle();
        scroll.autoShowScrollbars = !inPanel;
        scroll.autoShowVerticalScrollbar = !inPanel;
        pivotPanel_->SetContainerStyle(scroll);
        pivotPanel_->size.height = CSSLayout::Dimension::Auto();
        if (inPanel) {
            pivotPanel_->size.width = CSSLayout::Dimension::Auto();
            pivotPanel_->SetPadding(0, 0, 0, 0);
            root_->InvalidateLayout();
            return pivotPanel_;
        }
        pivotPanel_->size.width = CSSLayout::Dimension::Px(340.0f);
        pivotPanel_->SetPadding(8, 8, 8, 8);
        root_->AddChild(pivotPanel_);
        root_->InvalidateLayout();
        return nullptr;
    }

    // Whether the active cell is one of a pivot table this grid can change.
    bool PivotSupported() const {
        const int pivot = sheet_->GetActivePivotTable();
        const SpreadsheetSheet* sheet = sheet_->GetActiveSheet();
        return pivot >= 0 && sheet && sheet->GetPivotTables()[static_cast<size_t>(pivot)].unsupportedReason.empty();
    }

    // The sheet's zoom: a view of it, which leaves the workbook unchanged.
    void SetZoom(int percent) {
        sheet_->SetZoomLevel(std::clamp(percent, 10, 400));
        SyncView();
        sheet_->SetFocus(true);
    }

    void SyncView() { bars_.SyncTab(kViewGroupTab); }

    // Refresh: the active pivot, or every one.
    void RefreshPivot(UltraCanvasSpreadsheet& s) {
        std::string problem;
        const int pivot = s.GetActivePivotTable();
        const bool ok = pivot >= 0 ? s.RefreshPivotTable(s.GetActiveSheetIndex(), pivot, &problem) : s.RefreshPivotTables() >= 0;
        if (!ok && !problem.empty()) UltraCanvasDialogManager::ShowInformation(problem, Tr("Pivot table"), nullptr, window_);
    }

    // A change made in the field list beside the grid.
    void ApplyPanelPivot(const SpreadsheetPivotTable& changed) {
        if (panelSheet_ < 0 || panelPivot_ < 0) return;
        applyingPanel_ = true;   // the panel keeps its state; Sync must not rebuild it under its own call
        std::string problem;
        sheet_->SetPivotTable(panelSheet_, panelPivot_, changed, &problem);
        applyingPanel_ = false;
    }

    // The field list follows the active cell: shown, for its pivot, while
    // the cell is one of a pivot's (unless the reader closed it).
    void SyncPivotPanel() {
        const int pivot = sheet_->GetActivePivotTable();
        const int sheetIndex = sheet_->GetActiveSheetIndex();
        const SpreadsheetSheet* sheet = sheet_->GetActiveSheet();
        const bool supported = PivotSupported();
        const bool show = supported && !panelClosed_;
        if (show && !applyingPanel_ && (pivot != panelPivot_ || sheetIndex != panelSheet_ || !pivotPanel_->IsVisible())) {
            const SpreadsheetPivotTable& shown = sheet->GetPivotTables()[static_cast<size_t>(pivot)];
            pivotPanel_->SetPivot(shown, sheet_->GetSheetByName(shown.sourceSheet));
        }
        if (show) {
            panelSheet_ = sheetIndex;
            panelPivot_ = pivot;
        } else {
            panelSheet_ = panelPivot_ = -1;
        }
        if (pivotPanel_->IsVisible() != show) {
            pivotPanel_->SetVisible(show);
            if (auto* parent = pivotPanel_->GetParentContainer()) parent->InvalidateLayout();   // beside the grid, or a panel
            root_->InvalidateLayout();
        }
    }

    void Sync() {
        // (A file being read has taken every sheet away for a moment.)
        if (!sheet_->GetActiveSheet()) return;
        SyncPivotPanel();
        bars_.Sync();
    }
};

} // namespace

std::unique_ptr<OfficeHost> CreateSpreadsheetHost(UltraCanvasWindowBase* window) {
    return std::make_unique<SpreadsheetHost>(window);
}

} // namespace UltraOffice
