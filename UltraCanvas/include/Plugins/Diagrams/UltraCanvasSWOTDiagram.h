// include/Plugins/Diagrams/UltraCanvasSWOTDiagram.h
// Classic four-panel SWOT analysis infographic with multiple design presets
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// SWOT DIAGRAM ENUMERATIONS
// =============================================================================

    // The four SWOT quadrants in canonical order:
    // Strengths / Weaknesses (internal), Opportunities / Threats (external)
    enum class SWOTQuadrant {
        Strengths = 0,
        Weaknesses = 1,
        Opportunities = 2,
        Threats = 3
    };
    constexpr size_t kSWOTQuadrantCount = 4;

    // Geometry presets. All designs render the same four item lists; they only
    // change how the panels are arranged and decorated.
    enum class SWOTDesign {
        Matrix,         // Classic contiguous 2x2 grid (optional axis captions)
        Cards,          // Four separated rounded cards with colored header bars
        CornerBadges,   // Vivid filled panels, letter badges at the outer
                        // corners and a central "SWOT analysis" circle
        CenterDiamond,  // Central rotated square of four letter triangles with
                        // text blocks at the surrounding corners
        Rows,           // Four stacked horizontal bands with big letter blocks
        Columns         // Four side-by-side columns with header chips
    };

// =============================================================================
// SWOT DIAGRAM DATA STRUCTURES
// =============================================================================

    struct SWOTItem {
        std::string text;
        // Bullet marker color; fully transparent = use the quadrant accent
        Color bulletColor = Color(0, 0, 0, 0);

        SWOTItem() = default;
        SWOTItem(const std::string& t) : text(t) {}
        SWOTItem(const std::string& t, const Color& bullet) : text(t), bulletColor(bullet) {}
    };

    struct SWOTQuadrantConfig {
        std::string title;        // "Strengths"
        std::string badgeLetter;  // "S"
        Color accentColor;        // Main color; panel tints derive from it

        SWOTQuadrantConfig() = default;
        SWOTQuadrantConfig(const std::string& t, const std::string& badge, const Color& accent)
            : title(t), badgeLetter(badge), accentColor(accent) {}
    };

    // Identifies one item for hit testing / selection (quadrant + item index).
    // item < 0 means "no item".
    struct SWOTItemRef {
        int quadrant = -1;
        int item = -1;

        bool IsValid() const { return quadrant >= 0 && item >= 0; }
        bool operator==(const SWOTItemRef& o) const { return quadrant == o.quadrant && item == o.item; }
        bool operator!=(const SWOTItemRef& o) const { return !(*this == o); }
    };

// =============================================================================
// SWOT DIAGRAM ELEMENT CLASS
// =============================================================================

    class UltraCanvasSWOTDiagram : public UltraCanvasChartElementBase {
    private:
        // ===== DATA =====
        std::array<std::vector<SWOTItem>, kSWOTQuadrantCount> items;
        std::array<SWOTQuadrantConfig, kSWOTQuadrantCount> quadrants;
        SWOTDesign design = SWOTDesign::CornerBadges;

        // ===== VISUAL OPTIONS =====
        bool darkTheme = false;
        bool showBadges = true;         // S/W/O/T letter badges
        bool showCenterElement = true;  // Central circle/diamond where the design has one
        bool showItemBullets = true;    // Bullet markers before items
        bool showAxisCaptions = false;  // Internal/External + Helpful/Harmful (Matrix design)
        std::string centerText = "SWOT\nanalysis";

        // ===== FONT SETTINGS =====
        float titleFontSize = 17.0f;          // Element title (from base chartTitle)
        float quadrantTitleFontSize = 13.0f;  // Panel headers
        float itemFontSize = 11.0f;           // Item text
        float badgeFontSize = 15.0f;          // Letter inside badges
        float centerFontSize = 15.0f;         // Central circle text

        // ===== SELECTION & INTERACTION =====
        SWOTItemRef hoveredItem;
        SWOTItemRef selectedItem;

        // ===== LAYOUT CACHE (local coordinates) =====
        struct ItemHitRect {
            SWOTItemRef ref;
            Rect2Dd rect;
        };
        struct SWOTLayout {
            Rect2Dd diagramArea;
            std::array<Rect2Dd, kSWOTQuadrantCount> panels;     // Full panel rect
            std::array<Rect2Dd, kSWOTQuadrantCount> bodies;     // Item list area
            std::vector<ItemHitRect> itemRects;                 // For hit testing
            bool valid = false;
        };
        SWOTLayout layout;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasSWOTDiagram(const std::string& id, int x, int y, int w, int h);

        // ===== DESIGN & THEME =====
        void SetDesign(SWOTDesign d);
        SWOTDesign GetDesign() const { return design; }

        void SetDarkTheme(bool dark);
        bool GetDarkTheme() const { return darkTheme; }

        // ===== QUADRANT CONFIGURATION =====
        void SetQuadrantConfig(SWOTQuadrant q, const SWOTQuadrantConfig& config);
        const SWOTQuadrantConfig& GetQuadrantConfig(SWOTQuadrant q) const {
            return quadrants[static_cast<size_t>(q)];
        }
        void SetQuadrantTitle(SWOTQuadrant q, const std::string& title);
        void SetQuadrantAccentColor(SWOTQuadrant q, const Color& color);

        // ===== DATA MANAGEMENT =====
        void AddItem(SWOTQuadrant q, const std::string& text);
        void AddItem(SWOTQuadrant q, const SWOTItem& item);
        void SetItems(SWOTQuadrant q, const std::vector<SWOTItem>& list);
        const std::vector<SWOTItem>& GetItems(SWOTQuadrant q) const {
            return items[static_cast<size_t>(q)];
        }
        void RemoveItem(SWOTQuadrant q, size_t index);
        void ClearItems(SWOTQuadrant q);
        void ClearAllItems();
        size_t CountItems(SWOTQuadrant q) const { return items[static_cast<size_t>(q)].size(); }
        size_t CountAllItems() const;

        // Loads a small built-in business example
        void LoadSampleData();

        // ===== VISUAL TOGGLES =====
        void SetShowBadges(bool show);
        bool GetShowBadges() const { return showBadges; }

        void SetShowCenterElement(bool show);
        bool GetShowCenterElement() const { return showCenterElement; }

        void SetShowItemBullets(bool show);
        bool GetShowItemBullets() const { return showItemBullets; }

        void SetShowAxisCaptions(bool show);
        bool GetShowAxisCaptions() const { return showAxisCaptions; }

        void SetCenterText(const std::string& text);
        const std::string& GetCenterText() const { return centerText; }

        // ===== SELECTION =====
        // Selection and tooltips are controlled by the base class:
        // SetEnableSelection(bool) / SetEnableTooltips(bool)
        SWOTItemRef GetSelectedItem() const { return selectedItem; }
        void ClearSelection();

        // ===== CALLBACKS =====
        std::function<void(SWOTQuadrant, size_t, const SWOTItem&)> onItemSelect;
        std::function<void(SWOTQuadrant, size_t, const SWOTItem&)> onItemHover;
        std::function<void(SWOTQuadrant, size_t, const SWOTItem&)> onItemDoubleClick;
        std::function<void()> onSelectionChange;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== THEME HELPERS =====
        Color PanelFillColor(size_t q) const;   // Tinted panel background
        Color BodyTextColor(size_t q) const;    // Item text color on that panel
        Color DiagramTextColor() const;         // Text on the element background

        // ===== LAYOUT =====
        void UpdateLayout(IRenderContext* ctx);
        void InvalidateLayout() { layout.valid = false; }
        SWOTItemRef FindItemAt(const Point2Di& pos) const;

        // ===== EVENT HELPERS =====
        bool HandleItemClick(const UCEvent& event);
        bool HandleItemDoubleClick(const UCEvent& event);
        std::string BuildItemTooltip(const SWOTItemRef& ref) const;

        // ===== DRAWING =====
        void RenderMatrix(IRenderContext* ctx);
        void RenderCards(IRenderContext* ctx);
        void RenderCornerBadges(IRenderContext* ctx);
        void RenderCenterDiamond(IRenderContext* ctx);
        void RenderRows(IRenderContext* ctx);
        void RenderColumns(IRenderContext* ctx);

        // Wraps and draws the item list of quadrant q inside rect; records hit
        // rects. Returns the y coordinate after the last drawn line.
        double DrawItemList(IRenderContext* ctx, size_t q, const Rect2Dd& rect,
                            const Color& textColor, const Color& bulletColor);
        void DrawBadgeCircle(IRenderContext* ctx, size_t q, const Point2Dd& center,
                             double radius);
        void DrawCenterCircle(IRenderContext* ctx, const Point2Dd& center, double radius);
        void DrawItemHighlights(IRenderContext* ctx);
        std::vector<std::string> WrapText(IRenderContext* ctx, const std::string& text,
                                          double maxWidth) const;
        void DrawMultilineCentered(IRenderContext* ctx, const std::string& text,
                                   const Point2Dd& center, float lineHeight);
    };

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace SWOTDiagramSamples {
        // Business example: 4-5 short items per quadrant
        std::array<std::vector<SWOTItem>, kSWOTQuadrantCount> BusinessExample();
    }

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

    std::shared_ptr<UltraCanvasSWOTDiagram> CreateSWOTDiagramElement(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasSWOTDiagram> CreateSWOTDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            SWOTDesign design);

} // namespace UltraCanvas
