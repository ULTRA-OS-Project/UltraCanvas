// include/Plugins/Diagrams/UltraCanvasPertChart.h
// Interactive PERT (Program Evaluation and Review Technique) chart component
// Version: 1.1.0
//
// Changelog:
//   1.1.0 - Circle design gained a label mode (PertCircleLabel): Code keeps
//           the AOA look (code inside, name outside), Name centers the
//           activity name inside the circle as a single-cell node, with the
//           font auto-shrunk to fit. SetCircleLabelMode()/GetCircleLabelMode().
//   1.0.0 - Initial public release.
//
// A comprehensive project-scheduling diagram element. Supports the classic
// activity-on-node presentation (cards with activity code, duration and
// start/end dates), a compact presentation (rounded boxes with name and
// duration) and an activity-on-arrow style presentation (numbered circles
// with activity details along the connectors).
//
// Built-in analysis:
//   * Critical Path Method (CPM) forward/backward pass: earliest start /
//     earliest finish / latest start / latest finish / slack per activity.
//   * PERT three-point estimation: expected duration TE = (O + 4M + P) / 6,
//     per-activity variance ((P - O) / 6)^2, project variance along the
//     critical path and probability of finishing within a target duration.
//   * Cycle detection (a cyclic dependency graph disables scheduling).
//   * Automatic layered left-to-right layout with per-activity manual
//     position overrides (drag or SetActivityPosition).
//
// Design options (PertNodeDesign):
//   * Card         - header bar with the activity name, one row with
//                    activity code + duration, one row with start/end dates.
//   * DetailedCard - Card plus a "responsible" footer row.
//   * Compact      - rounded box with the name and "(N days)" underneath.
//   * Circle       - numbered circle per activity; name and duration are
//                    drawn along the outgoing dependencies (AOA look).
//
// Palette options (PertChartPaletteKind): Classic, Ocean, Vibrant, Pastel,
// Mint, Midnight, Dark and Monochrome built-ins, plus fully custom palettes
// via SetCustomPalette(). Activities may be color-coded by group (team);
// groups are shown in an optional legend.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <cmath>

#undef None
namespace UltraCanvas {

// =============================================================================
// PERT CHART ENUMERATIONS
// =============================================================================

// Visual presentation of an activity node.
enum class PertNodeDesign {
    Card,           // header + code/duration row + dates row
    DetailedCard,   // Card + responsible row
    Compact,        // rounded box, name + duration
    Circle          // numbered circle, details along connectors (AOA style)
};

// What the Circle design draws inside the circle.
enum class PertCircleLabel {
    Code,   // activity code inside, name outside above the circle (AOA look)
    Name    // activity name inside the circle (single-cell circular node)
};

// Routing of dependency connectors.
enum class PertConnectorStyle {
    Straight,
    Orthogonal,
    Curved
};

// Built-in color palettes.
enum class PertChartPaletteKind {
    Classic,        // white cards, blue headers (project-plan look)
    Ocean,          // deep blue headers, light blue fills
    Vibrant,        // saturated blue/orange/green group colors
    Pastel,         // soft muted fills
    Mint,           // teal circles / mint accents
    Midnight,       // dark navy nodes on light background
    Dark,           // dark background, light nodes
    Monochrome,     // grayscale
    Custom          // palette supplied via SetCustomPalette()
};

// =============================================================================
// PERT CHART PALETTE
// =============================================================================

// A complete color scheme for the chart. All colors used during rendering
// come from the palette unless overridden per activity / per dependency.
struct PertChartPalette {
    Color backgroundColor = Color(245, 247, 252, 255);
    Color gridColor = Color(232, 235, 242, 255);

    Color nodeHeaderColor = Color(43, 87, 154, 255);   // title bar / circle fill
    Color nodeFillColor = Color(255, 255, 255, 255);   // card body
    Color nodeBorderColor = Color(43, 87, 154, 255);
    Color nodeHeaderTextColor = Color(255, 255, 255, 255);
    Color nodeTextColor = Color(40, 45, 55, 255);
    Color nodeSecondaryTextColor = Color(110, 118, 130, 255); // dates/responsible
    Color nodeDividerColor = Color(210, 216, 226, 255);       // row/cell separators

    Color milestoneColor = Color(30, 41, 82, 255);     // start/end nodes
    Color milestoneTextColor = Color(255, 255, 255, 255);

    Color connectorColor = Color(120, 130, 145, 255);
    Color criticalConnectorColor = Color(217, 83, 60, 255);
    Color criticalNodeAccentColor = Color(217, 83, 60, 255); // border of critical nodes
    Color dummyConnectorColor = Color(150, 158, 170, 255);   // dashed dummy links

    Color connectorLabelColor = Color(70, 76, 88, 255);      // AOA labels
    Color selectionColor = Color(0, 120, 215, 255);
    Color legendTextColor = Color(60, 66, 78, 255);

    // Cycled through for groups that were not given an explicit color.
    std::vector<Color> groupColors = {
        Color(100, 160, 240, 255),
        Color(43, 87, 154, 255),
        Color(30, 41, 82, 255),
        Color(80, 190, 150, 255),
        Color(240, 150, 70, 255),
        Color(170, 110, 220, 255)
    };

    // Returns a copy of one of the built-in palettes. For
    // PertChartPaletteKind::Custom the Classic palette is returned.
    static PertChartPalette BuiltIn(PertChartPaletteKind kind);
};

// =============================================================================
// PERT CHART DATA STRUCTURES
// =============================================================================

struct PertActivity {
    std::string id;
    std::string name;
    std::string code;            // displayed activity number, e.g. "002"

    // Duration. When three-point estimates are set the expected duration
    // TE = (optimistic + 4 * mostLikely + pessimistic) / 6 is used for
    // scheduling instead of `duration`.
    double duration = 1.0;
    double optimistic = 0.0;
    double mostLikely = 0.0;
    double pessimistic = 0.0;
    bool hasEstimates = false;

    std::string startDate;       // display-only strings, any format
    std::string endDate;
    std::string responsible;

    std::string groupId;         // color-coding group ("" = ungrouped)
    bool isMilestone = false;    // start/end marker: zero duration by default

    // Position of the top-left corner in world coordinates. Managed by
    // AutoLayout() unless the user drags the node or calls
    // SetActivityPosition() (which sets hasManualPosition).
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;          // 0 = use the default for the active design
    double height = 0.0;
    bool hasManualPosition = false;

    // Optional per-activity color overrides (take precedence over the
    // palette and the group color).
    bool hasCustomColors = false;
    Color customHeaderColor;
    Color customFillColor;
    Color customBorderColor;
    Color customTextColor;

    // Computed by ComputeSchedule(); do not set manually.
    double earliestStart = 0.0;
    double earliestFinish = 0.0;
    double latestStart = 0.0;
    double latestFinish = 0.0;
    double slack = 0.0;
    bool onCriticalPath = false;
    int layer = 0;

    bool isSelected = false;
    bool isHovered = false;

    PertActivity() = default;
    PertActivity(const std::string& activityId, const std::string& activityName)
        : id(activityId), name(activityName) {}
};

struct PertDependency {
    std::string id;
    std::string fromId;
    std::string toId;

    // Dummy dependencies model AOA dummy activities: zero-duration logical
    // links rendered with a dashed line.
    bool isDummy = false;

    std::string label;           // optional label at the connector midpoint

    bool hasCustomColor = false;
    Color customColor;

    bool isSelected = false;

    PertDependency() = default;
    PertDependency(const std::string& depId, const std::string& from, const std::string& to)
        : id(depId), fromId(from), toId(to) {}
};

struct PertGroup {
    std::string id;
    std::string name;
    Color color;
    bool colorAssigned = false;  // false = auto-assign from palette.groupColors
};

// Global (non-color) style knobs.
struct PertChartStyle {
    std::string fontFamily = "Arial";
    double baseFontSize = 11.0;
    double headerFontSize = 12.0;
    double secondaryFontSize = 10.0;

    bool showGrid = true;
    double gridSpacing = 20.0;

    double cornerRadius = 8.0;
    double borderWidth = 1.5;
    double connectorWidth = 1.8;
    double criticalConnectorWidth = 3.0;
    double arrowSize = 9.0;
    double selectionWidth = 2.0;

    // AutoLayout() spacing between layers (horizontal) and rows (vertical).
    double layerSpacing = 90.0;
    double rowSpacing = 40.0;
    double marginX = 40.0;
    double marginY = 40.0;
};

// =============================================================================
// PERT CHART COMPONENT CLASS
// =============================================================================

class UltraCanvasPertChart : public UltraCanvasUIElement {
public:
    UltraCanvasPertChart(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }

    // =============================================================================
    // ACTIVITY MANAGEMENT
    // =============================================================================

    void AddActivity(const std::string& id, const std::string& name);
    void AddActivity(const std::string& id, const std::string& name, double duration);
    void AddActivity(const std::string& id, const std::string& name, double duration,
                     const std::string& code);
    // Milestones are zero-duration start/end markers rendered with the
    // palette's milestone colors.
    void AddMilestone(const std::string& id, const std::string& name);
    void RemoveActivity(const std::string& id);
    void Clear();

    void SetActivityDuration(const std::string& id, double duration);
    // PERT three-point estimate; scheduling switches to TE = (o + 4m + p) / 6.
    void SetActivityThreePointEstimate(const std::string& id,
                                       double optimistic, double mostLikely, double pessimistic);
    void ClearActivityThreePointEstimate(const std::string& id);
    void SetActivityDates(const std::string& id, const std::string& startDate,
                          const std::string& endDate);
    void SetActivityResponsible(const std::string& id, const std::string& responsible);
    void SetActivityCode(const std::string& id, const std::string& code);
    void SetActivityGroup(const std::string& id, const std::string& groupId);
    void SetActivityColors(const std::string& id, const Color& headerColor,
                           const Color& fillColor, const Color& borderColor,
                           const Color& textColor);
    void ClearActivityColors(const std::string& id);
    // Manual placement; marks the activity so AutoLayout() leaves it alone.
    void SetActivityPosition(const std::string& id, double x, double y);
    void SetActivitySize(const std::string& id, double width, double height);

    PertActivity* GetActivity(const std::string& id);
    const PertActivity* GetActivity(const std::string& id) const;
    std::vector<std::string> GetAllActivityIds() const;

    // =============================================================================
    // DEPENDENCY MANAGEMENT
    // =============================================================================

    void AddDependency(const std::string& id, const std::string& fromId,
                       const std::string& toId);
    // Dashed zero-duration logical link (AOA "dummy activity").
    void AddDummyDependency(const std::string& id, const std::string& fromId,
                            const std::string& toId);
    void RemoveDependency(const std::string& id);
    void SetDependencyLabel(const std::string& id, const std::string& label);
    void SetDependencyColor(const std::string& id, const Color& color);
    void ClearDependencyColor(const std::string& id);

    PertDependency* GetDependency(const std::string& id);
    std::vector<std::string> GetAllDependencyIds() const;

    // =============================================================================
    // GROUPS (team color-coding + legend)
    // =============================================================================

    void DefineGroup(const std::string& groupId, const std::string& name);
    void DefineGroup(const std::string& groupId, const std::string& name, const Color& color);
    void RemoveGroup(const std::string& groupId);
    const PertGroup* GetGroup(const std::string& groupId) const;
    std::vector<std::string> GetAllGroupIds() const;
    // Effective color of a group (explicit or auto-assigned from the palette).
    Color GetGroupColor(const std::string& groupId) const;

    void SetLegendVisible(bool visible);
    bool IsLegendVisible() const { return showLegend; }

    // =============================================================================
    // SCHEDULE ANALYSIS (CPM + PERT statistics)
    // =============================================================================

    // Runs the CPM forward/backward pass and fills the computed fields of
    // every activity. Returns false (and leaves results untouched) when the
    // dependency graph contains a cycle. Called automatically before
    // rendering when the graph changed.
    bool ComputeSchedule();
    bool HasCycle() const;

    // Longest path length through the network, in duration units.
    double GetProjectDuration() const;
    // Activity ids on the critical path, in topological order.
    std::vector<std::string> GetCriticalPath() const;
    bool IsOnCriticalPath(const std::string& id) const;
    double GetSlack(const std::string& id) const;

    // Scheduling duration of one activity: TE when three-point estimates are
    // set, `duration` otherwise (0 for milestones without explicit duration).
    double GetExpectedDuration(const std::string& id) const;
    // ((pessimistic - optimistic) / 6)^2; 0 without three-point estimates.
    double GetActivityVariance(const std::string& id) const;
    // Sum of variances along the critical path.
    double GetProjectVariance() const;
    double GetProjectStandardDeviation() const;
    // P(project finishes within targetDuration), assuming the usual normal
    // approximation of the critical path length. Returns 0.5 when the
    // project variance is zero and targetDuration equals the duration.
    double GetCompletionProbability(double targetDuration) const;

    // =============================================================================
    // DESIGN OPTIONS
    // =============================================================================

    void SetNodeDesign(PertNodeDesign design);
    PertNodeDesign GetNodeDesign() const { return nodeDesign; }
    void SetConnectorStyle(PertConnectorStyle style);
    PertConnectorStyle GetConnectorStyle() const { return connectorStyle; }

    // Circle design only: what goes inside the circle. In Name mode the
    // activity name is centered inside (font auto-shrunk to fit) and nothing
    // is drawn above the circle; in Code mode (default) the code sits inside
    // and the name above, as in classic activity-on-arrow charts.
    void SetCircleLabelMode(PertCircleLabel mode);
    PertCircleLabel GetCircleLabelMode() const { return circleLabel; }

    // Critical-path emphasis (thicker connectors + accent border). On by default.
    void SetCriticalPathHighlight(bool enable);
    bool GetCriticalPathHighlight() const { return highlightCriticalPath; }

    void SetShowDates(bool show);            // Card/DetailedCard date row
    void SetShowActivityCodes(bool show);    // code cell / circle number
    void SetShowDurations(bool show);
    void SetShowSlack(bool show);            // "slack: N" hint on non-critical nodes
    void SetDurationUnit(const std::string& unitSuffix); // e.g. "days"
    const std::string& GetDurationUnit() const { return durationUnit; }

    PertChartStyle& GetStyle() { return style; }
    const PertChartStyle& GetStyle() const { return style; }
    void SetFontFamily(const std::string& fontFamily);
    void SetBackgroundColor(const Color& color); // palette override shortcut
    void SetGridVisible(bool visible, double spacing = 20.0);

    // =============================================================================
    // PALETTE OPTIONS
    // =============================================================================

    void SetPalette(PertChartPaletteKind kind);
    void SetCustomPalette(const PertChartPalette& palette);
    PertChartPaletteKind GetPaletteKind() const { return paletteKind; }
    const PertChartPalette& GetPalette() const { return palette; }

    // =============================================================================
    // LAYOUT & VIEW
    // =============================================================================

    // Layered left-to-right auto layout (layer = longest dependency chain
    // from the start). Skips activities with manual positions unless
    // `resetManualPositions` is true.
    void AutoLayout(bool resetManualPositions = false);
    // Adjusts zoom + pan so the whole diagram is visible.
    void FitToView();

    void SetZoomLevel(double zoom);
    double GetZoomLevel() const { return zoomLevel; }
    void SetPanOffset(double x, double y);
    Point2Dd GetPanOffset() const { return panOffset; }

    // =============================================================================
    // INTERACTION
    // =============================================================================

    void SelectActivity(const std::string& id);
    void DeselectAll();
    std::string GetSelectedActivityId() const { return selectedActivityId; }

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // =============================================================================
    // CALLBACKS
    // =============================================================================

    std::function<void(const std::string&)> onActivityClick;
    std::function<void(const std::string&)> onActivityDoubleClick;
    std::function<void(const std::string&)> onActivitySelected;
    std::function<void(const std::string&, double, double)> onActivityDragged;
    std::function<void(const std::string&)> onDependencyClick;
    std::function<void(double /*projectDuration*/)> onScheduleComputed;

private:
    // =============================================================================
    // DATA MEMBERS
    // =============================================================================

    std::map<std::string, PertActivity> activities;
    std::vector<PertDependency> dependencies;
    std::vector<PertGroup> groups;

    PertChartStyle style;
    PertChartPalette palette = PertChartPalette::BuiltIn(PertChartPaletteKind::Classic);
    PertChartPaletteKind paletteKind = PertChartPaletteKind::Classic;

    PertNodeDesign nodeDesign = PertNodeDesign::Card;
    PertCircleLabel circleLabel = PertCircleLabel::Code;
    PertConnectorStyle connectorStyle = PertConnectorStyle::Orthogonal;
    bool highlightCriticalPath = true;
    bool showDates = true;
    bool showActivityCodes = true;
    bool showDurations = true;
    bool showSlack = false;
    bool showLegend = true;
    std::string durationUnit = "days";

    std::string selectedActivityId;
    std::string hoveredActivityId;

    bool scheduleDirty = true;
    bool layoutDirty = true;
    bool scheduleValid = false;   // last ComputeSchedule() succeeded
    double projectDuration = 0.0;

    bool isDraggingNode = false;
    bool isPanning = false;
    double dragOffsetX = 0.0, dragOffsetY = 0.0;
    Point2Di panStartPos;
    Point2Dd panStartOffset;

    double zoomLevel = 1.0;
    Point2Dd panOffset;

    // =============================================================================
    // SCHEDULING HELPERS
    // =============================================================================

    // Kahn topological order of activity ids; empty when the graph is cyclic
    // and hasActivities is true.
    std::vector<std::string> TopologicalOrder() const;
    void MarkGraphChanged();      // scheduleDirty + layoutDirty + redraw
    void EnsureScheduleAndLayout();

    // =============================================================================
    // GEOMETRY HELPERS
    // =============================================================================

    double DefaultNodeWidth() const;
    double DefaultNodeHeight() const;
    double NodeWidth(const PertActivity& a) const;
    double NodeHeight(const PertActivity& a) const;
    Point2Dd NodeCenter(const PertActivity& a) const;
    // Connector attachment point on the node silhouette facing `toward`.
    Point2Dd ConnectorPoint(const PertActivity& a, const Point2Dd& toward) const;
    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    std::string FindActivityAt(const Point2Dd& worldPos) const;
    std::string FindDependencyAt(const Point2Dd& worldPos) const;
    bool PointInActivity(const PertActivity& a, const Point2Dd& p) const;
    double DistanceToSegment(const Point2Dd& p, const Point2Dd& a, const Point2Dd& b) const;

    // Effective node colors after palette / group / custom-override resolution.
    void ResolveNodeColors(const PertActivity& a, Color& header, Color& fill,
                           Color& border, Color& headerText, Color& bodyText) const;

    // =============================================================================
    // RENDERING HELPERS
    // =============================================================================

    void RenderGrid(IRenderContext* ctx);
    void RenderDependencies(IRenderContext* ctx);
    void RenderDependency(IRenderContext* ctx, const PertDependency& dep);
    void RenderActivities(IRenderContext* ctx);
    void RenderActivity(IRenderContext* ctx, const PertActivity& a);
    void RenderCardNode(IRenderContext* ctx, const PertActivity& a, bool detailed);
    void RenderCompactNode(IRenderContext* ctx, const PertActivity& a);
    void RenderCircleNode(IRenderContext* ctx, const PertActivity& a);
    void RenderSelectionHighlight(IRenderContext* ctx, const PertActivity& a);
    void RenderLegend(IRenderContext* ctx);
    void RenderArrowHead(IRenderContext* ctx, const Point2Dd& tip, double angle,
                         const Color& color);
    void RenderConnectorLabel(IRenderContext* ctx, const PertDependency& dep,
                              const Point2Dd& anchor);
    std::vector<Point2Dd> ComputeConnectorPath(const Point2Dd& start, const Point2Dd& end) const;
    std::string FormatDuration(double d) const;

    // =============================================================================
    // EVENT HANDLERS
    // =============================================================================

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleDoubleClick(const UCEvent& event);
};

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

inline std::shared_ptr<UltraCanvasPertChart> CreatePertChart(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasPertChart>(id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasPertChart> CreatePertChart(
        const std::string& id, int x, int y, int w, int h,
        PertNodeDesign design, PertChartPaletteKind paletteKind) {
    auto chart = std::make_shared<UltraCanvasPertChart>(id, x, y, w, h);
    chart->SetNodeDesign(design);
    chart->SetPalette(paletteKind);
    return chart;
}

} // namespace UltraCanvas
