// core/UltraCanvasSpreadsheetXlsIO.cpp
// UltraCanvasSpreadsheet::LoadXLS - a legacy Excel workbook (.xls) into the
// spreadsheet, through the reader in UltraCanvasSpreadsheetXls.h. Mirrors
// LoadXLSX: sheets, typed values (dates, times, percentages and currency by
// their number format), formulas with their cached results, merged cells,
// column widths, row heights, hidden rows / columns / sheets, defined names
// and the CellStyle subset (font, fill, borders, alignment, wrap).
//
// A file named .xls is not always a binary workbook: an .xlsx renamed goes to
// LoadXLSX, delimited text to LoadCSV, and the HTML tables and Excel 2003 XML
// that applications export under the name are read the way Excel reads them.
// There is no .xls writer: save as .xlsx or .ods.
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetFormula.h"
#include "UltraCanvasSpreadsheetXls.h"

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace UltraCanvas {

namespace {

// The same conversions the .xlsx loader uses: a character of the default font
// is ~7 px, a point 1.33 px.
constexpr double kPixelsPerWidthChar = 7.0;
constexpr double kPixelsPerPoint = 1.33;
// Excel's 1900 and 1904 date systems differ by this many days.
constexpr double kDays1904 = 1462.0;

FillPattern FillPatternFor(int biffPattern) {
    switch (biffPattern) {
        case 1: return FillPattern::Solid;
        case 2: return FillPattern::Gray50;
        case 3: return FillPattern::Gray75;
        case 4: return FillPattern::Gray25;
        case 5: return FillPattern::HorzStripe;
        case 6: return FillPattern::VertStripe;
        case 7: return FillPattern::ReverseDiagStripe;
        case 8: return FillPattern::DiagStripe;
        case 9: return FillPattern::DiagCross;
        case 10: return FillPattern::ThickDiagCross;
        case 11: return FillPattern::ThinHorzStripe;
        case 12: return FillPattern::ThinVertStripe;
        case 13: return FillPattern::ThinReverseDiagStripe;
        case 14: return FillPattern::ThinDiagStripe;
        case 15: return FillPattern::ThinHorzCross;
        case 16: return FillPattern::ThinDiagCross;
        case 17: return FillPattern::Gray125;
        case 18: return FillPattern::Gray0625;
        default: return FillPattern::None;
    }
}

BorderStyle BorderStyleFor(int biffStyle) {
    switch (biffStyle) {
        case 1: return BorderStyle::Thin;
        case 2: return BorderStyle::Medium;
        case 3: return BorderStyle::Dashed;
        case 4: return BorderStyle::Dotted;
        case 5: return BorderStyle::Thick;
        case 6: return BorderStyle::Double;
        case 7: return BorderStyle::Hair;
        case 8: return BorderStyle::MediumDashed;
        case 9: return BorderStyle::DashDot;
        case 10: return BorderStyle::MediumDashDot;
        case 11: return BorderStyle::DashDotDot;
        case 12: return BorderStyle::DashDotDot;   // medium dash-dot-dot: no own style
        case 13: return BorderStyle::SlantDashDot;
        default: return BorderStyle::None;
    }
}

HorizontalAlignment HorizontalFor(int biffAlign) {
    switch (biffAlign) {
        case 1: return HorizontalAlignment::Left;
        case 2:
        case 6: return HorizontalAlignment::Center;   // centre across selection
        case 3: return HorizontalAlignment::Right;
        case 4: return HorizontalAlignment::Fill;
        case 5: return HorizontalAlignment::Justify;
        case 7: return HorizontalAlignment::Distributed;
        default: return HorizontalAlignment::General;
    }
}

class XlsWorkbookLoader {
public:
    XlsWorkbookLoader(UltraCanvasSpreadsheet* spreadsheet, const XlsWorkbook& wb)
        : spreadsheet_(spreadsheet), wb_(wb), styles_(wb.formats.size()),
          stylesBuilt_(wb.formats.size(), false) {}

    void Load() {
        while (spreadsheet_->GetSheetCount() > 0) spreadsheet_->RemoveSheet(0);
        for (const XlsSheet& s : wb_.sheets) {
            SpreadsheetSheet* sheet = spreadsheet_->AddSheet(s.name);
            if (sheet) sheet->SetVisible(!s.hidden);
        }
        if (spreadsheet_->GetSheetCount() == 0) spreadsheet_->AddSheet("Sheet1");
        LoadNames();
        for (size_t i = 0; i < wb_.sheets.size(); ++i) {
            SpreadsheetSheet* sheet = spreadsheet_->GetSheet(static_cast<int>(i));
            if (sheet) LoadSheet(wb_.sheets[i], sheet);
        }
        // Open on the first sheet a person can see.
        int active = 0;
        for (int i = 0; i < spreadsheet_->GetSheetCount(); ++i) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(i);
            if (sheet && sheet->IsVisible()) { active = i; break; }
        }
        spreadsheet_->SetActiveSheet(active);
    }

private:
    UltraCanvasSpreadsheet* spreadsheet_;
    const XlsWorkbook& wb_;
    std::vector<std::shared_ptr<CellStyle>> styles_;   // per XF, built on first use
    std::vector<bool> stylesBuilt_;
    std::set<std::string> names_;                      // defined names the engine holds

    void LoadNames() {
        for (const XlsDefinedName& n : wb_.names) {
            if (n.builtIn || n.refSheet < 0 ||
                n.refSheet >= static_cast<int>(wb_.sheets.size()))
                continue;
            CellRange range(n.firstRow, n.firstColumn, n.lastRow, n.lastColumn);
            range.start.sheetName = wb_.sheets[static_cast<size_t>(n.refSheet)].name;
            range.end.sheetName = range.start.sheetName;
            const std::string scope =
                n.sheetScope >= 0 && n.sheetScope < static_cast<int>(wb_.sheets.size())
                    ? wb_.sheets[static_cast<size_t>(n.sheetScope)].name
                    : std::string();
            spreadsheet_->AddNamedRange(n.name, range, scope);
            names_.insert(n.name);
        }
    }

    // The workbook's first font is its default (Arial 10 in Excel 97); a cell
    // in it is drawn in the spreadsheet's own default font, so a sheet looks
    // the same whether a cell came from the file or was typed in afterwards.
    // Another family or size is kept as the file states it.
    CellFont FontFor(int index) const {
        CellFont font;
        if (index < 0 || index >= static_cast<int>(wb_.fonts.size())) return font;
        const XlsFont& f = wb_.fonts[static_cast<size_t>(index)];
        const XlsFont& base = wb_.fonts.front();
        if (f.name != base.name && !f.name.empty()) font.family = f.name;
        if (f.sizePoints != base.sizePoints && f.sizePoints > 0) {
            font.size = static_cast<float>(f.sizePoints);
        }
        font.bold = f.bold;
        font.italic = f.italic;
        font.strikethrough = f.strikethrough;
        font.underline = f.underline == 2 ? UnderlineStyle::Double
                       : f.underline == 1 ? UnderlineStyle::Single
                                          : UnderlineStyle::None;
        font.superscript = f.superscript;
        font.subscript = f.subscript;
        if (!f.automaticColor) font.color = f.color;
        return font;
    }

    NumberFormatCategory CategoryOf(int format) const {
        if (format < 0 || format >= static_cast<int>(wb_.formats.size()))
            return NumberFormatCategory::General;
        const XlsCellFormat& f = wb_.formats[static_cast<size_t>(format)];
        return ExcelNumberFormatCategory(f.numberFormatId, f.numberFormatCode);
    }

    // The shared style of an XF, or nullptr when it changes nothing visible.
    std::shared_ptr<CellStyle> StyleFor(int format) {
        if (format < 0 || format >= static_cast<int>(wb_.formats.size())) return nullptr;
        const size_t index = static_cast<size_t>(format);
        if (stylesBuilt_[index]) return styles_[index];
        stylesBuilt_[index] = true;

        const XlsCellFormat& f = wb_.formats[index];
        CellStyle style;
        style.font = FontFor(f.font);
        if (f.fillPattern > 0) {
            style.fill.pattern = FillPatternFor(f.fillPattern);
            style.fill.foregroundColor = f.patternColor;
            style.fill.backgroundColor = f.backgroundColor;
        }
        auto border = [](const XlsBorderLine& line, CellBorder& side) {
            side.style = BorderStyleFor(line.style);
            side.color = line.color;
        };
        border(f.left, style.borders.left);
        border(f.right, style.borders.right);
        border(f.top, style.borders.top);
        border(f.bottom, style.borders.bottom);
        style.hAlign = HorizontalFor(f.horizontalAlign);
        style.vAlign = f.verticalAlign == 0 ? VerticalAlignment::Top
                     : f.verticalAlign == 1 ? VerticalAlignment::Middle
                                            : VerticalAlignment::Bottom;
        style.wrapText = f.wrapText;
        style.shrinkToFit = f.shrinkToFit;
        style.textRotation = f.rotation;
        style.indent = f.indent;
        style.locked = f.locked;
        style.hidden = f.formulaHidden;
        style.numberFormat.category = CategoryOf(format);
        // Ids below 164 are Excel's built-in formats, which Excel itself
        // renders in the reader's locale (14 is "the short date"); only a
        // workbook's own codes are kept, as the .xlsx loader does.
        const bool customCode = f.numberFormatId >= 164 && f.numberFormatCode != "General";
        if (customCode) style.numberFormat.formatCode = f.numberFormatCode;

        const CellStyle defaults;
        const bool meaningful = !(style.font == defaults.font) || style.fill.HasFill() ||
                                style.borders.HasAnyBorder() ||
                                style.hAlign != defaults.hAlign ||
                                style.vAlign != defaults.vAlign || style.wrapText ||
                                style.shrinkToFit || style.textRotation != 0 ||
                                style.indent != 0 || customCode;
        if (meaningful) styles_[index] = std::make_shared<CellStyle>(style);
        return styles_[index];
    }

    double Serial(double value) const { return wb_.date1904 ? value + kDays1904 : value; }

    void SetTypedNumber(SpreadsheetCell* cell, double value, NumberFormatCategory category) const {
        switch (category) {
            case NumberFormatCategory::Date: {
                int y, m, d;
                DateTimeValue(Serial(value)).ToDate(y, m, d);
                cell->SetDate(y, m, d);
                break;
            }
            case NumberFormatCategory::DateTime:
                cell->SetDateTime(DateTimeValue(Serial(value)));
                break;
            case NumberFormatCategory::Time: {
                int h, m, s;
                DateTimeValue(value).ToTime(h, m, s);
                cell->SetTime(h, m, s);
                break;
            }
            case NumberFormatCategory::Percentage:
                cell->SetPercentage(value);
                break;
            case NumberFormatCategory::Currency:
                cell->SetCurrency(value);
                break;
            default:
                cell->SetNumber(value);
                break;
        }
    }

    // Whether the engine can take the translated formula as it is: it
    // parses, and every function and name in it exists here. Otherwise the
    // cell keeps Excel's result as a plain value - a formula that would
    // recalculate to #NAME? is worse than none.
    bool EngineAccepts(const XlsCell& c) const {
        SpreadsheetFormula formula(c.formula, CellAddress(c.row, c.col));
        if (!formula.Parse() || !formula.IsValid() || !formula.GetAST()) return false;
        SpreadsheetFormulaEngine* engine = spreadsheet_->GetFormulaEngine();
        if (!engine) return false;
        const FormulaFunctionLibrary& library = engine->GetFunctionLibrary();
        bool ok = true;
        std::function<void(const FormulaNode*)> walk = [&](const FormulaNode* node) {
            if (!node || !ok) return;
            if (node->nodeType == FormulaNodeType::FunctionCall &&
                !library.HasFunction(node->functionName))
                ok = false;
            if (node->nodeType == FormulaNodeType::NamedRef && !names_.count(node->namedRef))
                ok = false;
            for (const auto& child : node->children) walk(child.get());
        };
        walk(formula.GetAST());
        return ok;
    }

    void SetValue(SpreadsheetCell* cell, const XlsCell& c) const {
        switch (c.type) {
            case XlsValueType::Number:
                SetTypedNumber(cell, c.number, CategoryOf(c.format));
                break;
            case XlsValueType::Text:
                cell->SetText(c.text);
                break;
            case XlsValueType::Boolean:
                cell->SetBoolean(c.boolean);
                break;
            case XlsValueType::Error:
                cell->SetError(XlsErrorType(c.errorCode));
                break;
            default:
                break;
        }
    }

    void LoadCell(SpreadsheetSheet* sheet, const XlsCell& c) {
        const std::shared_ptr<CellStyle> style = StyleFor(c.format);
        const bool hasValue = c.type != XlsValueType::Empty || c.hasFormula;
        if (!hasValue && !style) return;
        SpreadsheetCell* cell = sheet->GetCell(c.row, c.col);
        if (!cell) return;
        if (c.hasFormula && !c.formula.empty() && EngineAccepts(c)) {
            cell->SetFormula(c.formula);
            // The plain setters would replace the formula: the cached result
            // goes in as the formula's result.
            switch (c.type) {
                case XlsValueType::Number:
                    cell->SetFormulaResult(c.number, CellValueType::Number);
                    break;
                case XlsValueType::Text:
                    cell->SetFormulaResult(c.text, CellValueType::Text);
                    break;
                case XlsValueType::Boolean:
                    cell->SetFormulaResult(c.boolean, CellValueType::Boolean);
                    break;
                case XlsValueType::Error:
                    cell->SetFormulaResult(XlsErrorType(c.errorCode), CellValueType::Error);
                    break;
                default:
                    cell->SetFormulaResult(std::string(), CellValueType::Text);
                    break;
            }
        } else {
            SetValue(cell, c);
        }
        if (style) cell->SetStyleShared(style);
    }

    void LoadSheet(const XlsSheet& s, SpreadsheetSheet* sheet) {
        if (s.defaultColumnWidthChars > 0 && s.defaultColumnWidthChars != 8) {
            // DEFCOLWIDTH counts characters without the cell padding.
            sheet->SetDefaultColumnWidth(
                static_cast<int>(s.defaultColumnWidthChars * kPixelsPerWidthChar + 5.5));
        }
        for (const XlsColumnInfo& c : s.columns) {
            const int pixels = static_cast<int>(c.widthChars * kPixelsPerWidthChar + 0.5);
            const int first = std::max(0, c.firstColumn);
            const int last = std::min(c.lastColumn, SpreadsheetLimits::MaxColumns - 1);
            if (last < first) continue;
            // A run to the sheet's last column (IV) is how some writers state
            // the width of every column not otherwise sized: a default, not
            // two hundred column definitions.
            if (c.lastColumn >= 255 && last - first + 1 > 16) {
                if (pixels > 0 && !c.hidden) sheet->SetDefaultColumnWidth(pixels);
                continue;
            }
            for (int col = first; col <= last; ++col) {
                if (pixels > 0) sheet->SetColumnWidth(col, pixels, c.customWidth);
                if (c.hidden) sheet->SetColumnHidden(col, true);
            }
        }
        for (const XlsRowInfo& r : s.rows) {
            if (r.row < 0 || r.row >= SpreadsheetLimits::MaxRows) continue;
            if (r.customHeight && r.heightPoints > 0) {
                sheet->SetRowHeight(r.row, static_cast<int>(r.heightPoints * kPixelsPerPoint + 0.5));
            }
            if (r.hidden) sheet->SetRowHidden(r.row, true);
        }
        for (const XlsCell& c : s.cells) {
            if (c.row < 0 || c.row >= SpreadsheetLimits::MaxRows || c.col < 0 ||
                c.col >= SpreadsheetLimits::MaxColumns)
                continue;
            LoadCell(sheet, c);
        }
        for (const XlsMergedRange& m : s.merges) {
            if (m.lastRow > m.firstRow || m.lastColumn > m.firstColumn) {
                sheet->MergeCells(m.firstRow, m.firstColumn, m.lastRow, m.lastColumn);
            }
        }
    }
};

} // namespace

bool UltraCanvasSpreadsheet::LoadXLS(const std::string& filePath) {
    lastError_.clear();
    switch (DetectXlsFileKind(filePath)) {
        case XlsFileKind::Missing:
            lastError_ = "Cannot open file: " + filePath;
            return false;
        case XlsFileKind::OpenXml:
            // An .xlsx under the old name - Excel opens it, and so do we.
            return LoadXLSX(filePath);
        case XlsFileKind::Text:
            // Delimited text: the CSV reader detects the encoding and separator.
            return LoadCSV(filePath);
        case XlsFileKind::Unknown:
            lastError_ = "The file is not an Excel workbook (.xls): " + filePath;
            return false;
        case XlsFileKind::Html:
        case XlsFileKind::XmlSpreadsheet:
        case XlsFileKind::Biff:
            break;
    }
    XlsWorkbook workbook;
    if (!ReadXlsWorkbook(filePath, workbook, lastError_)) return false;
    XlsWorkbookLoader loader(this, workbook);
    loader.Load();
    Recalculate();
    RequestAutoFitForUnsizedColumns();
    Invalidate();
    return true;
}

} // namespace UltraCanvas
