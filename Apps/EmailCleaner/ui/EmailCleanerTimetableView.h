// Apps/EmailCleaner/ui/EmailCleanerTimetableView.h
// The timetable: when a sender's mail actually arrives. The top half is a
// weekday x hour grid (UltraCanvasHeatmapChart) — the shape that makes a
// machine obvious, because a marketing platform fires at 03:00 every Tuesday
// while a person writes across office hours. The bottom half is the same
// traffic over calendar time (UltraCanvasBarChartElement), so a campaign that
// started in March and stopped in June is visible as such.
//
// Scoped by whatever the map view has selected, or the whole mailbox when
// nothing is.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"

#include "EmailCleanerAnalytics.h"

#include <memory>
#include <string>

namespace EmailCleaner {

class TimetableView {
public:
    void SetAnalytics(Analytics* analytics) { analytics_ = analytics; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Redraw for a filter. `title` names what is being shown ("All senders",
    // an address, a domain) and goes in the heading.
    void Refresh(const MessageFilter& filter, const std::string& title);

private:
    void RebuildTimetable();
    void RebuildTimeline();

    Analytics*    analytics_ = nullptr;
    MessageFilter filter_;
    std::string   title_ = "All senders";
    // Auto means "pick from the span"; the segmented control pins it otherwise.
    bool          autoBucket_ = true;
    TimeBucket    bucket_ = TimeBucket::Month;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer>              container_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>                  heading_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>                  peakLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasHeatmapChartElement>    heatmap_;
    std::shared_ptr<UltraCanvas::UltraCanvasBarChartElement>        timeline_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl>       bucketPicker_;
    std::shared_ptr<UltraCanvas::ChartDataVector>                   timelineData_;
};

} // namespace EmailCleaner
