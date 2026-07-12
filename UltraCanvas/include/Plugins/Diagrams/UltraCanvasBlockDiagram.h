// include/Plugins/Diagrams/UltraCanvasBlockDiagram.h
// Interactive block diagram component with 3D isometric rendering
// Version: 2.3.1
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework
//
// Changelog 2.3.0:
//  - GetConnectionPoints() signature changed: now takes the connection id
//    as the first argument. When multiple connections share the same face
//    of a node, they are distributed evenly along that face instead of
//    all anchoring at the midpoint. Visually: parallel arrows no longer
//    overlap, and an arrow into a node never appears to start from
//    another arrow.
//    Old: GetConnectionPoints(sourceId, targetId, x1, y1, x2, y2)
//    New: GetConnectionPoints(connId, sourceId, targetId, x1, y1, x2, y2)
//    The function is private — no external callers affected.
//  - New private helper CountFaceUsage() counts how many connections use
//    a given face of a node and returns the index of a specific connection
//    within that ordering. Used for the per-face slot calculation.
//
// Changelog 2.2.0:
//  - RenderArrow() signature changed: now takes the arrow tip position plus
//    the direction vector of the last segment, not two endpoints. This fixes
//    arrowhead misalignment on Orthogonal connections where the visual
//    direction at the tip differs from the source-to-target vector.
//    Old: RenderArrow(ctx, x1, y1, x2, y2, color)
//    New: RenderArrow(ctx, tipX, tipY, dirX, dirY, color, lineWidth)
//    The function is private — no external callers affected.
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

enum class BlockShape {
    // Basic shapes
    Rectangle,
    RoundedRectangle,
    Oval,
    Circle,
    Diamond,
    
    // Flowchart shapes  
    Process,            // Same as RoundedRectangle
    Decision,           // Same as Diamond
    Terminal,           // Pill shape (rounded rectangle)
    Data,              // Parallelogram
    Document,          // Rectangle with wavy bottom
    
    // Geometric shapes
    Hexagon,
    Octagon,
    Trapezoid,
    Parallelogram,
    Pentagon,
    Triangle,
    Star,
    
    // Special shapes
    Cloud,
    Cylinder,
    StickyNote,        // Square with folded corner
    Actor              // Stick figure
};

enum class ConnectionStyle {
    Straight,          // Direct line
    Orthogonal,        // Right-angle bends
    Curved,            // Smooth curve
    Bezier             // Cubic bezier curve
};

enum class ArrowStyle {
    NoneStyle,         // No arrow (renamed from None to avoid X11 macro collision)
    Forward,           // Arrow at end
    Backward,          // Arrow at start
    Bidirectional,     // Arrows at both ends
    Diamond,           // Diamond end
    Circle             // Circle end
};

enum class LineStyle {
    Solid,
    Dashed,
    Dotted,
    DashDot
};

// =============================================================================
// DATA STRUCTURES
// =============================================================================

struct BlockNode {
    std::string id;
    std::string label;
    BlockShape shape = BlockShape::Rectangle;
    
    float x = 0.0f, y = 0.0f;
    float width = 120.0f, height = 60.0f;

    Color fillColor = Color(200, 200, 220, 255);
    Color borderColor = Color(80, 80, 100, 255);
    float borderWidth = 2.0f;

    Color textColor = Color(0, 0, 0, 255);
    float fontSize = 12.0f;
    
    bool isSelected = false;
    bool isHovered = false;
};

struct BlockConnection {
    std::string id;
    std::string sourceId;
    std::string targetId;
    
    ConnectionStyle style = ConnectionStyle::Straight;
    ArrowStyle arrowStyle = ArrowStyle::Forward;
    LineStyle lineStyle = LineStyle::Solid;
    
    Color color = Color(80, 80, 80, 255);
    float width = 2.0f;
    
    std::string label;
    Color labelBackgroundColor = Color(255, 255, 255, 220);
    Color labelTextColor = Color(0, 0, 0, 255);
    
    bool isSelected = false;
};

// =============================================================================
// INTERACTIVE BLOCK DIAGRAM CLASS
// =============================================================================

class UltraCanvasBlockDiagram : public UltraCanvasUIElement {
public:
    enum class EditMode {
        Select,            // Select and move nodes
        CreateNode,        // Click to create node
        CreateConnection,  // Click nodes to connect
        Pan                // Pan the canvas
    };
    
    enum class RenderStyle {
        Flat,              // Flat 2D rendering
        Isometric3D        // 3D isometric blocks (HVAC/technical style)
    };

private:
    std::map<std::string, BlockNode> nodes;
    std::vector<BlockConnection> connections;
    
    float zoomLevel = 1.0f;
    float panOffsetX = 0.0f, panOffsetY = 0.0f;
    Color backgroundColor = Color(255, 255, 255, 255);

    bool showGrid = true;
    float gridSpacing = 25.0f;
    Color gridColor = Color(230, 230, 230, 255);
    bool snapToGrid = false;

    std::string selectedNodeId;
    EditMode currentMode = EditMode::Select;
    BlockShape pendingNodeShape = BlockShape::Rectangle;

    bool isCreatingConnection = false;
    std::string connectionSourceId;
    float connectionEndX = 0.0f, connectionEndY = 0.0f;

    bool isDraggingNode = false;
    float dragOffsetX = 0.0f, dragOffsetY = 0.0f;

    int nextNodeId = 1;
    int nextConnId = 1;

    // 3D Isometric rendering parameters
    RenderStyle renderStyle = RenderStyle::Isometric3D;
    float isometricDepth = 12.0f;
    float isometricAngle = 30.0f;  // degrees

public:
    UltraCanvasBlockDiagram(const std::string& id, int x, int y, int width, int height);
    
    // =============================================================================
    // NODE MANAGEMENT
    // =============================================================================
    
    void AddNode(const std::string& id, BlockShape shape, const std::string& label, float x, float y);
    void AddNode(const std::string& id, BlockShape shape, const std::string& label, float x, float y, float width, float height);
    void RemoveNode(const std::string& id);
    BlockNode* GetNode(const std::string& id);
    void UpdateNodePosition(const std::string& id, float x, float y);
    
    // Node styling
    void SetNodeColor(const std::string& id, const Color& fill, const Color& border);
    void SetNodeTextColor(const std::string& id, const Color& color);
    void SetNodeFontSize(const std::string& id, float size);
    void SetNodeShape(const std::string& id, BlockShape shape);
    void SetNodeBorderWidth(const std::string& id, float width);
    
    // =============================================================================
    // CONNECTION MANAGEMENT
    // =============================================================================
    
    void AddConnection(const std::string& id, const std::string& sourceId, const std::string& targetId);
    void AddConnection(const std::string& id, const std::string& sourceId, const std::string& targetId, ConnectionStyle style);
    void AddConnection(const std::string& id, const std::string& sourceId, const std::string& targetId, ConnectionStyle style, ArrowStyle arrowStyle);
    void RemoveConnection(const std::string& id);
    BlockConnection* GetConnection(const std::string& id);
    
    // Connection styling
    void SetConnectionColor(const std::string& id, const Color& color);
    void SetConnectionWidth(const std::string& id, float width);
    void SetConnectionLabel(const std::string& id, const std::string& label);
    void SetConnectionStyle(const std::string& id, ConnectionStyle style);
    void SetConnectionLineStyle(const std::string& id, LineStyle lineStyle);
    
    // =============================================================================
    // VIEW CONTROL
    // =============================================================================
    
    void SetZoomLevel(float zoom);
    double GetZoomLevel() const { return zoomLevel; }
    void SetPanOffset(float x, float y);
    
    // =============================================================================
    // GRID & BACKGROUND
    // =============================================================================
    
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible, float spacing = 25.0f);
    void SetGridColor(const Color& color);
    void SetSnapToGrid(bool enable);
    
    // =============================================================================
    // 3D ISOMETRIC RENDERING
    // =============================================================================
    
    void SetRenderStyle(RenderStyle style);
    RenderStyle GetRenderStyle() const { return renderStyle; }
    void SetIsometricDepth(float depth);
    double GetIsometricDepth() const { return isometricDepth; }
    void SetIsometricAngle(float angleDegrees);
    
    // =============================================================================
    // EDIT MODE
    // =============================================================================
    
    void SetEditMode(EditMode mode);
    EditMode GetEditMode() const { return currentMode; }
    void SetPendingNodeShape(BlockShape shape);
    BlockShape GetPendingNodeShape() const { return pendingNodeShape; }
    
    // =============================================================================
    // SELECTION
    // =============================================================================
    
    void SelectNode(const std::string& id);
    void DeselectAll();
    std::string GetSelectedNodeId() const { return selectedNodeId; }
    
    // =============================================================================
    // OPERATIONS
    // =============================================================================
    
    void DeleteSelected();
    void Clear();
    
    // =============================================================================
    // RENDERING
    // =============================================================================
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    
    // =============================================================================
    // CALLBACKS
    // =============================================================================
    
    std::function<void(const std::string&)> onNodeCreated;
    std::function<void(const std::string&)> onNodeSelected;
    std::function<void(const std::string&)> onNodeDoubleClick;
    std::function<void(const std::string&, const std::string&)> onConnectionCreated;
    std::function<void(EditMode)> onEditModeChanged;

private:
    // =============================================================================
    // RENDERING HELPERS
    // =============================================================================
    
    void RenderBackground(IRenderContext* ctx);
    void RenderGrid(IRenderContext* ctx);
    void RenderConnections(IRenderContext* ctx);
    void RenderNodes(IRenderContext* ctx);
    void RenderConnectionPreview(IRenderContext* ctx);
    
    void RenderNode(IRenderContext* ctx, const BlockNode& node);
    void RenderNodeShape(IRenderContext* ctx, const BlockNode& node);
    void RenderNodeText(IRenderContext* ctx, const BlockNode& node);
    void RenderConnection(IRenderContext* ctx, const BlockConnection& conn);
    
    // Draws an arrowhead at (tipX, tipY) oriented along (dirX, dirY).
    // The direction vector is the local direction of the segment ending
    // at the tip, not the global source-to-target vector. This matters
    // for Orthogonal connections where the last segment may be horizontal
    // or vertical regardless of the overall connection direction.
    void RenderArrow(IRenderContext* ctx, float tipX, float tipY,
                     float dirX, float dirY, const Color& color, float lineWidth);
    
    // Shape rendering
    void DrawRectangle(IRenderContext* ctx, const BlockNode& node);
    void DrawRoundedRectangle(IRenderContext* ctx, const BlockNode& node);
    void DrawOval(IRenderContext* ctx, const BlockNode& node);
    void DrawDiamond(IRenderContext* ctx, const BlockNode& node);
    void DrawHexagon(IRenderContext* ctx, const BlockNode& node);
    void DrawParallelogram(IRenderContext* ctx, const BlockNode& node);
    void DrawTriangle(IRenderContext* ctx, const BlockNode& node);
    void DrawStar(IRenderContext* ctx, const BlockNode& node);
    void DrawStickyNote(IRenderContext* ctx, const BlockNode& node);
    void DrawActor(IRenderContext* ctx, const BlockNode& node);
    void DrawCloud(IRenderContext* ctx, const BlockNode& node);
    
    // 3D Isometric rendering
    void DrawIsometric3DRectangle(IRenderContext* ctx, const BlockNode& node);
    void DrawIsometric3DRoundedRectangle(IRenderContext* ctx, const BlockNode& node);
    void GetIsometricOffsets(float& dx, float& dy);
    
    // =============================================================================
    // INTERACTION HELPERS
    // =============================================================================
    
    std::string FindNodeAt(float x, float y);
    bool PointInNode(const BlockNode& node, float x, float y);
    
    // Identifies one face of a node's bounding box. Used by the connection
    // anchor distribution logic so multiple connections that meet on the
    // same face spread out along it instead of stacking on the midpoint.
    enum class NodeFace { Top, Right, Bottom, Left };
    
    // Picks the face of `from` that points toward `to` based on the
    // dominant axis between their centers.
    NodeFace ChooseFaceTowards(const BlockNode& from, const BlockNode& to);
    
    // Counts how many connections in `connections` end up using `face` of
    // node `nodeId`, and returns this connection's index (0-based) within
    // that ordering. Indices are stable across frames because they follow
    // the storage order in the `connections` vector.
    void CountFaceUsage(const std::string& nodeId, NodeFace face,
                        const std::string& connId,
                        int& totalCount, int& myIndex);
    
    void GetConnectionPoints(const std::string& connId,
                              const std::string& sourceId, const std::string& targetId,
                              float& x1, float& y1, float& x2, float& y2);
    std::string GenerateNodeId();
    std::string GenerateConnectionId();
    float SnapToGrid(float value);
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasBlockDiagram> CreateBlockDiagram(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasBlockDiagram>(id, x, y, width, height);
}

} // namespace UltraCanvas