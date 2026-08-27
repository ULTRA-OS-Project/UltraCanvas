// Plugins/Diagrams/UltraCanvasFlowChart.cpp
// Interactive Flow Chart diagram component implementation
// Version: 2.3.0
// Last Modified: 2026-07-30
//
// Changelog:
//   2.3.0 - Routing extracted to UltraCanvasDiagramRouter (see
//           Plugins/Diagrams/UltraCanvasDiagramRouting.cpp). The algorithms
//           below are unchanged; this file now only adapts the chart's node
//           map into the router's obstacle list and forwards. Removed:
//           ComputeCardinalPath, RouteAStar, PathHasObstacles and the
//           file-local A* helpers.
//   2.2.0 - Obstacle-aware orthogonal routing via A*:
//           ComputeOrthogonalPath() is the new entry point for orthogonal
//           routing. It first tries the cheap cardinal L/Z path; if that
//           collides with any non-endpoint node, it runs A* over the grid
//           with 4-connected movement, turn penalty (5x straight cost), and
//           Manhattan heuristic. Source/target nodes are excluded from the
//           obstacle set so the line can leave/enter them. The first and
//           last waypoints are anchored at the exact cardinal endpoints,
//           with bridge waypoints inserted to keep all segments orthogonal.
//           Path model changed from "L or Z" to "vector<Point2Dd> waypoints";
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
        Point2Dd worldPos = ScreenToWorld(mousePos);
        
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
    
    Point2Dd worldPos = ScreenToWorld(mousePos);
    
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
        Point2Dd currentWorldPos = ScreenToWorld(mousePos);
        
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

// One zoom step about the cursor: the world point under it stays put. A wheel
// notch is eased in as a run of these (UltraCanvasSmoothZoom), and applying them
// in a row about the same cursor is exactly applying their product once.
void UltraCanvasFlowChart::ApplyZoomFactorAtCursor(double factor,
                                                   const Point2Di& cursor) {
    Point2Dd worldBefore = ScreenToWorld(cursor);
    double newZoom = std::clamp(zoomLevel * factor, 0.1, 10.0);
    if (newZoom == zoomLevel) return;

    zoomLevel = newZoom;
    Point2Dd worldAfter = ScreenToWorld(cursor);
    panOffset.x += (worldAfter.x - worldBefore.x) * zoomLevel;
    panOffset.y += (worldAfter.y - worldBefore.y) * zoomLevel;
}

bool UltraCanvasFlowChart::HandleMouseWheel(const UCEvent& event) {
    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                      [this] { RequestRedraw(); });
    }
    zoomCursor = event.pointer;
    zoomAnim.ZoomBy((event.wheelDelta > 0) ? 1.1 : 0.9, zoomLevel, 0.1, 10.0);
    return true;
}

bool UltraCanvasFlowChart::HandleDoubleClick(const UCEvent& event) {
    if (event.button != UCMouseButton::Left) return false;
    
    Rect2Di bounds = GetLocalBounds();
    Point2Di mousePos = event.pointer;
    Point2Dd worldPos = ScreenToWorld(mousePos);
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

void UltraCanvasFlowChart::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;
    
    Rect2Di bounds = GetLocalBounds();
    
    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(bounds);
    
    ctx->PushState();
    ctx->Translate(finalBounds.x + panOffset.x, finalBounds.y + panOffset.y);
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
    double width = static_cast<double>(finalBounds.width) / zoomLevel;
    double height = static_cast<double>(finalBounds.height) / zoomLevel;
    
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
            std::vector<Point2Dd> points = {
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
            std::vector<Point2Dd> points = {
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
            std::vector<Point2Dd> points = {
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
            std::vector<Point2Dd> points = {
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
            ctx->FillEllipse(Rect2Dd(cx - w/2.0f, y, w, eh));
            ctx->FillRectangle(Rect2Dd(x, y + eh/2.0f, w, h - eh));
            ctx->FillEllipse(Rect2Dd(cx - w/2.0f, y + h - eh, w, eh));
            ctx->DrawEllipse(Rect2Dd(cx - w/2.0f, y, w, eh));
            ctx->DrawEllipse(Rect2Dd(cx - w/2.0f, y + h - eh, w, eh));
            ctx->DrawLine({x, y + eh}, {x, y + h - eh});
            ctx->DrawLine({x + w, y + eh}, {x + w, y + h - eh});
            break;
        }
        
        case FlowChartShape::Star: {
            double cx = x + w / 2.0f;
            double cy = y + h / 2.0f;
            double outerR = std::min(w, h) / 2.0f * 0.9f;
            double innerR = outerR * 0.4f;
            std::vector<Point2Dd> points;
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
            ctx->FillEllipse(Rect2Dd(x + r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->FillEllipse(Rect2Dd(x + w/2.0f, y + h * 0.3f, r2 * 2, r2 * 2));
            ctx->FillEllipse(Rect2Dd(x + w - r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->FillEllipse(Rect2Dd(x + w/2.0f, y + h - r1, r1 * 2, r1 * 2));
            ctx->DrawEllipse(Rect2Dd(x + r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->DrawEllipse(Rect2Dd(x + w/2.0f, y + h * 0.3f, r2 * 2, r2 * 2));
            ctx->DrawEllipse(Rect2Dd(x + w - r1, cy - r1/2, r1 * 2, r1 * 2));
            ctx->DrawEllipse(Rect2Dd(x + w/2.0f, y + h - r1, r1 * 2, r1 * 2));
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
            std::vector<Point2Dd> body = {
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
            std::vector<Point2Dd> fold = {
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
    ctx->DrawRectangle(Rect2Dd(node.x - padding, node.y - padding,
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
    Point2Dd sourceCenter = GetNodeCenter(*sourceNode);
    Point2Dd targetCenter = GetNodeCenter(*targetNode);
    Point2Dd start = GetConnectionPoint(*sourceNode, targetCenter, conn.style);
    Point2Dd end   = GetConnectionPoint(*targetNode, sourceCenter, conn.style);
    
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
    std::vector<Point2Dd> orthogonalPath;
    
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
        std::vector<Point2Dd> pathForAngle = orthogonalPath.empty()
            ? std::vector<Point2Dd>{start, end}
            : orthogonalPath;
        double angle = ComputeIncomingAngle(pathForAngle, conn.style, targetSide);
        double retreat = targetNode->borderWidth * 0.5f + 1.0f;
        Point2Dd adjustedTip(end.x - retreat * std::cos(angle),
                             end.y - retreat * std::sin(angle));
        if (conn.arrowStyle == FlowChartArrowStyle::Diamond) {
            RenderDiamondArrow(ctx, adjustedTip, angle, lineColor, 8.0f);
        } else {
            RenderArrowHead(ctx, adjustedTip, angle, lineColor, 10.0f);
        }
    }
    
    if (!conn.label.empty()) {
        Point2Dd anchor;
        if (conn.style == FlowChartConnectionStyle::Orthogonal && !orthogonalPath.empty()) {
            anchor = ComputeOrthogonalLabelAnchor(orthogonalPath);
        } else {
            anchor = Point2Dd((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
        }
        RenderConnectionLabel(ctx, conn, anchor);
    }
}

void UltraCanvasFlowChart::RenderStraightLine(IRenderContext* ctx, const Point2Dd& start, const Point2Dd& end) {
    ctx->DrawLine(start, end);
}

// Draws a polyline through the given waypoints. The path can have any
// number of segments, all of which should be axis-aligned.
void UltraCanvasFlowChart::RenderOrthogonalLine(IRenderContext* ctx,
                                                 const std::vector<Point2Dd>& waypoints) {
    if (waypoints.size() < 2) return;
    for (size_t i = 1; i < waypoints.size(); ++i) {
        const Point2Dd& a = waypoints[i - 1];
        const Point2Dd& b = waypoints[i];
        ctx->DrawLine(a, b);
    }
}

// Builds the obstacle list handed to the shared router: every node except
// the two the connection attaches to (a path must be free to leave the
// source and enter the target).
std::vector<DiagramObstacle> UltraCanvasFlowChart::CollectRoutingObstacles(
        const std::string& sourceId, const std::string& targetId) const {
    std::vector<DiagramObstacle> obstacles;
    obstacles.reserve(nodes.size());
    for (const auto& kv : nodes) {
        if (kv.first == sourceId || kv.first == targetId) continue;
        const FlowChartNode& n = kv.second;
        obstacles.emplace_back(n.x, n.y, n.width, n.height);
    }
    return obstacles;
}

// Router options from the chart's own settings. The A* grid uses the chart's
// visual grid spacing (falling back to 20px) and spans the element bounds,
// exactly as the in-class router did before 2.3.0.
DiagramRoutingOptions UltraCanvasFlowChart::BuildRoutingOptions() const {
    DiagramRoutingOptions options;
    options.gridSize = style.gridSpacing > 0.0f ? style.gridSpacing : 20.0f;
    Rect2Di chartBounds = GetBounds();
    options.routingArea = Rect2Dd(0.0, 0.0,
                                  static_cast<double>(chartBounds.width),
                                  static_cast<double>(chartBounds.height));
    return options;
}

// Top-level orthogonal routing entry. Delegates to the shared router, which
// tries the cheap cardinal path first and only invokes A* if that path
// collides with another node (falling back to cardinal if A* finds nothing).
std::vector<Point2Dd> UltraCanvasFlowChart::ComputeOrthogonalPath(
        const Point2Dd& start, const Point2Dd& end,
        CardinalSide sourceSide, CardinalSide targetSide,
        const std::string& sourceId, const std::string& targetId) {
    return UltraCanvasDiagramRouter::ComputeOrthogonalPath(
        start, end, sourceSide, targetSide,
        CollectRoutingObstacles(sourceId, targetId),
        BuildRoutingOptions());
}

void UltraCanvasFlowChart::RenderCurvedLine(IRenderContext* ctx, const Point2Dd& start, const Point2Dd& end) {
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

void UltraCanvasFlowChart::RenderArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                                           double angle, const Color& color, double size) {
    const double arrowAngle = 0.5f;
    
    ctx->SetFillPaint(color);
    
    Point2Dd p1(tip.x - size * std::cos(angle - arrowAngle),
                tip.y - size * std::sin(angle - arrowAngle));
    Point2Dd p2(tip.x - size * std::cos(angle + arrowAngle),
                tip.y - size * std::sin(angle + arrowAngle));
    
    std::vector<Point2Dd> points = {
        {tip.x, tip.y},
        {p1.x, p1.y},
        {p2.x, p2.y}
    };
    ctx->FillLinePath(points);
}

void UltraCanvasFlowChart::RenderDiamondArrow(IRenderContext* ctx, const Point2Dd& tip,
                                              double angle, const Color& color, double size) {
    ctx->SetFillPaint(color);
    
    Point2Dd p1(tip.x - size * 0.7f * std::cos(angle),
                tip.y - size * 0.7f * std::sin(angle));
    Point2Dd p2(tip.x - size * std::cos(angle - 0.7f),
                tip.y - size * std::sin(angle - 0.7f));
    Point2Dd p3(tip.x - size * 1.4f * std::cos(angle),
                tip.y - size * 1.4f * std::sin(angle));
    Point2Dd p4(tip.x - size * std::cos(angle + 0.7f),
                tip.y - size * std::sin(angle + 0.7f));
    
    std::vector<Point2Dd> points = {
        {tip.x, tip.y},
        {p1.x, p1.y},
        {p2.x, p2.y},
        {p3.x, p3.y},
        {p4.x, p4.y}
    };
    ctx->FillLinePath(points);
}

void UltraCanvasFlowChart::RenderConnectionLabel(IRenderContext* ctx, const FlowChartConnection& conn,
                                                 const Point2Dd& anchor) {
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(10.0f);
    
    Size2Di labelDim = ctx->GetTextLineDimensions(conn.label);
    int labelW = labelDim.width;
    int labelH = labelDim.height;

    double boxX = anchor.x - labelW * 0.5f;
    double boxY = anchor.y - labelH * 0.5f;

    double padding = 3.0f;
    ctx->SetFillPaint(conn.labelBackgroundColor);
    ctx->FillRectangle(Rect2Dd(boxX - padding, boxY - padding,
                               labelW + padding * 2, labelH + padding * 2));

    // DrawText() Y is the TOP of the text bbox in this framework
    // (see UltraCanvasButton text rendering pattern).
    ctx->SetTextPaint(conn.labelTextColor);
    ctx->DrawText(conn.label, {boxX, boxY});
}

// =============================================================================
// HIT TESTING
// =============================================================================

std::string UltraCanvasFlowChart::FindNodeAt(const Point2Dd& pos) {
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (PointInNode(it->second, pos)) {
            return it->first;
        }
    }
    return "";
}

std::string UltraCanvasFlowChart::FindConnectionAt(const Point2Dd& pos) {
    for (const auto& conn : connections) {
        const auto* sourceNode = GetNode(conn.sourceNodeId);
        const auto* targetNode = GetNode(conn.targetNodeId);
        
        if (!sourceNode || !targetNode) continue;
        
        Point2Dd sourceCenter = GetNodeCenter(*sourceNode);
        Point2Dd targetCenter = GetNodeCenter(*targetNode);
        Point2Dd start = GetConnectionPoint(*sourceNode, targetCenter, conn.style);
        Point2Dd end   = GetConnectionPoint(*targetNode, sourceCenter, conn.style);
        
        double dist = DistanceToLineSegment(pos, start, end);
        
        if (dist < 8.0f) {
            return conn.id;
        }
    }
    return "";
}

bool UltraCanvasFlowChart::PointInNode(const FlowChartNode& node, const Point2Dd& pos) {
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

double UltraCanvasFlowChart::DistanceToLineSegment(const Point2Dd& point,
                                                   const Point2Dd& lineStart,
                                                   const Point2Dd& lineEnd) {
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

Point2Dd UltraCanvasFlowChart::GetNodeCenter(const FlowChartNode& node) const {
    return Point2Dd(node.x + node.width * 0.5f, node.y + node.height * 0.5f);
}

// Picks the cardinal side of `nodeCenter` that faces `otherCenter`. Splits
// the plane around the node into 4 quadrants by the two diagonals; the side
// whose outward normal best matches the (other - this) direction wins.
UltraCanvasFlowChart::CardinalSide
UltraCanvasFlowChart::GetCardinalSide(const Point2Dd& nodeCenter,
                                       const Point2Dd& otherCenter) const {
    return UltraCanvasDiagramRouter::GetCardinalSide(nodeCenter, otherCenter);
}

// Mid-point of the requested cardinal side of the node's bbox. For Diamond
// these correspond to the four rhombus vertices, which is exactly the visual
// edge users expect lines to attach to.
Point2Dd UltraCanvasFlowChart::GetCardinalPoint(const FlowChartNode& node,
                                                CardinalSide side) const {
    return UltraCanvasDiagramRouter::GetCardinalPoint(
        Rect2Dd(node.x, node.y, node.width, node.height), side);
}

// Returns the attach point on `node` for a connection coming from
// `otherCenter`. Behavior depends on `style`:
//   * Straight: silhouette intersection (ellipse/rhombus/bbox), so a
//     diagonal line touches the actual border without a gap.
//   * Orthogonal/Curved: midpoint of the cardinal side facing `otherCenter`,
//     so the line exits cleanly along an axis. For Diamond this lands on
//     the rhombus vertex (since the side midpoint of a diamond bbox IS a
//     vertex), keeping the visual semantics of the 2.1.0 fix.
Point2Dd UltraCanvasFlowChart::GetConnectionPoint(const FlowChartNode& node,
                                                  const Point2Dd& otherCenter,
                                                  FlowChartConnectionStyle style) const {
    double cx = node.x + node.width * 0.5f;
    double cy = node.y + node.height * 0.5f;
    double halfW = node.width * 0.5f;
    double halfH = node.height * 0.5f;
    
    double dx = otherCenter.x - cx;
    double dy = otherCenter.y - cy;
    
    // Degenerate: other node sits on top of us.
    if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
        return Point2Dd(cx + halfW, cy);
    }
    
    // Orthogonal/Curved paths exit along an axis; use cardinal midpoints so
    // the segment leaves perpendicular to the chosen side.
    if (style == FlowChartConnectionStyle::Orthogonal ||
        style == FlowChartConnectionStyle::Curved) {
        return GetCardinalPoint(node, GetCardinalSide(Point2Dd(cx, cy), otherCenter));
    }
    
    // Straight: shape-aware silhouette intersection.
    switch (node.shape) {
        case FlowChartShape::Diamond:
        case FlowChartShape::Decision: {
            double denom = std::abs(dx) / halfW + std::abs(dy) / halfH;
            if (denom < 0.001f) return Point2Dd(cx + halfW, cy);
            double t = 1.0f / denom;
            return Point2Dd(cx + t * dx, cy + t * dy);
        }
        case FlowChartShape::Circle:
        case FlowChartShape::Oval:
        case FlowChartShape::Ellipse: {
            double ndx = dx / halfW;
            double ndy = dy / halfH;
            double denom = std::sqrt(ndx * ndx + ndy * ndy);
            if (denom < 0.001f) return Point2Dd(cx + halfW, cy);
            double t = 1.0f / denom;
            return Point2Dd(cx + t * dx, cy + t * dy);
        }
        default: {
            double tx = (dx != 0.0f) ? halfW / std::abs(dx) : std::numeric_limits<double>::infinity();
            double ty = (dy != 0.0f) ? halfH / std::abs(dy) : std::numeric_limits<double>::infinity();
            double t = std::min(tx, ty);
            return Point2Dd(cx + t * dx, cy + t * dy);
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
Point2Dd UltraCanvasFlowChart::ComputeOrthogonalLabelAnchor(
        const std::vector<Point2Dd>& waypoints) const {
    return UltraCanvasDiagramRouter::ComputeLongestSegmentAnchor(waypoints);
}

// Angle of the final segment of the path. For orthogonal/curved we still
// trust targetSide (always perpendicular to the entry edge). For straight,
// derive from the geometric segment so callers without a path can use this.
double UltraCanvasFlowChart::ComputeIncomingAngle(const std::vector<Point2Dd>& waypoints,
                                                 FlowChartConnectionStyle s,
                                                 CardinalSide targetSide) const {
    // Orthogonal/curved paths always enter perpendicular to targetSide, so the
    // side alone gives the correct arrow angle even for a 2-point path.
    if (s == FlowChartConnectionStyle::Orthogonal ||
        s == FlowChartConnectionStyle::Curved) {
        return UltraCanvasDiagramRouter::ComputeApproachAngle(targetSide);
    }
    return UltraCanvasDiagramRouter::ComputeFinalSegmentAngle(waypoints);
}

bool UltraCanvasFlowChart::IsNodeSelected(const std::string& nodeId) const {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? it->second.isSelected : false;
}

Point2Dd UltraCanvasFlowChart::ScreenToWorld(const Point2Di& screenPos) const {
    double worldX = (screenPos.x - panOffset.x) / zoomLevel;
    double worldY = (screenPos.y - panOffset.y) / zoomLevel;
    return Point2Dd(worldX, worldY);
}

Point2Dd UltraCanvasFlowChart::ScreenToWorld(const Point2Dd& screenPos) const {
    double worldX = (screenPos.x - panOffset.x) / zoomLevel;
    double worldY = (screenPos.y - panOffset.y) / zoomLevel;
    return Point2Dd(worldX, worldY);
}

double UltraCanvasFlowChart::CalculateAngle(const Point2Dd& from, const Point2Dd& to) {
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