// core/UltraCanvasSpreadsheetChart.cpp
// Charts on a spreadsheet (include/UltraCanvasSpreadsheetChart.h): the model
// - ranges, anchors, a chart made from a block of cells and read again from
// them - and the grid's side of it (UltraCanvasSpreadsheet's chart members):
// drawn over the cells, selected, moved and resized with handles, deleted,
// each change an undo step, and a properties dialog.
//
// Version: 1.1.0 - every chart of the framework: the types by category (SpreadsheetChartTypes), the columns
//                  each reads (a bubble's sizes, a Sankey diagram's targets, dates as categories), why a
//                  selection does not fit a type, sample data; the grid inserts and changes a chart through
//                  the chart gallery, and draws every kind (DrawRichChart)
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
#include "UltraCanvasButton.h"
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

// A cell holding a date (by its type or its number format).
bool IsDateCell(const SpreadsheetSheet& sheet, int row, int col) {
    const SpreadsheetCell* cell = sheet.GetCellIfExists(row, col);
    if (!cell) return false;
    const CellValueType type = cell->GetValueType();
    if (type == CellValueType::Date || type == CellValueType::DateTime) return true;
    if (!cell->HasCustomStyle()) return false;
    const NumberFormatCategory category = cell->GetStyle().numberFormat.category;
    return category == NumberFormatCategory::Date || category == NumberFormatCategory::DateTime;
}

// The words of a range: a cell's text where it holds no number, "" where it does.
std::vector<std::string> RangeLabels(const SpreadsheetSheet& sheet, const CellRange& range) {
    std::vector<std::string> words;
    const long count = static_cast<long>(range.RowCount()) * range.ColCount();
    if (count <= 0 || count > 100000) return words;
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            double value = 0.0;
            words.push_back(SpreadsheetChartCellNumber(sheet, row, col, value) ? std::string() : CellWords(sheet, row, col));
        }
    }
    return words;
}

using ChartKind = RichChart::Kind;

// A kind that reads a column of words as a series (targets, deeper levels).
bool KindReadsWords(ChartKind kind) {
    return kind == ChartKind::Sankey || kind == ChartKind::Treemap || kind == ChartKind::Sunburst;
}

// A kind whose first column of numbers is every series' x.
bool KindTakesX(ChartKind kind) {
    return kind == ChartKind::Scatter || kind == ChartKind::Bubble;
}

// A kind that plots dates as numbers (a time axis of its own, a date's
// place): a first column of dates is a series, not the categories.
bool KindPlotsDates(ChartKind kind) {
    switch (kind) {
        case ChartKind::Scatter: case ChartKind::Bubble: case ChartKind::Gantt: case ChartKind::Timeline:
        case ChartKind::CalendarHeatmap: case ChartKind::Quadrant: case ChartKind::ImportancePerformance:
        case ChartKind::Ternary: case ChartKind::Hexbin: case ChartKind::Scatter3D: case ChartKind::Histogram:
        case ChartKind::BoxWhisker: case ChartKind::Jitter: case ChartKind::Ridgeline:
            return true;
        default:
            return false;
    }
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
    auto onHandle = [&](double hx, double hy) { return std::fabs(x - hx) <= reach && std::fabs(y - hy) <= reach; };
    const double l = box.x, r = box.x + box.width, t = box.y, b = box.y + box.height;
    const double cx = (l + r) / 2.0, cy = (t + b) / 2.0;
    if (onHandle(l, t)) return SlideHandle::TopLeft;
    if (onHandle(r, t)) return SlideHandle::TopRight;
    if (onHandle(r, b)) return SlideHandle::BottomRight;
    if (onHandle(l, b)) return SlideHandle::BottomLeft;
    if (onHandle(cx, t)) return SlideHandle::Top;
    if (onHandle(r, cy)) return SlideHandle::Right;
    if (onHandle(cx, b)) return SlideHandle::Bottom;
    if (onHandle(l, cy)) return SlideHandle::Left;
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
    // ...or, for a kind that does not plot dates as numbers, holds dates.
    auto datesOnly = [&](int fixed, int from, int to) {
        bool any = false;
        for (int i = from; i <= to; ++i) {
            const auto [row, col] = at(fixed, i);
            if (!IsFilled(sheet, row, col)) continue;
            if (!IsDateCell(sheet, row, col)) return false;
            any = true;
        }
        return any;
    };
    const bool lengthy = minorLast > minorFirst, several = majorLast > majorFirst;
    bool names = lengthy && wordsOnly(false, minorFirst, majorFirst + (several ? 1 : 0), majorLast);
    bool categories = several && wordsOnly(true, majorFirst, minorFirst + (names ? 1 : 0), minorLast);
    if (!categories && several && !KindPlotsDates(chart.chart.kind))
        categories = datesOnly(majorFirst, minorFirst + (names ? 1 : 0), minorLast);
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
    // A scatter (bubble) chart: the first line of numbers is the x values.
    const bool takesX = KindTakesX(chart.chart.kind);
    if (takesX && !categories && firstSeries < majorLast) {
        categoryRange = line(firstSeries, dataMinorFirst, minorLast);
        ++firstSeries;
    } else if (takesX && categories) {
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
        // A bubble series: its y, then its sizes in the next line.
        if (chart.chart.kind == RichChart::Kind::Bubble && major < majorLast) {
            one.sizes = line(major + 1, dataMinorFirst, minorLast);
            ++major;
        }
        sources.push_back(std::move(one));
    }
    // The series' look by position; new ones from the palette.
    std::vector<RichChartSeries> looks = chart.chart.series;
    looks.resize(sources.size());
    for (size_t i = 0; i < looks.size(); ++i) {
        looks[i].values.clear();
        looks[i].xValues.clear();
        looks[i].sizes.clear();
        looks[i].labels.clear();
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
        if (source.sizes.IsSet()) {
            const SpreadsheetSheet* sheet = sheetOf(source.sizes);
            series.sizes = sheet ? RangeNumbers(*sheet, source.sizes.range) : std::vector<double>();
            if (series.sizes.size() != series.values.size()) series.sizes.resize(series.values.size(), 0.0);
        }
        if (KindReadsWords(fresh.kind) && source.values.IsSet()) {
            const SpreadsheetSheet* sheet = sheetOf(source.values);
            series.labels = sheet ? RangeLabels(*sheet, source.values.range) : std::vector<std::string>();
            bool any = false;
            for (const std::string& label : series.labels) any = any || !label.empty();
            if (!any) series.labels.clear();
        } else {
            series.labels.clear();
        }
    }
    bool changed = fresh.categories != chart.chart.categories || fresh.series.size() != chart.chart.series.size();
    for (size_t i = 0; !changed && i < fresh.series.size(); ++i) {
        const RichChartSeries& a = fresh.series[i];
        const RichChartSeries& b = chart.chart.series[i];
        changed = a.name != b.name || a.values != b.values || a.xValues != b.xValues || a.sizes != b.sizes ||
                  a.labels != b.labels;
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
                          std::shared_ptr<UltraCanvasUIElement>* cache) {
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
        drawn = DrawRichChart(ctx, chart.chart, inner, natural, cache);
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
            rename(source.sizes);
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
            track(source.sizes);
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
// THE CHART TYPES
// ============================================================================

namespace {

std::vector<SpreadsheetChartType> MakeTypes() {
    using Kind = RichChart::Kind;
    using Cat = SpreadsheetChartCategory;
    auto type = [](Kind kind, Cat category, const char* id, bool stacked = false, bool percent = false) {
        SpreadsheetChartType one;
        one.kind = kind;
        one.category = category;
        one.id = id;
        one.stacked = stacked;
        one.percent = percent;
        return one;
    };
    std::vector<SpreadsheetChartType> types = {
        type(Kind::Column, Cat::ColumnAndBar, "column"),
        type(Kind::Column, Cat::ColumnAndBar, "column-stacked", true),
        type(Kind::Column, Cat::ColumnAndBar, "column-percent", true, true),
        type(Kind::Bar, Cat::ColumnAndBar, "bar"),
        type(Kind::Bar, Cat::ColumnAndBar, "bar-stacked", true),
        type(Kind::Bar, Cat::ColumnAndBar, "bar-percent", true, true),
        type(Kind::DivergingBar, Cat::ColumnAndBar, "diverging-bar"),
        type(Kind::PopulationPyramid, Cat::ColumnAndBar, "population-pyramid"),
        type(Kind::Dumbbell, Cat::ColumnAndBar, "dumbbell"),
        type(Kind::Mekko, Cat::ColumnAndBar, "mekko"),
        type(Kind::Line, Cat::LineAndArea, "line"),
        type(Kind::Area, Cat::LineAndArea, "area"),
        type(Kind::Area, Cat::LineAndArea, "area-stacked", true),
        type(Kind::Area, Cat::LineAndArea, "area-percent", true, true),
        type(Kind::CumulativeFlow, Cat::LineAndArea, "cumulative-flow"),
        type(Kind::NestedArea, Cat::LineAndArea, "nested-area"),
        type(Kind::Pie, Cat::PieAndCircular, "pie"),
        type(Kind::Doughnut, Cat::PieAndCircular, "doughnut"),
        type(Kind::Radar, Cat::PieAndCircular, "radar"),
        type(Kind::Radar, Cat::PieAndCircular, "radar-filled"),
        type(Kind::Polar, Cat::PieAndCircular, "polar"),
        type(Kind::RadialBar, Cat::PieAndCircular, "radial-bar"),
        type(Kind::CircularProgress, Cat::PieAndCircular, "circular-progress"),
        type(Kind::Gauge, Cat::PieAndCircular, "gauge"),
        type(Kind::Scatter, Cat::ScatterAndBubble, "scatter"),
        type(Kind::Scatter, Cat::ScatterAndBubble, "scatter-lines"),
        type(Kind::Bubble, Cat::ScatterAndBubble, "bubble"),
        type(Kind::Quadrant, Cat::ScatterAndBubble, "quadrant"),
        type(Kind::ImportancePerformance, Cat::ScatterAndBubble, "importance-performance"),
        type(Kind::Ternary, Cat::ScatterAndBubble, "ternary"),
        type(Kind::Hexbin, Cat::ScatterAndBubble, "hexbin"),
        type(Kind::Scatter3D, Cat::ScatterAndBubble, "scatter-3d"),
        type(Kind::Histogram, Cat::Statistical, "histogram"),
        type(Kind::BoxWhisker, Cat::Statistical, "box-whisker"),
        type(Kind::Jitter, Cat::Statistical, "jitter"),
        type(Kind::Ridgeline, Cat::Statistical, "ridgeline"),
        type(Kind::Stock, Cat::Financial, "stock"),
        type(Kind::Waterfall, Cat::Financial, "waterfall"),
        type(Kind::Funnel, Cat::FlowAndHierarchy, "funnel"),
        type(Kind::Pyramid, Cat::FlowAndHierarchy, "pyramid"),
        type(Kind::Sankey, Cat::FlowAndHierarchy, "sankey"),
        type(Kind::Chord, Cat::FlowAndHierarchy, "chord"),
        type(Kind::Treemap, Cat::FlowAndHierarchy, "treemap"),
        type(Kind::Sunburst, Cat::FlowAndHierarchy, "sunburst"),
        type(Kind::Gantt, Cat::FlowAndHierarchy, "gantt"),
        type(Kind::Timeline, Cat::FlowAndHierarchy, "timeline"),
        type(Kind::Heatmap, Cat::MatrixAndMaps, "heatmap"),
        type(Kind::CalendarHeatmap, Cat::MatrixAndMaps, "calendar-heatmap"),
        type(Kind::Contour, Cat::MatrixAndMaps, "contour"),
        type(Kind::Surface, Cat::MatrixAndMaps, "surface"),
        type(Kind::WordCloud, Cat::Other, "word-cloud"),
        type(Kind::Parliament, Cat::Other, "parliament"),
    };
    for (SpreadsheetChartType& one : types) {
        const std::string id = one.id;
        one.filled = id == "radar-filled";
        one.lines = id == "scatter-lines";
    }
    return types;
}

struct KindId {
    RichChart::Kind kind;
    const char* id;
};

const KindId kKindIds[] = {
    {RichChart::Kind::Column, "column"}, {RichChart::Kind::Bar, "bar"}, {RichChart::Kind::Line, "line"},
    {RichChart::Kind::Area, "area"}, {RichChart::Kind::Pie, "pie"}, {RichChart::Kind::Doughnut, "doughnut"},
    {RichChart::Kind::Scatter, "scatter"}, {RichChart::Kind::Radar, "radar"}, {RichChart::Kind::Bubble, "bubble"},
    {RichChart::Kind::Stock, "stock"}, {RichChart::Kind::Waterfall, "waterfall"},
    {RichChart::Kind::DivergingBar, "diverging-bar"}, {RichChart::Kind::PopulationPyramid, "population-pyramid"},
    {RichChart::Kind::Dumbbell, "dumbbell"}, {RichChart::Kind::Mekko, "mekko"},
    {RichChart::Kind::CumulativeFlow, "cumulative-flow"}, {RichChart::Kind::NestedArea, "nested-area"},
    {RichChart::Kind::Polar, "polar"}, {RichChart::Kind::RadialBar, "radial-bar"},
    {RichChart::Kind::CircularProgress, "circular-progress"}, {RichChart::Kind::Gauge, "gauge"},
    {RichChart::Kind::Quadrant, "quadrant"}, {RichChart::Kind::ImportancePerformance, "importance-performance"},
    {RichChart::Kind::Ternary, "ternary"}, {RichChart::Kind::Hexbin, "hexbin"},
    {RichChart::Kind::Scatter3D, "scatter-3d"}, {RichChart::Kind::Histogram, "histogram"},
    {RichChart::Kind::BoxWhisker, "box-whisker"}, {RichChart::Kind::Jitter, "jitter"},
    {RichChart::Kind::Ridgeline, "ridgeline"}, {RichChart::Kind::Funnel, "funnel"},
    {RichChart::Kind::Pyramid, "pyramid"}, {RichChart::Kind::Sankey, "sankey"}, {RichChart::Kind::Chord, "chord"},
    {RichChart::Kind::Treemap, "treemap"}, {RichChart::Kind::Sunburst, "sunburst"}, {RichChart::Kind::Gantt, "gantt"},
    {RichChart::Kind::Timeline, "timeline"}, {RichChart::Kind::Heatmap, "heatmap"},
    {RichChart::Kind::CalendarHeatmap, "calendar-heatmap"}, {RichChart::Kind::Contour, "contour"},
    {RichChart::Kind::Surface, "surface"}, {RichChart::Kind::WordCloud, "word-cloud"},
    {RichChart::Kind::Parliament, "parliament"},
};

// What the made chart holds: series of numbers, of words, its names.
struct ChartShape {
    size_t numbers = 0, words = 0, points = 0;
    bool names = false;
};

ChartShape ShapeOf(const SpreadsheetChart& chart) {
    ChartShape shape;
    const RichChart& data = chart.chart;
    shape.names = !data.categories.empty();
    shape.points = data.categories.size();
    for (size_t i = 0; i < data.series.size(); ++i) {
        const RichChartSeries& one = data.series[i];
        size_t wordCount = 0;
        for (const std::string& label : one.labels) wordCount += label.empty() ? 0 : 1;
        shape.points = std::max(shape.points, one.values.size());
        if (wordCount * 2 > one.labels.size() && wordCount > 0) {
            ++shape.words;
            continue;
        }
        // A series counts as numbers when its cells hold one.
        bool anyNumber = false;
        if (i < chart.sources.size() && chart.sources[i].values.IsSet()) {
            anyNumber = std::any_of(one.values.begin(), one.values.end(), [](double v) { return v != 0.0; });
        } else {
            anyNumber = !one.values.empty();
        }
        if (anyNumber) ++shape.numbers;
    }
    return shape;
}

} // namespace

const std::vector<SpreadsheetChartType>& SpreadsheetChartTypes() {
    static const std::vector<SpreadsheetChartType> types = MakeTypes();
    return types;
}

std::string SpreadsheetChartCategoryName(SpreadsheetChartCategory category) {
    switch (category) {
        case SpreadsheetChartCategory::ColumnAndBar: return TrC("chart category", "Column and bar");
        case SpreadsheetChartCategory::LineAndArea: return TrC("chart category", "Line and area");
        case SpreadsheetChartCategory::PieAndCircular: return TrC("chart category", "Pie and circular");
        case SpreadsheetChartCategory::ScatterAndBubble: return TrC("chart category", "Scatter and bubble");
        case SpreadsheetChartCategory::Statistical: return TrC("chart category", "Statistical");
        case SpreadsheetChartCategory::Financial: return TrC("chart category", "Financial");
        case SpreadsheetChartCategory::FlowAndHierarchy: return TrC("chart category", "Flow and hierarchy");
        case SpreadsheetChartCategory::MatrixAndMaps: return TrC("chart category", "Matrix and maps");
        case SpreadsheetChartCategory::Other: return TrC("chart category", "Other");
    }
    return std::string();
}

std::string SpreadsheetChartTypeName(size_t index) {
    const auto& types = SpreadsheetChartTypes();
    if (index >= types.size()) return std::string();
    const std::string id = types[index].id;
    if (id == "column") return TrC("chart kind", "Columns");
    if (id == "column-stacked") return TrC("chart kind", "Columns, stacked");
    if (id == "column-percent") return TrC("chart kind", "Columns, 100 %");
    if (id == "bar") return TrC("chart kind", "Bars");
    if (id == "bar-stacked") return TrC("chart kind", "Bars, stacked");
    if (id == "bar-percent") return TrC("chart kind", "Bars, 100 %");
    if (id == "diverging-bar") return TrC("chart kind", "Diverging bars");
    if (id == "population-pyramid") return TrC("chart kind", "Population pyramid");
    if (id == "dumbbell") return TrC("chart kind", "Dumbbell");
    if (id == "mekko") return TrC("chart kind", "Mekko");
    if (id == "line") return TrC("chart kind", "Line");
    if (id == "area") return TrC("chart kind", "Area");
    if (id == "area-stacked") return TrC("chart kind", "Area, stacked");
    if (id == "area-percent") return TrC("chart kind", "Area, 100 %");
    if (id == "cumulative-flow") return TrC("chart kind", "Cumulative flow");
    if (id == "nested-area") return TrC("chart kind", "Nested areas");
    if (id == "pie") return TrC("chart kind", "Pie");
    if (id == "doughnut") return TrC("chart kind", "Doughnut");
    if (id == "radar") return TrC("chart kind", "Radar");
    if (id == "radar-filled") return TrC("chart kind", "Radar, filled");
    if (id == "polar") return TrC("chart kind", "Polar");
    if (id == "radial-bar") return TrC("chart kind", "Radial bars");
    if (id == "circular-progress") return TrC("chart kind", "Progress rings");
    if (id == "gauge") return TrC("chart kind", "Gauge");
    if (id == "scatter") return TrC("chart kind", "Scatter");
    if (id == "scatter-lines") return TrC("chart kind", "Scatter with lines");
    if (id == "bubble") return TrC("chart kind", "Bubble");
    if (id == "quadrant") return TrC("chart kind", "Quadrant");
    if (id == "importance-performance") return TrC("chart kind", "Importance-performance");
    if (id == "ternary") return TrC("chart kind", "Ternary");
    if (id == "hexbin") return TrC("chart kind", "Hexbin");
    if (id == "scatter-3d") return TrC("chart kind", "3D scatter");
    if (id == "histogram") return TrC("chart kind", "Histogram");
    if (id == "box-whisker") return TrC("chart kind", "Box and whisker");
    if (id == "jitter") return TrC("chart kind", "Jitter");
    if (id == "ridgeline") return TrC("chart kind", "Ridgeline");
    if (id == "stock") return TrC("chart kind", "Stock");
    if (id == "waterfall") return TrC("chart kind", "Waterfall");
    if (id == "funnel") return TrC("chart kind", "Funnel");
    if (id == "pyramid") return TrC("chart kind", "Pyramid");
    if (id == "sankey") return TrC("chart kind", "Sankey");
    if (id == "chord") return TrC("chart kind", "Chord");
    if (id == "treemap") return TrC("chart kind", "Treemap");
    if (id == "sunburst") return TrC("chart kind", "Sunburst");
    if (id == "gantt") return TrC("chart kind", "Gantt");
    if (id == "timeline") return TrC("chart kind", "Timeline");
    if (id == "heatmap") return TrC("chart kind", "Heatmap");
    if (id == "calendar-heatmap") return TrC("chart kind", "Calendar heatmap");
    if (id == "contour") return TrC("chart kind", "Contour");
    if (id == "surface") return TrC("chart kind", "Surface");
    if (id == "word-cloud") return TrC("chart kind", "Word cloud");
    if (id == "parliament") return TrC("chart kind", "Parliament");
    return std::string();
}

std::string SpreadsheetChartTypeDescription(size_t index) {
    const auto& types = SpreadsheetChartTypes();
    if (index >= types.size()) return std::string();
    const std::string id = types[index].id;
    if (id == "column" || id == "bar") return Tr("Compares values across categories. Reads names, then one or more columns of numbers.");
    if (id == "column-stacked" || id == "bar-stacked") return Tr("Each category's values one on the other, showing its total. Reads names, then two or more columns of numbers.");
    if (id == "column-percent" || id == "bar-percent") return Tr("Each category's values as shares of its total. Reads names, then two or more columns of numbers.");
    if (id == "diverging-bar") return Tr("Answers either side of a centre, as on a Likert scale. Reads names, then the answers' counts from the most negative to the most positive.");
    if (id == "population-pyramid") return Tr("Two groups back to back, as men and women by age. Reads the age groups, then two columns of numbers.");
    if (id == "dumbbell") return Tr("Two values for each category joined by a line, as before and after. Reads names, then two columns of numbers.");
    if (id == "mekko") return Tr("Columns of shares whose widths show each column's total. Reads names, then a column of numbers per segment.");
    if (id == "line") return Tr("A trend over time or over ordered categories. Reads names, then one or more columns of numbers.");
    if (id == "area") return Tr("A trend with the area under it filled. Reads names, then one or more columns of numbers.");
    if (id == "area-stacked") return Tr("Areas one on the other, showing the total over time. Reads names, then two or more columns of numbers.");
    if (id == "area-percent") return Tr("Areas as shares of the total over time. Reads names, then two or more columns of numbers.");
    if (id == "cumulative-flow") return Tr("Work in each stage over time, as bands. Reads the periods, then a column of numbers per stage.");
    if (id == "nested-area") return Tr("Areas inside one another in proportion to their values. Reads names and a column of numbers.");
    if (id == "pie") return Tr("Parts of a whole as slices. Reads names and one column of numbers.");
    if (id == "doughnut") return Tr("Parts of a whole as a ring. Reads names and one column of numbers.");
    if (id == "radar" || id == "radar-filled") return Tr("Several measures round a circle, a shape per series. Reads at least three names, then one or more columns of numbers.");
    if (id == "polar") return Tr("Values round a circle, their distance from the centre. Reads at least three names, then one or more columns of numbers.");
    if (id == "radial-bar") return Tr("Bars growing out from a centre. Reads names, then one or more columns of numbers.");
    if (id == "circular-progress") return Tr("A ring per name, filled to its value out of 100 - or out of a second column.");
    if (id == "gauge") return Tr("One value on a dial. Reads the first number; a second column's first number sets the maximum.");
    if (id == "scatter") return Tr("How two values relate, as points. Reads a column of x values, then one or more columns of y values.");
    if (id == "scatter-lines") return Tr("Points joined in order. Reads a column of x values, then one or more columns of y values.");
    if (id == "bubble") return Tr("Points whose size shows a third value. Reads three columns of numbers: x, y and size.");
    if (id == "quadrant") return Tr("Items placed in four quadrants by two values. Reads names, then two columns of numbers: x and y.");
    if (id == "importance-performance") return Tr("Factors placed by importance and performance on Slack's nine-point scales (1 is best). Reads names, then two columns.");
    if (id == "ternary") return Tr("Mixes of three parts as points in a triangle. Reads three columns of numbers, one per part.");
    if (id == "hexbin") return Tr("How many points fall in each hexagon of the plane - for many points. Reads two columns of numbers: x and y.");
    if (id == "scatter-3d") return Tr("Points in a box of three axes. Reads three columns of numbers: x, y and z.");
    if (id == "histogram") return Tr("How values are spread, counted into bins. Reads one or more columns of raw values.");
    if (id == "box-whisker") return Tr("The spread of each group: median, quartiles and range over the values. Reads a column of raw values per group.");
    if (id == "jitter") return Tr("Every value of each group as a dot. Reads a column of raw values per group - or names and one column.");
    if (id == "ridgeline") return Tr("The spread of each group as a curve, one row each. Reads a column of raw values per group.");
    if (id == "stock") return Tr("Prices over time as candles or bars. Reads dates, then three or four columns: (open,) high, low and close.");
    if (id == "waterfall") return Tr("A running total built from rises and falls. Reads names and the changes; a name starting with Total stands on zero.");
    if (id == "funnel") return Tr("A process that narrows stage by stage. Reads the stages and one column of numbers.");
    if (id == "pyramid") return Tr("Levels of a whole stacked from base to top. Reads the levels and one column of numbers.");
    if (id == "sankey") return Tr("Flows from sources to targets, their widths the amounts. Reads a column of sources, a column of targets and a column of amounts.");
    if (id == "chord") return Tr("Flows between the members of a group round a circle. Reads a square table: the same names down the side and across the top.");
    if (id == "treemap") return Tr("A whole as nested rectangles. Reads names (and columns of deeper levels), then a column of numbers.");
    if (id == "sunburst") return Tr("A hierarchy as rings. Reads names (and columns of deeper levels), then a column of numbers.");
    if (id == "gantt") return Tr("A schedule of tasks as bars. Reads the tasks, then two columns of dates: start and end.");
    if (id == "timeline") return Tr("Events and spans on a date axis. Reads names and a column of dates (and a column of end dates).");
    if (id == "heatmap") return Tr("A table of numbers as colours. Reads names down the side and a column of numbers per column.");
    if (id == "calendar-heatmap") return Tr("A value per day on a calendar. Reads a column of dates and a column of numbers.");
    if (id == "contour") return Tr("A table of numbers as a map of levels. Reads at least two rows and two columns of numbers.");
    if (id == "surface") return Tr("A table of numbers as a 3D surface. Reads at least two rows and two columns of numbers.");
    if (id == "word-cloud") return Tr("Words sized by their weight. Reads the words and a column of numbers.");
    if (id == "parliament") return Tr("Seats of a chamber by party. Reads the parties and their seats.");
    return std::string();
}

int SpreadsheetChartTypeOf(const RichChart& chart) {
    const auto& types = SpreadsheetChartTypes();
    bool joined = false;
    for (const RichChartSeries& one : chart.series) joined = joined || one.joined;
    const bool stackable = chart.kind == RichChart::Kind::Column || chart.kind == RichChart::Kind::Bar ||
                           chart.kind == RichChart::Kind::Area;
    int first = -1;
    for (size_t i = 0; i < types.size(); ++i) {
        const SpreadsheetChartType& one = types[i];
        if (one.kind != chart.kind) continue;
        if (first < 0) first = static_cast<int>(i);
        if (stackable && (one.stacked != (chart.stacked || chart.percent) || one.percent != chart.percent)) continue;
        if (chart.kind == RichChart::Kind::Radar && one.filled != chart.filled) continue;
        if (chart.kind == RichChart::Kind::Scatter && one.lines != joined) continue;
        return static_cast<int>(i);
    }
    return first;
}

int SpreadsheetChartTypeById(const std::string& id) {
    const auto& types = SpreadsheetChartTypes();
    for (size_t i = 0; i < types.size(); ++i) {
        if (id == types[i].id) return static_cast<int>(i);
    }
    return -1;
}

const char* SpreadsheetChartKindId(RichChart::Kind kind) {
    for (const KindId& one : kKindIds) {
        if (one.kind == kind) return one.id;
    }
    return "";
}

bool SpreadsheetChartKindFromId(const std::string& id, RichChart::Kind& kind) {
    for (const KindId& one : kKindIds) {
        if (id == one.id) {
            kind = one.kind;
            return true;
        }
    }
    return false;
}

namespace {

// The type's look on a chart's model.
void ApplyLook(RichChart& chart, const SpreadsheetChartType& type) {
    chart.kind = type.kind;
    chart.stacked = type.stacked;
    chart.percent = type.percent;
    chart.filled = type.filled;
    for (RichChartSeries& one : chart.series) {
        if (type.kind == RichChart::Kind::Scatter) {
            one.joined = type.lines;
            one.marked = true;
        } else {
            one.joined = true;
        }
    }
    chart.legend = chart.series.size() > 1 || type.kind == RichChart::Kind::Pie || type.kind == RichChart::Kind::Doughnut;
}

// Kinds that read the same block in the same way share their ranges.
int ReadingOf(RichChart::Kind kind) {
    if (kind == RichChart::Kind::Bubble) return 2;
    if (KindTakesX(kind)) return 1;
    return KindPlotsDates(kind) ? 3 : 0;
}

} // namespace

SpreadsheetChart MakeSpreadsheetChartOfType(const SpreadsheetSheet& sheet, const CellRange& range, size_t type) {
    const auto& types = SpreadsheetChartTypes();
    if (type >= types.size()) return MakeSpreadsheetChart(sheet, range, RichChart::Kind::Column);
    const SpreadsheetChartType& chosen = types[type];
    // The series down the columns for the kinds whose columns are roles.
    SpreadsheetChartSeriesIn seriesIn = SpreadsheetChartSeriesIn::Automatic;
    switch (chosen.kind) {
        case RichChart::Kind::Bubble: case RichChart::Kind::Stock: case RichChart::Kind::Quadrant:
        case RichChart::Kind::ImportancePerformance: case RichChart::Kind::Ternary: case RichChart::Kind::Hexbin:
        case RichChart::Kind::Scatter3D: case RichChart::Kind::Sankey: case RichChart::Kind::Treemap:
        case RichChart::Kind::Sunburst: case RichChart::Kind::Gantt: case RichChart::Kind::Timeline:
        case RichChart::Kind::CalendarHeatmap: case RichChart::Kind::Histogram: case RichChart::Kind::BoxWhisker:
        case RichChart::Kind::Jitter: case RichChart::Kind::Ridgeline: case RichChart::Kind::Dumbbell:
        case RichChart::Kind::PopulationPyramid: case RichChart::Kind::Heatmap: case RichChart::Kind::Chord:
        case RichChart::Kind::Contour: case RichChart::Kind::Surface: case RichChart::Kind::DivergingBar:
        case RichChart::Kind::Mekko: case RichChart::Kind::CumulativeFlow:
            seriesIn = SpreadsheetChartSeriesIn::Columns;
            break;
        default:
            break;
    }
    SpreadsheetChart made = MakeSpreadsheetChart(sheet, range, chosen.kind, chosen.stacked, seriesIn);
    ApplyLook(made.chart, chosen);
    if (made.chart.series.size() != 1 || !made.sources.front().name.IsSet()) made.chart.title.clear();
    TouchSpreadsheetChart(made);
    return made;
}

void SetSpreadsheetChartType(SpreadsheetChart& chart, const SpreadsheetSheet& sheet, size_t type) {
    const auto& types = SpreadsheetChartTypes();
    if (type >= types.size()) return;
    const SpreadsheetChartType& chosen = types[type];
    const bool reread = ReadingOf(chart.chart.kind) != ReadingOf(chosen.kind) ||
                        KindReadsWords(chart.chart.kind) != KindReadsWords(chosen.kind);
    const std::string title = chart.chart.title;
    const bool legend = chart.chart.legend;
    chart.chart.kind = chosen.kind;
    if (reread) SetSpreadsheetChartSeriesInRows(chart, sheet, chart.seriesInRows);
    ApplyLook(chart.chart, chosen);
    chart.chart.title = title;
    chart.chart.legend = legend || chosen.kind == RichChart::Kind::Pie || chosen.kind == RichChart::Kind::Doughnut;
    TouchSpreadsheetChart(chart);
}

std::string SpreadsheetChartFitMessage(const SpreadsheetChart& chart) {
    using Kind = RichChart::Kind;
    const ChartShape shape = ShapeOf(chart);
    if (shape.numbers == 0 && chart.chart.kind != Kind::Sankey)
        return Tr("The selection holds no numbers to make a chart of.");
    switch (chart.chart.kind) {
        case Kind::Bubble: {
            bool sized = !chart.chart.series.empty();
            for (const SpreadsheetChartSeries& one : chart.sources) sized = sized && one.sizes.IsSet();
            if (chart.sources.empty() || !sized || !chart.sources.front().categories.IsSet())
                return Tr("A bubble chart needs three columns of numbers: x, y and size.");
            return std::string();
        }
        case Kind::Stock:
            return shape.numbers >= 3 ? std::string()
                                      : Tr("A stock chart needs three or four columns of numbers: (open,) high, low and close.");
        case Kind::Radar:
        case Kind::Polar:
            return shape.points >= 3 ? std::string() : Tr("This chart needs at least three categories round its circle.");
        case Kind::Dumbbell:
            return shape.numbers >= 2 ? std::string() : Tr("A dumbbell chart needs two columns of numbers.");
        case Kind::PopulationPyramid:
            return shape.numbers >= 2 ? std::string() : Tr("A population pyramid needs two columns of numbers, one for each side.");
        case Kind::Quadrant:
        case Kind::ImportancePerformance:
        case Kind::Hexbin:
            return shape.numbers >= 2 ? std::string() : Tr("This chart needs two columns of numbers: x and y.");
        case Kind::Ternary:
            return shape.numbers >= 3 ? std::string() : Tr("A ternary plot needs three columns of numbers, one for each part.");
        case Kind::Scatter3D:
            return shape.numbers >= 3 ? std::string() : Tr("A 3D scatter chart needs three columns of numbers: x, y and z.");
        case Kind::Sankey:
            return shape.names && shape.words >= 1 && shape.numbers >= 1
                ? std::string() : Tr("A Sankey diagram needs a column of sources, a column of targets and a column of amounts.");
        case Kind::Treemap:
        case Kind::Sunburst:
            return shape.names ? std::string() : Tr("This chart needs a column of names and a column of numbers.");
        case Kind::Gantt:
            return shape.names && shape.numbers >= 2
                ? std::string() : Tr("A Gantt chart needs a column of tasks and two columns of dates: start and end.");
        case Kind::Timeline:
            return shape.names ? std::string() : Tr("A timeline needs a column of names and a column of dates.");
        case Kind::CalendarHeatmap:
            return shape.numbers >= 2 ? std::string() : Tr("A calendar heatmap needs a column of dates and a column of numbers.");
        case Kind::Heatmap:
        case Kind::Contour:
        case Kind::Surface:
            return shape.numbers >= 2 && shape.points >= 2
                ? std::string() : Tr("This chart needs a table of numbers: at least two rows and two columns.");
        case Kind::Chord:
            return shape.numbers >= 2 && shape.points >= 2
                ? std::string() : Tr("A chord diagram needs a square table of flows: the same names down the side and across the top.");
        default:
            return std::string();
    }
}

RichChart SampleSpreadsheetChartData(RichChart::Kind kind) {
    using Kind = RichChart::Kind;
    RichChart chart;
    chart.kind = kind;
    auto series = [&](const std::string& name, std::vector<double> values) {
        RichChartSeries one;
        one.name = name;
        one.values = std::move(values);
        chart.series.push_back(std::move(one));
    };
    switch (kind) {
        case Kind::Scatter:
        case Kind::Bubble: {
            series("A", {3, 5, 4, 7, 6, 9});
            chart.series[0].xValues = {1, 2, 3, 4, 5, 6};
            chart.series[0].sizes = {4, 9, 2, 6, 8, 3};
            chart.series[0].marked = true;
            chart.series[0].joined = false;
            break;
        }
        case Kind::Stock:
            chart.categories = {"Mon", "Tue", "Wed", "Thu", "Fri"};
            series("Open", {20, 22, 21, 24, 23});
            series("High", {24, 25, 24, 27, 26});
            series("Low", {18, 20, 19, 22, 21});
            series("Close", {22, 21, 24, 23, 25});
            break;
        case Kind::Waterfall:
            chart.categories = {"Start", "Sales", "Costs", "Tax", "Total"};
            series("Change", {50, 30, -20, -8, 0});
            break;
        case Kind::DivergingBar:
            chart.categories = {"Price", "Service", "Speed"};
            series("Poor", {10, 5, 15});
            series("Fair", {20, 15, 20});
            series("Neutral", {15, 20, 15});
            series("Good", {35, 40, 30});
            series("Great", {20, 20, 20});
            break;
        case Kind::PopulationPyramid:
            chart.categories = {"0-19", "20-39", "40-59", "60-79", "80+"};
            series("Men", {20, 26, 24, 15, 4});
            series("Women", {19, 25, 25, 17, 7});
            break;
        case Kind::Dumbbell:
            chart.categories = {"North", "South", "East", "West"};
            series("2020", {30, 45, 25, 50});
            series("2025", {48, 52, 40, 55});
            break;
        case Kind::Quadrant:
        case Kind::ImportancePerformance:
            chart.categories = {"Speed", "Price", "Range", "Help", "Style"};
            series("Importance", {2, 4, 6, 8, 3});
            series("Performance", {3, 7, 2, 5, 8});
            break;
        case Kind::Ternary:
        case Kind::Scatter3D:
            chart.categories = {"P1", "P2", "P3", "P4", "P5", "P6"};
            series("Sand", {60, 20, 30, 10, 45, 33});
            series("Silt", {25, 50, 30, 30, 35, 33});
            series("Clay", {15, 30, 40, 60, 20, 34});
            break;
        case Kind::Hexbin:
            series("X", {1, 1.2, 1.4, 2, 2.2, 2.4, 2.5, 3, 3.1, 3.3, 3.6, 4, 4.2, 4.4, 5, 2.6, 2.8, 3.2});
            series("Y", {2, 2.4, 1.8, 3, 3.2, 2.8, 3.5, 4, 3.6, 4.2, 4.1, 5, 4.6, 5.2, 6, 3.1, 3.3, 3.9});
            break;
        case Kind::Histogram:
        case Kind::BoxWhisker:
        case Kind::Jitter:
        case Kind::Ridgeline:
            series("A", {4, 5, 5, 6, 6, 6, 7, 7, 8, 9, 5, 6});
            series("B", {6, 7, 7, 8, 8, 8, 9, 9, 10, 11, 8, 7});
            series("C", {2, 3, 3, 4, 4, 5, 5, 6, 7, 4, 3, 5});
            break;
        case Kind::Sankey: {
            chart.categories = {"Salary", "Salary", "Salary", "Gifts", "Gifts"};
            series("Target", {0, 0, 0, 0, 0});
            chart.series[0].labels = {"Rent", "Food", "Savings", "Food", "Savings"};
            series("Amount", {40, 25, 20, 5, 10});
            break;
        }
        case Kind::Treemap:
        case Kind::Sunburst: {
            chart.categories = {"Fruit", "Fruit", "Fruit", "Vegetables", "Vegetables"};
            series("Kind", {0, 0, 0, 0, 0});
            chart.series[0].labels = {"Apples", "Pears", "Plums", "Beans", "Peas"};
            series("Sold", {40, 25, 15, 30, 20});
            break;
        }
        case Kind::Chord:
        case Kind::Heatmap:
        case Kind::Contour:
        case Kind::Surface:
            chart.categories = {"A", "B", "C", "D"};
            series("A", {1, 4, 6, 3});
            series("B", {4, 2, 5, 7});
            series("C", {6, 5, 3, 4});
            series("D", {3, 7, 4, 2});
            break;
        case Kind::Gantt:
            chart.categories = {"Plan", "Build", "Test", "Launch"};
            series("Start", {46023, 46030, 46044, 46051});
            series("End", {46031, 46045, 46052, 46052});
            break;
        case Kind::Timeline:
            chart.categories = {"Idea", "Design", "Release"};
            series("Date", {46023, 46030, 46060});
            series("Until", {0, 46045, 0});
            break;
        case Kind::CalendarHeatmap: {
            std::vector<double> days, values;
            for (int d = 0; d < 120; ++d) {
                days.push_back(46023 + d);
                values.push_back(static_cast<double>((d * 7) % 11));
            }
            series("Date", days);
            series("Count", values);
            break;
        }
        case Kind::CircularProgress:
            chart.categories = {"Tasks", "Budget", "Hours"};
            series("Done", {72, 45, 88});
            break;
        case Kind::Gauge:
            chart.categories = {"Score"};
            series("Score", {72});
            break;
        case Kind::Funnel:
        case Kind::Pyramid:
            chart.categories = {"Visits", "Sign-ups", "Trials", "Sales"};
            series("People", {1000, 600, 300, 120});
            break;
        case Kind::Parliament:
            chart.categories = {"Reds", "Blues", "Greens", "Golds"};
            series("Seats", {120, 95, 40, 25});
            break;
        case Kind::WordCloud:
            chart.categories = {"chart", "data", "cells", "table", "sum", "trend", "value", "share"};
            series("Weight", {10, 8, 6, 6, 4, 5, 3, 2});
            break;
        default:
            chart.categories = {"Q1", "Q2", "Q3", "Q4", "Q5"};
            series("North", {12, 18, 15, 22, 26});
            series("South", {8, 11, 14, 12, 17});
            break;
    }
    if (kind == Kind::Radar) chart.filled = false;
    return chart;
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

int UltraCanvasSpreadsheet::InsertChartOfTypeFromSelection(size_t type, std::string* problem) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || type >= SpreadsheetChartTypes().size()) return -1;
    if (IsEditing()) StopEditing(true);
    if (formulaEngine_ && formulaEngine_->HasPendingChanges()) formulaEngine_->Recalculate();
    SpreadsheetChart chart = MakeSpreadsheetChartOfType(*sheet, GetSelection(), type);
    const std::string why = SpreadsheetChartFitMessage(chart);
    if (!why.empty()) {
        if (problem) *problem = why;
        return -1;
    }
    return InsertChart(chart);
}

void UltraCanvasSpreadsheet::ShowInsertChartDialog() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    if (IsEditing()) StopEditing(true);
    if (formulaEngine_ && formulaEngine_->HasPendingChanges()) formulaEngine_->Recalculate();
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    const uint64_t sheetId = sheet->GetId();
    UltraCanvasWindowBase* window = GetWindow();
    ShowSpreadsheetChartGallery(*sheet, GetSelection(), SpreadsheetChartTypeById("column"),
                                [weakSelf, sheetId, window](size_t type) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self || !self->GetActiveSheet() || self->GetActiveSheet()->GetId() != sheetId) return;
        std::string problem;
        if (self->InsertChartOfTypeFromSelection(type, &problem) < 0 && !problem.empty())
            UltraCanvasDialogManager::ShowInformation(problem, Tr("Chart type"), nullptr, window);
    }, window);
}

void UltraCanvasSpreadsheet::ShowChartTypeDialog(int index) {
    const SpreadsheetChart* chart = GetChart(index);
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!chart || !sheet) return;
    if (IsEditing()) StopEditing(true);
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    const uint64_t sheetId = sheet->GetId();
    UltraCanvasWindowBase* window = GetWindow();
    const CellRange data = chart->dataRange.IsSet() ? chart->dataRange.range : GetSelection();
    ShowSpreadsheetChartGallery(*sheet, data, SpreadsheetChartTypeOf(chart->chart),
                                [weakSelf, sheetId, index, window](size_t type) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        const SpreadsheetSheet* active = self->GetActiveSheet();
        const SpreadsheetChart* now = self->GetChart(index);
        if (!active || active->GetId() != sheetId || !now) return;
        SpreadsheetChart changed = *now;
        SetSpreadsheetChartType(changed, *active, type);
        RefreshSpreadsheetChart(changed, [active](const std::string& name) {
            return SameNameIgnoringCase(name, active->GetName()) ? active : nullptr;
        });
        const std::string problem = SpreadsheetChartFitMessage(changed);
        if (!problem.empty()) {
            UltraCanvasDialogManager::ShowInformation(problem, Tr("Chart type"), nullptr, window);
            return;
        }
        self->SetChart(index, changed);
    }, window);
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
    answers->choice = SpreadsheetChartTypeOf(current->chart);
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
    // The type: its name on a button that opens the chart gallery; the
    // field fills the control column.
    auto type = std::make_shared<UltraCanvasButton>("ChartType", 0, 0, 0, 28,
                                                    answers->choice >= 0 ? SpreadsheetChartTypeName(static_cast<size_t>(answers->choice))
                                                                         : Tr("Choose a type"));
    type->SetTooltip(Tr("Every chart type, by category, drawn from this chart's data"));
    {
        UltraCanvasButton* button = type.get();
        const CellRange data = current->dataRange.IsSet() ? current->dataRange.range : GetSelection();
        const SpreadsheetSheet* gallerySheet = sheet;
        UltraCanvasWindowBase* window = GetWindow();
        type->onClick = [answers, button, data, gallerySheet, window]() {
            ShowSpreadsheetChartGallery(*gallerySheet, data, answers->choice, [answers, button](size_t chosen) {
                answers->choice = static_cast<int>(chosen);
                button->SetText(SpreadsheetChartTypeName(chosen));
            }, window);
        };
    }
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
        changed.chart.title = answers->title;
        changed.chart.legend = answers->legend;
        if (changed.dataRange.IsSet() && answers->rows != changed.seriesInRows)
            SetSpreadsheetChartSeriesInRows(changed, *active, answers->rows);
        // A type of another reading reads the block again.
        const int was = SpreadsheetChartTypeOf(changed.chart);
        if (answers->choice >= 0 && answers->choice != was) {
            SetSpreadsheetChartType(changed, *active, static_cast<size_t>(answers->choice));
            changed.chart.title = answers->title;
            changed.chart.legend = answers->legend;
        }
        self->SetChart(index, changed);
    }, GetWindow());
}

} // namespace UltraCanvas
