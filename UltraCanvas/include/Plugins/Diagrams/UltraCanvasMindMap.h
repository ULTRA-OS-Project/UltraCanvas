// include/Plugins/Diagrams/UltraCanvasMindMap.h
// Interactive mind map element
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// CHANGELOG 1.0.0 (initial - MindMap proposal Phase 1):
//  - Topic tree editing on a pan/zoom canvas: balanced, logic, org chart and
//    radial structures over UltraCanvasMindMapLayout.
//  - Styling by LEVEL (root / main / sub / leaf) with a per-branch colour
//    cascade, plus per-topic overrides. Six built-in themes.
//  - Node shapes: rounded rect, rect, pill, circle, ellipse, diamond, hexagon,
//    cloud, bang, underline and text-only.
//  - Connectors: straight, curve, elbow, rounded elbow and tapered organic
//    branch, with per-connector colour modes and end decorations.
//  - Content: image nodes, inline icons and automatic outline numbering.
//  - Editing: in-place text editing through an embedded UltraCanvasTextInput
//    overlay, Enter/Tab authoring, drag-to-reparent, collapse/expand and a
//    full undo/redo stack.
//  - Exchange: JSON round-trip (UltraCanvasJSON) and Markdown outline import
//    and export.
//  - Viewport, minimap and controls come from the shared
//    UltraCanvasDiagramViewport - see UltraCanvasDiagramViewport.md.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasTextInput.h"
#include "Plugins/Diagrams/UltraCanvasDiagramViewport.h"
#include "Plugins/Diagrams/UltraCanvasMindMapLayout.h"
#include "Plugins/Diagrams/UltraCanvasMindMapIO.h"
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <set>
#include <deque>
#include <chrono>
#include <unordered_map>

namespace UltraCanvas {

// =============================================================================
// STYLING
// =============================================================================

enum class MindMapTheme {
    Default,
    Professional,
    Colorful,
    Pastel,
    Dark,
    HandDrawn
};

enum class MindMapConnectorStyle {
    Straight,
    Curve,          // Cubic bezier, the classic mind map look
    Elbow,          // Orthogonal, sharp corners
    RoundedElbow,   // Orthogonal with rounded corners
    TaperedBranch   // Organic branch, thick at the parent and thin at the child
};

// Where a connector takes its colour from.
enum class MindMapConnectorColorMode {
    InheritFromBranch,   // The branch hue (default)
    InheritFromChild,    // The child node's border colour
    Fixed                // Always MindMapStyle::connectorColor
};

// Where a connector meets a node (R4).
enum class MindMapConnectorAttach {
    Perimeter,   // Nearest point on the node outline (default)
    Baseline     // The node's bottom edge, the classic mind map convention
};

enum class MindMapEndDecoration {
    NoDecoration,   // Not "None" - X11 defines None as a macro
    Arrow,
    Dot,
    Square
};

// How a topic's ordinal is displayed (C8).
enum class MindMapNumbering {
    Off,
    Prefix,   // "1.2 Topic text"
    Badge     // Filled circle in the branch hue, drawn before the node
};

struct MindMapStyle {
    std::string fontFamily = "Arial";

    Color backgroundColor = Color(252, 252, 253, 255);
    bool  showGrid = false;
    Color gridColor = Color(235, 235, 240, 255);
    double gridSpacing = 25.0;

    // Connectors
    MindMapConnectorStyle connectorStyle = MindMapConnectorStyle::Curve;
    MindMapConnectorColorMode connectorColorMode = MindMapConnectorColorMode::InheritFromBranch;
    Color connectorColor = Color(150, 150, 160, 255);
    double connectorWidth = 2.0;
    double taperStartWidth = 6.0;     // TaperedBranch width at the parent
    double taperEndWidth   = 1.5;     // ...and at the child
    MindMapConnectorAttach connectorAttach = MindMapConnectorAttach::Perimeter;
    MindMapEndDecoration endDecoration = MindMapEndDecoration::NoDecoration;
    double endDecorationSize = 5.0;
    // Labels on branch connectors (R9). Relationship labels are always drawn.
    bool showConnectorLabels = true;

    // Selection & hover
    Color selectionColor = Color(0, 120, 215, 255);
    double selectionWidth = 2.5;
    Color hoverColor = Color(0, 120, 215, 90);
    Color dropIndicatorColor = Color(0, 170, 90, 255);

    // Collapse handle
    bool showCollapseHandles = true;
    double collapseHandleRadius = 7.0;
    Color collapseHandleFill = Color(255, 255, 255, 255);
    Color collapseHandleBorder = Color(140, 140, 150, 255);
    Color collapseHandleGlyph = Color(70, 70, 80, 255);
    bool showChildCountWhenCollapsed = true;

    // Numbering
    MindMapNumbering numbering = MindMapNumbering::Off;

    // Per-depth lightening of the branch hue (image 9's cluster look).
    // 0 disables; 0.15 lightens each level by 15% towards white.
    double depthLightenStep = 0.0;

    // ---- Drop shadows (S8) ----
    bool   showShadows = false;
    Color  shadowColor = Color(0, 0, 0, 45);
    double shadowOffsetX = 2.0;
    double shadowOffsetY = 3.0;

    // ---- Connector badges (R7) ----
    // A circular badge drawn on the connector, carrying the child's ordinal or
    // its icon. `connectorBadgeT` is the position along the path, 0 at the
    // parent and 1 at the child.
    bool   showConnectorBadges = false;
    double connectorBadgeT = 0.5;
    double connectorBadgeRadius = 9.0;
    Color  connectorBadgeTextColor = Color(255, 255, 255, 255);

    // ---- Content indicators ----
    bool showNoteIndicator = true;    // C4
    bool showLinkIndicator = true;    // C6
    bool showMarkers = true;          // C3
    double markerSize = 12.0;

    // ---- Hover (I11) ----
    // Fades every topic outside the hovered topic's branch.
    bool   dimUnrelatedOnHover = false;
    double dimOpacity = 0.28;

    // ---- Callout cards (C5) ----
    Color calloutLeaderColor = Color(160, 160, 172, 255);
    double calloutLeaderWidth = 1.2;
    bool   calloutLeaderDashed = true;

    // ---- Animated re-layout (L14) ----
    // Topics tween from their old positions to the new ones after a layout
    // change. 0 disables.
    double layoutAnimationMs = 0.0;

    // ---- Background layer (S9) ----
    // Drawn in WORLD space beneath the map, before the grid. The rect passed is
    // the currently visible world region.
    std::function<void(IRenderContext*, const Rect2Dd&)> backgroundRenderer;
};

// =============================================================================
// ELEMENT
// =============================================================================

class UltraCanvasMindMap : public UltraCanvasUIElement {
public:
    UltraCanvasMindMap(const std::string& id, int x, int y, int width, int height);
    ~UltraCanvasMindMap() override;

    bool AcceptsFocus() const override { return true; }

    // =========================================================================
    // MODEL
    // =========================================================================

    std::string SetCentralTopic(const std::string& text);
    std::string AddTopic(const std::string& parentId, const std::string& text);
    bool        AddTopic(const MindMapTopic& topic);
    std::string AddFloatingTopic(const std::string& text, double x, double y);
    void        RemoveTopic(const std::string& id);
    bool        MoveTopic(const std::string& id, const std::string& newParentId, int index = -1);
    void        SetTopicText(const std::string& id, const std::string& text);
    void        BuildFromOutline(const std::vector<std::pair<int, std::string>>& outline);
    void        Clear();

    MindMapTopic*       GetTopic(const std::string& id);
    const MindMapTopic* GetTopic(const std::string& id) const;
    const std::string&  GetRootId() const { return model.GetRootId(); }
    size_t              GetTopicCount() const { return model.GetTopicCount(); }
    MindMapModel&       Model() { return model; }
    const MindMapModel& Model() const { return model; }

    // Relationships (D8)
    bool AddRelationship(const std::string& id, const std::string& fromId,
                         const std::string& toId, const std::string& label = "");
    bool AddRelationship(const MindMapRelationship& relationship);
    bool RemoveRelationship(const std::string& id);

    // Content
    void SetTopicMarkers(const std::string& id, const std::vector<std::string>& markers);
    void AddTopicMarker(const std::string& id, const std::string& marker);
    void ClearTopicMarkers(const std::string& id);
    void SetTopicLink(const std::string& id, const std::string& link);
    void SetTopicConnectorLabel(const std::string& id, const std::string& label);
    // Free-form application data (D6).
    void SetTopicUserData(const std::string& id, const std::string& userData);
    std::string GetTopicUserData(const std::string& id) const;
    void SetTopicAttribute(const std::string& id, const std::string& key,
                           const std::string& value);
    std::string GetTopicAttribute(const std::string& id, const std::string& key,
                                  const std::string& fallback = std::string()) const;
    // Callout card (C5): a floating topic that points at `targetId`.
    std::string AddCallout(const std::string& targetId, const std::string& text,
                           double x, double y);
    void SetTopicIcon(const std::string& id, const std::string& iconPath);
    void SetTopicImage(const std::string& id, const std::string& imagePath);
    void SetTopicNote(const std::string& id, const std::string& note);
    void SetTopicWeight(const std::string& id, double weight);

    // =========================================================================
    // LAYOUT
    // =========================================================================

    void SetStructure(MindMapStructure structure);
    MindMapStructure GetStructure() const { return layoutOptions.structure; }
    void SetBalancePolicy(MindMapBalancePolicy policy);
    void SetSpacing(double siblingGap, double levelGap, double rootGap);
    void SetLayoutOptions(const MindMapLayoutOptions& options);
    const MindMapLayoutOptions& GetLayoutOptions() const { return layoutOptions; }
    void SetTopicSide(const std::string& id, MindMapTopicSide side);

    // Forces a re-layout now. Layout is normally lazy: mutations mark it dirty
    // and Render() recomputes at most once per frame (A3).
    void RunLayout();
    void SetAutoFitOnLayout(bool autoFit) { autoFitOnLayout = autoFit; }

    // =========================================================================
    // STYLE
    // =========================================================================

    void SetTheme(MindMapTheme theme);
    MindMapTheme GetTheme() const { return theme; }
    void SetStyle(const MindMapStyle& style);
    const MindMapStyle& GetStyle() const { return style; }
    MindMapStyle& Style() { return style; }

    // Level 0 is the central topic, 1 the main topics, and so on. The deepest
    // configured level applies to everything beyond it.
    void SetLevelStyle(int level, const MindMapTopicStyle& topicStyle);
    MindMapTopicStyle GetLevelStyle(int level) const;
    void SetTopicStyle(const std::string& id, const MindMapTopicStyle& topicStyle);
    void ClearTopicStyle(const std::string& id);

    void SetBranchPalette(const std::vector<Color>& palette);
    const std::vector<Color>& GetBranchPalette() const { return branchPalette; }
    Color GetBranchColor(int branchIndex) const;

    void SetConnectorStyle(MindMapConnectorStyle connectorStyle);
    void SetNumbering(MindMapNumbering numbering);
    void SetBackgroundColor(const Color& color);
    void SetFontFamily(const std::string& family);

    // Resolved style for a topic: per-topic override, then level style, with the
    // branch hue and depth lightening applied.
    MindMapTopicStyle ResolveTopicStyle(const MindMapTopic& topic) const;

    // =========================================================================
    // SELECTION
    // =========================================================================

    void SelectTopic(const std::string& id, bool addToSelection = false);
    void SelectSubtree(const std::string& id);
    void SelectAll();
    void DeselectAll();
    bool IsTopicSelected(const std::string& id) const;
    std::vector<std::string> GetSelectedTopicIds() const;
    std::string GetPrimarySelectedId() const { return primarySelection; }

    // =========================================================================
    // COLLAPSE & NAVIGATION
    // =========================================================================

    void SetCollapsed(const std::string& id, bool collapsed);
    void ToggleCollapsed(const std::string& id);
    bool IsCollapsed(const std::string& id) const;
    void ExpandAll();
    void CollapseAll();
    void ExpandToLevel(int level);

    void CenterOnTopic(const std::string& id);
    void FitView(double padding = 50.0);
    void RevealTopic(const std::string& id);   // Expands ancestors, then centres
    std::vector<std::string> FindTopics(const std::string& text) const;

    // Viewport passthroughs
    void SetZoomLevel(double zoom);
    double GetZoomLevel() const { return viewport.GetZoomLevel(); }
    void ZoomIn(double factor = 1.2);
    void ZoomOut(double factor = 1.2);
    void SetMinimapVisible(bool visible);
    void SetControlsVisible(bool visible);
    UltraCanvasDiagramViewport& Viewport() { return viewport; }

    // =========================================================================
    // EDITING
    // =========================================================================

    void SetEditable(bool editable);
    bool IsEditable() const { return editable; }

    void BeginEditTopic(const std::string& id);
    void CommitEdit();
    void CancelEdit();
    bool IsEditing() const { return !editingTopicId.empty(); }

    // Creates a sibling after id (Enter) or a child of id (Tab), selects it and
    // starts editing. Returns the new topic id.
    std::string CreateSibling(const std::string& id, const std::string& text = "");
    std::string CreateChild(const std::string& id, const std::string& text = "");
    void DeleteSelected();

    // Clipboard (I7). Copy/cut place the subtree on an internal clipboard as
    // JSON; paste grafts it under the target with fresh ids so the same subtree
    // can be pasted repeatedly.
    bool CopySelection();
    bool CutSelection();
    bool PasteInto(const std::string& targetParentId);
    bool HasClipboardContent() const { return !clipboardJson.empty(); }
    // Pastes an indented plain-text outline as a subtree.
    bool PasteOutlineInto(const std::string& targetParentId, const std::string& outlineText);

    void Undo();
    void Redo();
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    void SetUndoLimit(size_t limit) { undoLimit = limit; }

    // =========================================================================
    // EXCHANGE
    // =========================================================================

    std::string ToJson() const;
    bool FromJson(const std::string& json);
    std::string ToMarkdown() const;
    bool FromMarkdown(const std::string& markdown);

    // Interchange formats (X3-X8), delegated to UltraCanvasMindMapIO. Each
    // import replaces the whole map and is undoable; a failed import leaves the
    // current map untouched and returns false.
    bool FromMermaid(const std::string& source);
    std::string ToMermaid() const;
    bool FromFreeMind(const std::string& xml);
    std::string ToFreeMind() const;
    bool FromOpml(const std::string& xml);
    std::string ToOpml(const std::string& title = "Mind Map") const;
    bool FromIndentedText(const std::string& text, int spacesPerLevel = 2);
    bool FromCsv(const std::string& csv);
    std::string ToCsv() const;
    bool FromXMindFile(const std::string& filePath);

    // Sniffs the payload and dispatches to the matching importer.
    bool ImportAuto(const std::string& text);

    // SVG export of the current layout (X7).
    std::string ToSvg(double scale = 1.0) const;
    bool ExportSvg(const std::string& filePath, double scale = 1.0) const;
    // Indented reading order, for accessibility and quick dumps (A6).
    std::string ToOutlineText() const;

    // =========================================================================
    // RENDERING & EVENTS
    // =========================================================================

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    std::function<void(const std::string&)> onTopicClick;
    std::function<void(const std::string&)> onTopicDoubleClick;
    std::function<void(const std::string&, const std::string&)> onTopicTextChanged;
    std::function<void(const std::string&)> onTopicAdded;
    std::function<void(const std::string&)> onTopicRemoved;
    std::function<void(const std::string&, const std::string&)> onTopicMoved;
    std::function<void(const std::string&, bool)> onCollapseChanged;
    std::function<void(const std::vector<std::string>&)> onSelectionChanged;
    std::function<void(double, double, double)> onViewportChanged;
    std::function<void(const std::string&, double, double)> onTopicRightClick;
    std::function<void(double, double)> onCanvasRightClick;
    // Fires when the link indicator on a topic is clicked (C6).
    std::function<void(const std::string&, const std::string&)> onTopicLinkActivated;

private:
    // ---- Layout ----
    void EnsureLayout();
    void MarkLayoutDirty() { layoutDirty = true; }
    void SyncViewportSize();
    DiagramContentBounds ComputeContentBounds() const;
    Size2Dd MeasureText(IRenderContext* ctx, const std::string& text,
                        double fontSize, bool bold, double maxWidth) const;

    // ---- Rendering ----
    void RenderGrid(IRenderContext* ctx);
    void RenderConnectors(IRenderContext* ctx);
    void RenderConnector(IRenderContext* ctx, const MindMapTopic& parent,
                         const MindMapTopic& child);
    void RenderRelationships(IRenderContext* ctx);
    void RenderTopics(IRenderContext* ctx);
    void RenderTopicShape(IRenderContext* ctx, const MindMapTopic& topic,
                          const MindMapTopicStyle& topicStyle);
    void RenderTopicContent(IRenderContext* ctx, const MindMapTopic& topic,
                            const MindMapTopicStyle& topicStyle);
    void RenderCollapseHandle(IRenderContext* ctx, const MindMapTopic& topic);
    void RenderTopicIndicators(IRenderContext* ctx, const MindMapTopic& topic,
                               const MindMapTopicStyle& topicStyle);
    void RenderConnectorBadge(IRenderContext* ctx, const MindMapTopic& child,
                              const Point2Dd& from, const Point2Dd& to, const Color& color);
    // Opacity a topic renders at, accounting for hover dimming (I11).
    double TopicOpacity(const MindMapTopic& topic) const;
    // Screen-space rect of the link indicator, for hit testing.
    Rect2Dd LinkIndicatorRect(const MindMapTopic& topic) const;
    std::string FindLinkIndicatorAt(const Point2Di& localPos) const;
    void RenderDropIndicator(IRenderContext* ctx);
    void RenderCallouts(IRenderContext* ctx);
    void RenderConnectorLabel(IRenderContext* ctx, const MindMapTopic& child,
                              const Point2Dd& from, const Point2Dd& to);
    // Animation (L14): captures the pre-layout positions and, while a tween is
    // running, returns the interpolated box for a topic.
    void CaptureAnimationStart();
    bool IsAnimating() const;
    Rect2Dd AnimatedBounds(const std::string& id, const Rect2Dd& target) const;
    void RenderEndDecoration(IRenderContext* ctx, const Point2Dd& tip,
                             const Point2Dd& direction, const Color& color);

    // ---- Geometry ----
    // Point on a node's perimeter in the direction of `towards` (R3).
    Point2Dd PerimeterPoint(const MindMapTopic& topic, const MindMapTopicStyle& topicStyle,
                            const Point2Dd& towards) const;
    Point2Dd TopicCenter(const MindMapTopic& topic) const;
    Rect2Dd CollapseHandleRect(const MindMapTopic& topic) const;

    // ---- Hit testing (all in element-local coordinates) ----
    std::string FindTopicAt(const Point2Di& localPos) const;
    std::string FindCollapseHandleAt(const Point2Di& localPos) const;
    bool PointInTopic(const MindMapTopic& topic, const Point2Dd& worldPos) const;

    // ---- Events ----
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);
    bool HandleDoubleClick(const UCEvent& event);

    // ---- Editing overlay ----
    void EnsureEditOverlay();
    void PositionEditOverlay();

    // ---- Undo ----
    struct UndoEntry {
        std::string json;
        std::string label;
    };
    void PushUndo(const std::string& label);
    void ApplySnapshot(const std::string& json);

    // Builds the SVG styling options from the element's own style.
    MindMapSvgOptions MakeSvgOptions(double scale) const;

    // ---- Notifications ----
    void NotifySelectionChanged();
    void NotifyViewportChanged();

    // ---- Themes ----
    void ApplyTheme(MindMapTheme newTheme);

    // =========================================================================
    // DATA
    // =========================================================================

    MindMapModel model;
    MindMapLayoutOptions layoutOptions;
    MindMapLayoutResult layoutResult;
    bool layoutDirty = true;
    bool autoFitOnLayout = true;
    bool pendingAutoFit = false;

    MindMapStyle style;
    MindMapTheme theme = MindMapTheme::Default;
    std::vector<MindMapTopicStyle> levelStyles;   // Index = level
    std::vector<Color> branchPalette;

    UltraCanvasDiagramViewport viewport;

    // Selection & hover
    std::set<std::string> selectedTopics;
    std::string primarySelection;
    std::string hoveredTopicId;
    std::string hoveredHandleTopicId;
    int hoveredControlButton = -1;

    // Interaction state
    bool editable = true;
    bool isDraggingViewport = false;
    bool isDraggingTopic = false;
    bool isDraggingMinimap = false;
    bool isMultiSelectKeyHeld = false;
    std::string dragTopicId;
    std::string dropTargetId;
    Point2Di dragStartPos;
    Point2Di lastMousePos;
    bool dragExceededThreshold = false;
    // Ctrl held when the drag began: drop on empty canvas creates a child (I9).
    bool dragCreateModifier = false;

    // In-place editing
    std::shared_ptr<UltraCanvasTextInput> editOverlay;
    std::string editingTopicId;
    bool editCommitting = false;   // Guards re-entrancy from onFocusLost

    // Clipboard
    std::string clipboardJson;

    // Undo/redo
    std::deque<UndoEntry> undoStack;
    std::deque<UndoEntry> redoStack;
    size_t undoLimit = 100;
    bool suppressUndo = false;

    // Animation state (L14)
    std::unordered_map<std::string, Rect2Dd> animationFrom;
    std::chrono::steady_clock::time_point animationStart;
    bool animationActive = false;

    // Cached render context for text measurement between frames.
    mutable IRenderContext* measureContext = nullptr;
};

// =============================================================================
// FACTORY
// =============================================================================

inline std::shared_ptr<UltraCanvasMindMap> CreateMindMap(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasMindMap>(id, x, y, w, h);
}

} // namespace UltraCanvas
