// core/UltraCanvasSpreadsheetFileIO.cpp
// Spreadsheet file I/O implementation (ODS, XLSX, CSV); LoadFromFile also
// dispatches .xls to LoadXLS (UltraCanvasSpreadsheetXlsIO.cpp)
// Version: 1.8.0 - .ods keeps pivot tables: table:data-pilot-tables read and written
//                  (UltraCanvasSpreadsheetPivotIO.cpp)
// Version: 1.7.0 - .ods array formulas: table:number-matrix-*-spanned read (a fixed array) and written
//                  (a fixed array or a spill as it stands), A1# written as its block, inline arrays,
//                  COM.MICROSOFT. function names; a plain formula is read as one value; numbers in
//                  formulas are written to read back exactly
// Version: 1.6.0 - templates: .ots read and written as an .ods (its template media type), .xltx as an
//                  .xlsx, .xltm read
// Version: 1.5.0 - .ods keeps conditional formats (calcext:conditional-formats, their cell styles in
//                  styles.xml), validations (table:content-validations), comments (office:annotation)
//                  and links (text:a); a cell with only a note or a validation is written
// Version: 1.4.0 - .ods keeps a sheet's charts: draw:frame objects in table:shapes, read from cells too
//                  (UltraCanvasSpreadsheetChartIO.cpp)
// Version: 1.3.0 - a cell's number is written to read back exactly (it kept six significant digits)
// Version: 1.2.0 - .ods keeps what a sheet holds beyond its cells: frozen panes (settings.xml), hidden
//                  rows and columns (table:visibility), named ranges of the workbook and of each sheet
//                  (table:named-expressions), the auto-filter (table:database-range with filter buttons,
//                  value filters), the print area (table:print-ranges), print titles (header rows and
//                  columns), page breaks and the page layout, header and footer (styles.xml); merged
//                  cells written with covered cells and read with an empty first cell; content.xml
//                  numbers dot-decimal, and its "of:" formula namespace declared (LibreOffice read
//                  every formula as an error without it)
// Version: 1.1.0 - a password-protected .ods opened with its password (UltraCanvasOdfEncryption.h)
// Version: 1.0.3 - CSV rows through CSVFormatRecord (UltraCanvasCSVImport.h); with "quote all fields"
//                  an empty cell is written "" as every other field is
// Version: 1.0.2
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include <stdexcept>   // predeclare std::runtime_error for libspecific/Cairo/ImageCairo.h
#include "UltraCanvasTextUtils.h"   // TryParseFloat / ParseFloatClassic - dot-decimal, non-throwing
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetCell.h"
#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasSpreadsheetOdsFormula.h"
#include "UltraCanvasOdfEncryption.h"   // a password-protected .ods
#include <fstream>
#include <sstream>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <climits>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <filesystem>

// For ZIP handling (ODS/XLSX are ZIP archives)
// Reading uses miniz directly (vendored: third_party/miniz); writing goes
// through UCZipPackageWriter for ODF-compliant archives (see ODSSaver).
#include "miniz.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasSpreadsheetChart.h"   // the sheets' charts
#include "UltraCanvasSpreadsheetPivot.h"   // the sheets' pivot tables

// For XML parsing
#include "tinyxml2.h"
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {

// ============================================================================
// ODS FORMAT CONSTANTS
// ============================================================================

namespace ODS {
    constexpr const char* MIMETYPE = "application/vnd.oasis.opendocument.spreadsheet";
    constexpr const char* TEMPLATE_MIMETYPE = "application/vnd.oasis.opendocument.spreadsheet-template";
    constexpr const char* CONTENT_XML = "content.xml";
    constexpr const char* STYLES_XML = "styles.xml";
    constexpr const char* META_XML = "meta.xml";
    constexpr const char* SETTINGS_XML = "settings.xml";
    constexpr const char* MANIFEST_XML = "META-INF/manifest.xml";
    
    // ODS namespaces
    constexpr const char* NS_OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
    constexpr const char* NS_TABLE = "urn:oasis:names:tc:opendocument:xmlns:table:1.0";
    constexpr const char* NS_TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0";
    constexpr const char* NS_STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
    constexpr const char* NS_FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
    constexpr const char* NS_NUMBER = "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0";
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

namespace {

// Parse color from hex string (#RRGGBB or #AARRGGBB)
Color ParseHexColor(const std::string& colorStr) {
    if (colorStr.empty() || colorStr[0] != '#') {
        return Colors::Black;
    }
    
    std::string hex = colorStr.substr(1);
    unsigned int value = 0;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> value;
    
    if (hex.length() == 6) {
        return Color((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    } else if (hex.length() == 8) {
        return Color((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, (value >> 24) & 0xFF);
    }
    return Colors::Black;
}

// Convert color to hex string
std::string ColorToHex(const Color& color) {
    std::stringstream ss;
    ss << "#" << std::hex << std::setfill('0')
       << std::setw(2) << static_cast<int>(color.r)
       << std::setw(2) << static_cast<int>(color.g)
       << std::setw(2) << static_cast<int>(color.b);
    return ss.str();
}

// Parse date from ODS format (YYYY-MM-DD)
double ParseODSDate(const std::string& dateStr) {
    if (dateStr.empty()) return 0.0;
    
    int year = 0, month = 0, day = 0;
    if (sscanf(dateStr.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        std::time_t time = std::mktime(&tm);
        return static_cast<double>(time) / 86400.0 + 25569.0;
    }
    return 0.0;
}

// Parse time from ODS format (PT12H30M45S)
double ParseODSTime(const std::string& timeStr) {
    if (timeStr.empty() || timeStr[0] != 'P') return 0.0;
    
    double hours = 0, minutes = 0, seconds = 0;
    size_t pos = timeStr.find('T');
    if (pos != std::string::npos) {
        std::string timePart = timeStr.substr(pos + 1);
        size_t hPos = timePart.find('H');
        size_t mPos = timePart.find('M');
        size_t sPos = timePart.find('S');
        
        if (hPos != std::string::npos) {
            TryParseFloat(timePart.substr(0, hPos), hours);
        }
        if (mPos != std::string::npos) {
            size_t start = (hPos != std::string::npos) ? hPos + 1 : 0;
            TryParseFloat(timePart.substr(start, mPos - start), minutes);
        }
        if (sPos != std::string::npos) {
            size_t start = (mPos != std::string::npos) ? mPos + 1 : ((hPos != std::string::npos) ? hPos + 1 : 0);
            TryParseFloat(timePart.substr(start, sPos - start), seconds);
        }
    }
    
    return (hours * 3600 + minutes * 60 + seconds) / 86400.0;
}

// Parse border style
BorderStyle ParseBorderStyle(const std::string& styleStr) {
    if (styleStr == "solid") return BorderStyle::Thin;
    if (styleStr == "double") return BorderStyle::Double;
    if (styleStr == "dashed") return BorderStyle::Dashed;
    if (styleStr == "dotted") return BorderStyle::Dotted;
    return BorderStyle::NoBorder;
}

// Get XML attribute with default
std::string GetAttr(const tinyxml2::XMLElement* elem, const char* name, const std::string& defaultVal = "") {
    if (!elem) return defaultVal;
    const char* val = elem->Attribute(name);
    return val ? val : defaultVal;
}

int GetAttrInt(const tinyxml2::XMLElement* elem, const char* name, int defaultVal = 0) {
    if (!elem) return defaultVal;
    const char* val = elem->Attribute(name);
    return val ? std::atoi(val) : defaultVal;
}

double GetAttrDouble(const tinyxml2::XMLElement* elem, const char* name, double defaultVal = 0.0) {
    if (!elem) return defaultVal;
    const char* val = elem->Attribute(name);
    double parsed = defaultVal;
    if (val) TryParseFloat(val, parsed);
    return parsed;
}

// Recursively gather the text of an ODF text element (text:p / text:span /
// text:a ...). tinyxml2's GetText() only returns a leading text node, so
// nested runs — most importantly a hyperlink's <text:a>content</text:a> —
// would otherwise be dropped. The ODF inline whitespace elements are honoured:
//   text:s -> N spaces (text:c), text:tab -> tab, text:line-break -> newline.
void AppendElementText(const tinyxml2::XMLElement* elem, std::string& out) {
    for (const tinyxml2::XMLNode* node = elem->FirstChild();
         node; node = node->NextSibling()) {
        if (const tinyxml2::XMLText* textNode = node->ToText()) {
            out += textNode->Value();
        } else if (const tinyxml2::XMLElement* child = node->ToElement()) {
            std::string name = child->Name();
            if (name == "text:s") {
                int count = GetAttrInt(child, "text:c", 1);
                if (count > 0) out.append(static_cast<size_t>(count), ' ');
            } else if (name == "text:tab") {
                out += '\t';
            } else if (name == "text:line-break") {
                out += '\n';
            } else {
                AppendElementText(child, out);  // text:span, text:a, ...
            }
        }
    }
}

// Concatenate every text:p paragraph of a cell, newline-separated.
std::string GetCellText(const tinyxml2::XMLElement* cellElem) {
    std::string out;
    bool first = true;
    for (const tinyxml2::XMLElement* p = cellElem->FirstChildElement("text:p");
         p; p = p->NextSiblingElement("text:p")) {
        if (!first) out += '\n';
        first = false;
        AppendElementText(p, out);
    }
    return out;
}

// First hyperlink target (xlink:href on a text:a) found anywhere in the cell,
// or empty when the cell holds no link.
std::string FindCellHyperlink(const tinyxml2::XMLElement* elem) {
    for (const tinyxml2::XMLElement* child = elem->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        if (std::string(child->Name()) == "office:annotation") continue;   // a note's link is the note's
        if (std::string(child->Name()) == "text:a") {
            std::string href = GetAttr(child, "xlink:href");
            if (!href.empty()) return href;
        }
        std::string nested = FindCellHyperlink(child);
        if (!nested.empty()) return nested;
    }
    return "";
}

// ===== VALIDATIONS, CONDITIONAL FORMATS: CONDITION TEXT =====

std::string TrimOdf(const std::string& text) {
    const size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

// "of:cell-content()" -> "cell-content()": the namespace prefix a condition
// (and each part of it) may carry.
std::string StripOdfPrefix(const std::string& text) {
    std::string t = TrimOdf(text);
    const size_t colon = t.find(':');
    const size_t paren = t.find('(');
    if (colon != std::string::npos && colon < paren && colon > 0 &&
        std::all_of(t.begin(), t.begin() + static_cast<long>(colon),
                    [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; })) {
        t = t.substr(colon + 1);
    }
    return t;
}

// The arguments of "name(a,b)" / "name(a;b)" split at the top level (outside
// quotes, brackets and parentheses); `text` is what is inside the parentheses.
std::vector<std::string> SplitOdfArguments(const std::string& text) {
    std::vector<std::string> args;
    std::string current;
    int depth = 0;
    bool inQuote = false;
    for (char c : text) {
        if (c == '"') inQuote = !inQuote;
        if (!inQuote) {
            if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
            else if ((c == ',' || c == ';') && depth == 0) {
                args.push_back(TrimOdf(current));
                current.clear();
                continue;
            }
        }
        current += c;
    }
    if (!TrimOdf(current).empty() || !args.empty()) args.push_back(TrimOdf(current));
    return args;
}

// "name(...)" -> what is inside the parentheses, when `text` is a call of
// `name`; nothing otherwise.
std::optional<std::string> OdfCallBody(const std::string& text, const std::string& name) {
    const std::string t = TrimOdf(text);
    if (t.size() < name.size() + 2 || t.compare(0, name.size(), name) != 0 || t[name.size()] != '(' ||
        t.back() != ')') {
        return std::nullopt;
    }
    return t.substr(name.size() + 1, t.size() - name.size() - 2);
}

// An ODF expression ("[.B1]*2") in the engine's syntax, with its '='.
std::string OdsExpressionToNative(const std::string& expression) {
    std::string native = OdsFormulaToNative("of:=" + TrimOdf(expression));
    if (native.empty() || native[0] != '=') native = "=" + native;
    return native;
}

bool IsOdfNumber(const std::string& text) {
    const std::string t = TrimOdf(text);
    if (t.empty()) return false;
    double value = 0.0;
    const char* first = t.c_str();
    const char* last = first + t.size();
    if (*first == '+' || *first == '-') ++first;
    return first != last && ParseFloatClassic(first, last, value) == last;
}

// An operand read back: a number or "text" as it is, anything else a formula.
std::string OdsOperandToNative(const std::string& operand) {
    const std::string t = TrimOdf(operand);
    if (t.empty() || IsOdfNumber(t) || (t.size() >= 2 && t.front() == '"' && t.back() == '"')) return t;
    return OdsExpressionToNative(t);
}

std::string UnquoteOdf(const std::string& text) {
    const std::string t = TrimOdf(text);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"') return t;
    std::string out;
    for (size_t i = 1; i + 1 < t.size(); ++i) {
        out += t[i];
        if (t[i] == '"' && i + 2 < t.size() && t[i + 1] == '"') ++i;
    }
    return out;
}

// A comparison at the start of `text` ("<=5"): the operator, and the rest.
bool ReadOdfComparison(const std::string& text, ValidationOperator& op, std::string& rest) {
    static const std::pair<const char*, ValidationOperator> kOperators[] = {
        {"!=", ValidationOperator::NotEqual}, {"<>", ValidationOperator::NotEqual},
        {"<=", ValidationOperator::LessOrEqual}, {">=", ValidationOperator::GreaterOrEqual},
        {"<", ValidationOperator::LessThan}, {">", ValidationOperator::GreaterThan},
        {"=", ValidationOperator::Equal}};
    const std::string t = TrimOdf(text);
    for (const auto& [symbol, value] : kOperators) {
        const size_t n = std::char_traits<char>::length(symbol);
        if (t.compare(0, n, symbol) == 0) {
            op = value;
            rest = t.substr(n);
            return true;
        }
    }
    return false;
}

// A table:content-validation's table:condition into a rule (type, operator,
// formula1 / formula2); false when it is not one this reads.
bool ParseOdfValidationCondition(const std::string& condition, DataValidation& rule) {
    // The parts joined by " and ": a type test and a comparison.
    std::vector<std::string> parts;
    {
        std::string t = TrimOdf(condition);
        size_t start = 0;
        int depth = 0;
        bool inQuote = false;
        for (size_t i = 0; i < t.size(); ++i) {
            const char c = t[i];
            if (c == '"') inQuote = !inQuote;
            if (inQuote) continue;
            if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
            else if (depth == 0 && t.compare(i, 5, " and ") == 0) {
                parts.push_back(StripOdfPrefix(t.substr(start, i - start)));
                start = i + 5;
                i += 4;
            }
        }
        parts.push_back(StripOdfPrefix(t.substr(start)));
    }
    rule.type = ValidationType::Decimal;
    bool understood = false;
    for (const std::string& part : parts) {
        if (part == "cell-content-is-whole-number()") { rule.type = ValidationType::WholeNumber; continue; }
        if (part == "cell-content-is-decimal-number()") { rule.type = ValidationType::Decimal; continue; }
        if (part == "cell-content-is-date()") { rule.type = ValidationType::Date; continue; }
        if (part == "cell-content-is-time()") { rule.type = ValidationType::Time; continue; }
        if (auto body = OdfCallBody(part, "cell-content-is-in-list")) {
            rule.type = ValidationType::List;
            std::vector<std::string> items = SplitOdfArguments(*body);
            if (items.size() == 1 && !items[0].empty() && items[0].front() != '"') {
                rule.formula1 = OdsExpressionToNative(items[0]);   // cells
            } else {
                std::string list;
                for (const std::string& item : items) list += (list.empty() ? "" : ",") + UnquoteOdf(item);
                rule.formula1 = list;
            }
            understood = true;
            continue;
        }
        if (auto body = OdfCallBody(part, "is-true-formula")) {
            rule.type = ValidationType::Custom;
            rule.formula1 = OdsExpressionToNative(*body);
            understood = true;
            continue;
        }
        const bool textLength = part.rfind("cell-content-text-length", 0) == 0;
        const std::string between = textLength ? "cell-content-text-length-is-between" : "cell-content-is-between";
        const std::string notBetween = textLength ? "cell-content-text-length-is-not-between"
                                                  : "cell-content-is-not-between";
        if (textLength) rule.type = ValidationType::TextLength;
        std::optional<std::string> body = OdfCallBody(part, notBetween);
        const bool isNot = body.has_value();
        if (!body) body = OdfCallBody(part, between);
        if (body) {
            const std::vector<std::string> args = SplitOdfArguments(*body);
            rule.op = isNot ? ValidationOperator::NotBetween : ValidationOperator::Between;
            rule.formula1 = args.size() > 0 ? OdsOperandToNative(args[0]) : std::string();
            rule.formula2 = args.size() > 1 ? OdsOperandToNative(args[1]) : std::string();
            understood = true;
            continue;
        }
        const std::string subject = textLength ? "cell-content-text-length()" : "cell-content()";
        if (part.rfind(subject, 0) == 0) {
            std::string rest;
            if (ReadOdfComparison(part.substr(subject.size()), rule.op, rest)) {
                rule.formula1 = OdsOperandToNative(rest);
                understood = true;
            }
        }
    }
    return understood;
}

// A calcext:condition's calcext:value into a rule; false when it is not one
// this reads. Formulas are written for `anchor` (the base cell).
bool ParseOdfConditionValue(const std::string& value, ConditionalFormatRule& rule) {
    const std::string t = TrimOdf(value);
    auto textRule = [&](const char* name, ConditionalFormatType type) {
        if (auto body = OdfCallBody(t, name)) {
            rule.type = type;
            rule.formula1 = UnquoteOdf(*body);
            return true;
        }
        return false;
    };
    if (auto body = OdfCallBody(t, "formula-is")) {
        rule.type = ConditionalFormatType::Expression;
        rule.formula1 = OdsExpressionToNative(*body);
        return true;
    }
    for (const char* name : {"between", "not-between"}) {
        if (auto body = OdfCallBody(t, name)) {
            const std::vector<std::string> args = SplitOdfArguments(*body);
            rule.type = ConditionalFormatType::CellValue;
            rule.op = std::string(name) == "between" ? ValidationOperator::Between : ValidationOperator::NotBetween;
            rule.formula1 = args.size() > 0 ? OdsOperandToNative(args[0]) : std::string();
            rule.formula2 = args.size() > 1 ? OdsOperandToNative(args[1]) : std::string();
            return true;
        }
    }
    if (textRule("contains-text", ConditionalFormatType::ContainsText) ||
        textRule("not-contains-text", ConditionalFormatType::NotContainsText) ||
        textRule("begins-with", ConditionalFormatType::BeginsWithText) ||
        textRule("ends-with", ConditionalFormatType::EndsWithText)) {
        return true;
    }
    struct Rank { const char* name; bool bottom; bool percent; };
    for (const Rank& rank : {Rank{"top-elements", false, false}, Rank{"bottom-elements", true, false},
                             Rank{"top-percent", false, true}, Rank{"bottom-percent", true, true}}) {
        if (auto body = OdfCallBody(t, rank.name)) {
            rule.type = ConditionalFormatType::Top10;
            rule.formula1 = TrimOdf(*body);
            rule.bottom = rank.bottom;
            rule.percent = rank.percent;
            return true;
        }
    }
    if (t == "is-error") { rule.type = ConditionalFormatType::ContainsErrors; return true; }
    if (t == "is-no-error") { rule.type = ConditionalFormatType::NotContainsErrors; return true; }
    if (t == "duplicate") { rule.type = ConditionalFormatType::DuplicateValues; return true; }
    if (t == "unique") { rule.type = ConditionalFormatType::UniqueValues; return true; }
    if (t == "above-average" || t == "below-average" || t == "above-equal-average" || t == "below-equal-average") {
        rule.type = ConditionalFormatType::AboveAverage;
        rule.aboveAverage = t.rfind("above", 0) == 0;
        rule.equalAverage = t.find("equal") != std::string::npos;
        return true;
    }
    std::string rest;
    if (ReadOdfComparison(t, rule.op, rest)) {
        rule.type = ConditionalFormatType::CellValue;
        rule.formula1 = OdsOperandToNative(rest);
        return true;
    }
    return false;
}

ConditionalThreshold OdfThreshold(const std::string& type, const std::string& value) {
    if (type == "maximum" || type == "auto-maximum") return ConditionalThreshold(ConditionalThresholdKind::Highest);
    if (type == "percent") return ConditionalThreshold(ConditionalThresholdKind::Percent, value);
    if (type == "percentile") return ConditionalThreshold(ConditionalThresholdKind::Percentile, value);
    if (type == "number") {
        return IsOdfNumber(value) ? ConditionalThreshold(ConditionalThresholdKind::Number, value)
                                  : ConditionalThreshold(ConditionalThresholdKind::Formula, OdsOperandToNative(value));
    }
    if (type == "formula") return ConditionalThreshold(ConditionalThresholdKind::Formula, OdsOperandToNative(value));
    return ConditionalThreshold(ConditionalThresholdKind::Lowest);
}

const char* OdfThresholdType(ConditionalThresholdKind kind) {
    switch (kind) {
        case ConditionalThresholdKind::Lowest: return "minimum";
        case ConditionalThresholdKind::Highest: return "maximum";
        case ConditionalThresholdKind::Number: return "number";
        case ConditionalThresholdKind::Percent: return "percent";
        case ConditionalThresholdKind::Percentile: return "percentile";
        case ConditionalThresholdKind::Formula: return "formula";
    }
    return "minimum";
}

const char* OdfComparison(ValidationOperator op) {
    switch (op) {
        case ValidationOperator::Equal: return "=";
        case ValidationOperator::NotEqual: return "!=";
        case ValidationOperator::GreaterThan: return ">";
        case ValidationOperator::LessThan: return "<";
        case ValidationOperator::GreaterOrEqual: return ">=";
        case ValidationOperator::LessOrEqual: return "<=";
        default: return "=";
    }
}

// ===== ODF ADDRESSES, PAGE LAYOUT, HEADERS =====

// A sheet name as an ODF address writes it: bare when it is a plain
// identifier, else quoted with '' for a quote inside.
std::string OdfSheetName(const std::string& name) {
    bool plain = !name.empty();
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) { plain = false; break; }
    }
    if (plain) return name;
    std::string quoted = "'";
    for (char c : name) {
        if (c == '\'') quoted += '\'';
        quoted += c;
    }
    return quoted + "'";
}

// $Sheet1.$A$1, or .$A$1 for an address that names no sheet.
std::string OdfCellAddress(const std::string& sheet, int row, int col) {
    std::string out = sheet.empty() ? "." : "$" + OdfSheetName(sheet) + ".";
    return out + "$" + CellAddress::ColumnToLetter(col) + "$" + std::to_string(row + 1);
}

// $Sheet1.$A$1:$Sheet1.$D$20 - the sheet on both ends, which every reader takes.
std::string OdfRangeAddress(const std::string& sheet, const CellRange& range) {
    return OdfCellAddress(sheet, range.start.row, range.start.col) + ":" +
           OdfCellAddress(sheet, range.end.row, range.end.col);
}

// One end of an ODF cell range address: [$][Sheet.][$]COL[$]ROW, where the
// sheet may be quoted. Reads from `i` on; row / col are -1 where absent.
bool ParseOdfCorner(const std::string& text, size_t& i, std::string& sheet, int& row, int& col) {
    sheet.clear();
    row = col = -1;
    size_t start = i;
    if (i < text.size() && text[i] == '$') ++i;
    if (i < text.size() && text[i] == '\'') {
        ++i;
        while (i < text.size()) {
            if (text[i] == '\'') {
                if (i + 1 < text.size() && text[i + 1] == '\'') { sheet += '\''; i += 2; continue; }
                ++i;
                break;
            }
            sheet += text[i++];
        }
        if (i >= text.size() || text[i] != '.') return false;
        ++i;
    } else {
        // A bare sheet name runs to the last '.' before the end of this corner.
        size_t end = text.find(':', i);
        if (end == std::string::npos) end = text.size();
        size_t dot = text.rfind('.', end == 0 ? 0 : end - 1);
        if (dot != std::string::npos && dot >= i && dot < end) {
            sheet = text.substr(i, dot - i);
            i = dot + 1;
        } else {
            i = start;
        }
    }
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
    if (letters == 0 && digits == 0) return false;
    if (letters > 0) col = c - 1;
    if (digits > 0) {
        if (r < 1) return false;
        row = static_cast<int>(r) - 1;
    }
    return true;
}

// "$Sheet1.$A$1:.$B$4", "Sheet1.A1:Sheet1.D20", "$'My sheet'.$A$1" -> the
// sheet of its start and the range, with that sheet on both ends.
bool ParseOdfRangeAddress(const std::string& text, std::string& sheet, CellRange& range) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    std::string endSheet;
    int r1, c1, r2, c2;
    if (!ParseOdfCorner(text, i, sheet, r1, c1)) return false;
    if (i < text.size() && text[i] == ':') {
        ++i;
        if (!ParseOdfCorner(text, i, endSheet, r2, c2)) return false;
    } else {
        r2 = r1;
        c2 = c1;
    }
    if ((r1 < 0) != (r2 < 0) || (c1 < 0) != (c2 < 0) || (r1 < 0 && c1 < 0)) return false;
    if (r1 < 0) { r1 = 0; r2 = SpreadsheetLimits::MaxRows - 1; }
    if (c1 < 0) { c1 = 0; c2 = SpreadsheetLimits::MaxColumns - 1; }
    range = CellRange(r1, c1, r2, c2);
    if (!range.IsValid()) return false;
    range.start.sheetName = range.end.sheetName = sheet;
    return true;
}

// A space-separated list of range addresses (table:print-ranges), split where
// a space is not inside a quoted sheet name.
std::vector<std::string> SplitOdfRangeList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    for (char c : text) {
        if (c == '\'') quoted = !quoted;
        if (c == ' ' && !quoted) {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// "0.75in", "1.905cm", "19.05mm", "54pt" -> inches; `fallback` when it is none.
double OdfLengthToInches(const std::string& value, double fallback) {
    double number = 0.0;
    if (!TryParseFloat(value, number)) return fallback;
    size_t i = 0;
    while (i < value.size() && (std::isdigit(static_cast<unsigned char>(value[i])) ||
                                value[i] == '.' || value[i] == '-' || value[i] == '+' ||
                                std::isspace(static_cast<unsigned char>(value[i])))) {
        ++i;
    }
    std::string unit = value.substr(i);
    if (unit == "in" || unit == "inch") return number;
    if (unit == "cm") return number / 2.54;
    if (unit == "mm") return number / 25.4;
    if (unit == "pt") return number / 72.0;
    if (unit == "pc") return number / 6.0;
    return fallback;
}

std::string OdfInches(double inches) {
    return FormatFloatClassic(inches, 6) + "in";
}

// Portrait width and height in inches of the paper sizes PrintSettings names
// (B4 and B5 are the JIS sizes, as Excel's paperSize 12 and 13 are).
bool PaperDimensions(PaperSize size, double& width, double& height) {
    switch (size) {
        case PaperSize::Letter: width = 8.5; height = 11.0; return true;
        case PaperSize::Legal: width = 8.5; height = 14.0; return true;
        case PaperSize::A3: width = 297 / 25.4; height = 420 / 25.4; return true;
        case PaperSize::A4: width = 210 / 25.4; height = 297 / 25.4; return true;
        case PaperSize::A5: width = 148 / 25.4; height = 210 / 25.4; return true;
        case PaperSize::B4: width = 257 / 25.4; height = 364 / 25.4; return true;
        case PaperSize::B5: width = 182 / 25.4; height = 257 / 25.4; return true;
        default: return false;
    }
}

PaperSize PaperSizeFromDimensions(double width, double height) {
    const double shortSide = std::min(width, height), longSide = std::max(width, height);
    for (PaperSize size : {PaperSize::Letter, PaperSize::Legal, PaperSize::A3, PaperSize::A4,
                           PaperSize::A5, PaperSize::B4, PaperSize::B5}) {
        double w = 0, h = 0;
        PaperDimensions(size, w, h);
        if (std::abs(w - shortSide) < 0.08 && std::abs(h - longSide) < 0.08) return size;
    }
    return PaperSize::Custom;
}

// A header or footer text in Excel's codes (PrintSettings::headerLeft ...) as
// the ODF paragraphs of a header region: &P, &N, &A, &F, &D and &T become the
// matching text fields, && a '&', a line break a new paragraph; the codes that
// only format (&B, &"Font", &12, &K00FF00 ...) have no ODF form and are left out.
std::string ExcelCodesToOdfParagraphs(const std::string& text) {
    std::string out = "<text:p>";
    auto escape = [&](char c) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') { out += "</text:p><text:p>"; continue; }
        if (c != '&' || i + 1 >= text.size()) { escape(c); continue; }
        char code = text[++i];
        switch (code) {
            case '&': out += "&amp;"; break;
            case 'P': out += "<text:page-number>1</text:page-number>"; break;
            case 'N': out += "<text:page-count>1</text:page-count>"; break;
            case 'A': out += "<text:sheet-name>Sheet</text:sheet-name>"; break;
            case 'F': out += "<text:file-name>File</text:file-name>"; break;
            case 'D': out += "<text:date/>"; break;
            case 'T': out += "<text:time/>"; break;
            case '"': {
                size_t close = text.find('"', i + 1);
                i = close == std::string::npos ? text.size() : close;
                break;
            }
            case 'K':
                i = std::min(text.size() - 1, i + 6);
                break;
            default:
                while (i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]))) ++i;
                break;
        }
    }
    return out + "</text:p>";
}

// The other way: a header region's paragraphs as Excel's codes.
void AppendOdfAsExcelCodes(const tinyxml2::XMLElement* elem, std::string& out) {
    for (const tinyxml2::XMLNode* node = elem->FirstChild(); node; node = node->NextSibling()) {
        if (const tinyxml2::XMLText* textNode = node->ToText()) {
            for (const char* p = textNode->Value(); *p; ++p) {
                if (*p == '&') out += "&&";
                else out += *p;
            }
            continue;
        }
        const tinyxml2::XMLElement* child = node->ToElement();
        if (!child) continue;
        std::string name = child->Name();
        if (name == "text:page-number") out += "&P";
        else if (name == "text:page-count") out += "&N";
        else if (name == "text:sheet-name") out += "&A";
        else if (name == "text:file-name" || name == "text:title") out += "&F";
        else if (name == "text:date") out += "&D";
        else if (name == "text:time") out += "&T";
        else if (name == "text:s") out.append(static_cast<size_t>(std::max(1, GetAttrInt(child, "text:c", 1))), ' ');
        else if (name == "text:tab") out += '\t';
        else if (name == "text:line-break") out += '\n';
        else AppendOdfAsExcelCodes(child, out);
    }
}

std::string OdfRegionAsExcelCodes(const tinyxml2::XMLElement* region) {
    std::string out;
    bool first = true;
    for (const tinyxml2::XMLElement* p = region->FirstChildElement("text:p"); p;
         p = p->NextSiblingElement("text:p")) {
        if (!first) out += '\n';
        first = false;
        AppendOdfAsExcelCodes(p, out);
    }
    return out;
}

} // anonymous namespace

// ============================================================================
// ODS LOADER
// ============================================================================

// A <table:table-column> repeated more than this many times is stating a
// sheet-wide default rather than sizing individual columns.
constexpr int kWideColumnRun = 512;

class ODSLoader {
private:
    UltraCanvasSpreadsheet* spreadsheet_;
    mz_zip_archive zip_;
    // Ours: miniz reads the archive through it but never closes it.
    std::FILE* file_ = nullptr;
    std::map<std::string, CellStyle> styles_;
    // Number/currency/date/percentage formats keyed by their ODF data-style
    // name (e.g. "N110"), referenced from a cell style's style:data-style-name.
    std::map<std::string, NumberFormat> numberFormats_;
    // Column/row styles keyed by style:name. ODF never puts the size on the
    // <table:table-column>/<table:table-row> element itself - it lives in a
    // style:style of family "table-column"/"table-row" that the element points
    // at through table:style-name - so these have to be resolved to get any
    // column width out of a real document.
    struct ODSColumnStyle {
        int widthPixels = 0;        // 0 = the style carries no width
        bool useOptimalWidth = false;  // style:use-optimal-column-width="true"
        bool pageBreakBefore = false;  // fo:break-before="page": a manual page break
    };
    struct ODSRowStyle {
        int heightPixels = 0;
        bool useOptimalHeight = false;
        bool pageBreakBefore = false;
    };
    std::map<std::string, ODSColumnStyle> columnStyles_;
    std::map<std::string, ODSRowStyle> rowStyles_;
    // Print settings: a sheet's table style (family "table", in content.xml)
    // names a master page (styles.xml), which names a page layout and holds
    // the header and footer.
    struct ODSPageLayout {
        double pageWidth = 0, pageHeight = 0;           // inches, 0 = not stated
        std::string orientation;
        double marginTop = -1, marginBottom = -1, marginLeft = -1, marginRight = -1;
        bool hasPrintTokens = false;
        std::string printTokens;                       // style:print
        std::string centering;                         // style:table-centering
        std::string scaleTo;                           // style:scale-to ("80%")
        int scaleToX = -1, scaleToY = -1;              // fit to pages wide / tall
        double headerHeight = -1, footerHeight = -1;   // header / footer height, inches
    };
    struct ODSMasterPage {
        std::string layout;
        bool hasHeader = false, hasFooter = false;
        std::string headerLeft, headerCenter, headerRight;
        std::string footerLeft, footerCenter, footerRight;
    };
    std::map<std::string, ODSPageLayout> pageLayouts_;
    std::map<std::string, ODSMasterPage> masterPages_;
    std::map<std::string, std::string> tableMasterPages_;   // table style -> master page
    std::string error_;
    // A password-protected file's package, decrypted: miniz reads it from here.
    std::vector<uint8_t> decrypted_;

public:
    ODSLoader(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {
        memset(&zip_, 0, sizeof(zip_));
    }

    ~ODSLoader() {
        mz_zip_reader_end(&zip_);
        if (file_) std::fclose(file_);
    }

    const std::string& GetError() const { return error_; }

    bool Load(const std::string& filePath, const std::string& password = std::string()) {
        // A password-protected .ods is decrypted into an ordinary package in
        // memory first, and read from there.
        switch (DecryptOdfPackageFile(filePath, password, decrypted_, error_)) {
            case OfficeDecryptStatus::NotEncrypted:
                error_.clear();
                break;
            case OfficeDecryptStatus::Decrypted:
                if (!mz_zip_reader_init_mem(&zip_, decrypted_.data(), decrypted_.size(), 0)) {
                    error_ = "The decrypted OpenDocument spreadsheet is not valid: " + filePath;
                    return false;
                }
                return LoadOpened(filePath);
            default:
                error_ += ": " + filePath;
                return false;
        }
        // Open ZIP archive. ODS files are ZIP containers, so a failure here is
        // either a file-access problem (locked / no permission / missing) or the
        // file simply is not a valid OpenDocument archive — tell them which.
        // Opened through OpenFileUtf8, not miniz's own fopen: that converts a
        // UTF-8 name only under MSVC and 64-bit MinGW, and there opens with
        // _wfopen_s, which locks other programs out of writing the file.
        file_ = OpenFileUtf8(filePath, "rb");
        if (!file_ || !mz_zip_reader_init_cfile(&zip_, file_, 0, 0)) {
            std::string openError = CSVDescribeOpenError(filePath);
            error_ = openError.empty()
                ? "The file is not a valid OpenDocument spreadsheet (.ods): " + filePath
                : openError;
            return false;
        }
        return LoadOpened(filePath);
    }

    // The rest of Load, once zip_ holds the archive (the file, or the
    // decrypted package).
    bool LoadOpened(const std::string& filePath) {
        // Verify mimetype
        std::string mimetype = ReadFileFromZip("mimetype");
        if (mimetype.find("opendocument.spreadsheet") == std::string::npos) {
            error_ = "This file is not an OpenDocument spreadsheet (.ods): " + filePath;
            return false;
        }

        // Load styles first
        std::string stylesXml = ReadFileFromZip(ODS::STYLES_XML);
        if (!stylesXml.empty()) {
            ParseStyles(stylesXml);
        }

        // Load content.xml (main data)
        std::string contentXml = ReadFileFromZip(ODS::CONTENT_XML);
        if (contentXml.empty()) {
            error_ = "The spreadsheet file is corrupt (missing content): " + filePath;
            return false;
        }

        // Also parse automatic styles from content.xml
        ParseContentStyles(contentXml);

        // Parse spreadsheet content
        if (!ParseContent(contentXml)) {
            if (error_.empty())
                error_ = "The spreadsheet content could not be read: " + filePath;
            return false;
        }

        // The view settings: frozen panes.
        std::string settingsXml = ReadFileFromZip(ODS::SETTINGS_XML);
        if (!settingsXml.empty()) {
            ParseSettings(settingsXml);
        }
        // The charts, each an object of the package.
        ReadOdsCharts(*spreadsheet_, contentXml, [this](const std::string& part) { return ReadFileFromZip(part); });
        // The pivot tables (data pilots), now that every sheet is read.
        ReadOdsPivots(*spreadsheet_, contentXml);
        return true;
    }

private:
    std::string ReadFileFromZip(const std::string& filename) {
        int index = mz_zip_reader_locate_file(&zip_, filename.c_str(), nullptr, 0);
        if (index < 0) return "";
        
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip_, index, &stat)) return "";
        
        std::string content;
        content.resize(static_cast<size_t>(stat.m_uncomp_size));
        
        if (!mz_zip_reader_extract_to_mem(&zip_, index, content.data(), content.size(), 0)) {
            return "";
        }
        
        return content;
    }
    
    void ParseStyles(const std::string& xml) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        
        // Find office:document-styles / office:styles
        auto* root = doc.RootElement();
        if (!root) return;
        
        // Number/data styles must be parsed before the cell styles that
        // reference them via style:data-style-name.
        auto* officeStyles = root->FirstChildElement("office:styles");
        auto* automaticStyles = root->FirstChildElement("office:automatic-styles");
        if (officeStyles) ParseNumberStyles(officeStyles);
        if (automaticStyles) ParseNumberStyles(automaticStyles);
        if (officeStyles) ParseStyleElements(officeStyles);
        if (automaticStyles) ParseStyleElements(automaticStyles);
        if (officeStyles) ParsePageLayouts(officeStyles);
        if (automaticStyles) ParsePageLayouts(automaticStyles);
        if (auto* masterStyles = root->FirstChildElement("office:master-styles")) {
            ParseMasterPages(masterStyles);
        }
    }

    void ParsePageLayouts(tinyxml2::XMLElement* stylesElem) {
        for (auto* layout = stylesElem->FirstChildElement("style:page-layout"); layout;
             layout = layout->NextSiblingElement("style:page-layout")) {
            ODSPageLayout info;
            if (auto* props = layout->FirstChildElement("style:page-layout-properties")) {
                info.pageWidth = OdfLengthToInches(GetAttr(props, "fo:page-width"), 0);
                info.pageHeight = OdfLengthToInches(GetAttr(props, "fo:page-height"), 0);
                info.orientation = GetAttr(props, "style:print-orientation");
                std::string margin = GetAttr(props, "fo:margin");
                double all = OdfLengthToInches(margin, -1);
                info.marginTop = OdfLengthToInches(GetAttr(props, "fo:margin-top"), all);
                info.marginBottom = OdfLengthToInches(GetAttr(props, "fo:margin-bottom"), all);
                info.marginLeft = OdfLengthToInches(GetAttr(props, "fo:margin-left"), all);
                info.marginRight = OdfLengthToInches(GetAttr(props, "fo:margin-right"), all);
                info.hasPrintTokens = props->Attribute("style:print") != nullptr;
                info.printTokens = " " + GetAttr(props, "style:print") + " ";
                info.centering = GetAttr(props, "style:table-centering");
                info.scaleTo = GetAttr(props, "style:scale-to");
                // ODF 1.3 names fitting to pages wide / tall style:scale-to-X
                // and -Y; LibreOffice wrote them as loext: before.
                for (const char* prefix : {"style:", "loext:"}) {
                    std::string x = GetAttr(props, (std::string(prefix) + "scale-to-X").c_str());
                    std::string y = GetAttr(props, (std::string(prefix) + "scale-to-Y").c_str());
                    if (!x.empty() && info.scaleToX < 0) info.scaleToX = std::atoi(x.c_str());
                    if (!y.empty() && info.scaleToY < 0) info.scaleToY = std::atoi(y.c_str());
                }
            }
            // The header's height: a fixed one (svg:height, as this writer
            // states it), else its minimum (fo:min-height, LibreOffice's).
            auto minHeight = [&](const char* tag) {
                auto* style = layout->FirstChildElement(tag);
                auto* props = style ? style->FirstChildElement("style:header-footer-properties") : nullptr;
                if (!props) return -1.0;
                double fixed = OdfLengthToInches(GetAttr(props, "svg:height"), -1);
                return fixed >= 0 ? fixed : OdfLengthToInches(GetAttr(props, "fo:min-height"), -1);
            };
            info.headerHeight = minHeight("style:header-style");
            info.footerHeight = minHeight("style:footer-style");
            pageLayouts_[GetAttr(layout, "style:name")] = info;
        }
    }

    void ParseMasterPages(tinyxml2::XMLElement* masterStyles) {
        for (auto* master = masterStyles->FirstChildElement("style:master-page"); master;
             master = master->NextSiblingElement("style:master-page")) {
            ODSMasterPage page;
            page.layout = GetAttr(master, "style:page-layout-name");
            auto readRegions = [](const tinyxml2::XMLElement* elem, std::string& left,
                                  std::string& center, std::string& right) {
                auto* l = elem->FirstChildElement("style:region-left");
                auto* c = elem->FirstChildElement("style:region-center");
                auto* r = elem->FirstChildElement("style:region-right");
                if (!l && !c && !r) {
                    center = OdfRegionAsExcelCodes(elem);   // one region, centred
                    return;
                }
                if (l) left = OdfRegionAsExcelCodes(l);
                if (c) center = OdfRegionAsExcelCodes(c);
                if (r) right = OdfRegionAsExcelCodes(r);
            };
            if (auto* header = master->FirstChildElement("style:header")) {
                if (GetAttr(header, "style:display") != "false") {
                    page.hasHeader = true;
                    readRegions(header, page.headerLeft, page.headerCenter, page.headerRight);
                }
            }
            if (auto* footer = master->FirstChildElement("style:footer")) {
                if (GetAttr(footer, "style:display") != "false") {
                    page.hasFooter = true;
                    readRegions(footer, page.footerLeft, page.footerCenter, page.footerRight);
                }
            }
            masterPages_[GetAttr(master, "style:name")] = page;
        }
    }

    // The page style a table names, into the sheet's PrintSettings. A header
    // shown on the page sits in the top margin: the page's own margin is then
    // Excel's header margin, and the header's height makes up the rest.
    void ApplyPageStyle(const std::string& tableStyle, SpreadsheetSheet* sheet) {
        std::string masterName = "Default";
        auto table = tableMasterPages_.find(tableStyle);
        if (table != tableMasterPages_.end()) masterName = table->second;
        auto master = masterPages_.find(masterName);
        if (master == masterPages_.end()) return;
        const ODSMasterPage& page = master->second;
        PrintSettings& print = sheet->GetPrintSettings();
        print.headerLeft = page.headerLeft;
        print.headerCenter = page.headerCenter;
        print.headerRight = page.headerRight;
        print.footerLeft = page.footerLeft;
        print.footerCenter = page.footerCenter;
        print.footerRight = page.footerRight;

        auto layoutIt = pageLayouts_.find(page.layout);
        if (layoutIt == pageLayouts_.end()) return;
        const ODSPageLayout& layout = layoutIt->second;
        if (layout.orientation == "landscape") print.orientation = PageOrientation::Landscape;
        else if (layout.orientation == "portrait") print.orientation = PageOrientation::Portrait;
        if (layout.pageWidth > 0 && layout.pageHeight > 0) {
            print.paperSize = PaperSizeFromDimensions(layout.pageWidth, layout.pageHeight);
        }
        if (layout.marginLeft >= 0) print.marginLeft = static_cast<float>(layout.marginLeft);
        if (layout.marginRight >= 0) print.marginRight = static_cast<float>(layout.marginRight);
        if (layout.marginTop >= 0) {
            if (page.hasHeader && layout.headerHeight >= 0) {
                print.headerMargin = static_cast<float>(layout.marginTop);
                print.marginTop = static_cast<float>(layout.marginTop + layout.headerHeight);
            } else {
                print.marginTop = static_cast<float>(layout.marginTop);
            }
        }
        if (layout.marginBottom >= 0) {
            if (page.hasFooter && layout.footerHeight >= 0) {
                print.footerMargin = static_cast<float>(layout.marginBottom);
                print.marginBottom = static_cast<float>(layout.marginBottom + layout.footerHeight);
            } else {
                print.marginBottom = static_cast<float>(layout.marginBottom);
            }
        }
        if (layout.hasPrintTokens) {
            print.printGridlines = layout.printTokens.find(" grid ") != std::string::npos;
            print.printRowColHeaders = layout.printTokens.find(" headers ") != std::string::npos;
        }
        if (!layout.centering.empty()) {
            print.centerHorizontally = layout.centering == "horizontal" || layout.centering == "both";
            print.centerVertically = layout.centering == "vertical" || layout.centering == "both";
        }
        double scale = 0;
        if (!layout.scaleTo.empty() && TryParseFloat(layout.scaleTo, scale) && scale >= 10 && scale <= 400) {
            print.scale = static_cast<int>(scale + 0.5);
        }
        if (layout.scaleToX >= 0 || layout.scaleToY >= 0) {
            print.fitToWidth = std::max(0, layout.scaleToX);
            print.fitToHeight = std::max(0, layout.scaleToY);
        }
    }

    // settings.xml: the frozen rows and columns of each sheet, kept by
    // LibreOffice in the view settings' per-table entries (split mode 2 is a
    // freeze; its position the number of columns / rows frozen).
    void ParseSettings(const std::string& xml) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        const tinyxml2::XMLElement* tables = FindNamedConfigMap(doc.RootElement(), "Tables");
        if (!tables) return;
        for (auto* entry = tables->FirstChildElement("config:config-item-map-entry"); entry;
             entry = entry->NextSiblingElement("config:config-item-map-entry")) {
            SpreadsheetSheet* sheet = spreadsheet_->GetSheetByName(GetAttr(entry, "config:name"));
            if (!sheet) continue;
            std::map<std::string, int> items;
            for (auto* item = entry->FirstChildElement("config:config-item"); item;
                 item = item->NextSiblingElement("config:config-item")) {
                items[GetAttr(item, "config:name")] = item->GetText() ? std::atoi(item->GetText()) : 0;
            }
            int cols = items["HorizontalSplitMode"] == 2 ? items["HorizontalSplitPosition"] : 0;
            int rows = items["VerticalSplitMode"] == 2 ? items["VerticalSplitPosition"] : 0;
            if (rows > 0 || cols > 0) sheet->SetFreezePanes(rows, cols);
        }
    }

    static const tinyxml2::XMLElement* FindNamedConfigMap(const tinyxml2::XMLElement* elem,
                                                          const char* name) {
        if (!elem) return nullptr;
        if (std::strcmp(elem->Name(), "config:config-item-map-named") == 0 &&
            GetAttr(elem, "config:name") == name) {
            return elem;
        }
        for (auto* child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            if (auto* found = FindNamedConfigMap(child, name)) return found;
        }
        return nullptr;
    }

    void ParseContentStyles(const std::string& xml) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;

        auto* root = doc.RootElement();
        if (!root) return;

        auto* automaticStyles = root->FirstChildElement("office:automatic-styles");
        if (automaticStyles) {
            ParseNumberStyles(automaticStyles);
            ParseStyleElements(automaticStyles);
        }
    }

    // Parse every number:*-style child of the given container into a
    // NumberFormat keyed by its style:name. ODF number styles model currency
    // symbol/position, decimal places, thousands grouping, red negatives and
    // date/time component order; we translate those into the engine's
    // NumberFormat so display matches the authoring application.
    void ParseNumberStyles(tinyxml2::XMLElement* parent) {
        static const char* kTags[] = {
            "number:number-style", "number:currency-style",
            "number:percentage-style", "number:date-style",
            "number:time-style"
        };
        for (const char* tag : kTags) {
            for (auto* elem = parent->FirstChildElement(tag);
                 elem; elem = elem->NextSiblingElement(tag)) {
                std::string name = GetAttr(elem, "style:name");
                if (name.empty()) continue;
                numberFormats_[name] = ParseNumberFormatStyle(elem, tag);
            }
        }
    }

    NumberFormat ParseNumberFormatStyle(tinyxml2::XMLElement* styleElem,
                                        const std::string& tag) {
        NumberFormat fmt;
        if (tag == "number:currency-style")        fmt.category = NumberFormatCategory::Currency;
        else if (tag == "number:percentage-style") fmt.category = NumberFormatCategory::Percentage;
        else if (tag == "number:date-style")       fmt.category = NumberFormatCategory::Date;
        else if (tag == "number:time-style")       fmt.category = NumberFormatCategory::Time;
        else                                       fmt.category = NumberFormatCategory::Number;

        bool negativeRed = false;
        std::string dateCode;              // built for date/time styles
        int numberIndex = -1, symbolIndex = -1, childIndex = 0;
        std::vector<int> spaceTextIndices; // number:text children that are blank
        bool sawNumber = false;

        for (auto* child = styleElem->FirstChildElement();
             child; child = child->NextSiblingElement(), ++childIndex) {
            std::string name = child->Name();
            if (name == "style:text-properties") {
                std::string color = GetAttr(child, "fo:color");
                if (!color.empty()) {
                    Color c = ParseHexColor(color);
                    if (c.r > 180 && c.g < 80 && c.b < 80) negativeRed = true;
                }
            } else if (name == "number:number") {
                numberIndex = childIndex;
                sawNumber = true;
                fmt.decimalPlaces = GetAttrInt(child, "number:decimal-places", 0);
                if (GetAttr(child, "number:grouping") == "true")
                    fmt.useThousandsSeparator = true;
            } else if (name == "number:currency-symbol") {
                symbolIndex = childIndex;
                const char* t = child->GetText();
                if (t) fmt.currencySymbol = t;
            } else if (name == "number:day") {
                dateCode += (GetAttr(child, "number:style") == "long") ? "DD" : "D";
            } else if (name == "number:month") {
                dateCode += (GetAttr(child, "number:style") == "long") ? "MM" : "M";
            } else if (name == "number:year") {
                dateCode += (GetAttr(child, "number:style") == "long") ? "YYYY" : "YY";
            } else if (name == "number:hours") {
                dateCode += (GetAttr(child, "number:style") == "long") ? "HH" : "H";
            } else if (name == "number:minutes") {
                dateCode += "NN";
            } else if (name == "number:seconds") {
                dateCode += "SS";
            } else if (name == "number:text") {
                // tinyxml2 drops pure-whitespace text nodes (GetText() == null);
                // such an empty number:text almost always stood for a single
                // space separator, so treat it as one.
                const char* t = child->GetText();
                std::string txt = (t && *t) ? t : " ";
                dateCode += txt;   // literal separators (used only for date/time)
                bool blank = txt.find_first_not_of(" \t\xC2\xA0") == std::string::npos;
                if (blank) spaceTextIndices.push_back(childIndex);
            }
        }

        fmt.negativeRed = negativeRed;

        if (fmt.category == NumberFormatCategory::Currency) {
            fmt.currencySymbolAfter = (symbolIndex > numberIndex);
            // FormatNumber auto-inserts a space when the symbol trails the
            // amount ("1,234.00 €") but not when it leads. If the document put
            // a blank between a leading symbol and the amount ("€ 1,234.00"),
            // fold that space into the symbol so it is preserved.
            if (!fmt.currencySymbolAfter && symbolIndex >= 0 && numberIndex >= 0) {
                for (int idx : spaceTextIndices) {
                    if (idx > symbolIndex && idx < numberIndex) {
                        fmt.currencySymbol += " ";
                        break;
                    }
                }
            }
            (void)sawNumber;
        } else if (fmt.category == NumberFormatCategory::Date ||
                   fmt.category == NumberFormatCategory::Time) {
            bool hasDate = dateCode.find('Y') != std::string::npos ||
                           dateCode.find('D') != std::string::npos ||
                           dateCode.find('M') != std::string::npos;
            bool hasTime = dateCode.find('H') != std::string::npos ||
                           dateCode.find("NN") != std::string::npos ||
                           dateCode.find("SS") != std::string::npos;
            if (hasDate && hasTime)      fmt.category = NumberFormatCategory::DateTime;
            else if (hasTime && !hasDate) fmt.category = NumberFormatCategory::Time;
            else                          fmt.category = NumberFormatCategory::Date;
            if (!dateCode.empty()) fmt.formatCode = dateCode;
        }

        return fmt;
    }
    
    void ParseStyleElements(tinyxml2::XMLElement* stylesElem) {
        for (auto* style = stylesElem->FirstChildElement("style:style"); 
             style; 
             style = style->NextSiblingElement("style:style")) {
            
            std::string styleName = GetAttr(style, "style:name");
            std::string family = GetAttr(style, "style:family");
            
            if (family == "table-column" && !styleName.empty()) {
                ODSColumnStyle colStyle;
                if (auto* props = style->FirstChildElement("style:table-column-properties")) {
                    // style:column-width is the authored width; fo:min-width is
                    // the fallback some producers write instead.
                    std::string width = GetAttr(props, "style:column-width");
                    if (width.empty()) width = GetAttr(props, "fo:min-width");
                    if (!width.empty()) {
                        colStyle.widthPixels = SpreadsheetLengthToPixels(width, 0);
                    }
                    colStyle.useOptimalWidth =
                        (GetAttr(props, "style:use-optimal-column-width") == "true");
                    colStyle.pageBreakBefore = GetAttr(props, "fo:break-before") == "page";
                }
                columnStyles_[styleName] = colStyle;
                continue;
            }

            if (family == "table" && !styleName.empty()) {
                std::string master = GetAttr(style, "style:master-page-name");
                if (!master.empty()) tableMasterPages_[styleName] = master;
                continue;
            }

            if (family == "table-row" && !styleName.empty()) {
                ODSRowStyle rowStyle;
                if (auto* props = style->FirstChildElement("style:table-row-properties")) {
                    std::string height = GetAttr(props, "style:row-height");
                    if (height.empty()) height = GetAttr(props, "fo:min-height");
                    if (!height.empty()) {
                        rowStyle.heightPixels = SpreadsheetLengthToPixels(height, 0);
                    }
                    rowStyle.useOptimalHeight =
                        (GetAttr(props, "style:use-optimal-row-height") == "true");
                    rowStyle.pageBreakBefore = GetAttr(props, "fo:break-before") == "page";
                }
                rowStyles_[styleName] = rowStyle;
                continue;
            }

            if (family == "table-cell" && !styleName.empty()) {
                CellStyle cellStyle;
                
                // Parse text properties
                auto* textProps = style->FirstChildElement("style:text-properties");
                if (textProps) {
                    cellStyle.font.family = GetAttr(textProps, "style:font-name", "Arial");
                    std::string fontSize = GetAttr(textProps, "fo:font-size");
                    if (!fontSize.empty()) {
                        TryParseFloat(fontSize, cellStyle.font.size);
                    }
                    cellStyle.font.bold = (GetAttr(textProps, "fo:font-weight") == "bold");
                    cellStyle.font.italic = (GetAttr(textProps, "fo:font-style") == "italic");
                    std::string color = GetAttr(textProps, "fo:color");
                    if (!color.empty()) {
                        cellStyle.font.color = ParseHexColor(color);
                    }
                    std::string underline = GetAttr(textProps, "style:text-underline-style");
                    if (!underline.empty() && underline != "none") {
                        cellStyle.font.underline = (underline == "double")
                            ? UnderlineStyle::Double : UnderlineStyle::Single;
                    }
                    std::string strike = GetAttr(textProps, "style:text-line-through-style");
                    cellStyle.font.strikethrough = !strike.empty() && strike != "none";
                }
                
                // Parse table-cell properties
                auto* cellProps = style->FirstChildElement("style:table-cell-properties");
                if (cellProps) {
                    std::string bgColor = GetAttr(cellProps, "fo:background-color");
                    if (!bgColor.empty() && bgColor != "transparent") {
                        cellStyle.fill.pattern = FillPattern::Solid;
                        cellStyle.fill.foregroundColor = ParseHexColor(bgColor);
                    }
                    
                    // Borders: uniform "fo:border" or per-side attributes.
                    // Attribute value looks like "0.5pt solid #000000".
                    auto parseBorderAttr = [&](const char* attr, CellBorder& side) {
                        std::string value = GetAttr(cellProps, attr);
                        if (value.empty() || value == "none") return;
                        side.style = BorderStyle::Thin;
                        size_t hash = value.find('#');
                        side.color = (hash != std::string::npos)
                            ? ParseHexColor(value.substr(hash)) : Colors::Black;
                        if (value.find("double") != std::string::npos) {
                            side.style = BorderStyle::Double;
                        } else if (value.find("dashed") != std::string::npos) {
                            side.style = BorderStyle::Dashed;
                        } else if (value.find("dotted") != std::string::npos) {
                            side.style = BorderStyle::Dotted;
                        } else if (value.rfind("2pt", 0) == 0) {
                            side.style = BorderStyle::Thick;
                        } else if (value.rfind("1pt", 0) == 0) {
                            side.style = BorderStyle::Medium;
                        }
                    };
                    std::string border = GetAttr(cellProps, "fo:border");
                    if (!border.empty() && border != "none") {
                        CellBorder b;
                        parseBorderAttr("fo:border", b);
                        cellStyle.borders.left = cellStyle.borders.right =
                        cellStyle.borders.top = cellStyle.borders.bottom = b;
                    } else {
                        parseBorderAttr("fo:border-left", cellStyle.borders.left);
                        parseBorderAttr("fo:border-right", cellStyle.borders.right);
                        parseBorderAttr("fo:border-top", cellStyle.borders.top);
                        parseBorderAttr("fo:border-bottom", cellStyle.borders.bottom);
                    }
                    
                    // Vertical alignment
                    std::string vAlign = GetAttr(cellProps, "style:vertical-align");
                    if (vAlign == "top") cellStyle.vAlign = VerticalAlignment::Top;
                    else if (vAlign == "middle") cellStyle.vAlign = VerticalAlignment::Middle;
                    else if (vAlign == "bottom") cellStyle.vAlign = VerticalAlignment::Bottom;
                    
                    // Wrap
                    cellStyle.wrapText = (GetAttr(cellProps, "fo:wrap-option") == "wrap");
                }
                
                // Parse paragraph properties
                auto* paraProps = style->FirstChildElement("style:paragraph-properties");
                if (paraProps) {
                    std::string hAlign = GetAttr(paraProps, "fo:text-align");
                    if (hAlign == "start" || hAlign == "left") 
                        cellStyle.hAlign = HorizontalAlignment::Left;
                    else if (hAlign == "center") 
                        cellStyle.hAlign = HorizontalAlignment::Center;
                    else if (hAlign == "end" || hAlign == "right") 
                        cellStyle.hAlign = HorizontalAlignment::Right;
                }
                
                // Link the number/data format referenced by this cell style so
                // currency/percentage/date cells display as authored.
                std::string dataStyle = GetAttr(style, "style:data-style-name");
                if (!dataStyle.empty()) {
                    auto nf = numberFormats_.find(dataStyle);
                    if (nf != numberFormats_.end()) {
                        cellStyle.numberFormat = nf->second;
                    }
                }

                styles_[styleName] = cellStyle;
                // A common style is named by its display name where one
                // names it ("Good" for "Good_5f_1"): a condition's
                // calcext:apply-style-name does.
                const std::string displayName = GetAttr(style, "style:display-name");
                if (!displayName.empty()) styleDisplayNames_[displayName] = styleName;
            }
        }
    }

    bool ParseContent(const std::string& xml) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) {
            return false;
        }

        auto* root = doc.RootElement();
        if (!root) return false;

        // Find office:body / office:spreadsheet
        auto* body = root->FirstChildElement("office:body");
        if (!body) return false;

        auto* spreadsheetElem = body->FirstChildElement("office:spreadsheet");
        if (!spreadsheetElem) return false;

        // Clear existing sheets, and the names of the document they were
        while (spreadsheet_->GetSheetCount() > 0) {
            spreadsheet_->RemoveSheet(0);
        }
        for (const NamedRange& named : spreadsheet_->GetNamedRangeList()) {
            spreadsheet_->RemoveNamedRange(named.name, named.sheetScope);
        }

        // The validations the cells name (table:content-validation-name).
        if (auto* validations = spreadsheetElem->FirstChildElement("table:content-validations")) {
            LoadContentValidations(validations);
        }

        // Parse each table:table (sheet)
        int sheetIndex = 0;
        std::vector<std::pair<tinyxml2::XMLElement*, std::string>> sheetNames;
        for (auto* tableElem = spreadsheetElem->FirstChildElement("table:table");
             tableElem;
             tableElem = tableElem->NextSiblingElement("table:table")) {

            std::string sheetName = GetAttr(tableElem, "table:name", "Sheet" + std::to_string(sheetIndex + 1));

            SpreadsheetSheet* sheet = spreadsheet_->AddSheet(sheetName);
            if (sheet) {
                ParseTable(tableElem, sheet);
                ApplyCellExtras(sheet);
                ParseConditionalFormats(tableElem, sheet);
                ApplyPageStyle(GetAttr(tableElem, "table:style-name"), sheet);
                // The print area: the first of the table's print ranges.
                std::vector<std::string> printRanges =
                    SplitOdfRangeList(GetAttr(tableElem, "table:print-ranges"));
                std::string rangeSheet;
                CellRange printArea;
                if (!printRanges.empty() &&
                    ParseOdfRangeAddress(printRanges.front(), rangeSheet, printArea)) {
                    printArea.start.sheetName.clear();
                    printArea.end.sheetName.clear();
                    sheet->SetPrintArea(printArea);
                }
                // Names scoped to this sheet, read once every sheet is there
                // (one may cover a sheet further on).
                if (auto* names = tableElem->FirstChildElement("table:named-expressions")) {
                    sheetNames.emplace_back(names, sheet->GetName());
                }
            }

            ++sheetIndex;
        }

        // Ensure at least one sheet exists
        if (spreadsheet_->GetSheetCount() == 0) {
            spreadsheet_->AddSheet("Sheet1");
        }

        // The workbook's names, and the database ranges that carry the
        // sheets' auto-filters.
        if (auto* names = spreadsheetElem->FirstChildElement("table:named-expressions")) {
            LoadNamedRanges(names, "");
        }
        for (const auto& [names, scope] : sheetNames) {
            LoadNamedRanges(names, scope);
        }
        if (auto* databaseRanges = spreadsheetElem->FirstChildElement("table:database-ranges")) {
            LoadAutoFilters(databaseRanges);
        }

        spreadsheet_->SetActiveSheet(0);
        return true;
    }

    // table:named-range entries. LibreOffice also keeps a sheet's print area
    // and print titles as "_xlnm." names for Excel's sake; the table's own
    // print ranges and header rows say the same, so those are passed over, as
    // is a named expression (a formula, not a range).
    void LoadNamedRanges(tinyxml2::XMLElement* names, const std::string& scope) {
        for (auto* named = names->FirstChildElement("table:named-range"); named;
             named = named->NextSiblingElement("table:named-range")) {
            std::string name = GetAttr(named, "table:name");
            if (name.empty() || name.rfind("_xlnm.", 0) == 0) continue;
            std::string sheet;
            CellRange range;
            if (!ParseOdfRangeAddress(GetAttr(named, "table:cell-range-address"), sheet, range)) {
                continue;
            }
            // A range into another document ('file:///...'#$Sheet1.A1) has no sheet here.
            if (!sheet.empty() && !spreadsheet_->GetSheetByName(sheet)) continue;
            spreadsheet_->AddNamedRange(name, range, scope);
        }
    }

    // A database range showing filter buttons is a sheet's auto-filter; its
    // "=" conditions (one value, or a set of them) are the values each column
    // is filtered to.
    void LoadAutoFilters(tinyxml2::XMLElement* databaseRanges) {
        for (auto* db = databaseRanges->FirstChildElement("table:database-range"); db;
             db = db->NextSiblingElement("table:database-range")) {
            if (GetAttr(db, "table:display-filter-buttons") != "true") continue;
            std::string sheetName;
            CellRange range;
            if (!ParseOdfRangeAddress(GetAttr(db, "table:target-range-address"), sheetName, range)) {
                continue;
            }
            SpreadsheetSheet* sheet = spreadsheet_->GetSheetByName(sheetName);
            if (!sheet) continue;
            range.start.sheetName.clear();
            range.end.sheetName.clear();
            sheet->SetAutoFilter(range);
            std::map<int, std::vector<std::string>> values;   // field -> values
            if (auto* filter = db->FirstChildElement("table:filter")) {
                CollectFilterValues(filter, values);
            }
            for (const auto& [field, list] : values) {
                ColumnFilter columnFilter;
                columnFilter.op = FilterOperator::Equals;
                columnFilter.values = list;
                sheet->ApplyFilter(range.start.col + field, columnFilter);
            }
        }
    }

    static void CollectFilterValues(const tinyxml2::XMLElement* elem,
                                    std::map<int, std::vector<std::string>>& values) {
        for (auto* child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            std::string name = child->Name();
            if (name == "table:filter-and" || name == "table:filter-or") {
                CollectFilterValues(child, values);
                continue;
            }
            if (name != "table:filter-condition" || GetAttr(child, "table:operator") != "=") continue;
            std::vector<std::string>& list = values[GetAttrInt(child, "table:field-number", 0)];
            bool anyItem = false;
            for (auto* item = child->FirstChildElement("table:filter-set-item"); item;
                 item = item->NextSiblingElement("table:filter-set-item")) {
                list.push_back(GetAttr(item, "table:value"));
                anyItem = true;
            }
            if (!anyItem) list.push_back(GetAttr(child, "table:value"));
        }
    }

    // A hidden run longer than this many rows ("hide everything below") is
    // not hidden row by row: each hidden row costs a row definition.
    static constexpr int kHiddenRowRunLimit = 65536;

    void ParseTable(tinyxml2::XMLElement* tableElem, SpreadsheetSheet* sheet) {
        // Columns and rows may sit inside header (print-title), group and
        // plain wrapper elements, which a sheet's layout does not otherwise
        // depend on: walk through them.
        int colIndex = 0;
        ParseColumns(tableElem, sheet, colIndex, false);
        int rowIndex = 0;
        matrixAnchors_.clear();
        ParseRows(tableElem, sheet, rowIndex, false);
        // The values an array formula covers arrive as plain cells: they are
        // its values, not cells of their own.
        for (const CellAddress& at : matrixAnchors_) {
            const SpreadsheetCell* anchor = sheet->GetCellIfExists(at.row, at.col);
            if (!anchor) continue;
            for (int r = 0; r < anchor->GetArrayRows(); ++r) {
                for (int c = 0; c < anchor->GetArrayCols(); ++c) {
                    if (r == 0 && c == 0) continue;
                    if (SpreadsheetCell* cell = sheet->GetCellIfExists(at.row + r, at.col + c)) {
                        if (!cell->HasFormula()) cell->MarkSpilledFrom(r, c);
                    }
                }
            }
        }
    }

    // The array formulas (table:number-matrix-*-spanned) of the sheet being read.
    std::vector<CellAddress> matrixAnchors_;

    void ParseColumns(tinyxml2::XMLElement* parent, SpreadsheetSheet* sheet, int& colIndex,
                      bool headerColumns) {
        for (auto* elem = parent->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string name = elem->Name();
            if (name == "table:table-column") {
                ParseColumn(elem, sheet, colIndex, headerColumns);
            } else if (name == "table:table-header-columns") {
                ParseColumns(elem, sheet, colIndex, true);
            } else if (name == "table:table-columns" || name == "table:table-column-group") {
                ParseColumns(elem, sheet, colIndex, headerColumns);
            }
            if (colIndex >= SpreadsheetLimits::MaxColumns) return;
        }
    }

    // One table-column element. The width lives in the table-column style
    // the element references; a handful of producers (and our own older
    // writer) put style:column-width straight on the element, so accept that
    // too.
    void ParseColumn(tinyxml2::XMLElement* colElem, SpreadsheetSheet* sheet, int& colIndex,
                     bool headerColumn) {
        int repeat = std::max(1, GetAttrInt(colElem, "table:number-columns-repeated", 1));
        const int first = colIndex;
        const int last = std::min(colIndex + repeat, SpreadsheetLimits::MaxColumns) - 1;

        int width = 0;              // 0 = this column carries no authored width
        bool optimalWidth = false;
        bool pageBreak = false;
        auto styleIt = columnStyles_.find(GetAttr(colElem, "table:style-name"));
        if (styleIt != columnStyles_.end()) {
            width = styleIt->second.widthPixels;
            optimalWidth = styleIt->second.useOptimalWidth;
            pageBreak = styleIt->second.pageBreakBefore;
        }
        std::string inlineWidth = GetAttr(colElem, "style:column-width");
        if (!inlineWidth.empty()) {
            width = SpreadsheetLengthToPixels(inlineWidth, width);
        }
        std::string visibility = GetAttr(colElem, "table:visibility");
        const bool hidden = visibility == "collapse" || visibility == "filter";
        PrintSettings& print = sheet->GetPrintSettings();
        if (pageBreak && first > 0) print.colBreaks.push_back(first);
        if (headerColumn && last >= first) {
            if (!print.HasRepeatColumns()) print.repeatColumnsFirst = first;
            print.repeatColumnsLast = last;
        }
        if (hidden && last >= first) sheet->SetColumnsHidden(first, last, true);

        // ODF pads a table out to its full column space with one heavily
        // repeated element ("number-columns-repeated=16384"). That states a
        // sheet-wide default, not 16k individually sized columns - storing
        // it as the default costs one integer instead of megabytes of
        // column definitions, and leaves those columns free to auto-fit.
        if (repeat > kWideColumnRun) {
            if (width > 0 && !optimalWidth && !hidden) sheet->SetDefaultColumnWidth(width);
            colIndex += repeat;
            return;
        }

        for (int c = first; c <= last; ++c) {
            // "Optimal width" means the author asked for a fitted column, so
            // leave it unsized and let the auto-width pass do the fitting.
            if (width > 0 && !optimalWidth) {
                sheet->SetColumnWidth(c, width, /*explicitWidth*/ true);
            }
        }
        colIndex += repeat;
    }

    void ParseRows(tinyxml2::XMLElement* parent, SpreadsheetSheet* sheet, int& rowIndex,
                   bool headerRows) {
        for (auto* elem = parent->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string name = elem->Name();
            if (name == "table:table-row") {
                ParseRowElement(elem, sheet, rowIndex, headerRows);
            } else if (name == "table:table-header-rows") {
                ParseRows(elem, sheet, rowIndex, true);
            } else if (name == "table:table-rows" || name == "table:table-row-group") {
                ParseRows(elem, sheet, rowIndex, headerRows);
            }
            if (rowIndex >= SpreadsheetLimits::MaxRows) return;
        }
    }

    void ParseRowElement(tinyxml2::XMLElement* rowElem, SpreadsheetSheet* sheet, int& rowIndex,
                         bool headerRow) {
        int rowRepeat = std::max(1, GetAttrInt(rowElem, "table:number-rows-repeated", 1));
        const int first = rowIndex;
        const int last = std::min(rowIndex + rowRepeat, SpreadsheetLimits::MaxRows) - 1;

        std::string visibility = GetAttr(rowElem, "table:visibility");
        const bool hidden = visibility == "collapse" || visibility == "filter";
        PrintSettings& print = sheet->GetPrintSettings();
        auto rowStyleIt = rowStyles_.find(GetAttr(rowElem, "table:style-name"));
        if (rowStyleIt != rowStyles_.end() && rowStyleIt->second.pageBreakBefore && first > 0) {
            print.rowBreaks.push_back(first);
        }
        if (headerRow && last >= first) {
            if (!print.HasRepeatRows()) print.repeatRowsFirst = first;
            print.repeatRowsLast = last;
        }
        if (hidden && last >= first && last - first < kHiddenRowRunLimit) {
            sheet->SetRowsHidden(first, last, true);
        }

        // Skip large empty row blocks
        if (rowRepeat > 100) {
            // Check if row has any cells
            auto* firstCell = rowElem->FirstChildElement("table:table-cell");
            if (!firstCell || GetAttr(firstCell, "office:value-type").empty()) {
                RecordRowValidationRuns(rowElem, first, last);
                rowIndex += rowRepeat;
                return;
            }
        }

        for (int r = 0; r < rowRepeat && rowIndex < SpreadsheetLimits::MaxRows; ++r) {
            ParseRow(rowElem, sheet, rowIndex);
            ++rowIndex;
        }
    }

    void ParseRow(tinyxml2::XMLElement* rowElem, SpreadsheetSheet* sheet, int rowIndex) {
        // Parse row height - like the column width, it lives in the referenced
        // table-row style, with the inline attribute accepted as a fallback.
        int height = 0;
        bool optimalHeight = false;
        auto rowStyleIt = rowStyles_.find(GetAttr(rowElem, "table:style-name"));
        if (rowStyleIt != rowStyles_.end()) {
            height = rowStyleIt->second.heightPixels;
            optimalHeight = rowStyleIt->second.useOptimalHeight;
        }
        std::string inlineHeight = GetAttr(rowElem, "style:row-height");
        if (!inlineHeight.empty()) {
            height = SpreadsheetLengthToPixels(inlineHeight, height);
        }
        if (height > 0 && !optimalHeight) {
            sheet->SetRowHeight(rowIndex, height, /*explicitHeight*/ true);
        }

        // Parse cells. A cell a merge covers is a table:covered-table-cell; it
        // takes its column like any other cell.
        int colIndex = 0;
        for (auto* cellElem = rowElem->FirstChildElement();
             cellElem;
             cellElem = cellElem->NextSiblingElement()) {
            const std::string elemName = cellElem->Name();
            const bool covered = elemName == "table:covered-table-cell";
            if (!covered && elemName != "table:table-cell") continue;

            int colRepeat = std::max(1, GetAttrInt(cellElem, "table:number-columns-repeated", 1));
            if (covered) {
                colIndex += colRepeat;
                continue;
            }
            RecordCellExtras(cellElem, rowIndex, colIndex, colRepeat);
            int colSpan = GetAttrInt(cellElem, "table:number-columns-spanned", 1);
            int rowSpan = GetAttrInt(cellElem, "table:number-rows-spanned", 1);

            // Skip large empty cell blocks
            std::string valueType = GetAttr(cellElem, "office:value-type");
            std::string formula = GetAttr(cellElem, "table:formula");

            bool hasContent = !valueType.empty() || !formula.empty();
            if (!hasContent && colRepeat > 100) {
                colIndex += colRepeat;
                continue;
            }

            for (int c = 0; c < colRepeat && colIndex < SpreadsheetLimits::MaxColumns; ++c) {
                if (hasContent) {
                    ParseCell(cellElem, sheet, rowIndex, colIndex);
                }
                // A merge whose first cell is empty is still a merge.
                if (colSpan > 1 || rowSpan > 1) {
                    sheet->MergeCells(rowIndex, colIndex,
                                      rowIndex + std::max(1, rowSpan) - 1,
                                      colIndex + std::max(1, colSpan) - 1);
                }
                ++colIndex;
            }
        }
    }

    void ParseCell(tinyxml2::XMLElement* cellElem, SpreadsheetSheet* sheet, int row, int col) {
        SpreadsheetCell* cell = sheet->GetCell(row, col);
        if (!cell) return;
        
        std::string valueType = GetAttr(cellElem, "office:value-type");
        std::string formula = GetAttr(cellElem, "table:formula");
        
        // Parse formula
        bool hasFormula = !formula.empty();
        if (hasFormula) {
            // Convert ODF reference syntax ("of:=", bracketed [.A1] / range
            // [.A1:.B2] / cross-sheet ['Sheet'.A1]) into the engine's native
            // A1 / A1:B2 / 'Sheet'.A1 form.
            formula = OdsFormulaToNative(formula);
            cell->SetFormula(formula);
            // An array formula fills the block it spans (ODF has no dynamic
            // spill); any other formula is worth one value, a range in it
            // giving the cell on its row or column, as LibreOffice reads it.
            const int matrixCols = GetAttrInt(cellElem, "table:number-matrix-columns-spanned", 0);
            const int matrixRows = GetAttrInt(cellElem, "table:number-matrix-rows-spanned", 0);
            if (matrixCols > 0 || matrixRows > 0) {
                cell->SetFormulaArrayMode(FormulaArrayMode::FixedRange, std::max(1, matrixRows),
                                          std::max(1, matrixCols));
                matrixAnchors_.push_back(CellAddress(row, col));
            } else {
                cell->SetFormulaArrayMode(FormulaArrayMode::SingleValue);
            }
        }

        // Parse value based on type. Formula cells store the cached value via
        // SetFormulaResult — the plain setters would wipe the formula text.
        if (hasFormula) {
            if (valueType == "float" || valueType == "currency"
                || valueType == "percentage") {
                cell->SetFormulaResult(GetAttrDouble(cellElem, "office:value"),
                                       CellValueType::Number);
            } else if (valueType == "boolean") {
                cell->SetFormulaResult(
                    GetAttr(cellElem, "office:boolean-value") == "true",
                    CellValueType::Boolean);
            } else {
                cell->SetFormulaResult(GetCellText(cellElem), CellValueType::Text);
            }
        }
        else if (valueType == "float" || valueType == "currency" || valueType == "percentage") {
            double value = GetAttrDouble(cellElem, "office:value");
            if (valueType == "percentage") {
                cell->SetPercentage(value);
            } else if (valueType == "currency") {
                std::string currency = GetAttr(cellElem, "office:currency", "USD");
                cell->SetCurrency(value, currency);
            } else {
                cell->SetNumber(value);
            }
        }
        else if (valueType == "date") {
            std::string dateStr = GetAttr(cellElem, "office:date-value");
            int y = 0, m = 0, d = 0;
            if (sscanf(dateStr.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
                cell->SetDate(y, m, d);
            }
        }
        else if (valueType == "time") {
            std::string timeStr = GetAttr(cellElem, "office:time-value");
            int totalSec = static_cast<int>(ParseODSTime(timeStr) * 86400.0 + 0.5);
            cell->SetTime((totalSec / 3600) % 24, (totalSec / 60) % 60, totalSec % 60);
        }
        else if (valueType == "boolean") {
            bool value = (GetAttr(cellElem, "office:boolean-value") == "true");
            cell->SetBoolean(value);
        }
        else if (valueType == "string" || !valueType.empty()) {
            // Gather text from all paragraphs, including nested runs / links.
            cell->SetText(GetCellText(cellElem));
        }
        else if (formula.empty()) {
            // Check for text content without value-type.
            std::string text = GetCellText(cellElem);
            if (!text.empty()) {
                cell->SetValueFromString(text);
            }
        }

        // Preserve a hyperlink target carried by a text:a run.
        std::string href = FindCellHyperlink(cellElem);
        if (!href.empty()) {
            if (href[0] == '#') {
                cell->SetHyperlink(href.substr(1), HyperlinkType::InternalCell);
            } else {
                cell->SetHyperlink(href, href.rfind("mailto:", 0) == 0 ? HyperlinkType::Email
                                       : href.rfind("file:", 0) == 0 ? HyperlinkType::File
                                                                     : HyperlinkType::URL);
            }
        }

        // Apply style
        std::string styleName = GetAttr(cellElem, "table:style-name");
        if (!styleName.empty()) {
            auto it = styles_.find(styleName);
            if (it != styles_.end()) {
                cell->SetStyle(it->second);
            }
        }
    }
    // ===== VALIDATIONS, COMMENTS, CONDITIONAL FORMATS =====

    // table:content-validation by name; the cells name theirs.
    std::map<std::string, DataValidation> contentValidations_;
    // A style's display name -> its style:name.
    std::map<std::string, std::string> styleDisplayNames_;
    // The cells of the table being read that name a validation, as
    // rectangles (a run of repeated cells or rows is one), and its notes.
    struct ValidationRun { int r0, r1, c0, c1; std::string name; };
    std::vector<ValidationRun> validationRuns_;
    struct PendingComment { int row, col; CellComment comment; };
    std::vector<PendingComment> pendingComments_;

    static std::string ParagraphsText(const tinyxml2::XMLElement* parent) {
        std::string text;
        bool first = true;
        for (auto* p = parent->FirstChildElement("text:p"); p; p = p->NextSiblingElement("text:p")) {
            if (!first) text += '\n';
            first = false;
            AppendElementText(p, text);
        }
        return text;
    }

    void LoadContentValidations(tinyxml2::XMLElement* validations) {
        for (auto* v = validations->FirstChildElement("table:content-validation"); v;
             v = v->NextSiblingElement("table:content-validation")) {
            const std::string name = GetAttr(v, "table:name");
            DataValidation rule;
            if (name.empty() || !ParseOdfValidationCondition(GetAttr(v, "table:condition"), rule)) continue;
            rule.allowBlank = GetAttr(v, "table:allow-empty-cell", "true") != "false";
            rule.showDropdown = GetAttr(v, "table:display-list", "unsorted") != "none";
            if (auto* help = v->FirstChildElement("table:help-message")) {
                rule.showInputMessage = GetAttr(help, "table:display", "false") == "true";
                rule.inputTitle = GetAttr(help, "table:title");
                rule.inputMessage = ParagraphsText(help);
            }
            if (auto* error = v->FirstChildElement("table:error-message")) {
                rule.showErrorAlert = GetAttr(error, "table:display", "false") == "true";
                rule.errorTitle = GetAttr(error, "table:title");
                rule.errorMessage = ParagraphsText(error);
                const std::string type = GetAttr(error, "table:message-type", "stop");
                rule.alertStyle = type == "warning" ? ValidationAlertStyle::Warning
                                : type == "information" ? ValidationAlertStyle::Information
                                                        : ValidationAlertStyle::Stop;
            } else {
                rule.showErrorAlert = false;
            }
            contentValidations_[name] = rule;
        }
    }

    // A cell's validation name and note, `repeat` cells from (row, col).
    void RecordCellExtras(tinyxml2::XMLElement* cellElem, int row, int col, int repeat) {
        const std::string validation = GetAttr(cellElem, "table:content-validation-name");
        if (!validation.empty() && col < SpreadsheetLimits::MaxColumns) {
            validationRuns_.push_back({row, row, col,
                                       std::min(col + std::max(1, repeat), SpreadsheetLimits::MaxColumns) - 1,
                                       validation});
        }
        if (auto* note = cellElem->FirstChildElement("office:annotation")) {
            CellComment comment(ParagraphsText(note), "");
            if (auto* creator = note->FirstChildElement("dc:creator")) {
                comment.author = creator->GetText() ? creator->GetText() : "";
            }
            comment.visible = GetAttr(note, "office:display") == "true";
            pendingComments_.push_back({row, col, comment});
        }
    }

    // The validation names of a block of rows passed over as empty.
    void RecordRowValidationRuns(tinyxml2::XMLElement* rowElem, int firstRow, int lastRow) {
        int col = 0;
        for (auto* cellElem = rowElem->FirstChildElement(); cellElem; cellElem = cellElem->NextSiblingElement()) {
            const std::string elemName = cellElem->Name();
            if (elemName != "table:table-cell" && elemName != "table:covered-table-cell") continue;
            const int repeat = std::max(1, GetAttrInt(cellElem, "table:number-columns-repeated", 1));
            const std::string validation = GetAttr(cellElem, "table:content-validation-name");
            if (!validation.empty() && col < SpreadsheetLimits::MaxColumns) {
                validationRuns_.push_back({firstRow, lastRow, col,
                                           std::min(col + repeat, SpreadsheetLimits::MaxColumns) - 1, validation});
            }
            col += repeat;
        }
    }

    // The table's validations as ranges (runs of one rule joined down the
    // rows), and its notes, onto the sheet.
    void ApplyCellExtras(SpreadsheetSheet* sheet) {
        std::sort(validationRuns_.begin(), validationRuns_.end(), [](const ValidationRun& a, const ValidationRun& b) {
            return std::tie(a.name, a.c0, a.c1, a.r0) < std::tie(b.name, b.c0, b.c1, b.r0);
        });
        std::vector<ValidationRun> merged;
        for (const ValidationRun& run : validationRuns_) {
            if (!merged.empty()) {
                ValidationRun& last = merged.back();
                if (last.name == run.name && last.c0 == run.c0 && last.c1 == run.c1 && last.r1 + 1 >= run.r0) {
                    last.r1 = std::max(last.r1, run.r1);
                    continue;
                }
            }
            merged.push_back(run);
        }
        for (const ValidationRun& run : merged) {
            auto rule = contentValidations_.find(run.name);
            if (rule != contentValidations_.end()) {
                sheet->SetValidation(CellRange(run.r0, run.c0, run.r1, run.c1), rule->second);
            }
        }
        for (const PendingComment& pending : pendingComments_) {
            sheet->GetCell(pending.row, pending.col)->SetComment(pending.comment);
        }
        validationRuns_.clear();
        pendingComments_.clear();
    }

    // calcext:conditional-formats: conditions (with their cell styles),
    // colour scales and data bars.
    void ParseConditionalFormats(tinyxml2::XMLElement* tableElem, SpreadsheetSheet* sheet) {
        auto* formats = tableElem->FirstChildElement("calcext:conditional-formats");
        if (!formats) return;
        for (auto* format = formats->FirstChildElement("calcext:conditional-format"); format;
             format = format->NextSiblingElement("calcext:conditional-format")) {
            std::vector<CellRange> ranges;
            for (const std::string& part : SplitOdfRangeList(GetAttr(format, "calcext:target-range-address"))) {
                std::string rangeSheet;
                CellRange range;
                if (!ParseOdfRangeAddress(part, rangeSheet, range)) continue;
                range.start.sheetName.clear();
                range.end.sheetName.clear();
                ranges.push_back(range);
            }
            if (ranges.empty()) continue;
            std::vector<ConditionalFormatRule> rules;
            for (auto* entry = format->FirstChildElement(); entry; entry = entry->NextSiblingElement()) {
                const std::string name = entry->Name();
                ConditionalFormatRule rule;
                if (name == "calcext:condition") {
                    if (!ParseOdfConditionValue(GetAttr(entry, "calcext:value"), rule)) continue;
                    const std::string styleName = GetAttr(entry, "calcext:apply-style-name");
                    auto style = styles_.find(styleName);
                    if (style == styles_.end()) {
                        auto named = styleDisplayNames_.find(styleName);
                        if (named != styleDisplayNames_.end()) style = styles_.find(named->second);
                    }
                    if (style != styles_.end()) rule.style = style->second;
                } else if (name == "calcext:color-scale") {
                    rule.type = ConditionalFormatType::ColorScale;
                    for (auto* point = entry->FirstChildElement("calcext:color-scale-entry"); point;
                         point = point->NextSiblingElement("calcext:color-scale-entry")) {
                        rule.thresholds.push_back(OdfThreshold(GetAttr(point, "calcext:type"),
                                                               GetAttr(point, "calcext:value")));
                        rule.colorScale.push_back(ParseHexColor(GetAttr(point, "calcext:color", "#ffffff")));
                    }
                    if (rule.colorScale.size() < 2) continue;
                } else if (name == "calcext:data-bar") {
                    rule.type = ConditionalFormatType::DataBar;
                    rule.dataBarColor = ParseHexColor(GetAttr(entry, "calcext:positive-color", "#2a6099"));
                    rule.showValue = GetAttr(entry, "calcext:show-value", "true") != "false";
                    for (auto* point = entry->FirstChildElement("calcext:formatting-entry"); point;
                         point = point->NextSiblingElement("calcext:formatting-entry")) {
                        rule.thresholds.push_back(OdfThreshold(GetAttr(point, "calcext:type"),
                                                               GetAttr(point, "calcext:value")));
                    }
                } else {
                    continue;   // icon sets, dates: not drawn here
                }
                rules.push_back(std::move(rule));
            }
            if (rules.empty()) continue;
            for (const CellRange& range : ranges) {
                ConditionalFormat cf;
                cf.range = range;
                cf.rules = rules;
                sheet->AddConditionalFormat(cf);
            }
        }
    }

};

// ============================================================================
// ODS SAVER
// ============================================================================

class ODSSaver {
private:
    UltraCanvasSpreadsheet* spreadsheet_;
    // UCZipPackageWriter (not miniz's writer) because miniz emits local file
    // headers with the data-descriptor flag set, which the ODF package
    // readers in LibreOffice/OpenOffice reject — such .ods files fail to
    // open even though the archive is otherwise valid.
    UCZipPackageWriter zip_;
    std::map<std::string, std::string> styleMap_;
    // Styles in creation order with their full definition, so
    // WriteStyleDefinition can serialize real properties (the key alone is
    // not enough to reconstruct the style).
    std::vector<std::pair<std::string, CellStyle>> styleDefs_;
    int styleCounter_ = 0;
    // Number/data styles (currency/percentage/date/number) discovered from cell
    // formats, keyed by signature, plus their definitions in creation order.
    std::map<std::string, std::string> numberStyleMap_;
    std::vector<std::pair<std::string, NumberFormat>> numberStyleDefs_;
    int numberStyleCounter_ = 0;
    // Column widths and row heights, as ODF wants them: one automatic style per
    // distinct size, referenced by table:style-name. Writing the size straight
    // onto <table:table-column> (as this writer used to) is not valid ODF, and
    // LibreOffice silently ignores it - the saved sheet reopened with every
    // column back at the default width.
    // A size and whether a manual page break comes before the column / row
    // (fo:break-before="page") -> style name.
    std::map<std::pair<int, bool>, std::string> columnWidthStyles_;
    std::map<std::pair<int, bool>, std::string> rowHeightStyles_;
    // The charts' objects ("Object N/content.xml", XML), gathered as the
    // sheets are written.
    std::vector<std::pair<std::string, std::string>> chartObjects_;

public:
    ODSSaver(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {}

private:
    const char* mimeType_ = ODS::MIMETYPE;   // the template's for an .ots

public:

    bool Save(const std::string& filePath) {
        spreadsheet_->RefreshCharts();   // the charts' tables as the cells are now
        if (!zip_.Open(filePath)) {
            return false;
        }
        // A template (.ots) is the same package under its own media type.
        mimeType_ = IsSpreadsheetTemplateExtension(filePath) ? ODS::TEMPLATE_MIMETYPE : ODS::MIMETYPE;

        // Write mimetype (must be first, uncompressed), then the XML parts.
        if (!zip_.AddEntry("mimetype", std::string(mimeType_), false)
            || !zip_.AddEntry(ODS::STYLES_XML, GenerateStylesXml())
            || !zip_.AddEntry(ODS::CONTENT_XML, GenerateContentXml())
            || !zip_.AddEntry(ODS::META_XML, GenerateMetaXml())
            || !zip_.AddEntry(ODS::SETTINGS_XML, GenerateSettingsXml())
            || !zip_.AddEntry(ODS::MANIFEST_XML, GenerateManifestXml())) {
            return false;
        }
        for (const auto& [part, xml] : chartObjects_) {
            if (!zip_.AddEntry(part, xml)) return false;
        }

        return zip_.Finalize();
    }

private:
    // One page layout and one master page per sheet, carrying the sheet's
    // PrintSettings; the sheet's table style (GenerateContentXml) names it.
    std::string GenerateStylesXml() {
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<office:document-styles "
           << "xmlns:office=\"" << ODS::NS_OFFICE << "\" "
           << "xmlns:style=\"" << ODS::NS_STYLE << "\" "
           << "xmlns:fo=\"" << ODS::NS_FO << "\" "
           << "xmlns:text=\"" << ODS::NS_TEXT << "\" "
           << "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
           << "office:version=\"1.3\">\n";
        ss << "<office:styles>\n";
        WriteConditionalStyles(ss);
        ss << "</office:styles>\n";
        ss << "<office:automatic-styles>\n";
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            if (const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s)) {
                WritePageLayout(ss, s + 1, sheet->GetPrintSettings());
            }
        }
        ss << "</office:automatic-styles>\n";
        ss << "<office:master-styles>\n";
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            if (const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s)) {
                WriteMasterPage(ss, s + 1, sheet->GetPrintSettings());
            }
        }
        ss << "</office:master-styles>\n";
        ss << "</office:document-styles>\n";
        return ss.str();
    }

    static bool HasHeader(const PrintSettings& print) {
        return !print.headerLeft.empty() || !print.headerCenter.empty() || !print.headerRight.empty();
    }
    static bool HasFooter(const PrintSettings& print) {
        return !print.footerLeft.empty() || !print.footerCenter.empty() || !print.footerRight.empty();
    }

    void WritePageLayout(std::stringstream& ss, int number, const PrintSettings& print) {
        const bool landscape = print.orientation == PageOrientation::Landscape;
        const bool header = HasHeader(print), footer = HasFooter(print);
        // With a header the page's top margin is where the header starts
        // (Excel's header margin) and the header's height takes the rest of
        // the top margin; the same for the footer at the bottom.
        const double headerHeight = std::max(0.0, double(print.marginTop) - print.headerMargin);
        const double footerHeight = std::max(0.0, double(print.marginBottom) - print.footerMargin);
        ss << "<style:page-layout style:name=\"pm" << number << "\">"
           << "<style:page-layout-properties";
        double width = 0, height = 0;
        if (PaperDimensions(print.paperSize, width, height)) {
            if (landscape) std::swap(width, height);
            ss << " fo:page-width=\"" << OdfInches(width) << "\" fo:page-height=\""
               << OdfInches(height) << "\"";
        }
        ss << " style:print-orientation=\"" << (landscape ? "landscape" : "portrait") << "\""
           << " fo:margin-top=\"" << OdfInches(header ? print.headerMargin : print.marginTop) << "\""
           << " fo:margin-bottom=\"" << OdfInches(footer ? print.footerMargin : print.marginBottom) << "\""
           << " fo:margin-left=\"" << OdfInches(print.marginLeft) << "\""
           << " fo:margin-right=\"" << OdfInches(print.marginRight) << "\"";
        std::string tokens = "charts drawings";
        if (print.printGridlines) tokens += " grid";
        if (print.printRowColHeaders) tokens += " headers";
        tokens += " objects zero-values";
        ss << " style:print=\"" << tokens << "\"";
        const char* centering = print.centerHorizontally
            ? (print.centerVertically ? "both" : "horizontal")
            : (print.centerVertically ? "vertical" : "none");
        ss << " style:table-centering=\"" << centering << "\"";
        if (print.fitToWidth > 0 || print.fitToHeight > 0) {
            ss << " style:scale-to-X=\"" << std::max(0, print.fitToWidth) << "\""
               << " style:scale-to-Y=\"" << std::max(0, print.fitToHeight) << "\"";
        } else {
            ss << " style:scale-to=\"" << (print.scale > 0 ? print.scale : 100) << "%\"";
        }
        ss << " style:print-page-order=\"ttb\" style:writing-mode=\"lr-tb\"/>";
        auto spacing = [](double minHeight) { return std::min(0.1, minHeight / 2); };
        ss << "<style:header-style><style:header-footer-properties svg:height=\""
           << OdfInches(header ? headerHeight : 0.3) << "\" fo:margin-left=\"0in\""
           << " fo:margin-right=\"0in\" fo:margin-bottom=\""
           << OdfInches(spacing(header ? headerHeight : 0.3)) << "\"/></style:header-style>";
        ss << "<style:footer-style><style:header-footer-properties svg:height=\""
           << OdfInches(footer ? footerHeight : 0.3) << "\" fo:margin-left=\"0in\""
           << " fo:margin-right=\"0in\" fo:margin-top=\""
           << OdfInches(spacing(footer ? footerHeight : 0.3)) << "\"/></style:footer-style>";
        ss << "</style:page-layout>\n";
    }

    void WriteMasterPage(std::stringstream& ss, int number, const PrintSettings& print) {
        ss << "<style:master-page style:name=\"PageStyle_" << number
           << "\" style:page-layout-name=\"pm" << number << "\">";
        auto regions = [&](const char* tag, bool shown, const std::string& left,
                           const std::string& center, const std::string& right) {
            if (!shown) {
                ss << "<" << tag << " style:display=\"false\"/>";
                return;
            }
            ss << "<" << tag << ">";
            if (!left.empty()) {
                ss << "<style:region-left>" << ExcelCodesToOdfParagraphs(left) << "</style:region-left>";
            }
            if (!center.empty()) {
                ss << "<style:region-center>" << ExcelCodesToOdfParagraphs(center)
                   << "</style:region-center>";
            }
            if (!right.empty()) {
                ss << "<style:region-right>" << ExcelCodesToOdfParagraphs(right)
                   << "</style:region-right>";
            }
            ss << "</" << tag << ">";
        };
        regions("style:header", HasHeader(print), print.headerLeft, print.headerCenter,
                print.headerRight);
        regions("style:footer", HasFooter(print), print.footerLeft, print.footerCenter,
                print.footerRight);
        ss << "</style:master-page>\n";
    }

    // settings.xml: each frozen sheet's split, as LibreOffice keeps it in the
    // view settings (split mode 2 = frozen; the position counts columns /
    // rows; PositionRight / PositionBottom are the first unfrozen ones).
    std::string GenerateSettingsXml() {
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<office:document-settings xmlns:office=\"" << ODS::NS_OFFICE << "\" "
           << "xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\" "
           // The set's name "ooo:view-settings" is a qualified name: LibreOffice
           // resolves its prefix, and passes the set over without this.
           << "xmlns:ooo=\"http://openoffice.org/2004/office\" "
           << "office:version=\"1.3\">\n"
           << "<office:settings>\n"
           << "<config:config-item-set config:name=\"ooo:view-settings\">\n"
           << "<config:config-item-map-indexed config:name=\"Views\">\n"
           << "<config:config-item-map-entry>\n"
           << "<config:config-item config:name=\"ViewId\" config:type=\"string\">view1</config:config-item>\n"
           << "<config:config-item-map-named config:name=\"Tables\">\n";
        auto item = [&](const char* name, const char* type, int value) {
            ss << "<config:config-item config:name=\"" << name << "\" config:type=\"" << type
               << "\">" << value << "</config:config-item>";
        };
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            const FreezePanes& freeze = sheet->GetFreezePanes();
            const int cols = std::max(0, freeze.frozenColumns);
            const int rows = std::max(0, freeze.frozenRows);
            ss << "<config:config-item-map-entry config:name=\"" << EscapeXml(sheet->GetName()) << "\">";
            item("HorizontalSplitMode", "short", cols > 0 ? 2 : 0);
            item("VerticalSplitMode", "short", rows > 0 ? 2 : 0);
            item("HorizontalSplitPosition", "int", cols);
            item("VerticalSplitPosition", "int", rows);
            // 0 top left, 1 top right, 2 bottom left, 3 bottom right
            item("ActiveSplitRange", "short", rows > 0 ? (cols > 0 ? 3 : 2) : (cols > 0 ? 1 : 2));
            item("PositionLeft", "int", 0);
            item("PositionRight", "int", cols);
            item("PositionTop", "int", 0);
            item("PositionBottom", "int", rows);
            ss << "</config:config-item-map-entry>\n";
        }
        const SpreadsheetSheet* active = spreadsheet_->GetActiveSheet();
        if (!active) active = spreadsheet_->GetSheet(0);
        ss << "</config:config-item-map-named>\n"
           << "<config:config-item config:name=\"ActiveTable\" config:type=\"string\">"
           << EscapeXml(active ? active->GetName() : std::string()) << "</config:config-item>\n"
           << "</config:config-item-map-entry>\n"
           << "</config:config-item-map-indexed>\n"
           << "</config:config-item-set>\n"
           << "</office:settings>\n"
           << "</office:document-settings>\n";
        return ss.str();
    }

    std::string GenerateContentXml() {
        std::stringstream ss;
        ss.imbue(std::locale::classic());   // numbers in the file are dot-decimal
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<office:document-content "
           << "xmlns:office=\"" << ODS::NS_OFFICE << "\" "
           << "xmlns:table=\"" << ODS::NS_TABLE << "\" "
           << "xmlns:text=\"" << ODS::NS_TEXT << "\" "
           << "xmlns:style=\"" << ODS::NS_STYLE << "\" "
           << "xmlns:fo=\"" << ODS::NS_FO << "\" "
           << "xmlns:number=\"" << ODS::NS_NUMBER << "\" "
           // The "of:" of every table:formula names this namespace: without
           // it LibreOffice cannot tell the formula's syntax and shows Err.
           << "xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" "
           // A sheet's charts are drawing frames naming objects.
           << "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
           << "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
           << "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
           // Comments' authors, and conditional formats as LibreOffice writes them.
           << "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
           << "xmlns:calcext=\"urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0\" "
           << "office:version=\"1.3\">\n";

        // Automatic styles
        ss << "<office:automatic-styles>\n";
        
        // Generate cell styles
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            auto* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            
            sheet->ForEachCell([&](int row, int col, const SpreadsheetCell& cell) {
                if (cell.HasCustomStyle()) {
                    std::string styleName = GetOrCreateStyle(cell.GetStyle());
                }
            });
        }
        
        // Column/row sizing styles, collected before they are referenced below.
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            auto* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            const SheetExtent extent = MeasureSheet(sheet);
            for (int col = 0; col <= extent.lastColumn; ++col) {
                GetOrCreateColumnStyle(sheet->GetColumnWidth(col), IsBreak(sheet->GetPrintSettings().colBreaks, col));
            }
            for (int row = 0; row <= extent.lastRow; ++row) {
                GetOrCreateRowStyle(sheet->GetRowHeight(row), IsBreak(sheet->GetPrintSettings().rowBreaks, row));
            }
        }
        for (const auto& [key, name] : columnWidthStyles_) {
            ss << "<style:style style:name=\"" << name << "\" style:family=\"table-column\">"
               << "<style:table-column-properties style:column-width=\""
               << SpreadsheetPixelsToCentimetres(key.first) << "cm\" fo:break-before=\""
               << (key.second ? "page" : "auto") << "\"/>"
               << "</style:style>\n";
        }
        for (const auto& [key, name] : rowHeightStyles_) {
            ss << "<style:style style:name=\"" << name << "\" style:family=\"table-row\">"
               << "<style:table-row-properties style:row-height=\""
               << SpreadsheetPixelsToCentimetres(key.first) << "cm\" fo:break-before=\""
               << (key.second ? "page" : "auto") << "\"/>"
               << "</style:style>\n";
        }
        // One table style per sheet, naming the sheet's page style (styles.xml).
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            ss << "<style:style style:name=\"ta" << (s + 1) << "\" style:family=\"table\""
               << " style:master-page-name=\"PageStyle_" << (s + 1) << "\">"
               << "<style:table-properties table:display=\"true\" style:writing-mode=\"lr-tb\"/>"
               << "</style:style>\n";
        }

        // Write the number/data styles first so the cell styles that reference
        // them via style:data-style-name resolve.
        for (const auto& [numName, numFmt] : numberStyleDefs_) {
            WriteNumberStyleDefinition(ss, numName, numFmt);
        }

        // Write collected cell styles
        for (const auto& [styleName, style] : styleDefs_) {
            WriteStyleDefinition(ss, styleName, style);
        }

        ss << "</office:automatic-styles>\n";
        
        // Body
        ss << "<office:body>\n";
        ss << "<office:spreadsheet>\n";
        
        // The validations the cells name, then each sheet
        WriteContentValidations(ss);
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            auto* sheet = spreadsheet_->GetSheet(s);
            if (sheet) {
                WriteSheet(ss, sheet, s + 1);
            }
        }

        // The workbook's names (those scoped to a sheet are in its table),
        // then the sheets' auto-filters as database ranges - the order ODF
        // gives them after the tables.
        std::vector<NamedRange> workbookNames;
        for (const NamedRange& named : spreadsheet_->GetNamedRangeList()) {
            if (named.sheetScope.empty()) workbookNames.push_back(named);
        }
        WriteNamedRanges(ss, workbookNames, "");
        WriteDatabaseRanges(ss);
        ss << WriteOdsPivots(*spreadsheet_);   // table:data-pilot-tables, after the database ranges

        ss << "</office:spreadsheet>\n";
        ss << "</office:body>\n";
        ss << "</office:document-content>\n";

        return ss.str();
    }

    // How far a sheet's columns and rows are written: its used range, and
    // beyond it whatever else needs a column or row to stand on - a hidden
    // one, a page break, the print titles, a merge.
    struct SheetExtent {
        int lastColumn = -1;
        int lastRow = -1;
        int lastCellColumn = -1;   // the cells written in each row
    };

    static SheetExtent MeasureSheet(const SpreadsheetSheet* sheet) {
        SheetExtent extent;
        CellRange used = sheet->GetUsedRange();
        extent.lastColumn = extent.lastCellColumn = used.end.col;
        extent.lastRow = used.end.row;
        for (const auto& merge : sheet->GetMergedCells()) {
            extent.lastCellColumn = std::max(extent.lastCellColumn, merge.range.end.col);
            extent.lastRow = std::max(extent.lastRow, merge.range.end.row);
        }
        // A note, and a validation, live on the cells: those cells are
        // written even when empty - a validation's as far as the cells in use
        // or a thousand rows and 52 columns, not down whole columns.
        sheet->ForEachCell([&](int row, int col, const SpreadsheetCell& cell) {
            if (cell.HasComment() || cell.HasValidation()) {
                extent.lastCellColumn = std::max(extent.lastCellColumn, col);
                extent.lastRow = std::max(extent.lastRow, row);
            }
        });
        const int validationRows = std::max(used.end.row, 999);
        const int validationColumns = std::max(used.end.col, 51);
        for (const auto& [range, rule] : sheet->GetValidations()) {
            extent.lastCellColumn = std::max(extent.lastCellColumn, std::min(range.end.col, validationColumns));
            extent.lastRow = std::max(extent.lastRow, std::min(range.end.row, validationRows));
        }
        extent.lastColumn = std::max(extent.lastColumn, extent.lastCellColumn);
        for (const auto& [index, def] : sheet->GetColumnDefinitions()) {
            if (def.hidden) extent.lastColumn = std::max(extent.lastColumn, index);
        }
        for (const auto& [index, def] : sheet->GetRowDefinitions()) {
            if (def.hidden) extent.lastRow = std::max(extent.lastRow, index);
        }
        const PrintSettings& print = sheet->GetPrintSettings();
        for (int col : print.colBreaks) extent.lastColumn = std::max(extent.lastColumn, col);
        for (int row : print.rowBreaks) extent.lastRow = std::max(extent.lastRow, row);
        if (print.HasRepeatColumns()) extent.lastColumn = std::max(extent.lastColumn, print.repeatColumnsLast);
        if (print.HasRepeatRows()) extent.lastRow = std::max(extent.lastRow, print.repeatRowsLast);
        // A table has at least one column and one row.
        extent.lastColumn = std::clamp(extent.lastColumn, 0, SpreadsheetLimits::MaxColumns - 1);
        extent.lastRow = std::clamp(extent.lastRow, 0, SpreadsheetLimits::MaxRows - 1);
        return extent;
    }

    static bool IsBreak(const std::vector<int>& breaks, int index) {
        return index > 0 && std::find(breaks.begin(), breaks.end(), index) != breaks.end();
    }

    void WriteSheet(std::stringstream& ss, const SpreadsheetSheet* sheet, int number) {
        const PrintSettings& print = sheet->GetPrintSettings();
        ss << "<table:table table:name=\"" << EscapeXml(sheet->GetName())
           << "\" table:style-name=\"ta" << number << "\"";
        if (print.HasPrintArea() && print.printArea.IsValid()) {
            ss << " table:print-ranges=\""
               << EscapeXml(OdfRangeAddress(sheet->GetName(), print.printArea)) << "\"";
        }
        ss << ">\n";
        ss << WriteOdsSheetCharts(*sheet, chartObjects_);   // table:shapes, before the columns
        const SheetExtent extent = MeasureSheet(sheet);

        // Write columns; the print-title columns go inside table:table-header-columns.
        for (int col = 0; col <= extent.lastColumn; ++col) {
            if (print.HasRepeatColumns() && col == print.repeatColumnsFirst) {
                ss << "<table:table-header-columns>\n";
            }
            ss << "<table:table-column table:style-name=\""
               << GetOrCreateColumnStyle(sheet->GetColumnWidth(col), IsBreak(print.colBreaks, col)) << "\"";
            if (sheet->IsColumnHidden(col)) ss << " table:visibility=\"collapse\"";
            ss << "/>\n";
            if (print.HasRepeatColumns() && col == print.repeatColumnsLast) {
                ss << "</table:table-header-columns>\n";
            }
        }

        // Write rows; the print-title rows go inside table:table-header-rows.
        for (int row = 0; row <= extent.lastRow; ++row) {
            if (print.HasRepeatRows() && row == print.repeatRowsFirst) {
                ss << "<table:table-header-rows>\n";
            }
            ss << "<table:table-row table:style-name=\""
               << GetOrCreateRowStyle(sheet->GetRowHeight(row), IsBreak(print.rowBreaks, row)) << "\"";
            if (sheet->IsRowHidden(row)) ss << " table:visibility=\"collapse\"";
            ss << ">\n";

            // Write cells (a row holds at least one)
            for (int col = 0; col <= std::max(0, extent.lastCellColumn); ++col) {
                const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);
                WriteCell(ss, cell, sheet, row, col);
            }

            ss << "</table:table-row>\n";
            if (print.HasRepeatRows() && row == print.repeatRowsLast) {
                ss << "</table:table-header-rows>\n";
            }
        }

        // The names scoped to this sheet: the workbook's, and the sheet's own.
        std::vector<NamedRange> sheetNames;
        for (const NamedRange& named : spreadsheet_->GetNamedRangeList()) {
            if (!named.sheetScope.empty() && SameName(named.sheetScope, sheet->GetName())) {
                sheetNames.push_back(named);
            }
        }
        for (const auto& [key, named] : sheet->GetNamedRanges()) sheetNames.push_back(named);
        WriteNamedRanges(ss, sheetNames, sheet->GetName());
        WriteConditionalFormats(ss, sheet);

        ss << "</table:table>\n";
    }

    static bool SameName(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    // table:named-range entries. A range that names no sheet is written
    // without one when the name is the workbook's (it then covers the sheet
    // of the formula using it), and with the sheet a scoped name belongs to.
    void WriteNamedRanges(std::stringstream& ss, const std::vector<NamedRange>& names,
                          const std::string& ownSheet) {
        std::string entries;
        std::vector<std::string> written;
        const SpreadsheetSheet* firstSheet = spreadsheet_->GetSheet(0);
        for (const NamedRange& named : names) {
            if (named.name.empty() || !named.range.IsValid()) continue;
            std::string lower = named.name;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (std::find(written.begin(), written.end(), lower) != written.end()) continue;
            written.push_back(lower);
            std::string sheet = named.range.start.sheetName.empty() ? ownSheet : named.range.start.sheetName;
            std::string base = OdfCellAddress(sheet.empty() && firstSheet ? firstSheet->GetName() : sheet,
                                              named.range.start.row, named.range.start.col);
            entries += "<table:named-range table:name=\"" + EscapeXml(named.name) +
                       "\" table:base-cell-address=\"" + EscapeXml(base) +
                       "\" table:cell-range-address=\"" +
                       EscapeXml(OdfRangeAddress(sheet, named.range)) + "\"/>\n";
        }
        if (!entries.empty()) {
            ss << "<table:named-expressions>\n" << entries << "</table:named-expressions>\n";
        }
    }

    // A sheet's auto-filter is a sheet-local database range showing filter
    // buttons (named as LibreOffice names them); a column filtered to a list
    // of values is an "=" condition with a filter-set-item per value.
    void WriteDatabaseRanges(std::stringstream& ss) {
        std::string entries;
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (!sheet || !sheet->HasAutoFilter()) continue;
            const AutoFilter* filter = sheet->GetAutoFilter();
            entries += "<table:database-range table:name=\"__Anonymous_Sheet_DB__" +
                       std::to_string(s) + "\" table:target-range-address=\"" +
                       EscapeXml(OdfRangeAddress(sheet->GetName(), filter->range)) +
                       "\" table:display-filter-buttons=\"true\"";
            std::string conditions;
            for (const ColumnFilter& column : filter->columnFilters) {
                if (column.op != FilterOperator::Equals || column.values.empty() ||
                    column.column < filter->range.start.col || column.column > filter->range.end.col) {
                    continue;
                }
                conditions += "<table:filter-condition table:field-number=\"" +
                              std::to_string(column.column - filter->range.start.col) +
                              "\" table:value=\"" + EscapeXml(column.values.front()) +
                              "\" table:operator=\"=\"";
                if (column.values.size() == 1) {
                    conditions += "/>";
                    continue;
                }
                conditions += ">";
                for (const std::string& value : column.values) {
                    conditions += "<table:filter-set-item table:value=\"" + EscapeXml(value) + "\"/>";
                }
                conditions += "</table:filter-condition>";
            }
            if (conditions.empty()) {
                entries += "/>\n";
            } else {
                entries += "><table:filter><table:filter-and>" + conditions +
                           "</table:filter-and></table:filter></table:database-range>\n";
            }
        }
        if (!entries.empty()) {
            ss << "<table:database-ranges>\n" << entries << "</table:database-ranges>\n";
        }
    }

    void WriteCell(std::stringstream& ss, const SpreadsheetCell* cell,
                   const SpreadsheetSheet* sheet, int row, int col) {
        // Check for merged cells: the cells a merge covers are
        // table:covered-table-cell, as ODF requires.
        const MergedCell* merge = sheet->GetMergedCell(row, col);
        const char* element = (merge && !merge->IsTopLeft(row, col))
            ? "table:covered-table-cell" : "table:table-cell";
        ss << "<" << element;
        if (merge && merge->IsTopLeft(row, col)) {
            int colSpan = merge->range.end.col - merge->range.start.col + 1;
            int rowSpan = merge->range.end.row - merge->range.start.row + 1;
            if (colSpan > 1) ss << " table:number-columns-spanned=\"" << colSpan << "\"";
            if (rowSpan > 1) ss << " table:number-rows-spanned=\"" << rowSpan << "\"";
        }
        
        // The validation that applies to the cell, by its name.
        const std::string validationName = ValidationNameAt(sheet, row, col);
        if (!validationName.empty()) ss << " table:content-validation-name=\"" << validationName << "\"";

        if (!cell || cell->IsEmpty()) {
            if (cell && cell->HasComment()) {
                ss << ">\n";
                WriteAnnotation(ss, *cell->GetComment());
                ss << "</" << element << ">\n";
            } else {
                ss << "/>\n";
            }
            return;
        }
        
        // Style
        if (cell->HasCustomStyle()) {
            std::string styleName = GetOrCreateStyle(cell->GetStyle());
            ss << " table:style-name=\"" << styleName << "\"";
        }
        
        // Formula. Serialize into ODF syntax (bracketed references, "of:="
        // prefix). When the parsed AST is available we emit precise reference
        // brackets; otherwise fall back to the stored text.
        if (cell->HasFormula()) {
            std::string odf = FormulaToODS(cell, sheet);
            ss << " table:formula=\"of:" << EscapeXml(odf) << "\"";
            // An array formula - a fixed one, or a dynamic one as it spills
            // now (ODF has no dynamic spill) - spans its block.
            if (cell->CoversArray()) {
                ss << " table:number-matrix-columns-spanned=\"" << cell->GetArrayCols() << "\""
                   << " table:number-matrix-rows-spanned=\"" << cell->GetArrayRows() << "\"";
            }
        }
        
        // Value type and value
        switch (cell->GetValueType()) {
            case CellValueType::Number:
                ss << " office:value-type=\"float\"";
                ss << " office:value=\"" << FormatFloatRoundTrip(cell->GetNumber()) << "\"";
                break;
            case CellValueType::Boolean:
                ss << " office:value-type=\"boolean\"";
                ss << " office:boolean-value=\"" << (cell->GetBoolean() ? "true" : "false") << "\"";
                break;
            case CellValueType::Date:
            case CellValueType::DateTime:
                ss << " office:value-type=\"date\"";
                // Convert serial to ISO date
                ss << " office:date-value=\"" << FormatODSDate(cell->GetNumber()) << "\"";
                break;
            case CellValueType::Time:
                ss << " office:value-type=\"time\"";
                ss << " office:time-value=\"" << FormatODSTime(cell->GetNumber()) << "\"";
                break;
            case CellValueType::Percentage:
                ss << " office:value-type=\"percentage\"";
                ss << " office:value=\"" << FormatFloatRoundTrip(cell->GetNumber()) << "\"";
                break;
            case CellValueType::Currency: {
                auto currency = cell->GetCurrency();
                ss << " office:value-type=\"currency\"";
                ss << " office:currency=\"" << currency.currencyCode << "\"";
                ss << " office:value=\"" << FormatFloatRoundTrip(currency.amount) << "\"";
                break;
            }
            case CellValueType::Formula: {
                // Cached evaluation result; the formula itself is already in
                // the table:formula attribute.
                if (auto number = cell->TryGetNumber()) {
                    ss << " office:value-type=\"float\"";
                    ss << " office:value=\"" << FormatFloatRoundTrip(*number) << "\"";
                } else if (auto boolean = cell->TryGetBoolean()) {
                    ss << " office:value-type=\"boolean\"";
                    ss << " office:boolean-value=\"" << (*boolean ? "true" : "false") << "\"";
                } else {
                    ss << " office:value-type=\"string\"";
                }
                break;
            }
            case CellValueType::Text:
            default:
                ss << " office:value-type=\"string\"";
                break;
        }
        
        ss << ">\n";
        
        // The note (before the text, as ODF orders them), then the text -
        // a link when the cell has one.
        if (cell->HasComment()) WriteAnnotation(ss, *cell->GetComment());
        WriteCellParagraph(ss, cell);
        
        ss << "</" << element << ">\n";
    }
    
    // One automatic style per distinct column width / row height (and manual
    // page break before it), named "co1", "ro1", ... exactly as LibreOffice
    // writes them.
    std::string GetOrCreateColumnStyle(int widthPixels, bool pageBreakBefore = false) {
        const auto key = std::make_pair(widthPixels, pageBreakBefore);
        auto it = columnWidthStyles_.find(key);
        if (it != columnWidthStyles_.end()) return it->second;
        std::string name = "co" + std::to_string(columnWidthStyles_.size() + 1);
        columnWidthStyles_[key] = name;
        return name;
    }

    std::string GetOrCreateRowStyle(int heightPixels, bool pageBreakBefore = false) {
        const auto key = std::make_pair(heightPixels, pageBreakBefore);
        auto it = rowHeightStyles_.find(key);
        if (it != rowHeightStyles_.end()) return it->second;
        std::string name = "ro" + std::to_string(rowHeightStyles_.size() + 1);
        rowHeightStyles_[key] = name;
        return name;
    }

    std::string GetOrCreateStyle(const CellStyle& style) {
        std::string key = GenerateStyleKey(style);
        auto it = styleMap_.find(key);
        if (it != styleMap_.end()) {
            return it->second;
        }

        std::string name = "ce" + std::to_string(++styleCounter_);
        styleMap_[key] = name;
        styleDefs_.emplace_back(name, style);
        // Ensure the referenced data style is created now, so number styles are
        // known (and emitted) before the cell styles that reference them.
        GetOrCreateNumberStyle(style.numberFormat);
        return name;
    }

    std::string GenerateStyleKey(const CellStyle& style) {
        std::stringstream ss;
        ss << style.font.family << "|" << style.font.size << "|"
           << style.font.bold << "|" << style.font.italic << "|"
           << style.font.strikethrough << "|"
           << static_cast<int>(style.font.underline) << "|"
           << ColorToHex(style.font.color) << "|"
           << static_cast<int>(style.fill.pattern) << "|"
           << ColorToHex(style.fill.foregroundColor) << "|"
           << static_cast<int>(style.hAlign) << "|"
           << static_cast<int>(style.vAlign) << "|"
           << style.wrapText;
        auto border = [&](const CellBorder& b) {
            ss << "|" << static_cast<int>(b.style) << ColorToHex(b.color);
        };
        border(style.borders.left);
        border(style.borders.right);
        border(style.borders.top);
        border(style.borders.bottom);
        ss << "|nf:" << NumberStyleKey(style.numberFormat);
        return ss.str();
    }

    // Signature for a number format so equal formats share one data style.
    static std::string NumberStyleKey(const NumberFormat& f) {
        std::stringstream ss;
        ss << static_cast<int>(f.category) << "|" << f.decimalPlaces << "|"
           << f.useThousandsSeparator << "|" << f.currencySymbol << "|"
           << f.currencySymbolAfter << "|" << f.negativeRed << "|" << f.formatCode;
        return ss.str();
    }

    // Returns the ODF data-style name for a format, creating it on first use.
    // General/Text formats need no data style, so return "".
    std::string GetOrCreateNumberStyle(const NumberFormat& fmt) {
        if (fmt.category == NumberFormatCategory::General ||
            fmt.category == NumberFormatCategory::Text) {
            return "";
        }
        std::string key = NumberStyleKey(fmt);
        auto it = numberStyleMap_.find(key);
        if (it != numberStyleMap_.end()) return it->second;

        std::string name = "N" + std::to_string(++numberStyleCounter_);
        numberStyleMap_[key] = name;
        numberStyleDefs_.emplace_back(name, fmt);
        return name;
    }

    // ODS border attribute value, e.g. "0.5pt solid #000000". The loader
    // maps any visible style back to Thin, so the width/style detail is a
    // LibreOffice-facing nicety.
    static std::string BorderAttrValue(const CellBorder& border) {
        const char* lineStyle = "solid";
        switch (border.style) {
            case BorderStyle::Dashed:
            case BorderStyle::MediumDashed:
            case BorderStyle::DashDot:
            case BorderStyle::MediumDashDot:
            case BorderStyle::DashDotDot: lineStyle = "dashed"; break;
            case BorderStyle::Dotted: lineStyle = "dotted"; break;
            case BorderStyle::Double: lineStyle = "double"; break;
            default: break;
        }
        const char* width = "0.5pt";
        if (border.style == BorderStyle::Medium) width = "1pt";
        else if (border.style == BorderStyle::Thick) width = "2pt";
        else if (border.style == BorderStyle::Hair) width = "0.25pt";
        return std::string(width) + " " + lineStyle + " " + ColorToHex(border.color);
    }

    // Emit the currency number + symbol runs in the authored order. The loader
    // folds a leading symbol's separating space into currencySymbol; undo that
    // here so the ODF is a clean symbol element plus a text separator.
    void EmitCurrencyBody(std::stringstream& ss, const NumberFormat& fmt) {
        std::stringstream numAttr;
        numAttr << "number:decimal-places=\"" << fmt.decimalPlaces
                << "\" number:min-integer-digits=\"1\"";
        if (fmt.useThousandsSeparator) numAttr << " number:grouping=\"true\"";
        std::string numEl = "<number:number " + numAttr.str() + "/>";

        std::string sym = fmt.currencySymbol;
        bool trailingSpace = !sym.empty() && sym.back() == ' ';
        while (!sym.empty() && sym.back() == ' ') sym.pop_back();
        std::string symEl = "<number:currency-symbol>" + EscapeXml(sym)
                          + "</number:currency-symbol>";

        if (fmt.currencySymbolAfter) {
            ss << numEl << "<number:text> </number:text>" << symEl;
        } else {
            ss << symEl;
            if (trailingSpace) ss << "<number:text> </number:text>";
            ss << numEl;
        }
    }

    // Emit date/time component elements from a format code (inverse of the
    // formatCode built by the loader). Consecutive literals coalesce into one
    // number:text.
    void EmitDateTimeBody(std::stringstream& ss, const std::string& code) {
        std::string lit;
        auto flush = [&]() {
            if (!lit.empty()) { ss << "<number:text>" << EscapeXml(lit) << "</number:text>"; lit.clear(); }
        };
        for (size_t i = 0; i < code.size();) {
            auto st = [&](const char* t) { return code.compare(i, std::strlen(t), t) == 0; };
            if (st("YYYY"))      { flush(); ss << "<number:year number:style=\"long\"/>"; i += 4; }
            else if (st("YY"))   { flush(); ss << "<number:year/>"; i += 2; }
            else if (st("MM"))   { flush(); ss << "<number:month number:style=\"long\"/>"; i += 2; }
            else if (st("M"))    { flush(); ss << "<number:month/>"; i += 1; }
            else if (st("DD"))   { flush(); ss << "<number:day number:style=\"long\"/>"; i += 2; }
            else if (st("D"))    { flush(); ss << "<number:day/>"; i += 1; }
            else if (st("HH"))   { flush(); ss << "<number:hours number:style=\"long\"/>"; i += 2; }
            else if (st("H"))    { flush(); ss << "<number:hours/>"; i += 1; }
            else if (st("NN"))   { flush(); ss << "<number:minutes number:style=\"long\"/>"; i += 2; }
            else if (st("SS"))   { flush(); ss << "<number:seconds number:style=\"long\"/>"; i += 2; }
            else { lit += code[i]; i += 1; }
        }
        flush();
    }

    // Serialize a NumberFormat as an ODF number:*-style (inverse of
    // ParseNumberFormatStyle), so currency/percentage/date/number formats
    // survive a save round-trip.
    void WriteNumberStyleDefinition(std::stringstream& ss, const std::string& name,
                                    const NumberFormat& fmt) {
        auto grouping = [&]() { return fmt.useThousandsSeparator ? " number:grouping=\"true\"" : ""; };
        switch (fmt.category) {
            case NumberFormatCategory::Number:
                ss << "<number:number-style style:name=\"" << name << "\">"
                   << "<number:number number:decimal-places=\"" << fmt.decimalPlaces
                   << "\" number:min-integer-digits=\"1\"" << grouping()
                   << "/></number:number-style>\n";
                break;
            case NumberFormatCategory::Percentage:
                ss << "<number:percentage-style style:name=\"" << name << "\">"
                   << "<number:number number:decimal-places=\"" << fmt.decimalPlaces
                   << "\" number:min-integer-digits=\"1\"" << grouping()
                   << "/><number:text>%</number:text></number:percentage-style>\n";
                break;
            case NumberFormatCategory::Currency:
            case NumberFormatCategory::Accounting:
                if (fmt.negativeRed) {
                    // Positive variant, then a red base with a leading minus and
                    // a map back to the positive style (matches LibreOffice).
                    ss << "<number:currency-style style:name=\"" << name
                       << "P\" style:volatile=\"true\">";
                    EmitCurrencyBody(ss, fmt);
                    ss << "</number:currency-style>\n";
                    ss << "<number:currency-style style:name=\"" << name << "\">"
                       << "<style:text-properties fo:color=\"#ff0000\"/>"
                       << "<number:text>-</number:text>";
                    EmitCurrencyBody(ss, fmt);
                    ss << "<style:map style:condition=\"value()&gt;=0\""
                       << " style:apply-style-name=\"" << name << "P\"/>"
                       << "</number:currency-style>\n";
                } else {
                    ss << "<number:currency-style style:name=\"" << name << "\">";
                    EmitCurrencyBody(ss, fmt);
                    ss << "</number:currency-style>\n";
                }
                break;
            case NumberFormatCategory::Date:
            case NumberFormatCategory::DateTime:
                ss << "<number:date-style style:name=\"" << name << "\">";
                EmitDateTimeBody(ss, fmt.formatCode);
                ss << "</number:date-style>\n";
                break;
            case NumberFormatCategory::Time:
                ss << "<number:time-style style:name=\"" << name << "\">";
                EmitDateTimeBody(ss, fmt.formatCode);
                ss << "</number:time-style>\n";
                break;
            default:
                break;
        }
    }

    void WriteStyleDefinition(std::stringstream& ss, const std::string& name,
                              const CellStyle& style) {
        ss << "<style:style style:name=\"" << name << "\" style:family=\"table-cell\"";
        // Link the number/data style (already emitted) so currency/percentage/
        // date formats round-trip.
        std::string dataStyle = GetOrCreateNumberStyle(style.numberFormat);
        if (!dataStyle.empty()) {
            ss << " style:data-style-name=\"" << dataStyle << "\"";
        }
        ss << ">\n";

        // Cell properties: background, borders, vertical alignment, wrap.
        {
            std::stringstream props;
            if (style.fill.HasFill()) {
                props << " fo:background-color=\""
                      << ColorToHex(style.fill.foregroundColor) << "\"";
            }
            const CellBorders& b = style.borders;
            bool uniform = b.left.IsVisible()
                && b.left.style == b.right.style && b.left.style == b.top.style
                && b.left.style == b.bottom.style && b.left.color == b.right.color
                && b.left.color == b.top.color && b.left.color == b.bottom.color;
            if (uniform) {
                props << " fo:border=\"" << BorderAttrValue(b.left) << "\"";
            } else {
                if (b.left.IsVisible())
                    props << " fo:border-left=\"" << BorderAttrValue(b.left) << "\"";
                if (b.right.IsVisible())
                    props << " fo:border-right=\"" << BorderAttrValue(b.right) << "\"";
                if (b.top.IsVisible())
                    props << " fo:border-top=\"" << BorderAttrValue(b.top) << "\"";
                if (b.bottom.IsVisible())
                    props << " fo:border-bottom=\"" << BorderAttrValue(b.bottom) << "\"";
            }
            if (style.vAlign == VerticalAlignment::Top) {
                props << " style:vertical-align=\"top\"";
            } else if (style.vAlign == VerticalAlignment::Middle) {
                props << " style:vertical-align=\"middle\"";
            }
            if (style.wrapText) {
                props << " fo:wrap-option=\"wrap\"";
            }
            if (!props.str().empty()) {
                ss << "<style:table-cell-properties" << props.str() << "/>\n";
            }
        }

        // Horizontal alignment lives in paragraph properties.
        switch (style.hAlign) {
            case HorizontalAlignment::Left:
                ss << "<style:paragraph-properties fo:text-align=\"start\"/>\n";
                break;
            case HorizontalAlignment::Center:
                ss << "<style:paragraph-properties fo:text-align=\"center\"/>\n";
                break;
            case HorizontalAlignment::Right:
                ss << "<style:paragraph-properties fo:text-align=\"end\"/>\n";
                break;
            case HorizontalAlignment::Justify:
            case HorizontalAlignment::Distributed:
                ss << "<style:paragraph-properties fo:text-align=\"justify\"/>\n";
                break;
            default:
                break;
        }

        // Text properties: font, weight, style, color, decorations.
        {
            std::stringstream props;
            CellFont defaults;
            if (style.font.family != defaults.family) {
                props << " style:font-name=\"" << EscapeXml(style.font.family) << "\""
                      << " fo:font-family=\"" << EscapeXml(style.font.family) << "\"";
            }
            if (style.font.size != defaults.size) {
                props << " fo:font-size=\"" << style.font.size << "pt\"";
            }
            if (style.font.bold) props << " fo:font-weight=\"bold\"";
            if (style.font.italic) props << " fo:font-style=\"italic\"";
            if (!(style.font.color == defaults.color)) {
                props << " fo:color=\"" << ColorToHex(style.font.color) << "\"";
            }
            if (style.font.underline != UnderlineStyle::NoUnderline) {
                props << " style:text-underline-style=\""
                      << (style.font.underline == UnderlineStyle::Double
                          || style.font.underline == UnderlineStyle::DoubleAccounting
                              ? "double" : "solid")
                      << "\" style:text-underline-width=\"auto\"";
            }
            if (style.font.strikethrough) {
                props << " style:text-line-through-style=\"solid\"";
            }
            if (!props.str().empty()) {
                ss << "<style:text-properties" << props.str() << "/>\n";
            }
        }

        ss << "</style:style>\n";
    }
    
    // ---- Native formula -> ODF formula serialization ---------------------
    // ODF references are bracketed and sheet-qualified with a '.', e.g.
    // [.D28], [.B28:.B42], ['Sheet Name'.B55]. A current-sheet reference has an
    // empty sheet part ("[."). This is the inverse of OdsFormulaToNative().
    static std::string QuoteSheetName(const std::string& name) {
        // ODF requires quoting sheet names that are not simple identifiers.
        bool simple = !name.empty();
        for (char c : name) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                simple = false;
                break;
            }
        }
        if (simple) return name;
        std::string q = "'";
        for (char c : name) { if (c == '\'') q += '\''; q += c; }  // double internal quotes
        q += "'";
        return q;
    }

    static std::string BareCell(CellAddress addr) {
        addr.sheetName.clear();
        return addr.ToString();  // column letters + optional '$' + row
    }

    static std::string ODSCellRef(const CellAddress& addr) {
        std::string sheet = addr.sheetName.empty() ? "" : QuoteSheetName(addr.sheetName);
        return "[" + sheet + "." + BareCell(addr) + "]";
    }

    static std::string ODSRangeRef(const CellRange& range) {
        std::string startSheet =
            range.start.sheetName.empty() ? "" : QuoteSheetName(range.start.sheetName);
        // The end keeps its own sheet only when it differs from the start's.
        std::string endSheet;
        if (!range.end.sheetName.empty() && range.end.sheetName != range.start.sheetName) {
            endSheet = QuoteSheetName(range.end.sheetName);
        }
        return "[" + startSheet + "." + BareCell(range.start) + ":" +
               endSheet + "." + BareCell(range.end) + "]";
    }

    // `sheet` is the formula's own: an A1# on it is written as the block A1
    // spills now, since ODF has no spill reference.
    static std::string EmitODS(const FormulaNode* node, const SpreadsheetSheet* sheet = nullptr) {
        if (!node) return "";
        switch (node->nodeType) {
            case FormulaNodeType::Literal: {
                const FormulaValue& v = node->literalValue;
                if (v.IsEmpty()) return "";   // an argument left out
                if (v.IsText()) return "\"" + v.GetText() + "\"";
                if (v.IsBoolean()) return v.GetBoolean() ? "TRUE()" : "FALSE()";
                if (v.IsError()) return CellErrorToString(v.GetError());
                // An ODS formula uses the comma (or semicolon) as its
                // argument separator, so a comma decimal point would split one
                // literal into two arguments. The document's numbers are
                // dot-decimal whatever the writer's locale is, and keep
                // every digit.
                return FormatFloatRoundTrip(v.GetNumber());
            }
            case FormulaNodeType::CellRef:
                return ODSCellRef(node->cellRef);
            case FormulaNodeType::RangeRef:
                return ODSRangeRef(node->rangeRef);
            case FormulaNodeType::SpillRef: {
                const SpreadsheetSheet* on = sheet;
                if (!node->cellRef.sheetName.empty() && sheet && sheet->GetParent()) {
                    on = sheet->GetParent()->GetSheetByName(node->cellRef.sheetName);
                }
                const SpreadsheetCell* anchor = on ? on->GetCellIfExists(node->cellRef.row, node->cellRef.col) : nullptr;
                if (!anchor || !anchor->CoversArray()) return ODSCellRef(node->cellRef);
                CellRange range(node->cellRef.row, node->cellRef.col,
                                node->cellRef.row + anchor->GetArrayRows() - 1,
                                node->cellRef.col + anchor->GetArrayCols() - 1);
                range.start.sheetName = range.end.sheetName = node->cellRef.sheetName;
                range.start.rowRef = node->cellRef.rowRef;
                range.start.colRef = node->cellRef.colRef;
                return ODSRangeRef(range);
            }
            case FormulaNodeType::Array: {
                // ODF's inline array: ';' between columns, '|' between rows.
                std::string out = "{";
                for (size_t i = 0; i < node->children.size(); ++i) {
                    if (i) out += node->arrayCols > 0 && i % static_cast<size_t>(node->arrayCols) == 0 ? "|" : ";";
                    out += EmitODS(node->children[i].get(), sheet);
                }
                return out + "}";
            }
            case FormulaNodeType::NamedRef:
                return node->namedRef;
            case FormulaNodeType::BinaryOp:
                if (node->children.size() == 2) {
                    return "(" + EmitODS(node->children[0].get(), sheet) + node->op +
                           EmitODS(node->children[1].get(), sheet) + ")";
                }
                return "";
            case FormulaNodeType::UnaryOp:
                if (node->children.size() == 1) {
                    if (node->op == "%") return "(" + EmitODS(node->children[0].get(), sheet) + "%)";
                    // LibreOffice intersects where one value is expected anyway.
                    if (node->op == "@") return EmitODS(node->children[0].get(), sheet);
                    return "(" + node->op + EmitODS(node->children[0].get(), sheet) + ")";
                }
                return "";
            case FormulaNodeType::FunctionCall: {
                std::string out = NativeFunctionToOds(node->functionName) + "(";
                for (size_t i = 0; i < node->children.size(); ++i) {
                    if (i) out += ";";  // ODF uses ';' as the argument separator
                    out += EmitODS(node->children[i].get(), sheet);
                }
                out += ")";
                return out;
            }
            case FormulaNodeType::Invoke: {
                if (node->children.empty()) return "";
                std::string out = EmitODS(node->children[0].get(), sheet) + "(";
                for (size_t i = 1; i < node->children.size(); ++i) {
                    if (i > 1) out += ";";
                    out += EmitODS(node->children[i].get(), sheet);
                }
                return out + ")";
            }
            default:
                return "";
        }
    }

    static std::string FormulaToODS(const SpreadsheetCell* cell, const SpreadsheetSheet* sheet = nullptr) {
        auto formula = cell->GetFormula();
        if (formula && formula->GetAST()) {
            return "=" + EmitODS(formula->GetAST(), sheet);
        }
        // Fall back to the stored text when no parsed AST is available.
        return cell->GetFormulaText();
    }

    // Serial numbers use the ODS/Excel epoch (Dec 30, 1899) - the same epoch
    // DateTimeValue uses - so convert through DateTimeValue rather than a
    // Unix-epoch/localtime round-trip (which is timezone-dependent and breaks
    // for pre-1970 dates). Keeps load/save symmetric with the loader.
    std::string FormatODSDate(double serial) {
        int y = 0, m = 0, d = 0;
        DateTimeValue(serial).ToDate(y, m, d);
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(4) << y << "-"
           << std::setw(2) << m << "-" << std::setw(2) << d;
        return ss.str();
    }

    std::string FormatODSTime(double serial) {
        int h = 0, m = 0, s = 0;
        DateTimeValue(serial).ToTime(h, m, s);
        std::stringstream ss;
        ss << "PT" << h << "H" << m << "M" << s << "S";
        return ss.str();
    }
    
    std::string EscapeXml(const std::string& text) {
        std::string result;
        result.reserve(text.size() * 1.1);
        for (char c : text) {
            switch (c) {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '"': result += "&quot;"; break;
                case '\'': result += "&apos;"; break;
                default: result += c;
            }
        }
        return result;
    }
    
    std::string GenerateMetaXml() {
        std::stringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<office:document-meta xmlns:office=\"" << ODS::NS_OFFICE << "\" "
           << "office:version=\"1.3\">\n";
        ss << "<office:meta>\n";
        ss << "<meta:generator>UltraCanvas Spreadsheet</meta:generator>\n";
        ss << "</office:meta>\n";
        ss << "</office:document-meta>\n";
        return ss.str();
    }
    
    std::string GenerateManifestXml() {
        std::stringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
           << "manifest:version=\"1.3\">\n";
        ss << "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"" << mimeType_ << "\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"settings.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << OdsChartManifestEntries(chartObjects_);
        ss << "</manifest:manifest>\n";
        return ss.str();
    }
    // ===== VALIDATIONS, COMMENTS, LINKS, CONDITIONAL FORMATS =====

    // The validations' names (val1, val2 ...) by the rule they are, and the
    // conditional formats' cell styles (written as common styles in
    // styles.xml, which calcext:apply-style-name names) by their rule.
    std::map<const DataValidation*, std::string> validationNames_;
    std::map<const ConditionalFormatRule*, std::string> conditionalStyleNames_;

    static bool RuleHasStyle(const ConditionalFormatRule& rule) {
        return rule.type != ConditionalFormatType::ColorScale && rule.type != ConditionalFormatType::DataBar &&
               rule.type != ConditionalFormatType::IconSet && rule.type != ConditionalFormatType::TimePeriod;
    }

    // A formula of ours ("=A1>3", or without its '=') in ODF syntax, without
    // the '=' ([.A1]>3); the text as it is when it does not parse.
    static std::string NativeToOdsExpression(const std::string& formula) {
        std::string text = TrimOdf(formula);
        if (text.empty()) return text;
        if (text[0] != '=') text = "=" + text;
        SpreadsheetFormula parsed(text, CellAddress(0, 0));
        if (parsed.Parse() && parsed.IsValid() && parsed.GetAST()) {
            std::string odf = EmitODS(parsed.GetAST());
            // EmitODS wraps each operation in parentheses; one around the
            // whole is not needed.
            return odf;
        }
        return text.substr(1);
    }

    // An operand: a number or "text" as it is, a formula in ODF syntax,
    // bare text quoted.
    static std::string OdsOperand(const std::string& operand) {
        const std::string t = TrimOdf(operand);
        if (t.empty() || IsOdfNumber(t) || (t.size() >= 2 && t.front() == '"' && t.back() == '"')) return t;
        if (t[0] == '=') return NativeToOdsExpression(t);
        std::string quoted = "\"";
        for (char c : t) {
            quoted += c;
            if (c == '"') quoted += '"';
        }
        return quoted + "\"";
    }

    static std::string OdsBound(const std::string& bound, ValidationType type) {
        if (auto number = ReadDataValidationBound(bound, type)) return FormatFloatRoundTrip(*number);
        return OdsOperand(bound);
    }

    static std::string OdfValidationCondition(const DataValidation& rule) {
        const bool pair = rule.op == ValidationOperator::Between || rule.op == ValidationOperator::NotBetween;
        auto comparison = [&](const std::string& subject, const std::string& between) {
            const std::string a = OdsBound(rule.formula1, rule.type);
            if (pair) {
                return between + (rule.op == ValidationOperator::NotBetween ? "-is-not-between(" : "-is-between(") +
                       a + "," + OdsBound(rule.formula2, rule.type) + ")";
            }
            return subject + OdfComparison(rule.op) + a;
        };
        switch (rule.type) {
            case ValidationType::WholeNumber:
                return "of:cell-content-is-whole-number() and " + comparison("cell-content()", "cell-content");
            case ValidationType::Decimal:
                return "of:cell-content-is-decimal-number() and " + comparison("cell-content()", "cell-content");
            case ValidationType::Date:
                return "of:cell-content-is-date() and " + comparison("cell-content()", "cell-content");
            case ValidationType::Time:
                return "of:cell-content-is-time() and " + comparison("cell-content()", "cell-content");
            case ValidationType::TextLength:
                return "of:" + comparison("cell-content-text-length()", "cell-content-text-length");
            case ValidationType::List: {
                if (IsRangeListSource(rule)) {
                    return "of:cell-content-is-in-list(" + NativeToOdsExpression(rule.formula1) + ")";
                }
                std::string items;
                for (const std::string& item : DataValidationListItems(rule)) {
                    std::string quoted = "\"";
                    for (char c : item) {
                        quoted += c;
                        if (c == '"') quoted += '"';
                    }
                    items += (items.empty() ? "" : ";") + quoted + "\"";
                }
                return "of:cell-content-is-in-list(" + items + ")";
            }
            case ValidationType::Custom:
                return "of:is-true-formula(" + NativeToOdsExpression(rule.formula1) + ")";
            default:
                return std::string();
        }
    }

    // table:content-validations, before the tables: one entry per rule set
    // on a range or carried by a cell, named for the cells to point at.
    void WriteContentValidations(std::stringstream& ss) {
        std::string entries;
        int counter = 0;
        auto add = [&](const SpreadsheetSheet* sheet, const DataValidation& rule, const CellAddress& base) {
            const std::string condition = OdfValidationCondition(rule);
            if (condition.empty()) return;
            const std::string name = "val" + std::to_string(++counter);
            validationNames_[&rule] = name;
            entries += "<table:content-validation table:name=\"" + name + "\" table:condition=\"" +
                       EscapeXml(condition) + "\" table:allow-empty-cell=\"" + (rule.allowBlank ? "true" : "false") +
                       "\" table:base-cell-address=\"" +
                       EscapeXml(OdfCellAddress(sheet->GetName(), base.row, base.col)) + "\"";
            if (rule.type == ValidationType::List) {
                entries += std::string(" table:display-list=\"") + (rule.showDropdown ? "unsorted" : "none") + "\"";
            }
            entries += ">";
            if (rule.showInputMessage || !rule.inputMessage.empty() || !rule.inputTitle.empty()) {
                entries += "<table:help-message table:title=\"" + EscapeXml(rule.inputTitle) +
                           "\" table:display=\"" + (rule.showInputMessage ? "true" : "false") + "\">" +
                           ParagraphsXml(rule.inputMessage) + "</table:help-message>";
            }
            const char* type = rule.alertStyle == ValidationAlertStyle::Warning ? "warning"
                             : rule.alertStyle == ValidationAlertStyle::Information ? "information" : "stop";
            entries += std::string("<table:error-message table:message-type=\"") + type + "\" table:title=\"" +
                       EscapeXml(rule.errorTitle) + "\" table:display=\"" +
                       (rule.showErrorAlert ? "true" : "false") + "\">" + ParagraphsXml(rule.errorMessage) +
                       "</table:error-message>";
            entries += "</table:content-validation>\n";
        };
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            for (const auto& [range, rule] : sheet->GetValidations()) {
                if (rule.IsValid() && range.IsValid()) add(sheet, rule, range.start);
            }
            sheet->ForEachCell([&](int row, int col, const SpreadsheetCell& cell) {
                if (cell.HasValidation() && cell.GetValidation()->IsValid()) {
                    add(sheet, *cell.GetValidation(), CellAddress(row, col));
                }
            });
        }
        if (!entries.empty()) ss << "<table:content-validations>\n" << entries << "</table:content-validations>\n";
    }

    std::string ParagraphsXml(const std::string& text) {
        std::string out;
        size_t start = 0;
        while (true) {
            const size_t end = text.find('\n', start);
            out += "<text:p>" + EscapeXml(text.substr(start, end == std::string::npos ? std::string::npos
                                                                                     : end - start)) + "</text:p>";
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return out;
    }

    // The name of the validation that applies to a cell, if any was written.
    std::string ValidationNameAt(const SpreadsheetSheet* sheet, int row, int col) const {
        const DataValidation* rule = sheet->GetEffectiveValidation(row, col);
        if (!rule) return std::string();
        auto found = validationNames_.find(rule);
        return found == validationNames_.end() ? std::string() : found->second;
    }

    void WriteAnnotation(std::stringstream& ss, const CellComment& comment) {
        ss << "<office:annotation" << (comment.visible ? " office:display=\"true\"" : "") << ">";
        if (!comment.author.empty()) ss << "<dc:creator>" << EscapeXml(comment.author) << "</dc:creator>";
        ss << ParagraphsXml(comment.text) << "</office:annotation>\n";
    }

    // The cell's shown text as its paragraph, a link (text:a) when it has one.
    void WriteCellParagraph(std::stringstream& ss, const SpreadsheetCell* cell) {
        const std::string text = EscapeXml(cell->GetDisplayValue());
        const CellHyperlink* link = cell->GetHyperlink();
        if (!link || link->target.empty()) {
            ss << "<text:p>" << text << "</text:p>\n";
            return;
        }
        std::string href = link->target;
        const bool internal = link->type == HyperlinkType::InternalCell || link->type == HyperlinkType::InternalRange ||
                              link->type == HyperlinkType::InternalNamedRange;
        if (internal && href[0] != '#') href = "#" + href;
        if (link->type == HyperlinkType::Email && href.rfind("mailto:", 0) != 0) href = "mailto:" + href;
        ss << "<text:p><text:a xlink:href=\"" << EscapeXml(href) << "\" xlink:type=\"simple\"";
        if (!link->tooltip.empty()) ss << " office:title=\"" << EscapeXml(link->tooltip) << "\"";
        ss << ">" << text << "</text:a></text:p>\n";
    }

    // The conditional formats' cell styles, as common styles in styles.xml.
    void WriteConditionalStyles(std::stringstream& ss) {
        int counter = 0;
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            for (const ConditionalFormat& format : sheet->GetConditionalFormats()) {
                for (const ConditionalFormatRule& rule : format.rules) {
                    if (!RuleHasStyle(rule)) continue;
                    const std::string name = "ConditionalStyle_" + std::to_string(++counter);
                    conditionalStyleNames_[&rule] = name;
                    CellStyle style = rule.style;
                    style.numberFormat = NumberFormat();   // a data style lives in content.xml
                    WriteStyleDefinition(ss, name, style);
                }
            }
        }
    }

    std::string OdfConditionValue(const ConditionalFormatRule& rule, const std::string& anchor) const {
        auto quoted = [](const std::string& text) {
            std::string out = "\"";
            for (char c : text) {
                out += c;
                if (c == '"') out += '"';
            }
            return out + "\"";
        };
        switch (rule.type) {
            case ConditionalFormatType::CellValue:
                if (rule.op == ValidationOperator::Between || rule.op == ValidationOperator::NotBetween) {
                    return std::string(rule.op == ValidationOperator::Between ? "between(" : "not-between(") +
                           OdsOperand(rule.formula1) + "," + OdsOperand(rule.formula2) + ")";
                }
                return OdfComparison(rule.op) + OdsOperand(rule.formula1);
            case ConditionalFormatType::Expression:
                return "formula-is(" + NativeToOdsExpression(rule.formula1) + ")";
            case ConditionalFormatType::ContainsText: return "contains-text(" + quoted(rule.formula1) + ")";
            case ConditionalFormatType::NotContainsText: return "not-contains-text(" + quoted(rule.formula1) + ")";
            case ConditionalFormatType::BeginsWithText: return "begins-with(" + quoted(rule.formula1) + ")";
            case ConditionalFormatType::EndsWithText: return "ends-with(" + quoted(rule.formula1) + ")";
            case ConditionalFormatType::ContainsBlanks: return "formula-is(LEN(TRIM(" + anchor + "))=0)";
            case ConditionalFormatType::NotContainsBlanks: return "formula-is(LEN(TRIM(" + anchor + "))>0)";
            case ConditionalFormatType::ContainsErrors: return "is-error";
            case ConditionalFormatType::NotContainsErrors: return "is-no-error";
            case ConditionalFormatType::DuplicateValues: return "duplicate";
            case ConditionalFormatType::UniqueValues: return "unique";
            case ConditionalFormatType::Top10: {
                const int rank = std::atoi(TrimOdf(rule.formula1).c_str());
                const std::string name = std::string(rule.bottom ? "bottom" : "top") +
                                         (rule.percent ? "-percent(" : "-elements(");
                return name + std::to_string(rank > 0 ? rank : 10) + ")";
            }
            case ConditionalFormatType::AboveAverage:
                return std::string(rule.aboveAverage ? "above" : "below") + (rule.equalAverage ? "-equal" : "") +
                       "-average";
            default:
                return std::string();
        }
    }

    static std::vector<ConditionalThreshold> ThresholdsOf(const ConditionalFormatRule& rule, size_t count) {
        if (rule.thresholds.size() == count) return rule.thresholds;
        std::vector<ConditionalThreshold> points;
        if (rule.colorScaleValues.size() == count) {
            for (double v : rule.colorScaleValues) {
                points.emplace_back(ConditionalThresholdKind::Number, FormatFloatRoundTrip(v));
            }
            return points;
        }
        points.emplace_back(ConditionalThresholdKind::Lowest);
        if (count == 3) points.emplace_back(ConditionalThresholdKind::Percentile, "50");
        points.emplace_back(ConditionalThresholdKind::Highest);
        return points;
    }

    static std::string OdfThresholdValue(const ConditionalThreshold& point) {
        if (point.kind == ConditionalThresholdKind::Lowest || point.kind == ConditionalThresholdKind::Highest) {
            return "0";
        }
        if (point.kind == ConditionalThresholdKind::Formula ||
            (point.kind == ConditionalThresholdKind::Number && !IsOdfNumber(point.value))) {
            return NativeToOdsExpression(point.value);
        }
        return TrimOdf(point.value);
    }

    void WriteConditionalFormats(std::stringstream& ss, const SpreadsheetSheet* sheet) {
        std::string body;
        for (const ConditionalFormat& format : sheet->GetConditionalFormats()) {
            if (!format.range.IsValid() || format.rules.empty()) continue;
            const std::string base = OdfCellAddress(sheet->GetName(), format.range.start.row, format.range.start.col);
            const std::string anchor = "[." + CellAddress::ColumnToLetter(format.range.start.col) +
                                       std::to_string(format.range.start.row + 1) + "]";
            std::string entries;
            // ODF keeps the rules in the order they are tried: by priority
            // (a rule without one after those with one), then as they are.
            std::vector<const ConditionalFormatRule*> ordered;
            for (const ConditionalFormatRule& rule : format.rules) ordered.push_back(&rule);
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
                const int pa = a->priority > 0 ? a->priority : INT_MAX;
                const int pb = b->priority > 0 ? b->priority : INT_MAX;
                return pa < pb;
            });
            for (const ConditionalFormatRule* rulePointer : ordered) {
                const ConditionalFormatRule& rule = *rulePointer;
                if (rule.type == ConditionalFormatType::ColorScale) {
                    if (rule.colorScale.size() < 2) continue;
                    const size_t count = std::min<size_t>(rule.colorScale.size(), 3);
                    const auto points = ThresholdsOf(rule, count);
                    entries += "<calcext:color-scale>";
                    for (size_t i = 0; i < count; ++i) {
                        entries += "<calcext:color-scale-entry calcext:value=\"" +
                                   EscapeXml(OdfThresholdValue(points[i])) + "\" calcext:type=\"" +
                                   OdfThresholdType(points[i].kind) + "\" calcext:color=\"" +
                                   ColorToHex(rule.colorScale[i]) + "\"/>";
                    }
                    entries += "</calcext:color-scale>\n";
                    continue;
                }
                if (rule.type == ConditionalFormatType::DataBar) {
                    const auto points = ThresholdsOf(rule, 2);
                    entries += "<calcext:data-bar calcext:max-length=\"100\" calcext:positive-color=\"" +
                               ColorToHex(rule.dataBarColor) + "\" calcext:negative-color=\"#ff0000\"";
                    if (!rule.showValue) entries += " calcext:show-value=\"false\"";
                    entries += ">";
                    for (const auto& point : points) {
                        entries += "<calcext:formatting-entry calcext:value=\"" +
                                   EscapeXml(OdfThresholdValue(point)) + "\" calcext:type=\"" +
                                   OdfThresholdType(point.kind) + "\"/>";
                    }
                    entries += "</calcext:data-bar>\n";
                    continue;
                }
                auto style = conditionalStyleNames_.find(&rule);
                const std::string value = OdfConditionValue(rule, anchor);
                if (style == conditionalStyleNames_.end() || value.empty()) continue;
                entries += "<calcext:condition calcext:apply-style-name=\"" + style->second +
                           "\" calcext:value=\"" + EscapeXml(value) + "\" calcext:base-cell-address=\"" +
                           EscapeXml(base) + "\"/>\n";
            }
            if (entries.empty()) continue;
            body += "<calcext:conditional-format calcext:target-range-address=\"" +
                    EscapeXml(OdfRangeAddress(sheet->GetName(), format.range)) + "\">\n" + entries +
                    "</calcext:conditional-format>\n";
        }
        if (!body.empty()) ss << "<calcext:conditional-formats>\n" << body << "</calcext:conditional-formats>\n";
    }

};

// ============================================================================
// CSV LOADER / SAVER
// ============================================================================

class CSVLoader {
public:
    // Load a CSV file into the sheet using the shared import module. The options
    // control encoding, separators, the start row and number recognition. When
    // 'detect' is true the options are auto-detected from the file first.
    static bool Load(const std::string& filePath, SpreadsheetSheet* sheet,
                     const CSVImportOptions& options, bool detect = false,
                     std::string* error = nullptr) {
        std::string raw;
        if (!CSVReadFileRaw(filePath, raw, error)) return false;

        CSVImportOptions opt = options;
        if (detect) {
            CSVImportOptions detected =
                CSVDetectOptions(raw.substr(0, std::min<size_t>(raw.size(), 64 * 1024)));
            // Keep the caller's encoding only if they forced one via 'options';
            // otherwise adopt every detected setting.
            opt = detected;
        }

        CSVGrid grid = CSVParse(CSVDecodeToUtf8(raw, opt.encoding), opt);
        PopulateSheet(sheet, grid, opt);
        return true;
    }

    // Apply an already-parsed grid to a sheet, honouring number recognition and
    // the "quoted values are text" option.
    static void PopulateSheet(SpreadsheetSheet* sheet, const CSVGrid& grid,
                              const CSVImportOptions& opt) {
        for (int row = 0; row < static_cast<int>(grid.size())
                          && row < SpreadsheetLimits::MaxRows; ++row) {
            const CSVRow& fields = grid[row];
            for (int col = 0; col < static_cast<int>(fields.size())
                              && col < SpreadsheetLimits::MaxColumns; ++col) {
                const CSVField& f = fields[col];
                if (f.text.empty()) continue;

                SpreadsheetCell* cell = sheet->GetCell(row, col);
                if (!cell) continue;

                // Quoted values stay text when requested; formulas always honour '='.
                bool forceText = opt.quotedAsText && f.quoted;
                double num = 0.0;
                bool isPercent = false;
                if (!forceText && f.text[0] != '=' &&
                    CSVTryParseNumber(f.text, opt, num, isPercent)) {
                    if (isPercent) cell->SetPercentage(num);
                    else cell->SetNumber(num);
                } else {
                    cell->SetValueFromString(f.text);
                }
            }
        }
    }

    // Build the CSV/TSV text (UTF-8, before charset encoding) honouring the
    // separator, quote character, quoting policy and line ending.
    static std::string Build(const SpreadsheetSheet* sheet, const CSVExportOptions& opt) {
        std::string out;
        CellRange used = sheet->GetUsedRange();
        std::vector<std::string> fields;
        for (int row = used.start.row; row <= used.end.row; ++row) {
            fields.clear();
            for (int col = used.start.col; col <= used.end.col; ++col) {
                const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);
                fields.push_back(cell ? cell->GetDisplayValue() : std::string());
            }
            out += CSVFormatRecord(fields, opt);
        }
        return out;
    }

    static bool Save(const std::string& filePath, const SpreadsheetSheet* sheet,
                     const CSVExportOptions& opt) {
        std::ofstream file(UltraCanvas::PathFromUtf8(filePath), std::ios::binary);
        if (!file.is_open()) return false;

        std::string text = CSVEncodeFromUtf8(Build(sheet, opt), opt.encoding, opt.writeBOM);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }
};

// ============================================================================
// ULTRACANVASSPREADSHEET FILE I/O METHODS
// ============================================================================

bool IsSpreadsheetTemplateExtension(const std::string& extensionOrPath) {
    std::string ext = extensionOrPath;
    const size_t dot = ext.find_last_of('.');
    const size_t slash = ext.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ext = ext.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "ots" || ext == "xltx" || ext == "xltm";
}

bool UltraCanvasSpreadsheet::LoadFromFile(const std::string& filePath, const std::string& password) {
    lastError_.clear();

    // Report file-level problems (missing / locked / no permission) up front so
    // the message is meaningful even before we know the format.
    std::string openError = CSVDescribeOpenError(filePath);
    if (!openError.empty()) {
        lastError_ = openError;
        return false;
    }

    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos) {
        lastError_ = "The file has no extension, so its format is unknown: " + filePath;
        return false;
    }
    std::string ext = filePath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".ods" || ext == ".ots") {
        return LoadODS(filePath, password);
    } else if (ext == ".xlsx" || ext == ".xltx" || ext == ".xltm") {
        return LoadXLSX(filePath, password);
    } else if (ext == ".xls") {
        return LoadXLS(filePath, password);
    } else if (ext == ".csv") {
        return LoadCSV(filePath);
    } else if (ext == ".tsv") {
        return LoadCSV(filePath, 0);  // TSV uses tab delimiter
    }

    lastError_ = "Unsupported file type '" + ext +
                 "'. Supported formats are .ods, .xlsx, .xls, .csv and .tsv (and the templates .ots, .xltx): " +
                 filePath;
    return false;
}

bool UltraCanvasSpreadsheet::SaveToFile(const std::string& filePath, const std::string& password) {
    lastError_.clear();

    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos) {
        lastError_ = "The file has no extension, so the save format is unknown: " + filePath;
        return false;
    }
    std::string ext = filePath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Only an .xlsx can carry a password; saving another format without it
    // would drop the protection without a word.
    if (!password.empty() && ext != ".xlsx" && ext != ".xltx") {
        lastError_ = "Only an Excel workbook (.xlsx, .xltx) can be saved with a password: " + filePath;
        return false;
    }
    // A template is written as its workbook is, under the template's media
    // type (SaveODS / SaveXLSX read the extension).
    if (ext == ".ods" || ext == ".ots") {
        return SaveODS(filePath);
    } else if (ext == ".xlsx" || ext == ".xltx") {
        return SaveXLSX(filePath, password);
    } else if (ext == ".xltm") {
        lastError_ = "Excel macro-enabled templates (.xltm) are read, not written; save as .xltx instead: " +
                     filePath;
        return false;
    } else if (ext == ".csv") {
        return SaveCSV(filePath);
    } else if (ext == ".tsv") {
        CSVExportOptions tsv;
        tsv.fieldSeparator = '\t';
        return SaveCSVWithOptions(filePath, tsv);  // Current sheet, tab delimited
    } else if (ext == ".xls") {
        lastError_ = "Saving as Excel 97-2003 (.xls) is not supported; "
                     "save as .xlsx or .ods instead: " + filePath;
        return false;
    }

    lastError_ = "Unsupported save format '" + ext +
                 "'. Supported formats are .ods, .xlsx, .csv and .tsv (and the templates .ots, .xltx): " +
                 filePath;
    return false;
}

bool UltraCanvasSpreadsheet::LoadODS(const std::string& filePath, const std::string& password) {
    UndoRecordingPause pause(*this);   // a new document, not edits to undo
    lastError_.clear();
    ODSLoader loader(this);
    bool result = loader.Load(filePath, password);
    if (result) {
        ClearUndoHistory();   // the loaded document starts a fresh history
        Recalculate();
        // Columns the document did not size get fitted to their content on the
        // next render, once text can be measured with real font metrics.
        RequestAutoFitForUnsizedColumns();
        Invalidate();
    } else {
        lastError_ = loader.GetError();
        if (lastError_.empty()) lastError_ = "Could not read the spreadsheet: " + filePath;
    }
    return result;
}

bool UltraCanvasSpreadsheet::SaveODS(const std::string& filePath) {
    lastError_.clear();
    RefreshChangedPivotTables();   // the pivots' cells as their sources are now
    ODSSaver saver(this);
    if (!saver.Save(filePath)) {
        std::string writeError = DescribeFileWriteError(filePath);
        lastError_ = writeError.empty()
            ? ("Could not write the spreadsheet: " + filePath)
            : writeError;
        return false;
    }
    return true;
}

// LoadXLSX / SaveXLSX are implemented in UltraCanvasSpreadsheetXlsxIO.cpp.

SpreadsheetSheet* UltraCanvasSpreadsheet::PrepareCSVSheet(int sheetIndex) {
    SpreadsheetSheet* sheet = nullptr;
    if (sheetIndex < 0 || sheetIndex >= GetSheetCount()) {
        // Clear and use a fresh first sheet.
        while (GetSheetCount() > 0) {
            RemoveSheet(0);
        }
        sheet = AddSheet("Sheet1");
    } else {
        sheet = GetSheet(sheetIndex);
        if (sheet) {
            sheet->ClearAll();
        }
    }
    return sheet;
}

bool UltraCanvasSpreadsheet::LoadCSV(const std::string& filePath, int sheetIndex) {
    UndoRecordingPause pause(*this);   // a new document, not edits to undo
    lastError_.clear();
    SpreadsheetSheet* sheet = PrepareCSVSheet(sheetIndex);
    if (!sheet) { lastError_ = "Could not create a sheet to import into."; return false; }

    // Auto-detect encoding/separators/decimal so European (semicolon) files and
    // tab-separated files load into proper columns instead of one cell.
    bool result = CSVLoader::Load(filePath, sheet, CSVImportOptions{}, /*detect*/true, &lastError_);
    if (result) {
        SetActiveSheet(0);
        ClearUndoHistory();   // the loaded document starts a fresh history
        Recalculate();
        // A CSV carries no column widths at all, so every column is fitted.
        RequestAutoFitForUnsizedColumns();
        Invalidate();
    }
    return result;
}

bool UltraCanvasSpreadsheet::LoadCSVWithOptions(const std::string& filePath,
                                                const CSVImportOptions& options,
                                                int sheetIndex) {
    UndoRecordingPause pause(*this);   // a new document, not edits to undo
    lastError_.clear();
    SpreadsheetSheet* sheet = PrepareCSVSheet(sheetIndex);
    if (!sheet) { lastError_ = "Could not create a sheet to import into."; return false; }

    bool result = CSVLoader::Load(filePath, sheet, options, /*detect*/false, &lastError_);
    if (result) {
        SetActiveSheet(0);
        ClearUndoHistory();   // the loaded document starts a fresh history
        Recalculate();
        // A CSV carries no column widths at all, so every column is fitted.
        RequestAutoFitForUnsizedColumns();
        Invalidate();
    }
    return result;
}

bool UltraCanvasSpreadsheet::SaveCSV(const std::string& filePath, int sheetIndex) {
    // Default comma-separated export; SaveCSVWithOptions handles the rest.
    return SaveCSVWithOptions(filePath, CSVExportOptions{}, sheetIndex);
}

bool UltraCanvasSpreadsheet::SaveCSVWithOptions(const std::string& filePath,
                                                const CSVExportOptions& options,
                                                int sheetIndex) {
    lastError_.clear();
    const SpreadsheetSheet* sheet =
        (sheetIndex < 0) ? GetActiveSheet() : GetSheet(sheetIndex);

    if (!sheet) { lastError_ = "There is no sheet to save."; return false; }

    if (!CSVLoader::Save(filePath, sheet, options)) {
        std::string writeError = DescribeFileWriteError(filePath);
        lastError_ = writeError.empty()
            ? ("Could not write the file: " + filePath)
            : writeError;
        return false;
    }
    return true;
}

std::string UltraCanvasSpreadsheet::ExportCSVToString(const CSVExportOptions& options,
                                                      int sheetIndex) const {
    const SpreadsheetSheet* sheet =
        (sheetIndex < 0) ? GetActiveSheet() : GetSheet(sheetIndex);
    if (!sheet) return std::string();
    return CSVLoader::Build(sheet, options);
}

} // namespace UltraCanvas
