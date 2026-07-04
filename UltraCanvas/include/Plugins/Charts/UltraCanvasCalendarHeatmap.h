// include/Plugins/Charts/UltraCanvasCalendarHeatmap.h
// Calendar (contribution-graph style) heatmap: weeks as columns, weekdays as
// rows, cell colour encoding a per-day value. A thin date-aware layer on top of
// UltraCanvasHeatmapChart.
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasCalendarDate.h"
#include <map>
#include <vector>
#include <string>

namespace UltraCanvas {

    enum class CalendarWeekStart {
        Sunday,
        Monday
    };

    class UltraCanvasCalendarHeatmapElement : public UltraCanvasHeatmapChartElement {
    private:
        std::map<long, double> dayValues;   // serial day -> value
        long startSerial = 0;
        long endSerial = -1;
        bool hasRange = false;
        CalendarWeekStart weekStart = CalendarWeekStart::Sunday;
        double missingDayValue = 0.0;       // value for in-range days with no entry
        bool needsBuild = false;

    public:
        UltraCanvasCalendarHeatmapElement(const std::string& id, int x, int y, int width, int height);

        // ---- Range / data ----
        void SetDateRange(int startYear, int startMonth, int startDay,
                          int endYear, int endMonth, int endDay);
        // Set/replace the value for one day (does not change the range).
        void SetValue(int year, int month, int day, double value);
        // Accumulate into a day's value.
        void AddValue(int year, int month, int day, double value);
        // Convenience: consecutive daily values starting at the given date; sets
        // the range to cover exactly those days.
        void SetDailyValues(int startYear, int startMonth, int startDay,
                            const std::vector<double>& dailyValues);
        void ClearValues();

        // ---- Configuration ----
        void SetWeekStart(CalendarWeekStart start);
        void SetMissingDayValue(double v);  // value used for in-range days with no entry

        // Build the matrix now (normally done lazily on the next render).
        void Build();

        // ---- Overrides ----
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        int DayOfWeekIndex(long serial) const;   // 0..6 honouring weekStart
        void MarkBuild();
    };

    inline std::shared_ptr<UltraCanvasCalendarHeatmapElement> CreateCalendarHeatmapElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasCalendarHeatmapElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
