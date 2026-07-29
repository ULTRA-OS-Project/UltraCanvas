// include/Plugins/Charts/UltraCanvasCircularInfoGraphic.h
// Multi-ring circular infographic: concentric rings of individually styled
// cells, with value visualisations, decorative rings and cross-ring connections.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasConnectionRenderer.h"
#include "UltraCanvasRenderContext.h"
#include <functional>
#include <string>
#include <vector>

// X11 defines `None` as a macro that clobbers our enumerators.
#ifdef None
#undef None
#endif

namespace UltraCanvas {

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class CircularTextStyle {
        Horizontal,   // Upright text centred in the cell
        Circular,     // Text follows the ring, glyph by glyph
        Radial,       // Text runs along the radius, flipped on the left half
        StarStyle     // Text parked outside the chart with a leader line
    };

    enum class ValueVisualizationType {
        None,
        Bars,           // Radial bar filling the cell from its inner edge
        CircularLine,   // Polyline across the ring linking each cell's value
        CircularBar     // Angular progress arc drawn across the cell
    };

    enum class DecorativeRingType {
        None,
        ColorHighlight, // Solid ring stroked at the band's mid radius
        Graphics,       // Repeated image marks around the band
        Pattern,        // Repeating tick pattern
        Gradient        // Radial colour ramp across the band
    };

    enum class ConnectionLineType {
        None,
        InsideCenter,   // Routed through the middle of the chart
        OutsideRing,    // Routed around the outside of the outermost ring
        DirectCurve     // Gently bowed curve straight between the two cells
    };

// =============================================================================
// COLOR SCALE
// =============================================================================

    struct ColorScale {
        std::vector<Color> colors;
        double minValue = 0.0;
        double maxValue = 100.0;
        // When false the scale bands rather than blends between stops.
        bool interpolate = true;

        Color GetColorForValue(double value) const;
    };

// =============================================================================
// CELL / RING MODEL
// =============================================================================

    struct CircularCell {
        std::string text;
        Color textColor       = Color(30, 30, 30, 255);
        Color backgroundColor = Color(255, 255, 255, 255);
        std::string backgroundImage;
        double value = 0.0;

        bool isEmpty       = false;  // Skipped when drawing and hit testing
        bool isTransparent = false;  // Drawn without a background fill
        // Take the background from the chart's ColorScale instead of
        // backgroundColor. Ignored when a backgroundImage is set.
        bool useColorScale = false;

        float rotation   = 0.0f;     // Extra rotation in degrees for the label
        bool  autoRotate = true;     // Align the label with the cell's mid-angle
        bool  autoScale  = true;     // Scale backgroundImage to fill the cell
        bool  maintainAspectRatio = true;

        std::string groupName;       // Cells sharing a name are outlined together
        std::string tooltip;         // Overrides the generated tooltip when set
    };

    struct CircularRing {
        std::vector<CircularCell> cells;

        CircularTextStyle textStyle          = CircularTextStyle::Horizontal;
        ValueVisualizationType valueType     = ValueVisualizationType::None;
        DecorativeRingType decorativeType    = DecorativeRingType::None;

        Color decorativeColor = Color(128, 128, 128, 255);
        std::string decorativeGraphic;
        Color borderColor = Color(210, 210, 210, 255);
        float borderWidth = 1.0f;
        Color valueColor  = Color(60, 90, 200, 255);

        bool showConnectionIndicators = false;
        Color connectionIndicatorColor = Color(220, 40, 40, 255);

        std::string label;   // Optional ring caption, used by CSV import

        // Computed by the chart at layout time; writing to these has no effect.
        float innerRadius = 0.0f;
        float outerRadius = 0.0f;
    };

// =============================================================================
// CONNECTIONS
// =============================================================================

    struct CircularConnection {
        size_t fromRing = 0;
        size_t fromCell = 0;
        size_t toRing   = 0;
        size_t toCell   = 0;
        double value    = 0.0;      // Drives proportional thickness
        ConnectionStyle style;      // Visual description (see ConnectionRenderer)
        std::string label;
    };

// =============================================================================
// CIRCULAR INFOGRAPHIC ELEMENT
// =============================================================================

    class UltraCanvasCircularInfoGraphic : public UltraCanvasChartElementBase {
    public:
        UltraCanvasCircularInfoGraphic(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasCircularInfoGraphic() override = default;

        // ===== Rings =====
        // Appends a ring and returns its index. Rings are ordered inner to outer.
        size_t AddRing(const CircularRing& ring);
        bool SetRing(size_t index, const CircularRing& ring);
        const CircularRing* GetRing(size_t index) const;
        size_t GetRingCount() const { return rings.size(); }
        void ClearRings();
        // Resizes the ring vector to exactly `levels`, appending empty rings or
        // dropping trailing ones. It never duplicates rings added with AddRing.
        void SetNumberOfLevels(int levels);
        bool SetRingTextStyle(size_t index, CircularTextStyle style);
        bool SetRingValueType(size_t index, ValueVisualizationType type);

        // ===== Cells =====
        size_t GetCellCount(size_t ringIndex) const;
        const CircularCell* GetCell(size_t ringIndex, size_t cellIndex) const;
        bool UpdateCellValue(size_t ringIndex, size_t cellIndex, double value);
        bool UpdateCellText(size_t ringIndex, size_t cellIndex, const std::string& text);
        bool AddCell(size_t ringIndex, const CircularCell& cell);

        // ===== Geometry =====
        // Explicit radii in pixels; also disables auto-fit.
        void SetRadiusRange(float inner, float outer);
        void SetAutoFitRadius(bool on);
        // Hole size as a fraction of the outer radius, used while auto-fitting.
        void SetInnerRadiusFraction(float fraction);
        void SetAngleOffset(float degrees);       // Rotation of the whole chart
        float GetAngleOffset() const { return angleOffsetDeg; }
        void SetTotalAngle(float degrees);        // 10..360, partial circles
        float GetTotalAngle() const { return totalAngleDeg; }
        void SetRingSpacing(float pixels);
        void SetCellSpacingAngle(float degrees);

        // ===== Appearance =====
        void SetCenterGraphic(const std::string& imagePath);
        void SetBackgroundGraphic(const std::string& imagePath);
        void SetShowBackground(bool show);
        void SetShowCenterCircle(bool show);
        void SetCenterColor(const Color& fill, const Color& border);
        void SetCenterText(const std::string& text);
        void SetColorScale(const ColorScale& scale);
        const ColorScale& GetColorScale() const { return colorScale; }
        void SetLabelFont(const std::string& family, float size, FontWeight weight);
        void SetLabelColor(const Color& c);
        void SetGroupOutlineColor(const Color& c);
        void SetShowGroupOutlines(bool show);

        // ===== Connections =====
        void SetConnectionLineType(ConnectionLineType type);
        ConnectionLineType GetConnectionLineType() const { return connectionType; }
        // Returns the connection index, or SIZE_MAX if the endpoints are invalid.
        size_t AddConnection(size_t fromRing, size_t fromCell,
                             size_t toRing, size_t toCell, double value = 0.0);
        size_t AddConnection(const CircularConnection& connection);
        bool SetConnectionStyle(size_t index, const ConnectionStyle& style);
        void SetDefaultConnectionStyle(const ConnectionStyle& style);
        void ClearConnections();
        size_t GetConnectionCount() const { return connections.size(); }

        // ===== Data import =====
        // One call appends one ring. Format:
        //   "RingLabel;Cell,TextColor,BgColor,Value,Image;Cell,..."
        // Colours accept #RGB / #RRGGBB / #RRGGBBAA, rgb(r,g,b), rgba(r,g,b,a)
        // and the common colour names. Commas inside rgb(...) are not treated
        // as field separators, so RGB triples survive the split.
        bool ImportDataFromCSV(const std::string& csvData);
        // Parses one colour token using the same rules as the CSV importer.
        static Color ParseColorToken(const std::string& token,
                                     const Color& fallback = Color(0, 0, 0, 255));

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool on);
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }

        std::function<void(size_t ringIndex, size_t cellIndex)> onCellClick;
        std::function<void(size_t ringIndex, size_t cellIndex)> onCellHover;
        std::function<std::string(const CircularCell&)> customTooltipGenerator;

        // ===== Base overrides =====
        // Overridden because the infographic owns its ring model and must render
        // without an IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Model -----
        std::vector<CircularRing> rings;
        std::vector<CircularConnection> connections;
        ColorScale colorScale;

        // ----- Geometry configuration -----
        bool  autoFitRadius = true;
        float explicitInnerRadius = 0.0f;
        float explicitOuterRadius = 0.0f;
        float innerRadiusFraction = 0.25f;
        float angleOffsetDeg = -90.0f;
        float totalAngleDeg  = 360.0f;
        float ringSpacing    = 2.0f;
        float cellSpacingDeg = 0.5f;

        // ----- Appearance -----
        std::string centerGraphic;
        std::string backgroundGraphic;
        std::string centerText;
        // The base class' `showBackground` also paints a plot-area rectangle,
        // which a radial chart does not want, so the fill is handled here.
        bool drawOwnBackground = false;
        bool showCenterCircle = true;
        Color centerFillColor   = Color(245, 245, 245, 255);
        Color centerBorderColor = Color(205, 205, 205, 255);
        std::string labelFontFamily = "Arial";
        float labelFontSize = 10.0f;
        FontWeight labelFontWeight = FontWeight::Normal;
        Color labelColor = Color(40, 40, 40, 255);
        Color groupOutlineColor = Color(90, 90, 90, 255);
        bool showGroupOutlines = false;

        // ----- Connections -----
        ConnectionLineType connectionType = ConnectionLineType::None;
        ConnectionStyle defaultConnectionStyle;

        // ----- Interaction -----
        bool hoverHighlightEnabled = true;
        size_t hoveredRing = SIZE_MAX;
        size_t hoveredCell = SIZE_MAX;

        // ----- Cached layout -----
        Point2Dd cachedCenter;
        float cachedInnerRadius = 0.0f;
        float cachedOuterRadius = 0.0f;

        // ----- Layout -----
        void ComputeGeometry();
        double StartAngleRad() const;
        double TotalAngleRad() const;
        // Angular bounds of one cell, before per-cell spacing is applied.
        bool CellAngles(size_t ringIndex, size_t cellIndex,
                        double& startAngle, double& endAngle) const;
        Point2Dd CellCenterPoint(size_t ringIndex, size_t cellIndex) const;

        // ----- Rendering -----
        void DrawBackground(IRenderContext* ctx);
        void DrawRing(IRenderContext* ctx, size_t ringIndex);
        void DrawCellBackground(IRenderContext* ctx, const CircularRing& ring,
                                const CircularCell& cell, double a0, double a1);
        void DrawCellBar(IRenderContext* ctx, const CircularRing& ring,
                         const CircularCell& cell, double a0, double a1);
        void DrawCellCircularBar(IRenderContext* ctx, const CircularRing& ring,
                                 const CircularCell& cell, double a0, double a1);
        void DrawRingValueLine(IRenderContext* ctx, const CircularRing& ring);
        void DrawCellText(IRenderContext* ctx, const CircularRing& ring,
                          const CircularCell& cell, double a0, double a1);
        void DrawStarLabels(IRenderContext* ctx);
        void DrawDecorativeRing(IRenderContext* ctx, const CircularRing& ring);
        void DrawGroupOutlines(IRenderContext* ctx, const CircularRing& ring);
        void DrawConnections(IRenderContext* ctx);
        void DrawConnectionIndicators(IRenderContext* ctx, size_t ringIndex);
        // Split in two so connections routed through the middle are drawn on
        // top of the centre disc but still beneath its caption.
        void DrawCenterBase(IRenderContext* ctx);
        void DrawCenterLabel(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);

        void DrawCircularText(IRenderContext* ctx, const std::string& text,
                              double radius, double midAngle);
        void BuildSectorOutline(double innerR, double outerR, double a0, double a1,
                                std::vector<Point2Dd>& out) const;

        // ----- Helpers -----
        double NormalizedValue(double value) const;
        Color ResolveCellColor(const CircularCell& cell) const;
        std::string BuildTooltip(const CircularCell& cell,
                                 size_t ringIndex, size_t cellIndex) const;

        // ----- Hit testing -----
        bool HitTestCell(const Point2Dd& localPos, size_t& ringIndex, size_t& cellIndex) const;
        bool HandleClick(const UCEvent& event);
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasCircularInfoGraphic> CreateCircularInfoGraphic(
        const std::string& id, int x, int y, int w, int h);

    // Convenience: one ring per group, one cell per (label, value) pair.
    std::shared_ptr<UltraCanvasCircularInfoGraphic> CreateCircularInfoGraphicFromGroups(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string,
              std::vector<std::pair<std::string, double>>>>& groupedData);

} // namespace UltraCanvas
