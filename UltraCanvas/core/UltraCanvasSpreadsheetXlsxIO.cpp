// core/UltraCanvasSpreadsheetXlsxIO.cpp
// Excel 2007+ (.xlsx, SpreadsheetML/OPC) load and save for
// UltraCanvasSpreadsheet — implements the LoadXLSX/SaveXLSX entry points
// declared in UltraCanvasSpreadsheet.h. Mirrors the ODS loader/saver in
// UltraCanvasSpreadsheetFileIO.cpp: multi-sheet, typed values (numbers,
// text via shared strings, booleans, errors, dates/times/percentages/
// currency mapped through number formats), formulas, merged cells, column
// widths/row heights and the same CellStyle subset (font, solid fill,
// borders, alignment, wrap).
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasZipPackage.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
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

// ===== NUMBER FORMAT CLASSIFICATION =====

// Deduces the value category from a numFmt id or format code. Excel stores
// dates/times/percentages as plain numbers — only the format reveals them.
NumberFormatCategory CategoryForNumFmt(int numFmtId, const std::string& formatCode) {
    if ((numFmtId >= 14 && numFmtId <= 17)) return NumberFormatCategory::Date;
    if (numFmtId == 22) return NumberFormatCategory::DateTime;
    if (numFmtId >= 18 && numFmtId <= 21) return NumberFormatCategory::Time;
    if (numFmtId >= 45 && numFmtId <= 47) return NumberFormatCategory::Time;
    if (numFmtId == 9 || numFmtId == 10) return NumberFormatCategory::Percentage;
    if (numFmtId == 11 || numFmtId == 48) return NumberFormatCategory::Scientific;
    if (numFmtId == 49) return NumberFormatCategory::Text;
    if ((numFmtId >= 5 && numFmtId <= 8) || numFmtId == 42 || numFmtId == 44) {
        return NumberFormatCategory::Currency;
    }
    if (numFmtId >= 1 && numFmtId <= 4) return NumberFormatCategory::Number;
    if (numFmtId >= 37 && numFmtId <= 40) return NumberFormatCategory::Number;

    if (!formatCode.empty()) {
        // Custom code: strip quoted literals, then look for telltale tokens.
        std::string code;
        bool inQuote = false;
        for (char c : formatCode) {
            if (c == '"') inQuote = !inQuote;
            else if (!inQuote) code.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(c))));
        }
        if (code.find('%') != std::string::npos) return NumberFormatCategory::Percentage;
        if (formatCode.find("[$") != std::string::npos
            || formatCode.find_first_of("$\xE2\xC2") != std::string::npos) {
            // "[$..]" locale currency, '$', or a UTF-8 currency symbol lead byte
            if (code.find('0') != std::string::npos) return NumberFormatCategory::Currency;
        }
        bool hasDate = code.find('y') != std::string::npos
                       || code.find('d') != std::string::npos;
        bool hasTime = code.find('h') != std::string::npos
                       || code.find('s') != std::string::npos;
        if (hasDate && hasTime) return NumberFormatCategory::DateTime;
        if (hasDate) return NumberFormatCategory::Date;
        if (hasTime) return NumberFormatCategory::Time;
        if (code.find('e') != std::string::npos
            && code.find('0') != std::string::npos) return NumberFormatCategory::Scientific;
        if (code.find('0') != std::string::npos
            || code.find('#') != std::string::npos) return NumberFormatCategory::Number;
    }
    return NumberFormatCategory::General;
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
    if (name.empty() || name == "none") return BorderStyle::None;
    return BorderStyle::Thin;
}

// Excel column width is in "characters" of the default font; the grid model
// stores pixels. ~7px per character matches the default Calibri 11 metric.
constexpr double kPixelsPerWidthChar = 7.0;
constexpr double kPixelsPerPoint = 1.33;

// ===== XLSX LOADER =====

class XlsxLoader {
public:
    explicit XlsxLoader(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {}

    bool Load(const std::string& filePath, std::string& error) {
        if (!zip_.Open(filePath)) {
            error = "The file is not a valid Excel workbook (.xlsx): " + filePath;
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
        for (auto* sheetElem = sheets->FirstChildElement("sheet"); sheetElem;
             sheetElem = sheetElem->NextSiblingElement("sheet")) {
            std::string name = Attr(sheetElem, "name");
            std::string relId = Attr(sheetElem, "r:id");
            auto rel = relationships_.find(relId);
            if (rel == relationships_.end()) continue;
            SpreadsheetSheet* sheet = spreadsheet_->AddSheet(name);
            if (!sheet) continue;
            std::string sheetXml;
            if (zip_.ReadEntry(rel->second, sheetXml)) {
                ParseWorksheet(sheetXml, sheet);
            }
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
            NumberFormatCategory category = CategoryForNumFmt(numFmtId, formatCode);
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

        if (auto* cols = root->FirstChildElement("cols")) {
            for (auto* col = cols->FirstChildElement("col"); col;
                 col = col->NextSiblingElement("col")) {
                int min = col->IntAttribute("min", 1) - 1;
                int max = col->IntAttribute("max", 1) - 1;
                double width = col->DoubleAttribute("width", 0);
                if (width <= 0) continue;
                int pixels = static_cast<int>(width * kPixelsPerWidthChar + 0.5);
                for (int c = min; c <= max && c < SpreadsheetLimits::MaxColumns; ++c) {
                    sheet->SetColumnWidth(c, pixels);
                }
            }
        }

        if (auto* sheetData = root->FirstChildElement("sheetData")) {
            int rowIndex = 0;
            for (auto* row = sheetData->FirstChildElement("row"); row;
                 row = row->NextSiblingElement("row")) {
                rowIndex = row->IntAttribute("r", rowIndex + 1) - 1;
                double height = row->DoubleAttribute("ht", 0);
                if (height > 0 && row->BoolAttribute("customHeight", false)) {
                    sheet->SetRowHeight(rowIndex,
                                        static_cast<int>(height * kPixelsPerPoint + 0.5));
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
            std::string formula = f->GetText();
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
};

// ===== XLSX SAVER =====

class XlsxSaver {
public:
    explicit XlsxSaver(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {}

    bool Save(const std::string& filePath, std::string& error) {
        // Sheets are generated first so the style/string tables are complete
        // before styles.xml and sharedStrings.xml are written.
        std::vector<std::string> sheetXmls;
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
            if (sheet) sheetXmls.push_back(BuildWorksheetXml(sheet));
        }
        if (sheetXmls.empty()) {
            error = "The spreadsheet has no sheets to save";
            return false;
        }

        if (!zip_.Open(filePath)) {
            error = "Cannot create file: " + filePath;
            return false;
        }
        bool ok = zip_.AddEntry("[Content_Types].xml", BuildContentTypesXml(sheetXmls.size()))
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
        if (!ok || !zip_.Finalize()) {
            error = "Failed to write workbook package: " + zip_.GetLastError();
            return false;
        }
        return true;
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

    std::string BuildWorksheetXml(const SpreadsheetSheet* sheet) {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

        CellRange used = sheet->GetUsedRange();

        std::ostringstream cols;
        for (int col = 0; col <= used.end.col; ++col) {
            int width = sheet->GetColumnWidth(col);
            if (width != SpreadsheetLimits::DefaultColumnWidth) {
                cols << "<col min=\"" << (col + 1) << "\" max=\"" << (col + 1)
                     << "\" width=\"" << (width / kPixelsPerWidthChar)
                     << "\" customWidth=\"1\"/>";
            }
        }
        std::string colsStr = cols.str();
        if (!colsStr.empty()) xml << "<cols>" << colsStr << "</cols>\n";

        xml << "<sheetData>\n";
        for (int row = 0; row <= used.end.row; ++row) {
            std::ostringstream cells;
            for (int col = 0; col <= used.end.col; ++col) {
                const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);
                if (cell && (!cell->IsEmpty() || cell->HasCustomStyle())) {
                    WriteCell(cells, *cell, row, col);
                }
            }
            std::string cellsStr = cells.str();
            int height = sheet->GetRowHeight(row);
            bool customHeight = height != SpreadsheetLimits::DefaultRowHeight;
            if (cellsStr.empty() && !customHeight) continue;
            xml << "<row r=\"" << (row + 1) << "\"";
            if (customHeight) {
                xml << " ht=\"" << (height / kPixelsPerPoint) << "\" customHeight=\"1\"";
            }
            xml << ">" << cellsStr << "</row>\n";
        }
        xml << "</sheetData>\n";

        const auto& merges = sheet->GetMergedCells();
        if (!merges.empty()) {
            xml << "<mergeCells count=\"" << merges.size() << "\">";
            for (const auto& merge : merges) {
                xml << "<mergeCell ref=\"" << CellRef(merge.range.start.row, merge.range.start.col)
                    << ":" << CellRef(merge.range.end.row, merge.range.end.col) << "\"/>";
            }
            xml << "</mergeCells>\n";
        }
        xml << "</worksheet>\n";
        return xml.str();
    }

    void WriteCell(std::ostringstream& xml, const SpreadsheetCell& cell, int row, int col) {
        xml << "<c r=\"" << CellRef(row, col) << "\"";
        int xfIndex = XfIndexForCell(cell);
        if (xfIndex > 0) xml << " s=\"" << xfIndex << "\"";

        std::string formula;
        if (cell.HasFormula()) {
            formula = cell.GetFormulaText();
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
                xml << "<v>" << cell.GetCurrency().amount << "</v></c>";
                return;
            }
            case CellValueType::Empty:
            case CellValueType::Formula: {
                xml << ">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                auto number = cell.TryGetNumber();
                if (number) xml << "<v>" << *number << "</v>";
                xml << "</c>";
                return;
            }
            default:
                // Number, Date, Time, DateTime, Percentage: serial value; the
                // xf's numFmt communicates the presentation.
                xml << ">";
                if (!formula.empty()) xml << "<f>" << XmlEscape(formula) << "</f>";
                xml << "<v>" << cell.GetNumber() << "</v></c>";
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
            else if (font.underline != UnderlineStyle::None) fonts << "<u/>";
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
        xml << "</sheets></workbook>\n";
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

    static std::string BuildContentTypesXml(size_t sheetCount) {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
            << "<Default Extension=\"rels\" "
               "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
            << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
            << "<Override PartName=\"/xl/workbook.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
               "spreadsheetml.sheet.main+xml\"/>\n";
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
};

} // namespace

// ===== SPREADSHEET ENTRY POINTS =====

bool UltraCanvasSpreadsheet::LoadXLSX(const std::string& filePath) {
    lastError_.clear();
    XlsxLoader loader(this);
    if (!loader.Load(filePath, lastError_)) {
        return false;
    }
    Recalculate();
    Invalidate();
    return true;
}

bool UltraCanvasSpreadsheet::SaveXLSX(const std::string& filePath) {
    lastError_.clear();
    XlsxSaver saver(this);
    return saver.Save(filePath, lastError_);
}

} // namespace UltraCanvas
