// core/UltraCanvasSpreadsheetFileIO.cpp
// Spreadsheet file I/O implementation (ODS, XLSX, CSV)
// Version: 1.0.0
// Last Modified: 2026-01-09
// Author: UltraCanvas Framework

#include <stdexcept>   // predeclare std::runtime_error for libspecific/Cairo/ImageCairo.h
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetCell.h"
#include "UltraCanvasSpreadsheetTypes.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>

// For ZIP handling (ODS/XLSX are ZIP archives)
// Using miniz for lightweight ZIP support (vendored: third_party/miniz)
#include "miniz.h"

// For XML parsing
#include "tinyxml2.h"

namespace UltraCanvas {

// ============================================================================
// ODS FORMAT CONSTANTS
// ============================================================================

namespace ODS {
    constexpr const char* MIMETYPE = "application/vnd.oasis.opendocument.spreadsheet";
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
            hours = std::stod(timePart.substr(0, hPos));
        }
        if (mPos != std::string::npos) {
            size_t start = (hPos != std::string::npos) ? hPos + 1 : 0;
            minutes = std::stod(timePart.substr(start, mPos - start));
        }
        if (sPos != std::string::npos) {
            size_t start = (mPos != std::string::npos) ? mPos + 1 : ((hPos != std::string::npos) ? hPos + 1 : 0);
            seconds = std::stod(timePart.substr(start, sPos - start));
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
    return BorderStyle::None;
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
    return val ? std::stod(val) : defaultVal;
}

} // anonymous namespace

// ============================================================================
// ODS LOADER
// ============================================================================

class ODSLoader {
private:
    UltraCanvasSpreadsheet* spreadsheet_;
    mz_zip_archive zip_;
    std::map<std::string, CellStyle> styles_;
    std::string error_;

public:
    ODSLoader(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {
        memset(&zip_, 0, sizeof(zip_));
    }

    ~ODSLoader() {
        mz_zip_reader_end(&zip_);
    }

    const std::string& GetError() const { return error_; }

    bool Load(const std::string& filePath) {
        // Open ZIP archive. ODS files are ZIP containers, so a failure here is
        // either a file-access problem (locked / no permission / missing) or the
        // file simply is not a valid OpenDocument archive — tell them which.
        if (!mz_zip_reader_init_file(&zip_, filePath.c_str(), 0)) {
            std::string openError = CSVDescribeOpenError(filePath);
            error_ = openError.empty()
                ? "The file is not a valid OpenDocument spreadsheet (.ods): " + filePath
                : openError;
            return false;
        }

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
        
        auto* officeStyles = root->FirstChildElement("office:styles");
        if (officeStyles) {
            ParseStyleElements(officeStyles);
        }
        
        auto* automaticStyles = root->FirstChildElement("office:automatic-styles");
        if (automaticStyles) {
            ParseStyleElements(automaticStyles);
        }
    }
    
    void ParseContentStyles(const std::string& xml) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        
        auto* root = doc.RootElement();
        if (!root) return;
        
        auto* automaticStyles = root->FirstChildElement("office:automatic-styles");
        if (automaticStyles) {
            ParseStyleElements(automaticStyles);
        }
    }
    
    void ParseStyleElements(tinyxml2::XMLElement* stylesElem) {
        for (auto* style = stylesElem->FirstChildElement("style:style"); 
             style; 
             style = style->NextSiblingElement("style:style")) {
            
            std::string styleName = GetAttr(style, "style:name");
            std::string family = GetAttr(style, "style:family");
            
            if (family == "table-cell" && !styleName.empty()) {
                CellStyle cellStyle;
                
                // Parse text properties
                auto* textProps = style->FirstChildElement("style:text-properties");
                if (textProps) {
                    cellStyle.font.family = GetAttr(textProps, "style:font-name", "Arial");
                    std::string fontSize = GetAttr(textProps, "fo:font-size");
                    if (!fontSize.empty()) {
                        cellStyle.font.size = std::stof(fontSize);
                    }
                    cellStyle.font.bold = (GetAttr(textProps, "fo:font-weight") == "bold");
                    cellStyle.font.italic = (GetAttr(textProps, "fo:font-style") == "italic");
                    std::string color = GetAttr(textProps, "fo:color");
                    if (!color.empty()) {
                        cellStyle.font.color = ParseHexColor(color);
                    }
                }
                
                // Parse table-cell properties
                auto* cellProps = style->FirstChildElement("style:table-cell-properties");
                if (cellProps) {
                    std::string bgColor = GetAttr(cellProps, "fo:background-color");
                    if (!bgColor.empty() && bgColor != "transparent") {
                        cellStyle.fill.pattern = FillPattern::Solid;
                        cellStyle.fill.foregroundColor = ParseHexColor(bgColor);
                    }
                    
                    // Borders
                    std::string border = GetAttr(cellProps, "fo:border");
                    if (!border.empty() && border != "none") {
                        // Parse border: 0.002cm solid #000000
                        CellBorder b;
                        b.style = BorderStyle::Thin;
                        b.color = Colors::Black;
                        cellStyle.borders.left = cellStyle.borders.right = 
                        cellStyle.borders.top = cellStyle.borders.bottom = b;
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
                
                styles_[styleName] = cellStyle;
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
        
        // Clear existing sheets
        while (spreadsheet_->GetSheetCount() > 0) {
            spreadsheet_->RemoveSheet(0);
        }
        
        // Parse each table:table (sheet)
        int sheetIndex = 0;
        for (auto* tableElem = spreadsheetElem->FirstChildElement("table:table");
             tableElem;
             tableElem = tableElem->NextSiblingElement("table:table")) {
            
            std::string sheetName = GetAttr(tableElem, "table:name", "Sheet" + std::to_string(sheetIndex + 1));
            
            SpreadsheetSheet* sheet;
            if (sheetIndex == 0) {
                sheet = spreadsheet_->AddSheet(sheetName);
            } else {
                sheet = spreadsheet_->AddSheet(sheetName);
            }
            
            if (sheet) {
                ParseTable(tableElem, sheet);
            }
            
            ++sheetIndex;
        }
        
        // Ensure at least one sheet exists
        if (spreadsheet_->GetSheetCount() == 0) {
            spreadsheet_->AddSheet("Sheet1");
        }
        
        spreadsheet_->SetActiveSheet(0);
        return true;
    }
    
    void ParseTable(tinyxml2::XMLElement* tableElem, SpreadsheetSheet* sheet) {
        // Parse column definitions
        int colIndex = 0;
        for (auto* colElem = tableElem->FirstChildElement("table:table-column");
             colElem;
             colElem = colElem->NextSiblingElement("table:table-column")) {
            
            int repeat = GetAttrInt(colElem, "table:number-columns-repeated", 1);
            std::string widthStr = GetAttr(colElem, "style:column-width");
            
            int width = SpreadsheetLimits::DefaultColumnWidth;
            if (!widthStr.empty()) {
                // Parse width (e.g., "2.5cm", "1in", "72pt")
                double val = std::stod(widthStr);
                if (widthStr.find("cm") != std::string::npos) {
                    width = static_cast<int>(val * 37.8);  // cm to pixels approx
                } else if (widthStr.find("in") != std::string::npos) {
                    width = static_cast<int>(val * 96);
                } else if (widthStr.find("pt") != std::string::npos) {
                    width = static_cast<int>(val * 1.33);
                }
            }
            
            for (int i = 0; i < repeat && colIndex < SpreadsheetLimits::MaxColumns; ++i) {
                if (width != SpreadsheetLimits::DefaultColumnWidth) {
                    sheet->SetColumnWidth(colIndex, width);
                }
                ++colIndex;
            }
        }
        
        // Parse rows
        int rowIndex = 0;
        for (auto* rowElem = tableElem->FirstChildElement("table:table-row");
             rowElem;
             rowElem = rowElem->NextSiblingElement("table:table-row")) {
            
            int rowRepeat = GetAttrInt(rowElem, "table:number-rows-repeated", 1);
            
            // Skip large empty row blocks
            if (rowRepeat > 100) {
                // Check if row has any cells
                auto* firstCell = rowElem->FirstChildElement("table:table-cell");
                if (!firstCell || GetAttr(firstCell, "office:value-type").empty()) {
                    rowIndex += rowRepeat;
                    continue;
                }
            }
            
            for (int r = 0; r < rowRepeat && rowIndex < SpreadsheetLimits::MaxRows; ++r) {
                ParseRow(rowElem, sheet, rowIndex);
                ++rowIndex;
            }
        }
    }
    
    void ParseRow(tinyxml2::XMLElement* rowElem, SpreadsheetSheet* sheet, int rowIndex) {
        // Parse row height
        std::string heightStr = GetAttr(rowElem, "style:row-height");
        if (!heightStr.empty()) {
            double val = std::stod(heightStr);
            int height = SpreadsheetLimits::DefaultRowHeight;
            if (heightStr.find("cm") != std::string::npos) {
                height = static_cast<int>(val * 37.8);
            } else if (heightStr.find("pt") != std::string::npos) {
                height = static_cast<int>(val * 1.33);
            }
            if (height != SpreadsheetLimits::DefaultRowHeight) {
                sheet->SetRowHeight(rowIndex, height);
            }
        }
        
        // Parse cells
        int colIndex = 0;
        for (auto* cellElem = rowElem->FirstChildElement("table:table-cell");
             cellElem;
             cellElem = cellElem->NextSiblingElement("table:table-cell")) {
            
            int colRepeat = GetAttrInt(cellElem, "table:number-columns-repeated", 1);
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
                    
                    // Handle merged cells
                    if (colSpan > 1 || rowSpan > 1) {
                        sheet->MergeCells(rowIndex, colIndex, 
                                         rowIndex + rowSpan - 1, colIndex + colSpan - 1);
                    }
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
        if (!formula.empty()) {
            // ODS formulas start with "of:=" or namespace prefix
            if (formula.find("of:=") == 0) {
                formula = formula.substr(3);  // Remove "of:"
            } else if (formula[0] != '=') {
                formula = "=" + formula;
            }
            cell->SetFormula(formula);
        }
        
        // Parse value based on type
        if (valueType == "float" || valueType == "currency" || valueType == "percentage") {
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
            // Get text content from text:p element
            auto* textElem = cellElem->FirstChildElement("text:p");
            if (textElem) {
                const char* text = textElem->GetText();
                cell->SetText(text ? text : "");
            }
        }
        else if (formula.empty()) {
            // Check for text content without value-type
            auto* textElem = cellElem->FirstChildElement("text:p");
            if (textElem) {
                const char* text = textElem->GetText();
                if (text && text[0]) {
                    cell->SetValueFromString(text);
                }
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
};

// ============================================================================
// ODS SAVER
// ============================================================================

class ODSSaver {
private:
    UltraCanvasSpreadsheet* spreadsheet_;
    mz_zip_archive zip_;
    std::map<std::string, std::string> styleMap_;
    int styleCounter_ = 0;
    
public:
    ODSSaver(UltraCanvasSpreadsheet* spreadsheet) : spreadsheet_(spreadsheet) {
        memset(&zip_, 0, sizeof(zip_));
    }
    
    ~ODSSaver() {
        mz_zip_writer_end(&zip_);
    }
    
    bool Save(const std::string& filePath) {
        // Initialize ZIP writer
        if (!mz_zip_writer_init_file(&zip_, filePath.c_str(), 0)) {
            return false;
        }
        
        // Write mimetype (must be first, uncompressed)
        std::string mimetype = ODS::MIMETYPE;
        if (!mz_zip_writer_add_mem(&zip_, "mimetype", mimetype.c_str(), mimetype.size(), 
                                    MZ_NO_COMPRESSION)) {
            return false;
        }
        
        // Generate styles
        std::string stylesXml = GenerateStylesXml();
        if (!mz_zip_writer_add_mem(&zip_, ODS::STYLES_XML, stylesXml.c_str(), stylesXml.size(),
                                    MZ_DEFAULT_COMPRESSION)) {
            return false;
        }
        
        // Generate content
        std::string contentXml = GenerateContentXml();
        if (!mz_zip_writer_add_mem(&zip_, ODS::CONTENT_XML, contentXml.c_str(), contentXml.size(),
                                    MZ_DEFAULT_COMPRESSION)) {
            return false;
        }
        
        // Generate meta.xml
        std::string metaXml = GenerateMetaXml();
        if (!mz_zip_writer_add_mem(&zip_, ODS::META_XML, metaXml.c_str(), metaXml.size(),
                                    MZ_DEFAULT_COMPRESSION)) {
            return false;
        }
        
        // Generate manifest
        std::string manifestXml = GenerateManifestXml();
        if (!mz_zip_writer_add_mem(&zip_, ODS::MANIFEST_XML, manifestXml.c_str(), manifestXml.size(),
                                    MZ_DEFAULT_COMPRESSION)) {
            return false;
        }
        
        // Finalize ZIP
        if (!mz_zip_writer_finalize_archive(&zip_)) {
            return false;
        }
        
        return true;
    }
    
private:
    std::string GenerateStylesXml() {
        std::stringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<office:document-styles "
           << "xmlns:office=\"" << ODS::NS_OFFICE << "\" "
           << "xmlns:style=\"" << ODS::NS_STYLE << "\" "
           << "xmlns:fo=\"" << ODS::NS_FO << "\" "
           << "office:version=\"1.3\">\n";
        ss << "<office:styles>\n";
        ss << "</office:styles>\n";
        ss << "</office:document-styles>\n";
        return ss.str();
    }
    
    std::string GenerateContentXml() {
        std::stringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<office:document-content "
           << "xmlns:office=\"" << ODS::NS_OFFICE << "\" "
           << "xmlns:table=\"" << ODS::NS_TABLE << "\" "
           << "xmlns:text=\"" << ODS::NS_TEXT << "\" "
           << "xmlns:style=\"" << ODS::NS_STYLE << "\" "
           << "xmlns:fo=\"" << ODS::NS_FO << "\" "
           << "xmlns:number=\"" << ODS::NS_NUMBER << "\" "
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
        
        // Write collected styles
        for (const auto& [styleKey, styleName] : styleMap_) {
            WriteStyleDefinition(ss, styleName, styleKey);
        }
        
        ss << "</office:automatic-styles>\n";
        
        // Body
        ss << "<office:body>\n";
        ss << "<office:spreadsheet>\n";
        
        // Write each sheet
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            auto* sheet = spreadsheet_->GetSheet(s);
            if (sheet) {
                WriteSheet(ss, sheet);
            }
        }
        
        ss << "</office:spreadsheet>\n";
        ss << "</office:body>\n";
        ss << "</office:document-content>\n";
        
        return ss.str();
    }
    
    void WriteSheet(std::stringstream& ss, const SpreadsheetSheet* sheet) {
        ss << "<table:table table:name=\"" << EscapeXml(sheet->GetName()) << "\">\n";
        
        // Write columns
        CellRange used = sheet->GetUsedRange();
        for (int col = 0; col <= used.end.col; ++col) {
            int width = sheet->GetColumnWidth(col);
            ss << "<table:table-column";
            if (width != SpreadsheetLimits::DefaultColumnWidth) {
                ss << " style:column-width=\"" << (width / 37.8) << "cm\"";
            }
            ss << "/>\n";
        }
        
        // Write rows
        for (int row = 0; row <= used.end.row; ++row) {
            ss << "<table:table-row";
            int height = sheet->GetRowHeight(row);
            if (height != SpreadsheetLimits::DefaultRowHeight) {
                ss << " style:row-height=\"" << (height / 37.8) << "cm\"";
            }
            ss << ">\n";
            
            // Write cells
            for (int col = 0; col <= used.end.col; ++col) {
                const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);
                WriteCell(ss, cell, sheet, row, col);
            }
            
            ss << "</table:table-row>\n";
        }
        
        ss << "</table:table>\n";
    }
    
    void WriteCell(std::stringstream& ss, const SpreadsheetCell* cell, 
                   const SpreadsheetSheet* sheet, int row, int col) {
        ss << "<table:table-cell";
        
        // Check for merged cells
        const MergedCell* merge = sheet->GetMergedCell(row, col);
        if (merge && merge->IsTopLeft(row, col)) {
            int colSpan = merge->range.end.col - merge->range.start.col + 1;
            int rowSpan = merge->range.end.row - merge->range.start.row + 1;
            if (colSpan > 1) ss << " table:number-columns-spanned=\"" << colSpan << "\"";
            if (rowSpan > 1) ss << " table:number-rows-spanned=\"" << rowSpan << "\"";
        }
        
        if (!cell || cell->IsEmpty()) {
            ss << "/>\n";
            return;
        }
        
        // Style
        if (cell->HasCustomStyle()) {
            std::string styleName = GetOrCreateStyle(cell->GetStyle());
            ss << " table:style-name=\"" << styleName << "\"";
        }
        
        // Formula
        if (cell->HasFormula()) {
            std::string formula = cell->GetFormulaText();
            ss << " table:formula=\"of:" << EscapeXml(formula) << "\"";
        }
        
        // Value type and value
        switch (cell->GetValueType()) {
            case CellValueType::Number:
                ss << " office:value-type=\"float\"";
                ss << " office:value=\"" << cell->GetNumber() << "\"";
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
                ss << " office:value=\"" << cell->GetNumber() << "\"";
                break;
            case CellValueType::Currency: {
                auto currency = cell->GetCurrency();
                ss << " office:value-type=\"currency\"";
                ss << " office:currency=\"" << currency.currencyCode << "\"";
                ss << " office:value=\"" << currency.amount << "\"";
                break;
            }
            case CellValueType::Text:
            default:
                ss << " office:value-type=\"string\"";
                break;
        }
        
        ss << ">\n";
        
        // Text content
        ss << "<text:p>" << EscapeXml(cell->GetDisplayValue()) << "</text:p>\n";
        
        ss << "</table:table-cell>\n";
    }
    
    std::string GetOrCreateStyle(const CellStyle& style) {
        std::string key = GenerateStyleKey(style);
        auto it = styleMap_.find(key);
        if (it != styleMap_.end()) {
            return it->second;
        }
        
        std::string name = "ce" + std::to_string(++styleCounter_);
        styleMap_[key] = name;
        return name;
    }
    
    std::string GenerateStyleKey(const CellStyle& style) {
        std::stringstream ss;
        ss << style.font.family << "|" << style.font.size << "|"
           << style.font.bold << "|" << style.font.italic << "|"
           << ColorToHex(style.font.color) << "|"
           << static_cast<int>(style.fill.pattern) << "|"
           << ColorToHex(style.fill.foregroundColor) << "|"
           << static_cast<int>(style.hAlign) << "|"
           << static_cast<int>(style.vAlign);
        return ss.str();
    }
    
    void WriteStyleDefinition(std::stringstream& ss, const std::string& name, const std::string& key) {
        // Parse key back to style (simplified)
        ss << "<style:style style:name=\"" << name << "\" style:family=\"table-cell\">\n";
        ss << "</style:style>\n";
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
        ss << "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"" << ODS::MIMETYPE << "\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>\n";
        ss << "</manifest:manifest>\n";
        return ss.str();
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
        const char delimiter = opt.fieldSeparator;
        const char quote = opt.textDelimiter;
        const bool quoting = quote != 0;
        const std::string eol =
            (opt.lineEnding == CSVExportOptions::LineEnding::CRLF) ? "\r\n" : "\n";

        CellRange used = sheet->GetUsedRange();

        for (int row = used.start.row; row <= used.end.row; ++row) {
            for (int col = used.start.col; col <= used.end.col; ++col) {
                if (col > used.start.col) out += delimiter;

                const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);
                if (!cell) continue;
                std::string value = cell->GetDisplayValue();

                bool needsQuote = quoting && (opt.quoteAllFields ||
                    value.find(delimiter) != std::string::npos ||
                    value.find(quote) != std::string::npos ||
                    value.find('\n') != std::string::npos ||
                    value.find('\r') != std::string::npos);

                if (needsQuote) {
                    out += quote;
                    for (char c : value) {
                        if (c == quote) out += quote;  // double the quote to escape
                        out += c;
                    }
                    out += quote;
                } else {
                    out += value;
                }
            }
            out += eol;
        }

        return out;
    }

    static bool Save(const std::string& filePath, const SpreadsheetSheet* sheet,
                     const CSVExportOptions& opt) {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        std::string text = CSVEncodeFromUtf8(Build(sheet, opt), opt.encoding, opt.writeBOM);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }
};

// ============================================================================
// ULTRACANVASSPREADSHEET FILE I/O METHODS
// ============================================================================

bool UltraCanvasSpreadsheet::LoadFromFile(const std::string& filePath) {
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

    if (ext == ".ods") {
        return LoadODS(filePath);
    } else if (ext == ".xlsx") {
        return LoadXLSX(filePath);
    } else if (ext == ".csv") {
        return LoadCSV(filePath);
    } else if (ext == ".tsv") {
        return LoadCSV(filePath, 0);  // TSV uses tab delimiter
    }

    lastError_ = "Unsupported file type '" + ext +
                 "'. Supported formats are .ods, .csv and .tsv: " + filePath;
    return false;
}

bool UltraCanvasSpreadsheet::SaveToFile(const std::string& filePath) {
    lastError_.clear();

    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos) {
        lastError_ = "The file has no extension, so the save format is unknown: " + filePath;
        return false;
    }
    std::string ext = filePath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".ods") {
        return SaveODS(filePath);
    } else if (ext == ".xlsx") {
        return SaveXLSX(filePath);
    } else if (ext == ".csv") {
        return SaveCSV(filePath);
    } else if (ext == ".tsv") {
        CSVExportOptions tsv;
        tsv.fieldSeparator = '\t';
        return SaveCSVWithOptions(filePath, tsv);  // Current sheet, tab delimited
    }

    lastError_ = "Unsupported save format '" + ext +
                 "'. Supported formats are .ods, .csv and .tsv: " + filePath;
    return false;
}

bool UltraCanvasSpreadsheet::LoadODS(const std::string& filePath) {
    lastError_.clear();
    ODSLoader loader(this);
    bool result = loader.Load(filePath);
    if (result) {
        Recalculate();
        Invalidate();
    } else {
        lastError_ = loader.GetError();
        if (lastError_.empty()) lastError_ = "Could not read the spreadsheet: " + filePath;
    }
    return result;
}

bool UltraCanvasSpreadsheet::SaveODS(const std::string& filePath) {
    lastError_.clear();
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

bool UltraCanvasSpreadsheet::LoadXLSX(const std::string& filePath) {
    // XLSX format implementation would follow similar pattern
    // Using Office Open XML format (also ZIP-based with XML files)
    // For now, report a clear reason instead of a silent failure.
    lastError_ = "Excel (.xlsx) import is not supported yet. "
                 "Please convert the file to .ods or .csv: " + filePath;
    return false;
}

bool UltraCanvasSpreadsheet::SaveXLSX(const std::string& filePath) {
    // XLSX writing is not implemented yet; report a clear reason.
    lastError_ = "Saving to Excel (.xlsx) is not supported yet. "
                 "Please save as .ods or .csv: " + filePath;
    return false;
}

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
    lastError_.clear();
    SpreadsheetSheet* sheet = PrepareCSVSheet(sheetIndex);
    if (!sheet) { lastError_ = "Could not create a sheet to import into."; return false; }

    // Auto-detect encoding/separators/decimal so European (semicolon) files and
    // tab-separated files load into proper columns instead of one cell.
    bool result = CSVLoader::Load(filePath, sheet, CSVImportOptions{}, /*detect*/true, &lastError_);
    if (result) {
        SetActiveSheet(0);
        Recalculate();
        Invalidate();
    }
    return result;
}

bool UltraCanvasSpreadsheet::LoadCSVWithOptions(const std::string& filePath,
                                                const CSVImportOptions& options,
                                                int sheetIndex) {
    lastError_.clear();
    SpreadsheetSheet* sheet = PrepareCSVSheet(sheetIndex);
    if (!sheet) { lastError_ = "Could not create a sheet to import into."; return false; }

    bool result = CSVLoader::Load(filePath, sheet, options, /*detect*/false, &lastError_);
    if (result) {
        SetActiveSheet(0);
        Recalculate();
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
