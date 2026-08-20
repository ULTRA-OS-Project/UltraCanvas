// include/Plugins/Charts/UltraCanvasPyramidChart.h
// Pyramid diagram: a static hierarchy whose levels are independent parts of a whole
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include "UltraCanvasTooltipManager.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// PYRAMID DATA
// =============================================================================

// One slice of a level, used to break it down by segment, region, product or
// whatever else divides the population sitting at that tier.
    struct PyramidSubSegment {
        std::string label;
        double value;
        Color color;    // Transparent = shade the level colour by position

        PyramidSubSegment(const std::string& lbl = "", double val = 0.0,
                          const Color& col = Colors::Transparent)
                : label(lbl), value(val), color(col) {}
    };

    struct PyramidLevel : public ChartDataPoint {
        std::string levelLabel;      // "Self-actualisation", "Unit tests", ...
        double levelValue;           // Size of this tier - optional, see SetValuesEnabled()
        std::string calloutTitle;    // Heading of the side callout; empty falls back to levelLabel
        std::string description;     // Body of the side callout
        std::string badgeText;       // Short marker, e.g. "01"; empty falls back to the position
        std::string iconPath;        // Drawn in the icon callout style and inside the band
        Color levelColor;            // Transparent = take the colour from the colour mode
        double targetValue;          // <= 0 disables the target overlay for this level
        // Optional custom tooltip replacing the generated one. Tooltips are rendered
        // as Pango markup: the generated text escapes &, < and > for you, but text
        // supplied here is passed through verbatim so it can carry markup.
        std::string tooltipText;
        std::vector<PyramidSubSegment> segments;   // Empty = solid level

        PyramidLevel(const std::string& label = "", double value = 0.0)
                : ChartDataPoint(0.0, value, 0.0, label, value),
                  levelLabel(label), levelValue(value),
                  levelColor(Colors::Transparent), targetValue(0.0) {}

        bool HasSegments() const { return !segments.empty(); }

        // Total of the sub-segments, which need not add up to levelValue
        double GetSegmentTotal() const {
            double total = 0.0;
            for (const auto& segment : segments) total += segment.value;
            return total;
        }
    };

// Everything the chart derives about one level from the rest of the pyramid.
// Computed on demand rather than stored, so editing a value can never leave it
// stale. A pyramid is a hierarchy, not a process, so none of these are
// conversions: they describe a part against the whole and against the tier
// underneath it.
    struct PyramidLevelMetrics {
        double value = 0.0;
        double percentOfTotal = 0.0;        // Share of every level added together
        double percentOfBase = 100.0;       // Share of the widest tier at the bottom
        double ratioToLevelBelow = 100.0;   // Size against the tier directly beneath
        double cumulativeFromBase = 100.0;  // Share of the total held by this level and everything below it
        double cumulativeFromApex = 0.0;    // ... and by this level and everything above it
        size_t levelFromBase = 0;           // 0 = the base
        size_t levelFromApex = 0;           // 0 = the apex
    };

// =============================================================================
// PYRAMID DATA SOURCE
// =============================================================================

    class PyramidDataSource : public IChartDataSource {
    private:
        std::vector<PyramidLevel> levels;

    public:
        // ===== IChartDataSource =====
        size_t GetPointCount() const override { return levels.size(); }

        ChartDataPoint GetPoint(size_t index) override {
            if (index < levels.size()) return levels[index];
            return ChartDataPoint(0, 0);
        }

        bool SupportsStreaming() const override { return false; }

        // CSV layout: levelLabel[,value][,description]
        void LoadFromCSV(const std::string& filePath) override;

        // Each point becomes one level, using its label and y value
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {
            levels.clear();
            levels.reserve(data.size());
            for (const auto& point : data) {
                levels.emplace_back(point.label, point.y);
            }
        }

        // ===== PYRAMID SPECIFIC =====
        void AddLevel(const PyramidLevel& level) { levels.push_back(level); }

        void AddLevel(const std::string& label, double value = 0.0,
                      const std::string& description = "") {
            PyramidLevel level(label, value);
            level.description = description;
            levels.push_back(level);
        }

        // Reference access - the chart renders straight from the vector, no copies
        const PyramidLevel& GetLevel(size_t index) const {
            static const PyramidLevel empty;
            if (index < levels.size()) return levels[index];
            return empty;
        }

        PyramidLevel* GetMutableLevel(size_t index) {
            if (index < levels.size()) return &levels[index];
            return nullptr;
        }

        const std::vector<PyramidLevel>& GetLevels() const { return levels; }

        void Clear() { levels.clear(); }

        void LoadLevels(const std::vector<PyramidLevel>& data) { levels = data; }

        double GetMaxValue() const {
            double maxValue = 0.0;
            for (const auto& level : levels) maxValue = std::max(maxValue, level.levelValue);
            return maxValue;
        }

        double GetTotalValue() const {
            double total = 0.0;
            for (const auto& level : levels) total += level.levelValue;
            return total;
        }

        // True when no level carries a value, which is the normal state of a
        // qualitative hierarchy such as Maslow's or a testing pyramid.
        bool IsValueless() const {
            for (const auto& level : levels) {
                if (level.levelValue != 0.0) return false;
            }
            return true;
        }
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================
// X11 pollutes the global namespace with None, Above, Below, Always and friends,
// so every enumerator below carries a descriptive suffix.

// Which end of the supplied vector is the apex. The chart always draws apex to
// base internally, so this only says how to read the data.
    enum class PyramidInputOrder {
        ApexFirst,          // levels[0] is the narrow tier at the top
        BaseFirst           // levels[0] is the widest tier at the bottom
    };

    enum class PyramidShapeMode {
        SegmentedPyramid,   // Trapezoid tiers separated by a gap - the default
        SolidPyramid,       // One triangle cut by level lines, tiers touching
        SteppedPyramid,     // Rounded slabs stacked into a ziggurat
        Pyramid3D,          // Extruded solid with a lit top face and a shaded side
        CardRows,           // Rounded row card per tier with the wedge running through it
        SkewedInfographic,  // Sheared presentation slabs, alternating direction
        ExplodedPyramid,    // Tiers offset sideways, each one pulled out of the stack
        BarStylePyramid     // Plain centred bars - the accessible variant
    };

    enum class PyramidDirection {
        ApexUp,             // Classic pyramid
        ApexDown,           // Inverted pyramid
        ApexLeft,           // Horizontal, narrow end on the left
        ApexRight           // Horizontal, narrow end on the right
    };

    enum class PyramidScaleMode {
        EqualLevels,        // Every tier the same height - the hierarchy diagram, the default
        HeightProportional, // Tier height tracks the value
        WidthProportional,  // Tier width tracks the value, heights stay equal
        AreaProportional,   // Drawn trapezoid area tracks the value - the honest pyramid
        VolumeProportional  // Solid-of-revolution volume tracks the value, for Pyramid3D
    };

    enum class PyramidApexMode {
        PointApex,          // The pyramid closes to a point
        FlatApex,           // It closes to a plateau of SetApexRatio() width
        MinWidthApex        // The top tier keeps a readable width of its own
    };

    enum class PyramidApexCap {
        ApexMergedWithTopLevel, // The top tier is the apex
        ApexSeparateCap,        // A small detached triangle sits above the top tier
        ApexHidden              // No cap and no closing taper
    };

    enum class PyramidAlignment {
        CenterAligned,
        StartAligned,       // Left in a vertical pyramid, top in a horizontal one
        EndAligned          // The half-pyramid look when combined with SetHalfPyramid()
    };

    enum class PyramidFillStyle {
        SolidFill,
        GradientFill,       // Lightened along the cross axis
        OutlineOnly,        // Stroked tiers with no fill
        OutlineWithTint     // Stroked tiers over a faint wash of the same colour
    };

    enum class PyramidColorMode {
        SequentialRamp,     // Interpolate between two colours down the pyramid
        CategoricalPalette, // Cycle a palette, one colour per level
        SingleHue,          // One colour throughout
        PerLevelOverride,   // Only what each level sets, base colour otherwise
        ThresholdColor      // Colour by share of the total against the configured thresholds
    };

    enum class PyramidPalettePreset {
        SpectrumPalette,    // The rainbow ramp of the classic infographic pyramid
        WarmPalette,
        CoolPalette,
        CorporatePalette,
        GrayscalePalette
    };

    enum class PyramidLevelLabelPlacement {
        InsideLevelLabels,  // On the tier itself
        OutsideStartLabels, // Column before the pyramid
        OutsideEndLabels,   // Column after it
        LeaderLineLabels,   // Outside, joined to the tier by a leader line
        HideLevelLabels
    };

    enum class PyramidValueLabelPlacement {
        InsideValueLabels,
        OutsideStartValues,
        OutsideEndValues,
        HideValueLabels
    };

    enum class PyramidPercentPlacement {
        InsideLevelPercent,
        OutsideStartPercent,
        OutsideEndPercent,
        HidePercentColumn
    };

    enum class PyramidInsideTextAlign {
        InsideTextCenter,
        InsideTextStart,    // Left in a vertical pyramid - the wrapped-paragraph look
        InsideTextEnd
    };

    enum class PyramidCalloutStyle {
        NoCallouts,
        PlainTextCallouts,      // Title and body, no decoration
        ColorRibbonCallouts,    // Filled banner in the level colour, notched towards the tier
        RoundedCardCallouts,    // Outlined card
        IconCardCallouts,       // Outlined card with a circular icon on its inner edge
        NumberedBlockCallouts   // Large ghost numeral behind a title and body
    };

    enum class PyramidCalloutSide {
        CalloutsRight,
        CalloutsLeft,
        CalloutsAlternating,    // Odd tiers one way, even tiers the other
        CalloutsBothSides       // Split the levels between the two columns
    };

    enum class PyramidCalloutLayout {
        AlignedToLevel,     // Each block centred on its tier, nudged apart on collision
        EvenlyStacked       // Blocks share the height evenly, whatever the tiers do
    };

    enum class PyramidConnectorStyle {
        NoConnector,
        StraightConnector,
        ElbowConnector,
        DottedConnector,
        NotchPointer        // A wedge on the tier edge instead of a line
    };

    enum class PyramidBadgeShape {
        NoBadge,
        CircleBadge,
        RoundedBadge,
        SquareBadge,
        PlainNumber         // Bare text, no chip behind it
    };

    enum class PyramidBadgePlacement {
        BadgeInsideLevel,   // On the tier, at its leading edge
        BadgeOnEdge,        // Straddling the tier's leading edge
        BadgeOutsideStart,  // Its own narrow column outside the pyramid
        BadgeOnLabelRow     // On the level label column instead of the pyramid
    };

// Kept for API stability; SetLegendPosition() maps each value onto the shared
// ChartLegendPosition (Left/Center/Right become Start/Center/End on that edge).
    enum class PyramidLegendPosition {
        LegendTopLeft,
        LegendTopCenter,
        LegendTopRight,
        LegendBottomLeft,
        LegendBottomCenter,
        LegendBottomRight
    };

// Every order is read along the drawn sequence, which runs apex to base
    enum class PyramidSortOrder {
        InsertionOrder,
        ValueDescending,    // Largest tier at the apex - a deliberately top-heavy shape
        ValueAscending,     // Largest tier at the base - the ordinary pyramid
        LabelAscending,
        LabelDescending
    };

    enum class PyramidValuePolicy {
        AllowIncrease,      // Draw whatever was given, even where a tier is wider than the one below
        ClampToBelow,       // A tier never draws wider than the tier beneath it
        AutoSortToWiden     // Reorder so the pyramid always widens downwards
    };

// =============================================================================
// CACHED GEOMETRY
// =============================================================================

// One entry per drawn level, rebuilt with the rendering cache. Rendering, hit
// testing, callout placement and label placement all read from here so they can
// never disagree.
    struct PyramidLevelGeometry {
        size_t dataIndex = 0;           // Index into the data source, unaffected by sorting
        std::vector<Point2Dd> polygon;  // Front face outline in element coordinates
        double bandStart = 0.0;         // Along the flow axis, measured from the apex
        double bandEnd = 0.0;
        double halfExtentStart = 0.0;   // Across the flow axis, at bandStart (nearer the apex)
        double halfExtentEnd = 0.0;     // ... and at bandEnd
        double crossOffset = 0.0;       // Sideways displacement in ExplodedPyramid mode
        Point2Dd center;                // Where inside labels land
        Color color;
        PyramidLevelMetrics metrics;

        // Extruded faces - empty outside Pyramid3D
        std::vector<Point2Dd> topFace;
        std::vector<Point2Dd> sideFace;

        // Callout block resolved by the layout pass. width/height of 0 means the
        // level has nothing to say and no block was reserved.
        Rect2Dd calloutRect;
        Point2Dd calloutAnchor;         // Point on the tier edge the connector leaves from
        bool calloutAtStartSide = false;
    };

// =============================================================================
// PYRAMID CHART ELEMENT
// =============================================================================

    class UltraCanvasPyramidChart : public UltraCanvasChartElementBase {
    private:
        std::shared_ptr<PyramidDataSource> pyramidDataSource;

        // Level order actually drawn, apex first (indices into the data source)
        std::vector<size_t> displayOrder;
        PyramidInputOrder inputOrder = PyramidInputOrder::ApexFirst;
        PyramidSortOrder sortOrder = PyramidSortOrder::InsertionOrder;
        PyramidValuePolicy valuePolicy = PyramidValuePolicy::AllowIncrease;
        bool orderValid = false;

        // Cached layout
        std::vector<PyramidLevelGeometry> geometry;
        std::vector<Point2Dd> apexCapPolygon;   // ApexSeparateCap only

        // Shape
        PyramidShapeMode shapeMode = PyramidShapeMode::SegmentedPyramid;
        PyramidDirection direction = PyramidDirection::ApexUp;
        PyramidScaleMode scaleMode = PyramidScaleMode::EqualLevels;
        PyramidApexMode apexMode = PyramidApexMode::PointApex;
        PyramidApexCap apexCap = PyramidApexCap::ApexMergedWithTopLevel;
        PyramidAlignment alignment = PyramidAlignment::CenterAligned;

        double apexRatio = 0.12;            // FlatApex width as a share of the base
        double minLevelExtent = 14.0;       // Smallest drawn breadth in pixels
        double levelGap = 6.0;              // Space between tiers
        double levelThickness = 52.0;       // Used when autoFitLevels is off
        bool autoFitLevels = true;
        double cornerRadius = 0.0;
        double slabSkew = 0.0;              // Shear applied in SkewedInfographic mode
        double explodeOffset = 18.0;        // Sideways step in ExplodedPyramid mode
        bool halfPyramid = false;           // One sloped side only
        double pyramidWidthRatio = 1.0;     // Share of the plot area the base spans
        double apexCapHeight = 34.0;
        bool showBasePlinth = false;
        Color basePlinthColor = Color(210, 216, 224, 255);

        // 3D extrusion
        double depth3D = 26.0;
        double angle3D = 32.0;              // Degrees above the horizontal
        double topFaceLighten = 0.26;
        double sideFaceDarken = 0.24;
        bool showTopFace = true;

        // Colour
        PyramidFillStyle fillStyle = PyramidFillStyle::SolidFill;
        PyramidColorMode colorMode = PyramidColorMode::CategoricalPalette;
        Color rampStartColor = Color(21, 74, 120, 255);     // Apex
        Color rampEndColor = Color(158, 202, 232, 255);     // Base
        Color baseColor = Color(66, 133, 244, 255);
        std::vector<Color> palette;
        double gradientLightening = 0.22;
        double tintStrength = 0.14;         // OutlineWithTint wash
        bool showLevelBorder = false;
        Color levelBorderColor = Color(255, 255, 255, 255);
        double levelBorderWidth = 1.0;
        double outlineWidth = 2.0;          // OutlineOnly / OutlineWithTint
        bool dashedOutline = false;
        bool dimUnhoveredLevels = false;

        // Threshold colouring (PyramidColorMode::ThresholdColor)
        double majorShareThreshold = 40.0;
        double minorShareThreshold = 15.0;
        Color majorShareColor = Color(76, 175, 110, 255);
        Color middleShareColor = Color(240, 180, 60, 255);
        Color minorShareColor = Color(215, 85, 75, 255);

        // Labels
        PyramidLevelLabelPlacement levelLabelPlacement = PyramidLevelLabelPlacement::InsideLevelLabels;
        PyramidValueLabelPlacement pyramidValueLabelPlacement = PyramidValueLabelPlacement::HideValueLabels;
        PyramidPercentPlacement percentPlacement = PyramidPercentPlacement::HidePercentColumn;
        PyramidInsideTextAlign insideTextAlign = PyramidInsideTextAlign::InsideTextCenter;

        double levelLabelColumnWidth = 130.0;
        double percentColumnWidth = 78.0;
        double levelLabelFontSize = 11.0;
        Color levelLabelColor = Color(60, 60, 66, 255);
        Color titleColor = Colors::Transparent;   // Transparent = contrast with the background
        std::string subtitle;
        double subtitleFontSize = 10.5;
        Color subtitleColor = Color(130, 134, 142, 255);
        bool showLevelSwatch = false;       // Colour dot beside each label column row
        bool showLevelListRules = false;    // Hairline under each label column row

        std::string valueFormat = "%.0f";
        std::string percentFormat = "%.1f%%";
        bool abbreviateValues = false;      // 5680 -> "5.68K"
        Color insideLabelColor = Color(255, 255, 255, 255);
        bool autoContrastInsideLabels = true;
        bool wrapInsideText = true;
        bool shrinkInsideText = true;       // Step the font down before dropping lines
        double minInsideFontSize = 7.5;
        bool showDescriptionInside = false; // Wrap the description into the tier itself

        // Percentages
        bool showPercentOfTotal = true;
        bool showPercentOfBase = false;
        bool showRatioToLevelBelow = false;
        bool showCumulativeFromBase = false;

        // Badges
        PyramidBadgeShape badgeShape = PyramidBadgeShape::NoBadge;
        PyramidBadgePlacement badgePlacement = PyramidBadgePlacement::BadgeOnEdge;
        double badgeRadius = 13.0;
        double badgeFontSize = 11.0;
        Color badgeColor = Colors::Transparent;      // Transparent = use the level colour
        Color badgeTextColor = Color(255, 255, 255, 255);

        // Callouts
        PyramidCalloutStyle calloutStyle = PyramidCalloutStyle::NoCallouts;
        PyramidCalloutSide calloutSide = PyramidCalloutSide::CalloutsRight;
        PyramidCalloutLayout calloutLayout = PyramidCalloutLayout::AlignedToLevel;
        PyramidConnectorStyle connectorStyle = PyramidConnectorStyle::StraightConnector;
        double calloutColumnWidth = 210.0;
        double calloutSpacing = 8.0;
        double calloutPadding = 10.0;
        double calloutCornerRadius = 8.0;
        double calloutTitleFontSize = 11.0;
        double calloutBodyFontSize = 9.5;
        Color calloutTitleColor = Color(45, 48, 56, 255);
        Color calloutBodyColor = Color(120, 124, 134, 255);
        Color calloutCardColor = Color(255, 255, 255, 255);
        Color calloutBorderColor = Colors::Transparent;   // Transparent = use the level colour
        Color connectorColor = Color(180, 186, 196, 255);
        double connectorWidth = 1.0;
        double ghostNumberFontSize = 26.0;
        double ghostNumberAlpha = 0.30;

        // Analysis overlays
        bool showTargetOverlay = false;
        Color targetOverlayColor = Color(120, 120, 130, 160);
        std::vector<double> referenceShape;      // Normalised shares, apex first
        Color referenceShapeColor = Color(150, 150, 160, 190);
        bool highlightTopHeavy = false;
        Color topHeavyColor = Color(215, 85, 75, 255);
        bool benchmarkEnabled = false;
        double benchmarkValue = 0.0;
        std::string benchmarkLabel;
        Color benchmarkColor = Color(215, 85, 75, 220);

        // Legend - the shared ChartLegend component; the public
        // PyramidLegendPosition API maps onto its ChartLegendPosition
        ChartLegend legend;
        double legendInset = 0.0;   // Measured height the legend consumes at its edge

        // Card rows (PyramidShapeMode::CardRows)
        Color cardFillColor = Color(255, 255, 255, 255);
        Color cardBorderColor = Color(214, 222, 233, 255);
        double cardCornerRadius = 16.0;

        // Cached scalars recomputed with the rendering cache
        double maxLevelValue = 0.0;
        double totalLevelValue = 0.0;
        double maxHalfExtent = 1.0;
        double apexHalfExtent = 0.0;
        bool valuesAvailable = false;   // False for a hierarchy with no numbers
        int topHeavyIndex = -1;         // Index into displayOrder

        // Interaction state
        int hoveredLevelIndex = -1;     // Index into displayOrder
        int selectedLevelIndex = -1;

    public:
        UltraCanvasPyramidChart(const std::string& id, int x, int y, int width, int height);

        // ===== DATA =====
        void SetPyramidDataSource(std::shared_ptr<PyramidDataSource> data);
        std::shared_ptr<PyramidDataSource> GetPyramidDataSource() const { return pyramidDataSource; }

        void AddLevel(const std::string& label, double value = 0.0,
                      const std::string& description = "");
        void AddLevel(const PyramidLevel& level);
        void ClearData();

        size_t GetLevelCount() const {
            return pyramidDataSource ? pyramidDataSource->GetPointCount() : 0;
        }

        // Metrics for a level, addressed by its index in the data source
        PyramidLevelMetrics GetLevelMetrics(size_t dataIndex) const;

        // Value of the base over the value of the apex, as a ratio. 0 when either
        // end is empty or the pyramid carries no values at all.
        double GetBaseToApexRatio() const;

        // First level, counting from the apex, that is wider than the tier beneath
        // it - the top-heavy anti-pattern. SIZE_MAX when the pyramid is well formed.
        size_t GetTopHeavyLevel() const;

        void SetInputOrder(PyramidInputOrder order);
        PyramidInputOrder GetInputOrder() const { return inputOrder; }

        void SetSortOrder(PyramidSortOrder order);
        PyramidSortOrder GetSortOrder() const { return sortOrder; }

        void SetValuePolicy(PyramidValuePolicy policy);
        PyramidValuePolicy GetValuePolicy() const { return valuePolicy; }

        // ===== SHAPE =====
        void SetShapeMode(PyramidShapeMode mode);
        PyramidShapeMode GetShapeMode() const { return shapeMode; }

        void SetDirection(PyramidDirection value);
        PyramidDirection GetDirection() const { return direction; }

        void SetScaleMode(PyramidScaleMode mode);
        PyramidScaleMode GetScaleMode() const { return scaleMode; }

        void SetApexMode(PyramidApexMode mode);
        PyramidApexMode GetApexMode() const { return apexMode; }

        void SetApexCap(PyramidApexCap cap, double capHeight = 34.0);
        PyramidApexCap GetApexCap() const { return apexCap; }

        void SetAlignment(PyramidAlignment value);
        PyramidAlignment GetAlignment() const { return alignment; }

        void SetApexRatio(double ratio);
        double GetApexRatio() const { return apexRatio; }

        void SetMinLevelExtent(double extent);
        void SetLevelGap(double gap);
        double GetLevelGap() const { return levelGap; }

        void SetLevelThickness(double thickness);
        void SetAutoFitLevels(bool autoFit);
        bool GetAutoFitLevels() const { return autoFitLevels; }

        void SetCornerRadius(double radius);
        void SetSlabSkew(double skew);
        void SetExplodeOffset(double offset);
        void SetHalfPyramid(bool half);
        bool GetHalfPyramid() const { return halfPyramid; }
        void SetPyramidWidthRatio(double ratio);
        void SetShowBasePlinth(bool show, const Color& color = Color(210, 216, 224, 255));

        // Space needed to draw every level at the configured thickness
        double GetPreferredThickness() const;

        // ===== 3D =====
        void SetDepth3D(double depth);
        double GetDepth3D() const { return depth3D; }
        void SetAngle3D(double degrees);
        void SetFaceShading(double topLighten, double sideDarken);
        void SetShowTopFace(bool show);

        // ===== COLOUR =====
        void SetFillStyle(PyramidFillStyle style);
        PyramidFillStyle GetFillStyle() const { return fillStyle; }

        void SetColorMode(PyramidColorMode mode);
        PyramidColorMode GetColorMode() const { return colorMode; }

        // The ramp runs apex to base, which is the direction a reader scans a pyramid
        void SetColorRamp(const Color& apexColor, const Color& baseColorValue);
        void SetBaseColor(const Color& color);
        void SetPalette(const std::vector<Color>& colors);
        void SetPalettePreset(PyramidPalettePreset preset);
        void SetGradientLightening(double amount);
        void SetTintStrength(double amount);
        void SetShowLevelBorder(bool show, const Color& color = Color(255, 255, 255, 255),
                                double width = 1.0);
        void SetOutlineStyle(double width, bool dashed = false);
        void SetDimUnhoveredLevels(bool dim);

        void SetShareThresholds(double majorPercent, double minorPercent);
        void SetShareColors(const Color& major, const Color& middle, const Color& minor);

        // ===== LABELS =====
        void SetLevelLabelPlacement(PyramidLevelLabelPlacement placement);
        void SetPyramidValueLabelPlacement(PyramidValueLabelPlacement placement);
        void SetPercentPlacement(PyramidPercentPlacement placement);
        void SetInsideTextAlign(PyramidInsideTextAlign align);

        void SetLevelLabelColumnWidth(double width);
        void SetPercentColumnWidth(double width);
        void SetLevelLabelFontSize(double size);
        void SetLevelLabelColor(const Color& color);
        // Transparent picks light or dark text from the chart background
        void SetTitleColor(const Color& color);
        void SetSubtitle(const std::string& text);
        const std::string& GetSubtitle() const { return subtitle; }
        void SetSubtitleStyle(double fontSize, const Color& color);
        void SetShowLevelSwatch(bool show);
        void SetShowLevelListRules(bool show);

        void SetValueFormat(const std::string& format);
        const std::string& GetValueFormat() const { return valueFormat; }
        void SetPercentFormat(const std::string& format);
        void SetAbbreviateValues(bool abbreviate);
        void SetInsideLabelColor(const Color& color);
        void SetAutoContrastInsideLabels(bool autoContrast);
        void SetWrapInsideText(bool wrap);
        void SetShrinkInsideText(bool shrink, double minimumFontSize = 7.5);
        void SetShowDescriptionInside(bool show);

        void SetShowPercentOfTotal(bool show);
        void SetShowPercentOfBase(bool show);
        void SetShowRatioToLevelBelow(bool show);
        void SetShowCumulativeFromBase(bool show);

        // ===== BADGES =====
        void SetBadgeShape(PyramidBadgeShape shape);
        void SetBadgePlacement(PyramidBadgePlacement placement);
        void SetBadgeStyle(double radius, double fontSize);
        // A transparent fill takes the level's own colour
        void SetBadgeColors(const Color& fill, const Color& text);

        // ===== CALLOUTS =====
        void SetCalloutStyle(PyramidCalloutStyle style);
        PyramidCalloutStyle GetCalloutStyle() const { return calloutStyle; }
        void SetCalloutSide(PyramidCalloutSide side);
        void SetCalloutLayout(PyramidCalloutLayout layout);
        void SetConnectorStyle(PyramidConnectorStyle style);
        void SetCalloutColumnWidth(double width);
        void SetCalloutSpacing(double spacing);
        void SetCalloutPadding(double padding);
        void SetCalloutCornerRadius(double radius);
        void SetCalloutFontSizes(double titleSize, double bodySize);
        void SetCalloutColors(const Color& title, const Color& body);
        // A transparent border takes the level's own colour
        void SetCalloutCardStyle(const Color& fill, const Color& border);
        void SetConnectorAppearance(const Color& color, double width);
        void SetGhostNumberStyle(double fontSize, double alpha);

        // ===== ANALYSIS OVERLAYS =====
        void SetShowTargetOverlay(bool show);
        void SetTargetOverlayColor(const Color& color);
        // Shares run apex to base and are normalised; empty clears the overlay
        void SetReferenceShape(const std::vector<double>& shares,
                               const Color& color = Color(150, 150, 160, 190));
        void ClearReferenceShape();
        void SetHighlightTopHeavy(bool highlight);
        void SetTopHeavyColor(const Color& color);
        // Draws a rule where the running total from the base reaches this value
        void SetBenchmarkValue(double value, const std::string& label = "");
        void ClearBenchmark();
        void SetBenchmarkColor(const Color& color);

        // ===== LEGEND =====
        void SetShowLegend(bool show);
        bool GetShowLegend() const { return legend.IsVisible(); }
        void SetLegendPosition(PyramidLegendPosition position);
        void SetLegendFontSize(double size);

        // ===== CARD ROWS =====
        void SetCardStyle(const Color& fill, const Color& border, double radius);

        // ===== SELECTION =====
        // Index into the data source; SIZE_MAX clears the selection
        void SetSelectedLevel(size_t dataIndex);
        void ClearSelection();
        size_t GetSelectedLevel() const;

        // ===== ANIMATION =====
        void SetAnimationEnabled(bool enable);
        void SetAnimationDuration(float seconds);
        void RestartAnimation();

        // ===== CALLBACKS =====
        // Indices are into the data source, so sorting never shifts them
        std::function<void(size_t levelIndex)> onLevelClick;
        std::function<void(size_t levelIndex)> onLevelHover;
        std::function<void(size_t levelIndex)> onCalloutClick;

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
        void LayoutCallouts(IRenderContext* ctx);
        std::vector<double> ComputeDrawValues() const;
        // Normalised cut positions along the flow axis, size n + 1, running 0 -> 1
        std::vector<double> ComputeCuts(const std::vector<double>& values,
                                        PyramidScaleMode mode) const;
        // True when the cut positions encode the values, which is what the reference
        // shape has to be compared against
        bool ScaleEncodesValue() const;
        double HalfExtentAtT(double t) const;
        double HalfExtentForValue(double value) const;

        // Flow runs from the apex to the base; cross runs across the pyramid,
        // measured from its centre line. Both are element coordinates.
        Point2Dd MapFlowCross(double flow, double cross) const;
        bool IsVerticalPyramid() const {
            return direction == PyramidDirection::ApexUp || direction == PyramidDirection::ApexDown;
        }
        bool IsReversedFlow() const {
            return direction == PyramidDirection::ApexDown || direction == PyramidDirection::ApexRight;
        }
        double GetFlowOrigin() const;
        double GetFlowLength() const;
        double GetCrossCenter() const;
        double GetCrossHalfSpan() const;
        void CrossRangeForExtent(double halfExtent, double offset, double& low, double& high) const;
        void BuildBandPolygon(std::vector<Point2Dd>& out, double bandStart, double bandEnd,
                              double halfStart, double halfEnd, double offset, double skew) const;
        Point2Dd Offset3D() const;

        bool LabelTakesStartColumn() const;
        bool LabelTakesEndColumn() const;
        bool PercentTakesStartColumn() const;
        bool PercentTakesEndColumn() const;
        bool CalloutsTakeStartColumn() const;
        bool CalloutsTakeEndColumn() const;
        bool HasAnyPercentChannel() const;
        bool CalloutAtStartSide(size_t displayIndex) const;
        double GetMarginStart() const;      // Before the pyramid along the cross axis
        double GetMarginEnd() const;        // After it
        double GetMarginFlowStart() const;
        double GetMarginFlowEnd() const;

        // ===== LEGEND (shared ChartLegend component) =====
        bool LegendAtTop() const;
        Rect2Dd LegendArea() const;
        void RebuildLegendEntries();
        void UpdateLegendLayout(IRenderContext* ctx);

        // ===== COLOUR =====
        Color ResolveLevelColor(size_t displayIndex, size_t dataIndex,
                                const PyramidLevelMetrics& metrics) const;
        Color PickReadableTextColor(const Color& background) const;
        Color ResolveBadgeFill(const PyramidLevelGeometry& geo) const;

        // ===== RENDERING =====
        void RenderCardBackgrounds(IRenderContext* ctx);
        void RenderBasePlinth(IRenderContext* ctx);
        void RenderLevels(IRenderContext* ctx);
        void RenderLevelSlab(IRenderContext* ctx, const PyramidLevelGeometry& geo,
                             const PyramidLevel& level, size_t displayIndex, float progress);
        void RenderSubSegments(IRenderContext* ctx, const PyramidLevelGeometry& geo,
                               const PyramidLevel& level, float progress);
        void RenderApexCap(IRenderContext* ctx, float progress);
        void RenderTargetOverlay(IRenderContext* ctx);
        void RenderReferenceShape(IRenderContext* ctx);
        void RenderBenchmark(IRenderContext* ctx);
        void RenderInsideLabels(IRenderContext* ctx);
        void RenderLevelLabelColumn(IRenderContext* ctx);
        void RenderPercentColumn(IRenderContext* ctx);
        void RenderBadges(IRenderContext* ctx);
        void RenderCallouts(IRenderContext* ctx);
        void RenderCalloutConnector(IRenderContext* ctx, const PyramidLevelGeometry& geo);
        void RenderTitle(IRenderContext* ctx);

        void DrawBadgeChip(IRenderContext* ctx, const Point2Dd& center, const std::string& text,
                           const Color& fill);
        void DrawPolygon(IRenderContext* ctx, const std::vector<Point2Dd>& points,
                         const Color& fill, const Color& border, double borderWidth) const;
        void DrawGradientPolygon(IRenderContext* ctx, const std::vector<Point2Dd>& points,
                                 const Color& fill) const;
        void FillBandByStyle(IRenderContext* ctx, const std::vector<Point2Dd>& points,
                             const Color& color) const;
        std::vector<Point2Dd> ScalePolygon(const std::vector<Point2Dd>& points,
                                           float progress) const;
        float GetLevelProgress(size_t displayIndex) const;

        // ===== TEXT =====
        std::string FormatLevelValue(double value) const;
        std::string FormatPercent(double percent) const;
        std::string BadgeTextFor(size_t displayIndex) const;
        std::string TruncateToWidth(IRenderContext* ctx, const std::string& text,
                                    double maxWidth) const;
        std::vector<std::string> WrapText(IRenderContext* ctx, const std::string& text,
                                          double maxWidth) const;
        void DrawCenteredText(IRenderContext* ctx, const std::string& text,
                              const Point2Dd& center) const;
        std::vector<std::string> BuildPercentLines(const PyramidLevelMetrics& metrics) const;

        // ===== HIT TESTING =====
        int FindLevelAt(const Point2Di& mousePos) const;
        int FindCalloutAt(const Point2Di& mousePos) const;
        static bool PointInPolygon(const std::vector<Point2Dd>& polygon, double x, double y);

        std::string GeneratePyramidTooltip(const PyramidLevel& level,
                                           const PyramidLevelMetrics& metrics) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<PyramidDataSource> CreatePyramidDataSource() {
        return std::make_shared<PyramidDataSource>();
    }

    inline std::shared_ptr<UltraCanvasPyramidChart> CreatePyramidChart(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasPyramidChart>(id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasPyramidChart> CreatePyramidChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<PyramidDataSource> data, const std::string& title = "") {

        auto chart = CreatePyramidChart(id, x, y, width, height);
        chart->SetPyramidDataSource(data);
        if (!title.empty()) {
            chart->SetTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
