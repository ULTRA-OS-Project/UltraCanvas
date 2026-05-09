// Plugins/Charts/UltraCanvasTreeMapElement.cpp
// Interactive treemap component implementation
// Version: 1.0.2
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

void UltraCanvasTreeMapElement::SetDataFromHierarchy(
    const std::vector<std::tuple<std::string, std::string, double, double>>& data) {
    
    rootNode = std::make_shared<TreeMapNode>("Root", 0.0);
    
    std::map<std::string, std::shared_ptr<TreeMapNode>> nodeMap;
    nodeMap[""] = rootNode;
    
    for (const auto& [path, name, value, secValue] : data) {
        auto parts = SplitPath(path);
        std::string currentPath = "";
        std::shared_ptr<TreeMapNode> parent = rootNode;
        
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string fullPath = currentPath.empty() ? parts[i] : currentPath + "/" + parts[i];
            
            if (nodeMap.find(fullPath) == nodeMap.end()) {
                auto newNode = std::make_shared<TreeMapNode>(parts[i], 0.0);
                newNode->depth = i + 1;
                parent->AddChild(newNode);
                nodeMap[fullPath] = newNode;
            }
            
            parent = nodeMap[fullPath];
            currentPath = fullPath;
        }
        
        parent->value = value;
        parent->secondaryValue = secValue;
        parent->name = name.empty() ? parts.back() : name;
    }
    
    currentNode = rootNode;
    RecalculateLayout();
    RequestRedraw();
}

bool UltraCanvasTreeMapElement::DrillDown(std::shared_ptr<TreeMapNode> node) {
    if (!enableDrillDown || !node || node->isLeaf) return false;
    
    navigationHistory.push_back(currentNode);
    currentNode = node;
    
    if (enableAnimations) {
        StartDrillDownAnimation();
    } else {
        RecalculateLayout();
        RequestRedraw();
    }
    
    if (onNodeDrillDown) {
        onNodeDrillDown(node->name);
    }
    
    return true;
}

bool UltraCanvasTreeMapElement::DrillUp() {
    if (navigationHistory.empty()) return false;
    
    currentNode = navigationHistory.back();
    navigationHistory.pop_back();
    
    if (enableAnimations) {
        StartDrillUpAnimation();
    } else {
        RecalculateLayout();
        RequestRedraw();
    }
    
    if (onNodeDrillUp) {
        onNodeDrillUp(currentNode->name);
    }
    
    return true;
}

void UltraCanvasTreeMapElement::ResetToRoot() {
    if (currentNode == rootNode) return;
    
    navigationHistory.clear();
    currentNode = rootNode;
    
    if (enableAnimations) {
        StartResetAnimation();
    } else {
        RecalculateLayout();
        RequestRedraw();
    }
    
    if (onNodeReset) {
        onNodeReset();
    }
}

std::vector<std::string> UltraCanvasTreeMapElement::GetNavigationPath() const {
    std::vector<std::string> path;
    for (const auto& node : navigationHistory) {
        path.push_back(node->name);
    }
    path.push_back(currentNode->name);
    return path;
}

void UltraCanvasTreeMapElement::SelectNode(std::shared_ptr<TreeMapNode> node) {
    if (selectedNode != node) {
        selectedNode = node;
        RequestRedraw();
        
        if (onNodeSelect && node) {
            onNodeSelect(node->name, node->value);
        }
    }
}

void UltraCanvasTreeMapElement::ClearSelection() {
    if (selectedNode) {
        selectedNode = nullptr;
        RequestRedraw();
        
        if (onNodeDeselect) {
            onNodeDeselect();
        }
    }
}

std::shared_ptr<TreeMapNode> UltraCanvasTreeMapElement::FindNodeByName(const std::string& name) const {
    return FindNodeRecursive(rootNode, name);
}

std::vector<std::shared_ptr<TreeMapNode>> UltraCanvasTreeMapElement::FindNodesByValue(
    double minValue, double maxValue) const {
    std::vector<std::shared_ptr<TreeMapNode>> results;
    FindNodesByValueRecursive(rootNode, minValue, maxValue, results);
    return results;
}

void UltraCanvasTreeMapElement::HighlightNode(std::shared_ptr<TreeMapNode> node) {
    hoveredNode = node;
    RequestRedraw();
}

void UltraCanvasTreeMapElement::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {

    RenderCommonBackground(ctx);
    RenderChart(ctx);

    if (enableSelection) {
        DrawSelectionIndicators(ctx);
    }
}

void UltraCanvasTreeMapElement::RenderChart(IRenderContext* ctx) {
    if (!ctx || !currentNode) return;
    
    DrawGlobalBackground(ctx);
    
    if (currentNode->children.empty() && currentNode != rootNode) {
        DrawLeafNodeDetails(ctx);
    } else {
        if (currentNode->children.empty()) return;
        
        ctx->PushState();
        
        auto bounds = GetBounds();
        double centerX = bounds.x + bounds.width / 2;
        double centerY = bounds.y + bounds.height / 2;
        
        if (enablePan && (panOffset.x != 0 || panOffset.y != 0)) {
            ctx->Translate(static_cast<double>(panOffset.x), static_cast<double>(panOffset.y));
        }
        
        if (enableZoom && zoomLevel != 1.0f) {
            ctx->Translate(centerX, centerY);
            ctx->Scale(zoomLevel, zoomLevel);
            ctx->Translate(-centerX, -centerY);
        }
        
        if (isAnimating) {
            ApplyAnimationTransform(ctx);
        }
        
        for (const auto& child : currentNode->children) {
            if (child->isVisible) {
                DrawTreeMapRectangle(ctx, child);
            }
        }
        
        if (currentNode != rootNode) {
            DrawNavigationBreadcrumbs(ctx);
        }
        
        ctx->PopState();
    }
}

bool UltraCanvasTreeMapElement::HandleChartMouseMove(const Point2Di& mousePos) {
    auto node = GetNodeAtPosition(mousePos);
    
    if (node != hoveredNode) {
        hoveredNode = node;
        RequestRedraw();
        
        if (onNodeHover && node) {
            onNodeHover(node->name, mousePos);
        }
        
        if (enableTooltips && node) {
            ShowNodeTooltip(mousePos, node);
        } else {
            HideTooltip();
        }
    }
    
    return node != nullptr;
}

void UltraCanvasTreeMapElement::RecalculateLayout() {
    if (!currentNode || currentNode->children.empty()) return;
    
    Rect2Df availableArea = GetPlotArea();
    
    switch (algorithm) {
        case TreeMapAlgorithm::Squarified:
            CalculateSquarifiedLayout(currentNode->children, availableArea);
            break;
        case TreeMapAlgorithm::SliceAndDice:
            CalculateSliceAndDiceLayout(currentNode->children, availableArea, 0);
            break;
        case TreeMapAlgorithm::Strip:
            CalculateStripLayout(currentNode->children, availableArea);
            break;
        case TreeMapAlgorithm::Spiral:
            CalculateSpiralLayout(currentNode->children, availableArea);
            break;
        case TreeMapAlgorithm::Binary:
            CalculateBinaryLayout(currentNode->children, availableArea);
            break;
    }
}

void UltraCanvasTreeMapElement::CalculateSquarifiedLayout(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Df& area) {
    
    if (nodes.empty()) return;
    
    std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
        return a->GetTotalValue() > b->GetTotalValue();
    });
    
    SquarifyRecursive(nodes, 0, area, std::min(area.width, area.height));
}

void UltraCanvasTreeMapElement::SquarifyRecursive(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, 
    const Rect2Df& area, double length) {
    
    if (start >= nodes.size()) return;
    
    std::vector<std::shared_ptr<TreeMapNode>> row;
    double totalValue = GetTotalValueForNodes(nodes, start);
    
    size_t i = start;
    while (i < nodes.size()) {
        row.push_back(nodes[i]);
        
        if (row.size() > 1) {
            double currentRatio = CalculateWorstRatio(row, totalValue, length);
            row.pop_back();
            double previousRatio = CalculateWorstRatio(row, totalValue, length);
            
            if (previousRatio < currentRatio) {
                break;
            }
            row.push_back(nodes[i]);
        }
        i++;
    }
    
    LayoutRow(row, area, totalValue);
    
    if (i < nodes.size()) {
        Rect2Df remainingArea = CalculateRemainingArea(area, row, totalValue);
        double newLength = std::min(remainingArea.width, remainingArea.height);
        SquarifyRecursive(nodes, i, remainingArea, newLength);
    }
}

void UltraCanvasTreeMapElement::CalculateSliceAndDiceLayout(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Df& area, int depth) {
    
    if (nodes.empty()) return;
    
    bool horizontal = (depth % 2 == 0);
    double totalValue = 0.0f;
    for (const auto& node : nodes) {
        totalValue += node->GetTotalValue();
    }
    
    if (totalValue <= 0) return;
    
    double currentPos = horizontal ? area.x : area.y;
    double availableSize = horizontal ? area.width : area.height;
    
    for (const auto& node : nodes) {
        double proportion = node->GetTotalValue() / totalValue;
        double size = availableSize * proportion;
        
        if (horizontal) {
            node->bounds = Rect2Df(currentPos, area.y, size - padding, area.height);
            currentPos += size;
        } else {
            node->bounds = Rect2Df(area.x, currentPos, area.width, size - padding);
            currentPos += size;
        }
        
        if (node->bounds.width < minRectSize) node->bounds.width = minRectSize;
        if (node->bounds.height < minRectSize) node->bounds.height = minRectSize;
    }
}

void UltraCanvasTreeMapElement::CalculateStripLayout(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Df& area) {
    
    double totalValue = 0.0f;
    for (const auto& node : nodes) {
        totalValue += node->GetTotalValue();
    }
    
    if (totalValue <= 0) return;
    
    double currentY = area.y;
    for (const auto& node : nodes) {
        double proportion = node->GetTotalValue() / totalValue;
        double height = area.height * proportion - padding;
        
        node->bounds = Rect2Df(area.x, currentY, area.width, height);
        currentY += height + padding;
        
        if (node->bounds.height < minRectSize) node->bounds.height = minRectSize;
    }
}

void UltraCanvasTreeMapElement::CalculateSpiralLayout(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Df& area) {
    
    Point2Df center(area.x + area.width / 2, area.y + area.height / 2);
    double radius = 10.0f;
    double angle = 0.0f;
    double spiralTightness = 0.5f;
    
    for (const auto& node : nodes) {
        double size = std::sqrt(node->GetTotalValue()) * 20.0f;
        size = std::max(size, minRectSize);
        
        double x = center.x + radius * std::cos(angle) - size / 2;
        double y = center.y + radius * std::sin(angle) - size / 2;
        
        node->bounds = Rect2Df(x, y, size, size);
        
        angle += spiralTightness;
        radius += spiralTightness * 2;
    }
}

void UltraCanvasTreeMapElement::CalculateBinaryLayout(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Df& area) {
    
    BinaryLayoutRecursive(nodes, 0, nodes.size(), area);
}

void UltraCanvasTreeMapElement::BinaryLayoutRecursive(
    std::vector<std::shared_ptr<TreeMapNode>>& nodes, 
    size_t start, size_t end, const Rect2Df& area) {
    
    if (start >= end) return;
    
    size_t count = end - start;
    if (count == 1) {
        nodes[start]->bounds = area;
        return;
    }
    
    size_t mid = FindBalancedSplit(nodes, start, end);
    
    bool horizontal = area.width > area.height;
    if (horizontal) {
        double splitRatio = GetValueRatio(nodes, start, mid, end);
        double splitX = area.x + area.width * splitRatio;
        
        Rect2Df leftArea(area.x, area.y, splitX - area.x - padding/2, area.height);
        Rect2Df rightArea(splitX + padding/2, area.y, area.x + area.width - splitX - padding/2, area.height);
        
        BinaryLayoutRecursive(nodes, start, mid, leftArea);
        BinaryLayoutRecursive(nodes, mid, end, rightArea);
    } else {
        double splitRatio = GetValueRatio(nodes, start, mid, end);
        double splitY = area.y + area.height * splitRatio;
        
        Rect2Df topArea(area.x, area.y, area.width, splitY - area.y - padding/2);
        Rect2Df bottomArea(area.x, splitY + padding/2, area.width, area.y + area.height - splitY - padding/2);
        
        BinaryLayoutRecursive(nodes, start, mid, topArea);
        BinaryLayoutRecursive(nodes, mid, end, bottomArea);
    }
}

void UltraCanvasTreeMapElement::DrawGlobalBackground(IRenderContext* ctx) {
    auto bounds = GetLocalBounds();
    
    if (globalTransparent) {
        return;
    }
    
    if (!backgroundImagePath.empty() && hasGlobalBackground) {
        UCImageRaster img(backgroundImagePath);
        ctx->DrawImage(img, bounds, ImageFitMode::Fill);
    } else {
        ctx->SetFillPaint(backgroundColor);
        ctx->FillRectangle(bounds);
    }
}

void UltraCanvasTreeMapElement::DrawTreeMapRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node) {
    
    if (!node || node->bounds.width < 1 || node->bounds.height < 1) return;
    
    Rect2Df rect = node->bounds;
    
    switch (visualStyle) {
        case TreeMapStyle::Flat:
            DrawFlatRectangle(ctx, node, rect);
            break;
        case TreeMapStyle::Raised:
            DrawRaisedRectangle(ctx, node, rect);
            break;
        case TreeMapStyle::Sunken:
            DrawSunkenRectangle(ctx, node, rect);
            break;
        case TreeMapStyle::Gradient:
            DrawGradientRectangle(ctx, node, rect);
            break;
        case TreeMapStyle::Textured:
            DrawTexturedRectangle(ctx, node, rect);
            break;
        case TreeMapStyle::Minimal:
            DrawMinimalRectangle(ctx, node, rect);
            break;
    }
    
    if (node == selectedNode) {
        DrawSelectionIndicator(ctx, rect);
    }
    
    if (node == hoveredNode) {
        DrawHoverEffect(ctx, rect);
    }
}

void UltraCanvasTreeMapElement::DrawFlatRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    if (!node->isTransparent) {
        if (!node->backgroundImagePath.empty() && node->hasBackgroundImage) {
            UCImageRaster img(node->backgroundImagePath);
            ctx->DrawImage(img, rect, ImageFitMode::Fill);
        } else {
            Color bgColor = node->backgroundColor.a > 0 ? node->backgroundColor : GetNodeColor(node);
            ctx->SetFillPaint(bgColor);
            ctx->FillRectangle(rect);
        }
    }
    
    if (borderWidth > 0) {
        ctx->SetStrokePaint(borderColor);
        ctx->SetStrokeWidth(borderWidth);
        ctx->DrawRectangle(rect);
    }
    
    DrawRectangleContent(ctx, node, rect);
}

void UltraCanvasTreeMapElement::DrawRaisedRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    Color baseColor = node->backgroundColor.a > 0 ? node->backgroundColor : GetNodeColor(node);
    
    ctx->SetFillPaint(baseColor);
    ctx->FillRectangle(rect);
    
    Color highlight = LightenColor(baseColor, 0.3f);
    ctx->SetStrokePaint(highlight);
    ctx->SetStrokeWidth(2.0f);
    ctx->DrawLine({rect.x, rect.y}, {rect.x + rect.width, rect.y});
    ctx->DrawLine({rect.x, rect.y}, {rect.x, rect.y + rect.height});
    
    Color shadow = DarkenColor(baseColor, 0.3f);
    ctx->SetStrokePaint(shadow);
    ctx->DrawLine({rect.x + rect.width, rect.y}, {rect.x + rect.width, rect.y + rect.height});
    ctx->DrawLine({rect.x, rect.y + rect.height}, {rect.x + rect.width, rect.y + rect.height});
    
    DrawRectangleContent(ctx, node, rect);
}

void UltraCanvasTreeMapElement::DrawSunkenRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    Color baseColor = node->backgroundColor.a > 0 ? node->backgroundColor : GetNodeColor(node);
    
    ctx->SetFillPaint(baseColor);
    ctx->FillRectangle(rect);
    
    Color shadow = DarkenColor(baseColor, 0.3f);
    ctx->SetStrokePaint(shadow);
    ctx->SetStrokeWidth(2.0f);
    ctx->DrawLine({rect.x, rect.y}, {rect.x + rect.width, rect.y});
    ctx->DrawLine({rect.x, rect.y}, {rect.x, rect.y + rect.height});
    
    Color highlight = LightenColor(baseColor, 0.3f);
    ctx->SetStrokePaint(highlight);
    ctx->DrawLine({rect.x + rect.width, rect.y}, {rect.x + rect.width, rect.y + rect.height});
    ctx->DrawLine({rect.x, rect.y + rect.height}, {rect.x + rect.width, rect.y + rect.height});
    
    DrawRectangleContent(ctx, node, rect);
}

void UltraCanvasTreeMapElement::DrawGradientRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    Color baseColor = node->backgroundColor.a > 0 ? node->backgroundColor : GetNodeColor(node);
    Color startColor = LightenColor(baseColor, 0.2f);
    Color endColor = DarkenColor(baseColor, 0.2f);
    
    int steps = 20;
    double stepHeight = rect.height / steps;
    
    for (int i = 0; i < steps; ++i) {
        double ratio = static_cast<double>(i) / (steps - 1);
        Color stepColor = InterpolateColor(startColor, endColor, ratio);
        
        ctx->SetFillPaint(stepColor);
        ctx->FillRectangle({rect.x, rect.y + i * stepHeight, rect.width, stepHeight + 1});
    }
    
    if (borderWidth > 0) {
        ctx->SetStrokePaint(borderColor);
        ctx->SetStrokeWidth(borderWidth);
        ctx->DrawRectangle(rect);
    }
    
    DrawRectangleContent(ctx, node, rect);
}

void UltraCanvasTreeMapElement::DrawTexturedRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    if (!node->backgroundImagePath.empty() && node->hasBackgroundImage) {
        UCImageRaster img(node->backgroundImagePath);
        ctx->DrawImage(img, rect, ImageFitMode::Fill);
    } else {
        Color baseColor = node->backgroundColor.a > 0 ? node->backgroundColor : GetNodeColor(node);
        ctx->SetFillPaint(baseColor);
        ctx->FillRectangle(rect);
        
        ctx->SetStrokePaint(DarkenColor(baseColor, 0.1f));
        ctx->SetStrokeWidth(1.0f);
        
        for (double i = rect.x; i < rect.x + rect.width + rect.height; i += 8) {
            ctx->DrawLine({i, rect.y}, {i - rect.height, rect.y + rect.height});
        }
    }
    
    if (borderWidth > 0) {
        ctx->SetStrokePaint(borderColor);
        ctx->SetStrokeWidth(borderWidth);
        ctx->DrawRectangle(rect);
    }
    
    DrawRectangleContent(ctx, node, rect);
}

void UltraCanvasTreeMapElement::DrawMinimalRectangle(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    if (borderWidth > 0) {
        ctx->SetStrokePaint(borderColor);
        ctx->SetStrokeWidth(borderWidth);
        ctx->DrawRectangle(rect);
    }
    
    DrawRectangleContent(ctx, node, rect);
}

void UltraCanvasTreeMapElement::DrawRectangleContent(
    IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Df& rect) {
    
    if (rect.width < 20 || rect.height < 20) return;
    
    double contentY = rect.y + padding;
    double availableWidth = rect.width - 2 * padding;
    double lineHeight = fontSize + 2;
    
    if (showIcons && !node->iconPaths.empty()) {
        double iconSize = std::min(16.0, rect.height / 4);
        double iconX = rect.x + padding;
        
        for (const auto& iconPath : node->iconPaths) {
            if (iconX + iconSize > rect.x + rect.width - padding) break;
            
            Rect2Df iconRect(iconX, contentY, iconSize, iconSize);
            UCImageRaster img(iconPath);
            ctx->DrawImage(img, iconRect, ImageFitMode::Contain);
            iconX += iconSize + 2;
        }
        
        if (!node->iconPaths.empty()) {
            contentY += iconSize + 2;
        }
    }
    
    Color textColor = node->textColor.a > 0 ? node->textColor : Colors::Black;
    
    if (showLabels && !node->name.empty()) {
        double currentFontSize = autoScaleText ? CalculateOptimalFontSize(node->name, availableWidth, titleFontSize) : titleFontSize;
        ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(currentFontSize);
        ctx->SetTextPaint(textColor);
        
        ctx->DrawText(node->name, {rect.x + padding, contentY});
        contentY += currentFontSize + 2;
    }
    
    for (const auto& text : node->displayTexts) {
        if (contentY + lineHeight > rect.y + rect.height - padding) break;
        
        double currentFontSize = autoScaleText ? CalculateOptimalFontSize(text, availableWidth, fontSize) : fontSize;
        ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(currentFontSize);
        ctx->SetTextPaint(textColor);
        
        ctx->DrawText(text, {rect.x + padding, contentY});
        contentY += currentFontSize + 2;
    }
    
    if (showValues || showPercentages) {
        std::string valueText;
        
        if (showValues && showPercentages) {
            double percentage = (currentNode->GetTotalValue() > 0) ? 
                (node->GetTotalValue() / currentNode->GetTotalValue()) * 100.0 : 0.0;
            valueText = FormatValue(node->GetTotalValue()) + " (" + FormatPercentage(percentage) + ")";
        } else if (showValues) {
            valueText = FormatValue(node->GetTotalValue());
        } else {
            double percentage = (currentNode->GetTotalValue() > 0) ? 
                (node->GetTotalValue() / currentNode->GetTotalValue()) * 100.0 : 0.0;
            valueText = FormatPercentage(percentage);
        }
        
        if (contentY + lineHeight <= rect.y + rect.height - padding) {
            double currentFontSize = autoScaleText ? CalculateOptimalFontSize(valueText, availableWidth, fontSize) : fontSize;
            ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(currentFontSize);
            ctx->SetTextPaint(textColor);
            
            ctx->DrawText(valueText, {rect.x + padding, contentY});
        }
    }
}

void UltraCanvasTreeMapElement::DrawSelectionIndicator(IRenderContext* ctx, const Rect2Df& rect) {
    ctx->SetStrokePaint(Color(255, 165, 0, 255));
    ctx->SetStrokeWidth(3.0f);
    ctx->DrawRectangle({rect.x - 1, rect.y - 1, rect.width + 2, rect.height + 2});
}

void UltraCanvasTreeMapElement::DrawHoverEffect(IRenderContext* ctx, const Rect2Df& rect) {
    ctx->SetFillPaint(Color(255, 255, 255, 30));
    ctx->FillRectangle(rect);
}

void UltraCanvasTreeMapElement::DrawNavigationBreadcrumbs(IRenderContext* ctx) {
    std::vector<std::string> path = GetNavigationPath();
    if (path.empty()) return;
    
    auto bounds = GetBounds();
    double x = bounds.x + 10;
    double y = bounds.y + 15;
    
    ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(12.0f);
    ctx->SetTextPaint(Colors::Black);
    
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            ctx->DrawText(" > ", {x, y});
            x += 20;
        }
        
        ctx->DrawText(path[i], {x, y});
        x += path[i].length() * 8 + 10;
    }
}

void UltraCanvasTreeMapElement::DrawLeafNodeDetails(IRenderContext* ctx) {
    auto bounds = GetLocalBounds();
    
    ctx->SetFillPaint(backgroundColor);
    ctx->FillRectangle(bounds);
    
    double y = bounds.y + 20;
    
    ctx->SetFontFace(fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(titleFontSize);
    ctx->SetTextPaint(Colors::Black);
    ctx->DrawText("Details: " + currentNode->name, {20, y});
    
    y += titleFontSize + 10;
    
    ctx->SetFontSize(fontSize);
    ctx->DrawText("Value: " + FormatValue(currentNode->value), {20, y});
    y += fontSize + 5;
    
    if (currentNode->secondaryValue != 0) {
        ctx->DrawText("Secondary: " + FormatValue(currentNode->secondaryValue), {20, y});
        y += fontSize + 5;
    }
    
    if (!currentNode->description.empty()) {
        ctx->DrawText("Description: " + currentNode->description, {20, y});
    }
}

std::shared_ptr<TreeMapNode> UltraCanvasTreeMapElement::GetNodeAtPosition(const Point2Di& pos) const {
    if (!currentNode) return nullptr;
    
    Point2Df relativePos(static_cast<double>(pos.x),
                         static_cast<double>(pos.y));
    
    return FindNodeAtPositionRecursive(currentNode, relativePos);
}

std::shared_ptr<TreeMapNode> UltraCanvasTreeMapElement::FindNodeAtPositionRecursive(
    std::shared_ptr<TreeMapNode> node, const Point2Df& pos) const {
    
    if (!node || !node->isVisible) return nullptr;
    
    if (node->isLeaf && PointInRect(pos, node->bounds)) {
        return node;
    }
    
    for (const auto& child : node->children) {
        auto found = FindNodeAtPositionRecursive(child, pos);
        if (found) return found;
    }
    
    return nullptr;
}

Color UltraCanvasTreeMapElement::GetNodeColor(std::shared_ptr<TreeMapNode> node) const {
    if (!node) return colorPalette[0];
    
    size_t index;
    if (node->depth == 0) {
        index = 0;
    } else if (node->parent && node->parent->depth == 0) {
        size_t siblingIndex = 0;
        for (const auto& sibling : node->parent->children) {
            if (sibling == node) break;
            siblingIndex++;
        }
        index = siblingIndex % colorPalette.size();
    } else {
        index = node->depth % colorPalette.size();
    }
    
    switch (colorScheme) {
        case TreeMapColorScheme::Sequential:
            return InterpolateColor(colorPalette[0], colorPalette[1], 
                                  node->secondaryValue / GetMaxSecondaryValue());
        case TreeMapColorScheme::Diverging:
            return node->secondaryValue >= 0 ? colorPalette[0] : colorPalette[1];
        case TreeMapColorScheme::Categorical:
        case TreeMapColorScheme::Custom:
            return colorPalette[index];
        case TreeMapColorScheme::Random:
            return GenerateRandomColor(node->name);
        case TreeMapColorScheme::Monochrome:
            return InterpolateColor(Colors::White, colorPalette[0], 
                                  node->secondaryValue / GetMaxSecondaryValue());
    }
    
    return colorPalette[index];
}

void UltraCanvasTreeMapElement::UpdateColors() {
    if (rootNode) {
        UpdateNodeColorsRecursive(rootNode);
    }
}

void UltraCanvasTreeMapElement::UpdateNodeColorsRecursive(std::shared_ptr<TreeMapNode> node) {
    if (!node) return;
    
    node->backgroundColor = GetNodeColor(node);
    
    for (auto& child : node->children) {
        UpdateNodeColorsRecursive(child);
    }
}

void UltraCanvasTreeMapElement::ShowNodeTooltip(const Point2Di& mousePos, std::shared_ptr<TreeMapNode> node) {
    if (!node) return;
    
    std::string content = node->name;
    
    double percentage = (currentNode->GetTotalValue() > 0) ? 
        (node->GetTotalValue() / currentNode->GetTotalValue()) * 100.0 : 0.0;
    
    content += "\nValue: " + FormatValue(node->GetTotalValue());
    content += " (" + FormatPercentage(percentage) + ")";
    
    if (node->secondaryValue != 0) {
        content += "\nSecondary: " + FormatValue(node->secondaryValue);
    }
    
    if (!node->description.empty()) {
        content += "\n" + node->description;
    }
    
    if (!node->isLeaf) {
        content += "\n(" + std::to_string(node->children.size()) + " sub-categories)";
    }
    
    SetTooltip(content);
}

void UltraCanvasTreeMapElement::StartDrillDownAnimation() {
    for (const auto& child : currentNode->children) {
        oldBoundsMap[child->name] = child->bounds;
    }
    isAnimating = true;
    animationType = "drillDown";
    animationStartTime = std::chrono::steady_clock::now();
    RecalculateLayout();
    RequestRedraw();
}

void UltraCanvasTreeMapElement::StartDrillUpAnimation() {
    for (const auto& child : currentNode->children) {
        oldBoundsMap[child->name] = child->bounds;
    }
    isAnimating = true;
    animationType = "drillUp";
    animationStartTime = std::chrono::steady_clock::now();
    RecalculateLayout();
    RequestRedraw();
}

void UltraCanvasTreeMapElement::StartResetAnimation() {
    for (const auto& child : currentNode->children) {
        oldBoundsMap[child->name] = child->bounds;
    }
    isAnimating = true;
    animationType = "reset";
    animationStartTime = std::chrono::steady_clock::now();
    RecalculateLayout();
    RequestRedraw();
}

void UltraCanvasTreeMapElement::ApplyAnimationTransform(IRenderContext* ctx) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - animationStartTime).count();
    double progress = elapsed / animationDuration;
    
    if (progress >= 1.0f) {
        isAnimating = false;
        currentAnimationProgress = 1.0f;
        oldBoundsMap.clear();
    } else {
        currentAnimationProgress = progress;
        double eased = 1.0f - std::pow(1.0f - progress, 3.0f);
        
        for (const auto& child : currentNode->children) {
            if (oldBoundsMap.find(child->name) != oldBoundsMap.end()) {
                const Rect2Df& oldBounds = oldBoundsMap[child->name];
                const Rect2Df& newBounds = child->bounds;
                
                Rect2Df interpolated(
                    oldBounds.x + (newBounds.x - oldBounds.x) * eased,
                    oldBounds.y + (newBounds.y - oldBounds.y) * eased,
                    oldBounds.width + (newBounds.width - oldBounds.width) * eased,
                    oldBounds.height + (newBounds.height - oldBounds.height) * eased
                );
                child->bounds = interpolated;
            }
        }
    }
}

std::vector<std::string> UltraCanvasTreeMapElement::SplitPath(const std::string& path) const {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    return parts;
}

std::shared_ptr<TreeMapNode> UltraCanvasTreeMapElement::FindNodeRecursive(
    std::shared_ptr<TreeMapNode> node, const std::string& name) const {
    
    if (!node) return nullptr;
    if (node->name == name) return node;
    
    for (const auto& child : node->children) {
        auto result = FindNodeRecursive(child, name);
        if (result) return result;
    }
    
    return nullptr;
}

void UltraCanvasTreeMapElement::FindNodesByValueRecursive(
    std::shared_ptr<TreeMapNode> node, double minValue, double maxValue, 
    std::vector<std::shared_ptr<TreeMapNode>>& results) const {
    
    if (!node) return;
    
    if (node->value >= minValue && node->value <= maxValue) {
        results.push_back(node);
    }
    
    for (const auto& child : node->children) {
        FindNodesByValueRecursive(child, minValue, maxValue, results);
    }
}

double UltraCanvasTreeMapElement::GetTotalValueForNodes(
    const std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start) const {
    
    double total = 0.0f;
    for (size_t i = start; i < nodes.size(); ++i) {
        total += nodes[i]->GetTotalValue();
    }
    return total;
}

double UltraCanvasTreeMapElement::CalculateWorstRatio(
    const std::vector<std::shared_ptr<TreeMapNode>>& row, double totalValue, double length) const {
    
    if (row.empty()) return std::numeric_limits<double>::max();
    
    double rowValue = 0.0f;
    double minValue = std::numeric_limits<double>::max();
    double maxValue = 0.0f;
    
    for (const auto& node : row) {
        double value = node->GetTotalValue();
        rowValue += value;
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }
    
    double rowArea = (rowValue / totalValue) * length * length;
    double rowLength = rowArea / length;
    
    double ratio1 = (length * length * maxValue) / (rowArea * rowArea);
    double ratio2 = (rowArea * rowArea) / (length * length * minValue);
    
    return std::max(ratio1, ratio2);
}

void UltraCanvasTreeMapElement::LayoutRow(
    const std::vector<std::shared_ptr<TreeMapNode>>& row, const Rect2Df& area, double totalValue) {
    
    if (row.empty()) return;
    
    double rowValue = 0.0f;
    for (const auto& node : row) {
        rowValue += node->GetTotalValue();
    }
    
    bool horizontal = area.width > area.height;
    double position = horizontal ? area.x : area.y;
    
    for (const auto& node : row) {
        double proportion = node->GetTotalValue() / rowValue;
        
        if (horizontal) {
            double width = (area.width * proportion) - padding;
            node->bounds = Rect2Df(position, area.y, width, area.height - padding);
            position += width + padding;
        } else {
            double height = (area.height * proportion) - padding;
            node->bounds = Rect2Df(area.x, position, area.width - padding, height);
            position += height + padding;
        }
        
        if (node->bounds.width < minRectSize) node->bounds.width = minRectSize;
        if (node->bounds.height < minRectSize) node->bounds.height = minRectSize;
    }
}

Rect2Df UltraCanvasTreeMapElement::CalculateRemainingArea(
    const Rect2Df& area, const std::vector<std::shared_ptr<TreeMapNode>>& row, double totalValue) const {
    
    if (row.empty()) return area;
    
    double rowValue = 0.0f;
    for (const auto& node : row) {
        rowValue += node->GetTotalValue();
    }
    
    bool horizontal = area.width > area.height;
    double usedSpace = (rowValue / totalValue) * (horizontal ? area.width : area.height);
    
    if (horizontal) {
        return Rect2Df(area.x + usedSpace, area.y, area.width - usedSpace, area.height);
    } else {
        return Rect2Df(area.x, area.y + usedSpace, area.width, area.height - usedSpace);
    }
}

size_t UltraCanvasTreeMapElement::FindBalancedSplit(
    const std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, size_t end) const {
    
    double totalValue = 0.0f;
    for (size_t i = start; i < end; ++i) {
        totalValue += nodes[i]->GetTotalValue();
    }
    
    double targetValue = totalValue / 2.0f;
    double currentValue = 0.0f;
    
    for (size_t i = start; i < end - 1; ++i) {
        currentValue += nodes[i]->GetTotalValue();
        if (currentValue >= targetValue) {
            return i + 1;
        }
    }
    
    return (start + end) / 2;
}

double UltraCanvasTreeMapElement::GetValueRatio(
    const std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, size_t mid, size_t end) const {
    
    double leftValue = 0.0f;
    double totalValue = 0.0f;
    
    for (size_t i = start; i < mid; ++i) {
        leftValue += nodes[i]->GetTotalValue();
    }
    
    for (size_t i = start; i < end; ++i) {
        totalValue += nodes[i]->GetTotalValue();
    }
    
    return (totalValue > 0) ? leftValue / totalValue : 0.5f;
}

double UltraCanvasTreeMapElement::GetMaxSecondaryValue() const {
    double maxVal = 0.0;
    GetMaxSecondaryValueRecursive(rootNode, maxVal);
    return maxVal;
}

void UltraCanvasTreeMapElement::GetMaxSecondaryValueRecursive(
    std::shared_ptr<TreeMapNode> node, double& maxVal) const {
    
    if (!node) return;
    
    maxVal = std::max(maxVal, node->secondaryValue);
    
    for (const auto& child : node->children) {
        GetMaxSecondaryValueRecursive(child, maxVal);
    }
}

Color UltraCanvasTreeMapElement::LightenColor(const Color& color, double factor) const {
    return Color(
        std::min(255, static_cast<int>(color.r + (255 - color.r) * factor)),
        std::min(255, static_cast<int>(color.g + (255 - color.g) * factor)),
        std::min(255, static_cast<int>(color.b + (255 - color.b) * factor)),
        color.a
    );
}

Color UltraCanvasTreeMapElement::DarkenColor(const Color& color, double factor) const {
    return Color(
        static_cast<int>(color.r * (1.0f - factor)),
        static_cast<int>(color.g * (1.0f - factor)),
        static_cast<int>(color.b * (1.0f - factor)),
        color.a
    );
}

Color UltraCanvasTreeMapElement::InterpolateColor(const Color& start, const Color& end, double ratio) const {
    ratio = std::max(0.0, std::min(1.0, ratio));
    
    return Color(
        static_cast<int>(start.r + (end.r - start.r) * ratio),
        static_cast<int>(start.g + (end.g - start.g) * ratio),
        static_cast<int>(start.b + (end.b - start.b) * ratio),
        static_cast<int>(start.a + (end.a - start.a) * ratio)
    );
}

Color UltraCanvasTreeMapElement::GenerateRandomColor(const std::string& seed) const {
    std::hash<std::string> hasher;
    size_t hash = hasher(seed);
    
    return Color(
        (hash & 0xFF0000) >> 16,
        (hash & 0x00FF00) >> 8,
        (hash & 0x0000FF),
        255
    );
}

double UltraCanvasTreeMapElement::CalculateOptimalFontSize(
    const std::string& text, double availableWidth, double maxSize) const {
    
    double textWidth = text.length() * maxSize * 0.6f;
    
    if (textWidth <= availableWidth) {
        return maxSize;
    }
    
    double optimalSize = (availableWidth / textWidth) * maxSize;
    return std::max(8.0, optimalSize);
}

std::string UltraCanvasTreeMapElement::FormatValue(double value) const {
    if (value >= 1000000) {
        return std::to_string(static_cast<int>(value / 1000000)) + "M";
    } else if (value >= 1000) {
        return std::to_string(static_cast<int>(value / 1000)) + "K";
    } else {
        return std::to_string(static_cast<int>(value));
    }
}

std::string UltraCanvasTreeMapElement::FormatPercentage(double percentage) const {
    return std::to_string(static_cast<int>(percentage * 100) / 100.0) + "%";
}

bool UltraCanvasTreeMapElement::PointInRect(const Point2Df& point, const Rect2Df& rect) const {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

Rect2Df UltraCanvasTreeMapElement::GetPlotArea() const {
    auto bounds = GetBounds();
    
    double topMargin = (currentNode != rootNode) ? 30.0f : 10.0f;
    
    return Rect2Df(
        10,
        topMargin,
        bounds.width - 20,
        bounds.height - topMargin - 10
    );
}

bool UltraCanvasTreeMapElement::HandleChartMouseWheel(double delta) {
    if (!enableZoom) return false;
    
    double zoomFactor = (delta > 0) ? 1.1f : 0.9f;
    
    zoomLevel *= zoomFactor;
    zoomLevel = std::max(0.5f, std::min(3.0f, zoomLevel));
    
    RequestRedraw();
    
    return true;
}

bool UltraCanvasTreeMapElement::HandleChartPan(int dx, int dy) {
    if (!enablePan) return false;
    
    panOffset.x += dx;
    panOffset.y += dy;
    
    RequestRedraw();
    
    return true;
}

} // namespace UltraCanvas
