// Apps/EmailCleaner/ui/EmailCleanerTimetableView.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerTimetableView.h"

#include <string>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {
constexpr float kHeadingH = 24.0f;
constexpr float kPeakH    = 20.0f;
constexpr float kPickerH  = 26.0f;
} // namespace

std::shared_ptr<UltraCanvasContainer> TimetableView::Build(float x, float y,
                                                           float width, float height) {
    container_ = CreateContainer("ecTimetableView", x, y, width, height);

    heading_ = CreateLabel("ecTimetableHeading", 8, 6, width - 16, kHeadingH,
                           "All senders");
    heading_->SetFontSize(15.0f);
    container_->AddChild(heading_);

    peakLabel_ = CreateLabel("ecTimetablePeak", 8, 6 + kHeadingH, width - 16, kPeakH,
                             "No dated messages");
    container_->AddChild(peakLabel_);

    // ---- Weekday x hour grid ----------------------------------------------
    const float gridY = 6 + kHeadingH + kPeakH + 4;
    const float gridH = (height - gridY - kPickerH - 24) * 0.5f;

    heatmap_ = CreateHeatmapChartElement("ecTimetableGrid", 8, static_cast<int>(gridY),
                                         static_cast<int>(width - 16),
                                         static_cast<int>(gridH));
    heatmap_->SetChartTitle("Arrival time (UTC)");
    heatmap_->SetColormap(HeatmapColormap::Blues);
    heatmap_->SetShowCellBorders(true);
    heatmap_->SetShowCellValues(false);
    heatmap_->SetRowLabels({ WeekdayName(0), WeekdayName(1), WeekdayName(2),
                             WeekdayName(3), WeekdayName(4), WeekdayName(5),
                             WeekdayName(6) });
    std::vector<std::string> hourLabels;
    hourLabels.reserve(Timetable::Hours);
    for (int hour = 0; hour < Timetable::Hours; ++hour) {
        // Label every third hour; the rest would not fit at any sane width.
        hourLabels.push_back((hour % 3 == 0) ? (hour < 10 ? "0" + std::to_string(hour)
                                                          : std::to_string(hour))
                                             : "");
    }
    heatmap_->SetColumnLabels(hourLabels);
    container_->AddChild(heatmap_);

    // ---- Traffic over time -------------------------------------------------
    const float chartY = gridY + gridH + 8;

    container_->AddChild(CreateLabel("ecTimelineLabel", 8, chartY, 60, 20, "Buckets"));
    bucketPicker_ = CreateSegmentedControl("ecTimelineBucket", 72, chartY - 3, 320, kPickerH);
    bucketPicker_->AddSegment("Auto");
    bucketPicker_->AddSegment("Day");
    bucketPicker_->AddSegment("Week");
    bucketPicker_->AddSegment("Month");
    bucketPicker_->AddSegment("Year");
    bucketPicker_->SetSelectedIndex(0);
    bucketPicker_->onSegmentSelected = [this](int index) {
        autoBucket_ = (index == 0);
        switch (index) {
            case 1:  bucket_ = TimeBucket::Day; break;
            case 2:  bucket_ = TimeBucket::Week; break;
            case 3:  bucket_ = TimeBucket::Month; break;
            case 4:  bucket_ = TimeBucket::Year; break;
            default: break;
        }
        RebuildTimeline();
    };
    container_->AddChild(bucketPicker_);

    timelineData_ = std::make_shared<ChartDataVector>();
    timeline_ = CreateBarChartElement("ecTimeline", 8, static_cast<int>(chartY + kPickerH + 4),
                                      static_cast<int>(width - 16),
                                      static_cast<int>(height - chartY - kPickerH - 12));
    timeline_->SetChartTitle("Messages over time");
    timeline_->SetDataSource(timelineData_);
    timeline_->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    timeline_->SetRotateXAxisLabels(true, 45.0f);
    timeline_->SetBarColor(Color(52, 152, 219, 255));
    container_->AddChild(timeline_);
    return container_;
}

void TimetableView::Refresh(const MessageFilter& filter, const std::string& title) {
    filter_ = filter;
    title_  = title.empty() ? "All senders" : title;
    if (heading_) heading_->SetText(title_);
    RebuildTimetable();
    RebuildTimeline();
}

void TimetableView::RebuildTimetable() {
    if (!analytics_ || !heatmap_) return;

    Timetable table;
    if (!analytics_->BuildTimetable(filter_, table)) {
        if (peakLabel_) peakLabel_->SetText("Could not read the analysis database.");
        return;
    }

    std::vector<double> cells;
    cells.reserve(static_cast<size_t>(Timetable::Days) * Timetable::Hours);
    for (int day = 0; day < Timetable::Days; ++day) {
        for (int hour = 0; hour < Timetable::Hours; ++hour)
            cells.push_back(static_cast<double>(table.At(day, hour)));
    }
    heatmap_->SetData(cells, Timetable::Hours, Timetable::Days);
    // Pin the low end at zero so a quiet hour reads as empty rather than as
    // "the least busy", which is what an auto range would show.
    heatmap_->SetValueRange(0.0, table.peakCount > 0 ? table.peakCount : 1.0);

    if (peakLabel_) peakLabel_->SetText(Analytics::DescribeTimetable(table));
}

void TimetableView::RebuildTimeline() {
    if (!analytics_ || !timeline_ || !timelineData_) return;

    TimeBucket bucket = bucket_;
    if (autoBucket_) {
        // The overview carries the corpus span, which is what decides how
        // fine the buckets can be without producing 3,000 columns.
        StoreOverview overview;
        if (analytics_->BuildOverview(filter_, overview))
            bucket = Analytics::ChooseBucket(overview.firstDate, overview.lastDate);
    }

    std::vector<TimelinePoint> points;
    if (!analytics_->BuildTimeline(filter_, bucket, points)) return;

    timelineData_->Clear();
    for (size_t i = 0; i < points.size(); ++i) {
        const TimelinePoint& point = points[i];
        ChartDataPoint dataPoint(static_cast<double>(i),
                                 static_cast<double>(point.messageCount),
                                 0.0, point.label,
                                 static_cast<double>(point.messageCount));
        // Colour a bucket by how much of it was unwanted, so a spam campaign
        // stands out from ordinary traffic in the same chart.
        if (point.messageCount > 0 && point.unwantedCount > 0) {
            SenderBlock share;
            share.messageCount  = point.messageCount;
            share.unwantedCount = point.unwantedCount;
            share.topCategory   = MessageCategory::ProductSpam;
            const MapColor color = Analytics::BlockColor(share);
            dataPoint.color = Color(color.r, color.g, color.b, 255);
        }
        timelineData_->AddPoint(dataPoint);
    }
    timeline_->SetDataSource(timelineData_);
    timeline_->SetChartTitle("Messages per " + ToString(bucket));
}

} // namespace EmailCleaner
