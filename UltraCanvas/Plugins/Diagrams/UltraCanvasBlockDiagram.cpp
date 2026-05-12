// Plugins/Diagrams/UltraCanvasBlockDiagram.cpp
// Interactive block diagram component - Complete implementation with 3D isometric rendering
// Version: 2.3.4
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework
//
// Changelog 2.3.3:
//  - RenderArrow(): the apex of the scanline-filled triangle was visibly
//    truncated because near t=1 the scanlines collapse to zero length and
//    their butt line caps draw a flat perpendicular cap at the apex,
//    producing the "flat-topped" arrow visible in 2.3.2. Fixed by
//    extending the apex point forward by scanStroke/2 so the final
//    short scanlines' caps land exactly at the geometric tip instead of
//    short of it.
//  - The outline pass added in 2.3.2 has been removed. With three
//    independent DrawLine calls (no shared join), the butt caps at the
//    apex reintroduced the same notch artifact the polyline approach
//    suffered from in 2.3.1. The scanline fill alone produces a clean
//    silhouette without it.
//
// Changelog 2.3.2:
//  - RenderArrow(): the arrowhead is now rasterized as a fan of thin
//    parallel scanlines that fills the triangle from base to tip. This
//    eliminates the V-vertex gap that persisted in 2.3.1 even with the
//    polyline approach: line-join behavior in IRenderContext (likely
//    bevel by default) was leaving a notch at the apex of the V. The
//    scanline fan does not depend on line joins or caps, only on
//    DrawLine, so it renders identically across all platform backends.
//  - The arrowhead now also outlines its perimeter once on top of the
//    fill, with a tip pulled back by half the line width. This sharpens
//    the edges so the arrow does not look fuzzy on high-DPI displays.
//
// Changelog 2.3.1:
//  - RenderArrow(): two visual fixes for the arrowhead.
//    (a) Drawn as a single 3-point polyline (back-tip-back) via DrawLinePath
//        instead of two separate DrawLine calls. The renderer joins the two
//        segments at the tip, eliminating the "decapitated V" artifact
//        caused by butt line caps not meeting at the vertex.
//    (b) The tip is pulled back by half the head stroke width along the
//        direction vector so the outer edge of the stroke sits tangent to
//        the block border instead of penetrating into it.
//
// Changelog 2.3.0:
//  - GetConnectionPoints() now distributes connection anchor points along
//    each face. When N connections share the same face of a node, anchors
//    sit at slots (k+1)/(N+1) of the face length. Resolves the case where
//    moving a node caused an arrow to appear to start from another arrow
//    instead of from the node's edge.
//  - New ChooseFaceTowards() helper extracts the dominant-axis face logic.
//  - New CountFaceUsage() helper counts and indexes connections per face.
//  - GetConnectionPoints() signature changed to take a connId parameter.
//
// Changelog 2.2.0:
//  - GetConnectionPoints(): rewritten with "dominant face" logic. Picks the
//    side of source and target (top/right/bottom/left) based on relative
//    position. Connection points always sit at the midpoint of a face,
//    so arrows terminate cleanly on the visible block edge regardless of
//    where the node has been dragged.
//  - RenderConnection(): Orthogonal routing now adapts to the chosen
//    faces (H-V, V-H, H-V-H, or V-H-V) instead of always doing H-V-H.
//    Avoids the inverted-Z zigzag when nodes are stacked vertically and
//    keeps the last segment perpendicular to the target face.
//  - RenderArrow(): signature changed to (tipX, tipY, dirX, dirY, color,
//    lineWidth). Arrowhead is now drawn as two thick stroke lines forming
//    a solid-looking V at the tip (DrawLinePath cannot fill, FillPolygon
//    is unavailable in IRenderContext). Orientation comes from the local
//    direction vector of the last segment, fixing the misaligned heads
//    on Orthogonal connections.

#include "Plugins/Diagrams/UltraCanvasBlockDiagram.h"
#include <cmath>
#include <sstream>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

UltraCanvasBlockDiagram::UltraCanvasBlockDiagram(const std::string& id,
                                                   int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
}

// =============================================================================
// NODE MANAGEMENT
// =============================================================================

void UltraCanvasBlockDiagram::AddNode(const std::string& nodeId, BlockShape shape,
                                       const std::string& label, float x, float y) {
    AddNode(nodeId, shape, label, x, y, 120.0f, 60.0f);
}

void UltraCanvasBlockDiagram::AddNode(const std::string& nodeId, BlockShape shape,
                                       const std::string& label, float x, float y,
                                       float width, float height) {
    if (nodes.find(nodeId) != nodes.end()) return;
    
    BlockNode node;
    node.id = nodeId;
    node.label = label;
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

void UltraCanvasBlockDiagram::RemoveNode(const std::string& nodeId) {
    // Remove connections to/from this node
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&nodeId](const BlockConnection& conn) {
                return conn.sourceId == nodeId || conn.targetId == nodeId;
            }),
        connections.end()
    );
    
    nodes.erase(nodeId);
    
    if (selectedNodeId == nodeId) {
        selectedNodeId.clear();
    }
    
    RequestRedraw();
}

BlockNode* UltraCanvasBlockDiagram::GetNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

void UltraCanvasBlockDiagram::UpdateNodePosition(const std::string& nodeId, float x, float y) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->x = snapToGrid ? SnapToGrid(x) : x;
        node->y = snapToGrid ? SnapToGrid(y) : y;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetNodeColor(const std::string& nodeId, const Color& fill, const Color& border) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->fillColor = fill;
        node->borderColor = border;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetNodeTextColor(const std::string& nodeId, const Color& color) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->textColor = color;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetNodeFontSize(const std::string& nodeId, float size) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->fontSize = size;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetNodeShape(const std::string& nodeId, BlockShape shape) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->shape = shape;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetNodeBorderWidth(const std::string& nodeId, float width) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->borderWidth = width;
        RequestRedraw();
    }
}

// =============================================================================
// CONNECTION MANAGEMENT
// =============================================================================

void UltraCanvasBlockDiagram::AddConnection(const std::string& connId, 
                                              const std::string& sourceId, 
                                              const std::string& targetId) {
    AddConnection(connId, sourceId, targetId, ConnectionStyle::Straight, ArrowStyle::Forward);
}

void UltraCanvasBlockDiagram::AddConnection(const std::string& connId, 
                                              const std::string& sourceId, 
                                              const std::string& targetId,
                                              ConnectionStyle style) {
    AddConnection(connId, sourceId, targetId, style, ArrowStyle::Forward);
}

void UltraCanvasBlockDiagram::AddConnection(const std::string& connId, 
                                              const std::string& sourceId, 
                                              const std::string& targetId,
                                              ConnectionStyle style, 
                                              ArrowStyle arrowStyle) {
    BlockConnection conn;
    conn.id = connId;
    conn.sourceId = sourceId;
    conn.targetId = targetId;
    conn.style = style;
    conn.arrowStyle = arrowStyle;
    
    connections.push_back(conn);
    
    if (onConnectionCreated) {
        onConnectionCreated(sourceId, targetId);
    }
    
    RequestRedraw();
}

void UltraCanvasBlockDiagram::RemoveConnection(const std::string& connId) {
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&connId](const BlockConnection& conn) {
                return conn.id == connId;
            }),
        connections.end()
    );
    RequestRedraw();
}

BlockConnection* UltraCanvasBlockDiagram::GetConnection(const std::string& connId) {
    for (auto& conn : connections) {
        if (conn.id == connId) {
            return &conn;
        }
    }
    return nullptr;
}

void UltraCanvasBlockDiagram::SetConnectionColor(const std::string& connId, const Color& color) {
    auto* conn = GetConnection(connId);
    if (conn) {
        conn->color = color;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetConnectionWidth(const std::string& connId, float width) {
    auto* conn = GetConnection(connId);
    if (conn) {
        conn->width = width;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetConnectionLabel(const std::string& connId, const std::string& label) {
    auto* conn = GetConnection(connId);
    if (conn) {
        conn->label = label;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetConnectionStyle(const std::string& connId, ConnectionStyle style) {
    auto* conn = GetConnection(connId);
    if (conn) {
        conn->style = style;
        RequestRedraw();
    }
}

void UltraCanvasBlockDiagram::SetConnectionLineStyle(const std::string& connId, LineStyle lineStyle) {
    auto* conn = GetConnection(connId);
    if (conn) {
        conn->lineStyle = lineStyle;
        RequestRedraw();
    }
}

// =============================================================================
// VIEW CONTROL
// =============================================================================

void UltraCanvasBlockDiagram::SetZoomLevel(float zoom) {
    zoomLevel = std::max(0.1f, std::min(5.0f, zoom));
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetPanOffset(float x, float y) {
    panOffsetX = x;
    panOffsetY = y;
    RequestRedraw();
}

// =============================================================================
// GRID & BACKGROUND
// =============================================================================

void UltraCanvasBlockDiagram::SetBackgroundColor(const Color& color) {
    backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetGridVisible(bool visible, float spacing) {
    showGrid = visible;
    gridSpacing = spacing;
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetGridColor(const Color& color) {
    gridColor = color;
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetSnapToGrid(bool enable) {
    snapToGrid = enable;
}

// =============================================================================
// 3D ISOMETRIC RENDERING
// =============================================================================

void UltraCanvasBlockDiagram::SetRenderStyle(RenderStyle style) {
    renderStyle = style;
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetIsometricDepth(float depth) {
    isometricDepth = depth;
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetIsometricAngle(float angleDegrees) {
    isometricAngle = angleDegrees;
    RequestRedraw();
}

// =============================================================================
// EDIT MODE
// =============================================================================

void UltraCanvasBlockDiagram::SetEditMode(EditMode mode) {
    currentMode = mode;
    isCreatingConnection = false;
    connectionSourceId.clear();
    
    if (onEditModeChanged) {
        onEditModeChanged(mode);
    }
    
    RequestRedraw();
}

void UltraCanvasBlockDiagram::SetPendingNodeShape(BlockShape shape) {
    pendingNodeShape = shape;
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasBlockDiagram::SelectNode(const std::string& nodeId) {
    // Deselect all
    for (auto& pair : nodes) {
        pair.second.isSelected = false;
    }
    
    // Select target
    auto* node = GetNode(nodeId);
    if (node) {
        node->isSelected = true;
        selectedNodeId = nodeId;
        
        if (onNodeSelected) {
            onNodeSelected(nodeId);
        }
    }
    
    RequestRedraw();
}

void UltraCanvasBlockDiagram::DeselectAll() {
    for (auto& pair : nodes) {
        pair.second.isSelected = false;
    }
    selectedNodeId.clear();
    RequestRedraw();
}

// =============================================================================
// OPERATIONS
// =============================================================================

void UltraCanvasBlockDiagram::DeleteSelected() {
    if (!selectedNodeId.empty()) {
        RemoveNode(selectedNodeId);
        selectedNodeId.clear();
    }
}

void UltraCanvasBlockDiagram::Clear() {
    nodes.clear();
    connections.clear();
    selectedNodeId.clear();
    isCreatingConnection = false;
    connectionSourceId.clear();
    RequestRedraw();
}

// =============================================================================
// EVENT HANDLING
// =============================================================================

bool UltraCanvasBlockDiagram::OnEvent(const UCEvent& event) {
    if (!IsVisible()) return false;
    
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
        float worldX = (event.pointer.x - panOffsetX) / zoomLevel;
        float worldY = (event.pointer.y - panOffsetY) / zoomLevel;
        
        if (currentMode == EditMode::Select) {
            std::string nodeId = FindNodeAt(worldX, worldY);
            if (!nodeId.empty()) {
                SelectNode(nodeId);
                isDraggingNode = true;
                auto* node = GetNode(nodeId);
                dragOffsetX = worldX - node->x;
                dragOffsetY = worldY - node->y;
                return true;
            } else {
                DeselectAll();
            }
        }
        else if (currentMode == EditMode::CreateNode) {
            std::string nodeId = GenerateNodeId();
            AddNode(nodeId, pendingNodeShape, "Node", worldX - 60, worldY - 30, 120, 60);
            SelectNode(nodeId);
            return true;
        }
        else if (currentMode == EditMode::CreateConnection) {
            std::string nodeId = FindNodeAt(worldX, worldY);
            if (!nodeId.empty()) {
                if (!isCreatingConnection) {
                    // Start connection
                    connectionSourceId = nodeId;
                    isCreatingConnection = true;
                    connectionEndX = worldX;
                    connectionEndY = worldY;
                } else {
                    // Complete connection
                    if (nodeId != connectionSourceId) {
                        std::string connId = GenerateConnectionId();
                        AddConnection(connId, connectionSourceId, nodeId);
                    }
                    isCreatingConnection = false;
                    connectionSourceId.clear();
                }
                return true;
            }
        }
    }
    else if (event.type == UCEventType::MouseUp && event.button == UCMouseButton::Left) {
        isDraggingNode = false;
        return true;
    }
    else if (event.type == UCEventType::MouseMove) {
        float worldX = (event.pointer.x - panOffsetX) / zoomLevel;
        float worldY = (event.pointer.y - panOffsetY) / zoomLevel;
        
        if (isDraggingNode && !selectedNodeId.empty()) {
            UpdateNodePosition(selectedNodeId, worldX - dragOffsetX, worldY - dragOffsetY);
            return true;
        }
        
        if (isCreatingConnection) {
            connectionEndX = worldX;
            connectionEndY = worldY;
            RequestRedraw();
            return true;
        }
        
        // Update hover state
        std::string hoveredId = FindNodeAt(worldX, worldY);
        bool changed = false;
        for (auto& pair : nodes) {
            bool shouldHover = (pair.first == hoveredId);
            if (pair.second.isHovered != shouldHover) {
                pair.second.isHovered = shouldHover;
                changed = true;
            }
        }
        if (changed) RequestRedraw();
    }
    else if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
        // Check for double-click
        float worldX = (event.pointer.x - panOffsetX) / zoomLevel;
        float worldY = (event.pointer.y - panOffsetY) / zoomLevel;
        
        std::string nodeId = FindNodeAt(worldX, worldY);
        if (!nodeId.empty() && onNodeDoubleClick) {
            onNodeDoubleClick(nodeId);
            return true;
        }
    }
    else if (event.type == UCEventType::KeyDown) {
        if (event.virtualKey == UCKeys::Delete || event.virtualKey == UCKeys::Backspace) {
            DeleteSelected();
            return true;
        }
        else if (event.virtualKey == UCKeys::Escape) {
            if (isCreatingConnection) {
                isCreatingConnection = false;
                connectionSourceId.clear();
                RequestRedraw();
                return true;
            }
        }
    }
    
    return false;
}

// ============================================================================= 
// RENDERING
// =============================================================================

void UltraCanvasBlockDiagram::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
    if (!ctx || !IsVisible()) return;
    
    ctx->PushState();

    // Clip to bounds (element-local 0-based coords)
    Rect2Di local = GetLocalBounds();
    ctx->ClipRect(Rect2Df(local.x, local.y, local.width, local.height));

    RenderBackground(ctx);

    // Apply transformations (zoom/pan in element-local space)
    ctx->Translate(local.x + panOffsetX, local.y + panOffsetY);
    ctx->Scale(zoomLevel, zoomLevel);
    
    if (showGrid) {
        RenderGrid(ctx);
    }
    
    RenderConnections(ctx);
    RenderNodes(ctx);
    
    if (isCreatingConnection) {
        RenderConnectionPreview(ctx);
    }
    
    ctx->PopState();
}

void UltraCanvasBlockDiagram::RenderBackground(IRenderContext* ctx) {
    Rect2Di local = GetLocalBounds();
    ctx->SetFillPaint(backgroundColor);
    ctx->FillRectangle(Rect2Df(local.x, local.y, local.width, local.height));
}

void UltraCanvasBlockDiagram::RenderGrid(IRenderContext* ctx) {
    Rect2Di local = GetLocalBounds();
    ctx->SetStrokePaint(gridColor);
    ctx->SetStrokeWidth(1.0f / zoomLevel);

    float startX = 0;
    float startY = 0;
    float endX = local.width / zoomLevel;
    float endY = local.height / zoomLevel;

    for (float x = startX; x < endX; x += gridSpacing) {
        ctx->DrawLine({x, startY}, {x, endY});
    }
    for (float y = startY; y < endY; y += gridSpacing) {
        ctx->DrawLine({startX, y}, {endX, y});
    }
}

void UltraCanvasBlockDiagram::RenderConnections(IRenderContext* ctx) {
    for (const auto& conn : connections) {
        RenderConnection(ctx, conn);
    }
}

void UltraCanvasBlockDiagram::RenderNodes(IRenderContext* ctx) {
    for (const auto& pair : nodes) {
        RenderNode(ctx, pair.second);
    }
}

void UltraCanvasBlockDiagram::RenderConnectionPreview(IRenderContext* ctx) {
    if (connectionSourceId.empty()) return;
    
    auto* sourceNode = GetNode(connectionSourceId);
    if (!sourceNode) return;
    
    float x1 = sourceNode->x + sourceNode->width / 2;
    float y1 = sourceNode->y + sourceNode->height / 2;
    
    ctx->SetStrokePaint(Color(100, 100, 100, 180));
    ctx->SetStrokeWidth(2.0f);
    
    // Dashed line
    UCDashPattern dash(std::vector<double>{5.0, 5.0});
    ctx->SetLineDash(dash);
    ctx->DrawLine({x1, y1}, {connectionEndX, connectionEndY});
    ctx->SetLineDash({});
}

// =============================================================================
// NODE RENDERING
// =============================================================================

void UltraCanvasBlockDiagram::RenderNode(IRenderContext* ctx, const BlockNode& node) {
    ctx->PushState();
    
    // Render shape
    RenderNodeShape(ctx, node);
    
    // Render text
    RenderNodeText(ctx, node);
    
    // Selection indicator
    if (node.isSelected) {
        ctx->SetStrokePaint(Color(0, 120, 255, 255));
        ctx->SetStrokeWidth(3.0f / zoomLevel);
        ctx->DrawRectangle(Rect2Df(node.x - 4, node.y - 4, node.width + 8, node.height + 8));
    }

    // Hover indicator
    if (node.isHovered && !node.isSelected) {
        ctx->SetStrokePaint(Color(0, 120, 255, 128));
        ctx->SetStrokeWidth(2.0f / zoomLevel);
        ctx->DrawRectangle(Rect2Df(node.x - 2, node.y - 2, node.width + 4, node.height + 4));
    }
    
    ctx->PopState();
}

void UltraCanvasBlockDiagram::RenderNodeShape(IRenderContext* ctx, const BlockNode& node) {
    // Use 3D isometric rendering for rectangles when in Isometric3D mode
    if (renderStyle == RenderStyle::Isometric3D) {
        switch (node.shape) {
            case BlockShape::Rectangle:
            case BlockShape::Process:
                DrawIsometric3DRectangle(ctx, node);
                return;
            case BlockShape::RoundedRectangle:
            case BlockShape::Terminal:
                DrawIsometric3DRoundedRectangle(ctx, node);
                return;
            default:
                // For other shapes, fall through to flat rendering
                break;
        }
    }
    
    // Flat rendering mode or non-rectangle shapes
    ctx->SetFillPaint(node.fillColor);
    ctx->SetStrokePaint(node.borderColor);
    ctx->SetStrokeWidth(node.borderWidth);
    
    switch (node.shape) {
        case BlockShape::Rectangle:
        case BlockShape::Process:
            DrawRectangle(ctx, node);
            break;
        case BlockShape::RoundedRectangle:
        case BlockShape::Terminal:
            DrawRoundedRectangle(ctx, node);
            break;
        case BlockShape::Oval:
        case BlockShape::Circle:
            DrawOval(ctx, node);
            break;
        case BlockShape::Diamond:
        case BlockShape::Decision:
            DrawDiamond(ctx, node);
            break;
        case BlockShape::Hexagon:
            DrawHexagon(ctx, node);
            break;
        case BlockShape::Parallelogram:
        case BlockShape::Data:
            DrawParallelogram(ctx, node);
            break;
        case BlockShape::Triangle:
            DrawTriangle(ctx, node);
            break;
        case BlockShape::Star:
            DrawStar(ctx, node);
            break;
        case BlockShape::StickyNote:
            DrawStickyNote(ctx, node);
            break;
        case BlockShape::Actor:
            DrawActor(ctx, node);
            break;
        case BlockShape::Cloud:
            DrawCloud(ctx, node);
            break;
        default:
            DrawRectangle(ctx, node);
            break;
    }
}

void UltraCanvasBlockDiagram::RenderNodeText(IRenderContext* ctx, const BlockNode& node) {
    if (node.label.empty()) return;
    
    ctx->SetTextPaint(node.textColor);
    ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(node.fontSize);
    
    Size2Di tDim = ctx->GetTextLineDimensions(node.label);
    int textW = tDim.width;
    int textH = tDim.height;

    float textX = node.x + (node.width - textW) / 2.0f;
    float textY = node.y + (node.height - textH) / 2.0f;

    ctx->DrawText(node.label, {textX, textY});
}

// Continued in next part...
// Continuation of UltraCanvasBlockDiagram.cpp - Part 2
// Shape rendering implementations

// =============================================================================
// SHAPE DRAWING IMPLEMENTATIONS
// =============================================================================

void UltraCanvasBlockDiagram::DrawRectangle(IRenderContext* ctx, const BlockNode& node) {
    ctx->FillRectangle(Rect2Df(node.x, node.y, node.width, node.height));
    ctx->DrawRectangle(Rect2Df(node.x, node.y, node.width, node.height));
}

void UltraCanvasBlockDiagram::DrawRoundedRectangle(IRenderContext* ctx, const BlockNode& node) {
    float radius = std::min(node.width, node.height) * 0.15f;
    ctx->FillRoundedRectangle(Rect2Df(node.x, node.y, node.width, node.height), radius);
    ctx->DrawRoundedRectangle(Rect2Df(node.x, node.y, node.width, node.height), radius);
}

void UltraCanvasBlockDiagram::DrawOval(IRenderContext* ctx, const BlockNode& node) {
    ctx->FillEllipse(Rect2Df(node.x, node.y, node.width, node.height));
    ctx->DrawEllipse(Rect2Df(node.x, node.y, node.width, node.height));
}

void UltraCanvasBlockDiagram::DrawDiamond(IRenderContext* ctx, const BlockNode& node) {
    float cx = node.x + node.width / 2;
    float cy = node.y + node.height / 2;
    float halfW = node.width / 2;
    float halfH = node.height / 2;
    
    std::vector<Point2Df> points = {
        {cx, node.y},           // Top
        {node.x + node.width, cy},  // Right
        {cx, node.y + node.height}, // Bottom
        {node.x, cy}            // Left
    };
    
// FillPolygon removed - use line path only
    ctx->DrawLinePath(points, true);
}

void UltraCanvasBlockDiagram::DrawHexagon(IRenderContext* ctx, const BlockNode& node) {
    float cx = node.x + node.width / 2;
    float cy = node.y + node.height / 2;
    float rx = node.width / 2;
    float ry = node.height / 2;
    
    std::vector<Point2Df> points;
    for (int i = 0; i < 6; i++) {
        float angle = (3.14159f / 3.0f) * i;
        points.push_back({
            cx + rx * std::cos(angle),
            cy + ry * std::sin(angle)
        });
    }
    
// FillPolygon removed - use line path only
    ctx->DrawLinePath(points, true);
}

void UltraCanvasBlockDiagram::DrawParallelogram(IRenderContext* ctx, const BlockNode& node) {
    float offset = node.width * 0.2f;
    
    std::vector<Point2Df> points = {
        {node.x + offset, node.y},
        {node.x + node.width, node.y},
        {node.x + node.width - offset, node.y + node.height},
        {node.x, node.y + node.height}
    };
    
// FillPolygon removed - use line path only
    ctx->DrawLinePath(points, true);
}

void UltraCanvasBlockDiagram::DrawTriangle(IRenderContext* ctx, const BlockNode& node) {
    float cx = node.x + node.width / 2;
    
    std::vector<Point2Df> points = {
        {cx, node.y},
        {node.x + node.width, node.y + node.height},
        {node.x, node.y + node.height}
    };
    
// FillPolygon removed - use line path only
    ctx->DrawLinePath(points, true);
}

void UltraCanvasBlockDiagram::DrawStar(IRenderContext* ctx, const BlockNode& node) {
    float cx = node.x + node.width / 2;
    float cy = node.y + node.height / 2;
    float outerR = std::min(node.width, node.height) / 2;
    float innerR = outerR * 0.4f;
    
    std::vector<Point2Df> points;
    for (int i = 0; i < 10; i++) {
        float angle = (3.14159f / 5.0f) * i - 3.14159f / 2;
        float r = (i % 2 == 0) ? outerR : innerR;
        points.push_back({
            cx + r * std::cos(angle),
            cy + r * std::sin(angle)
        });
    }
    
// FillPolygon removed - use line path only
    ctx->DrawLinePath(points, true);
}

void UltraCanvasBlockDiagram::DrawStickyNote(IRenderContext* ctx, const BlockNode& node) {
    float foldSize = std::min(node.width, node.height) * 0.15f;

    // Main rectangle
    ctx->FillRectangle(Rect2Df(node.x, node.y, node.width - foldSize, node.height));
    ctx->DrawRectangle(Rect2Df(node.x, node.y, node.width - foldSize, node.height));
    
    // Folded corner
    std::vector<Point2Df> fold = {
        {node.x + node.width - foldSize, node.y},
        {node.x + node.width, node.y + foldSize},
        {node.x + node.width - foldSize, node.y + foldSize}
    };
    
    Color darkerFill = node.fillColor;
    darkerFill.r = static_cast<uint8_t>(darkerFill.r * 0.8f);
    darkerFill.g = static_cast<uint8_t>(darkerFill.g * 0.8f);
    darkerFill.b = static_cast<uint8_t>(darkerFill.b * 0.8f);
    
    ctx->SetFillPaint(darkerFill);

    ctx->DrawLinePath(fold, true);
}

void UltraCanvasBlockDiagram::DrawActor(IRenderContext* ctx, const BlockNode& node) {
    float cx = node.x + node.width / 2;
    float headRadius = node.height * 0.15f;
    float headY = node.y + headRadius + 5;

    // Head
    ctx->FillEllipse(Rect2Df(cx - headRadius, headY - headRadius, headRadius * 2, headRadius * 2));
    ctx->DrawEllipse(Rect2Df(cx - headRadius, headY - headRadius, headRadius * 2, headRadius * 2));

    // Body
    float bodyTop = headY + headRadius;
    float bodyBottom = node.y + node.height * 0.7f;
    ctx->DrawLine({cx, bodyTop}, {cx, bodyBottom});

    // Arms
    float armY = bodyTop + (bodyBottom - bodyTop) * 0.3f;
    float armSpan = node.width * 0.4f;
    ctx->DrawLine({cx - armSpan, armY}, {cx + armSpan, armY});

    // Legs
    float legSpan = node.width * 0.3f;
    ctx->DrawLine({cx, bodyBottom}, {cx - legSpan, node.y + node.height});
    ctx->DrawLine({cx, bodyBottom}, {cx + legSpan, node.y + node.height});
}

void UltraCanvasBlockDiagram::DrawCloud(IRenderContext* ctx, const BlockNode& node) {
    float cx = node.x + node.width / 2;
    float cy = node.y + node.height / 2;

    // Draw 4-5 overlapping circles to create cloud shape
    float r1 = node.width * 0.25f;
    float r2 = node.width * 0.3f;
    float r3 = node.width * 0.25f;

    ctx->FillEllipse(Rect2Df(node.x + r1 - r1, cy - r1, r1 * 2, r1 * 2));
    ctx->FillEllipse(Rect2Df(cx - r2, node.y + r2 - r2, r2 * 2, r2 * 2));
    ctx->FillEllipse(Rect2Df(node.x + node.width - r3 - r3, cy - r3, r3 * 2, r3 * 2));
    ctx->FillEllipse(Rect2Df(cx - r1, node.y + node.height - r1 - r1, r1 * 2, r1 * 2));

    // Border
    ctx->DrawEllipse(Rect2Df(node.x + r1 - r1, cy - r1, r1 * 2, r1 * 2));
    ctx->DrawEllipse(Rect2Df(cx - r2, node.y + r2 - r2, r2 * 2, r2 * 2));
    ctx->DrawEllipse(Rect2Df(node.x + node.width - r3 - r3, cy - r3, r3 * 2, r3 * 2));
    ctx->DrawEllipse(Rect2Df(cx - r1, node.y + node.height - r1 - r1, r1 * 2, r1 * 2));
}

// =============================================================================
// 3D ISOMETRIC RENDERING IMPLEMENTATIONS
// =============================================================================

void UltraCanvasBlockDiagram::GetIsometricOffsets(float& dx, float& dy) {
    // Convert angle to radians
    float angleRad = isometricAngle * M_PI / 180.0f;
    
    // Calculate offsets for isometric projection
    dx = isometricDepth * std::cos(angleRad);
    dy = isometricDepth * std::sin(angleRad);
}

void UltraCanvasBlockDiagram::DrawIsometric3DRectangle(IRenderContext* ctx, const BlockNode& node) {
    float dx, dy;
    GetIsometricOffsets(dx, dy);
    
    // Color scheme for technical/HVAC style (blue tones)
    Color frontFace = node.fillColor;  // Use node's fill color
    
    // Calculate darker/lighter variations
    Color rightSide = Color(
        static_cast<uint8_t>(frontFace.r * 0.75f),
        static_cast<uint8_t>(frontFace.g * 0.75f),
        static_cast<uint8_t>(frontFace.b * 0.75f),
        frontFace.a
    );
    
    Color topFace = Color(
        static_cast<uint8_t>(std::min(255, static_cast<int>(frontFace.r * 1.2f))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(frontFace.g * 1.2f))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(frontFace.b * 1.2f))),
        frontFace.a
    );
    
    Color shadow = Color(128, 128, 128, 180);  // Gray shadow
    Color border = node.borderColor;
    
    // 1. Draw shadow (bottom-right offset)
    ctx->SetFillPaint(shadow);
    std::vector<Point2Df> shadowPoints = {
        {node.x + dx, node.y + node.height + dy},
        {node.x + node.width + dx, node.y + node.height + dy},
        {node.x + node.width + dx * 2, node.y + node.height},
        {node.x + dx * 2, node.y + node.height}
    };

    
    // 2. Draw top face
    ctx->SetFillPaint(topFace);
    std::vector<Point2Df> topPoints = {
        {node.x, node.y},
        {node.x + node.width, node.y},
        {node.x + node.width + dx, node.y - dy},
        {node.x + dx, node.y - dy}
    };

    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(node.borderWidth);
    ctx->DrawLinePath(topPoints, true);
    
    // 3. Draw right side face
    ctx->SetFillPaint(rightSide);
    std::vector<Point2Df> rightPoints = {
        {node.x + node.width, node.y},
        {node.x + node.width + dx, node.y - dy},
        {node.x + node.width + dx, node.y + node.height - dy},
        {node.x + node.width, node.y + node.height}
    };

    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(node.borderWidth);
    ctx->DrawLinePath(rightPoints, true);
    
    // 4. Draw front face (main surface)
    ctx->SetFillPaint(frontFace);
    ctx->FillRectangle(Rect2Df(node.x, node.y, node.width, node.height));
    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(node.borderWidth);
    ctx->DrawRectangle(Rect2Df(node.x, node.y, node.width, node.height));
}

void UltraCanvasBlockDiagram::DrawIsometric3DRoundedRectangle(IRenderContext* ctx, const BlockNode& node) {
    // For rounded rectangles in isometric view, we'll draw a regular 3D block
    // (proper rounded isometric is complex, so we simplify)
    DrawIsometric3DRectangle(ctx, node);
}

// =============================================================================
// CONNECTION RENDERING
// =============================================================================

void UltraCanvasBlockDiagram::RenderConnection(IRenderContext* ctx, const BlockConnection& conn) {
    auto* sourceNode = GetNode(conn.sourceId);
    auto* targetNode = GetNode(conn.targetId);
    
    if (!sourceNode || !targetNode) return;
    
    float x1, y1, x2, y2;
    GetConnectionPoints(conn.id, conn.sourceId, conn.targetId, x1, y1, x2, y2);
    
    // Determine the orientation of each endpoint's face. With anchor
    // distribution along faces (v2.3.0), an endpoint's x-coordinate may
    // coincidentally match a node's left or right edge even when it sits
    // on the top or bottom face — so we test against the horizontal faces
    // (y == top edge or y == bottom edge) instead, which is unambiguous:
    // an endpoint on the Top or Bottom face sits exactly on those y values
    // and an endpoint on the Left or Right face sits strictly between them.
    // sourceHorizontal == true means the line leaves the source horizontally
    // (i.e. the source endpoint is on the left or right face).
    bool sourceOnTopBottom = (std::abs(y1 - sourceNode->y) < 0.5f) ||
                             (std::abs(y1 - (sourceNode->y + sourceNode->height)) < 0.5f);
    bool targetOnTopBottom = (std::abs(y2 - targetNode->y) < 0.5f) ||
                             (std::abs(y2 - (targetNode->y + targetNode->height)) < 0.5f);
    bool sourceHorizontal = !sourceOnTopBottom;
    bool targetHorizontal = !targetOnTopBottom;
    
    // Set line style
    ctx->SetStrokePaint(conn.color);
    ctx->SetStrokeWidth(conn.width);
    
    if (conn.lineStyle == LineStyle::Dashed) {
        ctx->SetLineDash(UCDashPattern(std::vector<double>{10.0, 5.0}));
    } else if (conn.lineStyle == LineStyle::Dotted) {
        ctx->SetLineDash(UCDashPattern(std::vector<double>{2.0, 3.0}));
    } else if (conn.lineStyle == LineStyle::DashDot) {
        ctx->SetLineDash(UCDashPattern(std::vector<double>{10.0, 3.0, 2.0, 3.0}));
    }
    
    // Track the direction of the final segment (the one ending at x2,y2).
    // RenderArrow uses this to orient the arrowhead perpendicular to the
    // target face. Defaults match the Straight case.
    float forwardDirX = x2 - x1;
    float forwardDirY = y2 - y1;
    float backwardDirX = -forwardDirX;
    float backwardDirY = -forwardDirY;
    
    // Draw connection line based on style
    if (conn.style == ConnectionStyle::Straight) {
        ctx->DrawLine({x1, y1}, {x2, y2});
    }
    else if (conn.style == ConnectionStyle::Orthogonal) {
        // Choose the routing pattern based on the face orientations of the
        // two endpoints. Goal: every segment is axis-aligned and the last
        // segment is perpendicular to the target face.
        if (sourceHorizontal && targetHorizontal) {
            // Both ends on vertical faces: H-V-H (classic)
            float midX = (x1 + x2) * 0.5f;
            ctx->DrawLine({x1, y1}, {midX, y1});
            ctx->DrawLine({midX, y1}, {midX, y2});
            ctx->DrawLine({midX, y2}, {x2, y2});
            // Last forward segment: (midX, y2) -> (x2, y2), horizontal
            forwardDirX = x2 - midX;
            forwardDirY = 0.0f;
            // Backward arrow sits at (x1, y1); previous segment is (midX, y1) -> (x1, y1)
            backwardDirX = x1 - midX;
            backwardDirY = 0.0f;
        }
        else if (!sourceHorizontal && !targetHorizontal) {
            // Both ends on horizontal faces: V-H-V
            float midY = (y1 + y2) * 0.5f;
            ctx->DrawLine({x1, y1}, {x1, midY});
            ctx->DrawLine({x1, midY}, {x2, midY});
            ctx->DrawLine({x2, midY}, {x2, y2});
            // Last segment is vertical
            forwardDirX = 0.0f;
            forwardDirY = y2 - midY;
            backwardDirX = 0.0f;
            backwardDirY = -(midY - y1);
        }
        else if (sourceHorizontal && !targetHorizontal) {
            // Source leaves horizontally, target enters vertically: H then V
            ctx->DrawLine({x1, y1}, {x2, y1});
            ctx->DrawLine({x2, y1}, {x2, y2});
            // Last segment vertical
            forwardDirX = 0.0f;
            forwardDirY = y2 - y1;
            // Backward arrow at source needs horizontal direction
            backwardDirX = -(x2 - x1);
            backwardDirY = 0.0f;
        }
        else {
            // Source leaves vertically, target enters horizontally: V then H
            ctx->DrawLine({x1, y1}, {x1, y2});
            ctx->DrawLine({x1, y2}, {x2, y2});
            // Last segment horizontal
            forwardDirX = x2 - x1;
            forwardDirY = 0.0f;
            // Backward arrow at source needs vertical direction
            backwardDirX = 0.0f;
            backwardDirY = -(y2 - y1);
        }
    }
    else if (conn.style == ConnectionStyle::Curved) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float dist = std::sqrt(dx * dx + dy * dy);
        float curvature = dist * 0.3f;
        
        Point2Df cp1 = {x1 + curvature, y1};
        Point2Df cp2 = {x2 - curvature, y2};
        
        ctx->DrawBezierCurve({x1, y1}, cp1, cp2, {x2, y2});
        // Tangent at the end of a cubic Bezier is (P3 - P2)
        forwardDirX = x2 - cp2.x;
        forwardDirY = y2 - cp2.y;
        backwardDirX = x1 - cp1.x;
        backwardDirY = y1 - cp1.y;
    }
    
    ctx->SetLineDash({});
    
    // Draw arrow heads using the local direction of the last segment
    if (conn.arrowStyle == ArrowStyle::Forward || conn.arrowStyle == ArrowStyle::Bidirectional) {
        RenderArrow(ctx, x2, y2, forwardDirX, forwardDirY, conn.color, conn.width);
    }
    if (conn.arrowStyle == ArrowStyle::Backward || conn.arrowStyle == ArrowStyle::Bidirectional) {
        RenderArrow(ctx, x1, y1, backwardDirX, backwardDirY, conn.color, conn.width);
    }
    
    // Draw label
    if (!conn.label.empty()) {
        float labelX = (x1 + x2) / 2;
        float labelY = (y1 + y2) / 2;

        ctx->SetTextPaint(conn.labelTextColor);
        ctx->SetFontSize(10.0f);

        Size2Di lblDim = ctx->GetTextLineDimensions(conn.label);
        int textW = lblDim.width;
        int textH = lblDim.height;

        // Background
        ctx->SetFillPaint(conn.labelBackgroundColor);
        ctx->FillRectangle(Rect2Df(labelX - textW/2.0f - 3, labelY - textH/2.0f - 2,
                                    static_cast<float>(textW) + 6, static_cast<float>(textH) + 4));

        // Text
        ctx->DrawText(conn.label, {labelX - textW/2.0f, labelY - textH/2.0f});
    }

    // Selection indicator
    if (conn.isSelected) {
        ctx->SetStrokePaint(Color(0, 120, 255, 255));
        ctx->SetStrokeWidth(conn.width + 2);
        ctx->DrawLine({x1, y1}, {x2, y2});
    }
}

void UltraCanvasBlockDiagram::RenderArrow(IRenderContext* ctx, float tipX, float tipY,
                                           float dirX, float dirY, const Color& color, float lineWidth) {
    // Guard against zero-length direction
    float len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 0.001f) return;
    
    // Unit direction vector
    float ux = dirX / len;
    float uy = dirY / len;
    
    // Pull the tip back slightly so the arrow outline does not penetrate
    // the target block. We use the connection's lineWidth (not the head
    // stroke) because the visual silhouette of the head is now defined by
    // the outline pass, which strokes at lineWidth.
    float tipBack = std::max(lineWidth * 0.5f, 1.0f);
    float vtipX = tipX - ux * tipBack;
    float vtipY = tipY - uy * tipBack;
    
    float angle = std::atan2(dirY, dirX);
    float arrowSize = 11.0f;
    float arrowSpread = 0.5f;  // ~28.6 degrees half-spread
    
    // The two back corners of the triangular head
    float ax1 = vtipX - arrowSize * std::cos(angle - arrowSpread);
    float ay1 = vtipY - arrowSize * std::sin(angle - arrowSpread);
    float ax2 = vtipX - arrowSize * std::cos(angle + arrowSpread);
    float ay2 = vtipY - arrowSize * std::sin(angle + arrowSpread);
    
    ctx->SetStrokePaint(color);
    ctx->SetLineDash({});  // arrowheads always solid, even on dashed lines
    
    // Step 1: rasterize the triangle as a fan of parallel "scanlines" that
    // sweep from the base (ax1 -> ax2) toward the apex (vtip). This fills
    // the head visually without depending on FillPath, line joins, or line
    // caps — all we need is plain DrawLine.
    //
    // Each scanline at parameter t (0..1) goes from a point on the ax1->vtip
    // edge to a point on the ax2->vtip edge. At t=0 it equals the base; at
    // t=1 it collapses to the apex. Spacing is chosen so successive lines
    // overlap by ~0.5px — enough to look continuous on every backend
    // without burning draw calls.
    //
    // Subtlety: as t -> 1 the scanline length shrinks to zero, and the
    // butt line caps at each end then draw a flat rectangle perpendicular
    // to the line direction — which appears as a truncated, flat-topped
    // apex. To compensate, we sweep toward an apex that is pushed forward
    // by half a scanStroke. The rounded/butt cap of the final tiny line
    // then lands its outer edge exactly at the geometric vtip, producing
    // a sharp pointed apex instead of a flat one.
    int   steps = static_cast<int>(arrowSize * 2.5f);  // ~28 lines for size=11
    float scanStroke = 1.2f;  // thin lines, slight overlap between them
    float apexExtend = scanStroke * 0.5f;
    float apexX = vtipX + ux * apexExtend;
    float apexY = vtipY + uy * apexExtend;
    
    ctx->SetStrokeWidth(scanStroke);
    
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float lx = ax1 + (apexX - ax1) * t;
        float ly = ay1 + (apexY - ay1) * t;
        float rx = ax2 + (apexX - ax2) * t;
        float ry = ay2 + (apexY - ay2) * t;
        ctx->DrawLine({lx, ly}, {rx, ry});
    }
    
    // Note: an outline pass over the triangle (3 separate DrawLine calls)
    // was tried in 2.3.2 but was abandoned — three independent strokes
    // with butt line caps reintroduce the notch at the apex that the
    // scanline fill is meant to avoid. The scanline fan with apex
    // extension already produces a crisp silhouette on its own.
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

std::string UltraCanvasBlockDiagram::FindNodeAt(float x, float y) {
    // Check in reverse order (top nodes first)
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (PointInNode(it->second, x, y)) {
            return it->first;
        }
    }
    return "";
}

bool UltraCanvasBlockDiagram::PointInNode(const BlockNode& node, float x, float y) {
    if (node.shape == BlockShape::Diamond || node.shape == BlockShape::Decision) {
        // Diamond hit test
        float cx = node.x + node.width / 2;
        float cy = node.y + node.height / 2;
        float dx = std::abs(x - cx) / (node.width / 2);
        float dy = std::abs(y - cy) / (node.height / 2);
        return (dx + dy) <= 1.0f;
    }
    else if (node.shape == BlockShape::Oval || node.shape == BlockShape::Circle) {
        // Ellipse hit test
        float cx = node.x + node.width / 2;
        float cy = node.y + node.height / 2;
        float dx = (x - cx) / (node.width / 2);
        float dy = (y - cy) / (node.height / 2);
        return (dx * dx + dy * dy) <= 1.0f;
    }
    else {
        // Rectangle hit test (default for most shapes)
        return x >= node.x && x <= node.x + node.width &&
               y >= node.y && y <= node.y + node.height;
    }
}

UltraCanvasBlockDiagram::NodeFace
UltraCanvasBlockDiagram::ChooseFaceTowards(const BlockNode& from, const BlockNode& to) {
    float fcx = from.x + from.width  * 0.5f;
    float fcy = from.y + from.height * 0.5f;
    float tcx = to.x   + to.width    * 0.5f;
    float tcy = to.y   + to.height   * 0.5f;
    
    float dx = tcx - fcx;
    float dy = tcy - fcy;
    
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx >= 0.0f) ? NodeFace::Right : NodeFace::Left;
    } else {
        return (dy >= 0.0f) ? NodeFace::Bottom : NodeFace::Top;
    }
}

void UltraCanvasBlockDiagram::CountFaceUsage(const std::string& nodeId, NodeFace face,
                                              const std::string& connId,
                                              int& totalCount, int& myIndex) {
    totalCount = 0;
    myIndex = 0;
    
    auto* node = GetNode(nodeId);
    if (!node) return;
    
    // Walk connections in storage order so indices are stable across frames.
    // For each connection touching `nodeId`, figure out which face it uses
    // at this node (whether nodeId is the source or the target). If that
    // face matches `face`, count it; record our own index when we hit connId.
    for (const auto& conn : connections) {
        bool isSource = (conn.sourceId == nodeId);
        bool isTarget = (conn.targetId == nodeId);
        if (!isSource && !isTarget) continue;
        
        const std::string& otherId = isSource ? conn.targetId : conn.sourceId;
        auto* other = GetNode(otherId);
        if (!other) continue;
        
        // Face that this connection uses *at nodeId*. From the perspective
        // of nodeId, the face pointing toward `other` is the face this
        // connection uses here, regardless of whether nodeId is source or
        // target — the dominant-axis rule is symmetric.
        NodeFace usedFace = ChooseFaceTowards(*node, *other);
        
        if (usedFace != face) continue;
        
        if (conn.id == connId) {
            myIndex = totalCount;
        }
        totalCount++;
    }
    
    // Defensive: if connId wasn't found (shouldn't happen), behave like
    // a single-occupant face so the anchor stays at the midpoint.
    if (totalCount == 0) {
        totalCount = 1;
        myIndex = 0;
    }
}

void UltraCanvasBlockDiagram::GetConnectionPoints(const std::string& connId,
                                                    const std::string& sourceId, const std::string& targetId,
                                                    float& x1, float& y1, float& x2, float& y2) {
    auto* source = GetNode(sourceId);
    auto* target = GetNode(targetId);
    
    if (!source || !target) {
        x1 = y1 = x2 = y2 = 0;
        return;
    }
    
    // Pick the face on each end using dominant-axis logic.
    NodeFace sourceFace = ChooseFaceTowards(*source, *target);
    NodeFace targetFace = ChooseFaceTowards(*target, *source);
    
    // Count how many connections share each face, and where this one sits
    // in that ordering. Slot k of N is placed at (k+1)/(N+1) along the face,
    // so a single connection lands at 1/2 (midpoint, identical to the old
    // behavior), two land at 1/3 and 2/3, three land at 1/4, 2/4, 3/4, etc.
    int sourceCount = 1, sourceIdx = 0;
    int targetCount = 1, targetIdx = 0;
    CountFaceUsage(sourceId, sourceFace, connId, sourceCount, sourceIdx);
    CountFaceUsage(targetId, targetFace, connId, targetCount, targetIdx);
    
    float sourceSlot = static_cast<float>(sourceIdx + 1) / static_cast<float>(sourceCount + 1);
    float targetSlot = static_cast<float>(targetIdx + 1) / static_cast<float>(targetCount + 1);
    
    // Place the source endpoint on its face, offset by sourceSlot.
    switch (sourceFace) {
        case NodeFace::Right:
            x1 = source->x + source->width;
            y1 = source->y + source->height * sourceSlot;
            break;
        case NodeFace::Left:
            x1 = source->x;
            y1 = source->y + source->height * sourceSlot;
            break;
        case NodeFace::Bottom:
            x1 = source->x + source->width * sourceSlot;
            y1 = source->y + source->height;
            break;
        case NodeFace::Top:
            x1 = source->x + source->width * sourceSlot;
            y1 = source->y;
            break;
    }
    
    // Same for the target endpoint.
    switch (targetFace) {
        case NodeFace::Right:
            x2 = target->x + target->width;
            y2 = target->y + target->height * targetSlot;
            break;
        case NodeFace::Left:
            x2 = target->x;
            y2 = target->y + target->height * targetSlot;
            break;
        case NodeFace::Bottom:
            x2 = target->x + target->width * targetSlot;
            y2 = target->y + target->height;
            break;
        case NodeFace::Top:
            x2 = target->x + target->width * targetSlot;
            y2 = target->y;
            break;
    }
}

std::string UltraCanvasBlockDiagram::GenerateNodeId() {
    return "node_" + std::to_string(nextNodeId++);
}

std::string UltraCanvasBlockDiagram::GenerateConnectionId() {
    return "conn_" + std::to_string(nextConnId++);
}

float UltraCanvasBlockDiagram::SnapToGrid(float value) {
    return std::round(value / gridSpacing) * gridSpacing;
}

} // namespace UltraCanvas