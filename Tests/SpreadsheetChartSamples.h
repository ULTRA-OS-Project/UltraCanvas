// Tests/SpreadsheetChartSamples.h
// A chart kind's sample data (SampleSpreadsheetChartData) written into cells
// as a person would type it: the series' names across the first row, the
// categories down the first column (a scatter or bubble chart's x values
// there instead), a column of words where a series holds words (a Sankey
// diagram's targets), numbers in the rest. Shared by the spreadsheet chart
// tests.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasSpreadsheetSheet.h"

#include <algorithm>
#include <string>

namespace ChartSamples {

// Writes `kind`'s sample from (row, col) on; the block it fills.
inline UltraCanvas::CellRange WriteSampleCells(UltraCanvas::SpreadsheetSheet& sheet, UltraCanvas::RichChart::Kind kind,
                                               int row, int col) {
    using namespace UltraCanvas;
    const RichChart sample = SampleSpreadsheetChartData(kind);
    const bool takesX = kind == RichChart::Kind::Scatter || kind == RichChart::Kind::Bubble;
    int column = col;
    size_t points = sample.categories.size();
    for (const RichChartSeries& one : sample.series) points = std::max(points, one.values.size());
    // The first column: the names, or the x values.
    if (takesX) {
        const RichChartSeries& first = sample.series.front();
        sheet.SetCellValue(row, column, std::string("x"));
        for (size_t i = 0; i < first.xValues.size(); ++i) sheet.SetCellValue(row + 1 + static_cast<int>(i), column, first.xValues[i]);
        ++column;
    } else if (!sample.categories.empty()) {
        sheet.SetCellValue(row, column, std::string("Name"));
        for (size_t i = 0; i < sample.categories.size(); ++i)
            sheet.SetCellValue(row + 1 + static_cast<int>(i), column, sample.categories[i]);
        ++column;
    }
    for (const RichChartSeries& one : sample.series) {
        sheet.SetCellValue(row, column, one.name);
        for (size_t i = 0; i < points; ++i) {
            const int at = row + 1 + static_cast<int>(i);
            if (i < one.labels.size() && !one.labels[i].empty()) sheet.SetCellValue(at, column, one.labels[i]);
            else if (i < one.values.size()) sheet.SetCellValue(at, column, one.values[i]);
        }
        ++column;
        // A bubble series: its sizes beside it.
        if (kind == RichChart::Kind::Bubble && !one.sizes.empty()) {
            sheet.SetCellValue(row, column, std::string("size"));
            for (size_t i = 0; i < one.sizes.size(); ++i) sheet.SetCellValue(row + 1 + static_cast<int>(i), column, one.sizes[i]);
            ++column;
        }
    }
    return CellRange(row, col, row + static_cast<int>(points), column - 1);
}

} // namespace ChartSamples
