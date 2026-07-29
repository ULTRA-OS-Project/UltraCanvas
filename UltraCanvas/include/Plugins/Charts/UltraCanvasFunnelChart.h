// include/Plugins/Charts/UltraCanvasFunnelChart.h
// Funnel chart: a staged process whose value shrinks from one step to the next
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include "UltraCanvasTooltipManager.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// FUNNEL DATA
// =============================================================================

// One slice of a stage, used to break a stage down by channel, region, product
// or whatever else splits the population arriving at that step.
    struct FunnelSubSegment {
        std::string label;
        double value;
        Color color;    // Transparent = shade the stage colour by position

        FunnelSubSegment(const std::string& lbl = "", double val = 0.0,
                         const Color& col = Colors::Transparent)
                : label(lbl), value(val), color(col) {}
    };

    struct FunnelStage : public ChartDataPoint {
        std::string stageLabel;      // "Viewed", "Add to Cart", ...
        double stageValue;           // Population reaching this stage
        std::string description;     // Long-form annotation drawn beside the stage
        std::string badgeText;       // Short marker drawn outside the stage, e.g. "01"
        Color stageColor;            // Transparent = take the colour from the colour mode
        double targetValue;          // <= 0 disables the target overlay for this stage
        // Optional custom tooltip replacing the generated one. Tooltips are rendered
        // as Pango markup: the generated text escapes &, < and > for you, but text
        // supplied here is passed through verbatim so it can carry markup.
        std::string tooltipText;
        std::vector<FunnelSubSegment> segments;   // Empty = solid stage

        FunnelStage(const std::string& label = "", double value = 0.0)
                : ChartDataPoint(0.0, value, 0.0, label, value),
                  stageLabel(label), stageValue(value),
                  stageColor(Colors::Transparent), targetValue(0.0) {}

        bool HasSegments() const { return !segments.empty(); }

        // Total of the sub-segments, which need not add up to stageValue
        double GetSegmentTotal() const {
            double total = 0.0;
            for (const auto& segment : segments) total += segment.value;
            return total;
        }
    };

// Everything the chart derives about one stage from its neighbours. Computed on
// demand rather than stored, so editing a value can never leave it stale.
    struct FunnelStageMetrics {
        double value = 0.0;
        double percentOfFirst = 100.0;      // Share of the stage at the top of the funnel
        double percentOfPrevious = 100.0;   // Conversion from the stage above
        double percentOfTotal = 0.0;        // Share of every stage added together
        double dropOff = 0.0;               // Absolute loss since the stage above
        double dropOffPercent = 0.0;        // 100 - percentOfPrevious
    };

// =============================================================================
// FUNNEL DATA SOURCE
// =============================================================================

    class FunnelDataSource : public IChartDataSource {
    private:
        std::vector<FunnelStage> stages;

    public:
        // ===== IChartDataSource =====
        size_t GetPointCount() const override { return stages.size(); }

        ChartDataPoint GetPoint(size_t index) override {
            if (index < stages.size()) return stages[index];
            return ChartDataPoint(0, 0);
        }

        bool SupportsStreaming() const override { return false; }

        // CSV layout: stageLabel,value[,description]
        void LoadFromCSV(const std::string& filePath) override;

        // Each point becomes one stage, using its label and y value
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {
            stages.clear();
            stages.reserve(data.size());
            for (const auto& point : data) {
                stages.emplace_back(point.label, point.y);
            }
        }

        // ===== FUNNEL SPECIFIC =====
        void AddStage(const FunnelStage& stage) { stages.push_back(stage); }

        void AddStage(const std::string& label, double value,
                      const std::string& description = "") {
            FunnelStage stage(label, value);
            stage.description = description;
            stages.push_back(stage);
        }

        // Reference access - the chart renders straight from the vector, no copies
        const FunnelStage& GetStage(size_t index) const {
            static const FunnelStage empty;
            if (index < stages.size()) return stages[index];
            return empty;
        }

        FunnelStage* GetMutableStage(size_t index) {
            if (index < stages.size()) return &stages[index];
            return nullptr;
        }

        const std::vector<FunnelStage>& GetStages() const { return stages; }

        void Clear() { stages.clear(); }

        void LoadStages(const std::vector<FunnelStage>& data) { stages = data; }

        double GetMaxValue() const {
            double maxValue = 0.0;
            for (const auto& stage : stages) maxValue = std::max(maxValue, stage.stageValue);
            return maxValue;
        }

        double GetTotalValue() const {
            double total = 0.0;
            for (const auto& stage : stages) total += stage.stageValue;
            return total;
        }

        // Derived figures for one stage. Returns zeroed metrics for a bad index.
        FunnelStageMetrics GetMetrics(size_t index) const;
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================
// X11 pollutes the global namespace with None, Above, Below, Always and friends,
// so every enumerator below carries a descriptive suffix.

    enum class FunnelShapeMode {
        SegmentedBars,      // Detached bars joined by tapering connector necks
        ContinuousFunnel,   // One solid funnel, slabs touching edge to edge
        CardRows,           // Rounded row cards with the wedge running through them
        SkewedInfographic,  // Sheared presentation slabs with badges and callouts
        BarStyleFunnel      // Plain diminishing bars - the accessible variant
    };

    enum class FunnelOrientation {
        VerticalFunnel,     // Flows top to bottom
        HorizontalFunnel    // Flows left to right
    };

    enum class FunnelScaleMode {
        WidthProportional,  // Stage breadth tracks the value
        HeightProportional, // Stage thickness tracks the value, breadth tapers evenly
        AreaProportional,   // Slab area tracks the value - the honest funnel
        EqualStages         // Every stage the same size, a pure pipeline diagram
    };

    enum class FunnelTipMode {
        PointTip,           // Funnel closes to a point past the last stage
        FlatNeck,           // Funnel closes to a neck of SetNeckRatio() width
        MinWidthTip         // Last stage keeps its own breadth, no closing taper
    };

    enum class FunnelAlignment {
        CenterAligned,
        StartAligned,       // Left in vertical mode, top in horizontal mode
        EndAligned
    };

    enum class FunnelColorMode {
        SequentialRamp,     // Interpolate between two colours down the funnel
        CategoricalPalette, // Cycle a palette, one colour per stage
        SingleHue,          // One colour throughout
        PerStageOverride,   // Only what each stage sets, base colour otherwise
        ThresholdColor      // Colour by conversion against the configured thresholds
    };

    enum class FunnelStageLabelPlacement {
        OutsideStartLabels, // Column before the funnel
        OutsideEndLabels,   // Column after the funnel
        InsideStageLabels,
        LeaderLineLabels,   // Outside, joined to the stage by a leader line
        HideStageLabels
    };

    enum class FunnelValueLabelPlacement {
        InsideValueLabels,
        OutsideStartValues,
        OutsideEndValues,
        HideValueLabels
    };

    enum class FunnelPercentPlacement {
        OutsideEndPercent,  // Its own column after the funnel
        OutsideStartPercent,
        InsideStagePercent,
        HidePercentColumn
    };

    enum class FunnelLegendPosition {
        LegendTopLeft,
        LegendTopCenter,
        LegendTopRight,
        LegendBottomLeft,
        LegendBottomCenter,
        LegendBottomRight
    };

    enum class FunnelSortOrder {
        InsertionOrder,
        ValueDescending,
        ValueAscending,
        LabelAscending,
        LabelDescending
    };

    enum class FunnelValuePolicy {
        AllowIncrease,      // Draw whatever was given, even if a stage grows
        ClampToPrevious,    // A stage never draws wider than the one above it
        AutoSortDescending  // Reorder so the funnel always narrows
    };

// =============================================================================
// CACHED GEOMETRY
// =============================================================================

// One entry per drawn stage, rebuilt with the rendering cache. Rendering, hit
// testing and label placement all read from here so they can never disagree.
    struct FunnelStageGeometry {
        size_t dataIndex = 0;           // Index into the data source, unaffected by sorting
        std::vector<Point2Dd> polygon;  // Slab outline in element coordinates
        double bandStart = 0.0;         // Along the flow axis
        double bandEnd = 0.0;
        double halfExtentStart = 0.0;   // Across the flow axis, at bandStart
        double halfExtentEnd = 0.0;     // ... and at bandEnd
        Point2Dd center;                // Slab centroid, where inside labels land
        Color color;
        FunnelStageMetrics metrics;

        // Neck joining this stage to the next one - empty for the last stage and
        // for shape modes whose slabs already touch.
        std::vector<Point2Dd> connectorPolygon;
        Point2Dd connectorCenter;
    };

// =============================================================================
// FUNNEL CHART ELEMENT
// =============================================================================

    class UltraCanvasFunnelChart : public UltraCanvasChartElementBase {
    private:
        std::shared_ptr<FunnelDataSource> funnelDataSource;

        // Stage order actually drawn (indices into the data source)
        std::vector<size_t> displayOrder;
        FunnelSortOrder sortOrder = FunnelSortOrder::InsertionOrder;
        FunnelValuePolicy valuePolicy = FunnelValuePolicy::AllowIncrease;
        bool orderValid = false;

        // Cached layout
        std::vector<FunnelStageGeometry> geometry;

        // Shape
        FunnelShapeMode shapeMode = FunnelShapeMode::SegmentedBars;
        FunnelOrientation orientation = FunnelOrientation::VerticalFunnel;
        FunnelScaleMode scaleMode = FunnelScaleMode::WidthProportional;
        FunnelTipMode tipMode = FunnelTipMode::MinWidthTip;
        FunnelAlignment alignment = FunnelAlignment::CenterAligned;

        double neckRatio = 0.15;            // FlatNeck width as a share of the widest stage
        double minStageExtent = 12.0;       // Smallest drawn breadth in pixels
        double stageGap = 22.0;             // Space between slabs, where the necks live
        double stageThickness = 46.0;       // Used when autoFitStages is off
        bool autoFitStages = true;
        double cornerRadius = 0.0;
        double slabSkew = 0.0;              // Shear applied in SkewedInfographic mode
        bool showTipTriangle = false;       // Residual triangle past the last stage

        // Colour
        FunnelColorMode colorMode = FunnelColorMode::SequentialRamp;
        Color rampStartColor = Color(158, 202, 232, 255);
        Color rampEndColor = Color(21, 74, 120, 255);
        Color baseColor = Color(66, 133, 244, 255);
        std::vector<Color> palette;
        bool useGradientFill = false;
        double gradientLightening = 0.22;   // How much the gradient brightens each slab
        bool showStageBorder = false;
        Color stageBorderColor = Color(255, 255, 255, 255);
        double stageBorderWidth = 1.0;
        bool dimUnhoveredStages = false;

        // Threshold colouring (FunnelColorMode::ThresholdColor)
        double goodConversionThreshold = 70.0;
        double warnConversionThreshold = 40.0;
        Color goodConversionColor = Color(76, 175, 110, 255);
        Color warnConversionColor = Color(240, 180, 60, 255);
        Color poorConversionColor = Color(215, 85, 75, 255);

        // Labels
        FunnelStageLabelPlacement stageLabelPlacement = FunnelStageLabelPlacement::OutsideStartLabels;
        FunnelValueLabelPlacement funnelValueLabelPlacement = FunnelValueLabelPlacement::InsideValueLabels;
        FunnelPercentPlacement percentPlacement = FunnelPercentPlacement::OutsideEndPercent;

        double stageLabelColumnWidth = 110.0;
        double percentColumnWidth = 76.0;
        double descriptionColumnWidth = 0.0;   // 0 = no description column
        double stageLabelFontSize = 11.0;
        Color stageLabelColor = Color(60, 60, 60, 255);
        Color titleColor = Colors::Transparent;   // Transparent = contrast with the background
        double descriptionFontSize = 9.5;
        Color descriptionColor = Color(120, 120, 128, 255);
        bool showDescriptionLeaderLines = true;

        std::string valueFormat = "%.0f";
        std::string percentFormat = "%.2f%%";
        bool abbreviateValues = false;         // 5680 -> "5.68K"
        Color insideLabelColor = Color(255, 255, 255, 255);
        bool autoContrastInsideLabels = true;  // Flip to dark text on pale slabs

        // Percentages
        bool showPercentOfFirst = true;
        bool showPercentOfPrevious = false;
        bool showPercentOfTotal = false;
        bool showDropOffLabels = false;
        bool showConversionOnConnector = true;
        double connectorLabelFontSize = 9.0;
        Color connectorColor = Color(222, 233, 243, 255);
        Color connectorLabelColor = Color(90, 100, 110, 255);

        // Badges and leader lines
        bool showBadges = false;
        double badgeFontSize = 22.0;
        Color badgeColor = Color(150, 150, 160, 255);
        Color leaderLineColor = Color(150, 150, 160, 255);

        // Terminal callout - the circled outcome past the tip of the funnel
        bool showTerminalCallout = false;
        double terminalCalloutRadius = 22.0;
        Color terminalCalloutColor = Color(120, 120, 130, 255);

        // Target overlay and bottleneck marker
        bool showTargetOverlay = false;
        Color targetOverlayColor = Color(120, 120, 130, 160);
        bool highlightBottleneck = false;
        Color bottleneckColor = Color(215, 85, 75, 255);

        // Legend
        bool showLegend = false;
        FunnelLegendPosition legendPosition = FunnelLegendPosition::LegendTopCenter;
        double legendFontSize = 10.0;

        // Card rows (FunnelShapeMode::CardRows)
        Color cardFillColor = Color(255, 255, 255, 255);
        Color cardBorderColor = Color(214, 222, 233, 255);
        double cardCornerRadius = 18.0;

        // Cached scalars recomputed with the rendering cache
        double maxStageValue = 0.0;
        double totalStageValue = 0.0;
        double maxHalfExtent = 1.0;
        int bottleneckIndex = -1;   // Index into displayOrder

        // Interaction state
        int hoveredStageIndex = -1;     // Index into displayOrder
        int hoveredConnectorIndex = -1; // Index into displayOrder of the stage above

    public:
        UltraCanvasFunnelChart(const std::string& id, int x, int y, int width, int height);

        // ===== DATA =====
        void SetFunnelDataSource(std::shared_ptr<FunnelDataSource> data);
        std::shared_ptr<FunnelDataSource> GetFunnelDataSource() const { return funnelDataSource; }

        void AddStage(const std::string& label, double value, const std::string& description = "");
        void AddStage(const FunnelStage& stage);
        void ClearData();

        size_t GetStageCount() const {
            return funnelDataSource ? funnelDataSource->GetPointCount() : 0;
        }

        // Metrics for a stage, addressed by its index in the data source
        FunnelStageMetrics GetStageMetrics(size_t dataIndex) const;

        // Conversion from the first stage to the last, as a percentage
        double GetOverallConversion() const;

        // Index of the stage with the worst conversion from its predecessor, or
        // SIZE_MAX when there are fewer than two stages.
        size_t GetBottleneckStage() const;

        void SetSortOrder(FunnelSortOrder order);
        FunnelSortOrder GetSortOrder() const { return sortOrder; }

        void SetValuePolicy(FunnelValuePolicy policy);
        FunnelValuePolicy GetValuePolicy() const { return valuePolicy; }

        // ===== SHAPE =====
        void SetShapeMode(FunnelShapeMode mode);
        FunnelShapeMode GetShapeMode() const { return shapeMode; }

        void SetOrientation(FunnelOrientation value);
        FunnelOrientation GetOrientation() const { return orientation; }

        void SetScaleMode(FunnelScaleMode mode);
        FunnelScaleMode GetScaleMode() const { return scaleMode; }

        void SetTipMode(FunnelTipMode mode);
        FunnelTipMode GetTipMode() const { return tipMode; }

        void SetAlignment(FunnelAlignment value);
        FunnelAlignment GetAlignment() const { return alignment; }

        void SetNeckRatio(double ratio);
        double GetNeckRatio() const { return neckRatio; }

        void SetMinStageExtent(double extent);
        void SetStageGap(double gap);
        double GetStageGap() const { return stageGap; }

        void SetStageThickness(double thickness);
        void SetAutoFitStages(bool autoFit);
        bool GetAutoFitStages() const { return autoFitStages; }

        void SetCornerRadius(double radius);
        void SetSlabSkew(double skew);
        void SetShowTipTriangle(bool show);

        // Space needed to draw every stage at the configured thickness
        double GetPreferredThickness() const;

        // ===== COLOUR =====
        void SetColorMode(FunnelColorMode mode);
        FunnelColorMode GetColorMode() const { return colorMode; }

        void SetColorRamp(const Color& startColor, const Color& endColor);
        void SetBaseColor(const Color& color);
        void SetPalette(const std::vector<Color>& colors);
        void SetUseGradientFill(bool use, double lightening = 0.22);
        void SetShowStageBorder(bool show, const Color& color = Color(255, 255, 255, 255),
                                double width = 1.0);
        void SetDimUnhoveredStages(bool dim);

        void SetConversionThresholds(double goodPercent, double warnPercent);
        void SetConversionColors(const Color& good, const Color& warn, const Color& poor);

        // ===== LABELS =====
        void SetStageLabelPlacement(FunnelStageLabelPlacement placement);
        void SetFunnelValueLabelPlacement(FunnelValueLabelPlacement placement);
        void SetPercentPlacement(FunnelPercentPlacement placement);

        void SetStageLabelColumnWidth(double width);
        void SetPercentColumnWidth(double width);
        void SetDescriptionColumnWidth(double width);
        void SetStageLabelFontSize(double size);
        void SetStageLabelColor(const Color& color);
        // Transparent picks light or dark text from the chart background
        void SetTitleColor(const Color& color);
        void SetDescriptionStyle(double fontSize, const Color& color);
        void SetShowDescriptionLeaderLines(bool show);

        void SetValueFormat(const std::string& format);
        const std::string& GetValueFormat() const { return valueFormat; }
        void SetPercentFormat(const std::string& format);
        void SetAbbreviateValues(bool abbreviate);
        void SetInsideLabelColor(const Color& color);
        void SetAutoContrastInsideLabels(bool autoContrast);

        void SetShowPercentOfFirst(bool show);
        void SetShowPercentOfPrevious(bool show);
        void SetShowPercentOfTotal(bool show);
        void SetShowDropOffLabels(bool show);
        void SetShowConversionOnConnector(bool show);
        void SetConnectorColors(const Color& fill, const Color& label);

        void SetShowBadges(bool show, double fontSize = 22.0);
        void SetBadgeColor(const Color& color);
        void SetLeaderLineColor(const Color& color);

        void SetShowTerminalCallout(bool show, double radius = 22.0);
        void SetTerminalCalloutColor(const Color& color);

        // ===== ANALYSIS OVERLAYS =====
        void SetShowTargetOverlay(bool show);
        void SetTargetOverlayColor(const Color& color);
        void SetHighlightBottleneck(bool highlight);
        void SetBottleneckColor(const Color& color);

        // ===== LEGEND =====
        void SetShowLegend(bool show);
        bool GetShowLegend() const { return showLegend; }
        void SetLegendPosition(FunnelLegendPosition position);
        void SetLegendFontSize(double size);

        // ===== CARD ROWS =====
        void SetCardStyle(const Color& fill, const Color& border, double radius);

        // ===== ANIMATION =====
        void SetAnimationEnabled(bool enable);
        void SetAnimationDuration(float seconds);
        void RestartAnimation();

        // ===== CALLBACKS =====
        // Indices are into the data source, so sorting never shifts them
        std::function<void(size_t stageIndex)> onStageClick;
        std::function<void(size_t stageIndex)> onStageHover;
        // upperStageIndex is the stage above the neck that was clicked
        std::function<void(size_t upperStageIndex)> onConnectorClick;

        // ===== UIELEMENT / CHART BASE OVERRIDES =====
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;
        ChartDataBounds CalculateDataBounds() override;
        void UpdateRenderingCache() override;
        void RenderCommonBackground(IRenderContext* ctx) override;

    private:
        // ===== LAYOUT =====
        void RebuildDisplayOrder();
        void RebuildGeometry();
        std::vector<double> ComputeDrawValues() const;
        std::vector<double> ComputeBandThicknesses(const std::vector<double>& values,
                                                   double available) const;
        double HalfExtentForValue(double value) const;
        double TipHalfExtent(double lastStageHalfExtent) const;
        void FillPolygon(std::vector<Point2Dd>& out, double bandStart, double bandEnd,
                         double halfStart, double halfEnd, double skew) const;

        // Flow runs along the funnel, cross runs across it. Both are element
        // coordinates; cross is measured from the funnel's centre line.
        Point2Dd MapFlowCross(double flow, double cross) const;
        bool IsVertical() const { return orientation == FunnelOrientation::VerticalFunnel; }
        double GetFlowStart() const;
        double GetFlowLength() const;
        double GetCrossCenter() const;
        double GetCrossHalfSpan() const;
        void CrossRangeForExtent(double halfExtent, double& low, double& high) const;

        double GetMarginStart() const;    // Before the funnel along the cross axis
        double GetMarginEnd() const;      // After it
        double GetMarginFlowStart() const;
        double GetMarginFlowEnd() const;
        bool OutsideTextTakesStartColumn() const;
        bool OutsideTextTakesEndColumn() const;
        bool PercentTakesEndColumn() const;
        bool PercentTakesStartColumn() const;

        // ===== COLOUR =====
        Color ResolveStageColor(size_t displayIndex, size_t dataIndex,
                                const FunnelStageMetrics& metrics) const;
        Color PickReadableTextColor(const Color& background) const;

        // ===== RENDERING =====
        void RenderCardBackgrounds(IRenderContext* ctx);
        void RenderConnectors(IRenderContext* ctx);
        void RenderStages(IRenderContext* ctx);
        void RenderStageSlab(IRenderContext* ctx, const FunnelStageGeometry& geo,
                             const FunnelStage& stage, float progress);
        void RenderSubSegments(IRenderContext* ctx, const FunnelStageGeometry& geo,
                               const FunnelStage& stage, float progress);
        void RenderTipTriangle(IRenderContext* ctx, float progress);
        void RenderTargetOverlay(IRenderContext* ctx);
        void RenderStageLabels(IRenderContext* ctx);
        void DrawOutsideColumnText(IRenderContext* ctx, const FunnelStageGeometry& geo,
                                   const std::string& text, bool atStart, bool withLeaderLine);
        void RenderInsideLabels(IRenderContext* ctx);
        void RenderPercentColumn(IRenderContext* ctx);
        void RenderDescriptions(IRenderContext* ctx);
        void RenderBadges(IRenderContext* ctx);
        void RenderTerminalCallout(IRenderContext* ctx);
        void RenderLegend(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);

        void DrawPolygon(IRenderContext* ctx, const std::vector<Point2Dd>& points,
                         const Color& fill, const Color& border, double borderWidth) const;
        void DrawGradientPolygon(IRenderContext* ctx, const std::vector<Point2Dd>& points,
                                 const Color& fill) const;
        std::vector<Point2Dd> ScalePolygon(const std::vector<Point2Dd>& points,
                                           float progress) const;
        float GetStageProgress(size_t displayIndex) const;

        // ===== TEXT =====
        std::string FormatStageValue(double value) const;
        std::string FormatPercent(double percent) const;
        std::string TruncateToWidth(IRenderContext* ctx, const std::string& text,
                                    double maxWidth) const;
        void DrawCenteredText(IRenderContext* ctx, const std::string& text,
                              const Point2Dd& center) const;

        // ===== HIT TESTING =====
        int FindStageAt(const Point2Di& mousePos) const;
        int FindConnectorAt(const Point2Di& mousePos) const;
        static bool PointInPolygon(const std::vector<Point2Dd>& polygon, double x, double y);

        std::string GenerateFunnelTooltip(const FunnelStage& stage,
                                          const FunnelStageMetrics& metrics) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<FunnelDataSource> CreateFunnelDataSource() {
        return std::make_shared<FunnelDataSource>();
    }

    inline std::shared_ptr<UltraCanvasFunnelChart> CreateFunnelChart(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasFunnelChart>(id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasFunnelChart> CreateFunnelChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<FunnelDataSource> data, const std::string& title = "") {

        auto chart = CreateFunnelChart(id, x, y, width, height);
        chart->SetFunnelDataSource(data);
        if (!title.empty()) {
            chart->SetTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
