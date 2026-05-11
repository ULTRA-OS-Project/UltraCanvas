// Plugins/Diagrams/UltraCanvasFlowChart.cpp
// Interactive Flow Chart diagram component implementation
// Version: 2.2.1
// Last Modified: 2026-05-09
//
// Changelog:
//   2.2.0 - Obstacle-aware orthogonal routing via A*:
//           ComputeOrthogonalPath() is the new entry point for orthogonal
//           routing. It first tries the cheap cardinal L/Z path; if that
//           collides with any non-endpoint node, it runs A* over the grid
//           with 4-connected movement, turn penalty (5x straight cost), and
//           Manhattan heuristic. Source/target nodes are excluded from the
//           obstacle set so the line can leave/enter them. The first and
//           last waypoints are anchored at the exact cardinal endpoints,
//           with bridge waypoints inserted to keep all segments orthogonal.
//           Path model changed from "L or Z" to "vector<Point2Df> waypoints";
//           ComputeIncomingAngle and ComputeOrthogonalLabelAnchor updated
//           to take the waypoint list. No public API change.
//   2.1.4 - Cardinal-aware orthogonal routing.
//   2.1.3 - StickyNote shape geometry fixed.
//   2.1.2 - Text Y origin corrected (DrawText takes top, not baseline).
//   2.1.1 - Text baseline fix (superseded by 2.1.2). Multi-line label split
//           on '\n' (kept).
//   2.1.0 - Three render bugs fixed (no public API change):
//           1. GetConnectionPoint() shape-aware (Diamond, Oval, Circle, Ellipse).
//           2. Orthogonal label anchored to middle segment.
//           3. Arrow head aligned with last segment + retreats by border width.
//           Also: connection label now uses conn.labelBackgroundColor /
//                 conn.labelTextColor instead of hardcoded values.
//   2.0.0 - Initial release.

#include "Plugins/Diagrams/UltraCanvasFlowChart.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

UltraCanvasFlowChart::UltraCanvasFlowChart(const std::string& id,
                                           int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
}

// =============================================================================
// NODE MANAGEMENT
// =============================================================================

void UltraCanvasFlowChart::AddNode(const std::string& nodeId, FlowChartShape shape,
                                    const std::string& label, double x, double y) {
    AddNode(nodeId, shape, label, x, y, 120.0f, 60.0f);
}

void UltraCanvasFlowChart::AddNode(const std::string& nodeId, FlowChartShape shape,
                                    const std::string& label, double x, double y,
                                    double width, double height) {
    if (nodes.find(nodeId) != nodes.end()) return;
    
    FlowChartNode node(nodeId, label);
    node.shape = shape;
    node.x = snapToGrid ? SnapToGrid(x) : x;
    node.y = snapToGrid ? SnapToGrid(y) : y;
    node.width = width;
    node.height = height;
    
    nodes[nodeId] = node;
    
    if (onNodeCreated) {
        onNodeCreated(nodeId);
    }
    
    RequestRedraw();
}

void UltraCanvasFlowChart::RemoveNode(const std::string& nodeId) {
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&nodeId](const FlowChartConnection& conn) {
                return conn.sourceNodeId == nodeId || conn.targetNodeId == nodeId;
            }),
        connections.end()
    );
    
    nodes.erase(nodeId);
    
    if (selectedNodeId == nodeId) {
        selectedNodeId.clear();
    }
    
    RequestRedraw();
}

void UltraCanvasFlowChart::UpdateNodePosition(const std::string& nodeId, double x, double y) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->x = x;
        node->y = y;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::SetNodeSize(const std::string& nodeId, double width, double height) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->width = width;
        node->height = height;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::UpdateNodeLabel(const std::string& nodeId, const std::string& label) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->label = label;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::SetNodeColor(const std::string& nodeId, const Color& fill, const Color& border) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->fillColor = fill;
        node->borderColor = border;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::SetNodeShape(const std::string& nodeId, FlowChartShape shape) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->shape = shape;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::SetNodeTextColor(const std::string& nodeId, const Color& color) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->textColor = color;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::SetNodeFontSize(const std::string& nodeId, double size) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->fontSize = size;
        RequestRedraw();
    }
}

void UltraCanvasFlowChart::SetNodeBorderWidth(const std::string& nodeId, double width) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->borderWidth = width;
        RequestRedraw();
    }
}

FlowChartNode* UltraCanvasFlowChart::GetNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

const FlowChartNode* UltraCanvasFlowChart::GetNode(const std::string& nodeId) const {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

std::vector<std::string> UltraCanvasFlowChart::GetAllNodeIds() const {
    std::vector<std::string> ids;
    ids.reserve(nodes.size());
    for (const auto& pair : nodes) {
        ids.push_back(pair.first);
    }
    return ids;
}

// =============================================================================
// CONNECTION MANAGEMENT
// =============================================================================

void UltraCanvasFlowChart::AddConnection(const std::string& connId, 
                                          const std::string& sourceId, 
                                          const std::string& targetId) {
    AddConnection(connId, sourceId, targetId, FlowChartConnectionStyle::Orthogonal, FlowChartArrowStyle::Arrow);
}

void UltraCanvasFlowChart::AddConnection(const std::string& connId, 
                                          const std::string& sourceId, 
                                          const std::string& targetId,
                                          FlowChartConnectionStyle style, 
                                          FlowChartArrowStyle arrowStyle) {
    if (nodes.find(sourceId) == nodes.end() || nodes.find(targetId) == nodes.end()) {
        return;
    }
    
    FlowChartConnection conn(connId, sourceId, targetId);
    conn.style = style;
    conn.arrowStyle = arrowStyle;
    
    connections.push_back(conn);
    
    if (onConnectionCreated) {
        onConnectionCreated(sourceId, targetId);
    }
    
    RequestRedraw();
}

void UltraCanvasFlowChart::RemoveConnection(const std::string& connId) {
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&connId](const FlowChartConnection& conn) {
                return conn.id == connId;
            }),
        connections.end()
    );
    
    if (selectedConnectionId == connId) {
        selectedConnectionId.clear();
    }
    
    RequestRedraw();
}

void UltraCanvasFlowChart::SetConnectionLabel(const std::string& connId, const std::string& label) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            conn.label = label;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasFlowChart::SetConnectionStyle(const std::string& connId, FlowChartConnectionStyle style) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            conn.style = style;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasFlowChart::SetConnectionLineStyle(const std::string& connId, FlowChartLineStyle lineStyle) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            conn.lineStyle = lineStyle;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasFlowChart::SetConnectionColor(const std::string& connId, const Color& color) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            conn.lineColor = color;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasFlowChart::SetConnectionWidth(const std::string& connId, double width) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            conn.lineWidth = width;
            RequestRedraw();
            break;
        }
    }
}

FlowChartConnection* UltraCanvasFlowChart::GetConnection(const std::string& connId) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            return &conn;
        }
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasFlowChart::GetAllConnectionIds() const {
    std::vector<std::string> ids;
    ids.reserve(connections.size());
    for (const auto& conn : connections) {
        ids.push_back(conn.id);
    }
    return ids;
}

// =============================================================================
// VIEW CONTROL
// =============================================================================

void UltraCanvasFlowChart::SetZoomLevel(double zoom) {
    zoomLevel = std::clamp(zoom, 0.1, 10.0);
    RequestRedraw();
}

void UltraCanvasFlowChart::SetPanOffset(double x, double y) {
    panOffset.x = x;
    panOffset.y = y;
    RequestRedraw();
}

// =============================================================================
// GRID & BACKGROUND
// =============================================================================

void UltraCanvasFlowChart::SetBackgroundColor(const Color& color) {
    style.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasFlowChart::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    if (spacing > 0) {
        style.gridSpacing = spacing;
    }
    RequestRedraw();
}

void UltraCanvasFlowChart::SetGridColor(const Color& color) {
    style.gridColor = color;
    RequestRedraw();
}

void UltraCanvasFlowChart::SetSnapToGrid(bool enable) {
    snapToGrid = enable;
}

void UltraCanvasFlowChart::SetFontFamily(const std::string& fontFamily) {
    style.fontFamily = fontFamily;
    RequestRedraw();
}

void UltraCanvasFlowChart::SetFontSize(double size) {
    style.baseFontSize = size;
    RequestRedraw();
}

// =============================================================================
// STYLING & THEME
// =============================================================================

void UltraCanvasFlowChart::SetTheme(FlowChartTheme theme) {
    ApplyTheme(theme);
    RequestRedraw();
}

// =============================================================================
// NODE CREATION MODE
// =============================================================================

void UltraCanvasFlowChart::SetCreateNodeMode(bool enable) {
    isCreatingNode = enable;
    if (enable) {
        DeselectAll();
    }
}

void UltraCanvasFlowChart::SetPendingNodeShape(FlowChartShape shape) {
    pendingNodeShape = shape;
    isCreatingNode = true;
}

void UltraCanvasFlowChart::SetEditMode(EditMode mode) {
    currentMode = mode;
    isCreatingConnection = false;
    connectionSourceId.clear();
    
    if (onEditModeChanged) {
        onEditModeChanged(mode);
    }
    
    RequestRedraw();
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasFlowChart::SelectNode(const std::string& nodeId) {
    if (nodes.find(nodeId) == nodes.end()) return;
    
    DeselectAll();
    selectedNodeId = nodeId;
    nodes[nodeId].isSelected = true;
    
    if (onNodeClick) {
        onNodeClick(nodeId);
    }
    if (onNodeSelected) {
        onNodeSelected(nodeId);
    }
    
    RequestRedraw();
}

void UltraCanvasFlowChart::DeselectAll() {
    for (auto& pair : nodes) {
        pair.second.isSelected = false;
    }
    selectedNodeId.clear();
    selectedConnectionId.clear();
    for (auto& conn : connections) {
        conn.isSelected = false;
    }
    RequestRedraw();
}

void UltraCanvasFlowChart::Clear() {
    nodes.clear();
    connections.clear();
    selectedNodeId.clear();
    isCreatingConnection = false;
    connectionSourceId.clear();
    DeselectAll();
}

void UltraCanvasFlowChart::DeleteSelected() {
    if (!selectedNodeId.empty()) {
        RemoveNode(selectedNodeId);
        selectedNodeId.clear();
    }
}

// =============================================================================
// EVENT HANDLING
// =============================================================================

bool UltraCanvasFlowChart::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            return HandleMouseDown(event);
        case UCEventType::MouseUp:
            return HandleMouseUp(event);
        case UCEventType::MouseMove:
            return HandleMouseMove(event);
        case UCEventType::MouseWheel:
            return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick:
            return HandleDoubleClick(event);
        default:
            break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasFlowChart::HandleMouseDown(const UCEvent& event) {
    Rect2Di bounds = GetLocalBounds();
    Point2Di mousePos = event.pointer;
    
    if (event.button == UCMouseButton::Middle || event.button == UCMouseButton::Right) {
        isPanning = true;
        dragStartPos = mousePos;
        panStartOffset = panOffset;
        return true;
    }
    
    if (event.button == UCMouseButton::Left) {
        Point2Df worldPos = ScreenToWorld(mousePos);
        
        if (isCreatingNode) {
            std::string nodeId = "node_" + std::to_string(nextNodeId++);
            AddNode(nodeId, pendingNodeShape, "Node", worldPos.x - 60, worldPos.y - 30, 120, 60);
            SelectNode(nodeId);
            if (onNodeClick) {
                onNodeClick(nodeId);
            }
            return true;
        }
        
        if (currentMode == EditMode::CreateConnection) {
            std::string clickedNodeId = FindNodeAt(worldPos);
            if (!clickedNodeId.empty()) {
                if (!isCreatingConnection) {
                    connectionSourceId = clickedNodeId;
                    isCreatingConnection = true;
                    connectionEndX = worldPos.x;
                    connectionEndY = worldPos.y;
                } else {
                    if (clickedNodeId != connectionSourceId) {
                        std::string connId = "conn_" + std::to_string(nextConnId++);
                        AddConnection(connId, connectionSourceId, clickedNodeId);
                    }
                    isCreatingConnection = false;
                    connectionSourceId.clear();
                }
                RequestRedraw();
                return true;
            }
        }
        
        std::string clickedNodeId = FindNodeAt(worldPos);
        
        if (!clickedNodeId.empty()) {
            SelectNode(clickedNodeId);
            isDraggingNode = true;
            dragStartPos = mousePos;
            auto* node = GetNode(clickedNodeId);
            dragOffsetX = worldPos.x - node->x;
            dragOffsetY = worldPos.y - node->y;
            return true;
        }
        
        std::string clickedConnId = FindConnectionAt(worldPos);
        if (!clickedConnId.empty()) {
            selectedConnectionId = clickedConnId;
            auto* conn = GetConnection(clickedConnId);
            if (conn) conn->isSelected = true;
            if (onConnectionClick) {
                onConnectionClick(clickedConnId);
            }
            RequestRedraw();
            return true;
        }
        
        DeselectAll();
        return true;
    }
    
    return false;
}

bool UltraCanvasFlowChart::HandleMouseUp(const UCEvent& event) {
    if (event.button == UCMouseButton::Middle || event.button == UCMouseButton::Right) {
        isPanning = false;
        return true;
    }
    
    if (event.button == UCMouseButton::Left) {
        isDraggingNode = false;
        return true;
    }
    
    return false;
}

bool UltraCanvasFlowChart::HandleMouseMove(const UCEvent& event) {
    Rect2Di bounds = GetLocalBounds();
    Point2Di mousePos = event.pointer;
    
    if (isPanning) {
        panOffset.x = panStartOffset.x + (mousePos.x - dragStartPos.x);
        panOffset.y = panStartOffset.y + (mousePos.y - dragStartPos.y);
        RequestRedraw();
        return true;
    }
    
    Point2Df worldPos = ScreenToWorld(mousePos);
    
    if (isCreatingConnection) {
        connectionEndX = worldPos.x;
        connectionEndY = worldPos.y;
        RequestRedraw();
    }
    
    std::string newHoveredId = FindNodeAt(worldPos);
    bool changed = false;
    if (newHoveredId != hoveredNodeId) {
        hoveredNodeId = newHoveredId;
        changed = true;
    }
    
    for (auto& pair : nodes) {
        bool shouldHover = (pair.first == hoveredNodeId);
        if (pair.second.isHovered != shouldHover) {
            pair.second.isHovered = shouldHover;
            changed = true;
        }
    }
    
    if (changed) {
        RequestRedraw();
    }
    
    if (isDraggingNode && !selectedNodeId.empty()) {
        Point2Df currentWorldPos = ScreenToWorld(mousePos);
        
        double newX = currentWorldPos.x - dragOffsetX;
        double newY = currentWorldPos.y - dragOffsetY;
        
        if (style.showGrid) {
            newX = std::round(newX / style.gridSpacing) * style.gridSpacing;
            newY = std::round(newY / style.gridSpacing) * style.gridSpacing;
        }
        
        UpdateNodePosition(selectedNodeId, newX, newY);
        
        if (onNodeDragged) {
            std::string nodeId = selectedNodeId;
            onNodeDragged(nodeId, newX, newY);
        }
        
        return true;
    }
    
    return false;
}

bool UltraCanvasFlowChart::HandleMouseWheel(const UCEvent& event) {
    double zoomFactor = (event.wheelDelta > 0) ? 1.1f : 0.9f;
    Rect2Di bounds = GetLocalBounds();
    Point2Di mousePos = event.pointer;
    Point2Df worldBefore = ScreenToWorld(mousePos);
    
    double newZoom = zoomLevel * zoomFactor;
    newZoom = std::clamp(newZoom, 0.1, 10.0);
    
    if (newZoom != zoomLevel) {
        zoomLevel = newZoom;
        Point2Df worldAfter = ScreenToWorld(mousePos);
        panOffset.x += (worldAfter.x - worldBefore.x) * zoomLevel;
        panOffset.y += (worldAfter.y - worldBefore.y) * zoomLevel;
        RequestRedraw();
    }
    
    return true;
}

bool UltraCanvasFlowChart::HandleDoubleClick(const UCEvent& event) {
    if (event.button != UCMouseButton::Left) return false;
    
    Rect2Di bounds = GetLocalBounds();
    Point2Di mousePos = event.pointer;
    Point2Df worldPos = ScreenToWorld(mousePos);
    std::string clickedId = FindNodeAt(worldPos);
    
    if (!clickedId.empty()) {
        if (onNodeDoubleClick) {
            onNodeDoubleClick(clickedId);
        }
        return true;
    }
    
    return false;
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasFlowChart::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
    if (!ctx || !IsVisible()) return;
    
    Rect2Di bounds = GetLocalBounds();
    
    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(bounds);
    
    ctx->PushState();
    ctx->Translate(bounds.x + panOffset.x, bounds.y + panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);
    
    if (style.showGrid) {
        RenderGrid(ctx);
    }
    
    RenderConnections(ctx);
    RenderNodes(ctx);
    
    if (isCreatingConnection) {
        RenderConnectionPreview(ctx);
    }
    
    ctx->PopState();
}

void UltraCanvasFlowChart::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(0.5f);
    
    double spacing = style.gridSpacing;
    double width = static_cast<double>(bounds.width) / zoomLevel;
    double height = static_cast<double>(bounds.height) / zoomLevel;
    
    double startX = -panOffset.x / zoomLevel;
    double startY = -panOffset.y / zoomLevel;
    
    startX = std::floor(startX / spacing) * spacing;
    startY = std::floor(startY / spacing) * spacing;
    
    for (double x = startX; x < startX + width + spacing; x += spacing) {
        ctx->DrawLine({x, startY}, {x, startY + height + spacing});
    }
    
    for (double y = startY; y < startY + height + spacing; y += spacing) {
        ctx->DrawLine({startX, y}, {startX + width + spacing, y});
    }
}

void UltraCanvasFlowChart::RenderConnections(IRenderContext* ctx) {
    for (const auto& conn : connections) {
        RenderConnection(ctx, conn);
    }
}

void UltraCanvasFlowChart::RenderConnectionPreview(IRenderContext* ctx) {
    if (!isCreatingConnection || connectionSourceId.empty()) return;
    
    auto* sourceNode = GetNode(connectionSourceId);
    if (!sourceNode) return;
    
    double x1 = sourceNode->x + sourceNode->width;
    double y1 = sourceNode->y + sourceNode->height / 2;
    
    ctx->SetStrokePaint(Color(100, 100, 100, 180));
    ctx->SetStrokeWidth(2.0f);
    
    ctx->DrawLine({x1, y1}, {connectionEndX, connectionEndY});
}

void UltraCanvasFlowChart::RenderNodes(IRenderContext* ctx) {
    for (const auto& pair : nodes) {
        const FlowChartNode& node = pair.second;
        
        ctx->PushState();
        RenderNodeShape(ctx, node);
        ctx->PopState();
        
        ctx->PushState();
        RenderNodeText(ctx, node);
        ctx->PopState();
        
        if (node.isSelected) {
            RenderSelectionHighlight(ctx, node, style.selectionColor, style.selectionWidth);
        } else if (!hoveredNodeId.empty() && hoveredNodeId == node.id) {
            Color hoverColor(100, 180, 255, 150);
            RenderSelectionHighlight(ctx, node, hoverColor, 2.0f);
        }
    }
}

void UltraCanvasFlowChart::RenderNodeShape(IRenderContext* ctx, const FlowChartNode& node) {
    ctx->SetFillPaint(node.fillColor);
    ctx->SetStrokePaint(node.borderColor);
    ctx->SetStrokeWidth(node.borderWidth);
    
    double x = node.x;
    double y = node.y;
    double w = node.width;
    double h = node.height;
    
    switch (node.shape) {
        case FlowChartShape::Rectangle:
        case FlowChartShape::Process:
            ctx->DrawFilledRectangle({x,y,w,h}, node.fillColor, node.borderWidth, node.borderColor);
            break;
            
        case FlowChartShape::RoundedRectangle:
            ctx->DrawFilledRectangle({x,y,w,h}, node.fillColor, node.borderWidth, node.borderColor, std::min(w, h) * 0.15);
            break;
            
        case FlowChartShape::Oval:
        case FlowChartShape::Ellipse: {
            double cx = x + w / 2.0;
            double cy = y + h / 2.0;
            ctx->FillEllipse({cx - w/2.0f, cy - h/2.0f, w, h});
            ctx->DrawEllipse({cx - w/2.0f, cy - h/2.0f, w, h});
            break;
        }
            
        case FlowChartShape::Circle: {
            double cx = x + w / 2.0;
            double cy = y + h / 2.0;
            double r = std::min(w, h) / 2.0;
            ctx->FillCircle({cx, cy}, r);
            ctx->DrawCircle({cx, cy}, r);
            break;
        }
            
        case FlowChartShape::Diamond:
        case FlowChartShape::Decision: {
            double cx = x + w / 2.0f;
            double cy = y + h / 2.0f;
            std::vector<Point2Df> points = {
                {cx, y},
                {x + w, cy},
                {cx, y + h},
                {x, cy}
            };
            ctx->FillLinePath(points);
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLinePath(points, true);
            break;
        }
            
        case FlowChartShape::Parallelogram:
        case FlowChartShape::ManualInput: {
            double offset = w * 0.15f;
            std::vector<Point2Df> points = {
                {x + offset, y},
                {x + w, y},
                {x + w - offset, y + h},
                {x, y + h}
            };
            ctx->FillLinePath(points);
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLinePath(points, true);
            break;
        }
            
        case FlowChartShape::Hexagon: {
            double offset = w * 0.15f;
            std::vector<Point2Df> points = {
                {x + offset, y},
                {x + w - offset, y},
                {x + w, y + h / 2.0f},
                {x + w - offset, y + h},
                {x + offset, y + h},
                {x, y + h / 2.0f}
            };
            ctx->FillLinePath(points);
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLinePath(points, true);
            break;
        }
            
        case FlowChartShape::Triangle: {
            double cx = x + w / 2.0f;
            std::vector<Point2Df> points = {
                {cx, y},
                {x + w, y + h},
                {x, y + h}
            };
            ctx->FillLinePath(points);
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLinePath(points, true);
            break;
        }
            
        case FlowChartShape::Document: {
            double waveH = h * 0.12f;
            ctx->FillRectangle({x, y, w, h - waveH});
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawRectangle({x, y, w, h - waveH});
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLine({x, y + h - waveH}, {x + w, y + h - waveH});
            break;
        }
        
        case FlowChartShape::Database: {
            double eh = h * 0.15f;
            double cx = x + w / 2.0f;
            ctx->FillEllipse(Rect2Df(cx - w/2.0f, y, w, eh));
            ctx->FillRectangle(Rect2Df(x, y + eh/2.0f, w, h - eh));
            ctx->FillEllipse(Rect2Df(cx - w/2.0f, y + h - eh, w, eh));
            ctx->DrawEllipse(Rect2Df(cx - w/2.0f, y, w, eh));
            ctx->DrawEllipse(Rect2Df(cx - w/2.0f, y + h - eh, w, eh));
            ctx->DrawLine({x, y + eh}, {x, y + h - eh});
            ctx->DrawLine({x + w, y + eh}, {x + w, y + h - eh});
            break;
        }
        
        case FlowChartShape::Star: {
            double cx = x + w / 2.0f;
            double cy = y + h / 2.0f;
            double outerR = std::min(w, h) / 2.0f * 0.9f;
            double innerR = outerR * 0.4f;
            std::vector<Point2Df> points;
            for (int i = 0; i < 10; i++) {
                double angle = (3.14159f / 5.0f) * i - 3.14159f / 2;
                double r = (i % 2 == 0) ? outerR : innerR;
                points.push_back({cx + r * std::cos(angle), cy + r * std::sin(angle)});
            }
            ctx->FillLinePath(points);
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLinePath(points, true);
            break;
        }
        
        case FlowChartShape::Cloud: {
            double r1 = w * 0.25f;
            double r2 = w * 0.3f;
            double cy = y + h / 2.0f;
            ctx->FillEllipse(Rect2Df(x + r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->FillEllipse(Rect2Df(x + w/2.0f, y + h * 0.3f, r2 * 2, r2 * 2));
            ctx->FillEllipse(Rect2Df(x + w - r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->FillEllipse(Rect2Df(x + w/2.0f, y + h - r1, r1 * 2, r1 * 2));
            ctx->DrawEllipse(Rect2Df(x + r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->DrawEllipse(Rect2Df(x + w/2.0f, y + h * 0.3f, r2 * 2, r2 * 2));
            ctx->DrawEllipse(Rect2Df(x + w - r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->DrawEllipse(Rect2Df(x + w/2.0f, y + h - r1, r1 * 2, r1 * 2));
            break;
        }
        
        case FlowChartShape::StickyNote: {
            // The note occupies the full bbox [x, x+w] x [y, y+h]. The fold is
            // a small triangle clipped from the top-right corner, NOT extra
            // width stolen from the body. Body shape (clockwise from top-left):
            //
            //   x,y --------- x+w-foldSize, y
            //                  \\
            //                   \\
            //                    x+w, y+foldSize
            //                    |
            //                    x+w, y+h
            //   x,y+h -----------'
            //
            // This way the visual center of the note is x+w/2, matching every
            // other shape, and consumers can align it on the grid normally.
            double foldSize = std::min(w, h) * 0.15f;
            std::vector<Point2Df> body = {
                {x,            y},
                {x + w - foldSize, y},
                {x + w,        y + foldSize},
                {x + w,        y + h},
                {x,            y + h}
            };
            ctx->FillLinePath(body);
            ctx->DrawLinePath(body, true);
            
            // Fold triangle in the top-right corner, slightly darker.
            Color darkerFill = node.fillColor;
            darkerFill.r = static_cast<uint8_t>(darkerFill.r * 0.85f);
            darkerFill.g = static_cast<uint8_t>(darkerFill.g * 0.85f);
            darkerFill.b = static_cast<uint8_t>(darkerFill.b * 0.85f);
            ctx->SetFillPaint(darkerFill);
            std::vector<Point2Df> fold = {
                {x + w - foldSize, y},
                {x + w,            y + foldSize},
                {x + w - foldSize, y + foldSize}
            };
            ctx->FillLinePath(fold);
            ctx->SetFillPaint(node.fillColor);
            ctx->SetStrokePaint(node.borderColor);
            ctx->SetStrokeWidth(node.borderWidth);
            ctx->DrawLinePath(fold, true);
            break;
        }
        
        case FlowChartShape::Actor: {
            double cx = x + w / 2.0f;
            double headRadius = h * 0.12f;
            double headY = y + headRadius + 5;
            ctx->FillCircle({cx, headY}, headRadius);
            ctx->DrawCircle({cx, headY}, headRadius);
            double bodyTop = headY + headRadius;
            double bodyBottom = y + h * 0.7f;
            ctx->DrawLine({cx, bodyTop}, {cx, bodyBottom});
            double armY = bodyTop + (bodyBottom - bodyTop) * 0.3f;
            double armSpan = w * 0.4f;
            ctx->DrawLine({cx - armSpan, armY}, {cx + armSpan, armY});
            double legSpan = w * 0.3f;
            ctx->DrawLine({cx, bodyBottom}, {cx - legSpan, y + h});
            ctx->DrawLine({cx, bodyBottom}, {cx + legSpan, y + h});
            break;
        }
    }
}

void UltraCanvasFlowChart::RenderNodeText(IRenderContext* ctx, const FlowChartNode& node) {
    if (node.label.empty()) return;
    
    ctx->SetTextPaint(node.textColor);
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(node.fontSize);
    
    // Split on '\n' so multi-line labels (e.g. sticky notes) layout correctly.
    std::vector<std::string> lines;
    {
        size_t start = 0;
        for (size_t i = 0; i <= node.label.size(); ++i) {
            if (i == node.label.size() || node.label[i] == '\n') {
                lines.emplace_back(node.label.substr(start, i - start));
                start = i + 1;
            }
        }
    }
    
    Size2Di textDim = ctx->GetTextLineDimensions(lines.empty() ? node.label : lines[0]);
    int textH = textDim.height;

    double lineHeight = static_cast<double>(textH);
    double blockHeight = lineHeight * lines.size();

    // Top of the text block, vertically centered inside the node.
    // DrawText() in UltraCanvas takes Y as the TOP of the text bbox (matches
    // the pattern used in UltraCanvasButton: y = rect.y + (rect.h - textH)/2),
    // so we draw each line at its own top edge — no baseline offset needed.
    double topY = node.y + (node.height - blockHeight) * 0.5f;

    for (size_t i = 0; i < lines.size(); ++i) {
        Size2Di lineDim = ctx->GetTextLineDimensions(lines[i]);
        double lineX = node.x + (node.width - lineDim.width) * 0.5f;
        double lineY = topY + lineHeight * i;
        ctx->DrawText(lines[i], {lineX, lineY});
    }
}

void UltraCanvasFlowChart::RenderSelectionHighlight(IRenderContext* ctx, const FlowChartNode& node,
                                                     const Color& color, double width) {
    double padding = 4.0f;
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(width);
    ctx->DrawRectangle(Rect2Df(node.x - padding, node.y - padding,
                               node.width + padding * 2, node.height + padding * 2));
}

void UltraCanvasFlowChart::RenderConnection(IRenderContext* ctx, const FlowChartConnection& conn) {
    const auto* sourceNode = GetNode(conn.sourceNodeId);
    const auto* targetNode = GetNode(conn.targetNodeId);
    
    if (!sourceNode || !targetNode) return;
    
    // Endpoints depend on conn.style:
    //   * Straight: silhouette intersection (so a diagonal touches the actual
    //     border without a gap on Diamond/Oval/etc).
    //   * Orthogonal/Curved: midpoint of the cardinal side facing the other
    //     node, so the segment exits perpendicular to a clean axis.
    Point2Df sourceCenter = GetNodeCenter(*sourceNode);
    Point2Df targetCenter = GetNodeCenter(*targetNode);
    Point2Df start = GetConnectionPoint(*sourceNode, targetCenter, conn.style);
    Point2Df end   = GetConnectionPoint(*targetNode, sourceCenter, conn.style);
    
    // Cardinal sides (only meaningful for orthogonal/curved, but cheap to
    // compute always — Straight ignores them).
    CardinalSide sourceSide = GetCardinalSide(sourceCenter, targetCenter);
    CardinalSide targetSide = GetCardinalSide(targetCenter, sourceCenter);
    
    Color lineColor = (conn.isSelected || IsNodeSelected(conn.sourceNodeId) || IsNodeSelected(conn.targetNodeId))
                      ? style.selectionColor : conn.lineColor;
    
    ctx->SetStrokePaint(lineColor);
    ctx->SetStrokeWidth(conn.lineWidth);
    
    // Build waypoints for orthogonal connections (used by render, arrow,
    // and label so the three agree on the same path geometry).
    std::vector<Point2Df> orthogonalPath;
    
    switch (conn.style) {
        case FlowChartConnectionStyle::Straight:
            RenderStraightLine(ctx, start, end);
            break;
            
        case FlowChartConnectionStyle::Orthogonal:
            orthogonalPath = ComputeOrthogonalPath(start, end, sourceSide, targetSide,
                                                    conn.sourceNodeId, conn.targetNodeId);
            RenderOrthogonalLine(ctx, orthogonalPath);
            break;
            
        case FlowChartConnectionStyle::Curved:
            RenderCurvedLine(ctx, start, end);
            break;
    }
    
    // Arrow head: orientation comes from targetSide for orthogonal/curved
    // (the final segment is always perpendicular to that side), or the
    // straight start->end angle otherwise. The tip retreats along that
    // angle by borderWidth/2 + 1 px to touch the border cleanly.
    if (conn.arrowStyle == FlowChartArrowStyle::Arrow ||
        conn.arrowStyle == FlowChartArrowStyle::ArrowFilled ||
        conn.arrowStyle == FlowChartArrowStyle::Diamond) {
        std::vector<Point2Df> pathForAngle = orthogonalPath.empty()
            ? std::vector<Point2Df>{start, end}
            : orthogonalPath;
        double angle = ComputeIncomingAngle(pathForAngle, conn.style, targetSide);
        double retreat = targetNode->borderWidth * 0.5f + 1.0f;
        Point2Df adjustedTip(end.x - retreat * std::cos(angle),
                             end.y - retreat * std::sin(angle));
        if (conn.arrowStyle == FlowChartArrowStyle::Diamond) {
            RenderDiamondArrow(ctx, adjustedTip, angle, lineColor, 8.0f);
        } else {
            RenderArrowHead(ctx, adjustedTip, angle, lineColor, 10.0f);
        }
    }
    
    if (!conn.label.empty()) {
        Point2Df anchor;
        if (conn.style == FlowChartConnectionStyle::Orthogonal && !orthogonalPath.empty()) {
            anchor = ComputeOrthogonalLabelAnchor(orthogonalPath);
        } else {
            anchor = Point2Df((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
        }
        RenderConnectionLabel(ctx, conn, anchor);
    }
}

void UltraCanvasFlowChart::RenderStraightLine(IRenderContext* ctx, const Point2Df& start, const Point2Df& end) {
    ctx->DrawLine(start, end);
}

// Draws a polyline through the given waypoints. The path can have any
// number of segments, all of which should be axis-aligned.
void UltraCanvasFlowChart::RenderOrthogonalLine(IRenderContext* ctx,
                                                 const std::vector<Point2Df>& waypoints) {
    if (waypoints.size() < 2) return;
    for (size_t i = 1; i < waypoints.size(); ++i) {
        const Point2Df& a = waypoints[i - 1];
        const Point2Df& b = waypoints[i];
        ctx->DrawLine(a, b);
    }
}

// Cardinal-aware L-shape (mixed axes) or Z-shape (same axes) routing.
// This is the 2.1.4 behavior, now packaged as a waypoint generator and used
// either when there are no obstacles, or as a fallback when A* fails.
std::vector<Point2Df> UltraCanvasFlowChart::ComputeCardinalPath(
        const Point2Df& start, const Point2Df& end,
        CardinalSide sourceSide, CardinalSide targetSide) const {
    auto isHoriz = [](CardinalSide s) {
        return s == CardinalSide::Left || s == CardinalSide::Right;
    };
    bool srcH = isHoriz(sourceSide);
    bool tgtH = isHoriz(targetSide);
    
    std::vector<Point2Df> waypoints;
    waypoints.push_back(start);
    
    if (srcH != tgtH) {
        // L-shape: 1 corner.
        Point2Df corner = srcH ? Point2Df(end.x, start.y) : Point2Df(start.x, end.y);
        waypoints.push_back(corner);
    } else if (srcH) {
        // Z-shape on horizontal axis: midX vertical bend.
        double midX = (start.x + end.x) * 0.5f;
        waypoints.push_back(Point2Df(midX, start.y));
        waypoints.push_back(Point2Df(midX, end.y));
    } else {
        // Z-shape on vertical axis: midY horizontal bend.
        double midY = (start.y + end.y) * 0.5f;
        waypoints.push_back(Point2Df(start.x, midY));
        waypoints.push_back(Point2Df(end.x, midY));
    }
    
    waypoints.push_back(end);
    return waypoints;
}

// Returns true if any segment of `path` intersects the bbox of a node not
// in {sourceId, targetId}. Bbox uses a small inset so a path running exactly
// along the border is not considered a collision.
bool UltraCanvasFlowChart::PathHasObstacles(const std::vector<Point2Df>& path,
                                             const std::string& sourceId,
                                             const std::string& targetId) const {
    if (path.size() < 2) return false;
    const double inset = 1.0f;  // tolerance: a line grazing the border is OK
    
    for (const auto& kv : nodes) {
        const std::string& nid = kv.first;
        if (nid == sourceId || nid == targetId) continue;
        const FlowChartNode& n = kv.second;
        double l = n.x + inset, r = n.x + n.width - inset;
        double t = n.y + inset, b = n.y + n.height - inset;
        
        for (size_t i = 1; i < path.size(); ++i) {
            const Point2Df& a = path[i - 1];
            const Point2Df& c = path[i];
            // All segments are axis-aligned by construction.
            if (std::abs(a.y - c.y) < 0.5f) {
                // Horizontal segment at y = a.y.
                double y = a.y;
                if (y < t || y > b) continue;
                double xMin = std::min(a.x, c.x);
                double xMax = std::max(a.x, c.x);
                if (xMax >= l && xMin <= r) return true;
            } else if (std::abs(a.x - c.x) < 0.5f) {
                // Vertical segment at x = a.x.
                double x = a.x;
                if (x < l || x > r) continue;
                double yMin = std::min(a.y, c.y);
                double yMax = std::max(a.y, c.y);
                if (yMax >= t && yMin <= b) return true;
            }
        }
    }
    return false;
}

namespace {
    // A* node state. Coordinates are grid-cell indices, not pixels.
    struct AStarNode {
        int x, y;
        int dirFromParent;  // 0=N, 1=E, 2=S, 3=W, -1=start (no parent)
        double g;            // cost from start
        double f;            // g + heuristic
        int parentIdx;      // index into the visited-nodes vector, -1 if start
    };
    struct AStarOpenEntry {
        double f;
        int idx;
        bool operator<(const AStarOpenEntry& o) const { return f > o.f; }  // min-heap
    };
    inline uint64_t PackCellDir(int x, int y, int d) {
        // (x, y, dir) packed into a single key. x,y < 65536, d in [0,3].
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32)
             | (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 4)
             | static_cast<uint64_t>(d & 0xF);
    }
}

// A* over the chart grid. Path is built in cell space, then post-processed
// into pixel waypoints. The first cell sits next to the source endpoint
// (in the direction of sourceSide); the last cell sits next to the target.
// This guarantees the path leaves perpendicular to the source side and
// arrives perpendicular to the target side. Returns empty on failure.
std::vector<Point2Df> UltraCanvasFlowChart::RouteAStar(
        const Point2Df& start, const Point2Df& end,
        CardinalSide sourceSide, CardinalSide targetSide,
        const std::string& sourceId, const std::string& targetId) {
    const double gridSize = style.gridSpacing > 0.0f ? style.gridSpacing : 20.0f;
    Rect2Di chartBounds = GetBounds();
    
    // Grid extent in world coordinates: [0, chartBounds.width] x [0, chartBounds.height].
    int gridW = std::max(2, static_cast<int>(std::ceil(chartBounds.width  / gridSize)));
    int gridH = std::max(2, static_cast<int>(std::ceil(chartBounds.height / gridSize)));
    
    // Build obstacle map: a cell is blocked if its center sits inside any
    // node's bbox (with 1-cell padding so paths don't graze borders).
    auto cellBlocked = [&](int cx, int cy) -> bool {
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return true;
        double wx = (cx + 0.5f) * gridSize;
        double wy = (cy + 0.5f) * gridSize;
        for (const auto& kv : nodes) {
            if (kv.first == sourceId || kv.first == targetId) continue;
            const FlowChartNode& n = kv.second;
            double pad = gridSize * 0.5f;
            if (wx >= n.x - pad && wx <= n.x + n.width + pad &&
                wy >= n.y - pad && wy <= n.y + n.height + pad) {
                return true;
            }
        }
        return false;
    };
    
    // Map cardinal side to (dx, dy) step offset for "one cell outside the node".
    auto sideOffset = [](CardinalSide s) -> std::pair<int,int> {
        switch (s) {
            case CardinalSide::Top:    return { 0, -1};
            case CardinalSide::Right:  return { 1,  0};
            case CardinalSide::Bottom: return { 0,  1};
            case CardinalSide::Left:   return {-1,  0};
        }
        return {0, 0};
    };
    auto worldToCell = [&](const Point2Df& p) -> std::pair<int,int> {
        return {
            static_cast<int>(std::floor(p.x / gridSize)),
            static_cast<int>(std::floor(p.y / gridSize))
        };
    };
    
    auto srcCell = worldToCell(start);
    auto tgtCell = worldToCell(end);
    auto srcOff  = sideOffset(sourceSide);
    auto tgtOff  = sideOffset(targetSide);
    
    // Step ONE cell outside the source/target so the start/goal of A* sit in
    // free space (the cells inside the source and target nodes are blocked
    // for everyone else, but we're using them as anchors).
    int sx = srcCell.first  + srcOff.first;
    int sy = srcCell.second + srcOff.second;
    int gx = tgtCell.first  + tgtOff.first;
    int gy = tgtCell.second + tgtOff.second;
    
    if (cellBlocked(sx, sy) || cellBlocked(gx, gy)) {
        return {};  // Anchor cells themselves are blocked: A* cannot start.
    }
    
    // Initial direction the path must face when leaving the source. This
    // forces the first segment to be perpendicular to sourceSide.
    auto sideDir = [](CardinalSide s) -> int {
        switch (s) {
            case CardinalSide::Top:    return 0;  // moving north
            case CardinalSide::Right:  return 1;  // moving east
            case CardinalSide::Bottom: return 2;
            case CardinalSide::Left:   return 3;
        }
        return 1;
    };
    int initialDir = sideDir(sourceSide);
    
    // 4-connected grid moves: dx, dy per direction.
    static const int DX[4] = { 0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1,  0};
    
    // The goal must be reached coming from a direction perpendicular to
    // targetSide so the last segment enters the target node head-on. Top
    // side -> approach moving south (dir=2). Right -> approach moving west
    // (dir=3). Etc.
    auto requiredApproachDir = [](CardinalSide s) -> int {
        switch (s) {
            case CardinalSide::Top:    return 2;  // approach by moving down (north->south)
            case CardinalSide::Right:  return 3;  // approach by moving left
            case CardinalSide::Bottom: return 0;  // approach by moving up
            case CardinalSide::Left:   return 1;  // approach by moving right
        }
        return 0;
    };
    int goalDir = requiredApproachDir(targetSide);
    
    auto manhattan = [&](int x, int y) -> double {
        return static_cast<double>(std::abs(x - gx) + std::abs(y - gy));
    };
    
    std::vector<AStarNode> visited;
    visited.reserve(256);
    std::priority_queue<AStarOpenEntry> open;
    std::unordered_map<uint64_t, double> bestG;
    
    AStarNode startNode{sx, sy, initialDir, 0.0f, manhattan(sx, sy), -1};
    visited.push_back(startNode);
    open.push({startNode.f, 0});
    bestG[PackCellDir(sx, sy, initialDir)] = 0.0f;
    
    constexpr double TURN_PENALTY = 5.0f;
    constexpr int MAX_EXPANSIONS = 20000;
    int expansions = 0;
    int goalIdx = -1;
    
    while (!open.empty() && expansions < MAX_EXPANSIONS) {
        AStarOpenEntry top = open.top();
        open.pop();
        ++expansions;
        
        const AStarNode& cur = visited[top.idx];
        // Goal: cell matches AND we arrived facing the right way.
        if (cur.x == gx && cur.y == gy && cur.dirFromParent == goalDir) {
            goalIdx = top.idx;
            break;
        }
        
        // Stale entry (a better g was already found): skip.
        uint64_t curKey = PackCellDir(cur.x, cur.y, cur.dirFromParent);
        auto itG = bestG.find(curKey);
        if (itG != bestG.end() && cur.g > itG->second + 0.001f) continue;
        
        for (int d = 0; d < 4; ++d) {
            int nx = cur.x + DX[d];
            int ny = cur.y + DY[d];
            if (cellBlocked(nx, ny)) continue;
            
            double stepCost = 1.0f;
            if (cur.dirFromParent != -1 && d != cur.dirFromParent) {
                stepCost += TURN_PENALTY;
            }
            double ng = cur.g + stepCost;
            uint64_t nkey = PackCellDir(nx, ny, d);
            auto itB = bestG.find(nkey);
            if (itB != bestG.end() && ng >= itB->second - 0.001f) continue;
            bestG[nkey] = ng;
            
            int parentIdx = top.idx;
            AStarNode nn{nx, ny, d, ng, ng + manhattan(nx, ny), parentIdx};
            visited.push_back(nn);
            open.push({nn.f, static_cast<int>(visited.size()) - 1});
        }
    }
    
    if (goalIdx < 0) return {};
    
    // Reconstruct cell path (goal -> start -> reverse).
    std::vector<std::pair<int,int>> cellPath;
    int idx = goalIdx;
    while (idx != -1) {
        const AStarNode& n = visited[idx];
        cellPath.emplace_back(n.x, n.y);
        idx = n.parentIdx;
    }
    std::reverse(cellPath.begin(), cellPath.end());
    
    // Convert to world waypoints (cell centers), keeping only corners
    // (where direction changes). This collapses long straight runs.
    std::vector<Point2Df> raw;
    raw.reserve(cellPath.size());
    for (auto& c : cellPath) {
        raw.push_back(Point2Df((c.first + 0.5f) * gridSize, (c.second + 0.5f) * gridSize));
    }
    std::vector<Point2Df> simplified;
    simplified.push_back(raw.front());
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        // Keep waypoint i if it's a corner (direction changes here).
        double dx1 = raw[i].x - raw[i-1].x, dy1 = raw[i].y - raw[i-1].y;
        double dx2 = raw[i+1].x - raw[i].x, dy2 = raw[i+1].y - raw[i].y;
        bool sameDir = (std::abs(dx1) < 0.5f) == (std::abs(dx2) < 0.5f) &&
                       ((dx1 == 0.0f) == (dx2 == 0.0f));
        // Direction is "same" iff both are vertical or both are horizontal
        // AND have the same sign.
        bool h1 = std::abs(dx1) > 0.5f, h2 = std::abs(dx2) > 0.5f;
        bool sameSign = (h1 ? (dx1 * dx2 > 0.0f) : (dy1 * dy2 > 0.0f));
        if (h1 == h2 && sameSign) {
            // Collinear: drop this waypoint.
            (void)sameDir;
            continue;
        }
        simplified.push_back(raw[i]);
    }
    if (raw.size() > 1) simplified.push_back(raw.back());
    
    // Anchor first/last waypoints to the exact cardinal endpoints. The cells
    // adjacent to source/target may sit a few px off the endpoint axis;
    // insert bridge waypoints to keep the path strictly orthogonal.
    auto isHoriz = [](CardinalSide s) {
        return s == CardinalSide::Left || s == CardinalSide::Right;
    };
    
    std::vector<Point2Df> finalPath;
    finalPath.reserve(simplified.size() + 4);
    finalPath.push_back(start);
    
    // Bridge from `start` to simplified[0]: source side dictates which axis
    // the first segment uses.
    if (!simplified.empty()) {
        const Point2Df& first = simplified.front();
        if (isHoriz(sourceSide)) {
            // First segment is horizontal: keep start.y, snap to first.x.
            if (std::abs(first.x - start.x) > 0.5f) {
                finalPath.push_back(Point2Df(first.x, start.y));
            }
            if (std::abs(first.y - start.y) > 0.5f) {
                finalPath.push_back(first);
            }
        } else {
            // First segment is vertical: keep start.x, snap to first.y.
            if (std::abs(first.y - start.y) > 0.5f) {
                finalPath.push_back(Point2Df(start.x, first.y));
            }
            if (std::abs(first.x - start.x) > 0.5f) {
                finalPath.push_back(first);
            }
        }
    }
    
    // Middle waypoints (skip first/last, they're handled by bridges).
    for (size_t i = 1; i + 1 < simplified.size(); ++i) {
        finalPath.push_back(simplified[i]);
    }
    
    // Bridge from simplified.back() to `end`.
    if (simplified.size() > 1) {
        const Point2Df& last = simplified.back();
        if (isHoriz(targetSide)) {
            // Last segment is horizontal: keep end.y, come from last.x.
            if (std::abs(last.y - end.y) > 0.5f) {
                finalPath.push_back(Point2Df(last.x, end.y));
            }
        } else {
            // Last segment is vertical: keep end.x, come from last.y.
            if (std::abs(last.x - end.x) > 0.5f) {
                finalPath.push_back(Point2Df(end.x, last.y));
            }
        }
    }
    
    finalPath.push_back(end);
    
    // De-duplicate consecutive identical waypoints.
    std::vector<Point2Df> dedup;
    dedup.reserve(finalPath.size());
    for (const auto& p : finalPath) {
        if (dedup.empty() ||
            std::abs(p.x - dedup.back().x) > 0.5f ||
            std::abs(p.y - dedup.back().y) > 0.5f) {
            dedup.push_back(p);
        }
    }
    return dedup;
}

// Top-level orthogonal routing entry. Tries the cheap cardinal path first;
// only invokes A* if the cheap path collides with another node. Fallback to
// cardinal if A* finds nothing.
std::vector<Point2Df> UltraCanvasFlowChart::ComputeOrthogonalPath(
        const Point2Df& start, const Point2Df& end,
        CardinalSide sourceSide, CardinalSide targetSide,
        const std::string& sourceId, const std::string& targetId) {
    auto cheap = ComputeCardinalPath(start, end, sourceSide, targetSide);
    if (!PathHasObstacles(cheap, sourceId, targetId)) {
        return cheap;
    }
    auto astar = RouteAStar(start, end, sourceSide, targetSide, sourceId, targetId);
    if (astar.empty()) {
        return cheap;  // silent fallback: visible connection > no connection
    }
    return astar;
}

void UltraCanvasFlowChart::RenderCurvedLine(IRenderContext* ctx, const Point2Df& start, const Point2Df& end) {
    double dx = end.x - start.x;
    
    if (std::abs(dx) < 10.0f) {
        ctx->DrawLine(start, end);
        return;
    }
    
    double cpOffset = std::abs(dx) * 0.4f;
    int segments = 20;
    
    for (int i = 0; i < segments; i++) {
        double t1 = static_cast<double>(i) / segments;
        double t2 = static_cast<double>(i + 1) / segments;
        
        double x1 = (1-t1)*(1-t1)*(1-t1)*start.x + 3*(1-t1)*(1-t1)*t1*(start.x+cpOffset) + 3*(1-t1)*t1*t1*(end.x-cpOffset) + t1*t1*t1*end.x;
        double y1 = (1-t1)*(1-t1)*(1-t1)*start.y + 3*(1-t1)*(1-t1)*t1*start.y + 3*(1-t1)*t1*t1*end.y + t1*t1*t1*end.y;
        
        double x2 = (1-t2)*(1-t2)*(1-t2)*start.x + 3*(1-t2)*(1-t2)*t2*(start.x+cpOffset) + 3*(1-t2)*t2*t2*(end.x-cpOffset) + t2*t2*t2*end.x;
        double y2 = (1-t2)*(1-t2)*(1-t2)*start.y + 3*(1-t2)*(1-t2)*t2*start.y + 3*(1-t2)*t2*t2*end.y + t2*t2*t2*end.y;
        
        ctx->DrawLine({x1, y1}, {x2, y2});
    }
}

void UltraCanvasFlowChart::RenderArrowHead(IRenderContext* ctx, const Point2Df& tip,
                                           double angle, const Color& color, double size) {
    const double arrowAngle = 0.5f;
    
    ctx->SetFillPaint(color);
    
    Point2Df p1(tip.x - size * std::cos(angle - arrowAngle),
                tip.y - size * std::sin(angle - arrowAngle));
    Point2Df p2(tip.x - size * std::cos(angle + arrowAngle),
                tip.y - size * std::sin(angle + arrowAngle));
    
    std::vector<Point2Df> points = {
        {tip.x, tip.y},
        {p1.x, p1.y},
        {p2.x, p2.y}
    };
    ctx->FillLinePath(points);
}

void UltraCanvasFlowChart::RenderDiamondArrow(IRenderContext* ctx, const Point2Df& tip,
                                              double angle, const Color& color, double size) {
    ctx->SetFillPaint(color);
    
    Point2Df p1(tip.x - size * 0.7f * std::cos(angle),
                tip.y - size * 0.7f * std::sin(angle));
    Point2Df p2(tip.x - size * std::cos(angle - 0.7f),
                tip.y - size * std::sin(angle - 0.7f));
    Point2Df p3(tip.x - size * 1.4f * std::cos(angle),
                tip.y - size * 1.4f * std::sin(angle));
    Point2Df p4(tip.x - size * std::cos(angle + 0.7f),
                tip.y - size * std::sin(angle + 0.7f));
    
    std::vector<Point2Df> points = {
        {tip.x, tip.y},
        {p1.x, p1.y},
        {p2.x, p2.y},
        {p3.x, p3.y},
        {p4.x, p4.y}
    };
    ctx->FillLinePath(points);
}

void UltraCanvasFlowChart::RenderConnectionLabel(IRenderContext* ctx, const FlowChartConnection& conn,
                                                 const Point2Df& anchor) {
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(10.0f);
    
    Size2Di labelDim = ctx->GetTextLineDimensions(conn.label);
    int labelW = labelDim.width;
    int labelH = labelDim.height;

    double boxX = anchor.x - labelW * 0.5f;
    double boxY = anchor.y - labelH * 0.5f;

    double padding = 3.0f;
    ctx->SetFillPaint(conn.labelBackgroundColor);
    ctx->FillRectangle(Rect2Df(boxX - padding, boxY - padding,
                               labelW + padding * 2, labelH + padding * 2));

    // DrawText() Y is the TOP of the text bbox in this framework
    // (see UltraCanvasButton text rendering pattern).
    ctx->SetTextPaint(conn.labelTextColor);
    ctx->DrawText(conn.label, {boxX, boxY});
}

// =============================================================================
// HIT TESTING
// =============================================================================

std::string UltraCanvasFlowChart::FindNodeAt(const Point2Df& pos) {
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (PointInNode(it->second, pos)) {
            return it->first;
        }
    }
    return "";
}

std::string UltraCanvasFlowChart::FindConnectionAt(const Point2Df& pos) {
    for (const auto& conn : connections) {
        const auto* sourceNode = GetNode(conn.sourceNodeId);
        const auto* targetNode = GetNode(conn.targetNodeId);
        
        if (!sourceNode || !targetNode) continue;
        
        Point2Df sourceCenter = GetNodeCenter(*sourceNode);
        Point2Df targetCenter = GetNodeCenter(*targetNode);
        Point2Df start = GetConnectionPoint(*sourceNode, targetCenter, conn.style);
        Point2Df end   = GetConnectionPoint(*targetNode, sourceCenter, conn.style);
        
        double dist = DistanceToLineSegment(pos, start, end);
        
        if (dist < 8.0f) {
            return conn.id;
        }
    }
    return "";
}

bool UltraCanvasFlowChart::PointInNode(const FlowChartNode& node, const Point2Df& pos) {
    double cx = node.x + node.width / 2.0f;
    double cy = node.y + node.height / 2.0f;
    
    switch (node.shape) {
        case FlowChartShape::Diamond:
        case FlowChartShape::Decision: {
            double dx = std::abs(pos.x - cx) / (node.width / 2.0f);
            double dy = std::abs(pos.y - cy) / (node.height / 2.0f);
            return (dx + dy) <= 1.0f;
        }
        
        case FlowChartShape::Circle: {
            double r = std::min(node.width, node.height) / 2.0f;
            double dx = pos.x - cx;
            double dy = pos.y - cy;
            return (dx * dx + dy * dy) <= (r * r);
        }
        
        case FlowChartShape::Oval:
        case FlowChartShape::Ellipse: {
            double dx = (pos.x - cx) / (node.width / 2.0f);
            double dy = (pos.y - cy) / (node.height / 2.0f);
            return (dx * dx + dy * dy) <= 1.0f;
        }
        
        default:
            return (pos.x >= node.x && pos.x <= node.x + node.width &&
                    pos.y >= node.y && pos.y <= node.y + node.height);
    }
}

double UltraCanvasFlowChart::DistanceToLineSegment(const Point2Df& point,
                                                   const Point2Df& lineStart,
                                                   const Point2Df& lineEnd) {
    double dx = lineEnd.x - lineStart.x;
    double dy = lineEnd.y - lineStart.y;
    double lengthSq = dx * dx + dy * dy;
    
    if (lengthSq < 0.001f) {
        double ddx = point.x - lineStart.x;
        double ddy = point.y - lineStart.y;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    
    double t = std::clamp(((point.x - lineStart.x) * dx + (point.y - lineStart.y) * dy) / lengthSq, 0.0, 1.0);
    
    double projX = lineStart.x + t * dx;
    double projY = lineStart.y + t * dy;
    
    double distX = point.x - projX;
    double distY = point.y - projY;
    
    return std::sqrt(distX * distX + distY * distY);
}

// =============================================================================
// UTILITY
// =============================================================================

Point2Df UltraCanvasFlowChart::GetNodeCenter(const FlowChartNode& node) const {
    return Point2Df(node.x + node.width * 0.5f, node.y + node.height * 0.5f);
}

// Picks the cardinal side of `nodeCenter` that faces `otherCenter`. Splits
// the plane around the node into 4 quadrants by the two diagonals; the side
// whose outward normal best matches the (other - this) direction wins.
UltraCanvasFlowChart::CardinalSide
UltraCanvasFlowChart::GetCardinalSide(const Point2Df& nodeCenter,
                                       const Point2Df& otherCenter) const {
    double dx = otherCenter.x - nodeCenter.x;
    double dy = otherCenter.y - nodeCenter.y;
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx >= 0.0f) ? CardinalSide::Right : CardinalSide::Left;
    } else {
        return (dy >= 0.0f) ? CardinalSide::Bottom : CardinalSide::Top;
    }
}

// Mid-point of the requested cardinal side of the node's bbox. For Diamond
// these correspond to the four rhombus vertices, which is exactly the visual
// edge users expect lines to attach to.
Point2Df UltraCanvasFlowChart::GetCardinalPoint(const FlowChartNode& node,
                                                CardinalSide side) const {
    double cx = node.x + node.width * 0.5f;
    double cy = node.y + node.height * 0.5f;
    switch (side) {
        case CardinalSide::Top:    return Point2Df(cx, node.y);
        case CardinalSide::Right:  return Point2Df(node.x + node.width, cy);
        case CardinalSide::Bottom: return Point2Df(cx, node.y + node.height);
        case CardinalSide::Left:   return Point2Df(node.x, cy);
    }
    return Point2Df(cx, cy);
}

// Returns the attach point on `node` for a connection coming from
// `otherCenter`. Behavior depends on `style`:
//   * Straight: silhouette intersection (ellipse/rhombus/bbox), so a
//     diagonal line touches the actual border without a gap.
//   * Orthogonal/Curved: midpoint of the cardinal side facing `otherCenter`,
//     so the line exits cleanly along an axis. For Diamond this lands on
//     the rhombus vertex (since the side midpoint of a diamond bbox IS a
//     vertex), keeping the visual semantics of the 2.1.0 fix.
Point2Df UltraCanvasFlowChart::GetConnectionPoint(const FlowChartNode& node,
                                                  const Point2Df& otherCenter,
                                                  FlowChartConnectionStyle style) const {
    double cx = node.x + node.width * 0.5f;
    double cy = node.y + node.height * 0.5f;
    double halfW = node.width * 0.5f;
    double halfH = node.height * 0.5f;
    
    double dx = otherCenter.x - cx;
    double dy = otherCenter.y - cy;
    
    // Degenerate: other node sits on top of us.
    if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
        return Point2Df(cx + halfW, cy);
    }
    
    // Orthogonal/Curved paths exit along an axis; use cardinal midpoints so
    // the segment leaves perpendicular to the chosen side.
    if (style == FlowChartConnectionStyle::Orthogonal ||
        style == FlowChartConnectionStyle::Curved) {
        return GetCardinalPoint(node, GetCardinalSide(Point2Df(cx, cy), otherCenter));
    }
    
    // Straight: shape-aware silhouette intersection.
    switch (node.shape) {
        case FlowChartShape::Diamond:
        case FlowChartShape::Decision: {
            double denom = std::abs(dx) / halfW + std::abs(dy) / halfH;
            if (denom < 0.001f) return Point2Df(cx + halfW, cy);
            double t = 1.0f / denom;
            return Point2Df(cx + t * dx, cy + t * dy);
        }
        case FlowChartShape::Circle:
        case FlowChartShape::Oval:
        case FlowChartShape::Ellipse: {
            double ndx = dx / halfW;
            double ndy = dy / halfH;
            double denom = std::sqrt(ndx * ndx + ndy * ndy);
            if (denom < 0.001f) return Point2Df(cx + halfW, cy);
            double t = 1.0f / denom;
            return Point2Df(cx + t * dx, cy + t * dy);
        }
        default: {
            double tx = (dx != 0.0f) ? halfW / std::abs(dx) : std::numeric_limits<double>::infinity();
            double ty = (dy != 0.0f) ? halfH / std::abs(dy) : std::numeric_limits<double>::infinity();
            double t = std::min(tx, ty);
            return Point2Df(cx + t * dx, cy + t * dy);
        }
    }
}

// Mirrors RenderOrthogonalLine: for a 3-segment path the label belongs on
// the *middle* (vertical) segment, not at (start+end)/2 which sits in empty
// space when start.y != end.y. For a degenerate single-segment path falls
// back to the geometric midpoint.
// Anchor for the orthogonal label: midpoint of the longest segment in the
// path. Avoids placing the label exactly on a corner. Works for any number
// of waypoints (L, Z, or A* paths).
Point2Df UltraCanvasFlowChart::ComputeOrthogonalLabelAnchor(
        const std::vector<Point2Df>& waypoints) const {
    if (waypoints.size() < 2) return Point2Df(0, 0);
    if (waypoints.size() == 2) {
        return Point2Df((waypoints[0].x + waypoints[1].x) * 0.5f,
                        (waypoints[0].y + waypoints[1].y) * 0.5f);
    }
    // Find the longest segment.
    size_t bestIdx = 0;
    double bestLen = 0.0f;
    for (size_t i = 1; i < waypoints.size(); ++i) {
        double dx = waypoints[i].x - waypoints[i-1].x;
        double dy = waypoints[i].y - waypoints[i-1].y;
        double len = std::abs(dx) + std::abs(dy);  // Manhattan; segments are axis-aligned
        if (len > bestLen) {
            bestLen = len;
            bestIdx = i;
        }
    }
    return Point2Df((waypoints[bestIdx-1].x + waypoints[bestIdx].x) * 0.5f,
                    (waypoints[bestIdx-1].y + waypoints[bestIdx].y) * 0.5f);
}

// Angle of the final segment of the path. For orthogonal/curved we still
// trust targetSide (always perpendicular to the entry edge). For straight,
// derive from the geometric segment so callers without a path can use this.
double UltraCanvasFlowChart::ComputeIncomingAngle(const std::vector<Point2Df>& waypoints,
                                                 FlowChartConnectionStyle s,
                                                 CardinalSide targetSide) const {
    if (s == FlowChartConnectionStyle::Orthogonal ||
        s == FlowChartConnectionStyle::Curved) {
        switch (targetSide) {
            case CardinalSide::Top:    return  3.14159265f * 0.5f;   // arriving going down
            case CardinalSide::Bottom: return -3.14159265f * 0.5f;   // arriving going up
            case CardinalSide::Left:   return  0.0f;                 // arriving going right
            case CardinalSide::Right:  return  3.14159265f;          // arriving going left
        }
    }
    if (waypoints.size() < 2) return 0.0f;
    const Point2Df& a = waypoints[waypoints.size() - 2];
    const Point2Df& b = waypoints.back();
    return std::atan2(b.y - a.y, b.x - a.x);
}

bool UltraCanvasFlowChart::IsNodeSelected(const std::string& nodeId) const {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? it->second.isSelected : false;
}

Point2Df UltraCanvasFlowChart::ScreenToWorld(const Point2Di& screenPos) const {
    double worldX = (screenPos.x - panOffset.x) / zoomLevel;
    double worldY = (screenPos.y - panOffset.y) / zoomLevel;
    return Point2Df(worldX, worldY);
}

Point2Df UltraCanvasFlowChart::ScreenToWorld(const Point2Df& screenPos) const {
    double worldX = (screenPos.x - panOffset.x) / zoomLevel;
    double worldY = (screenPos.y - panOffset.y) / zoomLevel;
    return Point2Df(worldX, worldY);
}

double UltraCanvasFlowChart::CalculateAngle(const Point2Df& from, const Point2Df& to) {
    return std::atan2(to.y - from.y, to.x - from.x);
}

std::string UltraCanvasFlowChart::GenerateUniqueId(const std::string& prefix) {
    static int counter = 0;
    std::ostringstream oss;
    oss << prefix << "_" << (++counter);
    return oss.str();
}

void UltraCanvasFlowChart::ApplyThemeColors(FlowChartNode& node, FlowChartTheme t) {
    switch (t) {
        case FlowChartTheme::Professional:
            node.fillColor = Color(255, 255, 255, 255);
            node.borderColor = Color(100, 100, 100, 255);
            node.borderWidth = 2.0f;
            break;
        case FlowChartTheme::Colorful: {
            size_t colorIndex = nodes.size() % 6;
            Color colors[] = {
                Color(200, 220, 255, 255),
                Color(220, 255, 220, 255),
                Color(255, 220, 220, 255),
                Color(255, 255, 200, 255),
                Color(220, 200, 255, 255),
                Color(200, 255, 255, 255)
            };
            node.fillColor = colors[colorIndex];
            node.borderColor = Color(80, 80, 80, 255);
            break;
        }
        case FlowChartTheme::Minimal:
            node.fillColor = Color(255, 255, 255, 255);
            node.borderColor = Color(0, 0, 0, 255);
            node.borderWidth = 1.0f;
            break;
        case FlowChartTheme::Dark:
            node.fillColor = Color(50, 50, 55, 255);
            node.borderColor = Color(120, 120, 130, 255);
            node.textColor = Color(230, 230, 230, 255);
            node.borderWidth = 1.5f;
            break;
        default:
            break;
    }
}

// =============================================================================
// THEME
// =============================================================================

void UltraCanvasFlowChart::ApplyTheme(FlowChartTheme t) {
    theme = t;
    
    switch (t) {
        case FlowChartTheme::Default:
            ApplyDefaultTheme();
            break;
        case FlowChartTheme::Professional:
            ApplyProfessionalTheme();
            break;
        case FlowChartTheme::Colorful:
            ApplyColorfulTheme();
            break;
        case FlowChartTheme::Minimal:
            ApplyMinimalTheme();
            break;
        case FlowChartTheme::Dark:
            ApplyDarkTheme();
            break;
    }
    
    for (auto& pair : nodes) {
        ApplyThemeColors(pair.second, t);
    }
}

void UltraCanvasFlowChart::ApplyDefaultTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 12.0f;
    style.backgroundColor = Color(250, 250, 250, 255);
    style.gridColor = Color(230, 230, 230, 255);
    style.showGrid = true;
    style.gridSpacing = 20.0f;
    style.selectionColor = Color(0, 120, 215, 255);
    style.selectionWidth = 2.0f;
}

void UltraCanvasFlowChart::ApplyProfessionalTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 12.0f;
    style.backgroundColor = Color(248, 249, 250, 255);
    style.gridColor = Color(220, 225, 230, 255);
    style.showGrid = true;
    style.gridSpacing = 20.0f;
    style.selectionColor = Color(0, 123, 255, 255);
    style.selectionWidth = 2.5f;
}

void UltraCanvasFlowChart::ApplyColorfulTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 12.0f;
    style.backgroundColor = Color(255, 255, 255, 255);
    style.gridColor = Color(240, 240, 240, 255);
    style.showGrid = false;
    style.gridSpacing = 20.0f;
    style.selectionColor = Color(255, 100, 0, 255);
    style.selectionWidth = 2.0f;
}

void UltraCanvasFlowChart::ApplyMinimalTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 12.0f;
    style.backgroundColor = Color(255, 255, 255, 255);
    style.gridColor = Color(200, 200, 200, 255);
    style.showGrid = false;
    style.gridSpacing = 20.0f;
    style.selectionColor = Color(0, 0, 0, 255);
    style.selectionWidth = 1.5f;
}

void UltraCanvasFlowChart::ApplyDarkTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 12.0f;
    style.backgroundColor = Color(30, 30, 35, 255);
    style.gridColor = Color(60, 60, 70, 255);
    style.showGrid = true;
    style.gridSpacing = 20.0f;
    style.selectionColor = Color(100, 180, 255, 255);
    style.selectionWidth = 2.0f;
}

double UltraCanvasFlowChart::SnapToGrid(double value) {
    return std::round(value / style.gridSpacing) * style.gridSpacing;
}

std::string UltraCanvasFlowChart::GenerateNodeId() {
    return "node_" + std::to_string(nextNodeId++);
}

std::string UltraCanvasFlowChart::GenerateConnectionId() {
    return "conn_" + std::to_string(nextConnId++);
}

void UltraCanvasFlowChart::GetConnectionPoints(const std::string& sourceId, const std::string& targetId,
                                              double& x1, double& y1, double& x2, double& y2) {
    auto* source = GetNode(sourceId);
    auto* target = GetNode(targetId);
    
    if (!source || !target) {
        x1 = y1 = x2 = y2 = 0;
        return;
    }
    
    double sc_x = source->x + source->width / 2;
    double sc_y = source->y + source->height / 2;
    double tc_x = target->x + target->width / 2;
    double tc_y = target->y + target->height / 2;
    
    double dx = tc_x - sc_x;
    double dy = tc_y - sc_y;
    double angle = std::atan2(dy, dx);
    
    if (angle >= -3.14159f/4 && angle < 3.14159f/4) {
        x1 = source->x + source->width;
        y1 = sc_y;
    } else if (angle >= 3.14159f/4 && angle < 3*3.14159f/4) {
        x1 = sc_x;
        y1 = source->y + source->height;
    } else if (angle >= -3*3.14159f/4 && angle < -3.14159f/4) {
        x1 = sc_x;
        y1 = source->y;
    } else {
        x1 = source->x;
        y1 = sc_y;
    }
    
    double target_angle = angle + 3.14159f;
    if (target_angle > 3.14159f) target_angle -= 2 * 3.14159f;
    
    if (target_angle >= -3.14159f/4 && target_angle < 3.14159f/4) {
        x2 = target->x + target->width;
        y2 = tc_y;
    } else if (target_angle >= 3.14159f/4 && target_angle < 3*3.14159f/4) {
        x2 = tc_x;
        y2 = target->y + target->height;
    } else if (target_angle >= -3*3.14159f/4 && target_angle < -3.14159f/4) {
        x2 = tc_x;
        y2 = target->y;
    } else {
        x2 = target->x;
        y2 = tc_y;
    }
}

} // namespace UltraCanvas