// core/UltraCanvasSpreadsheetXlsxIO.cpp
// Excel 2007+ (.xlsx, SpreadsheetML/OPC) load and save for
// UltraCanvasSpreadsheet — implements the LoadXLSX/SaveXLSX entry points
// declared in UltraCanvasSpreadsheet.h. Mirrors the ODS loader/saver in
// UltraCanvasSpreadsheetFileIO.cpp: multi-sheet, typed values (numbers,
// text via shared strings, booleans, errors, dates/times/percentages/
// currency mapped through number formats), formulas, merged cells, column
// widths/row heights and the same CellStyle subset (font, solid fill,
// borders, alignment, wrap). Number formats are classified by
// ExcelNumberFormatCategory (UltraCanvasSpreadsheetXls.h), shared with the
// .xls reader.
// Version: 1.6.0 - conditional formats (conditionalFormatting, dxfs), data validations (dataValidations),
//                  hyperlinks (with their relationships) and comments (the comments part and the legacy
//                  VML drawing Excel shows them with) read and written; one relationships part per sheet
//                  for its links, comments and charts
// Version: 1.5.0 - charts: a sheet's drawing with its charts read and written (UltraCanvasSpreadsheetChartIO.cpp)
// Version: 1.4.0 - a cell's number is written to read back exactly (it kept six significant digits)
// Version: 1.3.0 - keeps what a sheet holds beyond its cells: frozen panes (sheetViews), hidden rows and
//                  columns, named ranges of the workbook and of each sheet (definedNames), the
//                  auto-filter (autoFilter, _xlnm._FilterDatabase, value filters), and the print area
//                  and print settings (_xlnm.Print_Area, _xlnm.Print_Titles, printOptions, pageMargins,
//                  pageSetup, headerFooter, rowBreaks / colBreaks); worksheet numbers written dot-decimal
// Version: 1.2.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"           // the sheets' charts
#include "UltraCanvasSpreadsheetXls.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasSpreadsheetExcelFormula.h"   // Excel's Sheet!A1 <-> 'Sheet'.A1
#include "UltraCanvasTextUtils.h"                  // TryParseFloat / FormatFloatClassic

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {

namespace {

// ===== SMALL HELPERS =====

const char* Attr(const tinyxml2::XMLElement* e, const char* name) {
    const char* v = e->Attribute(name);
    return v ? v : "";
}

// "FFRRGGBB" / "RRGGBB" (ARGB as Excel writes it) -> Color
Color ArgbToColor(const std::string& hex) {
    if (hex.size() != 8 && hex.size() != 6) return Colors::Black;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    size_t start = hex.size() == 8 ? 2 : 0;
    auto byteAt = [&](size_t i) {
        return static_cast<uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1]));
    };
    return Color(byteAt(start), byteAt(start + 2), byteAt(start + 4));
}

std::string ColorToArgb(const Color& color) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "FF%02X%02X%02X", color.r, color.g, color.b);
    return buffer;
}

std::string XmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// "BC23" -> row 22, col 54 (0-based). Returns false on malformed refs.
bool ParseCellRef(const std::string& ref, int& row, int& col) {
    col = 0;
    size_t pos = 0;
    while (pos < ref.size() && std::isalpha(static_cast<unsigned char>(ref[pos]))) {
        col = col * 26 + (std::toupper(static_cast<unsigned char>(ref[pos])) - 'A' + 1);
        ++pos;
    }
    if (pos == 0 || pos >= ref.size()) return false;
    row = std::atoi(ref.c_str() + pos);
    if (row <= 0 || col <= 0) return false;
    --row;
    --col;
    return true;
}

std::string ColumnLetters(int col) {
    std::string letters;
    ++col;   // 1-based for the base-26 bijective encoding
    while (col > 0) {
        int remainder = (col - 1) % 26;
        letters.insert(letters.begin(), static_cast<char>('A' + remainder));
        col = (col - 1) / 26;
    }
    return letters;
}

std::string CellRef(int row, int col) {
    return ColumnLetters(col) + std::to_string(row + 1);
}

const char* BorderStyleName(BorderStyle style) {
    switch (style) {
        case BorderStyle::Thin: return "thin";
        case BorderStyle::Medium: return "medium";
        case BorderStyle::Thick: return "thick";
        case BorderStyle::Dashed: return "dashed";
        case BorderStyle::Dotted: return "dotted";
        case BorderStyle::Double: return "double";
        case BorderStyle::Hair: return "hair";
        case BorderStyle::MediumDashed: return "mediumDashed";
        case BorderStyle::DashDot: return "dashDot";
        case BorderStyle::MediumDashDot: return "mediumDashDot";
        case BorderStyle::DashDotDot: return "dashDotDot";
        default: return nullptr;
    }
}

BorderStyle BorderStyleFromName(const std::string& name) {
    if (name == "thin") return BorderStyle::Thin;
    if (name == "medium") return BorderStyle::Medium;
    if (name == "thick") return BorderStyle::Thick;
    if (name == "dashed") return BorderStyle::Dashed;
    if (name == "dotted") return BorderStyle::Dotted;
    if (name == "double") return BorderStyle::Double;
    if (name == "hair") return BorderStyle::Hair;
    if (name == "mediumDashed") return BorderStyle::MediumDashed;
    if (name == "dashDot") return BorderStyle::DashDot;
    if (name == "mediumDashDot") return BorderStyle::MediumDashDot;
    if (name == "dashDotDot") return BorderStyle::DashDotDot;
    if (name.empty() || name == "none") return BorderStyle::NoBorder;
    return BorderStyle::Thin;
}

// ===== DEFINED NAMES, PRINT SETTINGS =====

// A number attribute read dot-decimal whatever the locale (tinyxml2's
// DoubleAttribute goes through sscanf, which reads LC_NUMERIC).
double AttrNumber(const tinyxml2::XMLElement* e, const char* name, double fallback) {
    const char* v = e->Attribute(name);
    double value = fallback;
    if (v) TryParseFloat(v, value);
    return value;
}

bool AttrFlag(const tinyxml2::XMLElement* e, const char* name) {
    std::string v = Attr(e, name);
    return v == "1" || v == "true";
}

bool SameSheetName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// "Sheet1!" or "'Sales 2024'!": quoted unless the name is a plain identifier
// that cannot be read as a cell reference.
std::string ExcelSheetPrefix(const std::string& name) {
    bool plain = !name.empty() && !std::isdigit(static_cast<unsigned char>(name[0]));
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) { plain = false; break; }
    }
    if (plain) {
        // "AB12" or "R1C1" would read as a reference.
        size_t i = 0;
        while (i < name.size() && std::isalpha(static_cast<unsigned char>(name[i]))) ++i;
        bool digitsAfter = i > 0 && i < name.size();
        for (size_t j = i; j < name.size() && digitsAfter; ++j) {
            if (!std::isdigit(static_cast<unsigned char>(name[j]))) digitsAfter = false;
        }
        if (digitsAfter || (name.size() >= 2 && (name[0] == 'R' || name[0] == 'r') &&
                            std::isdigit(static_cast<unsigned char>(name[1])))) {
            plain = false;
        }
    }
    return (plain ? name : ExcelFormulaDetail::QuoteSheet(name)) + "!";
}

std::string AbsoluteCell(int row, int col) {
    return "$" + ColumnLetters(col) + "$" + std::to_string(row + 1);
}

// 'Sheet'!$A$1:$B$4 (or $A$1 for one cell); no prefix when `sheet` is empty.
std::string ExcelArea(const std::string& sheet, const CellRange& range) {
    std::string out = sheet.empty() ? std::string() : ExcelSheetPrefix(sheet);
    out += AbsoluteCell(range.start.row, range.start.col);
    if (!range.IsSingleCell()) out += ":" + AbsoluteCell(range.end.row, range.end.col);
    return out;
}

// One area of a defined name: Sheet1!$A$1:$B$4, 'My sheet'!$A$1, $1:$3 (whole
// rows), $A:$C (whole columns).
struct ExcelAreaRef {
    std::string sheet;
    CellRange range;
    bool wholeRows = false;
    bool wholeColumns = false;
};

// "[$]COL[$]ROW", "[$]COL" or "[$]ROW" -> row / col, -1 for the part absent.
bool ParseAreaCorner(std::string text, int& row, int& col) {
    size_t bang = text.rfind('!');
    if (bang != std::string::npos) text = text.substr(bang + 1);   // Sheet1!A1:Sheet1!B2
    row = col = -1;
    size_t i = 0;
    if (i < text.size() && text[i] == '$') ++i;
    int letters = 0, c = 0;
    while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) {
        c = c * 26 + (std::toupper(static_cast<unsigned char>(text[i])) - 'A' + 1);
        ++i;
        if (++letters > 3) return false;
    }
    if (i < text.size() && text[i] == '$') ++i;
    int digits = 0;
    long r = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        r = r * 10 + (text[i] - '0');
        ++i;
        if (++digits > 7) return false;
    }
    if (i != text.size() || (letters == 0 && digits == 0)) return false;
    if (letters > 0) col = c - 1;
    if (digits > 0) {
        if (r < 1) return false;
        row = static_cast<int>(r) - 1;
    }
    return true;
}

bool ParseExcelArea(const std::string& text, ExcelAreaRef& out) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    std::string rest;
    if (i < text.size() && text[i] == '\'') {
        size_t after = ExcelFormulaDetail::ReadQuoted(text, i, out.sheet);
        if (after == std::string::npos || after >= text.size() || text[after] != '!') return false;
        rest = text.substr(after + 1);
    } else {
        size_t bang = text.find('!', i);
        if (bang != std::string::npos) {
            out.sheet = text.substr(i, bang - i);
            rest = text.substr(bang + 1);
        } else {
            rest = text.substr(i);
        }
    }
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) rest.pop_back();
    size_t colon = rest.find(':');
    int r1, c1, r2, c2;
    if (!ParseAreaCorner(rest.substr(0, colon), r1, c1)) return false;
    if (colon == std::string::npos) {
        r2 = r1;
        c2 = c1;
    } else if (!ParseAreaCorner(rest.substr(colon + 1), r2, c2)) {
        return false;
    }
    if ((r1 < 0) != (r2 < 0) || (c1 < 0) != (c2 < 0)) return false;
    out.wholeColumns = r1 < 0;
    out.wholeRows = c1 < 0;
    if (out.wholeColumns && out.wholeRows) return false;
    if (out.wholeColumns) { r1 = 0; r2 = SpreadsheetLimits::MaxRows - 1; }
    if (out.wholeRows) { c1 = 0; c2 = SpreadsheetLimits::MaxColumns - 1; }
    out.range = CellRange(r1, c1, r2, c2);
    if (!out.range.IsValid()) return false;
    out.range.start.sheetName = out.range.end.sheetName = out.sheet;
    return true;
}

// A defined name's areas, split on the commas between them (not those in a
// quoted sheet name).
std::vector<std::string> SplitExcelAreas(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    for (char c : text) {
        if (c == '\'') quoted = !quoted;
        if (c == ',' && !quoted) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// Excel's paperSize numbers for the sizes PrintSettings names.
int ExcelPaperSize(PaperSize size) {
    switch (size) {
        case PaperSize::Letter: return 1;
        case PaperSize::Legal: return 5;
        case PaperSize::A3: return 8;
        case PaperSize::A4: return 9;
        case PaperSize::A5: return 11;
        case PaperSize::B4: return 12;
        case PaperSize::B5: return 13;
        default: return 0;
    }
}

PaperSize PaperSizeFromExcel(int code) {
    switch (code) {
        case 1: return PaperSize::Letter;
        case 5: return PaperSize::Legal;
        case 8: return PaperSize::A3;
        case 9: return PaperSize::A4;
        case 11: return PaperSize::A5;
        case 12: return PaperSize::B4;
        case 13: return PaperSize::B5;
        default: return PaperSize::Custom;
    }
}

// "&LLeft&CCentre&RRight" -> its three sections, each kept in Excel's codes.
// Text before any section code is Excel's centre section.
void SplitHeaderFooter(const std::string& text, std::string& left, std::string& center,
                       std::string& right) {
    std::string* section = &center;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&' || i + 1 >= text.size()) {
            section->push_back(text[i]);
            continue;
        }
        char code = text[i + 1];
        if (code == 'L' || code == 'C' || code == 'R') {
            section = code == 'L' ? &left : code == 'C' ? &center : &right;
            ++i;
        } else if (code == '"') {
            // &"Font,Style" is copied whole, so a '&' inside starts no section.
            size_t close = text.find('"', i + 2);
            size_t end = close == std::string::npos ? text.size() : close + 1;
            section->append(text, i, end - i);
            i = end - 1;
        } else {
            section->push_back('&');
            section->push_back(code);
            ++i;
        }
    }
}

std::string JoinHeaderFooter(const std::string& left, const std::string& center,
                             const std::string& right) {
    std::string out;
    if (!left.empty()) out += "&L" + left;
    if (!center.empty()) out += "&C" + center;
    if (!right.empty()) out += "&R" + right;
    return out;
}

// Excel column width is in "characters" of the default font; the grid model
// stores pixels. ~7px per character matches the default Calibri 11 metric.
constexpr double kPixelsPerWidthChar = 7.0;
// A <col> run wider than this is a sheet-wide default, not per-column sizing.
constexpr int kWideColumnRun = 512;
constexpr double kPixelsPerPoint = 1.33;

// ===== CONDITIONAL FORMATS, VALIDATIONS, LINKS, COMMENTS =====

std::string TrimmedText(const std::string& text) {
    const size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

bool IsPlainNumber(const std::string& text) {
    double value = 0.0;
    const std::string t = TrimmedText(text);
    if (t.empty()) return false;
    const char* first = t.c_str();
    const char* last = first + t.size();
    if (*first == '+') ++first;
    return first != last && ParseFloatClassic(first, last, value) == last;
}

bool IsQuotedText(const std::string& text) {
    const std::string t = TrimmedText(text);
    return t.size() >= 2 && t.front() == '"' && t.back() == '"';
}

std::string QuoteExcelText(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        out += c;
        if (c == '"') out += '"';
    }
    return out + "\"";
}

// A rule's operand (a number, "quoted text", a formula "=$B$1", or bare
// text) as Excel writes it in <formula>: no '=', text quoted.
std::string ExcelOperand(const std::string& operand) {
    const std::string t = TrimmedText(operand);
    if (t.empty() || IsPlainNumber(t) || IsQuotedText(t)) return t;
    if (t[0] == '=') return NativeFormulaToExcel(t).substr(1);
    return QuoteExcelText(t);
}

// A formula of ours ("=A1>3", or without its '=') as Excel's <formula> text.
std::string ExcelFormulaBody(const std::string& formula) {
    std::string t = TrimmedText(formula);
    if (t.empty()) return t;
    if (t[0] != '=') t = "=" + t;
    return NativeFormulaToExcel(t).substr(1);
}

// <formula> text read back: a number or quoted text as it is, anything else
// a formula of ours ("=...").
std::string OperandFromExcel(const std::string& excel) {
    const std::string t = TrimmedText(excel);
    if (t.empty() || IsPlainNumber(t) || IsQuotedText(t)) return t;
    std::string native = ExcelFormulaToNative(t);
    if (native.empty() || native[0] != '=') native = "=" + native;
    return native;
}

const char* ExcelOperatorName(ValidationOperator op) {
    switch (op) {
        case ValidationOperator::Between: return "between";
        case ValidationOperator::NotBetween: return "notBetween";
        case ValidationOperator::Equal: return "equal";
        case ValidationOperator::NotEqual: return "notEqual";
        case ValidationOperator::GreaterThan: return "greaterThan";
        case ValidationOperator::LessThan: return "lessThan";
        case ValidationOperator::GreaterOrEqual: return "greaterThanOrEqual";
        case ValidationOperator::LessOrEqual: return "lessThanOrEqual";
    }
    return "between";
}

ValidationOperator OperatorFromExcel(const std::string& name, ValidationOperator fallback) {
    if (name == "between") return ValidationOperator::Between;
    if (name == "notBetween") return ValidationOperator::NotBetween;
    if (name == "equal") return ValidationOperator::Equal;
    if (name == "notEqual") return ValidationOperator::NotEqual;
    if (name == "greaterThan") return ValidationOperator::GreaterThan;
    if (name == "lessThan") return ValidationOperator::LessThan;
    if (name == "greaterThanOrEqual") return ValidationOperator::GreaterOrEqual;
    if (name == "lessThanOrEqual") return ValidationOperator::LessOrEqual;
    return fallback;
}

const char* ExcelThresholdType(ConditionalThresholdKind kind) {
    switch (kind) {
        case ConditionalThresholdKind::Lowest: return "min";
        case ConditionalThresholdKind::Highest: return "max";
        case ConditionalThresholdKind::Number: return "num";
        case ConditionalThresholdKind::Percent: return "percent";
        case ConditionalThresholdKind::Percentile: return "percentile";
        case ConditionalThresholdKind::Formula: return "formula";
    }
    return "min";
}

ConditionalThreshold ThresholdFromExcel(const tinyxml2::XMLElement* cfvo) {
    const std::string type = Attr(cfvo, "type");
    const std::string val = Attr(cfvo, "val");
    if (type == "max" || type == "autoMax") return ConditionalThreshold(ConditionalThresholdKind::Highest);
    if (type == "num") {
        return IsPlainNumber(val) ? ConditionalThreshold(ConditionalThresholdKind::Number, val)
                                  : ConditionalThreshold(ConditionalThresholdKind::Formula, OperandFromExcel(val));
    }
    if (type == "percent") return ConditionalThreshold(ConditionalThresholdKind::Percent, val);
    if (type == "percentile") return ConditionalThreshold(ConditionalThresholdKind::Percentile, val);
    if (type == "formula") return ConditionalThreshold(ConditionalThresholdKind::Formula, OperandFromExcel(val));
    return ConditionalThreshold(ConditionalThresholdKind::Lowest);
}

const char* ExcelValidationType(ValidationType type) {
    switch (type) {
        case ValidationType::WholeNumber: return "whole";
        case ValidationType::Decimal: return "decimal";
        case ValidationType::List: return "list";
        case ValidationType::Date: return "date";
        case ValidationType::Time: return "time";
        case ValidationType::TextLength: return "textLength";
        case ValidationType::Custom: return "custom";
        default: return "none";
    }
}

ValidationType ValidationTypeFromExcel(const std::string& name) {
    if (name == "whole") return ValidationType::WholeNumber;
    if (name == "decimal") return ValidationType::Decimal;
    if (name == "list") return ValidationType::List;
    if (name == "date") return ValidationType::Date;
    if (name == "time") return ValidationType::Time;
    if (name == "textLength") return ValidationType::TextLength;
    if (name == "custom") return ValidationType::Custom;
    return ValidationType::Any;
}

// "A1:B4 D2" -> the ranges; a single cell is a one-cell range.
std::vector<CellRange> ParseSqref(const std::string& sqref) {
    std::vector<CellRange> ranges;
    std::istringstream in(sqref);
    std::string part;
    while (in >> part) {
        const size_t colon = part.find(':');
        int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
        if (colon == std::string::npos) {
            if (ParseCellRef(part, r1, c1)) ranges.emplace_back(r1, c1, r1, c1);
        } else if (ParseCellRef(part.substr(0, colon), r1, c1) &&
                   ParseCellRef(part.substr(colon + 1), r2, c2)) {
            ranges.emplace_back(r1, c1, r2, c2);
        }
    }
    return ranges;
}

std::string RangeRef(const CellRange& range) {
    if (range.IsSingleCell()) return CellRef(range.start.row, range.start.col);
    return CellRef(range.start.row, range.start.col) + ":" + CellRef(range.end.row, range.end.col);
}

// A part's path inside the package from a relationship target, relative to
// the folder of the part that names it ("xl/worksheets" + "../comments1.xml").
std::string ResolvePartPath(const std::string& fromFolder, const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::vector<std::string> parts;
    auto push = [&](const std::string& path) {
        std::string piece;
        std::istringstream in(path);
        while (std::getline(in, piece, '/')) {
            if (piece.empty() || piece == ".") continue;
            if (piece == "..") { if (!parts.empty()) parts.pop_back(); continue; }
            parts.push_back(piece);
        }
    };
    push(fromFolder);
    push(target);
    std::string out;
    for (const auto& piece : parts) out += (out.empty() ? "" : "/") + piece;
    return out;
}

// ===== XLSX LOADER =====

class XlsxLoader {
public:
    explicit XlsxLoader(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {}

    bool Load(const std::string& filePath, const std::string& password, std::string& error) {
        // A password-protected workbook is a compound file holding the
        // package encrypted (UltraCanvasOfficeEncryption.h), not a ZIP.
        std::vector<uint8_t> package;
        switch (DecryptOfficePackageFile(filePath, password, package, error)) {
            case OfficeDecryptStatus::NotEncrypted:
                if (!zip_.Open(filePath)) {
                    error = "The file is not a valid Excel workbook (.xlsx): " + filePath;
                    return false;
                }
                break;
            case OfficeDecryptStatus::Decrypted:
                if (!zip_.OpenFromMemory(std::move(package))) {
                    error = "The decrypted workbook is not a valid Excel workbook (.xlsx): " + filePath;
                    return false;
                }
                break;
            default:
                error += ": " + filePath;
                return false;
        }
        std::string workbookXml;
        if (!zip_.ReadEntry("xl/workbook.xml", workbookXml)) {
            error = "The package has no xl/workbook.xml — not an Excel workbook: " + filePath;
            return false;
        }
        LoadRelationships();
        LoadSharedStrings();
        LoadStyles();

        tinyxml2::XMLDocument workbook;
        if (workbook.Parse(workbookXml.c_str()) != tinyxml2::XML_SUCCESS) {
            error = "The workbook content is not valid XML: " + filePath;
            return false;
        }
        auto* root = workbook.FirstChildElement("workbook");
        auto* sheets = root ? root->FirstChildElement("sheets") : nullptr;
        if (!sheets) {
            error = "The workbook has no sheet list: " + filePath;
            return false;
        }

        while (spreadsheet_->GetSheetCount() > 0) {
            spreadsheet_->RemoveSheet(0);
        }
        // The names of the document that was open before go with it.
        for (const NamedRange& named : spreadsheet_->GetNamedRangeList()) {
            spreadsheet_->RemoveNamedRange(named.name, named.sheetScope);
        }
        // By position in <sheets>, which is what a defined name's
        // localSheetId counts; null where a sheet could not be read.
        std::vector<SpreadsheetSheet*> sheetsByPosition;
        for (auto* sheetElem = sheets->FirstChildElement("sheet"); sheetElem;
             sheetElem = sheetElem->NextSiblingElement("sheet")) {
            sheetsByPosition.push_back(nullptr);
            std::string name = Attr(sheetElem, "name");
            std::string relId = Attr(sheetElem, "r:id");
            auto rel = relationships_.find(relId);
            if (rel == relationships_.end()) continue;
            SpreadsheetSheet* sheet = spreadsheet_->AddSheet(name);
            if (!sheet) continue;
            sheetsByPosition.back() = sheet;
            std::string sheetXml;
            if (zip_.ReadEntry(rel->second, sheetXml)) {
                currentSheetPath_ = rel->second;
                ParseWorksheet(sheetXml, sheet);
                ReadXlsxSheetCharts(zip_, rel->second, *sheet);
            }
        }
        if (auto* definedNames = root->FirstChildElement("definedNames")) {
            LoadDefinedNames(definedNames, sheetsByPosition);
        }
        if (spreadsheet_->GetSheetCount() == 0) {
            spreadsheet_->AddSheet("Sheet1");
        }
        spreadsheet_->SetActiveSheet(0);
        return true;
    }

private:
    UltraCanvasSpreadsheet* spreadsheet_;
    UCZipPackageReader zip_;
    std::map<std::string, std::string> relationships_;   // rId -> package path
    std::vector<std::string> sharedStrings_;
    std::vector<CellStyle> cellStyles_;                  // per cellXfs index
    std::vector<NumberFormatCategory> xfCategories_;

    // Named ranges (scoped to a sheet by localSheetId), and the built-in names
    // that carry a sheet's print area and print titles. _xlnm._FilterDatabase
    // only repeats the sheet's <autoFilter>, which is read with the sheet; a
    // name that is a formula or a constant rather than a range has no place in
    // the model and is passed over.
    void LoadDefinedNames(tinyxml2::XMLElement* definedNames,
                          const std::vector<SpreadsheetSheet*>& sheetsByPosition) {
        for (auto* dn = definedNames->FirstChildElement("definedName"); dn;
             dn = dn->NextSiblingElement("definedName")) {
            const std::string name = Attr(dn, "name");
            const std::string text = dn->GetText() ? dn->GetText() : "";
            SpreadsheetSheet* scopeSheet = nullptr;
            if (const char* local = dn->Attribute("localSheetId")) {
                int index = std::atoi(local);
                if (index < 0 || index >= static_cast<int>(sheetsByPosition.size()) ||
                    !sheetsByPosition[static_cast<size_t>(index)]) {
                    continue;   // scoped to a sheet that is not there
                }
                scopeSheet = sheetsByPosition[static_cast<size_t>(index)];
            }
            if (name.empty() || text.empty()) continue;

            if (name.rfind("_xlnm.", 0) == 0) {
                if (!scopeSheet) continue;
                PrintSettings& print = scopeSheet->GetPrintSettings();
                if (name == "_xlnm.Print_Area") {
                    std::vector<std::string> areas = SplitExcelAreas(text);
                    ExcelAreaRef area;
                    if (!areas.empty() && ParseExcelArea(areas.front(), area)) {
                        CellRange range = area.range;
                        range.start.sheetName.clear();
                        range.end.sheetName.clear();
                        scopeSheet->SetPrintArea(range);
                    }
                } else if (name == "_xlnm.Print_Titles") {
                    for (const std::string& part : SplitExcelAreas(text)) {
                        ExcelAreaRef area;
                        if (!ParseExcelArea(part, area)) continue;
                        if (area.wholeRows) {
                            print.repeatRowsFirst = area.range.start.row;
                            print.repeatRowsLast = area.range.end.row;
                        } else if (area.wholeColumns) {
                            print.repeatColumnsFirst = area.range.start.col;
                            print.repeatColumnsLast = area.range.end.col;
                        }
                    }
                }
                continue;
            }

            std::vector<std::string> areas = SplitExcelAreas(text);
            ExcelAreaRef area;
            if (areas.size() != 1 || !ParseExcelArea(areas.front(), area)) continue;
            // A name into another workbook ([1]Sheet1!A1) has no sheet here.
            if (!area.sheet.empty() && !spreadsheet_->GetSheetByName(area.sheet)) continue;
            spreadsheet_->AddNamedRange(name, area.range,
                                        scopeSheet ? scopeSheet->GetName() : std::string());
        }
    }

    void LoadRelationships() {
        std::string relsXml;
        if (!zip_.ReadEntry("xl/_rels/workbook.xml.rels", relsXml)) return;
        tinyxml2::XMLDocument rels;
        if (rels.Parse(relsXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = rels.FirstChildElement("Relationships");
        if (!root) return;
        for (auto* rel = root->FirstChildElement("Relationship"); rel;
             rel = rel->NextSiblingElement("Relationship")) {
            std::string target = Attr(rel, "Target");
            if (target.empty()) continue;
            std::string path = (target[0] == '/') ? target.substr(1) : "xl/" + target;
            relationships_[Attr(rel, "Id")] = path;
        }
    }

    // Shared strings may be plain (<si><t>) or rich (<si><r><t>...); rich
    // runs are flattened to their concatenated text.
    void LoadSharedStrings() {
        std::string sstXml;
        if (!zip_.ReadEntry("xl/sharedStrings.xml", sstXml)) return;
        tinyxml2::XMLDocument sst;
        if (sst.Parse(sstXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = sst.FirstChildElement("sst");
        if (!root) return;
        for (auto* si = root->FirstChildElement("si"); si;
             si = si->NextSiblingElement("si")) {
            std::string text;
            CollectTextElements(si, text);
            sharedStrings_.push_back(std::move(text));
        }
    }

    static void CollectTextElements(tinyxml2::XMLElement* parent, std::string& out) {
        for (auto* child = parent->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            std::string tag = child->Name() ? child->Name() : "";
            if (tag == "t") {
                if (child->GetText()) out += child->GetText();
            } else if (tag == "r") {
                CollectTextElements(child, out);
            }
        }
    }

    void LoadStyles() {
        std::string stylesXml;
        if (!zip_.ReadEntry("xl/styles.xml", stylesXml)) return;
        tinyxml2::XMLDocument styles;
        if (styles.Parse(stylesXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = styles.FirstChildElement("styleSheet");
        if (!root) return;
        LoadDifferentialStyles(root);

        std::map<int, std::string> customFormats;
        if (auto* numFmts = root->FirstChildElement("numFmts")) {
            for (auto* fmt = numFmts->FirstChildElement("numFmt"); fmt;
                 fmt = fmt->NextSiblingElement("numFmt")) {
                customFormats[fmt->IntAttribute("numFmtId")] = Attr(fmt, "formatCode");
            }
        }

        std::vector<CellFont> fonts;
        if (auto* fontsElem = root->FirstChildElement("fonts")) {
            for (auto* fontElem = fontsElem->FirstChildElement("font"); fontElem;
                 fontElem = fontElem->NextSiblingElement("font")) {
                CellFont font;
                if (auto* name = fontElem->FirstChildElement("name")) {
                    font.family = Attr(name, "val");
                }
                if (auto* sz = fontElem->FirstChildElement("sz")) {
                    font.size = sz->FloatAttribute("val", font.size);
                }
                font.bold = fontElem->FirstChildElement("b") != nullptr;
                font.italic = fontElem->FirstChildElement("i") != nullptr;
                font.strikethrough = fontElem->FirstChildElement("strike") != nullptr;
                if (auto* u = fontElem->FirstChildElement("u")) {
                    std::string val = Attr(u, "val");
                    font.underline = (val == "double") ? UnderlineStyle::Double
                                                       : UnderlineStyle::Single;
                }
                if (auto* color = fontElem->FirstChildElement("color")) {
                    std::string rgb = Attr(color, "rgb");
                    if (!rgb.empty()) font.color = ArgbToColor(rgb);
                }
                fonts.push_back(font);
            }
        }

        std::vector<CellFill> fills;
        if (auto* fillsElem = root->FirstChildElement("fills")) {
            for (auto* fillElem = fillsElem->FirstChildElement("fill"); fillElem;
                 fillElem = fillElem->NextSiblingElement("fill")) {
                CellFill fill;
                if (auto* pattern = fillElem->FirstChildElement("patternFill")) {
                    if (std::string(Attr(pattern, "patternType")) == "solid") {
                        fill.pattern = FillPattern::Solid;
                        if (auto* fg = pattern->FirstChildElement("fgColor")) {
                            std::string rgb = Attr(fg, "rgb");
                            if (!rgb.empty()) fill.foregroundColor = ArgbToColor(rgb);
                        }
                    }
                }
                fills.push_back(fill);
            }
        }

        std::vector<CellBorders> borders;
        if (auto* bordersElem = root->FirstChildElement("borders")) {
            for (auto* borderElem = bordersElem->FirstChildElement("border"); borderElem;
                 borderElem = borderElem->NextSiblingElement("border")) {
                CellBorders b;
                auto parseSide = [&](const char* tag, CellBorder& side) {
                    auto* sideElem = borderElem->FirstChildElement(tag);
                    if (!sideElem) return;
                    side.style = BorderStyleFromName(Attr(sideElem, "style"));
                    if (auto* color = sideElem->FirstChildElement("color")) {
                        std::string rgb = Attr(color, "rgb");
                        if (!rgb.empty()) side.color = ArgbToColor(rgb);
                    }
                };
                parseSide("left", b.left);
                parseSide("right", b.right);
                parseSide("top", b.top);
                parseSide("bottom", b.bottom);
                borders.push_back(b);
            }
        }

        auto* cellXfs = root->FirstChildElement("cellXfs");
        if (!cellXfs) return;
        for (auto* xf = cellXfs->FirstChildElement("xf"); xf;
             xf = xf->NextSiblingElement("xf")) {
            CellStyle style;
            size_t fontId = static_cast<size_t>(xf->IntAttribute("fontId"));
            size_t fillId = static_cast<size_t>(xf->IntAttribute("fillId"));
            size_t borderId = static_cast<size_t>(xf->IntAttribute("borderId"));
            int numFmtId = xf->IntAttribute("numFmtId");
            if (fontId < fonts.size()) style.font = fonts[fontId];
            if (fillId < fills.size()) style.fill = fills[fillId];
            if (borderId < borders.size()) style.borders = borders[borderId];
            if (auto* alignment = xf->FirstChildElement("alignment")) {
                std::string h = Attr(alignment, "horizontal");
                if (h == "left") style.hAlign = HorizontalAlignment::Left;
                else if (h == "center") style.hAlign = HorizontalAlignment::Center;
                else if (h == "right") style.hAlign = HorizontalAlignment::Right;
                else if (h == "fill") style.hAlign = HorizontalAlignment::Fill;
                else if (h == "justify") style.hAlign = HorizontalAlignment::Justify;
                else if (h == "distributed") style.hAlign = HorizontalAlignment::Distributed;
                std::string v = Attr(alignment, "vertical");
                if (v == "top") style.vAlign = VerticalAlignment::Top;
                else if (v == "center") style.vAlign = VerticalAlignment::Middle;
                style.wrapText = alignment->BoolAttribute("wrapText", false);
            }
            std::string formatCode;
            auto custom = customFormats.find(numFmtId);
            if (custom != customFormats.end()) formatCode = custom->second;
            NumberFormatCategory category = ExcelNumberFormatCategory(numFmtId, formatCode);
            style.numberFormat.category = category;
            if (!formatCode.empty()) style.numberFormat.formatCode = formatCode;
            cellStyles_.push_back(style);
            xfCategories_.push_back(category);
        }
    }

    void ParseWorksheet(const std::string& xml, SpreadsheetSheet* sheet) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = doc.FirstChildElement("worksheet");
        if (!root) return;

        // Sheet-wide default column width, when the workbook overrides Excel's.
        if (auto* fmt = root->FirstChildElement("sheetFormatPr")) {
            double defaultWidth = fmt->DoubleAttribute("defaultColWidth", 0);
            if (defaultWidth > 0) {
                sheet->SetDefaultColumnWidth(
                    static_cast<int>(defaultWidth * kPixelsPerWidthChar + 0.5));
            }
            double defaultHeight = fmt->DoubleAttribute("defaultRowHeight", 0);
            if (defaultHeight > 0) {
                sheet->SetDefaultRowHeight(
                    static_cast<int>(defaultHeight * kPixelsPerPoint + 0.5));
            }
        }

        // Frozen panes: the pane's split is the number of frozen columns
        // (xSplit) and rows (ySplit). A plain split (state="split") is a view
        // setting the grid does not have.
        if (auto* views = root->FirstChildElement("sheetViews")) {
            auto* view = views->FirstChildElement("sheetView");
            auto* pane = view ? view->FirstChildElement("pane") : nullptr;
            if (pane) {
                std::string state = Attr(pane, "state");
                if (state == "frozen" || state == "frozenSplit") {
                    int cols = static_cast<int>(AttrNumber(pane, "xSplit", 0) + 0.5);
                    int rows = static_cast<int>(AttrNumber(pane, "ySplit", 0) + 0.5);
                    if (rows > 0 || cols > 0) sheet->SetFreezePanes(rows, cols);
                }
            }
        }

        if (auto* cols = root->FirstChildElement("cols")) {
            for (auto* col = cols->FirstChildElement("col"); col;
                 col = col->NextSiblingElement("col")) {
                int min = col->IntAttribute("min", 1) - 1;
                int max = col->IntAttribute("max", 1) - 1;
                if (AttrFlag(col, "hidden") && min >= 0) {
                    sheet->SetColumnsHidden(min, std::min(max, SpreadsheetLimits::MaxColumns - 1), true);
                }
                double width = AttrNumber(col, "width", 0);
                if (width <= 0) continue;
                // bestFit means Excel sized the column to its content rather
                // than the user choosing a width, so leave it to our own
                // auto-width pass, which measures the font actually in use.
                bool authored = col->BoolAttribute("customWidth", false) ||
                                !col->BoolAttribute("bestFit", false);
                int pixels = static_cast<int>(width * kPixelsPerWidthChar + 0.5);
                if (max >= SpreadsheetLimits::MaxColumns) max = SpreadsheetLimits::MaxColumns - 1;
                // A run spanning the whole sheet ("min=1 max=16384") is how
                // Excel states a sheet-wide default. Storing 16k identical
                // column definitions for it would cost more memory than the
                // data; record it as the default instead.
                if (max - min + 1 > kWideColumnRun) {
                    // Only a run starting at column A states the sheet default;
                    // a wide run further in is left to the auto-width pass
                    // rather than materialized column by column.
                    if (min <= 0) sheet->SetDefaultColumnWidth(pixels);
                    continue;
                }
                for (int c = std::max(0, min); c <= max; ++c) {
                    sheet->SetColumnWidth(c, pixels, authored);
                }
            }
        }

        if (auto* sheetData = root->FirstChildElement("sheetData")) {
            int rowIndex = 0;
            for (auto* row = sheetData->FirstChildElement("row"); row;
                 row = row->NextSiblingElement("row")) {
                rowIndex = row->IntAttribute("r", rowIndex + 1) - 1;
                double height = AttrNumber(row, "ht", 0);
                if (height > 0 && row->BoolAttribute("customHeight", false)) {
                    sheet->SetRowHeight(rowIndex,
                                        static_cast<int>(height * kPixelsPerPoint + 0.5));
                }
                if (AttrFlag(row, "hidden") && rowIndex >= 0 &&
                    rowIndex < SpreadsheetLimits::MaxRows) {
                    sheet->SetRowHidden(rowIndex, true);
                }
                int colIndex = 0;
                for (auto* c = row->FirstChildElement("c"); c;
                     c = c->NextSiblingElement("c")) {
                    int cellRow = rowIndex, cellCol = colIndex;
                    if (!ParseCellRef(Attr(c, "r"), cellRow, cellCol)) {
                        cellRow = rowIndex;
                        cellCol = colIndex;
                    }
                    ParseCell(c, sheet, cellRow, cellCol);
                    colIndex = cellCol + 1;
                }
            }
        }

        if (auto* mergeCells = root->FirstChildElement("mergeCells")) {
            for (auto* merge = mergeCells->FirstChildElement("mergeCell"); merge;
                 merge = merge->NextSiblingElement("mergeCell")) {
                std::string ref = Attr(merge, "ref");
                size_t colon = ref.find(':');
                if (colon == std::string::npos) continue;
                int r1, c1, r2, c2;
                if (ParseCellRef(ref.substr(0, colon), r1, c1)
                    && ParseCellRef(ref.substr(colon + 1), r2, c2)) {
                    sheet->MergeCells(r1, c1, r2, c2);
                }
            }
        }

        if (auto* autoFilter = root->FirstChildElement("autoFilter")) {
            ParseAutoFilter(autoFilter, sheet);
        }
        ParsePrintSettings(root, sheet);
        ParseSheetExtras(root, sheet);
    }

    // The filter range and its buttons, and the columns filtered to a list of
    // values (<filters><filter val=..>), which is what the grid's filter does.
    static void ParseAutoFilter(tinyxml2::XMLElement* autoFilter, SpreadsheetSheet* sheet) {
        ExcelAreaRef area;
        if (!ParseExcelArea(Attr(autoFilter, "ref"), area) || area.wholeRows || area.wholeColumns) {
            return;
        }
        CellRange range = area.range;
        range.start.sheetName.clear();
        range.end.sheetName.clear();
        sheet->SetAutoFilter(range);
        for (auto* column = autoFilter->FirstChildElement("filterColumn"); column;
             column = column->NextSiblingElement("filterColumn")) {
            auto* filters = column->FirstChildElement("filters");
            if (!filters) continue;
            ColumnFilter filter;
            filter.op = FilterOperator::Equals;
            for (auto* f = filters->FirstChildElement("filter"); f;
                 f = f->NextSiblingElement("filter")) {
                filter.values.push_back(Attr(f, "val"));
            }
            if (filter.values.empty()) continue;
            sheet->ApplyFilter(range.start.col + column->IntAttribute("colId", 0), filter);
        }
    }

    // printOptions, pageMargins, pageSetup (with sheetPr's fitToPage),
    // headerFooter and the manual page breaks, into the sheet's PrintSettings.
    // The print area and the print titles are defined names, read with them.
    static void ParsePrintSettings(tinyxml2::XMLElement* root, SpreadsheetSheet* sheet) {
        PrintSettings& print = sheet->GetPrintSettings();
        if (auto* options = root->FirstChildElement("printOptions")) {
            print.printGridlines = AttrFlag(options, "gridLines");
            print.printRowColHeaders = AttrFlag(options, "headings");
            print.centerHorizontally = AttrFlag(options, "horizontalCentered");
            print.centerVertically = AttrFlag(options, "verticalCentered");
        }
        if (auto* margins = root->FirstChildElement("pageMargins")) {
            print.marginLeft = static_cast<float>(AttrNumber(margins, "left", print.marginLeft));
            print.marginRight = static_cast<float>(AttrNumber(margins, "right", print.marginRight));
            print.marginTop = static_cast<float>(AttrNumber(margins, "top", print.marginTop));
            print.marginBottom = static_cast<float>(AttrNumber(margins, "bottom", print.marginBottom));
            print.headerMargin = static_cast<float>(AttrNumber(margins, "header", print.headerMargin));
            print.footerMargin = static_cast<float>(AttrNumber(margins, "footer", print.footerMargin));
        }
        bool fitToPage = false;
        if (auto* sheetPr = root->FirstChildElement("sheetPr")) {
            if (auto* setup = sheetPr->FirstChildElement("pageSetUpPr")) {
                fitToPage = AttrFlag(setup, "fitToPage");
            }
        }
        if (auto* setup = root->FirstChildElement("pageSetup")) {
            if (setup->Attribute("paperSize")) {
                print.paperSize = PaperSizeFromExcel(setup->IntAttribute("paperSize", 1));
            }
            std::string orientation = Attr(setup, "orientation");
            if (orientation == "landscape") print.orientation = PageOrientation::Landscape;
            else if (orientation == "portrait") print.orientation = PageOrientation::Portrait;
            int scale = setup->IntAttribute("scale", 100);
            if (scale >= 10 && scale <= 400) print.scale = scale;
            if (fitToPage) {
                print.fitToWidth = setup->IntAttribute("fitToWidth", 1);
                print.fitToHeight = setup->IntAttribute("fitToHeight", 1);
            }
        } else if (fitToPage) {
            print.fitToWidth = print.fitToHeight = 1;
        }
        if (auto* headerFooter = root->FirstChildElement("headerFooter")) {
            if (auto* header = headerFooter->FirstChildElement("oddHeader")) {
                SplitHeaderFooter(header->GetText() ? header->GetText() : "",
                                  print.headerLeft, print.headerCenter, print.headerRight);
            }
            if (auto* footer = headerFooter->FirstChildElement("oddFooter")) {
                SplitHeaderFooter(footer->GetText() ? footer->GetText() : "",
                                  print.footerLeft, print.footerCenter, print.footerRight);
            }
        }
        auto readBreaks = [&](const char* tag, std::vector<int>& breaks, int limit) {
            auto* list = root->FirstChildElement(tag);
            if (!list) return;
            for (auto* brk = list->FirstChildElement("brk"); brk; brk = brk->NextSiblingElement("brk")) {
                int id = brk->IntAttribute("id", 0);
                if (id > 0 && id < limit &&
                    std::find(breaks.begin(), breaks.end(), id) == breaks.end()) {
                    breaks.push_back(id);
                }
            }
            std::sort(breaks.begin(), breaks.end());
        };
        readBreaks("rowBreaks", print.rowBreaks, SpreadsheetLimits::MaxRows);
        readBreaks("colBreaks", print.colBreaks, SpreadsheetLimits::MaxColumns);
    }

    void ParseCell(tinyxml2::XMLElement* c, SpreadsheetSheet* sheet, int row, int col) {
        std::string type = Attr(c, "t");
        auto* v = c->FirstChildElement("v");
        auto* f = c->FirstChildElement("f");
        std::string value = (v && v->GetText()) ? v->GetText() : "";

        bool hasContent = f || !value.empty() || type == "inlineStr";
        int styleIndex = c->IntAttribute("s", -1);
        bool hasStyle = styleIndex >= 0
                        && styleIndex < static_cast<int>(cellStyles_.size());
        if (!hasContent && !hasStyle) return;

        SpreadsheetCell* cell = sheet->GetCell(row, col);
        if (!cell) return;

        bool hasFormula = false;
        if (f && f->GetText()) {
            // Excel's Data!B5 is the engine's 'Data'.B5 (and _xlfn.IFS its IFS).
            std::string formula = ExcelFormulaToNative(f->GetText());
            if (!formula.empty() && formula[0] != '=') formula = "=" + formula;
            cell->SetFormula(formula);
            hasFormula = true;
        }

        NumberFormatCategory category = NumberFormatCategory::General;
        if (hasStyle) category = xfCategories_[static_cast<size_t>(styleIndex)];

        if (hasFormula) {
            // The plain setters replace the cell content and would wipe the
            // formula; cached results go in via SetFormulaResult.
            if (type == "str" || type == "s" || type == "inlineStr") {
                std::string text = value;
                if (type == "s") {
                    size_t index = static_cast<size_t>(std::atoll(value.c_str()));
                    text = index < sharedStrings_.size() ? sharedStrings_[index] : "";
                }
                cell->SetFormulaResult(text, CellValueType::Text);
            } else if (type == "b") {
                cell->SetFormulaResult(value == "1" || value == "true",
                                       CellValueType::Boolean);
            } else if (!value.empty()) {
                cell->SetFormulaResult(std::atof(value.c_str()), CellValueType::Number);
            }
        } else if (type == "s") {
            size_t index = static_cast<size_t>(std::atoll(value.c_str()));
            cell->SetText(index < sharedStrings_.size() ? sharedStrings_[index] : "");
        } else if (type == "inlineStr") {
            std::string text;
            if (auto* is = c->FirstChildElement("is")) CollectTextElements(is, text);
            cell->SetText(text);
        } else if (type == "str") {
            if (!value.empty()) cell->SetText(value);
        } else if (type == "b") {
            cell->SetBoolean(value == "1" || value == "true");
        } else if (type == "e") {
            cell->SetText(value);   // keep the error string visible
        } else if (!value.empty()) {
            double number = std::atof(value.c_str());
            // The number format decides whether the serial is a date/time/etc.
            switch (category) {
                case NumberFormatCategory::Date: {
                    int y, m, d;
                    DateTimeValue(number).ToDate(y, m, d);
                    cell->SetDate(y, m, d);
                    break;
                }
                case NumberFormatCategory::DateTime: {
                    cell->SetDateTime(DateTimeValue(number));
                    break;
                }
                case NumberFormatCategory::Time: {
                    int h, m, s;
                    DateTimeValue(number).ToTime(h, m, s);
                    cell->SetTime(h, m, s);
                    break;
                }
                case NumberFormatCategory::Percentage:
                    cell->SetPercentage(number);
                    break;
                case NumberFormatCategory::Currency:
                    cell->SetCurrency(number);
                    break;
                default:
                    cell->SetNumber(number);
                    break;
            }
        }

        if (hasStyle) {
            const CellStyle& style = cellStyles_[static_cast<size_t>(styleIndex)];
            CellStyle defaults;
            bool meaningful = !(style.font == defaults.font)
                              || style.fill.HasFill() || style.borders.HasAnyBorder()
                              || style.hAlign != defaults.hAlign
                              || style.vAlign != defaults.vAlign || style.wrapText;
            if (meaningful) cell->SetStyle(style);
        }
    }
    // ===== CONDITIONAL FORMATS, VALIDATIONS, LINKS, COMMENTS =====

    std::vector<CellStyle> dxfStyles_;    // <dxfs>: what a conditional format's rule applies
    std::string currentSheetPath_;        // the worksheet part being read, for its relationships

    // A <dxf>: only what it sets - a fill, a font's colour, bold, italic,
    // underline, strikethrough - as ApplyConditionalStyle lays it over a cell.
    void LoadDifferentialStyles(tinyxml2::XMLElement* styleSheet) {
        auto* dxfs = styleSheet->FirstChildElement("dxfs");
        if (!dxfs) return;
        for (auto* dxf = dxfs->FirstChildElement("dxf"); dxf; dxf = dxf->NextSiblingElement("dxf")) {
            CellStyle style;
            if (auto* font = dxf->FirstChildElement("font")) {
                if (auto* b = font->FirstChildElement("b")) style.font.bold = std::string(Attr(b, "val")) != "0";
                if (auto* i = font->FirstChildElement("i")) style.font.italic = std::string(Attr(i, "val")) != "0";
                if (auto* strike = font->FirstChildElement("strike")) {
                    style.font.strikethrough = std::string(Attr(strike, "val")) != "0";
                }
                if (auto* u = font->FirstChildElement("u")) {
                    const std::string val = Attr(u, "val");
                    if (val != "none") {
                        style.font.underline = val == "double" ? UnderlineStyle::Double : UnderlineStyle::Single;
                    }
                }
                if (auto* color = font->FirstChildElement("color")) {
                    const std::string rgb = Attr(color, "rgb");
                    if (!rgb.empty()) style.font.color = ArgbToColor(rgb);
                }
            }
            if (auto* fill = dxf->FirstChildElement("fill")) {
                if (auto* pattern = fill->FirstChildElement("patternFill")) {
                    // A dxf's solid fill is its bgColor (fgColor in some writers).
                    std::string rgb;
                    if (auto* bg = pattern->FirstChildElement("bgColor")) rgb = Attr(bg, "rgb");
                    if (rgb.empty()) {
                        if (auto* fg = pattern->FirstChildElement("fgColor")) rgb = Attr(fg, "rgb");
                    }
                    if (!rgb.empty() && std::string(Attr(pattern, "patternType")) != "none") {
                        style.fill = CellFill::Solid(ArgbToColor(rgb));
                    }
                }
            }
            dxfStyles_.push_back(style);
        }
    }

    // The worksheet's relationships: id -> (type, part path or external target).
    std::map<std::string, std::pair<std::string, std::string>> LoadSheetRelationships() {
        std::map<std::string, std::pair<std::string, std::string>> rels;
        const size_t slash = currentSheetPath_.rfind('/');
        const std::string folder = slash == std::string::npos ? std::string() : currentSheetPath_.substr(0, slash);
        const std::string file = slash == std::string::npos ? currentSheetPath_ : currentSheetPath_.substr(slash + 1);
        std::string relsXml;
        if (!zip_.ReadEntry(folder + "/_rels/" + file + ".rels", relsXml)) return rels;
        tinyxml2::XMLDocument doc;
        if (doc.Parse(relsXml.c_str()) != tinyxml2::XML_SUCCESS) return rels;
        auto* root = doc.FirstChildElement("Relationships");
        if (!root) return rels;
        for (auto* rel = root->FirstChildElement("Relationship"); rel; rel = rel->NextSiblingElement("Relationship")) {
            const std::string target = Attr(rel, "Target");
            const bool external = std::string(Attr(rel, "TargetMode")) == "External";
            rels[Attr(rel, "Id")] = {Attr(rel, "Type"), external ? target : ResolvePartPath(folder, target)};
        }
        return rels;
    }

    void ParseSheetExtras(tinyxml2::XMLElement* root, SpreadsheetSheet* sheet) {
        for (auto* cf = root->FirstChildElement("conditionalFormatting"); cf;
             cf = cf->NextSiblingElement("conditionalFormatting")) {
            ParseConditionalFormatting(cf, sheet);
        }
        if (auto* validations = root->FirstChildElement("dataValidations")) {
            for (auto* dv = validations->FirstChildElement("dataValidation"); dv;
                 dv = dv->NextSiblingElement("dataValidation")) {
                ParseDataValidation(dv, sheet);
            }
        }
        const auto rels = LoadSheetRelationships();
        if (auto* links = root->FirstChildElement("hyperlinks")) {
            for (auto* link = links->FirstChildElement("hyperlink"); link; link = link->NextSiblingElement("hyperlink")) {
                CellHyperlink hyperlink;
                const std::string location = Attr(link, "location");
                auto rel = rels.find(Attr(link, "r:id"));
                if (rel != rels.end()) {
                    hyperlink.target = rel->second.second;
                    if (!location.empty()) hyperlink.target += "#" + location;
                    hyperlink.type = hyperlink.target.rfind("mailto:", 0) == 0 ? HyperlinkType::Email
                                   : hyperlink.target.rfind("file:", 0) == 0 ? HyperlinkType::File
                                                                             : HyperlinkType::URL;
                } else if (!location.empty()) {
                    hyperlink.target = ExcelFormulaToNative(location);
                    hyperlink.type = HyperlinkType::InternalCell;
                } else {
                    continue;
                }
                hyperlink.tooltip = Attr(link, "tooltip");
                hyperlink.displayText = Attr(link, "display");
                for (const CellRange& range : ParseSqref(Attr(link, "ref"))) {
                    for (int r = range.start.row; r <= range.end.row && r - range.start.row < 1000; ++r) {
                        for (int c = range.start.col; c <= range.end.col && c - range.start.col < 100; ++c) {
                            sheet->GetCell(r, c)->SetHyperlink(hyperlink);
                        }
                    }
                }
            }
        }
        for (const auto& [id, rel] : rels) {
            if (rel.first.size() >= 9 && rel.first.compare(rel.first.size() - 9, 9, "/comments") == 0) {
                ParseComments(rel.second, sheet);
            }
        }
    }

    void ParseConditionalFormatting(tinyxml2::XMLElement* cf, SpreadsheetSheet* sheet) {
        const std::vector<CellRange> ranges = ParseSqref(Attr(cf, "sqref"));
        if (ranges.empty()) return;
        std::vector<ConditionalFormatRule> rules;
        for (auto* cfRule = cf->FirstChildElement("cfRule"); cfRule; cfRule = cfRule->NextSiblingElement("cfRule")) {
            ConditionalFormatRule rule;
            const std::string type = Attr(cfRule, "type");
            rule.priority = cfRule->IntAttribute("priority", 0);
            rule.stopIfTrue = AttrFlag(cfRule, "stopIfTrue");
            const int dxfId = cfRule->IntAttribute("dxfId", -1);
            if (dxfId >= 0 && dxfId < static_cast<int>(dxfStyles_.size())) {
                rule.style = dxfStyles_[static_cast<size_t>(dxfId)];
            }
            std::vector<std::string> formulas;
            for (auto* f = cfRule->FirstChildElement("formula"); f; f = f->NextSiblingElement("formula")) {
                formulas.push_back(f->GetText() ? f->GetText() : "");
            }
            const std::string text = Attr(cfRule, "text");
            if (type == "cellIs") {
                rule.type = ConditionalFormatType::CellValue;
                rule.op = OperatorFromExcel(Attr(cfRule, "operator"), ValidationOperator::Equal);
                if (!formulas.empty()) rule.formula1 = OperandFromExcel(formulas[0]);
                if (formulas.size() > 1) rule.formula2 = OperandFromExcel(formulas[1]);
            } else if (type == "expression") {
                rule.type = ConditionalFormatType::Expression;
                if (!formulas.empty()) rule.formula1 = OperandFromExcel(formulas[0]);
                if (!rule.formula1.empty() && rule.formula1[0] != '=') rule.formula1 = "=" + rule.formula1;
            } else if (type == "containsText" || type == "notContainsText" ||
                       type == "beginsWith" || type == "endsWith") {
                rule.type = type == "containsText" ? ConditionalFormatType::ContainsText
                          : type == "notContainsText" ? ConditionalFormatType::NotContainsText
                          : type == "beginsWith" ? ConditionalFormatType::BeginsWithText
                                                 : ConditionalFormatType::EndsWithText;
                rule.formula1 = text;
            } else if (type == "containsBlanks") {
                rule.type = ConditionalFormatType::ContainsBlanks;
            } else if (type == "notContainsBlanks") {
                rule.type = ConditionalFormatType::NotContainsBlanks;
            } else if (type == "containsErrors") {
                rule.type = ConditionalFormatType::ContainsErrors;
            } else if (type == "notContainsErrors") {
                rule.type = ConditionalFormatType::NotContainsErrors;
            } else if (type == "top10") {
                rule.type = ConditionalFormatType::Top10;
                rule.formula1 = std::to_string(cfRule->IntAttribute("rank", 10));
                rule.percent = AttrFlag(cfRule, "percent");
                rule.bottom = AttrFlag(cfRule, "bottom");
            } else if (type == "aboveAverage") {
                rule.type = ConditionalFormatType::AboveAverage;
                rule.aboveAverage = std::string(Attr(cfRule, "aboveAverage")) != "0";
                rule.equalAverage = AttrFlag(cfRule, "equalAverage");
            } else if (type == "duplicateValues") {
                rule.type = ConditionalFormatType::DuplicateValues;
            } else if (type == "uniqueValues") {
                rule.type = ConditionalFormatType::UniqueValues;
            } else if (type == "colorScale") {
                auto* scale = cfRule->FirstChildElement("colorScale");
                if (!scale) continue;
                rule.type = ConditionalFormatType::ColorScale;
                for (auto* cfvo = scale->FirstChildElement("cfvo"); cfvo; cfvo = cfvo->NextSiblingElement("cfvo")) {
                    rule.thresholds.push_back(ThresholdFromExcel(cfvo));
                }
                for (auto* color = scale->FirstChildElement("color"); color; color = color->NextSiblingElement("color")) {
                    const std::string rgb = Attr(color, "rgb");
                    rule.colorScale.push_back(rgb.empty() ? Colors::White : ArgbToColor(rgb));
                }
                if (rule.colorScale.size() < 2) continue;
            } else if (type == "dataBar") {
                auto* bar = cfRule->FirstChildElement("dataBar");
                if (!bar) continue;
                rule.type = ConditionalFormatType::DataBar;
                rule.showValue = std::string(Attr(bar, "showValue")) != "0";
                for (auto* cfvo = bar->FirstChildElement("cfvo"); cfvo; cfvo = cfvo->NextSiblingElement("cfvo")) {
                    rule.thresholds.push_back(ThresholdFromExcel(cfvo));
                }
                if (auto* color = bar->FirstChildElement("color")) {
                    const std::string rgb = Attr(color, "rgb");
                    if (!rgb.empty()) rule.dataBarColor = ArgbToColor(rgb);
                }
            } else {
                continue;   // icon sets and time periods are not drawn here
            }
            rules.push_back(std::move(rule));
        }
        if (rules.empty()) return;
        for (const CellRange& range : ranges) {
            ConditionalFormat format;
            format.range = range;
            format.rules = rules;
            sheet->AddConditionalFormat(format);
        }
    }

    void ParseDataValidation(tinyxml2::XMLElement* dv, SpreadsheetSheet* sheet) {
        DataValidation rule;
        rule.type = ValidationTypeFromExcel(Attr(dv, "type"));
        if (rule.type == ValidationType::Any) return;
        rule.op = OperatorFromExcel(Attr(dv, "operator"), ValidationOperator::Between);
        rule.allowBlank = AttrFlag(dv, "allowBlank");
        rule.showDropdown = !AttrFlag(dv, "showDropDown");   // Excel's flag hides the arrow
        rule.showInputMessage = AttrFlag(dv, "showInputMessage");
        rule.showErrorAlert = AttrFlag(dv, "showErrorMessage");
        const std::string style = Attr(dv, "errorStyle");
        rule.alertStyle = style == "warning" ? ValidationAlertStyle::Warning
                        : style == "information" ? ValidationAlertStyle::Information
                                                 : ValidationAlertStyle::Stop;
        rule.errorTitle = Attr(dv, "errorTitle");
        rule.errorMessage = Attr(dv, "error");
        rule.inputTitle = Attr(dv, "promptTitle");
        rule.inputMessage = Attr(dv, "prompt");
        auto formula = [dv](const char* tag) {
            auto* f = dv->FirstChildElement(tag);
            return std::string(f && f->GetText() ? f->GetText() : "");
        };
        const std::string f1 = TrimmedText(formula("formula1"));
        const std::string f2 = TrimmedText(formula("formula2"));
        if (rule.type == ValidationType::List) {
            // "a,b,c" quoted is the list itself; anything else names cells.
            rule.formula1 = IsQuotedText(f1) ? f1.substr(1, f1.size() - 2) : OperandFromExcel(f1);
            if (!rule.formula1.empty() && !IsQuotedText(f1) && rule.formula1[0] != '=') {
                rule.formula1 = "=" + rule.formula1;
            }
        } else if (rule.type == ValidationType::Custom) {
            rule.formula1 = OperandFromExcel(f1);
            if (!rule.formula1.empty() && rule.formula1[0] != '=') rule.formula1 = "=" + rule.formula1;
        } else {
            rule.formula1 = OperandFromExcel(f1);
            rule.formula2 = OperandFromExcel(f2);
        }
        for (const CellRange& range : ParseSqref(Attr(dv, "sqref"))) sheet->SetValidation(range, rule);
    }

    void ParseComments(const std::string& partPath, SpreadsheetSheet* sheet) {
        std::string xml;
        if (!zip_.ReadEntry(partPath, xml)) return;
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = doc.FirstChildElement("comments");
        if (!root) return;
        std::vector<std::string> authors;
        if (auto* list = root->FirstChildElement("authors")) {
            for (auto* a = list->FirstChildElement("author"); a; a = a->NextSiblingElement("author")) {
                authors.push_back(a->GetText() ? a->GetText() : "");
            }
        }
        auto* list = root->FirstChildElement("commentList");
        if (!list) return;
        for (auto* c = list->FirstChildElement("comment"); c; c = c->NextSiblingElement("comment")) {
            int row = 0, col = 0;
            if (!ParseCellRef(Attr(c, "ref"), row, col)) continue;
            const int authorId = c->IntAttribute("authorId", -1);
            const std::string author = (authorId >= 0 && authorId < static_cast<int>(authors.size()))
                                           ? authors[static_cast<size_t>(authorId)] : std::string();
            std::string text;
            if (auto* body = c->FirstChildElement("text")) CollectTextElements(body, text);
            // Excel starts a note with its author's name in bold ("Ann:\n").
            if (!author.empty() && text.rfind(author + ":\n", 0) == 0) text = text.substr(author.size() + 2);
            CellComment comment(text, author);
            sheet->GetCell(row, col)->SetComment(comment);
        }
    }

};

// ===== XLSX SAVER =====

class XlsxSaver {
public:
    explicit XlsxSaver(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {}

    bool Save(const std::string& filePath, const std::string& password, std::string& error) {
        spreadsheet_->RefreshCharts();   // the charts' caches as the cells are now
        // Sheets are generated first so the style/string tables are complete
        // before styles.xml and sharedStrings.xml are written.
        std::vector<std::string> sheetXmls;
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            sheetParts_.emplace_back();   // this sheet's comments and relationships
            sheetXmls.push_back(BuildWorksheetXml(sheet));
        }
        if (sheetXmls.empty()) {
            error = "The spreadsheet has no sheets to save";
            return false;
        }

        // With a password the package is built in memory and encrypted into
        // the file (UltraCanvasOfficeEncryption.h); nothing unencrypted is
        // written to disk.
        const bool encrypt = !password.empty();
        if (encrypt ? !zip_.OpenInMemory() : !zip_.Open(filePath)) {
            error = "Cannot create file: " + filePath;
            return false;
        }
        // The sheets' charts: their parts, each sheet's <drawing>, their types.
        // A worksheet's relationships to its drawing join those to its links
        // and comments in one part (MergeSheetRelationships).
        // A template (.xltx) is the same package with its main part's
        // content type saying so.
        std::string contentTypes = BuildContentTypesXml(sheetXmls.size(), ExtraContentTypes(),
                                                        IsSpreadsheetTemplateExtension(filePath));
        auto chartParts = WriteXlsxCharts(*spreadsheet_, sheetXmls, contentTypes);
        MergeSheetRelationships(chartParts);
        bool ok = zip_.AddEntry("[Content_Types].xml", contentTypes)
                  && zip_.AddEntry("_rels/.rels", BuildRootRelsXml())
                  && zip_.AddEntry("xl/workbook.xml", BuildWorkbookXml())
                  && zip_.AddEntry("xl/_rels/workbook.xml.rels",
                                   BuildWorkbookRelsXml(sheetXmls.size()))
                  && zip_.AddEntry("xl/styles.xml", BuildStylesXml())
                  && zip_.AddEntry("xl/sharedStrings.xml", BuildSharedStringsXml());
        for (size_t i = 0; ok && i < sheetXmls.size(); ++i) {
            ok = zip_.AddEntry("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml",
                               sheetXmls[i]);
        }
        ok = ok && WriteSheetParts();
        for (size_t i = 0; ok && i < chartParts.size(); ++i) {
            ok = zip_.AddEntry(chartParts[i].first, chartParts[i].second);
        }
        if (!ok || !zip_.Finalize()) {
            error = "Failed to write workbook package: " + zip_.GetLastError();
            return false;
        }
        return !encrypt || EncryptOfficePackageToFile(zip_.TakeBytes(), password, filePath, error);
    }

private:
    UltraCanvasSpreadsheet* spreadsheet_;
    UCZipPackageWriter zip_;
    std::vector<std::string> sharedStrings_;
    std::map<std::string, int> sharedStringIndex_;

    struct XfEntry {
        CellFont font;
        CellFill fill;
        CellBorders borders;
        HorizontalAlignment hAlign = HorizontalAlignment::General;
        VerticalAlignment vAlign = VerticalAlignment::Bottom;
        bool wrapText = false;
        int numFmtId = 0;
        std::string customFormatCode;   // when numFmtId >= 164
    };
    std::vector<XfEntry> xfs_{XfEntry{}};   // index 0 = default xf
    std::map<std::string, int> xfIndex_;
    int nextCustomNumFmt_ = 164;

    int SharedString(const std::string& text) {
        auto it = sharedStringIndex_.find(text);
        if (it != sharedStringIndex_.end()) return it->second;
        int index = static_cast<int>(sharedStrings_.size());
        sharedStrings_.push_back(text);
        sharedStringIndex_[text] = index;
        return index;
    }

    // Picks a builtin numFmt id (or allocates a custom one) for the cell's
    // effective number category.
    void ResolveNumberFormat(const NumberFormat& format, NumberFormatCategory category,
                             XfEntry& xf) {
        if (category == NumberFormatCategory::General
            && format.category != NumberFormatCategory::General) {
            category = format.category;
        }
        switch (category) {
            case NumberFormatCategory::Date: xf.numFmtId = 14; break;
            case NumberFormatCategory::Time: xf.numFmtId = 21; break;
            case NumberFormatCategory::DateTime: xf.numFmtId = 22; break;
            case NumberFormatCategory::Percentage:
                xf.numFmtId = (format.decimalPlaces == 0) ? 9 : 10;
                break;
            case NumberFormatCategory::Scientific: xf.numFmtId = 11; break;
            case NumberFormatCategory::Text: xf.numFmtId = 49; break;
            case NumberFormatCategory::Currency: {
                std::string digits = format.useThousandsSeparator ? "#,##0" : "0";
                if (format.decimalPlaces > 0) {
                    digits += "." + std::string(static_cast<size_t>(format.decimalPlaces), '0');
                }
                std::string symbol = "\"" + format.currencySymbol + "\"";
                xf.customFormatCode = format.currencySymbolAfter
                    ? digits + "\\ " + symbol : symbol + digits;
                xf.numFmtId = nextCustomNumFmt_;   // fixed up in DedupXf
                break;
            }
            case NumberFormatCategory::Number:
                if (format.useThousandsSeparator) {
                    xf.numFmtId = (format.decimalPlaces > 0) ? 4 : 3;
                } else {
                    xf.numFmtId = (format.decimalPlaces > 0) ? 2 : 1;
                }
                break;
            case NumberFormatCategory::Custom:
                if (!format.formatCode.empty() && format.formatCode != "General") {
                    xf.customFormatCode = format.formatCode;
                    xf.numFmtId = nextCustomNumFmt_;
                }
                break;
            default:
                xf.numFmtId = 0;
                break;
        }
    }

    static std::string XfKey(const XfEntry& xf) {
        std::ostringstream key;
        key << xf.font.family << '|' << xf.font.size << '|' << xf.font.bold
            << xf.font.italic << xf.font.strikethrough
            << static_cast<int>(xf.font.underline) << '|' << ColorToArgb(xf.font.color)
            << '|' << static_cast<int>(xf.fill.pattern) << ColorToArgb(xf.fill.foregroundColor)
            << '|' << static_cast<int>(xf.hAlign) << static_cast<int>(xf.vAlign)
            << xf.wrapText << '|' << xf.numFmtId << '|' << xf.customFormatCode << '|';
        auto side = [&](const CellBorder& b) {
            key << static_cast<int>(b.style) << ColorToArgb(b.color) << ';';
        };
        side(xf.borders.left);
        side(xf.borders.right);
        side(xf.borders.top);
        side(xf.borders.bottom);
        return key.str();
    }

    int DedupXf(XfEntry xf) {
        std::string key = XfKey(xf);
        auto it = xfIndex_.find(key);
        if (it != xfIndex_.end()) return it->second;
        if (!xf.customFormatCode.empty()) {
            xf.numFmtId = nextCustomNumFmt_++;
        }
        int index = static_cast<int>(xfs_.size());
        xfs_.push_back(std::move(xf));
        xfIndex_[key] = index;
        return index;
    }

    // Style index for a cell, folding in the value-type-implied number
    // format (a date without an explicit style still needs a date numFmt).
    int XfIndexForCell(const SpreadsheetCell& cell) {
        NumberFormatCategory implied = NumberFormatCategory::General;
        switch (cell.GetValueType()) {
            case CellValueType::Date: implied = NumberFormatCategory::Date; break;
            case CellValueType::Time: implied = NumberFormatCategory::Time; break;
            case CellValueType::DateTime: implied = NumberFormatCategory::DateTime; break;
            case CellValueType::Percentage: implied = NumberFormatCategory::Percentage; break;
            case CellValueType::Currency: implied = NumberFormatCategory::Currency; break;
            default: break;
        }

        XfEntry xf;
        NumberFormat format;
        if (cell.HasCustomStyle()) {
            const CellStyle& style = cell.GetStyle();
            xf.font = style.font;
            xf.fill = style.fill;
            xf.borders = style.borders;
            xf.hAlign = style.hAlign;
            xf.vAlign = style.vAlign;
            xf.wrapText = style.wrapText;
            format = style.numberFormat;
        }
        if (implied == NumberFormatCategory::Currency) {
            format.currencySymbol = cell.GetCurrency().currencyCode.empty()
                ? format.currencySymbol : cell.GetCurrency().currencyCode + " ";
        }
        ResolveNumberFormat(format, implied, xf);

        XfEntry defaults;
        if (XfKey(xf) == XfKey(defaults)) return 0;
        return DedupXf(std::move(xf));
    }

    // The elements are written in the order the schema gives them (sheetPr,
    // sheetViews, cols, sheetData, autoFilter, mergeCells, printOptions,
    // pageMargins, pageSetup, headerFooter, rowBreaks, colBreaks): Excel
    // refuses a worksheet whose elements are out of order.
    std::string BuildWorksheetXml(const SpreadsheetSheet* sheet) {
        std::ostringstream xml;
        xml.imbue(std::locale::classic());   // numbers in the file are dot-decimal
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n";

        CellRange used = sheet->GetUsedRange();
        const PrintSettings& print = sheet->GetPrintSettings();
        const bool fitToPage = print.fitToWidth > 0 || print.fitToHeight > 0;

        if (fitToPage) xml << "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>\n";

        const FreezePanes& freeze = sheet->GetFreezePanes();
        if (freeze.HasFreeze()) {
            const int rows = std::max(0, freeze.frozenRows);
            const int cols = std::max(0, freeze.frozenColumns);
            const char* pane = (rows > 0 && cols > 0) ? "bottomRight"
                             : rows > 0 ? "bottomLeft" : "topRight";
            xml << "<sheetViews><sheetView workbookViewId=\"0\"><pane";
            if (cols > 0) xml << " xSplit=\"" << cols << "\"";
            if (rows > 0) xml << " ySplit=\"" << rows << "\"";
            xml << " topLeftCell=\"" << CellRef(rows, cols) << "\" activePane=\"" << pane
                << "\" state=\"frozen\"/><selection pane=\"" << pane << "\"/>"
                << "</sheetView></sheetViews>\n";
        }

        // Columns: the sized ones up to the last used column, and every hidden
        // one wherever it is; neighbours that agree share one <col> run.
        int lastColumn = used.end.col;
        for (const auto& [index, def] : sheet->GetColumnDefinitions()) {
            if (def.hidden) lastColumn = std::max(lastColumn, index);
        }
        std::ostringstream cols;
        cols.imbue(std::locale::classic());
        for (int col = 0; col <= lastColumn;) {
            int width = sheet->GetColumnWidth(col);
            bool hidden = sheet->IsColumnHidden(col);
            if (width == SpreadsheetLimits::DefaultColumnWidth && !hidden) { ++col; continue; }
            int end = col;
            while (end + 1 <= lastColumn && sheet->GetColumnWidth(end + 1) == width &&
                   sheet->IsColumnHidden(end + 1) == hidden) {
                ++end;
            }
            cols << "<col min=\"" << (col + 1) << "\" max=\"" << (end + 1)
                 << "\" width=\"" << (width / kPixelsPerWidthChar) << "\"";
            if (hidden) cols << " hidden=\"1\"";
            if (width != SpreadsheetLimits::DefaultColumnWidth) cols << " customWidth=\"1\"";
            cols << "/>";
            col = end + 1;
        }
        std::string colsStr = cols.str();
        if (!colsStr.empty()) xml << "<cols>" << colsStr << "</cols>\n";

        int lastRow = used.end.row;
        for (const auto& [index, def] : sheet->GetRowDefinitions()) {
            if (def.hidden) lastRow = std::max(lastRow, index);
        }
        xml << "<sheetData>\n";
        for (int row = 0; row <= lastRow; ++row) {
            std::ostringstream cells;
            cells.imbue(std::locale::classic());
            for (int col = 0; row <= used.end.row && col <= used.end.col; ++col) {
                const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);
                if (cell && (!cell->IsEmpty() || cell->HasCustomStyle())) {
                    WriteCell(cells, *cell, row, col);
                }
            }
            std::string cellsStr = cells.str();
            int height = sheet->GetRowHeight(row);
            bool customHeight = height != SpreadsheetLimits::DefaultRowHeight;
            bool hidden = sheet->IsRowHidden(row);
            if (cellsStr.empty() && !customHeight && !hidden) continue;
            xml << "<row r=\"" << (row + 1) << "\"";
            if (customHeight) {
                xml << " ht=\"" << (height / kPixelsPerPoint) << "\" customHeight=\"1\"";
            }
            if (hidden) xml << " hidden=\"1\"";
            xml << ">" << cellsStr << "</row>\n";
        }
        xml << "</sheetData>\n";

        if (sheet->HasAutoFilter()) {
            const AutoFilter* filter = sheet->GetAutoFilter();
            const CellRange& range = filter->range;
            xml << "<autoFilter ref=\"" << CellRef(range.start.row, range.start.col) << ":"
                << CellRef(range.end.row, range.end.col) << "\"";
            std::ostringstream columns;
            for (const ColumnFilter& column : filter->columnFilters) {
                if (column.op != FilterOperator::Equals || column.values.empty() ||
                    column.column < range.start.col || column.column > range.end.col) {
                    continue;   // only a list of values has an <autoFilter> form here
                }
                columns << "<filterColumn colId=\"" << (column.column - range.start.col)
                        << "\"><filters>";
                for (const std::string& value : column.values) {
                    columns << "<filter val=\"" << XmlEscape(value) << "\"/>";
                }
                columns << "</filters></filterColumn>";
            }
            std::string columnsStr = columns.str();
            if (columnsStr.empty()) xml << "/>\n";
            else xml << ">" << columnsStr << "</autoFilter>\n";
        }

        const auto& merges = sheet->GetMergedCells();
        if (!merges.empty()) {
            xml << "<mergeCells count=\"" << merges.size() << "\">";
            for (const auto& merge : merges) {
                xml << "<mergeCell ref=\"" << CellRef(merge.range.start.row, merge.range.start.col)
                    << ":" << CellRef(merge.range.end.row, merge.range.end.col) << "\"/>";
            }
            xml << "</mergeCells>\n";
        }

        WriteSheetExtras(xml, sheet);
        WritePrintSettings(xml, print, fitToPage);
        WriteLegacyDrawing(xml);
        xml << "</worksheet>\n";
        return xml.str();
    }

    static void WritePrintSettings(std::ostringstream& xml, const PrintSettings& print,
                                   bool fitToPage) {
        if (print.printGridlines || print.printRowColHeaders || print.centerHorizontally ||
            print.centerVertically) {
            xml << "<printOptions";
            if (print.centerHorizontally) xml << " horizontalCentered=\"1\"";
            if (print.centerVertically) xml << " verticalCentered=\"1\"";
            if (print.printRowColHeaders) xml << " headings=\"1\"";
            if (print.printGridlines) xml << " gridLines=\"1\"";
            xml << "/>\n";
        }
        xml << "<pageMargins left=\"" << FormatFloatClassic(print.marginLeft)
            << "\" right=\"" << FormatFloatClassic(print.marginRight)
            << "\" top=\"" << FormatFloatClassic(print.marginTop)
            << "\" bottom=\"" << FormatFloatClassic(print.marginBottom)
            << "\" header=\"" << FormatFloatClassic(print.headerMargin)
            << "\" footer=\"" << FormatFloatClassic(print.footerMargin) << "\"/>\n";
        xml << "<pageSetup";
        if (int paper = ExcelPaperSize(print.paperSize)) xml << " paperSize=\"" << paper << "\"";
        if (print.scale != 100 && print.scale > 0) xml << " scale=\"" << print.scale << "\"";
        if (fitToPage) {
            xml << " fitToWidth=\"" << std::max(0, print.fitToWidth) << "\" fitToHeight=\""
                << std::max(0, print.fitToHeight) << "\"";
        }
        xml << " orientation=\""
            << (print.orientation == PageOrientation::Landscape ? "landscape" : "portrait")
            << "\"/>\n";
        std::string header = JoinHeaderFooter(print.headerLeft, print.headerCenter, print.headerRight);
        std::string footer = JoinHeaderFooter(print.footerLeft, print.footerCenter, print.footerRight);
        if (!header.empty() || !footer.empty()) {
            xml << "<headerFooter>";
            if (!header.empty()) xml << "<oddHeader>" << XmlEscape(header) << "</oddHeader>";
            if (!footer.empty()) xml << "<oddFooter>" << XmlEscape(footer) << "</oddFooter>";
            xml << "</headerFooter>\n";
        }
        auto writeBreaks = [&](const char* tag, const std::vector<int>& breaks, int max) {
            std::vector<int> ids;
            for (int id : breaks) {
                if (id > 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            }
            if (ids.empty()) return;
            std::sort(ids.begin(), ids.end());
            xml << "<" << tag << " count=\"" << ids.size() << "\" manualBreakCount=\""
                << ids.size() << "\">";
            for (int id : ids) {
                xml << "<brk id=\"" << id << "\" max=\"" << max << "\" man=\"1\"/>";
            }
            xml << "</" << tag << ">\n";
        };
        writeBreaks("rowBreaks", print.rowBreaks, SpreadsheetLimits::MaxColumns - 1);
        writeBreaks("colBreaks", print.colBreaks, SpreadsheetLimits::MaxRows - 1);
    }

    void WriteCell(std::ostringstream& xml, const SpreadsheetCell& cell, int row, int col) {
        xml << "<c r=\"" << CellRef(row, col) << "\"";
        int xfIndex = XfIndexForCell(cell);
        if (xfIndex > 0) xml << " s=\"" << xfIndex << "\"";

        std::string formula;
        if (cell.HasFormula()) {
            // Back into Excel's syntax: 'Data'.B5 is Data!B5 to Excel.
            formula = NativeFormulaToExcel(cell.GetFormulaText());
            if (!formula.empty() && formula[0] == '=') formula = formula.substr(1);
        }

        switch (cell.GetValueType()) {
            case CellValueType::Text: {
                xml << " t=\"s\"><v>" << SharedString(cell.GetText()) << "</v></c>";
                return;
            }
            case CellValueType::Boolean:
                xml << " t=\"b\">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                xml << "<v>" << (cell.GetBoolean() ? 1 : 0) << "</v></c>";
                return;
            case CellValueType::Error:
                xml << " t=\"e\">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                xml << "<v>" << XmlEscape(CellErrorToString(cell.GetError())) << "</v></c>";
                return;
            case CellValueType::Currency: {
                xml << ">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                xml << "<v>" << FormatFloatRoundTrip(cell.GetCurrency().amount) << "</v></c>";
                return;
            }
            case CellValueType::Empty:
            case CellValueType::Formula: {
                xml << ">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                auto number = cell.TryGetNumber();
                if (number) xml << "<v>" << FormatFloatRoundTrip(*number) << "</v>";
                xml << "</c>";
                return;
            }
            default:
                // Number, Date, Time, DateTime, Percentage: serial value; the
                // xf's numFmt communicates the presentation.
                xml << ">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                xml << "<v>" << FormatFloatRoundTrip(cell.GetNumber()) << "</v></c>";
                return;
        }
    }

    std::string BuildStylesXml() {
        std::ostringstream fonts, fills, borders, xfs, numFmts;
        std::map<std::string, int> fontIds, fillIds, borderIds;
        int numFmtCount = 0;

        auto fontId = [&](const CellFont& font) {
            std::ostringstream key;
            key << font.family << '|' << font.size << '|' << font.bold << font.italic
                << font.strikethrough << static_cast<int>(font.underline)
                << '|' << ColorToArgb(font.color);
            auto it = fontIds.find(key.str());
            if (it != fontIds.end()) return it->second;
            int id = static_cast<int>(fontIds.size());
            fontIds[key.str()] = id;
            fonts << "<font>";
            if (font.bold) fonts << "<b/>";
            if (font.italic) fonts << "<i/>";
            if (font.strikethrough) fonts << "<strike/>";
            if (font.underline == UnderlineStyle::Double) fonts << "<u val=\"double\"/>";
            else if (font.underline != UnderlineStyle::NoUnderline) fonts << "<u/>";
            fonts << "<sz val=\"" << font.size << "\"/>"
                  << "<color rgb=\"" << ColorToArgb(font.color) << "\"/>"
                  << "<name val=\"" << XmlEscape(font.family) << "\"/></font>";
            return id;
        };
        auto fillId = [&](const CellFill& fill) {
            if (!fill.HasFill()) return 0;
            std::string key = ColorToArgb(fill.foregroundColor);
            auto it = fillIds.find(key);
            if (it != fillIds.end()) return it->second;
            // Fill ids 0 (none) and 1 (gray125) are reserved by convention.
            int id = static_cast<int>(fillIds.size()) + 2;
            fillIds[key] = id;
            fills << "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"" << key
                  << "\"/><bgColor indexed=\"64\"/></patternFill></fill>";
            return id;
        };
        auto borderId = [&](const CellBorders& b) {
            if (!b.HasAnyBorder()) return 0;
            std::ostringstream entry;
            entry << "<border>";
            auto side = [&](const char* tag, const CellBorder& border) {
                const char* style = BorderStyleName(border.style);
                if (style) {
                    entry << "<" << tag << " style=\"" << style << "\"><color rgb=\""
                          << ColorToArgb(border.color) << "\"/></" << tag << ">";
                } else {
                    entry << "<" << tag << "/>";
                }
            };
            side("left", b.left);
            side("right", b.right);
            side("top", b.top);
            side("bottom", b.bottom);
            entry << "<diagonal/></border>";
            auto it = borderIds.find(entry.str());
            if (it != borderIds.end()) return it->second;
            int id = static_cast<int>(borderIds.size()) + 1;   // 0 = empty default
            borderIds[entry.str()] = id;
            borders << entry.str();
            return id;
        };

        for (const XfEntry& xf : xfs_) {
            if (!xf.customFormatCode.empty()) {
                numFmts << "<numFmt numFmtId=\"" << xf.numFmtId << "\" formatCode=\""
                        << XmlEscape(xf.customFormatCode) << "\"/>";
                ++numFmtCount;
            }
            int fId = fontId(xf.font);
            int flId = fillId(xf.fill);
            int bId = borderId(xf.borders);
            xfs << "<xf numFmtId=\"" << xf.numFmtId << "\" fontId=\"" << fId
                << "\" fillId=\"" << flId << "\" borderId=\"" << bId
                << "\" xfId=\"0\"";
            if (xf.numFmtId > 0) xfs << " applyNumberFormat=\"1\"";
            if (fId > 0) xfs << " applyFont=\"1\"";
            if (flId > 0) xfs << " applyFill=\"1\"";
            if (bId > 0) xfs << " applyBorder=\"1\"";
            bool hasAlign = xf.hAlign != HorizontalAlignment::General
                            || xf.vAlign != VerticalAlignment::Bottom || xf.wrapText;
            if (hasAlign) {
                xfs << " applyAlignment=\"1\"><alignment";
                switch (xf.hAlign) {
                    case HorizontalAlignment::Left: xfs << " horizontal=\"left\""; break;
                    case HorizontalAlignment::Center: xfs << " horizontal=\"center\""; break;
                    case HorizontalAlignment::Right: xfs << " horizontal=\"right\""; break;
                    case HorizontalAlignment::Fill: xfs << " horizontal=\"fill\""; break;
                    case HorizontalAlignment::Justify: xfs << " horizontal=\"justify\""; break;
                    case HorizontalAlignment::Distributed:
                        xfs << " horizontal=\"distributed\"";
                        break;
                    default: break;
                }
                if (xf.vAlign == VerticalAlignment::Top) xfs << " vertical=\"top\"";
                else if (xf.vAlign == VerticalAlignment::Middle) xfs << " vertical=\"center\"";
                if (xf.wrapText) xfs << " wrapText=\"1\"";
                xfs << "/></xf>";
            } else {
                xfs << "/>";
            }
        }

        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";
        if (numFmtCount > 0) {
            xml << "<numFmts count=\"" << numFmtCount << "\">" << numFmts.str()
                << "</numFmts>\n";
        }
        xml << "<fonts count=\"" << fontIds.size() << "\">" << fonts.str() << "</fonts>\n"
            << "<fills count=\"" << (fillIds.size() + 2)
            << "\"><fill><patternFill patternType=\"none\"/></fill>"
               "<fill><patternFill patternType=\"gray125\"/></fill>"
            << fills.str() << "</fills>\n"
            << "<borders count=\"" << (borderIds.size() + 1) << "\"><border><left/><right/>"
               "<top/><bottom/><diagonal/></border>" << borders.str() << "</borders>\n"
            << "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" "
               "borderId=\"0\"/></cellStyleXfs>\n"
            << "<cellXfs count=\"" << xfs_.size() << "\">" << xfs.str() << "</cellXfs>\n"
            << "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" "
               "builtinId=\"0\"/></cellStyles>\n"
            << BuildDxfsXml()
            << "</styleSheet>\n";
        return xml.str();
    }

    std::string BuildSharedStringsXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            << "count=\"" << sharedStrings_.size() << "\" uniqueCount=\""
            << sharedStrings_.size() << "\">\n";
        for (const auto& text : sharedStrings_) {
            bool preserve = !text.empty()
                            && (text.front() == ' ' || text.back() == ' ');
            xml << "<si><t" << (preserve ? " xml:space=\"preserve\"" : "") << ">"
                << XmlEscape(text) << "</t></si>\n";
        }
        xml << "</sst>\n";
        return xml.str();
    }

    std::string BuildWorkbookXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            << "<sheets>";
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            std::string name = sheet ? sheet->GetName() : "Sheet" + std::to_string(s + 1);
            xml << "<sheet name=\"" << XmlEscape(name) << "\" sheetId=\"" << (s + 1)
                << "\" r:id=\"rIdSheet" << (s + 1) << "\"/>";
        }
        xml << "</sheets>";
        std::string definedNames = BuildDefinedNamesXml();
        if (!definedNames.empty()) xml << "<definedNames>" << definedNames << "</definedNames>";
        xml << "</workbook>\n";
        return xml.str();
    }

    // The workbook's named ranges (a sheet-scoped one with the localSheetId of
    // its sheet), the names a sheet keeps itself, and per sheet the built-in
    // names Excel keeps the print area, the print titles and the auto-filter
    // range in.
    std::string BuildDefinedNamesXml() const {
        std::ostringstream xml;
        const int sheetCount = spreadsheet_->GetSheetCount();
        auto sheetIndex = [&](const std::string& name) {
            for (int s = 0; s < sheetCount; ++s) {
                const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
                if (sheet && SameSheetName(sheet->GetName(), name)) return s;
            }
            return -1;
        };
        auto entry = [&](const std::string& name, int localSheet, const std::string& ref,
                         bool hidden, const std::string& comment) {
            xml << "<definedName name=\"" << XmlEscape(name) << "\"";
            if (!comment.empty()) xml << " comment=\"" << XmlEscape(comment) << "\"";
            if (localSheet >= 0) xml << " localSheetId=\"" << localSheet << "\"";
            if (hidden) xml << " hidden=\"1\"";
            xml << ">" << XmlEscape(ref) << "</definedName>";
        };
        const SpreadsheetSheet* firstSheet = spreadsheet_->GetSheet(0);
        std::set<std::pair<int, std::string>> written;   // (scope, lower-case name)
        auto lower = [](std::string text) {
            for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        auto writeName = [&](const NamedRange& named, int scope, const std::string& ownSheet) {
            if (named.name.empty() || !named.range.IsValid()) return;
            if (!written.insert({scope, lower(named.name)}).second) return;
            // A range names its sheet; one that does not covers the sheet the
            // name is scoped to. Excel has no workbook name for "whichever
            // sheet the formula is on", so such a name is given the first.
            std::string sheetName = named.range.start.sheetName;
            if (sheetName.empty()) sheetName = ownSheet;
            if (sheetName.empty() && firstSheet) sheetName = firstSheet->GetName();
            entry(named.name, scope, ExcelArea(sheetName, named.range), named.hidden, named.comment);
        };

        for (const NamedRange& named : spreadsheet_->GetNamedRangeList()) {
            int scope = named.sheetScope.empty() ? -1 : sheetIndex(named.sheetScope);
            if (!named.sheetScope.empty() && scope < 0) continue;   // its sheet is gone
            writeName(named, scope, named.sheetScope);
        }
        for (int s = 0; s < sheetCount; ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            for (const auto& [key, named] : sheet->GetNamedRanges()) {
                writeName(named, s, sheet->GetName());
            }
            const PrintSettings& print = sheet->GetPrintSettings();
            if (sheet->HasAutoFilter()) {
                entry("_xlnm._FilterDatabase", s,
                      ExcelArea(sheet->GetName(), sheet->GetAutoFilter()->range), true, "");
            }
            if (print.HasPrintArea() && print.printArea.IsValid()) {
                entry("_xlnm.Print_Area", s, ExcelArea(sheet->GetName(), print.printArea), false, "");
            }
            std::string titles;
            if (print.HasRepeatColumns()) {
                titles = ExcelSheetPrefix(sheet->GetName()) + "$" +
                         ColumnLetters(print.repeatColumnsFirst) + ":$" +
                         ColumnLetters(print.repeatColumnsLast);
            }
            if (print.HasRepeatRows()) {
                if (!titles.empty()) titles += ",";
                titles += ExcelSheetPrefix(sheet->GetName()) + "$" +
                          std::to_string(print.repeatRowsFirst + 1) + ":$" +
                          std::to_string(print.repeatRowsLast + 1);
            }
            if (!titles.empty()) entry("_xlnm.Print_Titles", s, titles, false, "");
        }
        return xml.str();
    }

    static std::string BuildWorkbookRelsXml(size_t sheetCount) {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
        for (size_t i = 0; i < sheetCount; ++i) {
            xml << "<Relationship Id=\"rIdSheet" << (i + 1)
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                   "relationships/worksheet\" Target=\"worksheets/sheet" << (i + 1)
                << ".xml\"/>\n";
        }
        xml << "<Relationship Id=\"rIdStyles\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
               "Target=\"styles.xml\"/>\n"
            << "<Relationship Id=\"rIdSst\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" "
               "Target=\"sharedStrings.xml\"/>\n"
            << "</Relationships>\n";
        return xml.str();
    }

    static std::string BuildContentTypesXml(size_t sheetCount, const std::string& extraTypes, bool isTemplate) {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
            << "<Default Extension=\"rels\" "
               "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
            << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
            << "<Override PartName=\"/xl/workbook.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
            << (isTemplate ? "spreadsheetml.template.main+xml" : "spreadsheetml.sheet.main+xml") << "\"/>\n";
        for (size_t i = 0; i < sheetCount; ++i) {
            xml << "<Override PartName=\"/xl/worksheets/sheet" << (i + 1)
                << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
                   "spreadsheetml.worksheet+xml\"/>\n";
        }
        xml << "<Override PartName=\"/xl/styles.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
               "spreadsheetml.styles+xml\"/>\n"
            << "<Override PartName=\"/xl/sharedStrings.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
               "spreadsheetml.sharedStrings+xml\"/>\n"
            << extraTypes
            << "</Types>\n";
        return xml.str();
    }

    static std::string BuildRootRelsXml() {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
               "<Relationship Id=\"rId1\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
               "Target=\"xl/workbook.xml\"/>\n"
               "</Relationships>\n";
    }
    // ===== CONDITIONAL FORMATS, VALIDATIONS, LINKS, COMMENTS =====

    // What a worksheet needs beside its own part: relationships (links,
    // the comments and their drawing) and the comments' two parts.
    struct SheetParts {
        struct Relationship { std::string id, type, target; bool external = false; };
        std::vector<Relationship> rels;
        std::string commentsXml;
        std::string vmlXml;
        std::string moreRelationships;   // <Relationship> elements another writer made (the charts' drawing)
    };
    std::vector<SheetParts> sheetParts_;   // one per worksheet, in order
    std::vector<CellStyle> dxfs_;          // the conditional formats' styles

    static constexpr const char* kRelHyperlink =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    static constexpr const char* kRelComments =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
    static constexpr const char* kRelVmlDrawing =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";

    std::string AddSheetRelationship(const std::string& type, const std::string& target, bool external) {
        SheetParts& parts = sheetParts_.back();
        std::string id = "rIdX" + std::to_string(parts.rels.size() + 1);
        parts.rels.push_back({id, type, target, external});
        return id;
    }

    int AddDxf(const CellStyle& style) {
        dxfs_.push_back(style);
        return static_cast<int>(dxfs_.size()) - 1;
    }

    // conditionalFormatting, dataValidations and hyperlinks, in the place
    // the schema gives them (after mergeCells, before printOptions).
    void WriteSheetExtras(std::ostringstream& xml, const SpreadsheetSheet* sheet) {
        WriteConditionalFormats(xml, sheet);
        WriteDataValidations(xml, sheet);
        WriteHyperlinks(xml, sheet);
        BuildComments(sheet);
    }

    void WriteConditionalFormats(std::ostringstream& xml, const SpreadsheetSheet* sheet) {
        int priority = 0;
        for (const ConditionalFormat& format : sheet->GetConditionalFormats()) {
            if (!format.range.IsValid() || format.rules.empty()) continue;
            const std::string topLeft = CellRef(format.range.start.row, format.range.start.col);
            std::ostringstream rules;
            rules.imbue(std::locale::classic());
            for (const ConditionalFormatRule& rule : format.rules) {
                ++priority;
                const int rulePriority = rule.priority > 0 ? rule.priority : priority;
                auto open = [&](const char* type, bool withStyle) {
                    rules << "<cfRule type=\"" << type << "\"";
                    if (withStyle) rules << " dxfId=\"" << AddDxf(rule.style) << "\"";
                    rules << " priority=\"" << rulePriority << "\"";
                    if (rule.stopIfTrue) rules << " stopIfTrue=\"1\"";
                };
                auto formula = [&](const std::string& f) {
                    rules << "<formula>" << XmlEscape(f) << "</formula>";
                };
                const std::string quoted = QuoteExcelText(rule.formula1);
                switch (rule.type) {
                    case ConditionalFormatType::CellValue:
                        open("cellIs", true);
                        rules << " operator=\"" << ExcelOperatorName(rule.op) << "\">";
                        formula(ExcelOperand(rule.formula1));
                        if (rule.op == ValidationOperator::Between || rule.op == ValidationOperator::NotBetween) {
                            formula(ExcelOperand(rule.formula2));
                        }
                        break;
                    case ConditionalFormatType::Expression:
                        open("expression", true);
                        rules << ">";
                        formula(ExcelFormulaBody(rule.formula1));
                        break;
                    case ConditionalFormatType::ContainsText:
                        open("containsText", true);
                        rules << " operator=\"containsText\" text=\"" << XmlEscape(rule.formula1) << "\">";
                        formula("NOT(ISERROR(SEARCH(" + quoted + "," + topLeft + ")))");
                        break;
                    case ConditionalFormatType::NotContainsText:
                        open("notContainsText", true);
                        rules << " operator=\"notContains\" text=\"" << XmlEscape(rule.formula1) << "\">";
                        formula("ISERROR(SEARCH(" + quoted + "," + topLeft + "))");
                        break;
                    case ConditionalFormatType::BeginsWithText:
                        open("beginsWith", true);
                        rules << " operator=\"beginsWith\" text=\"" << XmlEscape(rule.formula1) << "\">";
                        formula("LEFT(" + topLeft + ",LEN(" + quoted + "))=" + quoted);
                        break;
                    case ConditionalFormatType::EndsWithText:
                        open("endsWith", true);
                        rules << " operator=\"endsWith\" text=\"" << XmlEscape(rule.formula1) << "\">";
                        formula("RIGHT(" + topLeft + ",LEN(" + quoted + "))=" + quoted);
                        break;
                    case ConditionalFormatType::ContainsBlanks:
                        open("containsBlanks", true);
                        rules << ">";
                        formula("LEN(TRIM(" + topLeft + "))=0");
                        break;
                    case ConditionalFormatType::NotContainsBlanks:
                        open("notContainsBlanks", true);
                        rules << ">";
                        formula("LEN(TRIM(" + topLeft + "))>0");
                        break;
                    case ConditionalFormatType::ContainsErrors:
                        open("containsErrors", true);
                        rules << ">";
                        formula("ISERROR(" + topLeft + ")");
                        break;
                    case ConditionalFormatType::NotContainsErrors:
                        open("notContainsErrors", true);
                        rules << ">";
                        formula("NOT(ISERROR(" + topLeft + "))");
                        break;
                    case ConditionalFormatType::Top10: {
                        open("top10", true);
                        int rank = std::atoi(TrimmedText(rule.formula1).c_str());
                        rules << " rank=\"" << (rank > 0 ? rank : 10) << "\"";
                        if (rule.percent) rules << " percent=\"1\"";
                        if (rule.bottom) rules << " bottom=\"1\"";
                        rules << ">";
                        break;
                    }
                    case ConditionalFormatType::AboveAverage:
                        open("aboveAverage", true);
                        if (!rule.aboveAverage) rules << " aboveAverage=\"0\"";
                        if (rule.equalAverage) rules << " equalAverage=\"1\"";
                        rules << ">";
                        break;
                    case ConditionalFormatType::DuplicateValues:
                        open("duplicateValues", true);
                        rules << ">";
                        break;
                    case ConditionalFormatType::UniqueValues:
                        open("uniqueValues", true);
                        rules << ">";
                        break;
                    case ConditionalFormatType::ColorScale: {
                        if (rule.colorScale.size() < 2) continue;
                        const size_t count = std::min<size_t>(rule.colorScale.size(), 3);
                        open("colorScale", false);
                        rules << "><colorScale>";
                        WriteThresholds(rules, rule, count);
                        for (size_t i = 0; i < count; ++i) {
                            rules << "<color rgb=\"" << ColorToArgb(rule.colorScale[i]) << "\"/>";
                        }
                        rules << "</colorScale>";
                        break;
                    }
                    case ConditionalFormatType::DataBar:
                        open("dataBar", false);
                        rules << "><dataBar";
                        if (!rule.showValue) rules << " showValue=\"0\"";
                        rules << ">";
                        WriteThresholds(rules, rule, 2);
                        rules << "<color rgb=\"" << ColorToArgb(rule.dataBarColor) << "\"/></dataBar>";
                        break;
                    default:
                        continue;   // icon sets, time periods: not written
                }
                rules << "</cfRule>";
            }
            const std::string body = rules.str();
            if (body.empty()) continue;
            xml << "<conditionalFormatting sqref=\"" << RangeRef(format.range) << "\">" << body
                << "</conditionalFormatting>\n";
        }
    }

    // A colour scale's or data bar's points as <cfvo>s: the rule's own, else
    // the defaults GetConditionalFormatting draws with.
    static void WriteThresholds(std::ostringstream& out, const ConditionalFormatRule& rule, size_t count) {
        std::vector<ConditionalThreshold> points = rule.thresholds;
        if (points.size() != count) {
            points.clear();
            if (rule.colorScaleValues.size() == count) {
                for (double v : rule.colorScaleValues) {
                    points.emplace_back(ConditionalThresholdKind::Number, FormatFloatRoundTrip(v));
                }
            } else {
                points.emplace_back(ConditionalThresholdKind::Lowest);
                if (count == 3) points.emplace_back(ConditionalThresholdKind::Percentile, "50");
                points.emplace_back(ConditionalThresholdKind::Highest);
            }
        }
        for (const ConditionalThreshold& point : points) {
            out << "<cfvo type=\"" << ExcelThresholdType(point.kind) << "\"";
            if (point.kind == ConditionalThresholdKind::Formula ||
                (point.kind == ConditionalThresholdKind::Number && !IsPlainNumber(point.value))) {
                out << " val=\"" << XmlEscape(ExcelOperand(point.value.empty() || point.value[0] == '='
                                                               ? point.value : "=" + point.value)) << "\"";
            } else if (point.kind != ConditionalThresholdKind::Lowest &&
                       point.kind != ConditionalThresholdKind::Highest) {
                out << " val=\"" << XmlEscape(TrimmedText(point.value)) << "\"";
            }
            out << "/>";
        }
    }

    // A bound of a Whole number / Decimal / Date / Time / Text length rule:
    // the number (a date as its serial number), or the formula.
    static std::string ExcelBound(const std::string& bound, ValidationType type) {
        if (auto number = ReadDataValidationBound(bound, type)) return FormatFloatRoundTrip(*number);
        return ExcelOperand(bound);
    }

    static void WriteDataValidation(std::ostringstream& out, const DataValidation& rule, const std::string& sqref) {
        if (rule.type == ValidationType::Any) return;
        out << "<dataValidation type=\"" << ExcelValidationType(rule.type) << "\"";
        if (rule.type != ValidationType::List && rule.type != ValidationType::Custom &&
            rule.op != ValidationOperator::Between) {
            out << " operator=\"" << ExcelOperatorName(rule.op) << "\"";
        }
        if (rule.alertStyle == ValidationAlertStyle::Warning) out << " errorStyle=\"warning\"";
        if (rule.alertStyle == ValidationAlertStyle::Information) out << " errorStyle=\"information\"";
        if (rule.allowBlank) out << " allowBlank=\"1\"";
        if (rule.type == ValidationType::List && !rule.showDropdown) out << " showDropDown=\"1\"";
        if (rule.showInputMessage) out << " showInputMessage=\"1\"";
        if (rule.showErrorAlert) out << " showErrorMessage=\"1\"";
        if (!rule.errorTitle.empty()) out << " errorTitle=\"" << XmlEscape(rule.errorTitle) << "\"";
        if (!rule.errorMessage.empty()) out << " error=\"" << XmlEscape(rule.errorMessage) << "\"";
        if (!rule.inputTitle.empty()) out << " promptTitle=\"" << XmlEscape(rule.inputTitle) << "\"";
        if (!rule.inputMessage.empty()) out << " prompt=\"" << XmlEscape(rule.inputMessage) << "\"";
        out << " sqref=\"" << sqref << "\">";
        if (rule.type == ValidationType::List) {
            if (IsRangeListSource(rule)) {
                out << "<formula1>" << XmlEscape(ExcelFormulaBody(rule.formula1)) << "</formula1>";
            } else {
                std::string items;
                for (const std::string& item : DataValidationListItems(rule)) {
                    items += (items.empty() ? "" : ",") + item;
                }
                out << "<formula1>" << XmlEscape("\"" + items + "\"") << "</formula1>";
            }
        } else if (rule.type == ValidationType::Custom) {
            out << "<formula1>" << XmlEscape(ExcelFormulaBody(rule.formula1)) << "</formula1>";
        } else {
            out << "<formula1>" << XmlEscape(ExcelBound(rule.formula1, rule.type)) << "</formula1>";
            if (rule.op == ValidationOperator::Between || rule.op == ValidationOperator::NotBetween) {
                out << "<formula2>" << XmlEscape(ExcelBound(rule.formula2, rule.type)) << "</formula2>";
            }
        }
        out << "</dataValidation>";
    }

    void WriteDataValidations(std::ostringstream& xml, const SpreadsheetSheet* sheet) {
        std::ostringstream body;
        body.imbue(std::locale::classic());
        int count = 0;
        for (const auto& [range, rule] : sheet->GetValidations()) {
            if (!rule.IsValid() || !range.IsValid()) continue;
            WriteDataValidation(body, rule, RangeRef(range));
            ++count;
        }
        // A rule a cell carries itself, where no range rule covers it.
        std::vector<std::pair<CellAddress, DataValidation>> own;
        sheet->ForEachCell([&](int row, int col, const SpreadsheetCell& cell) {
            if (cell.HasValidation() && cell.GetValidation()->IsValid() && !sheet->GetValidation(row, col)) {
                own.emplace_back(CellAddress(row, col), *cell.GetValidation());
            }
        });
        std::sort(own.begin(), own.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [at, rule] : own) {
            WriteDataValidation(body, rule, CellRef(at.row, at.col));
            ++count;
        }
        if (count > 0) {
            xml << "<dataValidations count=\"" << count << "\">" << body.str() << "</dataValidations>\n";
        }
    }

    void WriteHyperlinks(std::ostringstream& xml, const SpreadsheetSheet* sheet) {
        std::vector<std::pair<CellAddress, CellHyperlink>> links;
        sheet->ForEachCell([&](int row, int col, const SpreadsheetCell& cell) {
            if (cell.HasHyperlink() && !cell.GetHyperlink()->target.empty()) {
                links.emplace_back(CellAddress(row, col), *cell.GetHyperlink());
            }
        });
        if (links.empty()) return;
        std::sort(links.begin(), links.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        xml << "<hyperlinks>";
        for (const auto& [at, link] : links) {
            xml << "<hyperlink ref=\"" << CellRef(at.row, at.col) << "\"";
            const bool internal = link.type == HyperlinkType::InternalCell ||
                                  link.type == HyperlinkType::InternalRange ||
                                  link.type == HyperlinkType::InternalNamedRange ||
                                  (!link.target.empty() && link.target[0] == '#');
            if (internal) {
                std::string location = link.target[0] == '#' ? link.target.substr(1) : link.target;
                xml << " location=\"" << XmlEscape(NativeFormulaToExcel(location)) << "\"";
            } else {
                std::string target = link.target;
                if (link.type == HyperlinkType::Email && target.rfind("mailto:", 0) != 0) target = "mailto:" + target;
                xml << " r:id=\"" << AddSheetRelationship(kRelHyperlink, target, true) << "\"";
            }
            if (!link.tooltip.empty()) xml << " tooltip=\"" << XmlEscape(link.tooltip) << "\"";
            if (!link.displayText.empty()) xml << " display=\"" << XmlEscape(link.displayText) << "\"";
            xml << "/>";
        }
        xml << "</hyperlinks>\n";
    }

    // The comments part (the notes' text) and the legacy VML drawing Excel
    // shows them with; both named from the worksheet's relationships.
    void BuildComments(const SpreadsheetSheet* sheet) {
        std::vector<std::pair<CellAddress, CellComment>> notes;
        sheet->ForEachCell([&](int row, int col, const SpreadsheetCell& cell) {
            if (cell.HasComment()) notes.emplace_back(CellAddress(row, col), *cell.GetComment());
        });
        if (notes.empty()) return;
        std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const size_t number = sheetParts_.size();
        std::vector<std::string> authors;
        std::ostringstream list, shapes;
        list.imbue(std::locale::classic());
        shapes.imbue(std::locale::classic());
        int shapeId = static_cast<int>(number) * 1024 + 1;
        for (const auto& [at, note] : notes) {
            auto found = std::find(authors.begin(), authors.end(), note.author);
            const size_t authorId = static_cast<size_t>(found - authors.begin());
            if (found == authors.end()) authors.push_back(note.author);
            list << "<comment ref=\"" << CellRef(at.row, at.col) << "\" authorId=\"" << authorId
                 << "\"><text><r><t xml:space=\"preserve\">" << XmlEscape(note.text) << "</t></r></text></comment>";
            shapes << "<v:shape id=\"_x0000_s" << shapeId++ << "\" type=\"#_x0000_t202\" "
                      "style=\"position:absolute;margin-left:59.25pt;margin-top:1.5pt;width:108pt;height:59.25pt;"
                      "z-index:1;visibility:" << (note.visible ? "visible" : "hidden") << "\" fillcolor=\"#ffffe1\" "
                      "o:insetmode=\"auto\"><v:fill color2=\"#ffffe1\"/><v:shadow on=\"t\" color=\"black\" "
                      "obscured=\"t\"/><v:path o:connecttype=\"none\"/><v:textbox style=\"mso-direction-alt:auto\">"
                      "<div style=\"text-align:left\"></div></v:textbox><x:ClientData ObjectType=\"Note\">"
                      "<x:MoveWithCells/><x:SizeWithCells/><x:Anchor>"
                   << (at.col + 1) << ", 15, " << at.row << ", 10, " << (at.col + 3) << ", 15, " << (at.row + 4)
                   << ", 4</x:Anchor><x:AutoFill>False</x:AutoFill><x:Row>" << at.row << "</x:Row><x:Column>"
                   << at.col << "</x:Column>" << (note.visible ? "<x:Visible/>" : "")
                   << "</x:ClientData></v:shape>\n";
        }
        std::ostringstream comments;
        comments << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 << "<comments xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><authors>";
        for (const std::string& author : authors) comments << "<author>" << XmlEscape(author) << "</author>";
        comments << "</authors><commentList>" << list.str() << "</commentList></comments>\n";

        std::ostringstream vml;
        vml << "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
               "xmlns:x=\"urn:schemas-microsoft-com:office:excel\">\n"
            << "<o:shapelayout v:ext=\"edit\"><o:idmap v:ext=\"edit\" data=\"" << number << "\"/></o:shapelayout>\n"
            << "<v:shapetype id=\"_x0000_t202\" coordsize=\"21600,21600\" o:spt=\"202\" "
               "path=\"m,l,21600r21600,l21600,xe\"><v:stroke joinstyle=\"miter\"/>"
               "<v:path gradientshapeok=\"t\" o:connecttype=\"rect\"/></v:shapetype>\n"
            << shapes.str() << "</xml>\n";

        SheetParts& parts = sheetParts_.back();
        parts.commentsXml = comments.str();
        parts.vmlXml = vml.str();
        AddSheetRelationship(kRelComments, "../comments" + std::to_string(number) + ".xml", false);
    }

    // <legacyDrawing>: the comments' VML drawing, last of the elements the
    // worksheet carries here.
    void WriteLegacyDrawing(std::ostringstream& xml) {
        SheetParts& parts = sheetParts_.back();
        if (parts.vmlXml.empty()) return;
        const std::string id = AddSheetRelationship(
            kRelVmlDrawing, "../drawings/vmlDrawing" + std::to_string(sheetParts_.size()) + ".vml", false);
        xml << "<legacyDrawing r:id=\"" << id << "\"/>\n";
    }

    // The worksheet relationship parts the charts' writer made: their
    // <Relationship>s move into the sheet's own, written once by
    // WriteSheetParts with the links' and comments'.
    void MergeSheetRelationships(std::vector<std::pair<std::string, std::string>>& parts) {
        const std::string prefix = "xl/worksheets/_rels/sheet";
        const std::string suffix = ".xml.rels";
        for (auto it = parts.begin(); it != parts.end();) {
            const std::string& path = it->first;
            if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size() + suffix.size() ||
                path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
                ++it;
                continue;
            }
            const size_t number = static_cast<size_t>(
                std::atoi(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()).c_str()));
            const std::string& xml = it->second;
            const size_t open = xml.find("<Relationships");
            const size_t bodyStart = open == std::string::npos ? std::string::npos : xml.find('>', open);
            const size_t bodyEnd = xml.rfind("</Relationships>");
            if (number == 0 || number > sheetParts_.size() || bodyStart == std::string::npos ||
                bodyEnd == std::string::npos || bodyEnd < bodyStart) {
                ++it;
                continue;
            }
            sheetParts_[number - 1].moreRelationships += xml.substr(bodyStart + 1, bodyEnd - bodyStart - 1);
            it = parts.erase(it);
        }
    }

    // Each worksheet's relationships, comments and drawing.
    bool WriteSheetParts() {
        for (size_t i = 0; i < sheetParts_.size(); ++i) {
            const SheetParts& parts = sheetParts_[i];
            const std::string number = std::to_string(i + 1);
            if (!parts.rels.empty() || !parts.moreRelationships.empty()) {
                std::ostringstream rels;
                rels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                     << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
                for (const auto& rel : parts.rels) {
                    rels << "<Relationship Id=\"" << rel.id << "\" Type=\"" << rel.type << "\" Target=\""
                         << XmlEscape(rel.target) << "\"" << (rel.external ? " TargetMode=\"External\"" : "")
                         << "/>\n";
                }
                rels << parts.moreRelationships << "\n</Relationships>\n";
                if (!zip_.AddEntry("xl/worksheets/_rels/sheet" + number + ".xml.rels", rels.str())) return false;
            }
            if (!parts.commentsXml.empty() &&
                (!zip_.AddEntry("xl/comments" + number + ".xml", parts.commentsXml) ||
                 !zip_.AddEntry("xl/drawings/vmlDrawing" + number + ".vml", parts.vmlXml))) {
                return false;
            }
        }
        return true;
    }

    // The content types the parts above add.
    std::string ExtraContentTypes() const {
        std::string types;
        bool anyVml = false;
        for (size_t i = 0; i < sheetParts_.size(); ++i) {
            if (sheetParts_[i].commentsXml.empty()) continue;
            anyVml = true;
            types += "<Override PartName=\"/xl/comments" + std::to_string(i + 1) +
                     ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
                     "spreadsheetml.comments+xml\"/>\n";
        }
        if (anyVml) {
            types += "<Default Extension=\"vml\" "
                     "ContentType=\"application/vnd.openxmlformats-officedocument.vmlDrawing\"/>\n";
        }
        return types;
    }

    // <dxfs>: the conditional formats' styles, after cellStyles.
    std::string BuildDxfsXml() const {
        if (dxfs_.empty()) return std::string();
        std::ostringstream xml;
        xml.imbue(std::locale::classic());
        xml << "<dxfs count=\"" << dxfs_.size() << "\">";
        for (const CellStyle& style : dxfs_) {
            xml << "<dxf>";
            const CellFont& font = style.font;
            const bool fontSet = font.bold || font.italic || font.strikethrough ||
                                 font.underline != UnderlineStyle::NoUnderline || !(font.color == Colors::Black);
            if (fontSet) {
                xml << "<font>";
                if (font.bold) xml << "<b/>";
                if (font.italic) xml << "<i/>";
                if (font.strikethrough) xml << "<strike/>";
                if (font.underline == UnderlineStyle::Double) xml << "<u val=\"double\"/>";
                else if (font.underline != UnderlineStyle::NoUnderline) xml << "<u/>";
                if (!(font.color == Colors::Black)) xml << "<color rgb=\"" << ColorToArgb(font.color) << "\"/>";
                xml << "</font>";
            }
            if (style.fill.HasFill()) {
                xml << "<fill><patternFill patternType=\"solid\"><fgColor rgb=\""
                    << ColorToArgb(style.fill.foregroundColor) << "\"/><bgColor rgb=\""
                    << ColorToArgb(style.fill.foregroundColor) << "\"/></patternFill></fill>";
            }
            xml << "</dxf>";
        }
        xml << "</dxfs>\n";
        return xml.str();
    }

};

} // namespace

// ===== SPREADSHEET ENTRY POINTS =====

bool UltraCanvasSpreadsheet::LoadXLSX(const std::string& filePath, const std::string& password) {
    UndoRecordingPause pause(*this);   // a new document, not edits to undo
    lastError_.clear();
    XlsxLoader loader(this);
    if (!loader.Load(filePath, password, lastError_)) {
        return false;
    }
    ClearUndoHistory();   // the loaded document starts a fresh history
    Recalculate();
    RequestAutoFitForUnsizedColumns();
    Invalidate();
    return true;
}

bool UltraCanvasSpreadsheet::SaveXLSX(const std::string& filePath, const std::string& password) {
    lastError_.clear();
    XlsxSaver saver(this);
    return saver.Save(filePath, password, lastError_);
}

} // namespace UltraCanvas
