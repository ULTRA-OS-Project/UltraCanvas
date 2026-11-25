// core/UltraCanvasSpreadsheetChart.cpp
// Charts on a spreadsheet (include/UltraCanvasSpreadsheetChart.h): the model
// - ranges, anchors, a chart made from a block of cells and read again from
// them - and the grid's side of it (UltraCanvasSpreadsheet's chart members):
// drawn over the cells, selected, moved and resized with handles, deleted,
// each change an undo step, and a properties dialog.
//
// Version: 1.0.1 - the properties dialog is drawn: it was a Custom dialog, which builds no
//                  sections or buttons, and showed an empty window with no OK or Cancel
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasRichDocumentChart.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlideDeckEditor.h"   // SlideHandle and its reach, as the slide editor's handles
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasFormLayout.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <set>

namespace UltraCanvas {

namespace {

constexpr int kDefaultChartWidth = 480;    // pixels, as a new chart in a spreadsheet commonly is
constexpr int kDefaultChartHeight = 288;
constexpr int kMinimumChartSide = 24;
constexpr double kHandleSize = 8.0;
const Color kChartBorder(191, 191, 191);

bool SameNameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// A sheet name as a reference writes it: bare when it is a plain word that
// cannot be read as a cell, else in single quotes with ' doubled.
std::string QuotedSheet(const std::string& name) {
    bool plain = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') plain = false;
    }
    if (plain) {
        // "AB12" would read as a cell.
        size_t letters = 0;
        while (letters < name.size() && std::isalpha(static_cast<unsigned char>(name[letters]))) ++letters;
        bool digitsAfter = letters > 0 && letters < name.size();
        for (size_t i = letters; i < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) digitsAfter = false;
        }
        if (digitsAfter && letters <= 3) plain = false;
    }
    if (plain) return name;
    std::string out = "'";
    for (char c : name) {
        out += c;
        if (c == '\'') out += '\'';
    }
    return out + "'";
}

std::string AbsoluteCorner(int row, int col) {
    return "$" + CellAddress::ColumnToLetter(col) + "$" + std::to_string(row + 1);
}

// "$AB$12" (or "AB12") at `at` in `text`: its row and column; `at` moves past it.
bool ReadCorner(const std::string& text, size_t& at, int& row, int& col) {
    if (at < text.size() && text[at] == '$') ++at;
    int column = 0, letters = 0;
    while (at < text.size() && std::isalpha(static_cast<unsigned char>(text[at])) && letters < 4) {
        column = column * 26 + (std::toupper(static_cast<unsigned char>(text[at])) - 'A' + 1);
        ++at;
        ++letters;
    }
    if (letters == 0) return false;
    if (at < text.size() && text[at] == '$') ++at;
    long number = 0;
    int digits = 0;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) && digits < 8) {
        number = number * 10 + (text[at] - '0');
        ++at;
        ++digits;
    }
    if (digits == 0 || number < 1 || number > SpreadsheetLimits::MaxRows || column > SpreadsheetLimits::MaxColumns) return false;
    row = static_cast<int>(number - 1);
    col = column - 1;
    return true;
}

// A sheet name at `at` - quoted ('It''s') or up to one of `stops` - and `at` past it.
bool ReadSheetName(const std::string& text, size_t& at, const std::string& stops, std::string& name) {
    name.clear();
    if (at < text.size() && text[at] == '\'') {
        ++at;
        while (at < text.size()) {
            if (text[at] == '\'') {
                if (at + 1 < text.size() && text[at + 1] == '\'') { name += '\''; at += 2; continue; }
                ++at;
                return !name.empty();
            }
            name += text[at++];
        }
        return false;
    }
    while (at < text.size() && stops.find(text[at]) == std::string::npos) name += text[at++];
    return !name.empty();
}

// The value of a cell as a chart's word: its display text.
std::string CellWords(const SpreadsheetSheet& sheet, int row, int col) {
    return sheet.GetCellDisplayValue(row, col);
}

// The numbers of a range, row by row (a cell without one reads as 0).
std::vector<double> RangeNumbers(const SpreadsheetSheet& sheet, const CellRange& range) {
    std::vector<double> values;
    const long count = static_cast<long>(range.RowCount()) * range.ColCount();
    if (count <= 0 || count > 100000) return values;
    values.reserve(static_cast<size_t>(count));
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            double value = 0.0;
            if (!SpreadsheetChartCellNumber(sheet, row, col, value)) value = 0.0;
            values.push_back(value);
        }
    }
    return values;
}

std::vector<std::string> RangeWords(const SpreadsheetSheet& sheet, const CellRange& range) {
    std::vector<std::string> words;
    const long count = static_cast<long>(range.RowCount()) * range.ColCount();
    if (count <= 0 || count > 100000) return words;
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) words.push_back(CellWords(sheet, row, col));
    }
    return words;
}

bool IsFilled(const SpreadsheetSheet& sheet, int row, int col) {
    const SpreadsheetCell* cell = sheet.GetCellIfExists(row, col);
    return cell && !cell->IsEmpty();
}

// A block of one cell grown to the cells in use around it (the cells
// touching it, and theirs, as a spreadsheet's "current region").
CellRange CurrentRegion(const SpreadsheetSheet& sheet, CellRange range) {
    for (int pass = 0; pass < 10000; ++pass) {
        bool grew = false;
        auto filledRow = [&](int row) {
            if (row < 0 || row >= SpreadsheetLimits::MaxRows) return false;
            for (int col = std::max(0, range.start.col - 1); col <= range.end.col + 1; ++col)
                if (IsFilled(sheet, row, col)) return true;
            return false;
        };
        auto filledCol = [&](int col) {
            if (col < 0 || col >= SpreadsheetLimits::MaxColumns) return false;
            for (int row = std::max(0, range.start.row - 1); row <= range.end.row + 1; ++row)
                if (IsFilled(sheet, row, col)) return true;
            return false;
        };
        if (filledRow(range.start.row - 1)) { --range.start.row; grew = true; }
        if (filledRow(range.end.row + 1)) { ++range.end.row; grew = true; }
        if (filledCol(range.start.col - 1)) { --range.start.col; grew = true; }
        if (filledCol(range.end.col + 1)) { ++range.end.col; grew = true; }
        if (!grew) break;
    }
    return range;
}

int VisibleColumnWidth(const SpreadsheetSheet& sheet, int col) {
    return sheet.IsColumnHidden(col) ? 0 : sheet.GetColumnWidth(col);
}

int VisibleRowHeight(const SpreadsheetSheet& sheet, int row) {
    return sheet.IsRowHidden(row) ? 0 : sheet.GetRowHeight(row);
}

int ColumnLeft(const SpreadsheetSheet& sheet, int col) {
    int x = 0;
    for (int c = 0; c < col; ++c) x += VisibleColumnWidth(sheet, c);
    return x;
}

int RowTop(const SpreadsheetSheet& sheet, int row) {
    int y = 0;
    for (int r = 0; r < row; ++r) y += VisibleRowHeight(sheet, r);
    return y;
}

// Moves `index` for `count` lines inserted at `at` (removed where negative);
// a removed line's index goes to where they were.
void MoveIndex(int& index, int at, int count) {
    if (count > 0) {
        if (index >= at) index += count;
        return;
    }
    const int removed = -count;
    if (index < at) return;
    if (index >= at + removed) index -= removed;
    else index = at;
    index = std::max(0, index);
}

std::vector<SpreadsheetChartChoice> MakeChoices() {
    using Kind = RichChart::Kind;
    return {{Kind::Column, false}, {Kind::Column, true}, {Kind::Bar, false}, {Kind::Bar, true},
            {Kind::Line, false},   {Kind::Area, false},  {Kind::Area, true}, {Kind::Pie, false},
            {Kind::Doughnut, false}, {Kind::Scatter, false}, {Kind::Radar, false}};
}

// The handle of `box` under `x`, `y`, within the slide editor's reach.
SlideHandle HandleOf(const Rect2Di& box, int x, int y) {
    const double reach = UCSlideDeckEditor::kHandleReach;
    auto near = [&](double hx, double hy) { return std::fabs(x - hx) <= reach && std::fabs(y - hy) <= reach; };
    const double l = box.x, r = box.x + box.width, t = box.y, b = box.y + box.height;
    const double cx = (l + r) / 2.0, cy = (t + b) / 2.0;
    if (near(l, t)) return SlideHandle::TopLeft;
    if (near(r, t)) return SlideHandle::TopRight;
    if (near(r, b)) return SlideHandle::BottomRight;
    if (near(l, b)) return SlideHandle::BottomLeft;
    if (near(cx, t)) return SlideHandle::Top;
    if (near(r, cy)) return SlideHandle::Right;
    if (near(cx, b)) return SlideHandle::Bottom;
    if (near(l, cy)) return SlideHandle::Left;
    return SlideHandle::NoHandle;
}

UCMouseCursor CursorOf(SlideHandle handle) {
    switch (handle) {
        case SlideHandle::TopLeft: case SlideHandle::BottomRight: return UCMouseCursor::SizeNWSE;
        case SlideHandle::TopRight: case SlideHandle::BottomLeft: return UCMouseCursor::SizeNESW;
        case SlideHandle::Top: case SlideHandle::Bottom: return UCMouseCursor::SizeNS;
        case SlideHandle::Left: case SlideHandle::Right: return UCMouseCursor::SizeWE;
        default: return UCMouseCursor::SizeAll;
    }
}

} // namespace

// ============================================================================
// RANGES
// ============================================================================

std::string SpreadsheetChartRange::ToExcel() const {
    if (!IsSet()) return std::string();
    std::string out = QuotedSheet(sheet) + "!" + AbsoluteCorner(range.start.row, range.start.col);
    if (!range.IsSingleCell()) out += ":" + AbsoluteCorner(range.end.row, range.end.col);
    return out;
}

std::string SpreadsheetChartRange::ToOdf() const {
    if (!IsSet()) return std::string();
    std::string out = QuotedSheet(sheet) + "." + AbsoluteCorner(range.start.row, range.start.col);
    if (!range.IsSingleCell()) out += ":." + AbsoluteCorner(range.end.row, range.end.col);
    return out;
}

bool SpreadsheetChartRange::FromExcel(const std::string& given, SpreadsheetChartRange& out) {
    std::string text = given;
    if (!text.empty() && text[0] == '=') text.erase(0, 1);
    if (text.size() > 1 && text.front() == '(' && text.back() == ')') text = text.substr(1, text.size() - 2);
    size_t at = 0;
    std::string sheet;
    if (!ReadSheetName(text, at, "!", sheet) || at >= text.size() || text[at] != '!') return false;
    ++at;
    int row1 = 0, col1 = 0, row2 = 0, col2 = 0;
    if (!ReadCorner(text, at, row1, col1)) return false;
    row2 = row1;
    col2 = col1;
    if (at < text.size() && text[at] == ':') {
        ++at;
        // ("Sheet1!A1:Sheet1!B2" names the sheet again.)
        const size_t bang = text.find('!', at);
        if (bang != std::string::npos) at = bang + 1;
        if (!ReadCorner(text, at, row2, col2)) return false;
    }
    if (at != text.size()) return false;
    out.sheet = sheet;
    out.range = CellRange(row1, col1, row2, col2);
    return true;
}

bool SpreadsheetChartRange::FromOdf(const std::string& given, SpreadsheetChartRange& out) {
    std::string text = given;
    // (A list of ranges: the first - a space outside a quoted sheet name ends it.)
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\'') quoted = !quoted;
        else if (text[i] == ' ' && !quoted) { text.resize(i); break; }
    }
    size_t at = 0;
    if (at < text.size() && text[at] == '$') ++at;
    std::string sheet;
    if (!ReadSheetName(text, at, ".", sheet) || at >= text.size() || text[at] != '.') return false;
    ++at;
    int row1 = 0, col1 = 0, row2 = 0, col2 = 0;
    if (!ReadCorner(text, at, row1, col1)) return false;
    row2 = row1;
    col2 = col1;
    if (at < text.size() && text[at] == ':') {
        ++at;
        if (at < text.size() && text[at] == '$' && at + 1 < text.size() &&
            !std::isalpha(static_cast<unsigned char>(text[at + 1])) && text[at + 1] != '\'') {
            // "$." is not a sheet; nothing to skip.
        }
        // The second corner may name the sheet again ("Sheet1.B5", ".B5", "$Sheet1.$B$5").
        const size_t dot = text.find('.', at);
        if (dot != std::string::npos) at = dot + 1;
        if (!ReadCorner(text, at, row2, col2)) return false;
    }
    if (at != text.size()) return false;
    out.sheet = sheet;
    out.range = CellRange(row1, col1, row2, col2);
    return true;
}

// ============================================================================
// THE MODEL
// ============================================================================

void TouchSpreadsheetChart(SpreadsheetChart& chart) {
    static std::atomic<uint64_t> next{1};
    chart.revision = next++;
}

bool SpreadsheetChartCellNumber(const SpreadsheetSheet& sheet, int row, int col, double& value) {
    const SpreadsheetCell* cell = sheet.GetCellIfExists(row, col);
    if (!cell) return false;
    const CellValueVariant& raw = cell->GetRawValue();
    if (const double* number = std::get_if<double>(&raw)) {
        value = *number;
        return std::isfinite(value);
    }
    if (const CurrencyValue* money = std::get_if<CurrencyValue>(&raw)) {
        value = money->amount;
        return std::isfinite(value);
    }
    return false;
}

SpreadsheetChart MakeSpreadsheetChart(const SpreadsheetSheet& sheet, const CellRange& given, RichChart::Kind kind,
                                      bool stacked, SpreadsheetChartSeriesIn seriesIn) {
    SpreadsheetChart made;
    made.chart.kind = kind;
    made.chart.stacked = stacked;
    CellRange range = given;
    range.Normalize();
    if (range.IsSingleCell()) range = CurrentRegion(sheet, range);
    // A whole column or row selected: only the part in use.
    const CellRange used = sheet.GetUsedRange();
    if (used.IsValid() && used.end.row >= 0 && used.end.col >= 0) {
        range.end.row = std::min(range.end.row, used.end.row);
        range.end.col = std::min(range.end.col, used.end.col);
        range.start.row = std::min(range.start.row, range.end.row);
        range.start.col = std::min(range.start.col, range.end.col);
    }
    made.dataRange.sheet = sheet.GetName();
    made.dataRange.range = range;
    const int rows = range.RowCount(), cols = range.ColCount();
    made.seriesInRows = seriesIn == SpreadsheetChartSeriesIn::Rows ||
                        (seriesIn == SpreadsheetChartSeriesIn::Automatic && cols > rows);
    SetSpreadsheetChartSeriesInRows(made, sheet, made.seriesInRows);

    // Right of the block, a column between, the size a new chart has.
    const int left = ColumnLeft(sheet, std::min(range.end.col + 2, SpreadsheetLimits::MaxColumns - 1));
    const int top = RowTop(sheet, range.start.row);
    made.anchor = SpreadsheetChartAnchorForBox(sheet, Rect2Di(left, top, kDefaultChartWidth, kDefaultChartHeight));
    made.chart.legend = made.sources.size() > 1 || kind == RichChart::Kind::Pie || kind == RichChart::Kind::Doughnut;
    const std::string name = sheet.GetName();
    RefreshSpreadsheetChart(made, [&sheet, name](const std::string& wanted) {
        return SameNameIgnoringCase(wanted, name) ? &sheet : nullptr;
    });
    // One series: the chart is called what it is.
    if (made.chart.series.size() == 1 && made.sources.front().name.IsSet()) made.chart.title = made.chart.series.front().name;
    TouchSpreadsheetChart(made);
    return made;
}

bool SetSpreadsheetChartSeriesInRows(SpreadsheetChart& chart, const SpreadsheetSheet& sheet, bool inRows) {
    if (!chart.dataRange.IsSet()) return false;
    const CellRange range = chart.dataRange.range;
    const std::string sheetName = chart.dataRange.sheet;
    chart.seriesInRows = inRows;
    // Read across: `major` runs along the series, `minor` across them.
    auto at = [&](int major, int minor) { return inRows ? std::make_pair(major, minor) : std::make_pair(minor, major); };
    const int majorFirst = inRows ? range.start.row : range.start.col;
    const int majorLast = inRows ? range.end.row : range.end.col;
    const int minorFirst = inRows ? range.start.col : range.start.row;
    const int minorLast = inRows ? range.end.col : range.end.row;
    auto numeric = [&](int major, int minor) {
        const auto [row, col] = at(major, minor);
        double value = 0.0;
        return SpreadsheetChartCellNumber(sheet, row, col, value);
    };
    auto filled = [&](int major, int minor) {
        const auto [row, col] = at(major, minor);
        return IsFilled(sheet, row, col);
    };
    // Names: the first line across the series holds no numbers (past the
    // corner) and some words. Categories: the first series' line does.
    auto wordsOnly = [&](bool alongMinor, int fixed, int from, int to) {
        bool any = false;
        for (int i = from; i <= to; ++i) {
            const int major = alongMinor ? fixed : i, minor = alongMinor ? i : fixed;
            if (numeric(major, minor)) return false;
            any = any || filled(major, minor);
        }
        return any;
    };
    const bool lengthy = minorLast > minorFirst, several = majorLast > majorFirst;
    bool names = lengthy && wordsOnly(false, minorFirst, majorFirst + (several ? 1 : 0), majorLast);
    bool categories = several && wordsOnly(true, majorFirst, minorFirst + (names ? 1 : 0), minorLast);
    // An empty corner over words both ways: both.
    if (!names && !categories && lengthy && several && !filled(majorFirst, minorFirst)) {
        names = wordsOnly(false, minorFirst, majorFirst + 1, majorLast);
        categories = wordsOnly(true, majorFirst, minorFirst + 1, minorLast);
    }
    const int dataMinorFirst = minorFirst + (names ? 1 : 0);
    int firstSeries = majorFirst + (categories ? 1 : 0);
    auto line = [&](int major, int from, int to) {
        SpreadsheetChartRange part;
        part.sheet = sheetName;
        if (inRows) part.range = CellRange(major, from, major, to);
        else part.range = CellRange(from, major, to, major);
        return part;
    };
    SpreadsheetChartRange categoryRange;
    if (categories) categoryRange = line(majorFirst, dataMinorFirst, minorLast);
    // A scatter chart: the first line of numbers is the x values.
    if (chart.chart.kind == RichChart::Kind::Scatter && !categories && firstSeries < majorLast) {
        categoryRange = line(firstSeries, dataMinorFirst, minorLast);
        ++firstSeries;
    } else if (chart.chart.kind == RichChart::Kind::Scatter && categories) {
        categoryRange = SpreadsheetChartRange();   // words are no x values
    }
    std::vector<SpreadsheetChartSeries> sources;
    for (int major = firstSeries; major <= majorLast && sources.size() < 255; ++major) {
        SpreadsheetChartSeries one;
        if (names) {
            one.name.sheet = sheetName;
            const auto [row, col] = at(major, minorFirst);
            one.name.range = CellRange(row, col, row, col);
        }
        one.categories = categoryRange;
        one.values = line(major, dataMinorFirst, minorLast);
        sources.push_back(std::move(one));
    }
    // The series' look by position; new ones from the palette.
    std::vector<RichChartSeries> looks = chart.chart.series;
    looks.resize(sources.size());
    for (size_t i = 0; i < looks.size(); ++i) {
        looks[i].values.clear();
        looks[i].xValues.clear();
        looks[i].name = TrF("Series {0}", static_cast<int>(i + 1));
        if (chart.chart.kind == RichChart::Kind::Line || chart.chart.kind == RichChart::Kind::Scatter) looks[i].marked = true;
    }
    chart.chart.series = std::move(looks);
    chart.chart.categories.clear();
    chart.sources = std::move(sources);
    TouchSpreadsheetChart(chart);
    return true;
}

bool RefreshSpreadsheetChart(SpreadsheetChart& chart, const SpreadsheetSheetLookup& sheets) {
    if (chart.sources.empty()) return false;
    RichChart fresh = chart.chart;
    fresh.series.resize(chart.sources.size());
    const bool scatter = fresh.kind == RichChart::Kind::Scatter || fresh.kind == RichChart::Kind::Bubble;
    auto sheetOf = [&](const SpreadsheetChartRange& part) -> const SpreadsheetSheet* {
        return part.IsSet() && sheets ? sheets(part.sheet) : nullptr;
    };
    // The categories: the first series' (a scatter chart's are each series' x).
    const SpreadsheetChartRange& names = chart.sources.front().categories;
    if (!scatter && names.IsSet()) {
        const SpreadsheetSheet* sheet = sheetOf(names);
        fresh.categories = sheet ? RangeWords(*sheet, names.range) : std::vector<std::string>();
    }
    for (size_t i = 0; i < chart.sources.size(); ++i) {
        const SpreadsheetChartSeries& source = chart.sources[i];
        RichChartSeries& series = fresh.series[i];
        if (source.name.IsSet()) {
            const SpreadsheetSheet* sheet = sheetOf(source.name);
            series.name = sheet ? CellWords(*sheet, source.name.range.start.row, source.name.range.start.col) : std::string();
        }
        if (source.values.IsSet()) {
            const SpreadsheetSheet* sheet = sheetOf(source.values);
            series.values = sheet ? RangeNumbers(*sheet, source.values.range) : std::vector<double>();
        }
        if (scatter && source.categories.IsSet()) {
            const SpreadsheetSheet* sheet = sheetOf(source.categories);
            series.xValues = sheet ? RangeNumbers(*sheet, source.categories.range) : std::vector<double>();
            if (series.xValues.size() != series.values.size()) series.xValues.resize(series.values.size(), 0.0);
        }
    }
    bool changed = fresh.categories != chart.chart.categories || fresh.series.size() != chart.chart.series.size();
    for (size_t i = 0; !changed && i < fresh.series.size(); ++i) {
        const RichChartSeries& a = fresh.series[i];
        const RichChartSeries& b = chart.chart.series[i];
        changed = a.name != b.name || a.values != b.values || a.xValues != b.xValues;
    }
    if (!changed) return false;
    chart.chart = std::move(fresh);
    TouchSpreadsheetChart(chart);
    return true;
}

Rect2Di SpreadsheetChartBox(const SpreadsheetSheet& sheet, const SpreadsheetChartAnchor& anchor) {
    const int fromCol = std::clamp(anchor.from.col, 0, SpreadsheetLimits::MaxColumns - 1);
    const int fromRow = std::clamp(anchor.from.row, 0, SpreadsheetLimits::MaxRows - 1);
    const int toCol = std::clamp(anchor.to.col, 0, SpreadsheetLimits::MaxColumns - 1);
    const int toRow = std::clamp(anchor.to.row, 0, SpreadsheetLimits::MaxRows - 1);
    const int x0 = ColumnLeft(sheet, fromCol) + std::clamp(anchor.fromOffsetX, 0, VisibleColumnWidth(sheet, fromCol));
    const int y0 = RowTop(sheet, fromRow) + std::clamp(anchor.fromOffsetY, 0, VisibleRowHeight(sheet, fromRow));
    const int x1 = ColumnLeft(sheet, toCol) + std::clamp(anchor.toOffsetX, 0, VisibleColumnWidth(sheet, toCol));
    const int y1 = RowTop(sheet, toRow) + std::clamp(anchor.toOffsetY, 0, VisibleRowHeight(sheet, toRow));
    return Rect2Di(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

SpreadsheetChartAnchor SpreadsheetChartAnchorForBox(const SpreadsheetSheet& sheet, const Rect2Di& box) {
    // The cell a sheet pixel falls in, and how far into it.
    auto column = [&](int x, int& col, int& offset) {
        x = std::max(0, x);
        int left = 0;
        for (col = 0; col < SpreadsheetLimits::MaxColumns - 1; ++col) {
            const int width = VisibleColumnWidth(sheet, col);
            if (width > 0 && x < left + width) break;
            left += width;
        }
        offset = std::max(0, x - left);
    };
    auto row = [&](int y, int& r, int& offset) {
        y = std::max(0, y);
        int top = 0;
        for (r = 0; r < SpreadsheetLimits::MaxRows - 1; ++r) {
            const int height = VisibleRowHeight(sheet, r);
            if (height > 0 && y < top + height) break;
            top += height;
        }
        offset = std::max(0, y - top);
    };
    SpreadsheetChartAnchor anchor;
    column(box.x, anchor.from.col, anchor.fromOffsetX);
    row(box.y, anchor.from.row, anchor.fromOffsetY);
    column(box.x + std::max(0, box.width), anchor.to.col, anchor.toOffsetX);
    row(box.y + std::max(0, box.height), anchor.to.row, anchor.toOffsetY);
    return anchor;
}

bool DrawSpreadsheetChart(IRenderContext* ctx, const SpreadsheetChart& chart, const Rect2Dd& target,
                          std::shared_ptr<UltraCanvasChartEngineElement>* cache) {
    if (!ctx || target.width < 4 || target.height < 4) return false;
    ctx->PushState();
    // The chart area: white, with a thin border, as a spreadsheet draws one.
    ctx->SetFillPaint(Colors::White);
    ctx->FillRectangle(target);
    ctx->SetStrokePaint(kChartBorder);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRectangle(Rect2Dd(target.x + 0.5, target.y + 0.5, target.width - 1.0, target.height - 1.0));
    const double inset = 4.0;
    const Rect2Dd inner(target.x + inset, target.y + inset, target.width - 2 * inset, target.height - 2 * inset);
    bool drawn = false;
    if (inner.width >= 8 && inner.height >= 8) {
        ctx->ClipRect(target);   // (an axis's end label may stand a little past the plot)
        const Size2Di natural(static_cast<int>(std::lround(inner.width)), static_cast<int>(std::lround(inner.height)));
        drawn = DrawRichDocumentChart(ctx, chart.chart, inner, natural, cache);
    }
    ctx->PopState();
    return drawn;
}

void RenameSpreadsheetChartSheet(std::vector<SpreadsheetChart>& charts, const std::string& oldName,
                                 const std::string& newName) {
    auto rename = [&](SpreadsheetChartRange& part) {
        if (part.IsSet() && SameNameIgnoringCase(part.sheet, oldName)) part.sheet = newName;
    };
    for (SpreadsheetChart& chart : charts) {
        rename(chart.dataRange);
        for (SpreadsheetChartSeries& source : chart.sources) {
            rename(source.name);
            rename(source.categories);
            rename(source.values);
        }
    }
}

void MoveSpreadsheetChartRanges(std::vector<SpreadsheetChart>& charts, const std::string& sheetName, bool columns,
                                int at, int count) {
    if (count == 0) return;
    auto move = [&](SpreadsheetChartRange& part) {
        if (!part.IsSet() || !SameNameIgnoringCase(part.sheet, sheetName)) return;
        int& first = columns ? part.range.start.col : part.range.start.row;
        int& last = columns ? part.range.end.col : part.range.end.row;
        if (count < 0 && first >= at && last < at - count) return;   // the whole range went: left as it was
        MoveIndex(first, at, count);
        if (count < 0 && last >= at && last < at - count) last = at - 1;   // its end went: up to what remains
        else MoveIndex(last, at, count);
        if (last < first) last = first;
    };
    for (SpreadsheetChart& chart : charts) {
        bool moved = false;
        auto track = [&](SpreadsheetChartRange& part) {
            const SpreadsheetChartRange before = part;
            move(part);
            moved = moved || part != before;
        };
        track(chart.dataRange);
        for (SpreadsheetChartSeries& source : chart.sources) {
            track(source.name);
            track(source.categories);
            track(source.values);
        }
        if (moved) TouchSpreadsheetChart(chart);
    }
}

void MoveSpreadsheetChartAnchors(std::vector<SpreadsheetChart>& charts, bool columns, int at, int count) {
    if (count == 0) return;
    for (SpreadsheetChart& chart : charts) {
        int& from = columns ? chart.anchor.from.col : chart.anchor.from.row;
        int& to = columns ? chart.anchor.to.col : chart.anchor.to.row;
        MoveIndex(from, at, count);
        MoveIndex(to, at, count);
        if (to < from) to = from;
    }
}

const std::vector<SpreadsheetChartChoice>& SpreadsheetChartChoices() {
    static const std::vector<SpreadsheetChartChoice> choices = MakeChoices();
    return choices;
}

std::string SpreadsheetChartChoiceName(size_t index) {
    switch (index) {
        case 0: return TrC("chart kind", "Columns");
        case 1: return TrC("chart kind", "Columns, stacked");
        case 2: return TrC("chart kind", "Bars");
        case 3: return TrC("chart kind", "Bars, stacked");
        case 4: return TrC("chart kind", "Line");
        case 5: return TrC("chart kind", "Area");
        case 6: return TrC("chart kind", "Area, stacked");
        case 7: return TrC("chart kind", "Pie");
        case 8: return TrC("chart kind", "Doughnut");
        case 9: return TrC("chart kind", "Scatter");
        case 10: return TrC("chart kind", "Radar");
        default: return std::string();
    }
}

int SpreadsheetChartChoiceOf(const RichChart& chart) {
    const auto& choices = SpreadsheetChartChoices();
    const bool stackable = chart.kind == RichChart::Kind::Column || chart.kind == RichChart::Kind::Bar ||
                           chart.kind == RichChart::Kind::Area;
    for (size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].kind == chart.kind && (!stackable || choices[i].stacked == (chart.stacked || chart.percent)))
            return static_cast<int>(i);
    }
    return -1;
}

// ============================================================================
// THE GRID'S CHARTS
// ============================================================================

int UltraCanvasSpreadsheet::GetChartCount() const {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    return sheet ? static_cast<int>(sheet->GetCharts().size()) : 0;
}

const SpreadsheetChart* UltraCanvasSpreadsheet::GetChart(int index) const {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetCharts().size())) return nullptr;
    return &sheet->GetCharts()[static_cast<size_t>(index)];
}

int UltraCanvasSpreadsheet::GetSelectedChart() const {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || selectedChart_ < 0 || selectedChartSheet_ != sheet->GetId() ||
        selectedChart_ >= static_cast<int>(sheet->GetCharts().size())) {
        return -1;
    }
    return selectedChart_;
}

void UltraCanvasSpreadsheet::SelectChart(int index) {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetCharts().size())) index = -1;
    const int was = GetSelectedChart();
    selectedChart_ = index;
    selectedChartSheet_ = sheet ? sheet->GetId() : 0;
    if (was == index) return;
    Invalidate();
    if (onChartSelectionChange) onChartSelectionChange(index);
}

bool UltraCanvasSpreadsheet::RefreshCharts() {
    if (formulaEngine_ && formulaEngine_->HasPendingChanges()) formulaEngine_->Recalculate();
    const SpreadsheetSheetLookup lookup = [this](const std::string& name) -> const SpreadsheetSheet* {
        for (const auto& sheet : sheets_) {
            if (sheet && SameNameIgnoringCase(sheet->GetName(), name)) return sheet.get();
        }
        return nullptr;
    };
    bool changed = false;
    for (auto& sheet : sheets_) {
        if (!sheet) continue;
        for (SpreadsheetChart& chart : sheet->GetCharts()) changed = RefreshSpreadsheetChart(chart, lookup) || changed;
    }
    chartsDirty_ = false;
    if (changed) Invalidate();
    return changed;
}

void UltraCanvasSpreadsheet::RecordChartStep(SpreadsheetSheet& sheet, std::vector<SpreadsheetChart> before,
                                             const std::string& description) {
    if (recordingUndo_) {
        SpreadsheetUndoAction action;
        action.type = UndoActionType::ChartChange;
        action.description = description;
        action.sheetName = sheet.GetName();
        action.sheetId = sheet.GetId();
        action.chartData = std::move(before);
        PushUndo(action);
    }
    Invalidate();
    if (onChartsChange) onChartsChange();
}

void UltraCanvasSpreadsheet::ApplyChartStep(SpreadsheetUndoAction& action) {
    SpreadsheetSheet* sheet = FindUndoSheet(action);
    if (!sheet) return;
    std::swap(sheet->GetCharts(), action.chartData);
    chartsDirty_ = true;
    if (GetSelectedChart() < 0) SelectChart(-1);
    Invalidate();
    if (onChartsChange) onChartsChange();
}

int UltraCanvasSpreadsheet::InsertChart(const SpreadsheetChart& chart) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return -1;
    if (IsEditing()) StopEditing(true);
    std::vector<SpreadsheetChart> before = sheet->GetCharts();
    SpreadsheetChart added = chart;
    if (added.name.empty()) {
        // "Chart N", N one past the highest in use.
        int highest = 0;
        for (const SpreadsheetChart& other : before) {
            if (other.name.rfind("Chart ", 0) == 0) highest = std::max(highest, std::atoi(other.name.c_str() + 6));
        }
        added.name = "Chart " + std::to_string(highest + 1);
    }
    TouchSpreadsheetChart(added);
    sheet->GetCharts().push_back(std::move(added));
    RefreshCharts();
    RecordChartStep(*sheet, std::move(before), "Insert chart");
    const int index = static_cast<int>(sheet->GetCharts().size()) - 1;
    SelectChart(index);
    return index;
}

int UltraCanvasSpreadsheet::InsertChartFromSelection(RichChart::Kind kind, bool stacked, SpreadsheetChartSeriesIn seriesIn) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return -1;
    if (IsEditing()) StopEditing(true);
    if (formulaEngine_ && formulaEngine_->HasPendingChanges()) formulaEngine_->Recalculate();
    SpreadsheetChart chart = MakeSpreadsheetChart(*sheet, GetSelection(), kind, stacked, seriesIn);
    // Something to plot: a number in some series' cells.
    bool anyNumber = false;
    for (const SpreadsheetChartSeries& source : chart.sources) {
        const CellRange& cells = source.values.range;
        for (int row = cells.start.row; row <= cells.end.row && !anyNumber; ++row) {
            for (int col = cells.start.col; col <= cells.end.col && !anyNumber; ++col) {
                double value = 0.0;
                anyNumber = SpreadsheetChartCellNumber(*sheet, row, col, value);
            }
        }
        if (anyNumber) break;
    }
    if (!anyNumber) return -1;
    return InsertChart(chart);
}

bool UltraCanvasSpreadsheet::SetChart(int index, const SpreadsheetChart& chart) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetCharts().size())) return false;
    std::vector<SpreadsheetChart> before = sheet->GetCharts();
    SpreadsheetChart changed = chart;
    TouchSpreadsheetChart(changed);
    sheet->GetCharts()[static_cast<size_t>(index)] = std::move(changed);
    RefreshCharts();
    RecordChartStep(*sheet, std::move(before), "Change chart");
    return true;
}

bool UltraCanvasSpreadsheet::RemoveChart(int index) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetCharts().size())) return false;
    std::vector<SpreadsheetChart> before = sheet->GetCharts();
    sheet->GetCharts().erase(sheet->GetCharts().begin() + index);
    SelectChart(-1);
    RecordChartStep(*sheet, std::move(before), "Delete chart");
    return true;
}

Rect2Di UltraCanvasSpreadsheet::ChartScreenRect(const SpreadsheetChart& chart) const {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return Rect2Di(0, 0, 0, 0);
    const SpreadsheetChartAnchor& anchor = chart.anchor;
    const Point2Di from = CellToScreen(anchor.from.row, anchor.from.col);
    const Point2Di to = CellToScreen(anchor.to.row, anchor.to.col);
    const int x0 = from.x + std::clamp(anchor.fromOffsetX, 0, VisibleColumnWidth(*sheet, anchor.from.col));
    const int y0 = from.y + std::clamp(anchor.fromOffsetY, 0, VisibleRowHeight(*sheet, anchor.from.row));
    const int x1 = to.x + std::clamp(anchor.toOffsetX, 0, VisibleColumnWidth(*sheet, anchor.to.col));
    const int y1 = to.y + std::clamp(anchor.toOffsetY, 0, VisibleRowHeight(*sheet, anchor.to.row));
    return Rect2Di(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

Rect2Di UltraCanvasSpreadsheet::GetChartBounds(int index) const {
    const SpreadsheetChart* chart = GetChart(index);
    return chart ? ChartScreenRect(*chart) : Rect2Di(0, 0, 0, 0);
}

int UltraCanvasSpreadsheet::ChartAt(int x, int y) const {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || !CellAreaRect().Contains(static_cast<float>(x), static_cast<float>(y))) return -1;
    const auto& charts = sheet->GetCharts();
    for (int i = static_cast<int>(charts.size()) - 1; i >= 0; --i) {   // the topmost first
        const Rect2Di box = ChartScreenRect(charts[static_cast<size_t>(i)]);
        if (x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height) return i;
    }
    return -1;
}

void UltraCanvasSpreadsheet::RenderCharts(IRenderContext* ctx) {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!ctx || !sheet || sheet->GetCharts().empty()) {
        chartCache_.clear();
        return;
    }
    if (chartsDirty_) RefreshCharts();
    const Rect2Df area = CellAreaRect();
    ctx->PushState();
    ctx->ClipRect(area);
    std::set<uint64_t> drawn;
    const auto& charts = sheet->GetCharts();
    for (const SpreadsheetChart& chart : charts) {
        const Rect2Di box = ChartScreenRect(chart);
        if (box.width < 4 || box.height < 4) continue;
        if (box.x > area.x + area.width || box.y > area.y + area.height || box.x + box.width < area.x ||
            box.y + box.height < area.y) {
            continue;
        }
        drawn.insert(chart.revision);
        DrawSpreadsheetChart(ctx, chart, Rect2Dd(box.x, box.y, box.width, box.height), &chartCache_[chart.revision]);
    }
    // The selected chart's frame and its eight handles, as the slide editor draws them.
    const int selected = GetSelectedChart();
    if (selected >= 0) {
        const Rect2Di box = ChartScreenRect(charts[static_cast<size_t>(selected)]);
        ctx->SetStrokePaint(selectionBorderColor_);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawRectangle(Rect2Dd(box.x, box.y, box.width, box.height));
        const double l = box.x, r = box.x + box.width, t = box.y, b = box.y + box.height;
        const double cx = (l + r) / 2.0, cy = (t + b) / 2.0;
        ctx->SetStrokeWidth(1.0f);
        for (const Point2Dd& p : {Point2Dd(l, t), Point2Dd(r, t), Point2Dd(r, b), Point2Dd(l, b), Point2Dd(cx, t),
                                  Point2Dd(r, cy), Point2Dd(cx, b), Point2Dd(l, cy)}) {
            const Rect2Dd handle(p.x - kHandleSize / 2, p.y - kHandleSize / 2, kHandleSize, kHandleSize);
            ctx->SetFillPaint(Colors::White);
            ctx->FillRectangle(handle);
            ctx->SetStrokePaint(selectionBorderColor_);
            ctx->DrawRectangle(handle);
        }
    }
    ctx->PopState();
    // The drawings of charts no longer shown (or changed since) go.
    for (auto it = chartCache_.begin(); it != chartCache_.end();) {
        if (drawn.count(it->first)) ++it;
        else it = chartCache_.erase(it);
    }
}

bool UltraCanvasSpreadsheet::HandleChartEvent(const UCEvent& event) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return false;
    auto& charts = sheet->GetCharts();
    const int selected = GetSelectedChart();
    const int x = event.pointer.x, y = event.pointer.y;
    switch (event.type) {
        case UCEventType::MouseDown: {
            if (IsEditing() || charts.empty()) return false;
            SlideHandle handle = SlideHandle::NoHandle;
            if (selected >= 0 && event.button == UCMouseButton::Left)
                handle = HandleOf(ChartScreenRect(charts[static_cast<size_t>(selected)]), x, y);
            const int hit = handle != SlideHandle::NoHandle ? selected : ChartAt(x, y);
            if (hit < 0) {
                if (selected >= 0) SelectChart(-1);
                return false;
            }
            SetFocus(true);
            SelectChart(hit);
            if (event.button != UCMouseButton::Left) return true;
            chartDrag_ = handle != SlideHandle::NoHandle ? 2 : 1;
            chartHandle_ = static_cast<int>(handle);
            chartDragFrom_ = Point2Di(x, y);
            chartDragBox_ = SpreadsheetChartBox(*sheet, charts[static_cast<size_t>(hit)].anchor);
            chartsBeforeDrag_ = charts;
            return true;
        }
        case UCEventType::MouseMove: {
            if (chartDrag_ != 0 && selected >= 0) {
                const int dx = x - chartDragFrom_.x, dy = y - chartDragFrom_.y;
                Rect2Di box = chartDragBox_;
                if (chartDrag_ == 1) {
                    box.x = std::max(0, box.x + dx);
                    box.y = std::max(0, box.y + dy);
                } else {
                    const SlideHandle handle = static_cast<SlideHandle>(chartHandle_);
                    int left = box.x, top = box.y, right = box.x + box.width, bottom = box.y + box.height;
                    if (handle == SlideHandle::Left || handle == SlideHandle::TopLeft || handle == SlideHandle::BottomLeft)
                        left = std::clamp(left + dx, 0, right - kMinimumChartSide);
                    if (handle == SlideHandle::Right || handle == SlideHandle::TopRight || handle == SlideHandle::BottomRight)
                        right = std::max(left + kMinimumChartSide, right + dx);
                    if (handle == SlideHandle::Top || handle == SlideHandle::TopLeft || handle == SlideHandle::TopRight)
                        top = std::clamp(top + dy, 0, bottom - kMinimumChartSide);
                    if (handle == SlideHandle::Bottom || handle == SlideHandle::BottomLeft || handle == SlideHandle::BottomRight)
                        bottom = std::max(top + kMinimumChartSide, bottom + dy);
                    box = Rect2Di(left, top, right - left, bottom - top);
                }
                charts[static_cast<size_t>(selected)].anchor = SpreadsheetChartAnchorForBox(*sheet, box);
                SetMouseCursor(chartDrag_ == 1 ? UCMouseCursor::SizeAll : CursorOf(static_cast<SlideHandle>(chartHandle_)));
                Invalidate();
                return true;
            }
            if (mouseDown_ || IsEditing()) return false;
            if (selected >= 0) {
                const SlideHandle handle = HandleOf(ChartScreenRect(charts[static_cast<size_t>(selected)]), x, y);
                if (handle != SlideHandle::NoHandle) {
                    SetMouseCursor(CursorOf(handle));
                    return true;
                }
            }
            if (ChartAt(x, y) >= 0) {
                SetMouseCursor(UCMouseCursor::SizeAll);
                return true;
            }
            return false;
        }
        case UCEventType::MouseUp: {
            if (chartDrag_ == 0) return false;
            const bool resized = chartDrag_ == 2;
            chartDrag_ = 0;
            std::vector<SpreadsheetChart> before = std::move(chartsBeforeDrag_);
            chartsBeforeDrag_.clear();
            if (selected >= 0 && selected < static_cast<int>(before.size())) {
                const SpreadsheetChartAnchor& was = before[static_cast<size_t>(selected)].anchor;
                const SpreadsheetChartAnchor& now = charts[static_cast<size_t>(selected)].anchor;
                const bool moved = was.from != now.from || was.to != now.to || was.fromOffsetX != now.fromOffsetX ||
                                   was.fromOffsetY != now.fromOffsetY || was.toOffsetX != now.toOffsetX ||
                                   was.toOffsetY != now.toOffsetY;
                if (moved) RecordChartStep(*sheet, std::move(before), resized ? "Resize chart" : "Move chart");
            }
            return true;
        }
        case UCEventType::MouseDoubleClick: {
            if (IsEditing()) return false;
            const int hit = ChartAt(x, y);
            if (hit < 0) return false;
            SelectChart(hit);
            if (onChartDoubleClick) onChartDoubleClick(hit);
            else ShowChartPropertiesDialog(hit);
            return true;
        }
        case UCEventType::KeyDown: {
            if (selected < 0 || IsEditing()) return false;
            switch (event.virtualKey) {
                case UCKeys::Delete:
                case UCKeys::Backspace:
                    RemoveChart(selected);
                    return true;
                case UCKeys::Escape:
                    SelectChart(-1);
                    return true;
                case UCKeys::Left: case UCKeys::Right: case UCKeys::Up: case UCKeys::Down: {
                    // A nudge: a pixel, ten with Shift.
                    const int step = event.shift ? 10 : 1;
                    Rect2Di box = SpreadsheetChartBox(*sheet, charts[static_cast<size_t>(selected)].anchor);
                    if (event.virtualKey == UCKeys::Left) box.x = std::max(0, box.x - step);
                    if (event.virtualKey == UCKeys::Right) box.x += step;
                    if (event.virtualKey == UCKeys::Up) box.y = std::max(0, box.y - step);
                    if (event.virtualKey == UCKeys::Down) box.y += step;
                    std::vector<SpreadsheetChart> before = charts;
                    charts[static_cast<size_t>(selected)].anchor = SpreadsheetChartAnchorForBox(*sheet, box);
                    RecordChartStep(*sheet, std::move(before), "Move chart");
                    return true;
                }
                default:
                    // Anything else is the cells': the chart lets go.
                    if (!event.ctrl && !event.alt && !event.meta) SelectChart(-1);
                    return false;
            }
        }
        default:
            return false;
    }
}

void UltraCanvasSpreadsheet::ShowChartPropertiesDialog(int index) {
    const SpreadsheetChart* current = GetChart(index);
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!current || !sheet) return;
    if (IsEditing()) StopEditing(true);

    // What the controls say, kept as they change: the result reads it.
    struct Answers {
        int choice = -1;
        std::string title;
        bool legend = false;
        bool rows = false;
    };
    auto answers = std::make_shared<Answers>();
    answers->choice = SpreadsheetChartChoiceOf(current->chart);
    answers->title = current->chart.title;
    answers->legend = current->chart.legend;
    answers->rows = current->seriesInRows;

    DialogConfig config;
    config.title = Tr("Chart properties");
    // An Information dialog without its icon: a Custom one builds no
    // sections and no buttons, and showed an empty window.
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::OKCancel;
    config.defaultButton = DialogButton::OK;
    config.width = 440;
    config.height = 240;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    dialog->SetIconVisible(false);
    dialog->SetMessageVisible(false);

    auto form = CreateFormGrid("ChartPropertiesForm", 8.0f, 8.0f);
    // The list and the field fill the control column.
    auto type = std::make_shared<UltraCanvasDropdown>("ChartType", 0, 0, 0, 28);
    for (size_t i = 0; i < SpreadsheetChartChoices().size(); ++i) type->AddItem(SpreadsheetChartChoiceName(i));
    type->SetSelectedIndex(answers->choice, false);
    type->onSelectionChanged = [answers](int chosen, const DropdownItem&) { answers->choice = chosen; };
    AddFormRow(form, "ChartType", Tr("Type:"), type);

    auto title = CreateTextInput("ChartTitle", 0, 0, 0, 28);
    title->SetText(answers->title);
    title->onTextChanged = [answers](const std::string& text) { answers->title = text; };
    AddFormRow(form, "ChartTitle", Tr("Title:"), title);

    auto legend = UltraCanvasCheckbox::CreateCheckbox("ChartLegend", 0, 0, -1, 28, Tr("Legend"), answers->legend);
    legend->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    legend->onStateChanged = [answers](CheckedState, CheckedState now) { answers->legend = now == CheckedState::Checked; };
    AddFormRow(form, "ChartLegend", "", legend);

    if (current->dataRange.IsSet()) {
        auto rows = UltraCanvasCheckbox::CreateCheckbox("ChartSeriesInRows", 0, 0, -1, 28, Tr("Series in rows"),
                                                        answers->rows);
        rows->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
        rows->onStateChanged = [answers](CheckedState, CheckedState now) { answers->rows = now == CheckedState::Checked; };
        AddFormRow(form, "ChartSeriesInRows", "", rows);
    }
    dialog->AddDialogElement(form);

    // The dialog may outlive this element: it is held weakly, and the chart
    // found again on the sheet it was on.
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    const uint64_t sheetId = sheet->GetId();
    UltraCanvasDialogManager::ShowDialog(dialog, [weakSelf, answers, index, sheetId](DialogResult result) {
        if (result != DialogResult::OK) return;
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        const SpreadsheetSheet* active = self->GetActiveSheet();
        const SpreadsheetChart* chart = self->GetChart(index);
        if (!active || active->GetId() != sheetId || !chart) return;
        SpreadsheetChart changed = *chart;
        const auto& choices = SpreadsheetChartChoices();
        if (answers->choice >= 0 && answers->choice < static_cast<int>(choices.size())) {
            const SpreadsheetChartChoice& choice = choices[static_cast<size_t>(answers->choice)];
            const bool wasScatter = changed.chart.kind == RichChart::Kind::Scatter;
            changed.chart.kind = choice.kind;
            changed.chart.stacked = choice.stacked;
            changed.chart.percent = false;
            // A scatter chart reads its x from the block; another kind does not.
            if (wasScatter != (choice.kind == RichChart::Kind::Scatter))
                SetSpreadsheetChartSeriesInRows(changed, *active, changed.seriesInRows);
        }
        changed.chart.title = answers->title;
        changed.chart.legend = answers->legend;
        if (changed.dataRange.IsSet() && answers->rows != changed.seriesInRows)
            SetSpreadsheetChartSeriesInRows(changed, *active, answers->rows);
        self->SetChart(index, changed);
    }, GetWindow());
}

} // namespace UltraCanvas
