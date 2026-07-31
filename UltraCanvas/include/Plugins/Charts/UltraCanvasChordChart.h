// include/Plugins/Charts/UltraCanvasChordChart.h
// Chord diagram element: circular category arcs joined by proportional ribbons.
// Version: 1.0.1
// Last Modified: 2026-07-30
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
// CHORD DATA MODEL
// =============================================================================

    struct ChordCategory {
        std::string name;
        Color color = Color(128, 128, 128, 255);

        // Computed by the chart; writing to these has no effect.
        double totalValue = 0.0;   // Throughput: outgoing + incoming
        double startAngle = 0.0;   // Radians
        double endAngle   = 0.0;

        ChordCategory() = default;
        ChordCategory(const std::string& n, const Color& c) : name(n), color(c) {}
    };

    struct ChordFlow {
        size_t fromIndex = 0;
        size_t toIndex   = 0;
        double value     = 0.0;
        Color  flowColor;
        bool   isVisible = true;

        // Computed sub-arcs on each endpoint's category ring (radians).
        double sourceStartAngle = 0.0;
        double sourceEndAngle   = 0.0;
        double targetStartAngle = 0.0;
        double targetEndAngle   = 0.0;

        ChordFlow() = default;
        ChordFlow(size_t from, size_t to, double val)
            : fromIndex(from), toIndex(to), value(val) {}
    };

    // Square matrix of directed flows. `flows[i][j]` is the amount moving from
    // category i to category j. A category's arc is sized by its throughput
    // (row sum + column sum), so categories that only receive still get an arc.
    struct ChordMatrix {
        std::vector<std::vector<double>> flows;
        std::vector<ChordCategory> categories;

        void AddCategory(const ChordCategory& category);
        bool SetFlow(size_t from, size_t to, double value);
        double GetFlow(size_t from, size_t to) const;
        size_t Size() const { return categories.size(); }
        void Clear();

        // Grows/shrinks `flows` to match `categories`, preserving existing values.
        void ResizeMatrix();
        void RecalculateTotals();
        double GrandTotal() const;
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class ChordRibbonColorMode {
        Source,     // Ribbon takes the source category's colour
        Target,     // Ribbon takes the target category's colour
        Larger,     // Colour of whichever endpoint has the larger throughput
        Blend       // 50/50 blend of both endpoints
    };

    enum class ChordLabelPlacement {
        None,        // No labels
        Outside,     // Horizontal text just beyond the ring
        Radial       // Text runs along the radius, flipped on the left half
    };

// =============================================================================
// CHORD CHART ELEMENT
// =============================================================================

    class UltraCanvasChordChart : public UltraCanvasChartElementBase {
    public:
        UltraCanvasChordChart(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasChordChart() override = default;

        // ===== Data =====
        void SetChordMatrix(const ChordMatrix& matrix);
        const ChordMatrix& GetChordMatrix() const { return chordMatrix; }
        void AddCategory(const std::string& name, const Color& color);
        void SetFlow(size_t from, size_t to, double value);
        double GetFlow(size_t from, size_t to) const { return chordMatrix.GetFlow(from, to); }
        void ClearData();

        bool ImportFromMatrix(const std::vector<std::vector<double>>& matrix,
                              const std::vector<std::string>& categoryNames,
                              const std::vector<Color>& colors = {});

        size_t GetCategoryCount() const { return chordMatrix.categories.size(); }
        size_t GetFlowCount() const { return flows.size(); }

        // ===== Geometry =====
        // Explicit radius; also disables auto-fit. Pass <= 0 to re-enable it.
        void SetRadius(float r);
        float GetRadius() const { return cachedOuterRadius; }
        void SetAutoFitRadius(bool on);
        void SetRingThickness(float pixels);      // Category arc band width
        float GetRingThickness() const { return ringThickness; }
        void SetStartAngle(float degrees);        // Default -90 (12 o'clock)
        float GetStartAngle() const { return startAngleDeg; }
        void SetCategoryPadding(float degrees);   // Gap between category arcs
        float GetCategoryPadding() const { return categoryPaddingDeg; }

        // ===== Ribbons =====
        void SetRibbonOpacity(float opacity);     // 0..1
        float GetRibbonOpacity() const { return ribbonOpacity; }
        void SetRibbonCurvature(float curvature); // 0 = straight, 1 = through centre
        void SetRibbonColorMode(ChordRibbonColorMode mode);
        void SetRibbonBorder(const Color& color, float width);
        // Flows at or below this value are dropped, to cut visual clutter.
        void SetMinFlowValue(double minValue);
        double GetMinFlowValue() const { return minFlowValue; }

        // ===== Labels =====
        void SetLabelPlacement(ChordLabelPlacement placement);
        void SetShowLabels(bool show);            // Convenience over SetLabelPlacement
        void SetLabelDistance(float pixels);
        void SetLabelFont(const std::string& family, float size, FontWeight weight);
        void SetLabelColor(const Color& c);
        void SetValueFormatter(std::function<std::string(double)> f);

        // ===== Interaction =====
        void SetHoverHighlightEnabled(bool on);
        void SetTooltipsEnabled(bool on) { SetEnableTooltips(on); }
        // Non-hovered elements fade to this alpha multiplier while something
        // else is hovered. 1.0 disables dimming.
        void SetDimmedOpacity(float factor);

        std::function<void(size_t categoryIndex)> onCategoryClick;
        std::function<void(size_t from, size_t to, double value)> onFlowClick;
        std::function<void(size_t categoryIndex)> onCategoryHover;
        std::function<std::string(const ChordCategory&)> customCategoryTooltip;
        std::function<std::string(const ChordFlow&)> customFlowTooltip;

        // ===== Base overrides =====
        // Overridden because the chord chart holds its own matrix and must
        // render without an IChartDataSource attached.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

    private:
        // ----- Data -----
        ChordMatrix chordMatrix;
        std::vector<ChordFlow> flows;

        // ----- Geometry configuration -----
        bool  autoFitRadius   = true;
        float explicitRadius  = 0.0f;
        float ringThickness   = 20.0f;
        float startAngleDeg   = -90.0f;
        float categoryPaddingDeg = 2.0f;

        // ----- Ribbon style -----
        float ribbonOpacity   = 0.7f;
        float ribbonCurvature = 1.0f;
        ChordRibbonColorMode ribbonColorMode = ChordRibbonColorMode::Larger;
        Color ribbonBorderColor = Color(255, 255, 255, 90);
        float ribbonBorderWidth = 1.0f;
        double minFlowValue = 0.0;

        // ----- Labels -----
        ChordLabelPlacement labelPlacement = ChordLabelPlacement::Outside;
        float labelDistance = 12.0f;
        std::string labelFontFamily = "Arial";
        float labelFontSize = 11.0f;
        FontWeight labelFontWeight = FontWeight::Normal;
        Color labelColor = Color(45, 45, 45, 255);
        std::function<std::string(double)> valueFormatter;

        // ----- Interaction -----
        bool hoverHighlightEnabled = true;
        float dimmedOpacity = 0.25f;
        size_t hoveredCategory = SIZE_MAX;
        size_t hoveredRibbon   = SIZE_MAX;

        // ----- Cached layout -----
        bool layoutValid = false;
        // Element size the cached layout was built for; a change means the
        // layout engine resized us and everything derived has to be rebuilt.
        int lastLayoutWidth  = -1;
        int lastLayoutHeight = -1;
        Point2Dd cachedCenter;
        float cachedOuterRadius = 0.0f;
        float cachedInnerRadius = 0.0f;
        double cachedGrandTotal = 0.0;
        // Ribbon outlines, parallel to `flows`, used for fill and hit testing.
        std::vector<std::vector<Point2Dd>> ribbonOutlines;

        // ----- Layout -----
        void InvalidateLayout();
        void SyncLayoutToElementSize();
        void RebuildFlows();
        void ComputeGeometry();
        void AssignArcs();
        void BuildRibbonOutlines();

        // ----- Rendering -----
        void DrawRibbons(IRenderContext* ctx);
        void DrawCategories(IRenderContext* ctx);
        void DrawLabels(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);

        // ----- Helpers -----
        Color ResolveRibbonColor(const ChordFlow& flow) const;
        bool  IsRibbonActive(size_t ribbonIndex) const;
        bool  IsCategoryActive(size_t categoryIndex) const;
        bool  AnythingHovered() const {
            return hoveredCategory != SIZE_MAX || hoveredRibbon != SIZE_MAX;
        }
        std::string FormatValue(double v) const;
        std::string BuildCategoryTooltip(size_t index) const;
        std::string BuildFlowTooltip(const ChordFlow& flow) const;

        size_t HitTestCategory(const Point2Dd& localPos) const;
        size_t HitTestRibbon(const Point2Dd& localPos) const;
        bool HandleClick(const UCEvent& event);

        static std::vector<Color> DefaultPalette();
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasChordChart> CreateChordChart(
        const std::string& id, int x, int y, int w, int h);

    // Convenience: builds a chart straight from a square matrix.
    std::shared_ptr<UltraCanvasChordChart> CreateChordChartFromMatrix(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::vector<double>>& matrix,
        const std::vector<std::string>& categoryNames,
        const std::vector<Color>& colors = {});

} // namespace UltraCanvas
