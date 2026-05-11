// include/Plugins/Diagrams/UltraCanvasFlowChart.h
// Interactive Flow Chart diagram component
// Version: 2.2.0
//
// Changelog:
//   2.2.0 - Obstacle-aware orthogonal routing via A*:
//           Orthogonal connections now use A* pathfinding over the chart's
//           grid to avoid passing through other nodes. The path model
//           changed from "L-shape or Z-shape" to "list of waypoints"; this
//           is internal — public API unchanged. Falls back silently to the
//           2.1.4 cardinal-aware L/Z routing when A* finds no path (rare:
//           only when target is fully surrounded by obstacles).
//   2.1.4 - Cardinal-aware orthogonal routing (Connect tool now produces
//           clean L-shape / Z-shape paths). GetConnectionPoint() takes a
//           FlowChartConnectionStyle parameter; signature change is private.
//   2.1.3 - StickyNote body geometry fixed (visual center matches bbox).
//   2.1.2 - Text Y origin corrected (DrawText takes top, not baseline).
//           Multi-line label support kept from 2.1.1.
//   2.1.1 - Text baseline fix (superseded by 2.1.2).
//   2.1.0 - Render fixes (no public API change):
//           * GetConnectionPoint() is now shape-aware: lines anchor to the
//             real silhouette of Diamond/Decision/Oval/Circle/Ellipse instead
//             of the bounding box (eliminates gaps on rhombus connections).
//           * Orthogonal connection labels anchor to the middle vertical
//             segment instead of the geometric midpoint of (start, end),
//             so 'Yes'/'No' markers no longer double in empty space.
//           * Arrow heads align with the last segment of the path (not the
//             straight-line angle) and retreat by borderWidth/2 + 1 px so
//             the tip touches the destination border without overlapping.
//           * Connection label honors conn.labelBackgroundColor and
//             conn.labelTextColor (previously declared but ignored).
//   2.0.0 - Initial public release.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <cmath>

#undef None
namespace UltraCanvas {

// =============================================================================
// FLOW CHART ENUMERATIONS
// =============================================================================

enum class FlowChartShape {
    Rectangle,
    RoundedRectangle,
    Diamond,
    Oval,
    Circle,
    Parallelogram,
    Hexagon,
    Triangle,
    Star,
    Ellipse,
    Process,
    Decision,
    ManualInput,
    Document,
    Database,
    Cloud,
    StickyNote,
    Actor
};

enum class FlowChartConnectionStyle {
    Straight,
    Orthogonal,
    Curved
};

enum class FlowChartArrowStyle {
    None,
    Arrow,
    ArrowFilled,
    Diamond
};

enum class FlowChartLineStyle {
    Solid,
    Dashed,
    Dotted,
    DashDot
};

enum class FlowChartTheme {
    Default,
    Professional,
    Colorful,
    Minimal,
    Dark
};

// =============================================================================
// FLOW CHART DATA STRUCTURES
// =============================================================================

struct FlowChartNode {
    std::string id;
    std::string label;
    FlowChartShape shape = FlowChartShape::Rectangle;
    
    double x = 0.0f;
    double y = 0.0f;
    double width = 120.0f;
    double height = 60.0f;
    
    Color fillColor = Color(240, 240, 245, 255);
    Color borderColor = Color(80, 80, 85, 255);
    double borderWidth = 1.5f;
    
    Color textColor = Color(30, 30, 35, 255);
    double fontSize = 12.0f;
    
    bool isSelected = false;
    bool isDragging = false;
    bool isStart = false;
    bool isEnd = false;
    bool isHovered = false;
    
    FlowChartNode() = default;
    FlowChartNode(const std::string& nodeId, const std::string& nodeLabel)
        : id(nodeId), label(nodeLabel) {}
};

struct FlowChartConnection {
    std::string id;
    std::string sourceNodeId;
    std::string targetNodeId;
    
    FlowChartConnectionStyle style = FlowChartConnectionStyle::Orthogonal;
    FlowChartArrowStyle arrowStyle = FlowChartArrowStyle::Arrow;
    FlowChartLineStyle lineStyle = FlowChartLineStyle::Solid;
    
    Color lineColor = Color(60, 60, 65, 255);
    double lineWidth = 2.0f;
    
    std::string label;
    Color labelBackgroundColor = Color(255, 255, 255, 220);
    Color labelTextColor = Color(0, 0, 0, 255);
    
    bool isSelected = false;
    
    FlowChartConnection() = default;
    FlowChartConnection(const std::string& connId, const std::string& source, const std::string& target)
        : id(connId), sourceNodeId(source), targetNodeId(target) {}
};

struct FlowChartStyle {
    std::string fontFamily = "Arial";
    double baseFontSize = 12.0f;
    
    Color backgroundColor = Color(250, 250, 250, 255);
    Color gridColor = Color(230, 230, 235, 255);
    bool showGrid = true;
    double gridSpacing = 20.0f;
    
    Color selectionColor = Color(0, 120, 215, 255);
    double selectionWidth = 2.0f;
};

// =============================================================================
// FLOW CHART COMPONENT CLASS
// =============================================================================

class UltraCanvasFlowChart : public UltraCanvasUIElement {
public:
    enum class EditMode {
        Select,
        CreateNode,
        CreateConnection,
        Pan
    };

    enum class CardinalSide { Top, Right, Bottom, Left };

    UltraCanvasFlowChart(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }
    
    // =============================================================================
    // NODE MANAGEMENT
    // =============================================================================
    
    void AddNode(const std::string& id, FlowChartShape shape, const std::string& label, double x, double y);
    void AddNode(const std::string& id, FlowChartShape shape, const std::string& label, double x, double y, double width, double height);
    void RemoveNode(const std::string& id);
    void UpdateNodePosition(const std::string& id, double x, double y);
    void SetNodeSize(const std::string& id, double width, double height);
    void UpdateNodeLabel(const std::string& id, const std::string& label);
    void SetNodeColor(const std::string& id, const Color& fillColor, const Color& borderColor);
    void SetNodeTextColor(const std::string& id, const Color& color);
    void SetNodeFontSize(const std::string& id, double size);
    void SetNodeShape(const std::string& id, FlowChartShape shape);
    void SetNodeBorderWidth(const std::string& id, double width);
    
    FlowChartNode* GetNode(const std::string& id);
    const FlowChartNode* GetNode(const std::string& id) const;
    std::vector<std::string> GetAllNodeIds() const;
    
    // =============================================================================
    // CONNECTION MANAGEMENT
    // =============================================================================
    
    void AddConnection(const std::string& id, const std::string& sourceId, const std::string& targetId);
    void AddConnection(const std::string& id, const std::string& sourceId, const std::string& targetId, 
                      FlowChartConnectionStyle style, FlowChartArrowStyle arrowStyle = FlowChartArrowStyle::Arrow);
    void RemoveConnection(const std::string& id);
    void SetConnectionLabel(const std::string& id, const std::string& label);
    void SetConnectionStyle(const std::string& id, FlowChartConnectionStyle style);
    void SetConnectionLineStyle(const std::string& id, FlowChartLineStyle style);
    void SetConnectionColor(const std::string& id, const Color& color);
    void SetConnectionWidth(const std::string& id, double width);
    
    FlowChartConnection* GetConnection(const std::string& id);
    std::vector<std::string> GetAllConnectionIds() const;
    
    // =============================================================================
    // STYLING & THEME
    // =============================================================================
    
    void SetTheme(FlowChartTheme theme);
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible, double spacing = 20.0f);
    void SetGridColor(const Color& color);
    void SetSnapToGrid(bool enable);
    void SetFontFamily(const std::string& fontFamily);
    void SetFontSize(double size);
    
    // =============================================================================
    // INTERACTION
    // =============================================================================
    
    void SelectNode(const std::string& id);
    void DeselectAll();
    void Clear();
    void DeleteSelected();
    
    std::string GetSelectedNodeId() const { return selectedNodeId; }
    
    void SetZoomLevel(double zoom);
    void SetPanOffset(double x, double y);
    double GetZoomLevel() const { return zoomLevel; }
    Point2Df GetPanOffset() const { return panOffset; }
    
    void SetCreateNodeMode(bool enable);
    void SetPendingNodeShape(FlowChartShape shape);
    FlowChartShape GetPendingNodeShape() const { return pendingNodeShape; }
    void SetEditMode(EditMode mode);
    EditMode GetEditMode() const { return currentMode; }
    
    // =============================================================================
    // RENDERING
    // =============================================================================
    
    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    
    // =============================================================================
    // CALLBACKS
    // =============================================================================
    
    std::function<void(const std::string&)> onNodeClick;
    std::function<void(const std::string&)> onNodeDoubleClick;
    std::function<void(const std::string&, double, double)> onNodeDragged;
    std::function<void(const std::string&)> onConnectionClick;
    std::function<void(const std::string&)> onNodeCreated;
    std::function<void(const std::string&)> onNodeSelected;
    std::function<void(const std::string&, const std::string&)> onConnectionCreated;
    std::function<void(EditMode)> onEditModeChanged;

private:
    // =============================================================================
    // DATA MEMBERS
    // =============================================================================
    
    std::map<std::string, FlowChartNode> nodes;
    std::vector<FlowChartConnection> connections;
    
    FlowChartStyle style;
    FlowChartTheme theme = FlowChartTheme::Default;
    
    std::string selectedNodeId;
    std::string selectedConnectionId;
    std::string hoveredNodeId;
    
    bool isDraggingNode = false;
    bool isPanning = false;
    
    bool isCreatingNode = false;
    bool isCreatingConnection = false;
    std::string connectionSourceId;
    double connectionEndX = 0.0f, connectionEndY = 0.0f;
    double dragOffsetX = 0.0f, dragOffsetY = 0.0f;
    
    FlowChartShape pendingNodeShape = FlowChartShape::Rectangle;
    EditMode currentMode = EditMode::Select;
    bool snapToGrid = false;
    
    int nextNodeId = 1;
    int nextConnId = 1;
    Point2Di dragStartPos;
    Point2Df panStartOffset;
    Point2Df dragStartWorldPos;
    
    double zoomLevel = 1.0f;
    Point2Df panOffset;
    
    // =============================================================================
    // RENDERING HELPERS
    // =============================================================================
    
    void RenderGrid(IRenderContext* ctx);
    void RenderConnections(IRenderContext* ctx);
    void RenderNodes(IRenderContext* ctx);
    void RenderConnectionPreview(IRenderContext* ctx);
    void RenderNodeShape(IRenderContext* ctx, const FlowChartNode& node);
    void RenderNodeText(IRenderContext* ctx, const FlowChartNode& node);
    void RenderSelectionHighlight(IRenderContext* ctx, const FlowChartNode& node, const Color& color, double width);
    void RenderConnection(IRenderContext* ctx, const FlowChartConnection& conn);
    void RenderStraightLine(IRenderContext* ctx, const Point2Df& start, const Point2Df& end);
    // Computes the orthogonal path from `start` (on `sourceSide` of source) to
    // `end` (on `targetSide` of target), avoiding all other nodes. Falls back
    // to cardinal-aware L/Z routing if A* cannot find a path. The exclusion
    // set lists node ids that A* must NOT treat as obstacles (typically the
    // source and target nodes themselves so the line can leave/enter them).
    std::vector<Point2Df> ComputeOrthogonalPath(const Point2Df& start, const Point2Df& end,
                                                CardinalSide sourceSide, CardinalSide targetSide,
                                                const std::string& sourceId, const std::string& targetId);
    // Cardinal-aware L/Z fallback (the 2.1.4 behavior). Used when A* fails
    // or when no obstacle is in the way.
    std::vector<Point2Df> ComputeCardinalPath(const Point2Df& start, const Point2Df& end,
                                              CardinalSide sourceSide, CardinalSide targetSide) const;
    // A* over the chart's grid. Returns empty vector if no path exists.
    std::vector<Point2Df> RouteAStar(const Point2Df& start, const Point2Df& end,
                                     CardinalSide sourceSide, CardinalSide targetSide,
                                     const std::string& sourceId, const std::string& targetId);
    // True if the cardinal L/Z path between start and end intersects any
    // node not in {sourceId, targetId}. Used to decide whether to call A*.
    bool PathHasObstacles(const std::vector<Point2Df>& path,
                          const std::string& sourceId, const std::string& targetId) const;
    // Draws a polyline from the given waypoints.
    void RenderOrthogonalLine(IRenderContext* ctx, const std::vector<Point2Df>& waypoints);
    // Angle of the final segment (waypoints[n-2] -> waypoints[n-1]).
    double ComputeIncomingAngle(const std::vector<Point2Df>& waypoints,
                               FlowChartConnectionStyle style, CardinalSide targetSide) const;
    // Anchor for the connection label: midpoint of the longest segment in
    // the path. Avoids placing the label on a corner.
    Point2Df ComputeOrthogonalLabelAnchor(const std::vector<Point2Df>& waypoints) const;
    void RenderCurvedLine(IRenderContext* ctx, const Point2Df& start, const Point2Df& end);
    void RenderArrowHead(IRenderContext* ctx, const Point2Df& tip, double angle, const Color& color, double size);
    void RenderDiamondArrow(IRenderContext* ctx, const Point2Df& tip, double angle, const Color& color, double size);
    // Renders the connection label centered on `anchor`. For orthogonal lines
    // `anchor` is the midpoint of the longest segment; for straight/curved
    // lines it is the geometric midpoint.
    void RenderConnectionLabel(IRenderContext* ctx, const FlowChartConnection& conn, const Point2Df& anchor);
    void RenderArrow(IRenderContext* ctx, double x1, double y1, double x2, double y2, const Color& color);
    
    // =============================================================================
    // HIT TESTING
    // =============================================================================
    
    std::string FindNodeAt(const Point2Df& pos);
    std::string FindConnectionAt(const Point2Df& pos);
    bool PointInNode(const FlowChartNode& node, const Point2Df& pos);
    double DistanceToLineSegment(const Point2Df& point, const Point2Df& lineStart, const Point2Df& lineEnd);
    
    // =============================================================================
    // UTILITY
    // =============================================================================
    
    // Returns the point on the silhouette of `node` where a line coming from
    // `otherCenter` should attach. Behavior depends on `style`:
    //   * Straight: shape-aware silhouette intersection (Diamond rhombus,
    //     Oval/Circle/Ellipse ellipse, others bbox).
    //   * Orthogonal/Curved: midpoint of the cardinal side facing
    //     `otherCenter`. For Diamond/Decision the midpoints of the four sides
    //     are the rhombus vertices, which is the visually correct exit.
    Point2Df GetConnectionPoint(const FlowChartNode& node, const Point2Df& otherCenter,
                                FlowChartConnectionStyle style) const;
    Point2Df GetNodeCenter(const FlowChartNode& node) const;
    // Picks Top/Right/Bottom/Left based on the angle from nodeCenter to otherCenter.
    CardinalSide GetCardinalSide(const Point2Df& nodeCenter, const Point2Df& otherCenter) const;
    // Returns the midpoint of the given cardinal side of the node's bbox.
    Point2Df GetCardinalPoint(const FlowChartNode& node, CardinalSide side) const;
    Point2Df GetArrowEndPoint(const Point2Df& start, const Point2Df& end, FlowChartConnectionStyle style) const;
    void GetConnectionPoints(const std::string& sourceId, const std::string& targetId, double& x1, double& y1, double& x2, double& y2);
    Point2Df ScreenToWorld(const Point2Di& screenPos) const;
    Point2Df ScreenToWorld(const Point2Df& screenPos) const;
    bool IsNodeSelected(const std::string& nodeId) const;
    double CalculateAngle(const Point2Df& from, const Point2Df& to);
    std::string GenerateUniqueId(const std::string& prefix);
    std::string GenerateNodeId();
    std::string GenerateConnectionId();
    double SnapToGrid(double value);
    void ApplyThemeColors(FlowChartNode& node, FlowChartTheme t);
    
    // =============================================================================
    // EVENT HANDLERS
    // =============================================================================
    
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleDoubleClick(const UCEvent& event);
    
    // =============================================================================
    // THEME
    // =============================================================================
    
    void ApplyTheme(FlowChartTheme theme);
    void ApplyDefaultTheme();
    void ApplyProfessionalTheme();
    void ApplyColorfulTheme();
    void ApplyMinimalTheme();
    void ApplyDarkTheme();
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasFlowChart> CreateFlowChart(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasFlowChart>(id, x, y, w, h);
}

} // namespace UltraCanvas