// core/UltraCanvasSpreadsheetPivotIO.cpp
// A spreadsheet's pivot tables in its files (UltraCanvasSpreadsheetPivot.h).
//
// .xlsx: per pivot a cache definition (xl/pivotCache/pivotCacheDefinitionN.xml:
// the source sheet and block, a cache field per column with its distinct
// values and, for a grouped field, its fieldGroup), the cache's records
// (pivotCacheRecordsN.xml, each source row as indexes into those values) and
// a pivot table part (xl/pivotTables/pivotTableN.xml: where the table stands,
// each field's place, items, hidden items, order and subtotals, the value
// fields with their summary and show-as). The workbook lists the caches in
// <pivotCaches>; the worksheet the pivot stands on relates to its table, the
// table to its cache, the cache to its records. The cache is marked
// refreshOnLoad, so Excel computes the table again when it opens it; the
// cells the pivot wrote are in the worksheet as they are for any program
// that does not.
//
// .ods: a table:data-pilot-table per pivot in the workbook's
// table:data-pilot-tables - its target range, its source range
// (table:source-cell-range) and a table:data-pilot-field per field with its
// orientation, summary function, hidden members, sort order, subtotals and
// grouping (table:data-pilot-groups).
//
// Reading takes either back into the model. What the model cannot represent
// - a calculated field, a grouping that adds fields, a source outside the
// workbook - leaves the pivot with its cells as the file has them and the
// reason in SpreadsheetPivotTable::unsupportedReason.
//
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetPivot.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasZipPackage.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {

namespace {

const char* Attr(const tinyxml2::XMLElement* e, const char* name) {
    const char* value = e ? e->Attribute(name) : nullptr;
    return value ? value : "";
}

bool AttrTrue(const tinyxml2::XMLElement* e, const char* name, bool fallback) {
    const char* value = e ? e->Attribute(name) : nullptr;
    if (!value) return fallback;
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0;
}

double AttrNumber(const tinyxml2::XMLElement* e, const char* name, double fallback) {
    double value = fallback;
    const char* text = e ? e->Attribute(name) : nullptr;
    if (text && TryParseFloat(text, value)) return value;
    return fallback;
}

int AttrInt(const tinyxml2::XMLElement* e, const char* name, int fallback) {
    const char* text = e ? e->Attribute(name) : nullptr;
    if (!text || !*text) return fallback;
    return std::atoi(text);
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
            default: out += c;
        }
    }
    return out;
}

std::string ColumnLetters(int col) { return CellAddress::ColumnToLetter(col); }

std::string CellRef(int row, int col) { return ColumnLetters(col) + std::to_string(row + 1); }

std::string RangeRef(const CellRange& range) {
    return CellRef(range.start.row, range.start.col) + ":" + CellRef(range.end.row, range.end.col);
}

// "A1:E9" (or "$A$1:$E$9", or one cell) into a range.
bool ParseRangeRef(const std::string& text, CellRange& range) {
    auto corner = [](const std::string& part, int& row, int& col) {
        size_t i = 0;
        if (i < part.size() && part[i] == '$') ++i;
        col = 0;
        size_t letters = 0;
        while (i < part.size() && std::isalpha(static_cast<unsigned char>(part[i]))) {
            col = col * 26 + (std::toupper(static_cast<unsigned char>(part[i])) - 'A' + 1);
            ++i;
            ++letters;
        }
        if (i < part.size() && part[i] == '$') ++i;
        if (letters == 0 || i >= part.size()) return false;
        row = std::atoi(part.c_str() + i);
        --col;
        --row;
        return row >= 0 && col >= 0;
    };
    const size_t colon = text.find(':');
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
    if (!corner(text.substr(0, colon), r1, c1)) return false;
    if (colon == std::string::npos) {
        r2 = r1;
        c2 = c1;
    } else if (!corner(text.substr(colon + 1), r2, c2)) {
        return false;
    }
    range = CellRange(r1, c1, r2, c2);
    return true;
}

// The part a relationship's target names, from the folder of the part
// that relates to it ("xl/worksheets" + "../pivotTables/pivotTable1.xml").
std::string ResolvePart(const std::string& folder, const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::vector<std::string> parts;
    auto push = [&](const std::string& path) {
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const std::string piece = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (piece == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!piece.empty() && piece != ".") {
                parts.push_back(piece);
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
    };
    push(folder);
    push(target);
    std::string out;
    for (const std::string& piece : parts) out += (out.empty() ? "" : "/") + piece;
    return out;
}

// The relationships of `part`: Id -> (type, part path).
std::map<std::string, std::pair<std::string, std::string>> ReadRelationships(const UCZipPackageReader& package,
                                                                              const std::string& part) {
    std::map<std::string, std::pair<std::string, std::string>> rels;
    const size_t slash = part.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : part.substr(0, slash);
    const std::string file = slash == std::string::npos ? part : part.substr(slash + 1);
    std::string xml;
    if (!package.ReadEntry((folder.empty() ? "" : folder + "/") + "_rels/" + file + ".rels", xml)) return rels;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return rels;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("Relationships");
    for (const tinyxml2::XMLElement* rel = root ? root->FirstChildElement("Relationship") : nullptr; rel;
         rel = rel->NextSiblingElement("Relationship")) {
        rels[Attr(rel, "Id")] = {Attr(rel, "Type"), ResolvePart(folder, Attr(rel, "Target"))};
    }
    return rels;
}

bool EndsWith(const std::string& text, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

// Excel's name of a summary, and back.
const char* ExcelSubtotal(PivotSummary summary) {
    switch (summary) {
        case PivotSummary::Sum: return "sum";
        case PivotSummary::Count: return "count";
        case PivotSummary::Average: return "average";
        case PivotSummary::Max: return "max";
        case PivotSummary::Min: return "min";
        case PivotSummary::Product: return "product";
        case PivotSummary::CountNumbers: return "countNums";
        case PivotSummary::StdDev: return "stdDev";
        case PivotSummary::StdDevP: return "stdDevp";
        case PivotSummary::Var: return "var";
        case PivotSummary::VarP: return "varp";
    }
    return "sum";
}

bool SummaryFromExcel(const std::string& name, PivotSummary& summary) {
    static const std::map<std::string, PivotSummary> names = {
        {"sum", PivotSummary::Sum}, {"count", PivotSummary::Count}, {"average", PivotSummary::Average},
        {"max", PivotSummary::Max}, {"min", PivotSummary::Min}, {"product", PivotSummary::Product},
        {"countNums", PivotSummary::CountNumbers}, {"stdDev", PivotSummary::StdDev},
        {"stdDevp", PivotSummary::StdDevP}, {"var", PivotSummary::Var}, {"varp", PivotSummary::VarP}};
    if (name.empty()) { summary = PivotSummary::Sum; return true; }
    auto it = names.find(name);
    if (it == names.end()) return false;
    summary = it->second;
    return true;
}

// OpenDocument's.
const char* OdfFunction(PivotSummary summary) {
    switch (summary) {
        case PivotSummary::Sum: return "sum";
        case PivotSummary::Count: return "count";
        case PivotSummary::Average: return "average";
        case PivotSummary::Max: return "max";
        case PivotSummary::Min: return "min";
        case PivotSummary::Product: return "product";
        case PivotSummary::CountNumbers: return "countnums";
        case PivotSummary::StdDev: return "stdev";
        case PivotSummary::StdDevP: return "stdevp";
        case PivotSummary::Var: return "var";
        case PivotSummary::VarP: return "varp";
    }
    return "sum";
}

bool SummaryFromOdf(const std::string& name, PivotSummary& summary) {
    static const std::map<std::string, PivotSummary> names = {
        {"sum", PivotSummary::Sum}, {"auto", PivotSummary::Sum}, {"count", PivotSummary::Count},
        {"average", PivotSummary::Average}, {"max", PivotSummary::Max}, {"min", PivotSummary::Min},
        {"product", PivotSummary::Product}, {"countnums", PivotSummary::CountNumbers},
        {"stdev", PivotSummary::StdDev}, {"stdevp", PivotSummary::StdDevP}, {"var", PivotSummary::Var},
        {"varp", PivotSummary::VarP}};
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.empty()) { summary = PivotSummary::Sum; return true; }
    auto it = names.find(lower);
    if (it == names.end()) return false;
    summary = it->second;
    return true;
}

// A date serial as Excel writes it in a cache ("2024-01-05T00:00:00").
std::string ExcelDateText(double serial) {
    int year = 1900, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    DateTimeValue value(serial);
    value.ToDate(year, month, day);
    value.ToTime(hour, minute, second);
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, minute, second);
    return text;
}

double DateSerialFromExcel(const std::string& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 3) return 0.0;
    return DateTimeValue::FromDateTime(year, month, day, hour, minute, second).serialNumber;
}

const char* kMonthsInFile[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// A cache field's distinct values: what the cells hold, not as shown.
struct CacheValue {
    enum class Kind { Blank, Text, Number, Date, Boolean } kind = Kind::Blank;
    std::string text;
    double number = 0.0;
    bool operator<(const CacheValue& other) const {
        if (kind != other.kind) return kind < other.kind;
        if (kind == Kind::Text) return text < other.text;
        return number < other.number;
    }
};

CacheValue CellCacheValue(const SpreadsheetSheet& sheet, int row, int col) {
    CacheValue value;
    const SpreadsheetCell* cell = sheet.GetCellIfExists(row, col);
    if (!cell || cell->IsEmpty()) return value;
    const CellValueVariant& raw = cell->GetRawValue();
    if (const double* number = std::get_if<double>(&raw)) {
        const NumberFormatCategory category = cell->GetStyle().numberFormat.category;
        const CellValueType type = cell->GetValueType();
        const bool date = type == CellValueType::Date || type == CellValueType::DateTime ||
                          category == NumberFormatCategory::Date || category == NumberFormatCategory::DateTime;
        value.kind = date ? CacheValue::Kind::Date : CacheValue::Kind::Number;
        value.number = *number;
    } else if (const CurrencyValue* money = std::get_if<CurrencyValue>(&raw)) {
        value.kind = CacheValue::Kind::Number;
        value.number = money->amount;
    } else if (const bool* flag = std::get_if<bool>(&raw)) {
        value.kind = CacheValue::Kind::Boolean;
        value.number = *flag ? 1.0 : 0.0;
    } else {
        value.kind = CacheValue::Kind::Text;
        value.text = cell->GetDisplayValue();
    }
    return value;
}

// The key an item of this value has (PivotItem::key) without a grouping.
std::string KeyOfCacheValue(const CacheValue& value) {
    switch (value.kind) {
        case CacheValue::Kind::Blank: return std::string();
        case CacheValue::Kind::Text: return value.text;
        default: return FormatFloatRoundTrip(value.number);
    }
}

// The grouped items of a field as Excel lists them in groupItems: "<start",
// the groups, ">end"; and the index of each of our item keys there.
struct GroupItems {
    std::vector<std::string> labels;
    std::map<std::string, int> indexOfKey;
    std::string rangePr;   // the <rangePr .../> element
};

GroupItems MakeGroupItems(const PivotGrouping& grouping, const std::vector<PivotItem>& items, double low, double high) {
    GroupItems group;
    std::ostringstream range;
    switch (grouping.by) {
        case PivotGroupBy::Months:
        case PivotGroupBy::Quarters:
        case PivotGroupBy::Years: {
            const char* by = grouping.by == PivotGroupBy::Months ? "months"
                             : grouping.by == PivotGroupBy::Quarters ? "quarters" : "years";
            range << "<rangePr groupBy=\"" << by << "\" startDate=\"" << ExcelDateText(std::floor(low))
                  << "\" endDate=\"" << ExcelDateText(std::floor(high) + 1.0) << "\"/>";
            group.labels.push_back("<" + ExcelDateText(std::floor(low)).substr(0, 10));
            if (grouping.by == PivotGroupBy::Months) {
                for (int m = 1; m <= 12; ++m) {
                    group.indexOfKey[std::to_string(m)] = static_cast<int>(group.labels.size());
                    group.labels.push_back(kMonthsInFile[m - 1]);
                }
            } else if (grouping.by == PivotGroupBy::Quarters) {
                for (int q = 1; q <= 4; ++q) {
                    group.indexOfKey[std::to_string(q)] = static_cast<int>(group.labels.size());
                    group.labels.push_back("Qtr" + std::to_string(q));
                }
            } else {
                int y0 = 1900, y1 = 1900, m = 1, d = 1;
                DateTimeValue(low).ToDate(y0, m, d);
                DateTimeValue(high).ToDate(y1, m, d);
                for (int y = y0; y <= y1; ++y) {
                    group.indexOfKey[std::to_string(y)] = static_cast<int>(group.labels.size());
                    group.labels.push_back(std::to_string(y));
                }
            }
            group.labels.push_back(">" + ExcelDateText(std::floor(high) + 1.0).substr(0, 10));
            break;
        }
        case PivotGroupBy::NumberRange: {
            range << "<rangePr" << (grouping.autoStart ? "" : " autoStart=\"0\"")
                  << (grouping.autoEnd ? "" : " autoEnd=\"0\"") << " startNum=\"" << FormatFloatRoundTrip(grouping.start)
                  << "\" endNum=\"" << FormatFloatRoundTrip(grouping.end) << "\" groupInterval=\""
                  << FormatFloatRoundTrip(grouping.step) << "\"/>";
            group.indexOfKey["<"] = 0;
            group.labels.push_back("<" + FormatFloatClassic(grouping.start, 10));
            const int buckets = grouping.step > 0 ? static_cast<int>(std::floor((grouping.end - grouping.start) / grouping.step)) + 1 : 1;
            for (const PivotItem& item : items) {
                if (item.key == "<" || item.key == ">" || item.blank) continue;
                const int bucket = static_cast<int>(std::lround((item.sortNumber - grouping.start) / grouping.step));
                group.indexOfKey[item.key] = 1 + bucket;
            }
            const bool whole = std::floor(grouping.start) == grouping.start && std::floor(grouping.step) == grouping.step;
            for (int b = 0; b < buckets && b < 10000; ++b) {
                const double from = grouping.start + b * grouping.step;
                group.labels.push_back(FormatFloatClassic(from, 10) + "-" +
                                       FormatFloatClassic(whole ? from + grouping.step - 1 : from + grouping.step, 10));
            }
            group.indexOfKey[">"] = static_cast<int>(group.labels.size());
            group.labels.push_back(">" + FormatFloatClassic(grouping.end, 10));
            break;
        }
        case PivotGroupBy::NoGrouping:
            break;
    }
    group.rangePr = range.str();
    return group;
}

// A pivot's fields by source column: where each stands (row, column,
// filter), for the cache's and the table's lists.
struct FieldPlace {
    const PivotField* field = nullptr;
    const char* axis = nullptr;   // "axisRow", "axisCol", "axisPage"
    bool isData = false;
};

std::vector<FieldPlace> PlacesOf(const SpreadsheetPivotTable& pivot, int columns) {
    std::vector<FieldPlace> places(static_cast<size_t>(std::max(0, columns)));
    auto mark = [&](const std::vector<PivotField>& list, const char* axis) {
        for (const PivotField& f : list) {
            if (f.column >= 0 && f.column < columns && !places[static_cast<size_t>(f.column)].field) {
                places[static_cast<size_t>(f.column)].field = &f;
                places[static_cast<size_t>(f.column)].axis = axis;
            }
        }
    };
    mark(pivot.rowFields, "axisRow");
    mark(pivot.columnFields, "axisCol");
    mark(pivot.filterFields, "axisPage");
    for (const PivotValueField& v : pivot.valueFields) {
        if (v.column >= 0 && v.column < columns) places[static_cast<size_t>(v.column)].isData = true;
    }
    return places;
}

const char* kRelPivotCacheDefinition =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition";
const char* kRelPivotCacheRecords =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords";
const char* kRelPivotTable = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable";
const char* kMain = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const char* kRelNs = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

std::string RelationshipsXml(const std::string& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" +
           body + "</Relationships>\n";
}

std::string Relationship(const std::string& id, const char* type, const std::string& target) {
    return "<Relationship Id=\"" + id + "\" Type=\"" + type + "\" Target=\"" + XmlEscape(target) + "\"/>";
}

std::string Override(const std::string& part, const char* type) {
    return std::string("<Override PartName=\"/") + part + "\" ContentType=\"application/vnd.openxmlformats-officedocument." +
           type + "\"/>\n";
}

} // namespace

// ============================================================================
// .XLSX
// ============================================================================

namespace {

bool SameGrouping(const PivotGrouping& a, const PivotGrouping& b) {
    if (a.by != b.by) return false;
    if (a.by != PivotGroupBy::NumberRange) return true;
    return a.step == b.step && a.autoStart == b.autoStart && a.autoEnd == b.autoEnd &&
           (a.autoStart || a.start == b.start) && (a.autoEnd || a.end == b.end);
}

// One pivot cache: a source and, per column, the grouping the pivots using
// it give that column. Pivots over the same source share a cache unless
// they group a field differently - as Excel and LibreOffice keep them:
// LibreOffice takes one cache per source and the groupings of the first.
struct XlsxCache {
    const SpreadsheetSheet* source = nullptr;
    std::string sourceSheet;
    std::string sourceName;
    CellRange range;
    std::vector<PivotGrouping> groupings;      // per column
    std::vector<bool> fixed;                   // the column is an axis field of a pivot using the cache
    std::vector<std::string> names;
    // Built by BuildXlsxCache:
    std::vector<std::vector<CacheValue>> shared;
    std::vector<std::map<CacheValue, int>> sharedIndex;
    std::vector<GroupItems> groups;
    std::string definitionXml;
    std::string recordsXml;
};

// Whether `pivot` can use `cache`; with `take`, it does (its groupings join the cache's).
bool FitsXlsxCache(XlsxCache& cache, const SpreadsheetPivotTable& pivot, bool take) {
    if (cache.sourceSheet != pivot.sourceSheet || !(cache.range == pivot.sourceRange) ||
        cache.sourceName != pivot.sourceName) {
        return false;
    }
    const int columns = pivot.sourceRange.ColCount();
    const std::vector<FieldPlace> places = PlacesOf(pivot, columns);
    for (int c = 0; c < columns; ++c) {
        const FieldPlace& place = places[static_cast<size_t>(c)];
        if (!place.field) continue;
        if (cache.fixed[static_cast<size_t>(c)] && !SameGrouping(cache.groupings[static_cast<size_t>(c)], place.field->grouping)) {
            return false;
        }
    }
    if (take) {
        for (int c = 0; c < columns; ++c) {
            const FieldPlace& place = places[static_cast<size_t>(c)];
            if (!place.field) continue;
            cache.fixed[static_cast<size_t>(c)] = true;
            cache.groupings[static_cast<size_t>(c)] = place.field->grouping;
        }
    }
    return true;
}

// The cache's definition (its source, a cacheField per column with its
// distinct values and grouping) and its records.
void BuildXlsxCache(XlsxCache& cache) {
    const SpreadsheetSheet& source = *cache.source;
    const CellRange src = cache.range;
    const int columns = src.ColCount();
    cache.shared.assign(static_cast<size_t>(columns), {});
    cache.sharedIndex.assign(static_cast<size_t>(columns), {});
    cache.groups.assign(static_cast<size_t>(columns), {});
    std::vector<int> recordRows;
    for (int r = src.start.row + 1; r <= src.end.row; ++r) {
        bool any = false;
        for (int c = 0; c < columns && !any; ++c) {
            const SpreadsheetCell* cell = source.GetCellIfExists(r, src.start.col + c);
            any = cell && !cell->IsEmpty();
        }
        if (!any) continue;
        recordRows.push_back(r);
        for (int c = 0; c < columns; ++c) {
            cache.sharedIndex[static_cast<size_t>(c)].emplace(CellCacheValue(source, r, src.start.col + c), 0);
        }
    }
    for (int c = 0; c < columns; ++c) {
        int i = 0;
        for (auto& [value, at] : cache.sharedIndex[static_cast<size_t>(c)]) {
            at = i++;
            cache.shared[static_cast<size_t>(c)].push_back(value);
        }
    }
    std::ostringstream def;
    def << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        << "<pivotCacheDefinition xmlns=\"" << kMain << "\" xmlns:r=\"" << kRelNs
        << "\" r:id=\"rId1\" refreshOnLoad=\"1\" recordCount=\"" << recordRows.size()
        << "\" createdVersion=\"3\" refreshedVersion=\"3\" minRefreshableVersion=\"3\">"
        << "<cacheSource type=\"worksheet\"><worksheetSource ";
    if (!cache.sourceName.empty()) def << "name=\"" << XmlEscape(cache.sourceName) << "\"";
    else def << "ref=\"" << RangeRef(src) << "\" sheet=\"" << XmlEscape(cache.sourceSheet) << "\"";
    def << "/></cacheSource><cacheFields count=\"" << columns << "\">";
    // Names must differ: a second "Sales" is "Sales2".
    std::set<std::string> usedNames;
    for (int c = 0; c < columns; ++c) {
        const std::string base = c < static_cast<int>(cache.names.size()) ? cache.names[static_cast<size_t>(c)]
                                                                           : "Column" + std::to_string(c + 1);
        std::string name = base;
        for (int k = 2; usedNames.count(name); ++k) name = base + std::to_string(k);
        usedNames.insert(name);
        const auto& values = cache.shared[static_cast<size_t>(c)];
        bool hasText = false, hasNumber = false, hasDate = false, hasBlank = false, hasBool = false, integers = true;
        double low = 0, high = 0;
        bool first = true;
        for (const CacheValue& v : values) {
            switch (v.kind) {
                case CacheValue::Kind::Text: hasText = true; break;
                case CacheValue::Kind::Blank: hasBlank = true; break;
                case CacheValue::Kind::Boolean: hasBool = true; break;
                case CacheValue::Kind::Number:
                case CacheValue::Kind::Date:
                    (v.kind == CacheValue::Kind::Date ? hasDate : hasNumber) = true;
                    if (std::floor(v.number) != v.number) integers = false;
                    if (first || v.number < low) low = v.number;
                    if (first || v.number > high) high = v.number;
                    first = false;
                    break;
            }
        }
        def << "<cacheField name=\"" << XmlEscape(name) << "\" numFmtId=\"" << (hasDate ? 14 : 0) << "\">";
        def << "<sharedItems";
        if (hasNumber || hasDate || hasBool) {
            if (!hasText) def << " containsSemiMixedTypes=\"0\" containsString=\"0\"";
            if (hasDate) def << (hasNumber ? "" : " containsNonDate=\"0\"") << " containsDate=\"1\"";
            if (hasNumber) def << " containsNumber=\"1\"" << (integers ? " containsInteger=\"1\"" : "");
            if (hasText || hasBool || (hasDate && hasNumber)) def << " containsMixedTypes=\"1\"";
            if (hasDate && !hasNumber) {
                def << " minDate=\"" << ExcelDateText(low) << "\" maxDate=\"" << ExcelDateText(high) << "\"";
            } else if (!first) {
                def << " minValue=\"" << FormatFloatRoundTrip(low) << "\" maxValue=\"" << FormatFloatRoundTrip(high) << "\"";
            }
        }
        if (hasBlank) def << " containsBlank=\"1\"";
        def << " count=\"" << values.size() << "\">";
        for (const CacheValue& v : values) {
            switch (v.kind) {
                case CacheValue::Kind::Blank: def << "<m/>"; break;
                case CacheValue::Kind::Text: def << "<s v=\"" << XmlEscape(v.text) << "\"/>"; break;
                case CacheValue::Kind::Number: def << "<n v=\"" << FormatFloatRoundTrip(v.number) << "\"/>"; break;
                case CacheValue::Kind::Date: def << "<d v=\"" << ExcelDateText(v.number) << "\"/>"; break;
                case CacheValue::Kind::Boolean: def << "<b v=\"" << (v.number != 0 ? 1 : 0) << "\"/>"; break;
            }
        }
        def << "</sharedItems>";
        const PivotGrouping& wanted = cache.groupings[static_cast<size_t>(c)];
        if (wanted.by != PivotGroupBy::NoGrouping && !first) {
            PivotField field;
            field.column = c;
            field.grouping = wanted;
            const std::vector<PivotItem> items = SpreadsheetPivotFieldItems(source, src, field);
            // Automatic ends are the column's smallest and largest numbers.
            PivotGrouping grouping = wanted;
            if (grouping.by == PivotGroupBy::NumberRange) {
                if (grouping.autoStart) grouping.start = std::floor(low);
                if (grouping.autoEnd) grouping.end = high;
            }
            GroupItems group = MakeGroupItems(grouping, items, low, high);
            if (!group.rangePr.empty()) {
                def << "<fieldGroup base=\"" << c << "\">" << group.rangePr << "<groupItems count=\""
                    << group.labels.size() << "\">";
                for (const std::string& label : group.labels) def << "<s v=\"" << XmlEscape(label) << "\"/>";
                def << "</groupItems></fieldGroup>";
                cache.groups[static_cast<size_t>(c)] = std::move(group);
            }
        }
        def << "</cacheField>";
    }
    def << "</cacheFields></pivotCacheDefinition>\n";
    cache.definitionXml = def.str();

    std::ostringstream records;
    records << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<pivotCacheRecords xmlns=\"" << kMain << "\" xmlns:r=\"" << kRelNs << "\" count=\"" << recordRows.size()
            << "\">";
    for (int r : recordRows) {
        records << "<r>";
        for (int c = 0; c < columns; ++c) {
            records << "<x v=\"" << cache.sharedIndex[static_cast<size_t>(c)][CellCacheValue(source, r, src.start.col + c)]
                    << "\"/>";
        }
        records << "</r>";
    }
    records << "</pivotCacheRecords>\n";
    cache.recordsXml = records.str();
}

// A pivot table part over `cache` (number `cacheId`).
std::string XlsxPivotTableXml(const SpreadsheetPivotTable& pivot, const PivotOutput& out, const XlsxCache& cache,
                              int cacheId) {
    const SpreadsheetSheet& source = *cache.source;
    const CellRange src = pivot.sourceRange;
    const int columns = src.ColCount();
    const std::vector<FieldPlace> places = PlacesOf(pivot, columns);
    const size_t F = pivot.filterFields.size();
    const int V = static_cast<int>(pivot.valueFields.size());
    CellRange location = pivot.outputRange;
    location.start.row = pivot.target.row + out.tableTop;
    std::ostringstream table;
    table << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          << "<pivotTableDefinition xmlns=\"" << kMain << "\" name=\"" << XmlEscape(pivot.name) << "\" cacheId=\""
          << cacheId << "\" applyNumberFormats=\"0\" applyBorderFormats=\"0\""
          << " applyFontFormats=\"0\" applyPatternFormats=\"0\" applyAlignmentFormats=\"0\""
          << " applyWidthHeightFormats=\"1\" dataCaption=\"Values\" updatedVersion=\"3\""
          << " minRefreshableVersion=\"3\" createdVersion=\"3\" useAutoFormatting=\"1\" itemPrintTitles=\"1\""
          << " indent=\"0\" outline=\"1\" outlineData=\"1\" compact=\"0\" compactData=\"0\""
          << (pivot.valuesInRows && V > 1 ? " dataOnRows=\"1\"" : "")
          << (pivot.grandTotalColumn ? "" : " rowGrandTotals=\"0\"")
          << (pivot.grandTotalRow ? "" : " colGrandTotals=\"0\"") << ">";
    table << "<location ref=\"" << RangeRef(location) << "\" firstHeaderRow=\"1\" firstDataRow=\""
          << std::max(1, out.firstDataRow - out.tableTop) << "\" firstDataCol=\"" << out.firstDataCol << "\"";
    if (F > 0) table << " rowPageCount=\"" << F << "\" colPageCount=\"1\"";
    table << "/><pivotFields count=\"" << columns << "\">";
    for (int c = 0; c < columns; ++c) {
        const FieldPlace& place = places[static_cast<size_t>(c)];
        table << "<pivotField";
        if (place.axis) table << " axis=\"" << place.axis << "\"";
        if (place.isData) table << " dataField=\"1\"";
        table << " compact=\"0\" outline=\"0\" showAll=\"0\"";
        if (!place.field) {
            table << "/>";
            continue;
        }
        const PivotField& f = *place.field;
        if (f.descending) table << " sortType=\"descending\"";
        const bool subtotals = f.subtotals && std::string(place.axis) != "axisPage";
        if (!subtotals) table << " defaultSubtotal=\"0\"";
        const std::vector<PivotItem> items = SpreadsheetPivotFieldItems(source, src, f);
        std::set<std::string> hidden(f.hiddenItems.begin(), f.hiddenItems.end());
        if (std::string(place.axis) == "axisPage" && hidden.size() + 1 < items.size() && !hidden.empty()) {
            table << " multipleItemSelectionAllowed=\"1\"";
        }
        // The field's items as indexes into the cache's values - or, for a
        // grouped field, into its groups, every one of them.
        std::vector<std::pair<int, bool>> listed;   // x, hidden
        const GroupItems& group = cache.groups[static_cast<size_t>(c)];
        if (!group.labels.empty()) {
            std::map<int, std::string> keyOfIndex;
            for (const auto& [key, index] : group.indexOfKey) keyOfIndex[index] = key;
            for (int x = 0; x < static_cast<int>(group.labels.size()); ++x) {
                auto key = keyOfIndex.find(x);
                listed.emplace_back(x, key != keyOfIndex.end() && hidden.count(key->second));
            }
        } else {
            const auto& values = cache.shared[static_cast<size_t>(c)];
            for (const PivotItem& item : items) {
                for (size_t i = 0; i < values.size(); ++i) {
                    if (KeyOfCacheValue(values[i]) == item.key && (values[i].kind == CacheValue::Kind::Blank) == item.blank) {
                        listed.emplace_back(static_cast<int>(i), hidden.count(item.key) > 0);
                        break;
                    }
                }
            }
        }
        table << "><items count=\"" << listed.size() + (subtotals ? 1 : 0) << "\">";
        for (const auto& [x, isHidden] : listed) table << "<item x=\"" << x << "\"" << (isHidden ? " h=\"1\"" : "") << "/>";
        if (subtotals) table << "<item t=\"default\"/>";
        table << "</items></pivotField>";
    }
    table << "</pivotFields>";
    auto fieldList = [&](const char* element, const std::vector<PivotField>& list, bool values) {
        const size_t count = list.size() + (values ? 1 : 0);
        if (count == 0) return;
        table << "<" << element << " count=\"" << count << "\">";
        for (const PivotField& f : list) table << "<field x=\"" << f.column << "\"/>";
        if (values) table << "<field x=\"-2\"/>";
        table << "</" << element << ">";
    };
    fieldList("rowFields", pivot.rowFields, V > 1 && pivot.valuesInRows);
    fieldList("colFields", pivot.columnFields, V > 1 && !pivot.valuesInRows);
    if (F > 0) {
        table << "<pageFields count=\"" << F << "\">";
        for (const PivotField& f : pivot.filterFields) {
            table << "<pageField fld=\"" << f.column << "\" hier=\"-1\"";
            // One item shown: the page field names it, by its place in the field's items.
            const std::vector<PivotItem> items = SpreadsheetPivotFieldItems(source, src, f);
            std::set<std::string> hidden(f.hiddenItems.begin(), f.hiddenItems.end());
            int shown = -1, visible = 0;
            for (size_t i = 0; i < items.size(); ++i) {
                if (!hidden.count(items[i].key)) { shown = static_cast<int>(i); ++visible; }
            }
            if (visible == 1 && !hidden.empty()) table << " item=\"" << shown << "\"";
            table << "/>";
        }
        table << "</pageFields>";
    }
    if (V > 0) {
        table << "<dataFields count=\"" << V << "\">";
        for (const PivotValueField& v : pivot.valueFields) {
            table << "<dataField name=\"" << XmlEscape(PivotValueFieldCaption(v, out.fieldNames)) << "\" fld=\""
                  << v.column << "\" subtotal=\"" << ExcelSubtotal(v.summary) << "\"";
            switch (v.showAs) {
                case PivotShowAs::PercentOfTotal: table << " showDataAs=\"percentOfTotal\""; break;
                case PivotShowAs::PercentOfRow: table << " showDataAs=\"percentOfRow\""; break;
                case PivotShowAs::PercentOfColumn: table << " showDataAs=\"percentOfCol\""; break;
                case PivotShowAs::Value: break;
            }
            table << " baseField=\"0\" baseItem=\"0\"" << (v.showAs != PivotShowAs::Value ? " numFmtId=\"10\"" : "") << "/>";
        }
        table << "</dataFields>";
    }
    table << "<pivotTableStyleInfo name=\"PivotStyleLight16\" showRowHeaders=\"1\" showColHeaders=\"1\""
          << " showRowStripes=\"0\" showColStripes=\"0\" showLastColumn=\"1\"/></pivotTableDefinition>\n";
    return table.str();
}

} // namespace

std::vector<std::pair<std::string, std::string>> WriteXlsxPivots(const UltraCanvasSpreadsheet& workbook,
                                                                  std::string& workbookPivotCaches,
                                                                  std::string& workbookRelationships,
                                                                  std::string& contentTypesXml) {
    // Which pivots are written (their cells are in the sheet either way:
    // Excel gets no pivot it could not rebuild), and the caches they share.
    struct Job {
        int sheet = 0;
        const SpreadsheetPivotTable* pivot = nullptr;
        PivotOutput out;
        size_t cache = 0;
    };
    std::vector<Job> jobs;
    std::vector<XlsxCache> caches;
    for (int s = 0; s < workbook.GetSheetCount(); ++s) {
        const SpreadsheetSheet* sheet = workbook.GetSheet(s);
        if (!sheet) continue;
        for (const SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) {
            const SpreadsheetSheet* source = workbook.GetSheetByName(pivot.sourceSheet);
            if (!source || !pivot.unsupportedReason.empty() || !pivot.HasOutput() || pivot.sourceRange.RowCount() < 2) {
                continue;
            }
            Job job;
            job.sheet = s;
            job.pivot = &pivot;
            if (!ComputeSpreadsheetPivot(pivot, *source, job.out)) continue;
            size_t at = 0;
            while (at < caches.size() && !FitsXlsxCache(caches[at], pivot, true)) ++at;
            if (at == caches.size()) {
                XlsxCache cache;
                cache.source = source;
                cache.sourceSheet = pivot.sourceSheet;
                cache.sourceName = pivot.sourceName;
                cache.range = pivot.sourceRange;
                const size_t columns = static_cast<size_t>(pivot.sourceRange.ColCount());
                cache.groupings.assign(columns, PivotGrouping());
                cache.fixed.assign(columns, false);
                cache.names = job.out.fieldNames;
                FitsXlsxCache(cache, pivot, true);
                caches.push_back(std::move(cache));
            }
            job.cache = at;
            jobs.push_back(std::move(job));
        }
    }
    std::vector<std::pair<std::string, std::string>> parts;
    std::string cacheList;
    for (size_t i = 0; i < caches.size(); ++i) {
        BuildXlsxCache(caches[i]);
        const std::string n = std::to_string(i + 1);
        const std::string defPart = "xl/pivotCache/pivotCacheDefinition" + n + ".xml";
        const std::string recPart = "xl/pivotCache/pivotCacheRecords" + n + ".xml";
        parts.emplace_back(defPart, caches[i].definitionXml);
        parts.emplace_back("xl/pivotCache/_rels/pivotCacheDefinition" + n + ".xml.rels",
                           RelationshipsXml(Relationship("rId1", kRelPivotCacheRecords, "pivotCacheRecords" + n + ".xml")));
        parts.emplace_back(recPart, caches[i].recordsXml);
        cacheList += "<pivotCache cacheId=\"" + n + "\" r:id=\"rIdPivotCache" + n + "\"/>";
        workbookRelationships += Relationship("rIdPivotCache" + n, kRelPivotCacheDefinition,
                                              "pivotCache/pivotCacheDefinition" + n + ".xml") + "\n";
        contentTypesXml.insert(contentTypesXml.rfind("</Types>"),
                               Override(defPart, "spreadsheetml.pivotCacheDefinition+xml") +
                                   Override(recPart, "spreadsheetml.pivotCacheRecords+xml"));
    }
    std::map<int, std::string> sheetRels;   // sheet -> its <Relationship>s to its tables
    std::map<int, int> sheetRelCount;
    for (size_t j = 0; j < jobs.size(); ++j) {
        const Job& job = jobs[j];
        const std::string n = std::to_string(j + 1);
        const std::string tablePart = "xl/pivotTables/pivotTable" + n + ".xml";
        const std::string cacheNumber = std::to_string(job.cache + 1);
        parts.emplace_back(tablePart, XlsxPivotTableXml(*job.pivot, job.out, caches[job.cache], static_cast<int>(job.cache + 1)));
        parts.emplace_back("xl/pivotTables/_rels/pivotTable" + n + ".xml.rels",
                           RelationshipsXml(Relationship("rId1", kRelPivotCacheDefinition,
                                                         "../pivotCache/pivotCacheDefinition" + cacheNumber + ".xml")));
        sheetRels[job.sheet] += Relationship("rIdPivot" + std::to_string(++sheetRelCount[job.sheet]), kRelPivotTable,
                                             "../pivotTables/pivotTable" + n + ".xml");
        contentTypesXml.insert(contentTypesXml.rfind("</Types>"), Override(tablePart, "spreadsheetml.pivotTable+xml"));
    }
    for (const auto& [sheet, rels] : sheetRels) {
        // Merged into the worksheet's own relationships by the writer.
        parts.emplace_back("xl/worksheets/_rels/sheet" + std::to_string(sheet + 1) + ".xml.rels", RelationshipsXml(rels));
    }
    if (!cacheList.empty()) workbookPivotCaches = "<pivotCaches>" + cacheList + "</pivotCaches>";
    return parts;
}

namespace {

// What a cache field holds: its values (for the items' indexes), its
// grouping.
struct ReadCacheField {
    std::string name;
    std::vector<std::string> keys;      // per shared item, as PivotItem::key
    bool grouped = false;
    PivotGrouping grouping;
    std::vector<std::string> groupKeys; // per group item
    bool computed = false;              // a formula or a field grouping added
};

void ReadCacheFieldGroup(const tinyxml2::XMLElement* fieldGroup, int index, ReadCacheField& field) {
    if (!fieldGroup) return;
    if (fieldGroup->Attribute("par") || AttrInt(fieldGroup, "base", index) != index ||
        fieldGroup->FirstChildElement("discretePr")) {
        field.computed = true;   // a field of groups of another, or groups of chosen items
        return;
    }
    const tinyxml2::XMLElement* range = fieldGroup->FirstChildElement("rangePr");
    if (!range) return;
    const std::string by = Attr(range, "groupBy");
    field.grouped = true;
    const tinyxml2::XMLElement* items = fieldGroup->FirstChildElement("groupItems");
    std::vector<std::string> labels;
    for (const tinyxml2::XMLElement* e = items ? items->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
        labels.push_back(Attr(e, "v"));
    }
    if (by == "months" || by == "quarters" || by == "years") {
        field.grouping.by = by == "months" ? PivotGroupBy::Months : by == "quarters" ? PivotGroupBy::Quarters
                                                                                      : PivotGroupBy::Years;
        for (size_t i = 0; i < labels.size(); ++i) {
            if (i == 0 || i + 1 == labels.size()) {
                field.groupKeys.push_back(i == 0 ? "<" : ">");
            } else if (field.grouping.by == PivotGroupBy::Years) {
                field.groupKeys.push_back(labels[i]);
            } else {
                field.groupKeys.push_back(std::to_string(i));
            }
        }
    } else if (by.empty() || by == "range") {
        field.grouping.by = PivotGroupBy::NumberRange;
        field.grouping.autoStart = AttrTrue(range, "autoStart", true);
        field.grouping.autoEnd = AttrTrue(range, "autoEnd", true);
        field.grouping.start = AttrNumber(range, "startNum", 0.0);
        field.grouping.end = AttrNumber(range, "endNum", 0.0);
        field.grouping.step = AttrNumber(range, "groupInterval", 1.0);
        for (size_t i = 0; i < labels.size(); ++i) {
            if (i == 0) field.groupKeys.push_back("<");
            else if (i + 1 == labels.size()) field.groupKeys.push_back(">");
            else field.groupKeys.push_back(FormatFloatRoundTrip(field.grouping.start + static_cast<double>(i - 1) * field.grouping.step));
        }
    } else {
        field.computed = true;   // days, hours, minutes, seconds
    }
}

} // namespace

void ReadXlsxSheetPivots(const UCZipPackageReader& package, const std::string& sheetPart, SpreadsheetSheet& sheet,
                         const UltraCanvasSpreadsheet& workbook) {
    for (const auto& [id, rel] : ReadRelationships(package, sheetPart)) {
        (void)id;
        if (!EndsWith(rel.first, "/pivotTable")) continue;
        std::string tableXml;
        if (!package.ReadEntry(rel.second, tableXml)) continue;
        tinyxml2::XMLDocument tableDoc;
        if (tableDoc.Parse(tableXml.c_str()) != tinyxml2::XML_SUCCESS) continue;
        const tinyxml2::XMLElement* table = tableDoc.FirstChildElement("pivotTableDefinition");
        if (!table) continue;
        SpreadsheetPivotTable pivot;
        pivot.name = Attr(table, "name");
        auto unsupported = [&pivot](const std::string& why) {
            if (pivot.unsupportedReason.empty()) pivot.unsupportedReason = why;
        };
        // Where it stands: the location, the filters above it.
        const tinyxml2::XMLElement* location = table->FirstChildElement("location");
        CellRange where;
        if (!location || !ParseRangeRef(Attr(location, "ref"), where)) continue;
        const tinyxml2::XMLElement* pageFields = table->FirstChildElement("pageFields");
        int pages = 0;
        for (const tinyxml2::XMLElement* p = pageFields ? pageFields->FirstChildElement("pageField") : nullptr; p;
             p = p->NextSiblingElement("pageField")) {
            ++pages;
        }
        pivot.target = CellAddress(std::max(0, where.start.row - (pages > 0 ? pages + 1 : 0)), where.start.col);
        pivot.outputRange = CellRange(pivot.target.row, where.start.col, where.end.row,
                                      std::max(where.end.col, where.start.col + (pages > 0 ? 1 : 0)));
        pivot.valuesInRows = AttrTrue(table, "dataOnRows", false);
        pivot.grandTotalColumn = AttrTrue(table, "rowGrandTotals", true);
        pivot.grandTotalRow = AttrTrue(table, "colGrandTotals", true);

        // Its cache: the source and the fields.
        std::vector<ReadCacheField> fields;
        std::string cachePart;
        for (const auto& [tid, trel] : ReadRelationships(package, rel.second)) {
            (void)tid;
            if (EndsWith(trel.first, "/pivotCacheDefinition")) cachePart = trel.second;
        }
        std::string cacheXml;
        tinyxml2::XMLDocument cacheDoc;
        const tinyxml2::XMLElement* cache = nullptr;
        if (!cachePart.empty() && package.ReadEntry(cachePart, cacheXml) &&
            cacheDoc.Parse(cacheXml.c_str()) == tinyxml2::XML_SUCCESS) {
            cache = cacheDoc.FirstChildElement("pivotCacheDefinition");
        }
        if (!cache) {
            unsupported(Tr("The pivot table's data comes from outside the workbook."));
        } else {
            const tinyxml2::XMLElement* source = cache->FirstChildElement("cacheSource");
            const tinyxml2::XMLElement* ws = source ? source->FirstChildElement("worksheetSource") : nullptr;
            if (!ws || std::string(Attr(source, "type")) != "worksheet" || ws->Attribute("r:id")) {
                unsupported(Tr("The pivot table's data comes from outside the workbook."));
            } else if (ws->Attribute("ref")) {
                pivot.sourceSheet = ws->Attribute("sheet") ? Attr(ws, "sheet") : sheet.GetName();
                if (!ParseRangeRef(Attr(ws, "ref"), pivot.sourceRange)) unsupported(Tr("The pivot table's data comes from outside the workbook."));
            } else if (ws->Attribute("name")) {
                pivot.sourceName = Attr(ws, "name");
                const CellRange named = workbook.ResolveNamedRange(pivot.sourceName, sheet.GetName());
                if (named.IsValid()) {
                    pivot.sourceRange = CellRange(named.start.row, named.start.col, named.end.row, named.end.col);
                    pivot.sourceSheet = named.start.sheetName.empty() ? sheet.GetName() : named.start.sheetName;
                } else {
                    unsupported(Tr("The pivot table's data comes from outside the workbook."));
                }
            }
            const tinyxml2::XMLElement* cacheFields = cache->FirstChildElement("cacheFields");
            int index = 0;
            for (const tinyxml2::XMLElement* cf = cacheFields ? cacheFields->FirstChildElement("cacheField") : nullptr; cf;
                 cf = cf->NextSiblingElement("cacheField"), ++index) {
                ReadCacheField field;
                field.name = Attr(cf, "name");
                if (cf->Attribute("formula") || std::string(Attr(cf, "databaseField")) == "0") field.computed = true;
                if (const tinyxml2::XMLElement* items = cf->FirstChildElement("sharedItems")) {
                    for (const tinyxml2::XMLElement* e = items->FirstChildElement(); e; e = e->NextSiblingElement()) {
                        const std::string kind = e->Name() ? e->Name() : "";
                        const std::string v = Attr(e, "v");
                        if (kind == "m") field.keys.push_back(std::string());
                        else if (kind == "n") { double x = 0; TryParseFloat(v, x); field.keys.push_back(FormatFloatRoundTrip(x)); }
                        else if (kind == "d") field.keys.push_back(FormatFloatRoundTrip(DateSerialFromExcel(v)));
                        else if (kind == "b") field.keys.push_back(v == "1" || v == "true" ? "1" : "0");
                        else field.keys.push_back(v);
                    }
                }
                ReadCacheFieldGroup(cf->FirstChildElement("fieldGroup"), index, field);
                fields.push_back(std::move(field));
            }
            if (pivot.unsupportedReason.empty() && static_cast<int>(fields.size()) > pivot.sourceRange.ColCount()) {
                unsupported(Tr("The pivot table uses a calculated field, a grouping or a filter this program does not show."));
            }
        }

        // The table's fields.
        const tinyxml2::XMLElement* pivotFields = table->FirstChildElement("pivotFields");
        std::vector<const tinyxml2::XMLElement*> pivotFieldList;
        for (const tinyxml2::XMLElement* pf = pivotFields ? pivotFields->FirstChildElement("pivotField") : nullptr; pf;
             pf = pf->NextSiblingElement("pivotField")) {
            pivotFieldList.push_back(pf);
        }
        // A field by its index, with its order, subtotals, hidden items and grouping.
        auto makeField = [&](int x, bool page) {
            PivotField field;
            field.column = x;
            if (x < 0 || x >= static_cast<int>(pivotFieldList.size())) return field;
            const tinyxml2::XMLElement* pf = pivotFieldList[static_cast<size_t>(x)];
            field.descending = std::string(Attr(pf, "sortType")) == "descending";
            field.subtotals = !page && AttrTrue(pf, "defaultSubtotal", true);
            const ReadCacheField* cf = x < static_cast<int>(fields.size()) ? &fields[static_cast<size_t>(x)] : nullptr;
            if (cf && cf->computed) unsupported(Tr("The pivot table uses a calculated field, a grouping or a filter this program does not show."));
            if (cf && cf->grouped) field.grouping = cf->grouping;
            const tinyxml2::XMLElement* items = pf->FirstChildElement("items");
            int position = 0;
            for (const tinyxml2::XMLElement* item = items ? items->FirstChildElement("item") : nullptr; item;
                 item = item->NextSiblingElement("item"), ++position) {
                if (item->Attribute("t")) continue;   // a subtotal's item
                if (!AttrTrue(item, "h", false)) continue;
                const int xi = AttrInt(item, "x", -1);
                if (!cf || xi < 0) continue;
                const std::vector<std::string>& keys = cf->grouped ? cf->groupKeys : cf->keys;
                if (xi < static_cast<int>(keys.size())) field.hiddenItems.push_back(keys[static_cast<size_t>(xi)]);
            }
            return field;
        };
        auto readAxis = [&](const char* element, std::vector<PivotField>& list, bool& hasValues) {
            const tinyxml2::XMLElement* axis = table->FirstChildElement(element);
            for (const tinyxml2::XMLElement* f = axis ? axis->FirstChildElement("field") : nullptr; f;
                 f = f->NextSiblingElement("field")) {
                const int x = AttrInt(f, "x", 0);
                if (x == -2) { hasValues = true; continue; }
                list.push_back(makeField(x, false));
            }
        };
        bool valuesOnRows = false, valuesOnCols = false;
        readAxis("rowFields", pivot.rowFields, valuesOnRows);
        readAxis("colFields", pivot.columnFields, valuesOnCols);
        if (valuesOnRows) pivot.valuesInRows = true;
        for (const tinyxml2::XMLElement* p = pageFields ? pageFields->FirstChildElement("pageField") : nullptr; p;
             p = p->NextSiblingElement("pageField")) {
            const int x = AttrInt(p, "fld", 0);
            PivotField field = makeField(x, true);
            const int item = AttrInt(p, "item", -1);
            if (item >= 0 && x >= 0 && x < static_cast<int>(pivotFieldList.size()) && x < static_cast<int>(fields.size())) {
                // One item shown: every other is hidden.
                const ReadCacheField& cf = fields[static_cast<size_t>(x)];
                const std::vector<std::string>& keys = cf.grouped ? cf.groupKeys : cf.keys;
                const tinyxml2::XMLElement* items = pivotFieldList[static_cast<size_t>(x)]->FirstChildElement("items");
                int position = 0;
                field.hiddenItems.clear();
                for (const tinyxml2::XMLElement* it = items ? items->FirstChildElement("item") : nullptr; it;
                     it = it->NextSiblingElement("item"), ++position) {
                    const int xi = AttrInt(it, "x", -1);
                    if (it->Attribute("t") || xi < 0 || xi >= static_cast<int>(keys.size())) continue;
                    if (position != item) field.hiddenItems.push_back(keys[static_cast<size_t>(xi)]);
                }
            }
            pivot.filterFields.push_back(field);
        }
        const tinyxml2::XMLElement* dataFields = table->FirstChildElement("dataFields");
        for (const tinyxml2::XMLElement* d = dataFields ? dataFields->FirstChildElement("dataField") : nullptr; d;
             d = d->NextSiblingElement("dataField")) {
            PivotValueField value;
            value.column = AttrInt(d, "fld", 0);
            value.name = Attr(d, "name");
            if (!SummaryFromExcel(Attr(d, "subtotal"), value.summary)) unsupported(Tr("A value field is summarised or shown in a way this program does not show."));
            const std::string showAs = Attr(d, "showDataAs");
            if (showAs == "percentOfTotal") value.showAs = PivotShowAs::PercentOfTotal;
            else if (showAs == "percentOfRow") value.showAs = PivotShowAs::PercentOfRow;
            else if (showAs == "percentOfCol") value.showAs = PivotShowAs::PercentOfColumn;
            else if (!showAs.empty() && showAs != "normal") unsupported(Tr("A value field is summarised or shown in a way this program does not show."));
            if (value.column >= 0 && value.column < static_cast<int>(fields.size()) && fields[static_cast<size_t>(value.column)].computed) {
                unsupported(Tr("The pivot table uses a calculated field, a grouping or a filter this program does not show."));
            }
            pivot.valueFields.push_back(value);
        }
        if (const SpreadsheetSheet* source = workbook.GetSheetByName(pivot.sourceSheet)) {
            if (pivot.sourceRange.IsValid()) pivot.fieldNames = SpreadsheetPivotFieldNames(*source, pivot.sourceRange);
        } else if (pivot.unsupportedReason.empty()) {
            unsupported(Tr("The pivot table's data comes from outside the workbook."));
        }
        if (pivot.unsupportedReason.empty() && !SpreadsheetPivotFieldsFit(pivot)) {
            unsupported(Tr("A field of the pivot table is not a column of its source."));
        }
        sheet.GetPivotTables().push_back(std::move(pivot));
    }
}

// ============================================================================
// .ODS
// ============================================================================

std::string WriteOdsPivots(const UltraCanvasSpreadsheet& workbook) {
    std::ostringstream xml;
    bool any = false;
    for (int s = 0; s < workbook.GetSheetCount(); ++s) {
        const SpreadsheetSheet* sheet = workbook.GetSheet(s);
        if (!sheet) continue;
        for (const SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) {
            const SpreadsheetSheet* source = workbook.GetSheetByName(pivot.sourceSheet);
            if (!source || !pivot.unsupportedReason.empty() || !pivot.HasOutput()) continue;
            if (!any) xml << "<table:data-pilot-tables>\n";
            any = true;
            const std::vector<std::string> names = SpreadsheetPivotFieldNames(*source, pivot.sourceRange);
            auto nameOf = [&names](int column) {
                return column >= 0 && column < static_cast<int>(names.size()) ? names[static_cast<size_t>(column)]
                                                                               : std::string();
            };
            const char* grand = pivot.grandTotalRow && pivot.grandTotalColumn ? "both"
                                : pivot.grandTotalColumn                     ? "row"
                                : pivot.grandTotalRow                        ? "column"
                                                                             : "none";
            SpreadsheetChartRange target{sheet->GetName(), pivot.outputRange};
            SpreadsheetChartRange from{pivot.sourceSheet, pivot.sourceRange};
            xml << "<table:data-pilot-table table:name=\"" << XmlEscape(pivot.name)
                << "\" table:target-range-address=\"" << XmlEscape(target.ToOdf()) << "\" table:grand-total=\""
                << grand << "\" table:show-filter-button=\"true\">\n"
                << "<table:source-cell-range table:cell-range-address=\"" << XmlEscape(from.ToOdf()) << "\"/>\n";
            auto axisField = [&](const PivotField& f, const char* orientation, const std::string& selectedPage) {
                xml << "<table:data-pilot-field table:source-field-name=\"" << XmlEscape(nameOf(f.column))
                    << "\" table:orientation=\"" << orientation << "\" table:used-hierarchy=\"-1\" table:function=\"auto\"";
                if (!selectedPage.empty()) xml << " table:selected-page=\"" << XmlEscape(selectedPage) << "\"";
                xml << ">\n<table:data-pilot-level table:show-empty=\"false\">";
                xml << "<table:data-pilot-subtotals>";
                if (f.subtotals && std::strcmp(orientation, "page") != 0) {
                    xml << "<table:data-pilot-subtotal table:function=\"auto\"/>";
                }
                xml << "</table:data-pilot-subtotals>";
                if (!f.hiddenItems.empty()) {
                    xml << "<table:data-pilot-members>";
                    std::set<std::string> hidden(f.hiddenItems.begin(), f.hiddenItems.end());
                    for (const PivotItem& item : SpreadsheetPivotFieldItems(*source, pivot.sourceRange, f)) {
                        xml << "<table:data-pilot-member table:name=\"" << XmlEscape(item.label)
                            << "\" table:display=\"" << (hidden.count(item.key) ? "false" : "true")
                            << "\" table:show-details=\"true\"/>";
                    }
                    xml << "</table:data-pilot-members>";
                }
                xml << "<table:data-pilot-sort-info table:sort-mode=\"name\" table:order=\""
                    << (f.descending ? "descending" : "ascending") << "\"/>";
                xml << "</table:data-pilot-level>";
                switch (f.grouping.by) {
                    case PivotGroupBy::Months: xml << "<table:data-pilot-groups table:grouped-by=\"months\" table:start=\"auto\" table:end=\"auto\" table:step=\"0\"/>"; break;
                    case PivotGroupBy::Quarters: xml << "<table:data-pilot-groups table:grouped-by=\"quarters\" table:start=\"auto\" table:end=\"auto\" table:step=\"0\"/>"; break;
                    case PivotGroupBy::Years: xml << "<table:data-pilot-groups table:grouped-by=\"years\" table:start=\"auto\" table:end=\"auto\" table:step=\"0\"/>"; break;
                    case PivotGroupBy::NumberRange:
                        xml << "<table:data-pilot-groups table:start=\""
                            << (f.grouping.autoStart ? std::string("auto") : FormatFloatRoundTrip(f.grouping.start))
                            << "\" table:end=\""
                            << (f.grouping.autoEnd ? std::string("auto") : FormatFloatRoundTrip(f.grouping.end))
                            << "\" table:step=\"" << FormatFloatRoundTrip(f.grouping.step) << "\"/>";
                        break;
                    case PivotGroupBy::NoGrouping: break;
                }
                xml << "\n</table:data-pilot-field>\n";
            };
            const int V = static_cast<int>(pivot.valueFields.size());
            for (const PivotField& f : pivot.rowFields) axisField(f, "row", "");
            if (V > 1 && pivot.valuesInRows) {
                xml << "<table:data-pilot-field table:source-field-name=\"\" table:is-data-layout-field=\"true\" "
                       "table:orientation=\"row\" table:used-hierarchy=\"-1\" table:function=\"auto\"/>\n";
            }
            for (const PivotField& f : pivot.columnFields) axisField(f, "column", "");
            if (V > 1 && !pivot.valuesInRows) {
                xml << "<table:data-pilot-field table:source-field-name=\"\" table:is-data-layout-field=\"true\" "
                       "table:orientation=\"column\" table:used-hierarchy=\"-1\" table:function=\"auto\"/>\n";
            }
            for (const PivotField& f : pivot.filterFields) {
                std::string selected;
                if (!f.hiddenItems.empty()) {
                    std::set<std::string> hidden(f.hiddenItems.begin(), f.hiddenItems.end());
                    int visible = 0;
                    for (const PivotItem& item : SpreadsheetPivotFieldItems(*source, pivot.sourceRange, f)) {
                        if (!hidden.count(item.key)) { selected = item.label; ++visible; }
                    }
                    if (visible != 1) selected.clear();
                }
                axisField(f, "page", selected);
            }
            for (const PivotValueField& v : pivot.valueFields) {
                xml << "<table:data-pilot-field table:source-field-name=\"" << XmlEscape(nameOf(v.column))
                    << "\" table:orientation=\"data\" table:used-hierarchy=\"-1\" table:function=\""
                    << OdfFunction(v.summary) << "\">";
                const char* reference = v.showAs == PivotShowAs::PercentOfTotal  ? "total-percentage"
                                        : v.showAs == PivotShowAs::PercentOfRow    ? "row-percentage"
                                        : v.showAs == PivotShowAs::PercentOfColumn ? "column-percentage"
                                                                                   : nullptr;
                if (reference) {
                    xml << "<table:data-pilot-field-reference table:field-name=\"\" table:type=\"" << reference << "\"/>";
                }
                xml << "</table:data-pilot-field>\n";
            }
            xml << "</table:data-pilot-table>\n";
        }
    }
    if (any) xml << "</table:data-pilot-tables>\n";
    return xml.str();
}

void ReadOdsPivots(UltraCanvasSpreadsheet& workbook, const std::string& contentXml) {
    if (contentXml.find("data-pilot-table") == std::string::npos) return;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(contentXml.c_str()) != tinyxml2::XML_SUCCESS) return;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("office:document-content");
    const tinyxml2::XMLElement* body = root ? root->FirstChildElement("office:body") : nullptr;
    const tinyxml2::XMLElement* book = body ? body->FirstChildElement("office:spreadsheet") : nullptr;
    const tinyxml2::XMLElement* tables = book ? book->FirstChildElement("table:data-pilot-tables") : nullptr;
    for (const tinyxml2::XMLElement* dp = tables ? tables->FirstChildElement("table:data-pilot-table") : nullptr; dp;
         dp = dp->NextSiblingElement("table:data-pilot-table")) {
        SpreadsheetChartRange target;
        if (!SpreadsheetChartRange::FromOdf(Attr(dp, "table:target-range-address"), target)) continue;
        SpreadsheetSheet* sheet = workbook.GetSheetByName(target.sheet);
        if (!sheet) continue;
        SpreadsheetPivotTable pivot;
        pivot.name = Attr(dp, "table:name");
        pivot.outputRange = CellRange(target.range.start.row, target.range.start.col, target.range.end.row,
                                      target.range.end.col);
        pivot.target = CellAddress(target.range.start.row, target.range.start.col);
        auto unsupported = [&pivot](const std::string& why) {
            if (pivot.unsupportedReason.empty()) pivot.unsupportedReason = why;
        };
        const std::string grand = Attr(dp, "table:grand-total");
        pivot.grandTotalRow = grand.empty() || grand == "both" || grand == "column";
        pivot.grandTotalColumn = grand.empty() || grand == "both" || grand == "row";
        const tinyxml2::XMLElement* source = dp->FirstChildElement("table:source-cell-range");
        SpreadsheetChartRange from;
        const SpreadsheetSheet* sourceSheet = nullptr;
        if (!source || !SpreadsheetChartRange::FromOdf(Attr(source, "table:cell-range-address"), from)) {
            unsupported(Tr("The pivot table's data comes from outside the workbook."));
        } else {
            pivot.sourceSheet = from.sheet;
            pivot.sourceRange = CellRange(from.range.start.row, from.range.start.col, from.range.end.row,
                                          from.range.end.col);
            sourceSheet = workbook.GetSheetByName(from.sheet);
            if (!sourceSheet) unsupported(Tr("The pivot table's data comes from outside the workbook."));
            if (source->FirstChildElement("table:filter")) unsupported(Tr("The pivot table uses a calculated field, a grouping or a filter this program does not show."));
        }
        std::vector<std::string> names;
        if (sourceSheet) names = SpreadsheetPivotFieldNames(*sourceSheet, pivot.sourceRange);
        pivot.fieldNames = names;
        auto columnOf = [&names](const std::string& name) {
            for (size_t i = 0; i < names.size(); ++i) if (names[i] == name) return static_cast<int>(i);
            return -1;
        };
        for (const tinyxml2::XMLElement* f = dp->FirstChildElement("table:data-pilot-field"); f;
             f = f->NextSiblingElement("table:data-pilot-field")) {
            const std::string orientation = Attr(f, "table:orientation");
            if (AttrTrue(f, "table:is-data-layout-field", false)) {
                pivot.valuesInRows = orientation == "row";
                continue;
            }
            if (orientation == "hidden" || orientation.empty()) continue;
            const int column = columnOf(Attr(f, "table:source-field-name"));
            if (column < 0) {
                unsupported(Tr("A field of the pivot table is not a column of its source."));
                continue;
            }
            if (orientation == "data") {
                PivotValueField value;
                value.column = column;
                if (!SummaryFromOdf(Attr(f, "table:function"), value.summary)) unsupported(Tr("A value field is summarised or shown in a way this program does not show."));
                if (const tinyxml2::XMLElement* ref = f->FirstChildElement("table:data-pilot-field-reference")) {
                    const std::string type = Attr(ref, "table:type");
                    if (type == "total-percentage") value.showAs = PivotShowAs::PercentOfTotal;
                    else if (type == "row-percentage") value.showAs = PivotShowAs::PercentOfRow;
                    else if (type == "column-percentage") value.showAs = PivotShowAs::PercentOfColumn;
                    else if (!type.empty() && type != "none") unsupported(Tr("A value field is summarised or shown in a way this program does not show."));
                }
                pivot.valueFields.push_back(value);
                continue;
            }
            PivotField field;
            field.column = column;
            const tinyxml2::XMLElement* level = f->FirstChildElement("table:data-pilot-level");
            if (const tinyxml2::XMLElement* subs = level ? level->FirstChildElement("table:data-pilot-subtotals") : nullptr) {
                field.subtotals = subs->FirstChildElement("table:data-pilot-subtotal") != nullptr;
            }
            if (const tinyxml2::XMLElement* sort = level ? level->FirstChildElement("table:data-pilot-sort-info") : nullptr) {
                // Sorted by name; "manual" keeps the members' order, "data" sorts by a value.
                const std::string mode = Attr(sort, "table:sort-mode");
                field.descending = (mode.empty() || mode == "name") &&
                                   std::string(Attr(sort, "table:order")) == "descending";
            }
            if (const tinyxml2::XMLElement* groups = f->FirstChildElement("table:data-pilot-groups")) {
                const std::string by = Attr(groups, "table:grouped-by");
                if (by == "months") field.grouping.by = PivotGroupBy::Months;
                else if (by == "quarters") field.grouping.by = PivotGroupBy::Quarters;
                else if (by == "years") field.grouping.by = PivotGroupBy::Years;
                else if (by.empty()) {
                    field.grouping.by = PivotGroupBy::NumberRange;
                    const std::string start = Attr(groups, "table:start");
                    const std::string end = Attr(groups, "table:end");
                    field.grouping.autoStart = start.empty() || start == "auto";
                    field.grouping.autoEnd = end.empty() || end == "auto";
                    if (!field.grouping.autoStart) TryParseFloat(start, field.grouping.start);
                    if (!field.grouping.autoEnd) TryParseFloat(end, field.grouping.end);
                    field.grouping.step = AttrNumber(groups, "table:step", 1.0);
                } else {
                    unsupported(Tr("The pivot table uses a calculated field, a grouping or a filter this program does not show."));
                }
                if (groups->FirstChildElement("table:data-pilot-group")) {
                    unsupported(Tr("The pivot table uses a calculated field, a grouping or a filter this program does not show."));
                }
            }
            // Hidden members, by the items' labels (or keys).
            std::set<std::string> hiddenNames;
            const tinyxml2::XMLElement* members = level ? level->FirstChildElement("table:data-pilot-members") : nullptr;
            for (const tinyxml2::XMLElement* m = members ? members->FirstChildElement("table:data-pilot-member") : nullptr;
                 m; m = m->NextSiblingElement("table:data-pilot-member")) {
                if (!AttrTrue(m, "table:display", true)) hiddenNames.insert(Attr(m, "table:name"));
            }
            const std::string selectedPage = Attr(f, "table:selected-page");
            const bool onePage = orientation == "page" && !selectedPage.empty() &&
                                 !AttrTrue(f, "loext:ignore-selected-page", false);
            if (sourceSheet && (!hiddenNames.empty() || onePage)) {
                for (const PivotItem& item : SpreadsheetPivotFieldItems(*sourceSheet, pivot.sourceRange, field)) {
                    const bool hiddenMember = hiddenNames.count(item.label) || hiddenNames.count(item.key);
                    const bool otherPage = onePage && item.label != selectedPage && item.key != selectedPage;
                    if (hiddenMember || otherPage) field.hiddenItems.push_back(item.key);
                }
            }
            if (orientation == "row") pivot.rowFields.push_back(field);
            else if (orientation == "column") pivot.columnFields.push_back(field);
            else if (orientation == "page") { field.subtotals = false; pivot.filterFields.push_back(field); }
        }
        sheet->GetPivotTables().push_back(std::move(pivot));
    }
}

} // namespace UltraCanvas
