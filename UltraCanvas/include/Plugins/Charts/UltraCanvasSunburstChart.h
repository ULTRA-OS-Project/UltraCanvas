// include/Plugins/Charts/UltraCanvasSunburstChart.h
// Hierarchical sunburst / radial partition chart element for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasRenderContext.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

// X11 defines `None` as a macro that clobbers our enumerators.
#ifdef None
#undef None
#endif

namespace UltraCanvas {

// =============================================================================
// SUNBURST DATA MODEL
// =============================================================================

    // A node in the sunburst hierarchy. Leaf nodes carry a value; the weight of
    // a branch node is the sum of its children (its own `value` is ignored once
    // children are added, matching the standard sunburst/partition convention).
    struct SunburstNode : public std::enable_shared_from_this<SunburstNode> {
        std::string label;
        double value = 0.0;
        Color color = Colors::Transparent;   // Transparent = derive automatically
        std::vector<std::shared_ptr<SunburstNode>> children;
        std::weak_ptr<SunburstNode> parent;

        SunburstNode() = default;
        SunburstNode(const std::string& lbl, double val = 0.0,
                     const Color& clr = Colors::Transparent)
            : label(lbl), value(val), color(clr) {}

        std::shared_ptr<SunburstNode> AddChild(const std::string& lbl,
                                               double val = 0.0,
                                               const Color& clr = Colors::Transparent);
        std::shared_ptr<SunburstNode> FindChild(const std::string& lbl) const;

        bool IsLeaf() const { return children.empty(); }

        // Effective weight: sum of children for branches, own value for leaves.
        double AggregatedValue() const;

        // Depth of the subtree rooted here (leaf = 1).
        int SubtreeDepth() const;
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class SunburstLabelContent {
        None,
        Name,
        Value,
        Percentage,
        NameValue,
        NamePercentage
    };

    enum class SunburstLabelOrientation {
        Auto,           // Radial when it fits better, horizontal otherwise
        Horizontal,     // Always horizontal
        Radial          // Text runs along the radius, flipped on the left side
    };

    enum class SunburstShadeMode {
        None,           // Children inherit the branch color unchanged
        LightenByDepth, // Each deeper ring is progressively lighter
        DarkenByDepth   // Each deeper ring is progressively darker
    };

// =============================================================================
// SUNBURST CHART ELEMENT
// =============================================================================

    class UltraCanvasSunburstChart : public UltraCanvasChartElementBase {
    public:
        UltraCanvasSunburstChart(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasSunburstChart() override = default;

        // ===== Data management =====
        // The root node itself is not drawn; its children form the innermost ring.
        std::shared_ptr<SunburstNode> GetRootNode() const { return rootNode; }
        void SetRootNode(std::shared_ptr<SunburstNode> root);

        // Builds/extends the hierarchy along `path` and sets the value of the
        // final node. Missing intermediate nodes are created on the fly.
        // Returns the (leaf) node at the end of the path.
        std::shared_ptr<SunburstNode> AddNode(const std::vector<std::string>& path,
                                              double value,
                                              const Color& color = Colors::Transparent);
        void ClearData();

        // ===== Geometry =====
        void SetStartAngle(float degrees);            // default -90 (12 o'clock)
        float GetStartAngle() const { return startAngleDeg; }
        void SetSweepAngle(float degrees);            // 0..360, default 360
        float GetSweepAngle() const { return sweepAngleDeg; }
        void SetInnerRadiusFraction(float fraction);  // center hole, 0..0.9
        float GetInnerRadiusFraction() const { return innerRadiusFraction; }
        void SetRingSpacing(float pixels);            // radial gap between rings
        float GetRingSpacing() const { return ringSpacing; }
        void SetSegmentSpacingAngle(float degrees);   // angular gap between segments
        float GetSegmentSpacingAngle() const { return segmentSpacingDeg; }
        void SetMaxVisibleDepth(int depth);           // 0 = show all levels
        int GetMaxVisibleDepth() const { return maxVisibleDepth; }

        // ===== Colors & borders =====
        void SetColorPalette(const std::vector<Color>& palette); // per top branch
        const std::vector<Color>& GetColorPalette() const { return colorPalette; }
        void SetShadeMode(SunburstShadeMode mode);
        SunburstShadeMode GetShadeMode() const { return shadeMode; }
        void SetShadeStep(float step);                // 0..1 per depth level
        void SetBorderColor(const Color& c);
        void SetBorderWidth(float w);

        // ===== Center =====
        void SetCenterText(const std::string& text);  // empty = automatic
        void SetShowCenterTotal(bool show);           // append formatted total
        void SetCenterTextColor(const Color& c);
        void SetCenterFont(const std::string& family, float size, FontWeight weight);

        // ===== Labels =====
        void SetLabelContent(SunburstLabelContent content);
        SunburstLabelContent GetLabelContent() const { return labelContent; }
        void SetLabelOrientation(SunburstLabelOrientation orientation);
        SunburstLabelOrientation GetLabelOrientation() const { return labelOrientation; }
        void SetLabelFont(const std::string& family, float size, FontWeight weight);
        void SetLabelColor(const Color& c);
        void SetMinLabelAngle(float degrees);         // hide labels on tiny segments
        void SetValueFormatter(std::function<std::string(double)> f);
        void SetPercentageFormatter(std::function<std::string(double pct)> f);

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool on);
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }
        void SetSunburstTooltipFormatter(std::function<std::string(const SunburstNode&)> f);

        // Drill-down: clicking a branch segment re-roots the chart on it;
        // clicking the center hole navigates one level back up.
        void SetDrillDownEnabled(bool on);
        bool GetDrillDownEnabled() const { return drillDownEnabled; }
        void DrillTo(const std::shared_ptr<SunburstNode>& node);
        void DrillUp();
        void ResetDrill();
        std::shared_ptr<SunburstNode> GetCurrentRoot() const;

        // Callbacks: node + depth (1 = innermost visible ring)
        std::function<void(const std::shared_ptr<SunburstNode>&, int)> onSegmentClick;
        std::function<void(const std::shared_ptr<SunburstNode>&, int)> onSegmentHover;
        std::function<void(const std::shared_ptr<SunburstNode>&)> onDrillChange;

        // ===== Base overrides =====
        // Overridden because the sunburst keeps its own hierarchical data and
        // must render without an IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Data -----
        std::shared_ptr<SunburstNode> rootNode;
        std::weak_ptr<SunburstNode> drillRoot;        // empty = rootNode

        // ----- Geometry configuration -----
        float startAngleDeg = -90.0f;
        float sweepAngleDeg = 360.0f;
        float innerRadiusFraction = 0.25f;
        float ringSpacing = 1.0f;
        float segmentSpacingDeg = 0.4f;
        int maxVisibleDepth = 0;

        // ----- Style -----
        std::vector<Color> colorPalette;
        SunburstShadeMode shadeMode = SunburstShadeMode::LightenByDepth;
        float shadeStep = 0.14f;
        Color borderColor = Colors::White;
        float borderWidth = 1.5f;

        std::string centerText;
        bool showCenterTotal = true;
        Color centerTextColor = Color(60, 60, 60, 255);
        std::string centerFontFamily = "Arial";
        float centerFontSize = 13.0f;
        FontWeight centerFontWeight = FontWeight::Bold;

        SunburstLabelContent labelContent = SunburstLabelContent::Name;
        SunburstLabelOrientation labelOrientation = SunburstLabelOrientation::Auto;
        std::string labelFontFamily = "Arial";
        float labelFontSize = 10.0f;
        FontWeight labelFontWeight = FontWeight::Normal;
        Color labelColor = Color(40, 40, 40, 255);
        float minLabelAngleDeg = 4.0f;
        std::function<std::string(double)> valueFormatter;
        std::function<std::string(double)> percentFormatter;
        std::function<std::string(const SunburstNode&)> sunburstTooltipFormatter;

        bool hoverHighlightEnabled = true;
        bool drillDownEnabled = false;

        // ----- Cached layout -----
        struct Segment {
            std::shared_ptr<SunburstNode> node;
            int depth = 1;              // 1 = innermost visible ring
            double startAngle = 0.0;    // radians
            double endAngle = 0.0;      // radians
            double value = 0.0;         // aggregated weight
            double percentage = 0.0;    // of the current root total
            Color color;
        };

        std::vector<Segment> segments;
        bool segmentsValid = false;
        int cachedVisibleDepth = 0;
        double cachedTotal = 0.0;
        Point2Dd cachedCenter;
        float cachedInnerRadius = 0.0f;
        float cachedOuterRadius = 0.0f;
        float cachedRingWidth = 0.0f;

        size_t hoveredSegment = SIZE_MAX;

        // ----- Layout helpers -----
        void InvalidateSegments();
        void RebuildSegments();
        void LayoutNode(const std::shared_ptr<SunburstNode>& node, int depth,
                        double a0, double a1, const Color& branchColor);
        void ComputeRadii();
        Color ResolveBranchColor(size_t branchIndex,
                                 const std::shared_ptr<SunburstNode>& node) const;
        Color ShadeForDepth(const Color& base, int depth) const;

        // ----- Render helpers -----
        void DrawSegment(IRenderContext* ctx, const Segment& seg, bool highlighted);
        void RenderSegmentLabels(IRenderContext* ctx);
        void RenderCenter(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);
        std::string BuildLabelText(const Segment& seg) const;
        std::string FormatValue(double v) const;
        std::string FormatPercent(double pct) const;
        std::string BuildTooltipText(const Segment& seg) const;

        // ----- Hit testing -----
        size_t HitTestSegment(const Point2Dd& localPos) const;
        bool HitTestCenter(const Point2Dd& localPos) const;
        bool HandleClick(const UCEvent& event);

        static std::vector<Color> DefaultPalette();
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasSunburstChart> CreateSunburstChart(
        const std::string& id, int x, int y, int w, int h);

    // Convenience: builds a two-level sunburst out of category -> (label, value)
    // pairs, e.g. continent -> countries.
    std::shared_ptr<UltraCanvasSunburstChart> CreateSunburstChartFromGroups(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string,
              std::vector<std::pair<std::string, double>>>>& groupedData);

} // namespace UltraCanvas
