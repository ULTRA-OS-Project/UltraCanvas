// include/Plugins/Diagrams/UltraCanvasVennDiagram.h
// Interactive Venn diagram element with multiple circle support and intersection calculations
// Version: 1.1.0
// Last Modified: 2025-04-02
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_set>
#include <functional>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// VENN DIAGRAM DATA STRUCTURES
// =============================================================================

struct VennCircle {
    std::string label;
    Point2Df center;
    float radius;
    Color fillColor;
    Color borderColor;
    float borderWidth = 2.0f;
    float alpha = 0.6f;
    std::unordered_set<std::string> items;
    
    VennCircle(const std::string& circleLabel, float x, float y, float r, const Color& color)
        : label(circleLabel), center(x, y), radius(r), fillColor(color), borderColor(Colors::Black), borderWidth(2.0f) {}
    
    bool Contains(const Point2Df& point) const {
        float dx = point.x - center.x;
        float dy = point.y - center.y;
        return (dx * dx + dy * dy) <= (radius * radius);
    }
    
    void AddItem(const std::string& item) {
        items.insert(item);
    }
    
    void RemoveItem(const std::string& item) {
        items.erase(item);
    }
};

struct VennRegion {
    std::vector<size_t> circleIndices;
    std::unordered_set<std::string> items;
    Point2Df labelPosition;
    Color textColor = Colors::Black;
    bool isVisible = true;
    
    VennRegion(const std::vector<size_t>& indices) : circleIndices(indices) {}
    
    std::string GetRegionName(const std::vector<VennCircle>& circles) const {
        if (circleIndices.empty()) return "Outside";
        if (circleIndices.size() == 1) return circles[circleIndices[0]].label;
        
        std::string name;
        for (size_t i = 0; i < circleIndices.size(); ++i) {
            if (i > 0) name += " ∩ ";
            name += circles[circleIndices[i]].label;
        }
        return name;
    }
    
    void AddItem(const std::string& item) {
        items.insert(item);
    }
    
    void RemoveItem(const std::string& item) {
        items.erase(item);
    }
};

// =============================================================================
// VENN DIAGRAM LAYOUT PRESETS
// =============================================================================

enum class VennLayout {
    TwoCircles,          // Classic two-circle overlap
    ThreeCircles,        // Classic three-circle Venn
    FourCircles,         // Four-circle arrangement
    FiveCircles,         // Five-circle complex arrangement  
    Custom               // User-defined positions
};

// =============================================================================
// VENN DIAGRAM STYLE OPTIONS
// =============================================================================

enum class VennStyle {
    Classic,            // Traditional overlapping circles
    Modern,             // Stylized with gradients
    Minimal,            // Simple outlines only
    Filled,             // Solid colors with transparency
    Outlined            // Borders only, no fill
};

// =============================================================================
// MAIN VENN DIAGRAM ELEMENT
// =============================================================================

class UltraCanvasVennDiagramElement : public UltraCanvasChartElementBase {
private:
    // Core diagram data
    std::vector<VennCircle> circles;
    std::vector<VennRegion> regions;
    VennLayout layout = VennLayout::ThreeCircles;
    VennStyle style = VennStyle::Classic;
    
    // Display settings
    bool showLabels = true;
    bool showItemCounts = true;
    bool showRegionLabels = false;
    float fontSize = 12.0f;
    std::string fontFamily = "Arial";
    Color backgroundColor = Colors::White;
    
    // Interaction state
    size_t hoveredCircleIndex = SIZE_MAX;
    size_t hoveredRegionIndex = SIZE_MAX;
    Point2Di lastMousePos;
    bool isDragging = false;
    size_t draggedCircleIndex = SIZE_MAX;
    
    // Animation and visual effects
    bool animationEnabled = true;
    float animationProgress = 1.0f;
    bool highlightIntersections = false;
    
    // Color palette for automatic circle coloring
    std::vector<Color> colorPalette = {
        Color(54, 162, 235, 153),   // Blue
        Color(255, 99, 132, 153),   // Red  
        Color(255, 205, 86, 153),   // Yellow
        Color(75, 192, 192, 153),   // Teal
        Color(153, 102, 255, 153),  // Purple
        Color(255, 159, 64, 153),   // Orange
        Color(199, 199, 199, 153),  // Grey
        Color(83, 102, 255, 153)    // Light Blue
    };
    
    // Callbacks
    std::function<void(size_t, const VennCircle&)> onCircleHover;
    std::function<void(size_t, const VennRegion&)> onRegionHover;
    std::function<void(size_t, const VennCircle&)> onCircleClick;
    std::function<void(size_t, const VennRegion&)> onRegionClick;

public:
    UltraCanvasVennDiagramElement(const std::string& id, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, x, y, width, height) {
        enableZoom = true;
        enablePan = true;
        enableSelection = true;
        enableTooltips = true;
        SetupDefaultLayout();
    }
    
    virtual ~UltraCanvasVennDiagramElement() = default;

    // ===== CIRCLE MANAGEMENT =====
    
    void AddCircle(const std::string& label, const Color& color = Color());
    void RemoveCircle(size_t index);
    void ClearCircles();
    
    VennCircle& GetCircle(size_t index);
    const VennCircle& GetCircle(size_t index) const;
    size_t GetCircleCount() const;
    
    // ===== ITEM MANAGEMENT =====
    
    void AddItemToCircle(size_t circleIndex, const std::string& item);
    void AddItemToIntersection(const std::vector<size_t>& circleIndices, const std::string& item);
    
    // ===== LAYOUT MANAGEMENT =====
    
    void SetLayout(VennLayout newLayout);
    VennLayout GetLayout() const;
    
    void SetCirclePosition(size_t index, double x, double y);
    void SetCircleRadius(size_t index, double radius);
    
    // ===== STYLE MANAGEMENT =====
    
    void SetStyle(VennStyle newStyle);
    VennStyle GetStyle() const;
    
    void SetShowLabels(bool show);
    void SetShowItemCounts(bool show);
    void SetShowRegionLabels(bool show);
    void SetFontSize(double size);
    void SetBackgroundColor(const Color& color);
    
    void SetColorPalette(const std::vector<Color>& palette);
    
    // ===== EVENT CALLBACKS =====
    
    void SetOnCircleHover(std::function<void(size_t, const VennCircle&)> callback);
    void SetOnRegionHover(std::function<void(size_t, const VennRegion&)> callback);
    void SetOnCircleClick(std::function<void(size_t, const VennCircle&)> callback);
    void SetOnRegionClick(std::function<void(size_t, const VennRegion&)> callback);
    
    // ===== RENDERING OVERRIDES =====
    
    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
    void RenderChart(IRenderContext* ctxt) override;
    bool HandleChartMouseMove(const Point2Di& mousePos) override;
    bool OnEvent(const UCEvent& event) override;
    
    void ApplyLayout();

private:
    // ===== INTERNAL HELPER METHODS =====
    
    void SetupDefaultLayout();
    
    void ApplyTwoCircleLayout(double centerX, double centerY, double radius);
    void ApplyThreeCircleLayout(double centerX, double centerY, double radius);
    void ApplyFourCircleLayout(double centerX, double centerY, double radius);
    void ApplyFiveCircleLayout(double centerX, double centerY, double radius);
    
    void RecalculateRegions();
    std::vector<size_t> FindCirclesContainingPoint(const Point2Df& point) const;
    Point2Df CalculateRegionLabelPosition(const std::vector<size_t>& circleIndices) const;
    
    void RenderBackground(IRenderContext* ctx);
    void RenderCircles(IRenderContext* ctx);
    void RenderLabels(IRenderContext* ctx);
    void RenderRegionLabels(IRenderContext* ctx);
    void RenderIntersectionHighlights(IRenderContext* ctx);
    
    size_t FindCircleAtPoint(const Point2Df& point) const;
    size_t FindRegionAtPoint(const Point2Df& point) const;
    
    Color GetNextPaletteColor() const {
        return colorPalette[circles.size() % colorPalette.size()];
    }
    
    bool HandleMouseDown(const Point2Di& mousePos);
    bool HandleMouseUp(const Point2Di& mousePos);
    bool HandleMouseMove(const Point2Di& mousePos);
};

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

inline std::shared_ptr<UltraCanvasVennDiagramElement> CreateVennDiagram(
    const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasVennDiagramElement>(id, x, y, width, height);
}

} // namespace UltraCanvas