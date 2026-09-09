// Plugins/Charts/UltraCanvasTimelineChart.cpp
// Chronological timeline: milestones and spans to scale on a real date axis
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasTimelineChart.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// LOCAL HELPERS
// =============================================================================

    static Color MixColors(const Color& a, const Color& b, float t) {
        auto mix = [t](int ca, int cb) {
            return static_cast<uint8_t>(std::clamp(
                    static_cast<int>(ca + (cb - ca) * t + 0.5f), 0, 255));
        };
        return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255);
    }

    static Color LightenColor(const Color& c, float t) {
        return MixColors(c, Color(255, 255, 255, 255), t);
    }

    static Color DarkenColor(const Color& c, float t) {
        return MixColors(c, Color(0, 0, 0, 255), t);
    }

    static Color WithAlpha(const Color& c, int alpha) {
        return Color(c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(alpha, 0, 255)));
    }

    static Color ContrastText(const Color& background) {
        const double luminance =
                (0.299 * background.r + 0.587 * background.g + 0.114 * background.b) / 255.0;
        return luminance > 0.62 ? Color(38, 42, 51, 255) : Color(255, 255, 255, 255);
    }

    static Rect2Dd UnionRect(const Rect2Dd& a, const Rect2Dd& b) {
        if (a.width <= 0 || a.height <= 0) return b;
        if (b.width <= 0 || b.height <= 0) return a;
        const double x0 = std::min(a.x, b.x);
        const double y0 = std::min(a.y, b.y);
        const double x1 = std::max(a.x + a.width, b.x + b.width);
        const double y1 = std::max(a.y + a.height, b.y + b.height);
        return Rect2Dd(x0, y0, x1 - x0, y1 - y0);
    }

    // Shelf packer: rows are assigned from horizontal extents only, then each
    // row's height is taken from its tallest occupant and the offsets computed.
    // Spans and milestone callouts share one shelf per side, so a label can
    // never be placed on top of a bar.
    struct TimelineShelf {
        // Occupied x-intervals per row. Items are NOT placed in date order -
        // point events are placed by importance - so each row keeps its whole
        // interval list rather than just a right edge.
        std::vector<std::vector<std::pair<double, double>>> rows;
        std::vector<double> height;
        std::vector<double> offset;   // Distance from the axis to the row's near edge

        bool Fits(size_t row, double x0, double x1, double gap) const {
            for (const auto& span : rows[row]) {
                if (x0 < span.second + gap && span.first - gap < x1) return false;
            }
            return true;
        }

        // Returns -1 when nothing fits, so the caller can drop a label rather
        // than overprint one. Pass allowOverflow for items that must be placed.
        int Place(double x0, double x1, double rowHeight, int maxRows, double gap,
                  bool allowOverflow = false) {
            for (size_t i = 0; i < rows.size(); ++i) {
                if (Fits(i, x0, x1, gap)) {
                    rows[i].emplace_back(x0, x1);
                    height[i] = std::max(height[i], rowHeight);
                    return static_cast<int>(i);
                }
            }
            if (static_cast<int>(rows.size()) < std::max(1, maxRows)) {
                rows.push_back({{x0, x1}});
                height.push_back(rowHeight);
                return static_cast<int>(rows.size()) - 1;
            }
            if (!allowOverflow) return -1;
            // Must be placed: use the row with the least total occupancy
            size_t best = 0;
            double bestLoad = 1e18;
            for (size_t i = 0; i < rows.size(); ++i) {
                double load = 0.0;
                for (const auto& span : rows[i]) load += span.second - span.first;
                if (load < bestLoad) { bestLoad = load; best = i; }
            }
            rows[best].emplace_back(x0, x1);
            height[best] = std::max(height[best], rowHeight);
            return static_cast<int>(best);
        }

        void ComputeOffsets(double firstGap, double rowGap) {
            offset.assign(height.size(), 0.0);
            double cursor = firstGap;
            for (size_t i = 0; i < height.size(); ++i) {
                offset[i] = cursor;
                cursor += height[i] + rowGap;
            }
        }

        double Offset(int row) const {
            return (row >= 0 && row < static_cast<int>(offset.size())) ? offset[row] : 0.0;
        }
        double Height(int row) const {
            return (row >= 0 && row < static_cast<int>(height.size())) ? height[row] : 0.0;
        }
    };

// =============================================================================
// DATA SOURCE
// =============================================================================

    int TimelineChartDataSource::IndexOf(int id) const {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) return static_cast<int>(i);
        }
        return -1;
    }

    int TimelineChartDataSource::AddEntry(const TimelineChartEntry& entry) {
        entries.push_back(entry);
        const int id = nextId++;
        ids.push_back(id);
        return id;
    }

    int TimelineChartDataSource::AddMilestone(const std::string& name, double serial) {
        TimelineChartEntry entry;
        entry.kind = TimelineEntryKind::Milestone;
        entry.name = name;
        entry.start = serial;
        entry.end = serial;
        return AddEntry(entry);
    }

    int TimelineChartDataSource::AddMilestone(const std::string& name,
                                              int year, int month, int day) {
        return AddMilestone(name, TimeAxis::Serial(year, month, day));
    }

    int TimelineChartDataSource::AddSpan(const std::string& name, double from, double to) {
        TimelineChartEntry entry;
        entry.kind = TimelineEntryKind::Span;
        entry.name = name;
        entry.start = std::min(from, to);
        entry.end = std::max(from, to);
        return AddEntry(entry);
    }

    int TimelineChartDataSource::AddSpan(const std::string& name,
                                         int y1, int m1, int d1, int y2, int m2, int d2) {
        return AddSpan(name, TimeAxis::Serial(y1, m1, d1), TimeAxis::Serial(y2, m2, d2));
    }

    int TimelineChartDataSource::AddEra(const std::string& name, double from, double to) {
        TimelineChartEntry entry;
        entry.kind = TimelineEntryKind::Era;
        entry.name = name;
        entry.start = std::min(from, to);
        entry.end = std::max(from, to);
        return AddEntry(entry);
    }

    int TimelineChartDataSource::AddBookend(const std::string& name, double serial) {
        TimelineChartEntry entry;
        entry.kind = TimelineEntryKind::Bookend;
        entry.name = name;
        entry.start = serial;
        entry.end = serial;
        entry.marker = TimelineMarkerStyle::TriangleUp;
        return AddEntry(entry);
    }

    bool TimelineChartDataSource::RemoveEntry(int id) {
        const int index = IndexOf(id);
        if (index < 0) return false;
        entries.erase(entries.begin() + index);
        ids.erase(ids.begin() + index);
        return true;
    }

    void TimelineChartDataSource::Clear() {
        entries.clear();
        ids.clear();
    }

    TimelineChartEntry* TimelineChartDataSource::GetEntry(int id) {
        const int index = IndexOf(id);
        return index < 0 ? nullptr : &entries[index];
    }

    const TimelineChartEntry* TimelineChartDataSource::GetEntry(int id) const {
        const int index = IndexOf(id);
        return index < 0 ? nullptr : &entries[index];
    }

    bool TimelineChartDataSource::SetEntryColor(int id, const Color& color) {
        if (auto* e = GetEntry(id)) { e->color = color; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntryDetail(int id, const std::string& detail) {
        if (auto* e = GetEntry(id)) { e->detail = detail; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntryMarker(int id, TimelineMarkerStyle marker) {
        if (auto* e = GetEntry(id)) { e->marker = marker; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntryLane(int id, int lane) {
        if (auto* e = GetEntry(id)) { e->lane = lane; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntrySide(int id, int side) {
        if (auto* e = GetEntry(id)) { e->side = side; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntryProgress(int id, float progress) {
        if (auto* e = GetEntry(id)) { e->progress = progress; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntryImportance(int id, float importance) {
        if (auto* e = GetEntry(id)) { e->importance = importance; return true; }
        return false;
    }
    bool TimelineChartDataSource::SetEntrySwimlane(int id, const std::string& swimlaneName) {
        if (auto* e = GetEntry(id)) { e->swimlaneName = swimlaneName; return true; }
        return false;
    }

    bool TimelineChartDataSource::SetEntryDates(int id, double start, double end) {
        if (auto* e = GetEntry(id)) {
            e->start = std::min(start, end);
            e->end = std::max(start, end);
            return true;
        }
        return false;
    }

    bool TimelineChartDataSource::GetDateBounds(double& outStart, double& outEnd) const {
        if (entries.empty()) return false;
        bool first = true;
        for (const auto& e : entries) {
            const double lo = e.start - e.startUncertainty;
            const double hi = std::max(e.start, e.end) + e.endUncertainty;
            if (first) { outStart = lo; outEnd = hi; first = false; }
            else {
                outStart = std::min(outStart, lo);
                outEnd = std::max(outEnd, hi);
            }
        }
        if (outEnd - outStart < 1.0) outEnd = outStart + 1.0;
        return true;
    }

    void TimelineChartDataSource::LoadTasks(const std::vector<SimpleTask>& tasks) {
        for (const auto& task : tasks) {
            if (task.milestone || task.end <= task.start) {
                AddMilestone(task.name, task.start);
            } else {
                AddSpan(task.name, task.start, task.end);
            }
        }
    }

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasTimelineChart::UltraCanvasTimelineChart(const std::string& id,
                                                       int x, int y, int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        enableZoom = true;
        enablePan = true;
        showGrid = false;
        showAxes = false;
        backgroundColor = Color(252, 252, 253, 255);
        data = std::make_shared<TimelineChartDataSource>();
        style = TimelineChartStyles::CreateForDesign(design);
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasTimelineChart::SetData(std::shared_ptr<TimelineChartDataSource> source) {
        data = source ? source : std::make_shared<TimelineChartDataSource>();
        hoveredEntry = TimelineEntryRef();
        selectedEntry = TimelineEntryRef();
        viewInitialized = false;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::DataChanged() {
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::LoadSampleData() {
        SetData(TimelineChartSamples::DevelopmentTimeline(2026));
    }

// =============================================================================
// DESIGN, STYLE & THEME
// =============================================================================

    void UltraCanvasTimelineChart::SetDesign(TimelineChartDesign d) {
        if (design == d) return;
        design = d;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::ApplyDesign(TimelineChartDesign d) {
        design = d;
        style = TimelineChartStyles::CreateForDesign(d);
        if (d == TimelineChartDesign::Dark) {
            darkTheme = false;
            SetDarkTheme(true);
        } else if (darkTheme) {
            darkTheme = false;
            SetDarkTheme(true);
        }
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetStyle(const TimelineChartStyle& s) {
        style = s;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::StyleChanged() {
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        if (dark) {
            backgroundColor = Color(30, 33, 40, 255);
            style.textColor = Color(235, 238, 243, 255);
            style.mutedTextColor = Color(155, 162, 176, 255);
            style.axisColor = Color(78, 85, 98, 255);
            style.gridColor = Color(54, 59, 70, 255);
            style.headerFillColor = Color(41, 45, 54, 255);
            style.headerTextColor = Color(178, 186, 200, 255);
            style.weekendColor = Color(37, 41, 49, 255);
            style.leaderColor = Color(92, 100, 114, 255);
        } else {
            TimelineChartStyle preset = TimelineChartStyles::CreateForDesign(
                    design == TimelineChartDesign::Dark ? TimelineChartDesign::Modern : design);
            backgroundColor = Color(252, 252, 253, 255);
            style.textColor = preset.textColor;
            style.mutedTextColor = preset.mutedTextColor;
            style.axisColor = preset.axisColor;
            style.gridColor = preset.gridColor;
            style.headerFillColor = preset.headerFillColor;
            style.headerTextColor = preset.headerTextColor;
            style.weekendColor = preset.weekendColor;
            style.leaderColor = preset.leaderColor;
        }
        RequestRedraw();
    }

// =============================================================================
// PALETTE
// =============================================================================

    void UltraCanvasTimelineChart::SetLaneMode(TimelineLaneMode mode) {
        if (laneMode == mode) return;
        laneMode = mode;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetSwimlanes(const std::vector<TimelineSwimlane>& lanes) {
        swimlanes = lanes;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::ClearSwimlanes() {
        swimlanes.clear();
        InvalidateLayout();
        RequestRedraw();
    }

    int UltraCanvasTimelineChart::FindSwimlane(const std::string& name) const {
        for (size_t i = 0; i < swimlanes.size(); ++i) {
            if (swimlanes[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    const std::vector<TimelineSwimlane>& UltraCanvasTimelineChart::GetResolvedSwimlanes() const {
        return layout.lanes;
    }

    bool UltraCanvasTimelineChart::GetSwimlaneRect(size_t index, Rect2Dd& outRect) const {
        if (!layout.valid || index >= layout.laneBands.size()) return false;
        outRect = layout.laneBands[index].rect;
        return true;
    }

    void UltraCanvasTimelineChart::SetPalette(TimelineChartPalette p) {
        palette = p;
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetCustomPalette(const std::vector<Color>& colors) {
        customPalette = colors;
        palette = TimelineChartPalette::Custom;
        RequestRedraw();
    }

    const std::vector<Color>& UltraCanvasTimelineChart::GetPaletteColors() const {
        static std::vector<Color> cached;
        static TimelineChartPalette cachedKind = TimelineChartPalette::Custom;
        if (palette == TimelineChartPalette::Custom && !customPalette.empty()) {
            return customPalette;
        }
        if (cached.empty() || cachedKind != palette) {
            cached = TimelineChartStyles::GetPaletteColors(palette);
            cachedKind = palette;
        }
        return cached;
    }

    Color UltraCanvasTimelineChart::EntryColor(size_t index) const {
        const auto& entries = data->GetEntries();
        if (index < entries.size() && entries[index].color.a > 0) {
            return entries[index].color;
        }
        const std::vector<Color>& colors = GetPaletteColors();
        if (colors.empty()) return Color(41, 128, 185, 255);
        return colors[index % colors.size()];
    }

// =============================================================================
// AXIS & VIEW
// =============================================================================

    void UltraCanvasTimelineChart::SetScale(TimelineScale scale) {
        if (style.scale == scale) return;
        style.scale = scale;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetAxisPosition(TimelineAxisPosition position) {
        if (style.axisPosition == position) return;
        style.axisPosition = position;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetDateRange(double startSerial, double endSerial) {
        axis.SetRange(startSerial, endSerial);
        viewInitialized = true;
        InvalidateLayout();
        RequestRedraw();
        if (onViewChanged) onViewChanged();
    }

    void UltraCanvasTimelineChart::SetDateRange(int y1, int m1, int d1,
                                                int y2, int m2, int d2) {
        SetDateRange(TimeAxis::Serial(y1, m1, d1), TimeAxis::Serial(y2, m2, d2));
    }

    void UltraCanvasTimelineChart::GetDateRange(double& outStart, double& outEnd) const {
        outStart = axis.GetRangeStart();
        outEnd = axis.GetRangeEnd();
    }

    void UltraCanvasTimelineChart::FitToData() {
        double lo = 0.0, hi = 30.0;
        if (data && data->GetDateBounds(lo, hi)) {
            axis.SetRange(lo, hi);
            axis.Pad(style.axisPadFraction);
        } else {
            axis.SetRange(lo, hi);
        }
        viewInitialized = true;
        InvalidateLayout();
        RequestRedraw();
        if (onViewChanged) onViewChanged();
    }

    void UltraCanvasTimelineChart::EnsureView() {
        if (!viewInitialized) FitToData();
    }

    void UltraCanvasTimelineChart::SetNow(double serial) {
        nowSerial = serial;
        hasNow = true;
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::SetNow(int year, int month, int day) {
        SetNow(TimeAxis::Serial(year, month, day));
    }

    void UltraCanvasTimelineChart::ClearNow() {
        hasNow = false;
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::ZoomBy(double factor) {
        EnsureView();
        axis.ZoomAbout((layout.plotArea.x + layout.plotArea.width / 2.0), factor);
        InvalidateLayout();
        RequestRedraw();
        if (onViewChanged) onViewChanged();
    }

    void UltraCanvasTimelineChart::PanDays(double days) {
        EnsureView();
        axis.SetRange(axis.GetRangeStart() + days, axis.GetRangeEnd() + days);
        InvalidateLayout();
        RequestRedraw();
        if (onViewChanged) onViewChanged();
    }

    void UltraCanvasTimelineChart::SetSubtitle(const std::string& text) {
        subtitle = text;
        InvalidateLayout();
        RequestRedraw();
    }

// =============================================================================
// SELECTION & QUERIES
// =============================================================================

    void UltraCanvasTimelineChart::SetSelectedIndex(int index) {
        TimelineEntryRef next;
        if (index >= 0 && index < static_cast<int>(data->Count())) next.index = index;
        if (next == selectedEntry) return;
        selectedEntry = next;
        if (onSelectionChange) onSelectionChange();
        RequestRedraw();
    }

    void UltraCanvasTimelineChart::ClearSelection() {
        if (!selectedEntry.IsValid()) return;
        selectedEntry = TimelineEntryRef();
        RequestRedraw();
    }

    bool UltraCanvasTimelineChart::GetEntryRect(size_t index, Rect2Dd& outRect) const {
        if (!layout.valid || index >= layout.entries.size()) return false;
        if (!layout.entries[index].visible) return false;
        outRect = layout.entries[index].shapeRect;
        return true;
    }

// =============================================================================
// LAYOUT
// =============================================================================

    double UltraCanvasTimelineChart::TitleBandHeight() const {
        double h = 0.0;
        if (!chartTitle.empty()) h += 30.0;
        if (!subtitle.empty()) h += 19.0;
        if (h > 0.0) h += 6.0;
        return h;
    }

    Color UltraCanvasTimelineChart::SwimlaneColor(size_t index) const {
        if (index < layout.lanes.size() && layout.lanes[index].color.a > 0) {
            return layout.lanes[index].color;
        }
        const std::vector<Color>& colors = GetPaletteColors();
        if (colors.empty()) return Color(41, 128, 185, 255);
        return colors[index % colors.size()];
    }

    int UltraCanvasTimelineChart::SwimlaneOf(const TimelineChartEntry& entry) const {
        for (size_t i = 0; i < layout.lanes.size(); ++i) {
            if (layout.lanes[i].name == entry.swimlaneName) return static_cast<int>(i);
        }
        return layout.lanes.empty() ? -1 : 0;   // Unassigned entries fall into row 0
    }

    void UltraCanvasTimelineChart::ResolveSwimlanes() {
        layout.lanes = swimlanes;
        if (!layout.lanes.empty()) return;

        // Derive the rows from the data, in first-appearance order
        for (const auto& entry : data->GetEntries()) {
            if (entry.kind == TimelineEntryKind::Era) continue;
            bool known = false;
            for (const auto& lane : layout.lanes) {
                if (lane.name == entry.swimlaneName) { known = true; break; }
            }
            if (!known) layout.lanes.emplace_back(entry.swimlaneName);
        }
        if (layout.lanes.empty()) layout.lanes.emplace_back(std::string());
    }

    void UltraCanvasTimelineChart::UpdateLayout(IRenderContext* ctx) {
        EnsureView();

        const double top = TitleBandHeight();
        const double headerH = (style.showMajorTier ? style.majorTierHeight : 0.0) +
                               (style.showMinorTier ? style.minorTierHeight : 0.0);

        const bool swimlaneMode = (laneMode == TimelineLaneMode::Swimlanes);
        layout.lanes.clear();
        layout.laneBands.clear();
        if (swimlaneMode) ResolveSwimlanes();

        // Swimlane rows need a name column on the left; the plot starts after it
        const double fullWidth = std::max(40.0, static_cast<double>(GetWidth()) - 24.0);
        const double gutterW = (swimlaneMode && style.showSwimlaneHeaders)
                ? std::min(style.swimlaneHeaderWidth, fullWidth * 0.4) : 0.0;

        layout.gutterArea = Rect2Dd(12.0, top + headerH, gutterW,
                                    std::max(40.0, static_cast<double>(GetHeight()) - top - headerH - 12.0));
        layout.headerArea = Rect2Dd(12.0 + gutterW, top, fullWidth - gutterW, headerH);
        layout.plotArea = Rect2Dd(12.0 + gutterW, top + headerH,
                                  fullWidth - gutterW,
                                  std::max(40.0, static_cast<double>(GetHeight()) - top - headerH - 12.0));

        // Rows have identity in swimlane mode, so the axis has to sit at the top
        const TimelineAxisPosition axisPosition = swimlaneMode
                ? TimelineAxisPosition::Top : style.axisPosition;
        switch (axisPosition) {
            case TimelineAxisPosition::Top:
                layout.axisY = layout.plotArea.y + style.axisThickness;
                break;
            case TimelineAxisPosition::Bottom:
                layout.axisY = layout.plotArea.y + layout.plotArea.height - style.axisThickness;
                break;
            case TimelineAxisPosition::Center:
            default:
                layout.axisY = layout.plotArea.y + layout.plotArea.height / 2.0;
                break;
        }

        axis.SetPixelRange(layout.plotArea.x, layout.plotArea.x + layout.plotArea.width);

        layout.entries.assign(data->Count(), EntryLayout());
        layout.eraBands.clear();
        layout.eraIndices.clear();

        LayoutEras();
        if (swimlaneMode) LayoutSwimlaneEntries(ctx);
        else LayoutEntries(ctx);

        for (auto& el : layout.entries) {
            el.hitRect = UnionRect(el.shapeRect, el.labelRect);
        }
        layout.valid = true;
    }

    void UltraCanvasTimelineChart::LayoutEras() {
        const auto& entries = data->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind != TimelineEntryKind::Era) continue;
            const double x0 = axis.ToPixel(entries[i].start);
            const double x1 = axis.ToPixel(entries[i].end);
            if (x1 < layout.plotArea.x || x0 > layout.plotArea.x + layout.plotArea.width) {
                continue;
            }
            const double left = std::max(x0, layout.plotArea.x);
            const double right = std::min(x1, layout.plotArea.x + layout.plotArea.width);
            Rect2Dd band(left, layout.plotArea.y, std::max(1.0, right - left),
                         layout.plotArea.height);
            layout.eraBands.push_back(band);
            layout.eraIndices.push_back(i);

            EntryLayout& el = layout.entries[i];
            el.entryIndex = static_cast<int>(i);
            el.color = EntryColor(i);
            el.shapeRect = band;
            el.visible = true;
        }
    }

    void UltraCanvasTimelineChart::LayoutEntries(IRenderContext* ctx) {
        const auto& entries = data->GetEntries();
        const double plotLeft = layout.plotArea.x;
        const double plotRight = layout.plotArea.x + layout.plotArea.width;

        const double roomAbove = layout.axisY - layout.plotArea.y - style.axisGap;
        const double roomBelow = layout.plotArea.y + layout.plotArea.height -
                                 layout.axisY - style.axisGap;
        const double rowStep = style.laneHeight + style.laneGap;
        const int maxAbove = std::max(1, static_cast<int>(roomAbove / rowStep));
        const int maxBelow = std::max(1, static_cast<int>(roomBelow / rowStep));

        TimelineShelf above, below;

        // What the second pass needs to turn a row assignment into rectangles
        struct Pending {
            size_t index = 0;
            bool isSpan = false;
            double barX0 = 0.0;
            double barWidth = 0.0;
            double labelX = 0.0;
            double labelWidth = 0.0;
            double labelHeight = 0.0;
            double markerSize = 0.0;
            bool labelInside = false;
            bool hasLabel = false;
        };
        std::vector<Pending> pending;
        pending.reserve(entries.size());

        int autoSideCounter = 0;
        const bool labelsHidden = (style.labelPlacement == TimelineLabelPlacement::Hidden);

        // Spans keep document order; point events are placed most-important
        // first, so when space runs out it is the minor labels that are dropped.
        std::vector<size_t> order;
        order.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == TimelineEntryKind::Span) order.push_back(i);
        }
        std::vector<size_t> points;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == TimelineEntryKind::Milestone ||
                entries[i].kind == TimelineEntryKind::Bookend) {
                points.push_back(i);
            }
        }
        std::stable_sort(points.begin(), points.end(),
                         [&entries](size_t a, size_t b) {
                             return entries[a].importance > entries[b].importance;
                         });
        order.insert(order.end(), points.begin(), points.end());

        for (size_t i : order) {
            const TimelineChartEntry& entry = entries[i];
            const bool isSpan = (entry.kind == TimelineEntryKind::Span);
            const bool isPoint = (entry.kind == TimelineEntryKind::Milestone ||
                                  entry.kind == TimelineEntryKind::Bookend);
            if (!isSpan && !isPoint) continue;

            EntryLayout& el = layout.entries[i];
            el.entryIndex = static_cast<int>(i);
            el.color = EntryColor(i);

            const double x0 = axis.ToPixel(entry.start);
            const double x1 = isSpan ? (entry.openEnded ? plotRight : axis.ToPixel(entry.end))
                                     : x0;
            if (x1 < plotLeft - 250.0 || x0 > plotRight + 250.0) {
                el.visible = false;
                continue;
            }

            // A top/bottom axis leaves room on one side only, so it overrides
            // any explicit side - otherwise entries land on the header.
            int side;
            if (style.axisPosition == TimelineAxisPosition::Top) {
                side = 1;
            } else if (style.axisPosition == TimelineAxisPosition::Bottom) {
                side = -1;
            } else if (entry.side != 0) {
                side = (entry.side < 0) ? -1 : 1;
            } else {
                side = (autoSideCounter++ % 2 == 0) ? 1 : -1;
            }
            el.side = side;
            el.visible = true;

            Pending p;
            p.index = i;
            p.isSpan = isSpan;

            double occupiedLeft = x0;
            double occupiedRight = x1;
            double rowHeight = style.laneHeight;

            if (isSpan) {
                p.barX0 = x0;
                p.barWidth = std::max(3.0, x1 - x0);

                ctx->SetFontSize(style.labelFontSize);
                ctx->SetFontWeight(FontWeight::Bold);
                const double labelWidth =
                        ctx->GetTextLineDimensions(entry.name).width + 14.0;
                ctx->SetFontWeight(FontWeight::Normal);

                p.hasLabel = !labelsHidden && !entry.name.empty();
                p.labelWidth = labelWidth;
                p.labelHeight = style.labelFontSize * 1.4;
                // A hairline bar cannot hold readable text, so it always calls out
                const bool canHoldText = (style.barStyle != TimelineBarStyle::Line) &&
                                         (style.barHeight >= style.labelFontSize + 4.0);
                p.labelInside = canHoldText &&
                                ((style.labelPlacement == TimelineLabelPlacement::InsideBar) ||
                                 (style.labelPlacement != TimelineLabelPlacement::Callout &&
                                  labelWidth <= p.barWidth - 6.0));
                if (p.hasLabel && !p.labelInside) {
                    // Prefer the right of the bar; flip to the left when the
                    // label would run past the frame, clamp as a last resort
                    p.labelX = x0 + p.barWidth + style.labelGap;
                    if (p.labelX + labelWidth > plotRight) {
                        const double leftCandidate = x0 - style.labelGap - labelWidth;
                        p.labelX = (leftCandidate >= plotLeft)
                                   ? leftCandidate
                                   : std::max(plotLeft, plotRight - labelWidth);
                    }
                    occupiedLeft = std::min(occupiedLeft, p.labelX);
                    occupiedRight = std::max(occupiedRight, p.labelX + labelWidth);
                }
                rowHeight = std::max(style.barHeight, style.laneHeight);
            } else {
                p.markerSize = style.markerSize *
                               std::clamp(static_cast<double>(entry.importance), 0.4, 2.5);
                p.hasLabel = !labelsHidden && !entry.name.empty();
                if (p.hasLabel) {
                    ctx->SetFontSize(style.labelFontSize);
                    ctx->SetFontWeight(FontWeight::Bold);
                    double width = ctx->GetTextLineDimensions(entry.name).width;
                    double height = style.labelFontSize * 1.35;
                    ctx->SetFontWeight(FontWeight::Normal);
                    ctx->SetFontSize(style.detailFontSize);
                    if (style.showDetails && !entry.detail.empty()) {
                        width = std::max(width, static_cast<double>(
                                ctx->GetTextLineDimensions(entry.detail).width));
                        height += style.detailFontSize * 1.3;
                    }
                    const std::string caption = EntryDateCaption(entry);
                    if (!caption.empty()) {
                        width = std::max(width, static_cast<double>(
                                ctx->GetTextLineDimensions(caption).width));
                        height += style.detailFontSize * 1.3;
                    }
                    width += 10.0;

                    p.labelWidth = width;
                    p.labelHeight = height;
                    p.labelX = std::clamp(x0 - width / 2.0, plotLeft,
                                          std::max(plotLeft, plotRight - width));
                    occupiedLeft = std::min(occupiedLeft, p.labelX);
                    occupiedRight = std::max(occupiedRight, p.labelX + width);
                    rowHeight = std::max(rowHeight, height);
                } else {
                    occupiedLeft = x0 - p.markerSize;
                    occupiedRight = x0 + p.markerSize;
                }
            }

            TimelineShelf& shelf = (side < 0) ? above : below;
            const int maxRows = (side < 0) ? maxAbove : maxBelow;
            if (entry.lane >= 0) {
                el.row = entry.lane;
            } else {
                el.row = shelf.Place(occupiedLeft, occupiedRight, rowHeight, maxRows,
                                     style.labelGap);
                if (el.row < 0 && p.hasLabel) {
                    // No room for the label: keep the bar/marker, drop the text
                    p.hasLabel = false;
                    if (isSpan) {
                        el.row = shelf.Place(x0, std::max(x0 + p.barWidth, x1),
                                             std::max(style.barHeight, style.laneHeight),
                                             maxRows, style.labelGap, true);
                    } else {
                        el.row = -1;   // Markers live on the axis, no row needed
                    }
                } else if (el.row < 0) {
                    el.row = isSpan ? shelf.Place(x0, x1, rowHeight, maxRows,
                                                  style.labelGap, true)
                                    : -1;
                }
            }
            // An explicit lane still has to exist in the shelf
            if (entry.lane >= 0) {
                while (static_cast<int>(shelf.height.size()) <= el.row) {
                    shelf.rows.emplace_back();
                    shelf.height.push_back(style.laneHeight);
                }
                shelf.rows[el.row].emplace_back(occupiedLeft, occupiedRight);
                shelf.height[el.row] = std::max(shelf.height[el.row], rowHeight);
            }
            pending.push_back(p);
        }

        above.ComputeOffsets(style.axisGap, style.laneGap);
        below.ComputeOffsets(style.axisGap, style.laneGap);

        for (const Pending& p : pending) {
            EntryLayout& el = layout.entries[p.index];
            if (!el.visible) continue;
            const TimelineChartEntry& entry = entries[p.index];
            const TimelineShelf& shelf = (el.side < 0) ? above : below;
            const double rowHeight = shelf.Height(el.row);
            if (el.row < 0 && !p.isSpan) {
                el.markerCenter = Point2Dd(axis.ToPixel(entry.start), layout.axisY);
                el.shapeRect = Rect2Dd(el.markerCenter.x - p.markerSize,
                                       layout.axisY - p.markerSize,
                                       p.markerSize * 2.0, p.markerSize * 2.0);
                el.labelRect = Rect2Dd(0, 0, 0, 0);
                el.hasLeader = false;
                el.labelInside = false;
                continue;
            }
            const double rowTop = (el.side < 0)
                    ? layout.axisY - shelf.Offset(el.row) - rowHeight
                    : layout.axisY + shelf.Offset(el.row);

            if (p.isSpan) {
                const double barTop = rowTop + (rowHeight - style.barHeight) / 2.0;
                el.shapeRect = Rect2Dd(p.barX0, barTop, p.barWidth, style.barHeight);
                el.labelInside = p.labelInside;
                el.hasLeader = false;
                if (!p.hasLabel) {
                    el.labelRect = Rect2Dd(0, 0, 0, 0);
                } else if (p.labelInside) {
                    el.labelRect = el.shapeRect;
                } else {
                    el.labelRect = Rect2Dd(p.labelX, barTop, p.labelWidth, style.barHeight);
                }
            } else {
                el.markerCenter = Point2Dd(axis.ToPixel(entry.start), layout.axisY);
                el.shapeRect = Rect2Dd(el.markerCenter.x - p.markerSize,
                                       layout.axisY - p.markerSize,
                                       p.markerSize * 2.0, p.markerSize * 2.0);
                el.labelInside = false;
                if (!p.hasLabel) {
                    el.labelRect = Rect2Dd(0, 0, 0, 0);
                    el.hasLeader = false;
                    continue;
                }
                const double labelY = (el.side < 0)
                        ? rowTop + rowHeight - p.labelHeight
                        : rowTop;
                el.labelRect = Rect2Dd(p.labelX, labelY, p.labelWidth, p.labelHeight);
                el.hasLeader = true;
                if (el.side < 0) {
                    el.leaderFrom = Point2Dd(el.markerCenter.x, layout.axisY - p.markerSize);
                    el.leaderTo = Point2Dd(el.markerCenter.x, labelY + p.labelHeight);
                } else {
                    el.leaderFrom = Point2Dd(el.markerCenter.x, layout.axisY + p.markerSize);
                    el.leaderTo = Point2Dd(el.markerCenter.x, labelY);
                }
            }
        }
    }

    void UltraCanvasTimelineChart::LayoutSwimlaneEntries(IRenderContext* ctx) {
        const auto& entries = data->GetEntries();
        const double plotLeft = layout.plotArea.x;
        const double plotRight = layout.plotArea.x + layout.plotArea.width;
        const size_t laneCount = layout.lanes.size();
        if (laneCount == 0) return;

        // Measured once; packing is then cheap enough to repeat while the rows
        // negotiate for the shared height budget.
        struct Pending {
            size_t index = 0;
            bool isSpan = false;
            int lane = 0;
            int row = 0;
            double barX0 = 0.0;
            double barWidth = 0.0;
            double labelX = 0.0;
            double labelWidth = 0.0;
            double markerSize = 0.0;
            double rowHeight = 0.0;
            double labeledLeft = 0.0, labeledRight = 0.0;
            double bareLeft = 0.0, bareRight = 0.0;
            bool labelInside = false;
            bool wantsLabel = false;   // Has a label to place
            bool hasLabel = false;     // Room was found for it
        };
        std::vector<Pending> pending;
        pending.reserve(entries.size());

        const bool labelsHidden = (style.labelPlacement == TimelineLabelPlacement::Hidden);

        // Spans first in document order, then point events most-important-first
        std::vector<size_t> order;
        order.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == TimelineEntryKind::Span) order.push_back(i);
        }
        std::vector<size_t> points;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == TimelineEntryKind::Milestone ||
                entries[i].kind == TimelineEntryKind::Bookend) {
                points.push_back(i);
            }
        }
        std::stable_sort(points.begin(), points.end(),
                         [&entries](size_t a, size_t b) {
                             return entries[a].importance > entries[b].importance;
                         });
        order.insert(order.end(), points.begin(), points.end());

        for (size_t i : order) {
            const TimelineChartEntry& entry = entries[i];
            const bool isSpan = (entry.kind == TimelineEntryKind::Span);

            EntryLayout& el = layout.entries[i];
            el.entryIndex = static_cast<int>(i);
            el.color = EntryColor(i);
            el.side = 1;

            const double x0 = axis.ToPixel(entry.start);
            const double x1 = isSpan ? (entry.openEnded ? plotRight : axis.ToPixel(entry.end))
                                     : x0;
            if (x1 < plotLeft - 250.0 || x0 > plotRight + 250.0) {
                el.visible = false;
                continue;
            }
            el.visible = true;

            Pending p;
            p.index = i;
            p.isSpan = isSpan;
            p.lane = std::clamp(SwimlaneOf(entry), 0, static_cast<int>(laneCount) - 1);

            ctx->SetFontSize(style.labelFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            const double nameWidth = entry.name.empty()
                    ? 0.0 : ctx->GetTextLineDimensions(entry.name).width;
            ctx->SetFontWeight(FontWeight::Normal);

            if (isSpan) {
                p.barX0 = x0;
                p.barWidth = std::max(3.0, x1 - x0);
                p.wantsLabel = !labelsHidden && !entry.name.empty();
                p.labelWidth = nameWidth + 14.0;
                p.rowHeight = std::max(style.barHeight, style.laneHeight);
                p.bareLeft = x0;
                p.bareRight = x0 + p.barWidth;

                const bool canHoldText = (style.barStyle != TimelineBarStyle::Line) &&
                                         (style.barHeight >= style.labelFontSize + 4.0);
                p.labelInside = canHoldText &&
                                ((style.labelPlacement == TimelineLabelPlacement::InsideBar) ||
                                 (style.labelPlacement != TimelineLabelPlacement::Callout &&
                                  p.labelWidth <= p.barWidth - 6.0));
                if (p.wantsLabel && !p.labelInside) {
                    p.labelX = x0 + p.barWidth + style.labelGap;
                    if (p.labelX + p.labelWidth > plotRight) {
                        const double leftCandidate = x0 - style.labelGap - p.labelWidth;
                        p.labelX = (leftCandidate >= plotLeft)
                                   ? leftCandidate
                                   : std::max(plotLeft, plotRight - p.labelWidth);
                    }
                }
            } else {
                // Markers sit inside the band, not on the axis, and label to the
                // right so a row reads left-to-right like a schedule
                p.markerSize = style.markerSize *
                               std::clamp(static_cast<double>(entry.importance), 0.4, 2.0);
                p.wantsLabel = !labelsHidden && !entry.name.empty();
                p.labelWidth = nameWidth + 6.0;
                p.rowHeight = style.laneHeight;
                p.bareLeft = x0 - p.markerSize;
                p.bareRight = x0 + p.markerSize;
                if (p.wantsLabel) {
                    p.labelX = x0 + p.markerSize + style.labelGap * 0.6;
                    if (p.labelX + p.labelWidth > plotRight) {
                        const double leftCandidate =
                                x0 - p.markerSize - style.labelGap * 0.6 - p.labelWidth;
                        p.labelX = (leftCandidate >= plotLeft)
                                   ? leftCandidate
                                   : std::max(plotLeft, plotRight - p.labelWidth);
                    }
                }
            }

            if (p.wantsLabel && !p.labelInside) {
                p.labeledLeft = std::min(p.bareLeft, p.labelX);
                p.labeledRight = std::max(p.bareRight, p.labelX + p.labelWidth);
            } else {
                p.labeledLeft = p.bareLeft;
                p.labeledRight = p.bareRight;
            }
            pending.push_back(p);
        }

        std::vector<std::vector<size_t>> byLane(laneCount);   // Indices into `pending`
        for (size_t k = 0; k < pending.size(); ++k) {
            byLane[pending[k].lane].push_back(k);
        }

        // Pack one row with a given sub-row cap. Labels that cannot be placed
        // are dropped rather than overprinted; the bar/marker always lands.
        auto packLane = [&](size_t lane, int cap) {
            TimelineShelf shelf;
            for (size_t k : byLane[lane]) {
                Pending& p = pending[k];
                p.hasLabel = p.wantsLabel;
                int row = shelf.Place(p.labeledLeft, p.labeledRight, p.rowHeight,
                                      cap, style.labelGap);
                if (row < 0 && p.wantsLabel && !p.labelInside) {
                    p.hasLabel = false;
                    row = shelf.Place(p.bareLeft, p.bareRight, p.rowHeight,
                                      cap, style.labelGap);
                }
                if (row < 0) {
                    // Forced to share a row: the bars already overlap, so piling
                    // the text on top too only makes it unreadable
                    p.hasLabel = p.labelInside && p.wantsLabel;
                    row = shelf.Place(p.bareLeft, p.bareRight, p.rowHeight, cap,
                                      style.labelGap, true);
                }
                p.row = row;
            }
            return shelf;
        };

        // Sub-row heights come out of one shared budget. Compressing the rows
        // is always better than dropping a sub-row, because dropping one forces
        // two bars to share a line and overlap - so scale first, and only start
        // removing sub-rows once the compression floor is reached.
        const int kGenerousRows = 8;
        const double kMinRowScale = 0.5;
        const double available = layout.plotArea.height - style.axisGap -
                                 static_cast<double>(laneCount) * style.swimlaneGap;

        auto bandHeightOf = [&](const TimelineShelf& shelf, double scale) {
            double content = 0.0;
            for (size_t r = 0; r < shelf.height.size(); ++r) {
                content += shelf.height[r] * scale;
                if (r + 1 < shelf.height.size()) content += style.laneGap * scale;
            }
            return std::max(style.swimlaneMinHeight * scale,
                            content + style.swimlanePadding * 2.0);
        };

        std::vector<int> caps(laneCount, kGenerousRows);
        std::vector<TimelineShelf> shelves(laneCount);
        std::vector<double> heights(laneCount, 0.0);
        double rowScale = 1.0;

        auto measure = [&]() {
            double total = 0.0;
            for (size_t lane = 0; lane < laneCount; ++lane) {
                heights[lane] = bandHeightOf(shelves[lane], rowScale);
                total += heights[lane];
            }
            return total;
        };

        for (size_t lane = 0; lane < laneCount; ++lane) {
            shelves[lane] = packLane(lane, caps[lane]);
        }

        double total = measure();
        if (total > available) {
            rowScale = std::clamp(available / total, kMinRowScale, 1.0);
            total = measure();
        }

        for (int guard = 0; guard < 64 && total > available; ++guard) {
            // Still short at the compression floor: take a sub-row from the
            // tallest row. Some overlap becomes unavoidable at this point.
            size_t worst = laneCount;
            size_t worstRows = 1;
            for (size_t lane = 0; lane < laneCount; ++lane) {
                const size_t used = shelves[lane].height.size();
                if (used > worstRows) { worstRows = used; worst = lane; }
            }
            if (worst == laneCount) break;
            caps[worst] = static_cast<int>(worstRows) - 1;
            shelves[worst] = packLane(worst, caps[worst]);
            total = measure();
        }

        const double barH = style.barHeight * rowScale;
        for (size_t lane = 0; lane < laneCount; ++lane) {
            for (double& h : shelves[lane].height) h *= rowScale;
            shelves[lane].ComputeOffsets(0.0, style.laneGap * rowScale);
        }

        // Final geometry
        double cursor = layout.axisY + style.axisGap;
        const double bottomLimit = layout.plotArea.y + layout.plotArea.height;
        layout.laneBands.assign(laneCount, SwimlaneBand());
        for (size_t lane = 0; lane < laneCount; ++lane) {
            layout.laneBands[lane].rect =
                    Rect2Dd(layout.plotArea.x, cursor, layout.plotArea.width,
                            std::min(heights[lane], std::max(10.0, bottomLimit - cursor)));
            layout.laneBands[lane].contentTop = cursor + style.swimlanePadding;
            cursor += heights[lane] + style.swimlaneGap;
        }

        for (const Pending& p : pending) {
            EntryLayout& el = layout.entries[p.index];
            if (!el.visible) continue;
            const TimelineChartEntry& entry = entries[p.index];
            const TimelineShelf& shelf = shelves[p.lane];
            const double rowHeight = shelf.Height(p.row);
            const double rowTop = layout.laneBands[p.lane].contentTop + shelf.Offset(p.row);
            el.row = p.row;

            if (p.isSpan) {
                const double barTop = rowTop + (rowHeight - barH) / 2.0;
                el.shapeRect = Rect2Dd(p.barX0, barTop, p.barWidth, barH);
                el.labelInside = p.labelInside;
                el.hasLeader = false;
                if (!p.hasLabel) {
                    el.labelRect = Rect2Dd(0, 0, 0, 0);
                } else if (p.labelInside) {
                    el.labelRect = el.shapeRect;
                } else {
                    el.labelRect = Rect2Dd(p.labelX, barTop, p.labelWidth, barH);
                }
            } else {
                const double centerY = rowTop + rowHeight / 2.0;
                const double markerSize = std::min(p.markerSize, rowHeight / 2.0);
                el.markerCenter = Point2Dd(axis.ToPixel(entry.start), centerY);
                el.shapeRect = Rect2Dd(el.markerCenter.x - markerSize,
                                       centerY - markerSize,
                                       markerSize * 2.0, markerSize * 2.0);
                el.labelInside = false;
                el.hasLeader = false;
                el.labelRect = p.hasLabel
                        ? Rect2Dd(p.labelX, rowTop, p.labelWidth, rowHeight)
                        : Rect2Dd(0, 0, 0, 0);
            }
        }
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    std::string UltraCanvasTimelineChart::EntryDateCaption(
            const TimelineChartEntry& entry) const {
        if (!style.showDateCaptions) return "";
        if (entry.kind == TimelineEntryKind::Span) {
            return TimeAxis::FormatShortDate(entry.start) + " - " +
                   (entry.openEnded ? std::string("...")
                                    : TimeAxis::FormatShortDate(entry.end));
        }
        return TimeAxis::FormatShortDate(entry.start);
    }

    std::string UltraCanvasTimelineChart::BuildTooltip(size_t index) const {
        const auto& entries = data->GetEntries();
        if (index >= entries.size()) return "";
        const TimelineChartEntry& entry = entries[index];
        if (!entry.tooltip.empty()) return entry.tooltip;

        std::string text = entry.name;
        if (!entry.detail.empty()) text += "\n" + entry.detail;

        if (entry.kind == TimelineEntryKind::Span || entry.kind == TimelineEntryKind::Era) {
            text += "\n" + TimeAxis::FormatDate(entry.start) + " - " +
                    (entry.openEnded ? std::string("ongoing")
                                     : TimeAxis::FormatDate(entry.end));
            const long days = static_cast<long>(std::lround(entry.end - entry.start));
            if (!entry.openEnded && days > 0) {
                text += "\n" + std::to_string(days) + (days == 1 ? " day" : " days");
            }
        } else {
            text += "\n" + TimeAxis::FormatDate(entry.start);
        }
        if (entry.progress >= 0.0f) {
            text += "\n" + std::to_string(static_cast<int>(entry.progress * 100.0f + 0.5f)) + "%";
        }
        return text;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasTimelineChart::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        UpdateLayout(ctx);
        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }
        DrawTitles(ctx);
        RenderChart(ctx);
    }

    void UltraCanvasTimelineChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateLayout(ctx);

        const TimelineScale minor = axis.Resolve(style.scale);
        const std::vector<TimeAxisTick> minorTicks = axis.Ticks(minor, false);

        if (laneMode == TimelineLaneMode::Swimlanes) DrawSwimlanes(ctx);
        DrawEras(ctx);
        if (style.showGrid) DrawGrid(ctx, minorTicks);
        DrawHeader(ctx);
        DrawAxis(ctx);
        DrawSpans(ctx);
        DrawMilestones(ctx);
        if (style.showNowMarker && hasNow) DrawNowMarker(ctx);
    }

    void UltraCanvasTimelineChart::DrawTitles(IRenderContext* ctx) {
        if (chartTitle.empty() && subtitle.empty()) return;

        double y = 6.0;
        if (!chartTitle.empty()) {
            ctx->SetFontSize(19.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(TextColor());
            Size2Di s = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(GetWidth() / 2.0 - s.width / 2.0, y));
            ctx->SetFontWeight(FontWeight::Normal);
            y += 28.0;
        }
        if (!subtitle.empty()) {
            ctx->SetFontSize(11.5f);
            ctx->SetTextPaint(style.mutedTextColor);
            Size2Di s = ctx->GetTextLineDimensions(subtitle);
            ctx->DrawText(subtitle, Point2Dd(GetWidth() / 2.0 - s.width / 2.0, y));
        }
    }

    void UltraCanvasTimelineChart::DrawEras(IRenderContext* ctx) {
        if (!style.showEraBands) return;

        for (size_t k = 0; k < layout.eraBands.size(); ++k) {
            const size_t index = layout.eraIndices[k];
            const Rect2Dd& band = layout.eraBands[k];
            const Color color = EntryColor(index);
            ctx->SetFillPaint(WithAlpha(color, style.eraAlpha));
            ctx->FillRectangle(band);

            if (style.showEraLabels) {
                const std::string& name = data->GetEntries()[index].name;
                if (!name.empty()) {
                    ctx->SetFontSize(style.detailFontSize);
                    ctx->SetFontWeight(FontWeight::Bold);
                    ctx->SetTextPaint(darkTheme ? LightenColor(color, 0.35f)
                                                : DarkenColor(color, 0.25f));
                    Size2Di s = ctx->GetTextLineDimensions(name);
                    if (s.width + 8 < band.width) {
                        ctx->DrawText(name, Point2Dd(band.x + band.width / 2.0 - s.width / 2.0,
                                                     band.y + 3.0));
                    }
                    ctx->SetFontWeight(FontWeight::Normal);
                }
            }
        }
    }

    void UltraCanvasTimelineChart::DrawSwimlanes(IRenderContext* ctx) {
        if (layout.laneBands.empty()) return;

        for (size_t i = 0; i < layout.laneBands.size(); ++i) {
            const Rect2Dd& band = layout.laneBands[i].rect;
            if (band.height <= 0.0) continue;
            const Color color = SwimlaneColor(i);

            if (style.showSwimlaneBands) {
                ctx->SetFillPaint(WithAlpha(color, style.swimlaneBandAlpha));
                ctx->FillRectangle(band);
            }

            // Name column on the left, in a stronger tint of the row's color
            if (layout.gutterArea.width > 0.0) {
                Rect2Dd header(layout.gutterArea.x, band.y,
                               layout.gutterArea.width, band.height);
                ctx->SetFillPaint(WithAlpha(color, style.swimlaneHeaderAlpha));
                ctx->FillRectangle(header);

                const std::string& name = layout.lanes[i].name;
                const std::string& detail = layout.lanes[i].detail;
                if (!name.empty()) {
                    // Shrink the row title until it fits the name column
                    const double room = header.width - 16.0;
                    float titleSize = style.swimlaneTitleFontSize;
                    ctx->SetFontWeight(FontWeight::Bold);
                    ctx->SetFontSize(titleSize);
                    while (titleSize > 7.5f &&
                           ctx->GetTextLineDimensions(name).width > room) {
                        titleSize -= 0.5f;
                        ctx->SetFontSize(titleSize);
                    }
                    ctx->SetTextPaint(TextColor());
                    Size2Di s1 = ctx->GetTextLineDimensions(name);
                    const double lineH = style.swimlaneTitleFontSize * 1.35;
                    const bool twoLines = !detail.empty();
                    double y = band.y + band.height / 2.0 -
                               (twoLines ? lineH : s1.height / 2.0);
                    ctx->DrawText(name, Point2Dd(header.x + 10.0, y));
                    ctx->SetFontWeight(FontWeight::Normal);
                    if (twoLines) {
                        ctx->SetFontSize(style.detailFontSize);
                        ctx->SetTextPaint(style.mutedTextColor);
                        if (ctx->GetTextLineDimensions(detail).width <= room) {
                            ctx->DrawText(detail, Point2Dd(header.x + 10.0, y + lineH));
                        }
                    }
                }
            }

            if (style.showSwimlaneSeparators) {
                ctx->SetStrokePaint(style.gridColor);
                ctx->SetStrokeWidth(1.0);
                const double y = band.y + band.height;
                ctx->DrawLine(Point2Dd(layout.gutterArea.x, y),
                              Point2Dd(band.x + band.width, y));
            }
        }

        // Vertical rule closing the name column
        if (layout.gutterArea.width > 0.0 && style.showSwimlaneSeparators) {
            ctx->SetStrokePaint(style.gridColor);
            ctx->SetStrokeWidth(1.0);
            const double x = layout.plotArea.x;
            ctx->DrawLine(Point2Dd(x, layout.headerArea.y),
                          Point2Dd(x, layout.plotArea.y + layout.plotArea.height));
        }
    }

    void UltraCanvasTimelineChart::DrawGrid(IRenderContext* ctx,
                                            const std::vector<TimeAxisTick>& ticks) {
        const TimelineScale minor = axis.Resolve(style.scale);
        const double top = layout.plotArea.y;
        const double bottom = layout.plotArea.y + layout.plotArea.height;

        if (style.shadeWeekends && minor == TimelineScale::Days) {
            ctx->SetFillPaint(style.weekendColor);
            for (const auto& tick : ticks) {
                if (!tick.isWeekend) continue;
                ctx->FillRectangle(Rect2Dd(tick.x, top, std::max(1.0, tick.width),
                                           layout.plotArea.height));
            }
        }

        ctx->SetStrokePaint(style.gridColor);
        ctx->SetStrokeWidth(1.0);
        for (const auto& tick : ticks) {
            if (tick.x < layout.plotArea.x - 1.0 ||
                tick.x > layout.plotArea.x + layout.plotArea.width + 1.0) {
                continue;
            }
            ctx->DrawLine(Point2Dd(tick.x, top), Point2Dd(tick.x, bottom));
        }
    }

    void UltraCanvasTimelineChart::DrawHeader(IRenderContext* ctx) {
        if (layout.headerArea.height <= 0.0) return;

        const TimelineScale minor = axis.Resolve(style.scale);
        const TimelineScale major = TimeAxis::MajorFor(minor);

        ctx->SetFillPaint(style.headerFillColor);
        ctx->FillRectangle(layout.headerArea);

        double y = layout.headerArea.y;
        const double left = layout.plotArea.x;
        const double right = layout.plotArea.x + layout.plotArea.width;

        auto drawTier = [&](TimelineScale scale, bool isMajor, double height) {
            const std::vector<TimeAxisTick> ticks = axis.Ticks(scale, isMajor);
            ctx->SetFontSize(isMajor ? style.majorTierFontSize : style.minorTierFontSize);
            ctx->SetFontWeight(isMajor ? FontWeight::Bold : FontWeight::Normal);

            bool alternate = false;
            for (const auto& tick : ticks) {
                const double x0 = std::max(tick.x, left);
                const double x1 = std::min(tick.x + tick.width, right);
                alternate = !alternate;
                if (x1 <= x0) continue;
                // Too narrow to hold a label: drawing the separators would just
                // fill the tier with hatching
                if (tick.width < 6.0) continue;

                if (isMajor) {
                    ctx->SetFillPaint(alternate ? WithAlpha(style.axisColor, 26)
                                                : WithAlpha(style.axisColor, 12));
                    ctx->FillRectangle(Rect2Dd(x0, y, x1 - x0, height));
                }

                ctx->SetTextPaint(style.headerTextColor);
                Size2Di s = ctx->GetTextLineDimensions(tick.label);
                if (s.width + 6 <= x1 - x0) {
                    ctx->DrawText(tick.label,
                                  Point2Dd((x0 + x1) / 2.0 - s.width / 2.0,
                                           y + (height - s.height) / 2.0));
                }

                ctx->SetStrokePaint(style.gridColor);
                ctx->SetStrokeWidth(1.0);
                ctx->DrawLine(Point2Dd(x0, y), Point2Dd(x0, y + height));
            }
            ctx->SetFontWeight(FontWeight::Normal);
        };

        if (style.showMajorTier) {
            drawTier(major, true, style.majorTierHeight);
            y += style.majorTierHeight;
        }
        if (style.showMinorTier) {
            drawTier(minor, false, style.minorTierHeight);
        }
    }

    void UltraCanvasTimelineChart::DrawAxis(IRenderContext* ctx) {
        const double half = style.axisThickness / 2.0;
        Rect2Dd axisRect(layout.plotArea.x, layout.axisY - half,
                         layout.plotArea.width, style.axisThickness);
        ctx->SetFillPaint(style.axisColor);
        if (style.axisThickness > 3.0) {
            ctx->FillRoundedRectangle(axisRect, half);
        } else {
            ctx->FillRectangle(axisRect);
        }
    }

    void UltraCanvasTimelineChart::DrawSpans(IRenderContext* ctx) {
        const auto& entries = data->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind != TimelineEntryKind::Span) continue;
            const EntryLayout& el = layout.entries[i];
            if (!el.visible) continue;

            const TimelineChartEntry& entry = entries[i];
            DrawHighlight(ctx, i, el);

            // Uncertainty extensions render first, behind the solid bar
            if (entry.startUncertainty > 0.0 || entry.endUncertainty > 0.0) {
                ctx->SetFillPaint(WithAlpha(el.color, 70));
                if (entry.startUncertainty > 0.0) {
                    const double x = axis.ToPixel(entry.start - entry.startUncertainty);
                    ctx->FillRectangle(Rect2Dd(x, el.shapeRect.y + el.shapeRect.height * 0.25,
                                               std::max(1.0, el.shapeRect.x - x),
                                               el.shapeRect.height * 0.5));
                }
                if (entry.endUncertainty > 0.0) {
                    const double x = axis.ToPixel(entry.end + entry.endUncertainty);
                    const double right = el.shapeRect.x + el.shapeRect.width;
                    ctx->FillRectangle(Rect2Dd(right, el.shapeRect.y + el.shapeRect.height * 0.25,
                                               std::max(1.0, x - right),
                                               el.shapeRect.height * 0.5));
                }
            }

            Rect2Dd bar = el.shapeRect;
            if (style.barStyle == TimelineBarStyle::Line) {
                bar = Rect2Dd(bar.x, bar.y + bar.height / 2.0 - 2.0, bar.width, 4.0);
            }

            ctx->SetFillPaint(el.color);
            switch (style.barStyle) {
                case TimelineBarStyle::Pill:
                    ctx->FillRoundedRectangle(bar, bar.height / 2.0);
                    break;
                case TimelineBarStyle::Rounded:
                    ctx->FillRoundedRectangle(bar, std::min(style.barCornerRadius,
                                                            bar.height / 2.0));
                    break;
                case TimelineBarStyle::Flat:
                case TimelineBarStyle::Line:
                default:
                    ctx->FillRectangle(bar);
                    break;
            }

            // Open-ended spans fade towards the right edge
            if (entry.openEnded) {
                std::vector<GradientStop> stops = {
                        GradientStop(0.0, WithAlpha(el.color, 0)),
                        GradientStop(1.0, backgroundColor)};
                auto pattern = ctx->CreateLinearGradientPattern(
                        bar.x + bar.width - 60.0, bar.y, bar.x + bar.width, bar.y, stops);
                ctx->SetFillPaint(pattern);
                ctx->FillRectangle(Rect2Dd(std::max(bar.x, bar.x + bar.width - 60.0), bar.y,
                                           std::min(60.0, bar.width), bar.height));
            }

            if (style.showProgress && entry.progress >= 0.0f && bar.width > 2.0) {
                const double w = bar.width * std::clamp(entry.progress, 0.0f, 1.0f);
                ctx->SetFillPaint(WithAlpha(DarkenColor(el.color, 0.28f), 235));
                ctx->FillRectangle(Rect2Dd(bar.x, bar.y + bar.height - 4.0, w, 3.0));
            }

            DrawEntryLabel(ctx, i, el);
        }
    }

    void UltraCanvasTimelineChart::DrawMarkerShape(IRenderContext* ctx, const Point2Dd& center,
                                                   double size, TimelineMarkerStyle marker) {
        switch (marker) {
            case TimelineMarkerStyle::Circle:
                ctx->FillCircle(center, size);
                break;
            case TimelineMarkerStyle::Square:
                ctx->FillRectangle(Rect2Dd(center.x - size, center.y - size,
                                           size * 2.0, size * 2.0));
                break;
            case TimelineMarkerStyle::TriangleUp:
                ctx->FillLinePath({Point2Dd(center.x, center.y - size),
                                   Point2Dd(center.x + size, center.y + size * 0.8),
                                   Point2Dd(center.x - size, center.y + size * 0.8)});
                break;
            case TimelineMarkerStyle::TriangleDown:
                ctx->FillLinePath({Point2Dd(center.x, center.y + size),
                                   Point2Dd(center.x + size, center.y - size * 0.8),
                                   Point2Dd(center.x - size, center.y - size * 0.8)});
                break;
            case TimelineMarkerStyle::Flag: {
                const double poleTop = center.y - size * 1.8;
                ctx->FillRectangle(Rect2Dd(center.x - 1.0, poleTop, 2.0, size * 1.8));
                ctx->FillLinePath({Point2Dd(center.x + 1.0, poleTop),
                                   Point2Dd(center.x + size * 1.7, poleTop + size * 0.5),
                                   Point2Dd(center.x + 1.0, poleTop + size)});
                break;
            }
            case TimelineMarkerStyle::Pin:
                ctx->FillCircle(Point2Dd(center.x, center.y - size * 0.9), size * 0.85);
                ctx->FillLinePath({Point2Dd(center.x - size * 0.5, center.y - size * 0.5),
                                   Point2Dd(center.x + size * 0.5, center.y - size * 0.5),
                                   Point2Dd(center.x, center.y + size * 0.6)});
                break;
            case TimelineMarkerStyle::Star: {
                std::vector<Point2Dd> pts;
                for (int k = 0; k < 10; ++k) {
                    const double r = (k % 2 == 0) ? size : size * 0.45;
                    const double a = -M_PI / 2.0 + k * M_PI / 5.0;
                    pts.emplace_back(center.x + r * std::cos(a), center.y + r * std::sin(a));
                }
                ctx->FillLinePath(pts);
                break;
            }
            case TimelineMarkerStyle::Diamond:
            default:
                ctx->FillLinePath({Point2Dd(center.x, center.y - size),
                                   Point2Dd(center.x + size, center.y),
                                   Point2Dd(center.x, center.y + size),
                                   Point2Dd(center.x - size, center.y)});
                break;
        }
    }

    void UltraCanvasTimelineChart::DrawMilestones(IRenderContext* ctx) {
        const auto& entries = data->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const TimelineChartEntry& entry = entries[i];
            if (entry.kind != TimelineEntryKind::Milestone &&
                entry.kind != TimelineEntryKind::Bookend) {
                continue;
            }
            const EntryLayout& el = layout.entries[i];
            if (!el.visible) continue;

            DrawHighlight(ctx, i, el);

            if (el.hasLeader && style.leaderLength > 0.0) {
                ctx->SetStrokePaint(style.leaderColor);
                ctx->SetStrokeWidth(1.0);
                ctx->DrawLine(el.leaderFrom, el.leaderTo);
            }

            const double size = el.shapeRect.width / 2.0;

            // Halo in the background color separates the marker from the axis;
            // inside a swimlane band it would punch a hole in the tint
            if (style.markerBorderWidth > 0.0 && laneMode != TimelineLaneMode::Swimlanes) {
                ctx->SetFillPaint(backgroundColor);
                DrawMarkerShape(ctx, el.markerCenter, size + style.markerBorderWidth,
                                entry.marker);
            }
            ctx->SetFillPaint(el.color);
            DrawMarkerShape(ctx, el.markerCenter, size, entry.marker);

            if (!entry.iconGlyph.empty() && size >= 8.0) {
                ctx->SetFontSize(style.markerFontSize);
                ctx->SetFontWeight(FontWeight::Bold);
                ctx->SetTextPaint(ContrastText(el.color));
                Size2Di s = ctx->GetTextLineDimensions(entry.iconGlyph);
                ctx->DrawText(entry.iconGlyph,
                              Point2Dd(el.markerCenter.x - s.width / 2.0,
                                       el.markerCenter.y - s.height / 2.0));
                ctx->SetFontWeight(FontWeight::Normal);
            }

            DrawEntryLabel(ctx, i, el);
        }
    }

    void UltraCanvasTimelineChart::DrawEntryLabel(IRenderContext* ctx, size_t index,
                                                  const EntryLayout& el) {
        if (style.labelPlacement == TimelineLabelPlacement::Hidden) return;
        if (el.labelRect.width <= 0.0 || el.labelRect.height <= 0.0) return;

        const TimelineChartEntry& entry = data->GetEntries()[index];
        if (entry.name.empty()) return;

        if (el.labelInside) {
            // Inside a span bar
            ctx->SetFontSize(style.labelFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(ContrastText(el.color));
            Size2Di s = ctx->GetTextLineDimensions(entry.name);
            if (s.width + 8 <= el.shapeRect.width) {
                ctx->DrawText(entry.name,
                              Point2Dd(el.shapeRect.x + el.shapeRect.width / 2.0 - s.width / 2.0,
                                       el.shapeRect.y + el.shapeRect.height / 2.0 - s.height / 2.0));
            }
            ctx->SetFontWeight(FontWeight::Normal);
            return;
        }

        double y = el.labelRect.y;
        const bool centered = (entry.kind != TimelineEntryKind::Span) &&
                              (laneMode != TimelineLaneMode::Swimlanes);
        auto drawLine = [&](const std::string& text, float fontSize, const Color& color,
                            bool bold) {
            if (text.empty()) return;
            ctx->SetFontSize(fontSize);
            ctx->SetFontWeight(bold ? FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(color);
            Size2Di s = ctx->GetTextLineDimensions(text);
            const double x = centered
                    ? el.labelRect.x + el.labelRect.width / 2.0 - s.width / 2.0
                    : el.labelRect.x + 2.0;
            ctx->DrawText(text, Point2Dd(x, y));
            y += fontSize * 1.32;
            ctx->SetFontWeight(FontWeight::Normal);
        };

        if (entry.kind == TimelineEntryKind::Span ||
            laneMode == TimelineLaneMode::Swimlanes) {
            // Vertically centre a single-line label beside the bar or marker
            y = el.labelRect.y + el.labelRect.height / 2.0 - style.labelFontSize * 0.7;
            drawLine(entry.name, style.labelFontSize, TextColor(), true);
            return;
        }

        drawLine(entry.name, style.labelFontSize, TextColor(), true);
        if (style.showDetails) {
            drawLine(entry.detail, style.detailFontSize, style.mutedTextColor, false);
        }
        drawLine(EntryDateCaption(entry), style.detailFontSize, style.mutedTextColor, false);
    }

    void UltraCanvasTimelineChart::DrawHighlight(IRenderContext* ctx, size_t index,
                                                 const EntryLayout& el) {
        const bool isSelected = (selectedEntry.index == static_cast<int>(index));
        const bool isHovered = (hoveredEntry.index == static_cast<int>(index));
        if (!isSelected && !isHovered) return;
        if (el.hitRect.width <= 0.0 || el.hitRect.height <= 0.0) return;

        Rect2Dd rect(el.hitRect.x - 3.0, el.hitRect.y - 3.0,
                     el.hitRect.width + 6.0, el.hitRect.height + 6.0);
        ctx->SetFillPaint(WithAlpha(el.color, isSelected ? 52 : 28));
        ctx->FillRoundedRectangle(rect, 5.0);
        if (isSelected) {
            ctx->SetStrokePaint(el.color);
            ctx->SetStrokeWidth(1.5);
            ctx->DrawRoundedRectangle(rect, 5.0);
        }
    }

    void UltraCanvasTimelineChart::DrawNowMarker(IRenderContext* ctx) {
        const double x = axis.ToPixel(nowSerial);
        if (x < layout.plotArea.x || x > layout.plotArea.x + layout.plotArea.width) return;

        ctx->SetStrokePaint(style.nowColor);
        ctx->SetStrokeWidth(1.5);
        ctx->SetLineDash(UCDashPattern(std::vector<double>{5.0, 4.0}));
        ctx->DrawLine(Point2Dd(x, layout.plotArea.y),
                      Point2Dd(x, layout.plotArea.y + layout.plotArea.height));
        ctx->SetLineDash(UCDashPattern());

        ctx->SetFontSize(style.detailFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        const std::string label = "now";
        Size2Di s = ctx->GetTextLineDimensions(label);
        Rect2Dd chip(x - s.width / 2.0 - 4.0, layout.plotArea.y + 2.0,
                     s.width + 8.0, s.height + 4.0);
        ctx->SetFillPaint(style.nowColor);
        ctx->FillRoundedRectangle(chip, 3.0);
        ctx->SetTextPaint(Color(255, 255, 255, 255));
        ctx->DrawText(label, Point2Dd(chip.x + 4.0, chip.y + 2.0));
        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    TimelineEntryRef UltraCanvasTimelineChart::FindEntryAt(const Point2Di& pos) const {
        Point2Dd p(pos.x, pos.y);
        // Backwards so the entry drawn last wins; eras are checked last of all
        for (size_t i = layout.entries.size(); i-- > 0;) {
            const EntryLayout& el = layout.entries[i];
            if (!el.visible) continue;
            if (data->GetEntries()[i].kind == TimelineEntryKind::Era) continue;
            if (el.hitRect.width > 0.0 && el.hitRect.Contains(p)) {
                TimelineEntryRef ref;
                ref.index = static_cast<int>(i);
                return ref;
            }
        }
        for (size_t i = layout.entries.size(); i-- > 0;) {
            const EntryLayout& el = layout.entries[i];
            if (!el.visible) continue;
            if (data->GetEntries()[i].kind != TimelineEntryKind::Era) continue;
            if (el.shapeRect.width > 0.0 && el.shapeRect.Contains(p)) {
                TimelineEntryRef ref;
                ref.index = static_cast<int>(i);
                return ref;
            }
        }
        return TimelineEntryRef();
    }

    bool UltraCanvasTimelineChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) return false;

        TimelineEntryRef found = FindEntryAt(mousePos);
        if (found != hoveredEntry) {
            hoveredEntry = found;
            if (hoveredEntry.IsValid() && onEntryHover) {
                onEntryHover(static_cast<size_t>(hoveredEntry.index),
                             data->GetEntries()[hoveredEntry.index]);
            }
            RequestRedraw();
        }

        if (enableTooltips) {
            if (hoveredEntry.IsValid()) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildTooltip(static_cast<size_t>(hoveredEntry.index)),
                        windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }
        return hoveredEntry.IsValid();
    }

    bool UltraCanvasTimelineChart::HandleEntryClick(const UCEvent& event) {
        if (!enableSelection || !layout.valid) return false;

        TimelineEntryRef clicked = FindEntryAt(Point2Di(event.pointer.x, event.pointer.y));
        if (clicked.IsValid()) {
            selectedEntry = (clicked == selectedEntry) ? TimelineEntryRef() : clicked;
            if (selectedEntry.IsValid() && onEntrySelect) {
                onEntrySelect(static_cast<size_t>(selectedEntry.index),
                              data->GetEntries()[selectedEntry.index]);
            }
            if (onSelectionChange) onSelectionChange();
            RequestRedraw();
            return true;
        }
        if (selectedEntry.IsValid()) {
            ClearSelection();
            if (onSelectionChange) onSelectionChange();
            return true;
        }
        return false;
    }

    bool UltraCanvasTimelineChart::HandleEntryDoubleClick(const UCEvent& event) {
        if (!layout.valid) return false;
        TimelineEntryRef clicked = FindEntryAt(Point2Di(event.pointer.x, event.pointer.y));
        if (clicked.IsValid() && onEntryDoubleClick) {
            onEntryDoubleClick(static_cast<size_t>(clicked.index),
                               data->GetEntries()[clicked.index]);
            return true;
        }
        return false;
    }

    bool UltraCanvasTimelineChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    if (HandleEntryClick(event)) return true;
                    if (enablePan) {
                        panning = true;
                        panAnchor = Point2Di(event.pointer.x, event.pointer.y);
                        return true;
                    }
                }
                break;

            case UCEventType::MouseMove:
                if (panning) {
                    const Point2Di pos(event.pointer.x, event.pointer.y);
                    axis.PanPixels(static_cast<double>(pos.x - panAnchor.x));
                    panAnchor = pos;
                    InvalidateLayout();
                    RequestRedraw();
                    if (onViewChanged) onViewChanged();
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (panning) {
                    panning = false;
                    return true;
                }
                break;

            case UCEventType::MouseWheel:
                if (enableZoom) {
                    // Ease the zoom in: applying a run of small factors about
                    // the same x is exactly applying their product once, so the
                    // axis' own ZoomAbout does all the maths (see
                    // UltraCanvasSmoothScroll.h).
                    if (!zoomAnim.IsBound()) {
                        zoomAnim.Bind([this](double f) {
                                          axis.ZoomAbout(zoomAnchorX, f);
                                          InvalidateLayout();
                                      },
                                      [this] {
                                          RequestRedraw();
                                          if (onViewChanged) onViewChanged();
                                      });
                    }
                    zoomAnchorX = static_cast<double>(event.pointer.x);
                    // The axis clamps its own span, so the animator is given a
                    // wide range and only paces the factor.
                    zoomAnim.ZoomBy((event.wheelDelta > 0) ? (1.0 / 1.25) : 1.25,
                                    1.0, 1.0 / 1e6, 1e6);
                    return true;
                }
                break;

            case UCEventType::MouseDoubleClick:
                if (HandleEntryDoubleClick(event)) return true;
                if (enableZoom) {
                    FitToData();
                    return true;
                }
                break;

            case UCEventType::MouseLeave:
                panning = false;
                if (hoveredEntry.IsValid()) {
                    hoveredEntry = TimelineEntryRef();
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;

            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

// =============================================================================
// STYLE PRESETS
// =============================================================================

    namespace TimelineChartStyles {

        TimelineChartStyle CreateForDesign(TimelineChartDesign design) {
            TimelineChartStyle s;
            switch (design) {
                case TimelineChartDesign::Classic:
                    s.barStyle = TimelineBarStyle::Flat;
                    s.barCornerRadius = 0.0;
                    s.axisThickness = 3.0;
                    s.defaultMarker = TimelineMarkerStyle::Diamond;
                    s.markerSize = 9.0;
                    s.showEraBands = false;
                    break;

                case TimelineChartDesign::Minimal:
                    s.barStyle = TimelineBarStyle::Line;
                    s.axisThickness = 2.0;
                    s.markerSize = 8.0;
                    s.markerBorderWidth = 1.5;
                    s.showGrid = true;
                    s.gridColor = Color(240, 242, 246, 255);
                    s.headerFillColor = Color(252, 252, 253, 255);
                    s.showEraBands = false;
                    s.laneHeight = 26.0;
                    break;

                case TimelineChartDesign::Roadmap:
                    s.barStyle = TimelineBarStyle::Pill;
                    s.barHeight = 22.0;
                    s.axisThickness = 8.0;
                    s.markerSize = 14.0;
                    s.defaultMarker = TimelineMarkerStyle::Circle;
                    s.laneHeight = 34.0;
                    s.showEraBands = true;
                    s.eraAlpha = 30;
                    break;

                case TimelineChartDesign::Dark:
                case TimelineChartDesign::Modern:
                default:
                    s.barStyle = TimelineBarStyle::Rounded;
                    break;
            }
            return s;
        }

        std::vector<Color> GetPaletteColors(TimelineChartPalette palette) {
            switch (palette) {
                case TimelineChartPalette::Vibrant:
                    return {Color(52, 152, 219, 255), Color(46, 204, 113, 255),
                            Color(241, 196, 15, 255), Color(230, 126, 34, 255),
                            Color(231, 76, 60, 255), Color(155, 89, 182, 255),
                            Color(26, 188, 156, 255), Color(52, 73, 94, 255)};
                case TimelineChartPalette::Pastel:
                    return {Color(129, 199, 212, 255), Color(160, 214, 180, 255),
                            Color(247, 214, 141, 255), Color(244, 177, 131, 255),
                            Color(240, 152, 152, 255), Color(190, 168, 220, 255)};
                case TimelineChartPalette::Ocean:
                    return {Color(13, 71, 111, 255), Color(21, 101, 152, 255),
                            Color(31, 133, 176, 255), Color(46, 162, 186, 255),
                            Color(78, 190, 189, 255), Color(122, 209, 190, 255)};
                case TimelineChartPalette::Sunset:
                    return {Color(122, 40, 96, 255), Color(176, 52, 92, 255),
                            Color(219, 80, 74, 255), Color(238, 124, 62, 255),
                            Color(247, 165, 60, 255), Color(250, 200, 88, 255)};
                case TimelineChartPalette::Forest:
                    return {Color(27, 67, 50, 255), Color(45, 106, 79, 255),
                            Color(64, 145, 108, 255), Color(82, 183, 136, 255),
                            Color(116, 198, 157, 255), Color(149, 213, 178, 255)};
                case TimelineChartPalette::Slate:
                    return {Color(46, 52, 64, 255), Color(59, 66, 82, 255),
                            Color(76, 86, 106, 255), Color(94, 107, 130, 255),
                            Color(115, 129, 152, 255), Color(136, 150, 171, 255)};
                case TimelineChartPalette::Mono:
                    return {Color(33, 37, 43, 255), Color(60, 66, 76, 255),
                            Color(90, 97, 109, 255), Color(120, 128, 141, 255),
                            Color(150, 158, 171, 255)};
                case TimelineChartPalette::CorporateBlue:
                case TimelineChartPalette::Custom:
                default:
                    return {Color(31, 97, 176, 255), Color(41, 128, 185, 255),
                            Color(52, 152, 219, 255), Color(93, 173, 226, 255),
                            Color(26, 188, 156, 255), Color(22, 160, 133, 255),
                            Color(72, 100, 132, 255), Color(120, 144, 168, 255)};
            }
        }

    } // namespace TimelineChartStyles

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace TimelineChartSamples {

        std::shared_ptr<TimelineChartDataSource> DevelopmentTimeline(int year) {
            auto source = std::make_shared<TimelineChartDataSource>();

            const int startId = source->AddBookend("Project start",
                                                   TimeAxis::Serial(year, 1, 15));
            source->SetEntryDetail(startId, "kick-off");
            const int endId = source->AddBookend("Project end",
                                                 TimeAxis::Serial(year, 12, 15));
            source->SetEntryDetail(endId, "hand-over");
            source->SetEntryColor(endId, Color(196, 60, 60, 255));

            // Phases below the axis
            const int referral = source->AddSpan("Partner referral",
                                                 year, 1, 15, year, 3, 31);
            const int legal = source->AddSpan("Legal agreements executed",
                                              year, 3, 1, year, 5, 30);
            const int review = source->AddSpan("Architecture review",
                                               year, 5, 1, year, 7, 15);
            const int handoff = source->AddSpan("Hand-off calls",
                                                year, 6, 15, year, 8, 31);
            const int ongoing = source->AddSpan("Ongoing partner development",
                                                year, 8, 1, year, 12, 15);
            const int marketing = source->AddSpan("Partner marketing",
                                                  year, 10, 1, year, 12, 31);
            for (int id : {referral, legal, review, handoff, ongoing, marketing}) {
                source->SetEntrySide(id, 1);
            }
            source->SetEntryProgress(referral, 1.0f);
            source->SetEntryProgress(legal, 1.0f);
            source->SetEntryProgress(review, 0.65f);
            source->SetEntryProgress(handoff, 0.3f);
            if (auto* e = source->GetEntry(ongoing)) e->openEnded = false;

            // Major milestones above the axis
            struct M { const char* name; int month; int day; };
            const M milestones[] = {
                    {"Milestone 1", 5, 15},
                    {"Milestone 2", 8, 15},
                    {"Milestone 3", 11, 1}};
            for (const auto& m : milestones) {
                const int id = source->AddMilestone(m.name, year, m.month, m.day);
                source->SetEntrySide(id, -1);
                source->SetEntryMarker(id, TimelineMarkerStyle::TriangleDown);
                source->SetEntryImportance(id, 1.6f);
                source->SetEntryColor(id, Color(43, 58, 84, 255));
            }

            // Smaller dated events, also above the axis
            const int contract = source->AddMilestone("Contract signed", year, 2, 20);
            source->SetEntrySide(contract, -1);
            source->SetEntryMarker(contract, TimelineMarkerStyle::Flag);
            const int pilot = source->AddMilestone("Pilot go-live", year, 9, 10);
            source->SetEntrySide(pilot, -1);
            source->SetEntryMarker(pilot, TimelineMarkerStyle::Flag);

            return source;
        }

        std::shared_ptr<TimelineChartDataSource> CompanyHistory() {
            auto source = std::make_shared<TimelineChartDataSource>();

            source->AddEra("Start-up years", TimeAxis::Serial(1985, 1, 1),
                           TimeAxis::Serial(1995, 1, 1));
            source->AddEra("Expansion", TimeAxis::Serial(1995, 1, 1),
                           TimeAxis::Serial(2010, 1, 1));
            source->AddEra("Platform era", TimeAxis::Serial(2010, 1, 1),
                           TimeAxis::Serial(2026, 1, 1));

            struct Event { const char* name; int year; int month; const char* detail; };
            const Event events[] = {
                    {"Founded", 1985, 4, "two engineers, one workshop"},
                    {"First export order", 1991, 9, "three countries"},
                    {"Factory opens", 1996, 6, "120 staff"},
                    {"IPO", 2003, 11, "listed"},
                    {"Acquisition", 2011, 2, "controls division"},
                    {"Cloud platform", 2017, 5, "SaaS launch"},
                    {"One million users", 2024, 8, "milestone"}};
            for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
                const int id = source->AddMilestone(events[i].name, events[i].year,
                                                    events[i].month, 1);
                source->SetEntryDetail(id, events[i].detail);
                source->SetEntryMarker(id, (i % 2 == 0) ? TimelineMarkerStyle::Circle
                                                        : TimelineMarkerStyle::Diamond);
            }
            return source;
        }

        std::vector<TimelineSwimlane> ProgramSwimlanes() {
            return {TimelineSwimlane("Delivery Management", "Programme office",
                                     Color(224, 106, 96, 255)),
                    TimelineSwimlane("Operations", "Platform team",
                                     Color(240, 173, 78, 255)),
                    TimelineSwimlane("Risk Management", "Compliance",
                                     Color(91, 168, 214, 255)),
                    TimelineSwimlane("Quality of Service", "SRE",
                                     Color(87, 178, 148, 255))};
        }

        std::shared_ptr<TimelineChartDataSource> SwimlaneProgram(int year) {
            auto source = std::make_shared<TimelineChartDataSource>();

            struct Row { const char* lane; const char* name;
                         int m1; int d1; int m2; int d2; bool milestone; };
            const Row rows[] = {
                    // Delivery Management
                    {"Delivery Management", "Usage baseline",   1,  8,  2, 20, false},
                    {"Delivery Management", "Phase 1",          2, 25,  5, 10, false},
                    {"Delivery Management", "Off-boarding",     5, 20,  7, 15, false},
                    {"Delivery Management", "Rollout Phase",    7, 20, 11, 30, false},
                    {"Delivery Management", "Kick-off",         1,  8,  1,  8, true},
                    {"Delivery Management", "Perf Review",      8, 12,  8, 12, true},
                    // Operations
                    {"Operations", "Security policy setup",     1, 20,  3, 15, false},
                    {"Operations", "Documentation programme",   3,  1,  6, 30, false},
                    {"Operations", "Station setup",             6, 10,  8, 20, false},
                    {"Operations", "Code development",          8, 15, 12, 10, false},
                    {"Operations", "Doc finalisation",          6, 30,  6, 30, true},
                    // Risk Management
                    {"Risk Management", "Risk profiling",       2,  1,  4, 10, false},
                    {"Risk Management", "Access & control",     4, 20,  7, 31, false},
                    {"Risk Management", "Content management",   8, 10, 11, 20, false},
                    {"Risk Management", "Hiring round 1",       3, 12,  3, 12, true},
                    {"Risk Management", "Certification",        7, 31,  7, 31, true},
                    {"Risk Management", "Password policy",     11, 20, 11, 20, true},
                    // Quality of Service
                    {"Quality of Service", "Alerts setup",      1, 15,  3, 31, false},
                    {"Quality of Service", "Availability metrics", 4, 15, 7, 20, false},
                    {"Quality of Service", "Uptime & SLA tracking", 8, 1, 12, 20, false},
                    {"Quality of Service", "User onboarding",   9, 10,  9, 10, true}};

            for (const Row& row : rows) {
                const int id = row.milestone
                        ? source->AddMilestone(row.name, year, row.m1, row.d1)
                        : source->AddSpan(row.name, year, row.m1, row.d1,
                                          year, row.m2, row.d2);
                source->SetEntrySwimlane(id, row.lane);
                if (row.milestone) {
                    source->SetEntryMarker(id, TimelineMarkerStyle::Diamond);
                    source->SetEntryImportance(id, 1.2f);
                }
            }
            return source;
        }

    } // namespace TimelineChartSamples

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasTimelineChart> CreateTimelineChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasTimelineChart>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasTimelineChart> CreateTimelineChartElement(
            const std::string& id, int x, int y, int width, int height,
            TimelineChartDesign design) {
        auto chart = std::make_shared<UltraCanvasTimelineChart>(id, x, y, width, height);
        chart->ApplyDesign(design);
        return chart;
    }

    std::shared_ptr<UltraCanvasTimelineChart> CreateTimelineChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<TimelineChartDataSource> data, TimelineChartDesign design) {
        auto chart = std::make_shared<UltraCanvasTimelineChart>(id, x, y, width, height);
        chart->ApplyDesign(design);
        chart->SetData(data);
        return chart;
    }

} // namespace UltraCanvas
