// include/Plugins/Diagrams/UltraCanvasCircleDiagram.h
// Hub-and-spoke circle diagram infographic: a centre hub, a backbone ring, and
// equally sized labelled nodes threaded onto that ring, each carrying a fan of
// satellites on leader lines. Presentation-only - authored from code, rendered,
// hover/tooltip/click only.
// Version: 1.0.0
// Last Modified: 2026-08-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
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
// CONFIGURATION ENUMS
// =============================================================================

    // Structure preset. Designs change how the ring, nodes and satellites are
    // arranged and decorated; they never change the node count, which is set
    // independently with SetNodeCount(). Applying a design fills in defaults
    // that every individual setter can still override afterwards.
    enum class CircleDiagramDesign {
        SatelliteWheel,  // Hairline backbone, saturated nodes, 3 satellites each
        BandedWheel,     // Thick coloured backbone band, icon-over-label nodes
        Custom           // Nothing pre-filled; assemble from the primitives
    };

    // Categorical colour theme. Each named palette is a hue ramp sampled at N
    // points, so any node count stays coherent - these are not fixed lists.
    enum class CircleDiagramPaletteKind {
        Vibrant,
        Ocean,
        Mint,
        Pastel,
        Midnight,
        Dark,
        Monochrome,
        Custom          // Colours supplied via SetCustomPalette(), cycled if short
    };

    enum class CircleBackboneStyle {
        NoRing,         // Nodes sit on an invisible circle
        Hairline,       // Thin outline ring
        Band            // Thick filled band
    };

    enum class CircleLeaderStyle {
        NoLeader,
        Solid,
        Dashed,
        Dotted
    };

    // Where a node's label is drawn relative to its disc.
    enum class CircleNodeLabelPlacement {
        Inside,         // Wrapped inside the disc (references 1 and 2)
        Outside         // Parked radially outside the disc
    };

// =============================================================================
// NODE / SATELLITE MODEL
// =============================================================================

    struct CircleSatellite {
        std::string text;
        // Fully transparent means "derive from the parent node's colour".
        Color fillColor   = Color(0, 0, 0, 0);
        Color borderColor = Color(0, 0, 0, 0);
        Color textColor   = Color(0, 0, 0, 0);
        std::string iconImage;
        std::string tooltip;    // Overrides the generated tooltip when set
        double value = 0.0;     // Carried for tooltips/callbacks only - never sizes the disc

        CircleSatellite() = default;
        explicit CircleSatellite(const std::string& t) : text(t) {}
    };

    struct CircleNode {
        std::string text;
        // Fully transparent means "take the palette colour for this index".
        Color fillColor   = Color(0, 0, 0, 0);
        Color borderColor = Color(0, 0, 0, 0);
        Color textColor   = Color(0, 0, 0, 0);
        std::string iconImage;      // Drawn above the label, inside the disc
        std::string tooltip;
        double value = 0.0;         // Tooltip/callback payload only - see note below
        std::vector<CircleSatellite> satellites;

        CircleNode() = default;
        explicit CircleNode(const std::string& t) : text(t) {}
    };

    // Note on sizing: every node disc has the same radius, and every satellite
    // disc has the same radius. `value` never scales a disc. Equal radii are
    // what keep the satellite fans uniform and the ring rhythm even, and all
    // the surveyed references are equal-sized. Use SetNodeRadius() /
    // SetSatelliteRadius() to size them per diagram.

// =============================================================================
// CIRCLE DIAGRAM ELEMENT
// =============================================================================

    class UltraCanvasCircleDiagram : public UltraCanvasChartElementBase {
    public:
        // Node counts outside this range cannot be laid out legibly: below the
        // minimum the ring reads as a line, above the maximum labels collide at
        // any radius. SetNodeCount() clamps into the range rather than
        // degrading silently, the way SetTotalAngle() clamps to 10..360 degrees.
        static constexpr size_t kMinNodeCount = 3;
        static constexpr size_t kMaxNodeCount = 12;

        UltraCanvasCircleDiagram(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasCircleDiagram() override = default;

        // ===== Design & palette =====
        void SetDesign(CircleDiagramDesign d);
        CircleDiagramDesign GetDesign() const { return design; }
        void SetPalette(CircleDiagramPaletteKind kind);
        CircleDiagramPaletteKind GetPalette() const { return paletteKind; }
        // Explicit colours for CircleDiagramPaletteKind::Custom. Cycled when
        // shorter than the node count; also selects Custom as a side effect.
        void SetCustomPalette(const std::vector<Color>& colors);
        // The colour the palette resolves for node `index` of `total`.
        Color PaletteColor(size_t index, size_t total) const;

        // ===== Nodes =====
        size_t AddNode(const CircleNode& node);
        size_t AddNode(const std::string& text);
        bool SetNode(size_t index, const CircleNode& node);
        const CircleNode* GetNode(size_t index) const;
        size_t GetNodeCount() const { return nodes.size(); }
        void ClearNodes();
        // Resizes to exactly `count` nodes, clamped to [kMinNodeCount,
        // kMaxNodeCount]; appends blank nodes or drops trailing ones.
        void SetNodeCount(size_t count);
        bool UpdateNodeText(size_t index, const std::string& text);

        // ===== Satellites =====
        bool AddSatellite(size_t nodeIndex, const CircleSatellite& satellite);
        bool AddSatellite(size_t nodeIndex, const std::string& text);
        size_t GetSatelliteCount(size_t nodeIndex) const;
        const CircleSatellite* GetSatellite(size_t nodeIndex, size_t satelliteIndex) const;
        bool ClearSatellites(size_t nodeIndex);

        // ===== Hub =====
        void SetHubText(const std::string& text);
        const std::string& GetHubText() const { return hubText; }
        void SetHubIcon(const std::string& imagePath);
        // A hub disc is optional; without it the caption floats on the background.
        void SetShowHubDisc(bool show);
        void SetHubColor(const Color& fill, const Color& border);
        void SetHubTextColor(const Color& c);
        void SetHubRadiusFraction(float fraction);   // Of the backbone radius

        // ===== Geometry =====
        void SetNodeRadius(float pixels);            // 0 = auto-fit from the arc
        void SetSatelliteRadius(float pixels);       // 0 = 0.55 x node radius
        void SetSatelliteOffset(float pixels);       // Gap between node and satellite rims
        void SetSatelliteFanAngle(float degrees);    // Total spread, clamped 0..300
        void SetAngleOffset(float degrees);          // Rotation of the whole diagram
        float GetAngleOffset() const { return angleOffsetDeg; }
        void SetBackboneStyle(CircleBackboneStyle style);
        void SetBackboneThickness(float pixels);
        void SetBackboneColor(const Color& c);

        // ===== Appearance =====
        void SetLeaderStyle(CircleLeaderStyle style);
        void SetLeaderColor(const Color& c);
        void SetLeaderWidth(float pixels);
        void SetNodeLabelPlacement(CircleNodeLabelPlacement placement);
        void SetNodeFont(const std::string& family, float size, FontWeight weight);
        void SetSatelliteFont(const std::string& family, float size, FontWeight weight);
        void SetHubFont(const std::string& family, float size, FontWeight weight);
        void SetNodeBorderWidth(float pixels);
        void SetShowNodeIcons(bool show);

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool on);
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }

        std::function<void(size_t nodeIndex)> onNodeClick;
        std::function<void(size_t nodeIndex)> onNodeHover;
        std::function<void(size_t nodeIndex, size_t satelliteIndex)> onSatelliteClick;
        std::function<void(size_t nodeIndex, size_t satelliteIndex)> onSatelliteHover;
        std::function<std::string(const CircleNode&)> customNodeTooltipGenerator;

        // ===== Base overrides =====
        // Overridden because the diagram owns its node model and renders with
        // no IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Model -----
        std::vector<CircleNode> nodes;
        std::vector<Color> customPalette;

        CircleDiagramDesign design = CircleDiagramDesign::SatelliteWheel;
        CircleDiagramPaletteKind paletteKind = CircleDiagramPaletteKind::Vibrant;

        // ----- Hub -----
        std::string hubText;
        std::string hubIcon;
        bool showHubDisc = false;
        Color hubFillColor   = Color(245, 245, 248, 255);
        Color hubBorderColor = Color(205, 205, 212, 255);
        Color hubTextColor   = Color(40, 44, 60, 255);
        float hubRadiusFraction = 0.52f;

        // ----- Geometry configuration -----
        float explicitNodeRadius = 0.0f;      // 0 = derive from the per-node arc
        float explicitSatelliteRadius = 0.0f; // 0 = 0.55 x node radius
        float satelliteOffset = 14.0f;
        float satelliteFanDeg = 110.0f;
        float angleOffsetDeg = -90.0f;        // 12 o'clock, family convention
        CircleBackboneStyle backboneStyle = CircleBackboneStyle::Hairline;
        float backboneThickness = 1.5f;
        Color backboneColor = Color(170, 174, 186, 255);

        // ----- Appearance -----
        CircleLeaderStyle leaderStyle = CircleLeaderStyle::Solid;
        Color leaderColor = Color(150, 154, 166, 255);
        float leaderWidth = 1.0f;
        CircleNodeLabelPlacement nodeLabelPlacement = CircleNodeLabelPlacement::Inside;
        std::string nodeFontFamily = "Arial";
        float nodeFontSize = 10.0f;
        FontWeight nodeFontWeight = FontWeight::Bold;
        std::string satelliteFontFamily = "Arial";
        float satelliteFontSize = 8.5f;
        FontWeight satelliteFontWeight = FontWeight::Normal;
        std::string hubFontFamily = "Arial";
        float hubFontSize = 15.0f;
        FontWeight hubFontWeight = FontWeight::Normal;
        float nodeBorderWidth = 0.0f;
        bool showNodeIcons = true;

        // Fallbacks for satellites that leave a colour fully transparent. The
        // design owns these: SatelliteWheel wants white discs with a hairline
        // and dark text, BandedWheel wants dark discs with light text. A
        // per-satellite colour always wins over them.
        Color satelliteDefaultFill   = Color(255, 255, 255, 255);
        Color satelliteDefaultBorder = Color(176, 180, 190, 255);
        Color satelliteDefaultText   = Color(45, 48, 62, 255);

        // ----- Interaction -----
        bool hoverHighlightEnabled = true;
        size_t hoveredNode = SIZE_MAX;
        size_t hoveredSatellite = SIZE_MAX;

        // ----- Cached layout -----
        Point2Dd cachedCenter;
        float cachedBackboneRadius = 0.0f;
        float cachedNodeRadius = 0.0f;
        float cachedSatelliteRadius = 0.0f;
        // The configured fan narrowed to what the node's own 360/N wedge can
        // hold, so fans never reach into a neighbour's at high node counts.
        float cachedFanDeg = 0.0f;

        // ----- Layout -----
        void ComputeGeometry();
        // Narrows the configured fan - and, if needed, the satellite radius -
        // to what one node's 360/N wedge can hold.
        void FitSatelliteFan();
        double NodeAngle(size_t index) const;
        Point2Dd NodeCenter(size_t index) const;
        Point2Dd SatelliteCenter(size_t nodeIndex, size_t satelliteIndex) const;
        // Largest satellite fan reach, in pixels beyond the backbone radius.
        // Drives the outer margin so satellites are never clipped.
        float OuterReach() const;
        size_t MaxSatelliteCount() const;

        // ----- Rendering -----
        void DrawBackbone(IRenderContext* ctx);
        void DrawHub(IRenderContext* ctx);
        void DrawNode(IRenderContext* ctx, size_t index);
        void DrawSatellites(IRenderContext* ctx, size_t nodeIndex);
        void DrawLeader(IRenderContext* ctx, const Point2Dd& from, const Point2Dd& to,
                        const Color& color);
        void DrawDiscLabel(IRenderContext* ctx, const std::string& text,
                           const Point2Dd& center, double radius,
                           const std::string& iconPath, const Color& textColor,
                           const std::string& family, float size, FontWeight weight);
        void RenderTitle(IRenderContext* ctx);
        void ApplyLeaderDash(IRenderContext* ctx) const;

        // ----- Helpers -----
        Color ResolveNodeFill(size_t index) const;
        Color ResolveNodeText(size_t index) const;
        Color ResolveSatelliteFill(size_t nodeIndex, size_t satelliteIndex) const;
        Color ResolveSatelliteText(size_t nodeIndex, size_t satelliteIndex) const;
        std::string BuildNodeTooltip(size_t index) const;
        std::string BuildSatelliteTooltip(size_t nodeIndex, size_t satelliteIndex) const;

        // ----- Hit testing -----
        bool HitTest(const Point2Dd& localPos, size_t& nodeIndex, size_t& satelliteIndex) const;
        bool HandleClick(const UCEvent& event);
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasCircleDiagram> CreateCircleDiagram(
        const std::string& id, int x, int y, int w, int h);

    // Convenience: one node per (label, [satellite labels]) pair.
    std::shared_ptr<UltraCanvasCircleDiagram> CreateCircleDiagramFromGroups(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, std::vector<std::string>>>& groupedData);

} // namespace UltraCanvas
