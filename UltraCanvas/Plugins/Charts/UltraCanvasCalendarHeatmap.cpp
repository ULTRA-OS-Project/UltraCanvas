// Plugins/Charts/UltraCanvasCalendarHeatmap.cpp
// Calendar (contribution-graph style) heatmap.
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasCalendarHeatmap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UltraCanvas {

    static const char* kMonthNames[13] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    UltraCanvasCalendarHeatmapElement::UltraCanvasCalendarHeatmapElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasHeatmapChartElement(id, x, y, width, height) {
        // Contribution-graph style defaults.
        SetColormap(HeatmapColormap::Greens);
        SetColorLevels(5);
        SetCellShape(HeatmapCellShape::RoundedRectangle);
        SetCellGap(0.18);
        SetCellCornerRadius(2.0);
        SetRowOrder(HeatmapRowOrder::TopDown);
        SetColumnLabelsOnTop(true);
        SetShowColorBar(false);
        SetNaNColor(Color(235, 237, 240, 255)); // empty / out-of-range day
        SetShowRowLabels(true);
        SetShowColumnLabels(true);
    }

    void UltraCanvasCalendarHeatmapElement::MarkBuild() {
        needsBuild = true;
        RequestRedraw();
    }

    int UltraCanvasCalendarHeatmapElement::DayOfWeekIndex(long serial) const {
        int sun0 = CalendarWeekdaySunday0(serial);   // 0=Sun..6=Sat
        if (weekStart == CalendarWeekStart::Monday) {
            return (sun0 + 6) % 7;                    // 0=Mon..6=Sun
        }
        return sun0;
    }

    void UltraCanvasCalendarHeatmapElement::SetDateRange(int sy, int sm, int sd,
                                                         int ey, int em, int ed) {
        startSerial = CalendarDaysFromCivil(sy, sm, sd);
        endSerial = CalendarDaysFromCivil(ey, em, ed);
        if (endSerial < startSerial) std::swap(startSerial, endSerial);
        hasRange = true;
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::SetValue(int year, int month, int day, double value) {
        dayValues[CalendarDaysFromCivil(year, month, day)] = value;
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::AddValue(int year, int month, int day, double value) {
        dayValues[CalendarDaysFromCivil(year, month, day)] += value;
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::SetDailyValues(int sy, int sm, int sd,
                                                           const std::vector<double>& dailyValues) {
        dayValues.clear();
        startSerial = CalendarDaysFromCivil(sy, sm, sd);
        endSerial = startSerial + static_cast<long>(dailyValues.size()) - 1;
        hasRange = !dailyValues.empty();
        for (size_t i = 0; i < dailyValues.size(); ++i) {
            dayValues[startSerial + static_cast<long>(i)] = dailyValues[i];
        }
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::ClearValues() {
        dayValues.clear();
        hasRange = false;
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::SetWeekStart(CalendarWeekStart start) {
        weekStart = start;
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::SetMissingDayValue(double v) {
        missingDayValue = v;
        MarkBuild();
    }

    void UltraCanvasCalendarHeatmapElement::Build() {
        needsBuild = false;
        if (!hasRange || endSerial < startSerial) {
            ClearData();
            return;
        }

        // Expand to whole weeks: first column starts on the week-start day on or
        // before the range start; last column ends on the week-end day.
        const long alignedStart = startSerial - DayOfWeekIndex(startSerial);
        const long alignedEnd = endSerial + (6 - DayOfWeekIndex(endSerial));
        const int rows = 7;
        const int cols = static_cast<int>((alignedEnd - alignedStart) / 7) + 1;

        std::vector<double> vals(static_cast<size_t>(rows) * cols,
                                 std::numeric_limits<double>::quiet_NaN());
        for (int c = 0; c < cols; ++c) {
            for (int r = 0; r < rows; ++r) {
                long serial = alignedStart + static_cast<long>(c) * 7 + r;
                if (serial < startSerial || serial > endSerial) continue; // padding -> NaN
                auto it = dayValues.find(serial);
                vals[static_cast<size_t>(r) * cols + c] =
                        (it != dayValues.end()) ? it->second : missingDayValue;
            }
        }
        SetData(vals, cols, rows);
        SetAutoValueRange(true);

        // Weekday labels (rows): show Mon/Wed/Fri like the GitHub graph.
        static const char* sunNames[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        std::vector<std::string> rowLabels(rows);
        for (int r = 0; r < rows; ++r) {
            // Map row index back to a Sunday-based weekday for naming.
            int sun0 = (weekStart == CalendarWeekStart::Monday) ? (r + 1) % 7 : r;
            bool show = (sun0 == 1 || sun0 == 3 || sun0 == 5); // Mon, Wed, Fri
            rowLabels[r] = show ? sunNames[sun0] : "";
        }
        SetRowLabels(rowLabels);

        // Month labels (columns): label the column where the month of its top
        // cell changes.
        std::vector<std::string> colLabels(cols);
        int prevMonth = -1;
        for (int c = 0; c < cols; ++c) {
            long serial = alignedStart + static_cast<long>(c) * 7; // top cell of the column
            int y, m, d;
            CalendarCivilFromDays(serial, y, m, d);
            if (m != prevMonth) {
                colLabels[c] = kMonthNames[m];
                prevMonth = m;
            }
        }
        SetColumnLabels(colLabels);
    }

    void UltraCanvasCalendarHeatmapElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (needsBuild) {
            Build();
        }
        UltraCanvasHeatmapChartElement::Render(ctx, dirtyRect);
    }

} // namespace UltraCanvas
