// Plugins/Diagrams/UltraCanvasVennDiagram.cpp
// Interactive Venn diagram element implementation
// Version: 2.0.1 (Fixed API compatibility)
// Last Modified: 2025-04-02
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasVennDiagram.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

// ===== INITIALIZATION =====

void UltraCanvasVennDiagramElement::SetupDefaultLayout() {
    // Starts empty - circles are added by demo/user
}

// ===== CIRCLE MANAGEMENT =====

void UltraCanvasVennDiagramElement::AddCircle(const std::string& label, const Color& color) {
    Color finalColor = (color.a == 0) ? GetNextPaletteColor() : color;
    
    switch (circles.size()) {
        case 0:
            circles.emplace_back(label, GetWidth() / 2.0f - 50, GetHeight() / 2.0f, 80, finalColor);
            break;
        case 1:
            circles.emplace_back(label, GetWidth() / 2.0f + 50, GetHeight() / 2.0f, 80, finalColor);
            break;
        case 2:
            circles.emplace_back(label, GetWidth() / 2.0f, GetHeight() / 2.0f - 60, 80, finalColor);
            break;
        default:
            circles.emplace_back(label, GetWidth() / 2.0f, GetHeight() / 2.0f, 80, finalColor);
            break;
    }
    
    RecalculateRegions();
    RequestRedraw();
}

void UltraCanvasVennDiagramElement::RemoveCircle(size_t index) {
    if (index < circles.size()) {
        circles.erase(circles.begin() + index);
        RecalculateRegions();
        RequestRedraw();
    }
}

void UltraCanvasVennDiagramElement::ClearCircles() {
    circles.clear();
    regions.clear();
    RequestRedraw();
}

VennCircle& UltraCanvasVennDiagramElement::GetCircle(size_t index) {
    return circles[index];
}

const VennCircle& UltraCanvasVennDiagramElement::GetCircle(size_t index) const {
    return circles[index];
}

size_t UltraCanvasVennDiagramElement::GetCircleCount() const {
    return circles.size();
}

// ===== ITEM MANAGEMENT =====

void UltraCanvasVennDiagramElement::AddItemToCircle(size_t circleIndex, const std::string& item) {
    if (circleIndex < circles.size()) {
        circles[circleIndex].AddItem(item);
        RecalculateRegions();
        RequestRedraw();
    }
}

void UltraCanvasVennDiagramElement::AddItemToIntersection(const std::vector<size_t>& circleIndices, const std::string& item) {
    for (size_t idx : circleIndices) {
        if (idx < circles.size()) {
            circles[idx].AddItem(item);
        }
    }
    RecalculateRegions();
    RequestRedraw();
}

// ===== LAYOUT MANAGEMENT =====

void UltraCanvasVennDiagramElement::SetLayout(VennLayout newLayout) {
    layout = newLayout;
    ApplyLayout();
    RequestRedraw();
}

VennLayout UltraCanvasVennDiagramElement::GetLayout() const {
    return layout;
}

void UltraCanvasVennDiagramElement::SetCirclePosition(size_t index, double x, double y) {
    if (index < circles.size()) {
        circles[index].center.x = x;
        circles[index].center.y = y;
        RecalculateRegions();
        RequestRedraw();
    }
}

void UltraCanvasVennDiagramElement::SetCircleRadius(size_t index, double radius) {
    if (index < circles.size()) {
        circles[index].radius = radius;
        RecalculateRegions();
        RequestRedraw();
    }
}

// ===== STYLE MANAGEMENT =====

void UltraCanvasVennDiagramElement::SetStyle(VennStyle newStyle) {
    style = newStyle;
    RequestRedraw();
}

VennStyle UltraCanvasVennDiagramElement::GetStyle() const {
    return style;
}

void UltraCanvasVennDiagramElement::SetShowLabels(bool show) {
    showLabels = show;
    RequestRedraw();
}

void UltraCanvasVennDiagramElement::SetShowItemCounts(bool show) {
    showItemCounts = show;
    RequestRedraw();
}

void UltraCanvasVennDiagramElement::SetShowRegionLabels(bool show) {
    showRegionLabels = show;
    RequestRedraw();
}

void UltraCanvasVennDiagramElement::SetFontSize(double size) {
    fontSize = size;
    RequestRedraw();
}

void UltraCanvasVennDiagramElement::SetBackgroundColor(const Color& color) {
    backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasVennDiagramElement::SetColorPalette(const std::vector<Color>& palette) {
    colorPalette = palette;
    for (size_t i = 0; i < circles.size() && i < palette.size(); ++i) {
        circles[i].fillColor = palette[i];
    }
    RequestRedraw();
}

// ===== EVENT CALLBACKS =====

void UltraCanvasVennDiagramElement::SetOnCircleHover(std::function<void(size_t, const VennCircle&)> callback) {
    onCircleHover = callback;
}

void UltraCanvasVennDiagramElement::SetOnRegionHover(std::function<void(size_t, const VennRegion&)> callback) {
    onRegionHover = callback;
}

void UltraCanvasVennDiagramElement::SetOnCircleClick(std::function<void(size_t, const VennCircle&)> callback) {
    onCircleClick = callback;
}

void UltraCanvasVennDiagramElement::SetOnRegionClick(std::function<void(size_t, const VennRegion&)> callback) {
    onRegionClick = callback;
}

// ===== LAYOUT IMPLEMENTATION =====

void UltraCanvasVennDiagramElement::ApplyLayout() {
    if (circles.empty()) return;
    
    double centerX = GetWidth() / 2.0f;
    double centerY = GetHeight() / 2.0f;
    double baseRadius = std::min(GetWidth(), GetHeight()) / 4.5f;
    
    switch (layout) {
        case VennLayout::TwoCircles:
            ApplyTwoCircleLayout(centerX, centerY, baseRadius);
            break;
        case VennLayout::ThreeCircles:
            ApplyThreeCircleLayout(centerX, centerY, baseRadius);
            break;
        case VennLayout::FourCircles:
            ApplyFourCircleLayout(centerX, centerY, baseRadius);
            break;
        case VennLayout::FiveCircles:
            ApplyFiveCircleLayout(centerX, centerY, baseRadius);
            break;
        case VennLayout::Custom:
            break;
    }
    
    RecalculateRegions();
}

void UltraCanvasVennDiagramElement::ApplyTwoCircleLayout(double centerX, double centerY, double radius) {
    if (circles.size() < 2) return;
    
    double overlap = radius * 0.35f;
    
    circles[0].center = Point2Df(centerX - overlap, centerY);
    circles[0].radius = radius;
    
    circles[1].center = Point2Df(centerX + overlap, centerY);
    circles[1].radius = radius;
}

void UltraCanvasVennDiagramElement::ApplyThreeCircleLayout(double centerX, double centerY, double radius) {
    if (circles.size() < 3) return;
    
    double angle120 = 2.0f * M_PI / 3.0f;
    double offsetDistance = radius * 0.55f;
    
    circles[0].center = Point2Df(centerX, centerY - offsetDistance);
    circles[0].radius = radius;
    
    circles[1].center = Point2Df(
        centerX + offsetDistance * cos(angle120 + M_PI/2), 
        centerY + offsetDistance * sin(angle120 + M_PI/2)
    );
    circles[1].radius = radius;
    
    circles[2].center = Point2Df(
        centerX + offsetDistance * cos(2 * angle120 + M_PI/2),
        centerY + offsetDistance * sin(2 * angle120 + M_PI/2)
    );
    circles[2].radius = radius;
}

void UltraCanvasVennDiagramElement::ApplyFourCircleLayout(double centerX, double centerY, double radius) {
    if (circles.size() < 4) return;
    
    double offset = radius * 0.65f;
    double adjustedRadius = radius * 0.85f;
    
    circles[0].center = Point2Df(centerX - offset, centerY - offset);
    circles[0].radius = adjustedRadius;
    
    circles[1].center = Point2Df(centerX + offset, centerY - offset);
    circles[1].radius = adjustedRadius;
    
    circles[2].center = Point2Df(centerX - offset, centerY + offset);
    circles[2].radius = adjustedRadius;
    
    circles[3].center = Point2Df(centerX + offset, centerY + offset);
    circles[3].radius = adjustedRadius;
}

void UltraCanvasVennDiagramElement::ApplyFiveCircleLayout(double centerX, double centerY, double radius) {
    if (circles.size() < 5) return;
    
    double pentagonRadius = radius * 0.75f;
    double circleRadius = radius * 0.65f;
    
    for (size_t i = 0; i < 5; ++i) {
        double angle = (i * 2.0f * M_PI / 5.0f) - M_PI/2;
        circles[i].center = Point2Df(
            centerX + pentagonRadius * cos(angle),
            centerY + pentagonRadius * sin(angle)
        );
        circles[i].radius = circleRadius;
    }
}

// ===== REGION CALCULATION =====

void UltraCanvasVennDiagramElement::RecalculateRegions() {
    regions.clear();
    
    if (circles.empty()) return;
    
    size_t numCircles = circles.size();
    size_t numRegions = 1 << numCircles;
    
    for (size_t regionMask = 0; regionMask < numRegions; ++regionMask) {
        std::vector<size_t> circleIndices;
        
        for (size_t i = 0; i < numCircles; ++i) {
            if (regionMask & (1 << i)) {
                circleIndices.push_back(i);
            }
        }
        
        VennRegion region(circleIndices);
        
        if (!circleIndices.empty()) {
            std::unordered_set<std::string> regionItems = circles[circleIndices[0]].items;
            
            for (size_t i = 1; i < circleIndices.size(); ++i) {
                std::unordered_set<std::string> intersection;
                const auto& circleItems = circles[circleIndices[i]].items;
                
                for (const auto& item : regionItems) {
                    if (circleItems.find(item) != circleItems.end()) {
                        intersection.insert(item);
                    }
                }
                regionItems = intersection;
            }
            
            for (size_t i = 0; i < numCircles; ++i) {
                bool isInRegion = std::find(circleIndices.begin(), circleIndices.end(), i) != circleIndices.end();
                if (!isInRegion) {
                    const auto& excludeItems = circles[i].items;
                    for (const auto& item : excludeItems) {
                        regionItems.erase(item);
                    }
                }
            }
            
            region.items = regionItems;
        }
        
        region.labelPosition = CalculateRegionLabelPosition(circleIndices);
        
        regions.push_back(region);
    }
}

std::vector<size_t> UltraCanvasVennDiagramElement::FindCirclesContainingPoint(const Point2Df& point) const {
    std::vector<size_t> containingCircles;
    
    for (size_t i = 0; i < circles.size(); ++i) {
        if (circles[i].Contains(point)) {
            containingCircles.push_back(i);
        }
    }
    
    return containingCircles;
}

Point2Df UltraCanvasVennDiagramElement::CalculateRegionLabelPosition(const std::vector<size_t>& circleIndices) const {
    if (circleIndices.empty()) {
        return Point2Df(GetWidth() * 0.1f, GetHeight() * 0.1f);
    }
    
    if (circleIndices.size() == 1) {
        return circles[circleIndices[0]].center;
    }
    
    double sumX = 0, sumY = 0;
    for (size_t idx : circleIndices) {
        sumX += circles[idx].center.x;
        sumY += circles[idx].center.y;
    }
    
    return Point2Df(sumX / circleIndices.size(), sumY / circleIndices.size());
}

// ===== RENDERING IMPLEMENTATION =====
void UltraCanvasVennDiagramElement::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
        RenderChart(ctx);
}

void UltraCanvasVennDiagramElement::RenderChart(IRenderContext* ctx) {
    if (!ctx) return;
    
    RenderBackground(ctx);
    RenderCircles(ctx);
    
    if (highlightIntersections) {
        RenderIntersectionHighlights(ctx);
    }
    
    if (showLabels) {
        RenderLabels(ctx);
    }
    
    if (showRegionLabels) {
        RenderRegionLabels(ctx);
    }
}

void UltraCanvasVennDiagramElement::RenderBackground(IRenderContext* ctx) {
    if (backgroundColor.a > 0) {
        ctx->SetFillPaint(backgroundColor);
        ctx->FillRectangle(GetBounds());
    }
}

void UltraCanvasVennDiagramElement::RenderCircles(IRenderContext* ctx) {
    for (size_t i = 0; i < circles.size(); ++i) {
        const VennCircle& circle = circles[i];
        
        double renderRadius = circle.radius;
        if (animationEnabled && animationProgress < 1.0f) {
            renderRadius *= animationProgress;
        }
        
        Color renderColor = circle.fillColor;
        if (i == hoveredCircleIndex) {
            renderColor.a = std::min(255, (int)(renderColor.a * 1.3f));
        }
        
        switch (style) {
            case VennStyle::Classic:
            case VennStyle::Modern:
            case VennStyle::Filled:
                ctx->SetFillPaint(renderColor);
                ctx->FillCircle(circle.center, renderRadius);
                
                if (circle.borderWidth > 0) {
                    ctx->SetStrokePaint(circle.borderColor);
                    ctx->SetStrokeWidth(circle.borderWidth);
                    ctx->DrawCircle(circle.center, renderRadius);
                }
                break;
                
            case VennStyle::Minimal:
            case VennStyle::Outlined:
                ctx->SetStrokePaint(circle.borderColor);
                ctx->SetStrokeWidth(circle.borderWidth);
                ctx->DrawCircle(circle.center, renderRadius);
                break;
        }
    }
}

void UltraCanvasVennDiagramElement::RenderLabels(IRenderContext* ctx) {
    ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(fontSize);
    ctx->SetTextPaint(Colors::Black);
    
    for (size_t i = 0; i < circles.size(); ++i) {
        const VennCircle& circle = circles[i];
        
        double labelX = circle.center.x;
        double labelY = circle.center.y - circle.radius - 18;
        
        std::string labelText = circle.label;
        if (showItemCounts) {
            labelText += " (" + std::to_string(circle.items.size()) + ")";
        }
        
        ctx->DrawText(labelText, Point2Df(labelX, labelY));
    }
}

void UltraCanvasVennDiagramElement::RenderRegionLabels(IRenderContext* ctx) {
    ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(fontSize * 0.9f);
    
    for (size_t i = 0; i < regions.size(); ++i) {
        const VennRegion& region = regions[i];
        
        if (!region.isVisible || region.items.empty()) continue;
        
        ctx->SetTextPaint(region.textColor);
        
        std::string regionText = std::to_string(region.items.size());
        
        ctx->DrawText(regionText, region.labelPosition);
    }
}

void UltraCanvasVennDiagramElement::RenderIntersectionHighlights(IRenderContext* ctx) {
    ctx->SetStrokeWidth(3.0f);
    
    for (const auto& region : regions) {
        if (region.circleIndices.size() > 1) {
            ctx->SetStrokePaint(Color(255, 255, 0, 128));
            ctx->DrawCircle(region.labelPosition, 12);
        }
    }
}

// ===== INTERACTION HANDLING =====

bool UltraCanvasVennDiagramElement::HandleChartMouseMove(const Point2Di& mousePos) {
    if (isDragging && draggedCircleIndex < circles.size()) {
        Point2Di delta(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);
        
        circles[draggedCircleIndex].center.x += delta.x;
        circles[draggedCircleIndex].center.y += delta.y;
        
        lastMousePos = mousePos;
        RecalculateRegions();
        RequestRedraw();
        return true;
    }
    
    size_t newHoveredCircle = FindCircleAtPoint(mousePos);
    if (newHoveredCircle != hoveredCircleIndex) {
        hoveredCircleIndex = newHoveredCircle;
        if (hoveredCircleIndex != SIZE_MAX && onCircleHover) {
            onCircleHover(hoveredCircleIndex, circles[hoveredCircleIndex]);
        }
        RequestRedraw();
        return true;
    }
    
    size_t newHoveredRegion = FindRegionAtPoint(mousePos);
    if (newHoveredRegion != hoveredRegionIndex) {
        hoveredRegionIndex = newHoveredRegion;
        if (hoveredRegionIndex != SIZE_MAX && onRegionHover) {
            onRegionHover(hoveredRegionIndex, regions[hoveredRegionIndex]);
        }
    }
    
    return newHoveredCircle != SIZE_MAX || newHoveredRegion != SIZE_MAX;
}

bool UltraCanvasVennDiagramElement::HandleMouseDown(const Point2Di& mousePos) {
    Point2Df localPos(mousePos.x, mousePos.y);
    
    size_t circleIndex = FindCircleAtPoint(localPos);
    if (circleIndex != SIZE_MAX) {
        isDragging = true;
        draggedCircleIndex = circleIndex;
        lastMousePos = mousePos;
        
        if (onCircleClick) {
            onCircleClick(circleIndex, circles[circleIndex]);
        }
        return true;
    }
    
    size_t regionIndex = FindRegionAtPoint(localPos);
    if (regionIndex != SIZE_MAX && onRegionClick) {
        onRegionClick(regionIndex, regions[regionIndex]);
        return true;
    }
    
    return false;
}

bool UltraCanvasVennDiagramElement::HandleMouseUp(const Point2Di& mousePos) {
    if (isDragging) {
        isDragging = false;
        draggedCircleIndex = SIZE_MAX;
        RecalculateRegions();
        RequestRedraw();
        return true;
    }
    return false;
}

bool UltraCanvasVennDiagramElement::HandleMouseMove(const Point2Di& mousePos) {
    return HandleChartMouseMove(mousePos);
}

bool UltraCanvasVennDiagramElement::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseMove:
            return HandleMouseMove(event.pointer);
        case UCEventType::MouseDown:
            return HandleMouseDown(event.pointer);
        case UCEventType::MouseUp:
            return HandleMouseUp(event.pointer);
        default:
            break;
    }
    return false;
}

// ===== UTILITY METHODS =====

size_t UltraCanvasVennDiagramElement::FindCircleAtPoint(const Point2Df& point) const {
    for (size_t i = 0; i < circles.size(); ++i) {
        if (circles[i].Contains(point)) {
            return i;
        }
    }
    return SIZE_MAX;
}

size_t UltraCanvasVennDiagramElement::FindRegionAtPoint(const Point2Df& point) const {
    std::vector<size_t> containingCircles = FindCirclesContainingPoint(point);
    
    size_t regionMask = 0;
    for (size_t circleIdx : containingCircles) {
        regionMask |= (1 << circleIdx);
    }
    
    for (size_t i = 0; i < regions.size(); ++i) {
        size_t mask = 0;
        for (size_t circleIdx : regions[i].circleIndices) {
            mask |= (1 << circleIdx);
        }
        if (mask == regionMask) {
            return i;
        }
    }
    
    return SIZE_MAX;
}

} // namespace UltraCanvas
