// Apps/EmailCleaner/engine/EmailCleanerAnalytics.h
// The view models the analysis database feeds: the two-level sender map
// (domain -> sender) the map view draws as a treemap, the weekday x hour
// timetable, the timeline series, and the little summaries that go beside
// them.
//
// It sits between the store and the UI so the shaping rules — which domains
// survive, what "Other" collects, what colour a category is — are decided
// once, headlessly, and can be unit-tested without a window.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerStore.h"
#include "EmailCleanerTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace EmailCleaner {

// An RGB colour, kept UI-free so the engine does not depend on UltraCanvas.
struct MapColor {
    uint8_t r = 0, g = 0, b = 0;
};

// One cell of the map view. Leaf nodes are senders; their parents are domains.
struct MapNode {
    std::string key;          // sender address, or domain for a parent
    std::string label;        // what the cell shows
    std::string tooltip;      // the block summary
    double      value = 0.0;  // the chosen metric
    MessageCategory category = MessageCategory::Unclassified;
    MapColor    color;
    SenderBlock block;        // the numbers behind the cell (empty for "Other")
    bool        isAggregate = false;   // the "Other" catch-all cell
    std::vector<MapNode> children;

    bool IsLeaf() const { return children.empty(); }
};

// How much of the map to draw. A treemap with 4000 cells is a texture, not a
// picture, so the shaping caps both levels and rolls the tail into "Other".
struct MapShape {
    int  maxDomains          = 24;
    int  maxSendersPerDomain = 12;
    bool groupByDomain       = true;   // false = one flat level of senders
    // Domains contributing less than this share of the total are pooled into
    // "Other" even when they fit under maxDomains.
    double minShare = 0.004;
};

class Analytics {
public:
    explicit Analytics(const AnalysisStore& store) : store_(store) {}

    // The sender map for a filter. Returns false when the store query fails;
    // an empty database yields an empty root, not a failure.
    bool BuildSenderMap(const MessageFilter& filter, SenderMetric metric,
                        const MapShape& shape, MapNode& out) const;

    // The timetable grid for a filter (typically one sender).
    bool BuildTimetable(const MessageFilter& filter, Timetable& out) const;

    // The timeline series for a filter. `bucket` picks the granularity;
    // ChooseBucket() below picks a sensible one from the data's span.
    bool BuildTimeline(const MessageFilter& filter, TimeBucket bucket,
                       std::vector<TimelinePoint>& out) const;

    // Pick a bucket that yields a readable number of columns for a span.
    static TimeBucket ChooseBucket(int64_t firstDate, int64_t lastDate);

    // The one-glance numbers for a filter (also what picks the auto bucket).
    bool BuildOverview(const MessageFilter& filter, StoreOverview& out) const;

    // Category rollup ordered for a legend (taxonomy order, empties dropped).
    bool BuildCategoryLegend(const MessageFilter& filter,
                             std::vector<CategoryTotal>& out) const;

    // ---- Presentation helpers (pure) ---------------------------------------

    // The palette shared by the map cells, the legend and the timeline.
    static MapColor CategoryColor(MessageCategory category);
    // Shaded towards red as a block's unwanted ratio rises, so a mostly-clean
    // sender and a mostly-spam one differ at a glance even within a category.
    static MapColor BlockColor(const SenderBlock& block);

    // "312 messages · 84% unwanted · 12.4 MB · Mar 2024 - Aug 2026"
    static std::string DescribeBlock(const SenderBlock& block);
    // "Busiest on Tue 09:00 (23 messages)" — the timetable's headline.
    static std::string DescribeTimetable(const Timetable& table);
    // "1,204 messages from 87 senders · 214 unwanted (18%)"
    static std::string DescribeOverview(const StoreOverview& overview);

private:
    const AnalysisStore& store_;
};

} // namespace EmailCleaner
