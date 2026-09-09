// include/Plugins/Charts/UltraCanvasTimelineChart.h
// Chronological timeline: milestones and spans placed to scale on a real date
// axis, with automatic lane packing and callout labels.
//
// This element covers "Family B" of the timeline proposal - the date-accurate
// timeline. It deliberately has no task table and no dependency graph: for a
// full project schedule use UltraCanvasGanttChart, and for a presentation
// timeline whose items are evenly spaced use UltraCanvasTimelineDiagram.
//
// See Docs/UltraCanvas/UltraCanvasTimelineDiagramProposal.md for the research.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmoothScroll.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasTimeAxis.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// TIMELINE CHART ENUMERATIONS
// =============================================================================

    enum class TimelineEntryKind {
        Milestone,   // Point in time, drawn as a marker on the axis
        Span,        // Date range, drawn as a bar in a packed lane
        Era,         // Date range, drawn as a background band behind everything
        Bookend      // Point in time anchored to the axis end (project start/end)
    };

    enum class TimelineMarkerStyle {
        Diamond,
        Circle,
        Square,
        TriangleUp,
        TriangleDown,
        Flag,        // Pennant on a short pole
        Pin,         // Map pin
        Star
    };

    enum class TimelineBarStyle {
        Flat,
        Rounded,
        Pill,
        Line         // Thin bar with square end caps
    };

    // Where the date axis sits inside the plot area.
    enum class TimelineAxisPosition {
        Top,         // Axis at the top, everything hangs below it
        Bottom,      // Axis at the bottom, everything stacks above it
        Center       // Axis in the middle, entries above and below
    };

    // How an entry's label is placed relative to its bar/marker.
    enum class TimelineLabelPlacement {
        Hidden,
        InsideBar,   // Inside the bar when it fits, callout otherwise
        Callout,     // Always beside/above the entry with a leader line
        AutoPlace    // Inside when it fits, otherwise a callout
    };

    enum class TimelineChartPalette {
        CorporateBlue,
        Vibrant,
        Pastel,
        Ocean,
        Sunset,
        Forest,
        Slate,
        Mono,
        Custom
    };

    // How entries are assigned to rows.
    enum class TimelineLaneMode {
        Packed,      // Auto-packed into as few lanes as fit (default)
        Swimlanes    // One named band per category, sub-packed inside the band
    };

    enum class TimelineChartDesign {
        Classic,     // Flat bars, plain header, print friendly
        Modern,      // Rounded colorful bars, tinted header bands
        Minimal,     // Thin line bars, hairline grid
        Roadmap,     // Big markers, era bands, quarter header
        Dark         // Dark background variant of Modern
    };

// =============================================================================
// TIMELINE CHART DATA
// =============================================================================

    // One named row of a swimlane timeline. Entries are assigned to a lane by
    // TimelineChartEntry::swimlaneName.
    struct TimelineSwimlane {
        std::string name;
        std::string detail;               // Optional second header line (owner, team)
        Color color = Color(0, 0, 0, 0);  // Transparent = palette entry for this row

        TimelineSwimlane() = default;
        explicit TimelineSwimlane(const std::string& n) : name(n) {}
        TimelineSwimlane(const std::string& n, const Color& c) : name(n), color(c) {}
        TimelineSwimlane(const std::string& n, const std::string& d, const Color& c)
                : name(n), detail(d), color(c) {}
    };

    struct TimelineChartEntry {
        TimelineEntryKind kind = TimelineEntryKind::Milestone;
        std::string name;
        std::string detail;        // Second label line (a date caption, an owner)
        std::string iconGlyph;     // Short text/glyph drawn inside the marker

        // Day serials since 1970-01-01, fractional part = time of day.
        // Milestones use `start` only; span ends are inclusive-exclusive, so a
        // one-day span is start..start+1.
        double start = 0.0;
        double end = 0.0;
        bool openEnded = false;    // Span with no known end: fades out

        // Uncertainty in days, drawn as a lighter extension on each end
        double startUncertainty = 0.0;
        double endUncertainty = 0.0;

        int lane = -1;             // -1 = pack automatically
        int side = 0;              // -1 above the axis, +1 below, 0 = automatic
        std::string swimlaneName;  // Row this entry belongs to in Swimlanes mode
        Color color = Color(0, 0, 0, 0);          // Transparent = palette
        TimelineMarkerStyle marker = TimelineMarkerStyle::Diamond;
        float importance = 1.0f;   // Scales the marker; higher wins label space
        float progress = -1.0f;    // 0..1 on a span; < 0 = no progress display
        std::string tooltip;       // Overrides the generated tooltip

        TimelineChartEntry() = default;
    };

    // Owns the entries. Mirrors GanttDataSource in shape so the two are
    // interchangeable in application code.
    class TimelineChartDataSource {
    public:
        // ===== CONSTRUCTION =====
        int AddMilestone(const std::string& name, double serial);
        int AddMilestone(const std::string& name, int year, int month, int day);
        int AddSpan(const std::string& name, double from, double to);
        int AddSpan(const std::string& name, int y1, int m1, int d1, int y2, int m2, int d2);
        int AddEra(const std::string& name, double from, double to);
        int AddBookend(const std::string& name, double serial);
        int AddEntry(const TimelineChartEntry& entry);

        // ===== EDITING =====
        bool RemoveEntry(int id);
        void Clear();
        TimelineChartEntry* GetEntry(int id);
        const TimelineChartEntry* GetEntry(int id) const;

        bool SetEntryColor(int id, const Color& color);
        bool SetEntryDetail(int id, const std::string& detail);
        bool SetEntryMarker(int id, TimelineMarkerStyle marker);
        bool SetEntryLane(int id, int lane);
        bool SetEntrySide(int id, int side);
        bool SetEntryProgress(int id, float progress);
        bool SetEntryImportance(int id, float importance);
        bool SetEntryDates(int id, double start, double end);
        bool SetEntrySwimlane(int id, const std::string& swimlaneName);

        // ===== QUERIES =====
        const std::vector<TimelineChartEntry>& GetEntries() const { return entries; }
        std::vector<TimelineChartEntry>& EditEntries() { return entries; }
        size_t Count() const { return entries.size(); }
        // Earliest start / latest end across every entry. Returns false if empty.
        bool GetDateBounds(double& outStart, double& outEnd) const;

        // Builds a stakeholder timeline from a Gantt-style plan: pass leaf task
        // rows as spans and zero-duration rows as milestones. Kept generic so
        // this header does not depend on the Gantt chart element.
        struct SimpleTask {
            std::string name;
            double start = 0.0;
            double end = 0.0;
            bool milestone = false;
        };
        void LoadTasks(const std::vector<SimpleTask>& tasks);

    private:
        std::vector<TimelineChartEntry> entries;
        std::vector<int> ids;
        int nextId = 1;
        int IndexOf(int id) const;
    };

// =============================================================================
// STYLE
// =============================================================================

    struct TimelineChartStyle {
        // ===== AXIS =====
        TimelineAxisPosition axisPosition = TimelineAxisPosition::Center;
        TimelineScale scale = TimelineScale::Auto;
        double axisThickness = 6.0;
        double majorTierHeight = 22.0;
        double minorTierHeight = 20.0;
        bool showMajorTier = true;
        bool showMinorTier = true;
        bool showGrid = true;
        bool shadeWeekends = true;
        bool showNowMarker = true;
        double axisPadFraction = 0.04;    // Range padding when fitting to data

        // ===== LANES =====
        double laneHeight = 30.0;
        double laneGap = 6.0;
        double axisGap = 14.0;            // Space between the axis and lane 0

        // ===== SWIMLANES =====
        double swimlaneHeaderWidth = 140.0;   // Left gutter carrying the row names
        double swimlaneMinHeight = 54.0;
        double swimlanePadding = 7.0;         // Inset between a band and its content
        double swimlaneGap = 4.0;             // Gap between bands
        bool showSwimlaneHeaders = true;
        bool showSwimlaneBands = true;
        bool showSwimlaneSeparators = true;
        int swimlaneBandAlpha = 26;
        int swimlaneHeaderAlpha = 52;
        float swimlaneTitleFontSize = 11.5f;

        // ===== BARS =====
        TimelineBarStyle barStyle = TimelineBarStyle::Rounded;
        double barHeight = 20.0;
        double barCornerRadius = 5.0;
        bool showProgress = true;

        // ===== MARKERS =====
        TimelineMarkerStyle defaultMarker = TimelineMarkerStyle::Diamond;
        double markerSize = 11.0;         // Half-extent at importance 1.0
        double markerBorderWidth = 2.0;

        // ===== LABELS =====
        TimelineLabelPlacement labelPlacement = TimelineLabelPlacement::AutoPlace;
        bool showDetails = true;          // Second label line
        bool showDateCaptions = true;     // Auto date under milestone labels
        double leaderLength = 10.0;
        double labelGap = 8.0;

        // ===== ERAS =====
        bool showEraBands = true;
        int eraAlpha = 38;
        bool showEraLabels = true;

        // ===== FONTS =====
        float majorTierFontSize = 12.0f;
        float minorTierFontSize = 10.0f;
        float labelFontSize = 11.5f;
        float detailFontSize = 9.5f;
        float markerFontSize = 10.0f;

        // ===== COLORS =====
        Color axisColor = Color(190, 196, 206, 255);
        Color gridColor = Color(230, 233, 238, 255);
        Color headerFillColor = Color(244, 246, 249, 255);
        Color headerTextColor = Color(78, 86, 100, 255);
        Color textColor = Color(38, 42, 51, 255);
        Color mutedTextColor = Color(112, 119, 132, 255);
        Color weekendColor = Color(244, 246, 249, 255);
        Color nowColor = Color(224, 62, 62, 255);
        Color leaderColor = Color(178, 185, 196, 255);
        Color progressColor = Color(255, 255, 255, 90);
    };

    // Identifies one entry for hit testing. index < 0 means "no entry".
    struct TimelineEntryRef {
        int index = -1;
        bool IsValid() const { return index >= 0; }
        bool operator==(const TimelineEntryRef& o) const { return index == o.index; }
        bool operator!=(const TimelineEntryRef& o) const { return index != o.index; }
    };

// =============================================================================
// TIMELINE CHART ELEMENT
// =============================================================================

    class UltraCanvasTimelineChart : public UltraCanvasChartElementBase {
    private:
        // ===== DATA =====
        std::shared_ptr<TimelineChartDataSource> data;
        std::string subtitle;

        // ===== CONFIGURATION =====
        TimelineChartStyle style;
        TimelineChartDesign design = TimelineChartDesign::Modern;
        TimelineLaneMode laneMode = TimelineLaneMode::Packed;
        std::vector<TimelineSwimlane> swimlanes;   // Empty = derived from the data
        TimelineChartPalette palette = TimelineChartPalette::CorporateBlue;
        std::vector<Color> customPalette;
        bool darkTheme = false;
        double nowSerial = 0.0;
        bool hasNow = false;

        // ===== VIEW =====
        TimeAxis axis;
        bool viewInitialized = false;
        bool panning = false;
        // Wheel zoom eases in instead of stepping: the animator hands out a
        // series of small factors, each applied with the axis' own ZoomAbout()
        // (see UltraCanvasSmoothScroll.h).
        UltraCanvasSmoothZoom zoomAnim;
        double zoomAnchorX = 0.0;
        Point2Di panAnchor;

        // ===== INTERACTION STATE =====
        TimelineEntryRef hoveredEntry;
        TimelineEntryRef selectedEntry;

        // ===== LAYOUT CACHE (element-local coordinates) =====
        struct EntryLayout {
            int entryIndex = -1;
            Rect2Dd shapeRect;      // Bar rect, or the marker's bounding box
            Rect2Dd labelRect;
            Rect2Dd hitRect;
            Point2Dd markerCenter;
            Point2Dd leaderFrom;
            Point2Dd leaderTo;
            Color color;
            int side = -1;
            int row = -1;
            bool hasLeader = false;
            bool labelInside = false;
            bool visible = false;
        };
        struct SwimlaneBand {
            Rect2Dd rect;
            double contentTop = 0.0;   // Inside the band padding
        };
        struct ChartLayout {
            Rect2Dd plotArea;       // Below the header tiers, right of the gutter
            Rect2Dd headerArea;
            Rect2Dd gutterArea;     // Swimlane name column (zero width when unused)
            double axisY = 0.0;
            std::vector<EntryLayout> entries;   // Parallel to data->GetEntries()
            std::vector<Rect2Dd> eraBands;
            std::vector<size_t> eraIndices;
            std::vector<TimelineSwimlane> lanes;   // Declared or derived
            std::vector<SwimlaneBand> laneBands;   // Parallel to lanes
            bool valid = false;
        };
        ChartLayout layout;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasTimelineChart(const std::string& id, int x, int y, int w, int h);

        // ===== DATA =====
        void SetData(std::shared_ptr<TimelineChartDataSource> source);
        std::shared_ptr<TimelineChartDataSource> GetData() const { return data; }
        void DataChanged();          // Call after editing the data source
        void LoadSampleData();       // Built-in development-timeline example

        // ===== DESIGN & STYLE =====
        void SetDesign(TimelineChartDesign d);
        TimelineChartDesign GetDesign() const { return design; }
        void ApplyDesign(TimelineChartDesign d);   // Replaces the whole style

        void SetStyle(const TimelineChartStyle& s);
        const TimelineChartStyle& GetStyle() const { return style; }
        TimelineChartStyle& EditStyle() { return style; }
        void StyleChanged();

        void SetDarkTheme(bool dark);
        bool GetDarkTheme() const { return darkTheme; }

        // ===== LANE MODE & SWIMLANES =====
        void SetLaneMode(TimelineLaneMode mode);
        TimelineLaneMode GetLaneMode() const { return laneMode; }

        // Declares the rows explicitly. When left empty, Swimlanes mode derives
        // them from the distinct TimelineChartEntry::swimlaneName values in the
        // order they first appear.
        void SetSwimlanes(const std::vector<TimelineSwimlane>& lanes);
        const std::vector<TimelineSwimlane>& GetSwimlanes() const { return swimlanes; }
        void ClearSwimlanes();
        int FindSwimlane(const std::string& name) const;   // -1 when absent
        // The rows actually drawn - declared or derived. Valid after a render.
        const std::vector<TimelineSwimlane>& GetResolvedSwimlanes() const;
        // Band rectangle of a row, element-local. False when out of range.
        bool GetSwimlaneRect(size_t index, Rect2Dd& outRect) const;

        void SetPalette(TimelineChartPalette p);
        TimelineChartPalette GetPalette() const { return palette; }
        void SetCustomPalette(const std::vector<Color>& colors);
        const std::vector<Color>& GetPaletteColors() const;

        // ===== AXIS & VIEW =====
        void SetScale(TimelineScale scale);
        TimelineScale GetScale() const { return style.scale; }
        void SetAxisPosition(TimelineAxisPosition position);

        void SetDateRange(double startSerial, double endSerial);
        void SetDateRange(int y1, int m1, int d1, int y2, int m2, int d2);
        void GetDateRange(double& outStart, double& outEnd) const;
        void FitToData();            // Range from the data plus padding
        void ResetView() { FitToData(); }

        void SetNow(double serial);
        void SetNow(int year, int month, int day);
        void ClearNow();

        void ZoomBy(double factor);  // < 1 zooms in, about the view center
        void PanDays(double days);

        // ===== TITLES =====
        void SetSubtitle(const std::string& text);
        const std::string& GetSubtitle() const { return subtitle; }

        // ===== SELECTION =====
        int GetSelectedIndex() const { return selectedEntry.index; }
        void SetSelectedIndex(int index);
        void ClearSelection();

        // ===== GEOMETRY QUERIES =====
        // Valid after the first render.
        bool GetEntryRect(size_t index, Rect2Dd& outRect) const;
        double DateToPixel(double serial) const { return axis.ToPixel(serial); }
        double PixelToDate(double pixel) const { return axis.ToSerial(pixel); }

        // ===== CALLBACKS =====
        std::function<void(size_t, const TimelineChartEntry&)> onEntrySelect;
        std::function<void(size_t, const TimelineChartEntry&)> onEntryHover;
        std::function<void(size_t, const TimelineChartEntry&)> onEntryDoubleClick;
        std::function<void()> onSelectionChange;
        std::function<void()> onViewChanged;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== HELPERS =====
        Color EntryColor(size_t index) const;
        Color TextColor() const { return style.textColor; }
        double TitleBandHeight() const;
        void EnsureView();
        void InvalidateLayout() { layout.valid = false; }

        // ===== LAYOUT =====
        void UpdateLayout(IRenderContext* ctx);
        void LayoutEras();
        // Spans and milestone labels share one packer, so a callout can never
        // land on top of a bar.
        void LayoutEntries(IRenderContext* ctx);
        // Swimlanes mode: one band per row, the same packer run inside each band
        void ResolveSwimlanes();
        void LayoutSwimlaneEntries(IRenderContext* ctx);
        int SwimlaneOf(const TimelineChartEntry& entry) const;
        Color SwimlaneColor(size_t index) const;

        // ===== DRAWING =====
        void DrawTitles(IRenderContext* ctx);
        void DrawEras(IRenderContext* ctx);
        void DrawSwimlanes(IRenderContext* ctx);
        void DrawHeader(IRenderContext* ctx);
        void DrawAxis(IRenderContext* ctx);
        void DrawGrid(IRenderContext* ctx, const std::vector<TimeAxisTick>& ticks);
        void DrawSpans(IRenderContext* ctx);
        void DrawMilestones(IRenderContext* ctx);
        void DrawNowMarker(IRenderContext* ctx);
        void DrawMarkerShape(IRenderContext* ctx, const Point2Dd& center, double size,
                             TimelineMarkerStyle marker);
        void DrawEntryLabel(IRenderContext* ctx, size_t index, const EntryLayout& el);
        void DrawHighlight(IRenderContext* ctx, size_t index, const EntryLayout& el);

        // ===== TEXT =====
        std::string EntryDateCaption(const TimelineChartEntry& entry) const;
        std::string BuildTooltip(size_t index) const;

        // ===== EVENTS =====
        TimelineEntryRef FindEntryAt(const Point2Di& pos) const;
        bool HandleEntryClick(const UCEvent& event);
        bool HandleEntryDoubleClick(const UCEvent& event);
    };

// =============================================================================
// STYLE PRESETS & SAMPLES
// =============================================================================

    namespace TimelineChartStyles {
        TimelineChartStyle CreateForDesign(TimelineChartDesign design);
        std::vector<Color> GetPaletteColors(TimelineChartPalette palette);
    }

    namespace TimelineChartSamples {
        // Partner-development plan: phases, milestones and project bookends,
        // modeled on the classic "development timeline" poster
        std::shared_ptr<TimelineChartDataSource> DevelopmentTimeline(int year);
        // Company history over five decades, for the wide-scale case
        std::shared_ptr<TimelineChartDataSource> CompanyHistory();
        // Programme plan grouped into four workstream rows, for Swimlanes mode
        std::shared_ptr<TimelineChartDataSource> SwimlaneProgram(int year);
        // The rows SwimlaneProgram() expects, in order
        std::vector<TimelineSwimlane> ProgramSwimlanes();
    }

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

    std::shared_ptr<UltraCanvasTimelineChart> CreateTimelineChartElement(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasTimelineChart> CreateTimelineChartElement(
            const std::string& id, int x, int y, int width, int height,
            TimelineChartDesign design);

    std::shared_ptr<UltraCanvasTimelineChart> CreateTimelineChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<TimelineChartDataSource> data, TimelineChartDesign design);

} // namespace UltraCanvas
