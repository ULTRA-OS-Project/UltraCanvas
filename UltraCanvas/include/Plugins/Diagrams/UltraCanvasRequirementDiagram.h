// include/Plugins/Diagrams/UltraCanvasRequirementDiagram.h
// SysML requirement diagram (req) component — requirements, traceability
// relationships, containment trees, callouts, legends and coverage analysis
// Version: 2.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// CHANGELOG 2.0.0 (phase 2 of the proposal — additive, no phase-1 breakage):
//  - REFACTOR: the data, semantics and interchange moved into RequirementModel
//    (UltraCanvasRequirementModel.h), following the proposal's "model first,
//    view second" principle. Every phase-1 method is kept on this class as a
//    forwarding wrapper, so existing callers are unaffected. GetModel() gives
//    direct access for headless work.
//  - NEW: endpoint legality per relationship kind, with Lenient (report and
//    flag) and Strict (reject) modes, and auto-correction of a backwards
//    satisfy/verify/refine.
//  - NEW: hierarchical id auto-numbering (AssignHierarchicalIds).
//  - NEW: SysML compartment notation — derived/derivedFrom/satisfiedBy/
//    verifiedBy/refinedBy/tracedTo/master lists computed from the relations.
//  - NEW: Folder, FoldedNote and StickFigure shapes; anchored «rationale» and
//    «problem» notes.
//  - NEW: obstacle-aware orthogonal routing (A* over a coarse grid) with a
//    silent fallback to the phase-1 Z route.
//  - NEW: layered (Sugiyama-style) layout for non-tree topologies.
//  - NEW: SysML diagram frame with the pentagon header tab.
//  - NEW: expand/collapse sub-trees, per-node detail toggle, trace
//    highlighting, and filters by kind/category/risk/status.
//  - NEW: editing — create node, drag-to-connect, delete, inline rename.
//  - NEW: coverage analysis and the uncovered/unverified overlay.
//  - NEW: Mermaid and CSV import/export (via the model).
//
// A requirement diagram is the structural view of a system's requirements:
// requirements as compartmented boxes, the containment hierarchy that
// decomposes them, and the typed relationships that trace design elements and
// test cases back to the requirements they satisfy or verify.
//
// Notation implemented (SysML v1.x):
//   * Containment  - solid line, crosshair circle at the PARENT end
//   * DeriveReqt   - dashed line, open arrowhead, «deriveReqt»
//   * Satisfy      - dashed line, open arrowhead, «satisfy»
//   * Verify       - dashed line, open arrowhead, «verify»
//   * Refine       - dashed line, open arrowhead, «refine»
//   * Trace        - dashed line, open arrowhead, «trace»
//   * Copy         - dashed line, open arrowhead, «copy»
//   * Generalization - solid line, hollow closed triangle at the target
//
// All dependency relationships point FROM the dependent element TO the
// requirement. Only containment points from parent to child.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Diagrams/UltraCanvasRequirementModel.h"
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <functional>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// VIEW ENUMERATIONS
// =============================================================================

enum class RequirementLayoutMode {
    Manual,           // positions set by the caller / by dragging
    ContainmentTree,  // tidy tree over the containment relationships
    Layered           // Sugiyama-style layers for non-tree topologies
};

enum class RequirementOrientation {
    TopDown,
    BottomUp,
    LeftRight,
    RightLeft
};

enum class RequirementPaletteKind {
    Classic,       // white boxes, black lines - textbook SysML
    Pastel,        // soft yellow/green fills
    Vibrant,       // saturated per-kind colours
    Professional,  // muted blue-grey
    Dark,
    Monochrome,    // print / greyscale
    Custom
};

// Where a node's fill colour comes from.
enum class RequirementColorSource {
    ByKind,        // palette entry for the node kind
    ByCategory,    // the category's colour
    ByRisk,        // green / amber / red from the risk property
    ByStatus,      // status string mapped through the status colour map
    Explicit       // RequirementNode::fillColor only
};

enum class RequirementPanelPosition {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// What the legend lists.
enum class RequirementLegendSource {
    Categories,     // one row per category actually used on the diagram
    NodeKinds,      // one row per node kind actually used
    RelationKinds,  // one row per relationship kind actually used
    Coverage,       // satisfied / unsatisfied / unverified swatches
    Custom          // rows supplied via SetLegendEntries()
};

// Interaction mode.
enum class RequirementEditMode {
    Select,           // select, drag, rubber-band
    CreateNode,       // click empty canvas to create a node
    CreateRelation,   // drag from one box to another to connect them
    Pan               // drag anywhere to pan
};

// What a double-click on a box does.
enum class RequirementDoubleClickAction {
    NoAction,
    ToggleDetail,     // Collapsed <-> Full  (default)
    ToggleCollapse,   // expand / collapse the sub-tree
    Rename            // start the inline name editor
};

// =============================================================================
// LEGEND, TITLE & FRAME
// =============================================================================

struct RequirementLegendEntry {
    std::string label;
    Color color = Color(200, 200, 200, 255);
    bool isLine = false;        // draw a line sample instead of a swatch
    bool dashed = false;
};

struct RequirementLegendConfig {
    bool visible = false;
    RequirementPanelPosition position = RequirementPanelPosition::BottomLeft;
    RequirementLegendSource source = RequirementLegendSource::Categories;
    std::string title = "Legend";
    double padding = 10.0;      // distance from the element edge
    double innerPadding = 8.0;
    double swatchSize = 12.0;
    double rowGap = 4.0;
    double fontSize = 10.0;
    Color backgroundColor = Color(255, 255, 255, 235);
    Color borderColor = Color(170, 170, 170, 255);
    Color textColor = Color(50, 50, 55, 255);
};

struct RequirementTitleConfig {
    bool visible = false;
    std::string title;
    std::string subtitle;
    double height = 34.0;
    double fontSize = 15.0;
    double subtitleFontSize = 10.0;
    Color backgroundColor = Color(64, 96, 160, 255);
    Color textColor = Color(255, 255, 255, 255);
    TextAlignment alignment = TextAlignment::Center;
};

// The SysML diagram frame: a border around the content with a pentagon header
// tab reading `req [Package] Name [Diagram Name]`.
struct RequirementFrameConfig {
    bool visible = false;
    std::string diagramKind = "req";     // the `req` in the header
    std::string elementType = "Package"; // the bracketed model element type
    std::string elementName;             // the model element the diagram is of
    std::string diagramName;             // the trailing [Diagram Name]
    double tabHeight = 22.0;
    double tabNotch = 12.0;              // width of the pentagon's cut corner
    double fontSize = 10.5;
    double margin = 6.0;                 // between the frame and the element edge
    Color borderColor = Color(110, 116, 128, 255);
    Color tabFillColor = Color(244, 246, 250, 255);
    Color textColor = Color(45, 50, 60, 255);
    double borderWidth = 1.2;
};

// =============================================================================
// PALETTE
// =============================================================================

struct RequirementPalette {
    Color backgroundColor = Color(252, 252, 253, 255);
    Color gridColor = Color(234, 236, 240, 255);

    // Per-kind box colours.
    Color requirementFill = Color(255, 255, 255, 255);
    Color requirementBorder = Color(70, 70, 80, 255);
    Color blockFill = Color(214, 232, 250, 255);
    Color blockBorder = Color(70, 110, 160, 255);
    Color testCaseFill = Color(214, 240, 236, 255);
    Color testCaseBorder = Color(60, 140, 130, 255);
    Color useCaseFill = Color(232, 222, 248, 255);
    Color useCaseBorder = Color(120, 90, 170, 255);
    Color actorFill = Color(245, 240, 220, 255);
    Color actorBorder = Color(160, 140, 80, 255);
    Color packageFill = Color(240, 240, 244, 255);
    Color packageBorder = Color(120, 120, 130, 255);
    Color noteFill = Color(255, 250, 214, 255);
    Color noteBorder = Color(190, 175, 100, 255);

    // Header band behind the stereotype/name lines. Alpha 0 = no band.
    Color headerBandColor = Color(0, 0, 0, 0);
    Color headerTextColor = Color(35, 38, 45, 255);
    Color stereotypeTextColor = Color(90, 95, 105, 255);
    Color propertyTextColor = Color(55, 58, 66, 255);
    Color compartmentHeadingColor = Color(95, 100, 112, 255);
    Color dividerColor = Color(180, 184, 194, 255);

    // Relationship colours.
    Color containmentColor = Color(70, 74, 84, 255);
    Color deriveReqtColor = Color(90, 110, 170, 255);
    Color satisfyColor = Color(60, 140, 90, 255);
    Color verifyColor = Color(170, 110, 60, 255);
    Color refineColor = Color(130, 90, 170, 255);
    Color traceColor = Color(130, 134, 144, 255);
    Color copyColor = Color(110, 130, 150, 255);
    Color generalizationColor = Color(70, 74, 84, 255);
    Color relationLabelColor = Color(60, 64, 74, 255);
    Color relationLabelBackground = Color(255, 255, 255, 225);

    // Risk colours for RequirementColorSource::ByRisk and the risk stripe.
    Color riskLowColor = Color(150, 205, 150, 255);
    Color riskMediumColor = Color(245, 215, 130, 255);
    Color riskHighColor = Color(240, 160, 150, 255);

    // Coverage overlay.
    Color coveredColor = Color(70, 160, 100, 255);
    Color uncoveredColor = Color(214, 96, 70, 255);
    Color unverifiedColor = Color(226, 168, 62, 255);

    Color selectionColor = Color(0, 120, 215, 255);
    Color hoverColor = Color(0, 150, 240, 255);
    Color warningColor = Color(220, 80, 60, 255);
    Color calloutFill = Color(255, 252, 226, 255);
    Color calloutBorder = Color(196, 182, 110, 255);

    // Cycled through for categories that were not given an explicit colour.
    std::vector<Color> categoryColors = {
        Color(252, 244, 205, 255),
        Color(214, 240, 224, 255),
        Color(214, 232, 250, 255),
        Color(240, 224, 240, 255),
        Color(226, 240, 214, 255),
        Color(248, 228, 214, 255)
    };

    // Returns a copy of one of the built-in palettes. For
    // RequirementPaletteKind::Custom the Classic palette is returned.
    static RequirementPalette BuiltIn(RequirementPaletteKind kind);

    Color FillForKind(RequirementNodeKind kind) const;
    Color BorderForKind(RequirementNodeKind kind) const;
    Color ColorForRelation(RequirementRelationKind kind) const;
    Color ColorForRisk(RequirementRisk risk) const;
};

// =============================================================================
// STYLE
// =============================================================================

struct RequirementDiagramStyle {
    std::string fontFamily = "Arial";
    double baseFontSize = 10.0;          // property rows
    double nameFontSize = 12.0;          // the bold name line
    double stereotypeFontSize = 9.0;     // the «...» line
    double relationLabelFontSize = 9.0;

    double nodeMinWidth = 130.0;
    double nodeMaxWidth = 230.0;
    double nodePadding = 7.0;
    double nodeCornerRadius = 5.0;
    double borderWidth = 1.2;
    double rowSpacing = 2.0;
    double headerGap = 4.0;              // between the header block and the divider
    int maxTextLines = 6;                // per property row before ellipsis

    double relationLineWidth = 1.3;
    double arrowSize = 9.0;
    double crosshairRadius = 7.0;        // containment ⊕
    double trianglSize = 11.0;           // generalisation triangle
    double toggleRadius = 6.0;           // expand/collapse ⊕ / ⊖

    bool showGrid = false;
    double gridSpacing = 25.0;

    // Layout
    double levelGap = 60.0;              // between tree levels
    double siblingGap = 22.0;            // between siblings
    double subtreeGap = 30.0;            // between adjacent subtrees

    // Obstacle-aware routing
    double routingGridSize = 12.0;       // A* cell size, world units
    double routingClearance = 8.0;       // margin kept around every box
    int routingMaxCells = 40000;         // node budget before falling back

    double selectionWidth = 2.5;
    double dimmedAlpha = 0.18;           // opacity of elements outside a trace
    Color selectionBoxFill = Color(0, 120, 215, 36);
    Color selectionBoxStroke = Color(0, 120, 215, 190);

    UCDashPattern relationDash = UCDashPattern({5.0, 4.0});
    UCDashPattern leaderDash = UCDashPattern({3.0, 3.0});
};

struct RequirementSnapGrid {
    bool enabled = false;
    double snapX = 10.0;
    double snapY = 10.0;
};

// =============================================================================
// REQUIREMENT DIAGRAM COMPONENT
// =============================================================================

class UltraCanvasRequirementDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasRequirementDiagram(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }

    // =========================================================================
    // MODEL ACCESS
    // =========================================================================

    // Direct access for headless work, bulk edits and the analysis API. Call
    // NotifyModelChanged() after mutating it directly so the view re-measures.
    RequirementModel& GetModel() { return model; }
    const RequirementModel& GetModel() const { return model; }
    void NotifyModelChanged();

    // =========================================================================
    // NODE MANAGEMENT
    // =========================================================================

    bool AddNode(const RequirementNode& node);
    bool AddNode(RequirementNodeKind kind, const std::string& id, const std::string& name);
    bool AddRequirement(const std::string& id, const std::string& name,
                        const std::string& text);
    bool AddRequirement(const std::string& id, const std::string& name,
                        const std::string& text, RequirementRisk risk,
                        RequirementVerifyMethod verifyMethod);

    bool RemoveNode(const std::string& id);
    bool RenameNode(const std::string& oldId, const std::string& newId);
    void Clear();

    RequirementNode* GetNode(const std::string& id);
    const RequirementNode* GetNode(const std::string& id) const;
    std::vector<std::string> GetAllNodeIds() const;
    size_t GetNodeCount() const { return model.GetNodeCount(); }

    void SetNodePosition(const std::string& id, double x, double y);
    void SetNodeSize(const std::string& id, double width, double height);
    void SetNodeDetail(const std::string& id, RequirementDetailLevel detail);
    void SetNodeCategory(const std::string& id, const std::string& category);
    void SetNodeColors(const std::string& id, const Color& fill, const Color& border);
    void SetNodePinned(const std::string& id, bool pinned);
    void SetNodeTemplate(const std::string& nodeId, const RequirementNodeTemplate& tpl);
    // Attaches a «rationale» / «problem» / note node to an element or relation,
    // drawn with a dashed leader line.
    void SetNoteAnchor(const std::string& noteId, const std::string& anchorId);

    std::string GenerateNodeId(const std::string& prefix = "REQ");

    // =========================================================================
    // RELATIONSHIP MANAGEMENT
    // =========================================================================

    std::string AddRelation(const RequirementRelation& relation);
    std::string AddRelation(RequirementRelationKind kind,
                            const std::string& sourceId, const std::string& targetId);
    std::string AddContainment(const std::string& parentId, const std::string& childId);

    bool RemoveRelation(const std::string& id);
    RequirementRelation* GetRelation(const std::string& id);
    const RequirementRelation* GetRelation(const std::string& id) const;
    std::vector<std::string> GetAllRelationIds() const;
    size_t GetRelationCount() const { return model.GetRelationCount(); }

    void SetRelationLabel(const std::string& id, const std::string& label);
    void SetRelationColor(const std::string& id, const Color& color);
    void SetRelationRouting(const std::string& id, RequirementRouting routing);
    void SetRelationVisible(const std::string& id, bool visible);
    void SetRelationKindVisible(RequirementRelationKind kind, bool visible);

    std::string GetParentId(const std::string& nodeId) const;
    std::vector<std::string> GetChildIds(const std::string& nodeId) const;
    std::vector<std::string> GetRootIds() const;
    std::vector<std::string> GetRelationsOf(const std::string& nodeId) const;

    void BuildFromRelations(const std::vector<RequirementNode>& nodeList,
                            const std::vector<RequirementRelation>& relationList);

    // =========================================================================
    // MODEL INTEGRITY
    // =========================================================================

    void SetSemanticsMode(RequirementSemanticsMode mode);
    RequirementSemanticsMode GetSemanticsMode() const { return model.GetSemanticsMode(); }
    bool HasContainmentCycle() const { return model.HasContainmentCycle(); }
    std::vector<std::string> GetContainmentCycleNodes() const {
        return model.GetContainmentCycleNodes();
    }
    std::vector<RequirementWarning> Validate() const { return model.Validate(); }
    int AssignHierarchicalIds(const std::string& prefix = "R");

    // =========================================================================
    // TRACEABILITY ANALYSIS
    // =========================================================================

    std::vector<std::string> GetUncoveredRequirements() const {
        return model.GetUncoveredRequirements();
    }
    std::vector<std::string> GetUnverifiedRequirements() const {
        return model.GetUnverifiedRequirements();
    }
    std::vector<std::string> GetOrphanRequirements() const {
        return model.GetOrphanRequirements();
    }
    RequirementCoverage GetCoverage() const { return model.GetCoverage(); }
    RequirementCoverage GetCoverageForCategory(const std::string& category) const {
        return model.GetCoverageForCategory(category);
    }
    std::set<std::string> GetTraceChain(const std::string& nodeId,
                                         RequirementTraceDirection direction,
                                         int depth = 0) const {
        return model.GetTraceChain(nodeId, direction, depth);
    }

    // Marks uncovered / unverified requirements with a coloured corner badge.
    void SetCoverageOverlayVisible(bool visible);
    bool IsCoverageOverlayVisible() const { return coverageOverlay; }

    // =========================================================================
    // TRACE HIGHLIGHTING
    // =========================================================================

    // Dims everything not reachable from `nodeId` within `depth` hops
    // (depth <= 0 = unlimited).
    void HighlightTraceChain(const std::string& nodeId,
                             RequirementTraceDirection direction = RequirementTraceDirection::Both,
                             int depth = 0);
    void ClearHighlight();
    bool HasHighlight() const { return highlightActive; }

    // =========================================================================
    // FILTERS
    // =========================================================================

    void SetKindVisible(RequirementNodeKind kind, bool visible);
    void SetCategoryVisible(const std::string& category, bool visible);
    void SetRiskVisible(RequirementRisk risk, bool visible);
    void SetStatusVisible(const std::string& status, bool visible);
    void ClearFilters();
    bool IsNodeDisplayed(const std::string& id) const;

    // =========================================================================
    // EXPAND / COLLAPSE
    // =========================================================================

    void SetNodeCollapsed(const std::string& id, bool collapsed);
    void ToggleNodeCollapsed(const std::string& id);
    bool IsNodeCollapsed(const std::string& id) const;
    void ExpandAll();
    void CollapseAll();

    // =========================================================================
    // COMPARTMENT TEMPLATE
    // =========================================================================

    void SetNodeTemplate(const RequirementNodeTemplate& tpl);
    const RequirementNodeTemplate& GetNodeTemplate() const { return nodeTemplate; }
    void SetPropertyFormat(RequirementPropertyFormat format);
    void SetDefaultDetailLevel(RequirementDetailLevel detail);
    RequirementDetailLevel GetDefaultDetailLevel() const { return defaultDetail; }

    // =========================================================================
    // CALLOUTS
    // =========================================================================

    bool AddCallout(const RequirementCallout& callout);
    bool AddCallout(const std::string& calloutId, const std::string& targetNodeId,
                    const std::vector<RequirementField>& fields, double x, double y);
    bool RemoveCallout(const std::string& id);
    RequirementCallout* GetCallout(const std::string& id);
    std::vector<std::string> GetAllCalloutIds() const;

    // =========================================================================
    // CATEGORIES & LEGEND
    // =========================================================================

    void AddCategory(const RequirementCategory& category);
    void AddCategory(const std::string& name, const Color& fill, const Color& border);
    const RequirementCategory* GetCategory(const std::string& name) const;
    std::vector<std::string> GetCategoryNames() const;

    void SetLegendVisible(bool visible);
    void SetLegendVisible(bool visible, RequirementPanelPosition position);
    void SetLegendSource(RequirementLegendSource source);
    void SetLegendEntries(const std::vector<RequirementLegendEntry>& entries);
    void SetLegendConfig(const RequirementLegendConfig& config);
    RequirementLegendConfig GetLegendConfig() const { return legendConfig; }

    // =========================================================================
    // TITLE & FRAME
    // =========================================================================

    void SetTitle(const std::string& title);
    void SetTitle(const std::string& title, const std::string& subtitle);
    void SetTitleVisible(bool visible);
    void SetTitleConfig(const RequirementTitleConfig& config);
    RequirementTitleConfig GetTitleConfig() const { return titleConfig; }

    // `req [Package] HSVSpecification [Requirements Diagram]`
    void SetFrame(const std::string& elementType, const std::string& elementName,
                  const std::string& diagramName);
    void SetFrameVisible(bool visible);
    void SetFrameConfig(const RequirementFrameConfig& config);
    RequirementFrameConfig GetFrameConfig() const { return frameConfig; }

    // =========================================================================
    // STYLING & THEME
    // =========================================================================

    void SetPalette(RequirementPaletteKind kind);
    void SetCustomPalette(const RequirementPalette& palette);
    const RequirementPalette& GetPalette() const { return palette; }

    void SetColorSource(RequirementColorSource source);
    RequirementColorSource GetColorSource() const { return colorSource; }
    void SetStatusColor(const std::string& status, const Color& color);

    void SetStyle(const RequirementDiagramStyle& style);
    const RequirementDiagramStyle& GetStyle() const { return style; }
    void SetFontFamily(const std::string& fontFamily);
    void SetFontSize(double propertyFontSize, double nameFontSize);
    void SetNodeWidthRange(double minWidth, double maxWidth);
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible, double spacing = 25.0);
    void SetRiskStripeVisible(bool visible);

    // =========================================================================
    // LAYOUT
    // =========================================================================

    void SetLayoutMode(RequirementLayoutMode mode);
    RequirementLayoutMode GetLayoutMode() const { return layoutMode; }
    void SetLayoutOrientation(RequirementOrientation orientation);
    RequirementOrientation GetLayoutOrientation() const { return orientation; }
    void SetLevelGap(double gap);
    void SetSiblingGap(double gap);
    void SetSubtreeGap(double gap);

    // Which relationship kinds define the hierarchy the layout works on.
    // Default: containment only. Everything else is merely routed.
    void SetLayoutRelationKinds(const std::set<RequirementRelationKind>& kinds);
    const std::set<RequirementRelationKind>& GetLayoutRelationKinds() const {
        return layoutRelationKinds;
    }

    void SetDefaultRouting(RequirementRouting routing);
    RequirementRouting GetDefaultRouting() const { return defaultRouting; }
    // Orthogonal routes avoid passing through other boxes (A* over a coarse
    // grid). Falls back silently to the direct Z route when no path exists or
    // the search budget is exceeded.
    void SetObstacleAvoidance(bool enabled);
    bool IsObstacleAvoidanceEnabled() const { return obstacleAvoidance; }

    void RunLayout();
    void SetAutoFitOnLayout(bool autoFit) { autoFitOnLayout = autoFit; }
    bool GetAutoFitOnLayout() const { return autoFitOnLayout; }

    Rect2Dd GetContentBounds() const;

    // =========================================================================
    // VIEWPORT
    // =========================================================================

    void SetZoomLevel(double zoom);
    double GetZoomLevel() const { return zoomLevel; }
    void SetPanOffset(double x, double y);
    Point2Dd GetPanOffset() const { return panOffset; }
    void ZoomIn(double factor = 1.2);
    void ZoomOut(double factor = 1.2);
    void FitView(double padding = 30.0);
    void CenterOn(double worldX, double worldY);
    void SetMinZoom(double z) { minZoom = z; }
    void SetMaxZoom(double z) { maxZoom = z; }

    // =========================================================================
    // SELECTION & INTERACTION
    // =========================================================================

    void SelectNode(const std::string& id, bool addToSelection = false);
    void SelectRelation(const std::string& id, bool addToSelection = false);
    void SelectAll();
    void DeselectAll();
    void DeleteSelected();
    std::vector<std::string> GetSelectedNodeIds() const;
    std::vector<std::string> GetSelectedRelationIds() const;
    bool IsNodeSelected(const std::string& id) const;
    bool IsRelationSelected(const std::string& id) const;

    void SetInteractive(bool interactive) { isInteractive = interactive; }
    bool IsInteractive() const { return isInteractive; }
    void SetNodesDraggable(bool draggable) { nodesDraggable = draggable; }
    void SetPanOnDrag(bool pan) { panOnDrag = pan; }
    void SetZoomOnScroll(bool zoom) { zoomOnScroll = zoom; }
    void SetSnapToGrid(bool enabled);
    void SetSnapGrid(double snapX, double snapY);
    bool IsSnapToGridEnabled() const { return snapGrid.enabled; }
    void SetTooltipsEnabled(bool enabled) { tooltipsEnabled = enabled; }

    // =========================================================================
    // EDITING
    // =========================================================================

    void SetEditMode(RequirementEditMode mode);
    RequirementEditMode GetEditMode() const { return editMode; }
    // Kind used by CreateNode clicks and by drag-to-connect.
    void SetPendingNodeKind(RequirementNodeKind kind) { pendingNodeKind = kind; }
    RequirementNodeKind GetPendingNodeKind() const { return pendingNodeKind; }
    void SetPendingRelationKind(RequirementRelationKind kind) { pendingRelationKind = kind; }
    RequirementRelationKind GetPendingRelationKind() const { return pendingRelationKind; }

    void SetDoubleClickAction(RequirementDoubleClickAction action) { doubleClickAction = action; }
    RequirementDoubleClickAction GetDoubleClickAction() const { return doubleClickAction; }

    // Inline name editor: typing goes into the node's name until Enter
    // (commit) or Escape (cancel).
    void BeginRename(const std::string& nodeId);
    void CommitRename();
    void CancelRename();
    bool IsRenaming() const { return !renamingNodeId.empty(); }

    // =========================================================================
    // SERIALIZATION
    // =========================================================================

    // Model, layout and the style/palette selection, via the framework JSON
    // module (DataFormats/UltraCanvasJSON.h).
    std::string ToJson(bool pretty = true) const;
    bool FromJson(const std::string& json);

    // Mermaid `requirementDiagram` and flat CSV, via the model. Importing
    // replaces the diagram's contents and re-runs the layout.
    std::string ToMermaid(const std::string& direction = "TB") const;
    bool FromMermaid(const std::string& text, std::string* outError = nullptr);
    std::string ToCsv(const RequirementCsvSchema& schema = RequirementCsvSchema()) const;
    bool FromCsv(const std::string& text,
                 const RequirementCsvSchema& schema = RequirementCsvSchema(),
                 std::string* outError = nullptr);

    // =========================================================================
    // RENDERING & EVENTS
    // =========================================================================

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    std::function<void(const std::string&)> onNodeClick;
    std::function<void(const std::string&)> onNodeDoubleClick;
    std::function<void(const std::string&)> onNodeHover;
    std::function<void(const std::string&, double, double)> onNodeDrag;
    std::function<void(const std::string&)> onRelationClick;
    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> onSelectionChange;
    std::function<void(double zoom, double panX, double panY)> onViewportChange;
    std::function<void(double worldX, double worldY)> onCanvasRightClick;
    std::function<void(const RequirementWarning&)> onValidationWarning;
    // Editing
    std::function<void(const std::string&)> onNodeCreated;
    std::function<void(const RequirementRelation&)> onRelationCreated;
    std::function<void(const std::string&, const std::string&)> onNodeRenamed;  // (id, newName)

private:
    // =========================================================================
    // MEASUREMENT
    // =========================================================================

    struct MeasuredLine {
        std::string text;
        double fontSize = 10.0;
        bool bold = false;
        bool centered = false;
        bool heading = false;
        Color color;
    };

    struct MeasuredNode {
        std::vector<MeasuredLine> headerLines;   // stereotype + name
        std::vector<MeasuredLine> bodyLines;     // property rows + compartments
        double headerHeight = 0.0;
        double bodyHeight = 0.0;
        bool hasDivider = false;
    };

    void MeasureAllNodes(IRenderContext* ctx);
    void MeasureNode(IRenderContext* ctx, RequirementNode& node, MeasuredNode& out);
    void MeasureCallout(IRenderContext* ctx, RequirementCallout& callout, MeasuredNode& out);
    std::vector<std::string> WrapText(IRenderContext* ctx, const std::string& text,
                                      double maxWidth, int maxLines) const;
    void EstimateNodeSize(RequirementNode& node) const;

    const RequirementNodeTemplate& TemplateForNode(const RequirementNode& node) const;
    std::vector<RequirementPropertyRow> RowsForNode(const RequirementNode& node) const;

    // =========================================================================
    // LAYOUT
    // =========================================================================

    void ApplyContainmentTreeLayout();
    double LayoutSubtree(const std::string& nodeId, int depth, double crossOffset,
                         std::map<int, double>& levelOrigin,
                         std::set<std::string>& visited);
    void ComputeLevelDepths(const std::string& nodeId, int depth,
                            std::map<int, double>& levelSize,
                            std::set<std::string>& visited);
    void ApplyLayeredLayout();
    void ApplyGridFallbackLayout();
    void OrientPositions();
    // Children as seen by the layout: containment plus any other kinds the
    // caller added through SetLayoutRelationKinds(), minus hidden nodes.
    std::vector<std::string> LayoutChildIds(const std::string& nodeId) const;
    std::vector<std::string> LayoutRootIds() const;

    // =========================================================================
    // VISIBILITY
    // =========================================================================

    // A node is drawn when it passes the filters and no ancestor is collapsed.
    bool IsDisplayed(const RequirementNode& node) const;
    bool IsHiddenByCollapse(const std::string& id) const;
    void ApplyFilters();

    // =========================================================================
    // RENDERING
    // =========================================================================

    void RenderGrid(IRenderContext* ctx);
    void RenderRelations(IRenderContext* ctx);
    void RenderContainmentBuses(IRenderContext* ctx);
    void RenderRelation(IRenderContext* ctx, const RequirementRelation& relation);
    void RenderRelationLabel(IRenderContext* ctx, const RequirementRelation& relation,
                             const std::vector<Point2Dd>& path);
    void RenderNodes(IRenderContext* ctx);
    void RenderNode(IRenderContext* ctx, const RequirementNode& node);
    void RenderNodeShape(IRenderContext* ctx, const RequirementNode& node,
                         const Color& fill, const Color& border, double borderWidth);
    void RenderNodeBody(IRenderContext* ctx, const RequirementNode& node,
                        const MeasuredNode& measured);
    void RenderNoteLeaders(IRenderContext* ctx);
    void RenderCollapseToggles(IRenderContext* ctx);
    void RenderCoverageBadges(IRenderContext* ctx);
    void RenderCallouts(IRenderContext* ctx);
    void RenderSelectionBox(IRenderContext* ctx);
    void RenderConnectionPreview(IRenderContext* ctx);
    void RenderRenameEditor(IRenderContext* ctx);
    void RenderTitle(IRenderContext* ctx);
    void RenderFrame(IRenderContext* ctx);
    void RenderLegend(IRenderContext* ctx);

    void DrawCrosshair(IRenderContext* ctx, const Point2Dd& center, const Color& color);
    void DrawOpenArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                           double dirX, double dirY, const Color& color);
    void DrawHollowTriangle(IRenderContext* ctx, const Point2Dd& tip,
                            double dirX, double dirY, const Color& color, const Color& fill);
    void DrawTextLine(IRenderContext* ctx, const MeasuredLine& line,
                      double x, double y, double boxWidth);

    Color ResolveFillColor(const RequirementNode& node) const;
    Color ResolveBorderColor(const RequirementNode& node) const;
    Color ResolveTextColor(const RequirementNode& node) const;
    RequirementNodeShape ResolveShape(const RequirementNode& node) const;
    Color ApplyDim(const Color& color, bool dimmed) const;
    std::vector<RequirementLegendEntry> BuildLegendEntries() const;

    // =========================================================================
    // GEOMETRY & ROUTING
    // =========================================================================

    enum class NodeFace { Top, Right, Bottom, Left };

    NodeFace ChooseFaceTowards(const RequirementNode& from, const RequirementNode& to) const;
    void CountFaceUsage(const std::string& nodeId, NodeFace face,
                        const std::string& relationId,
                        int& totalCount, int& myIndex) const;
    Point2Dd AnchorPoint(const RequirementNode& node, NodeFace face,
                         int slotIndex, int slotCount) const;
    void BuildRelationPath(const RequirementRelation& relation,
                           std::vector<Point2Dd>& outPath) const;
    void BuildOrthogonalPath(const Point2Dd& from, NodeFace fromFace,
                             const Point2Dd& to, NodeFace toFace,
                             std::vector<Point2Dd>& outPath) const;
    // A* over a coarse grid whose blocked cells are the other boxes. Returns
    // false (leaving outPath untouched) when no route is found in budget.
    bool BuildAvoidingPath(const RequirementRelation& relation,
                           const Point2Dd& from, NodeFace fromFace,
                           const Point2Dd& to, NodeFace toFace,
                           std::vector<Point2Dd>& outPath) const;

    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    Point2Di WorldToScreen(const Point2Dd& worldPos) const;
    Point2Dd SnapPoint(const Point2Dd& p) const;
    Rect2Dd NodeRect(const RequirementNode& node) const;
    Point2Dd CollapseTogglePosition(const RequirementNode& node) const;

    std::string FindNodeAt(const Point2Di& screenPos) const;
    std::string FindRelationAt(const Point2Di& screenPos) const;
    std::string FindCalloutAt(const Point2Di& screenPos) const;
    std::string FindCollapseToggleAt(const Point2Di& screenPos) const;
    bool PointInNode(const RequirementNode& node, const Point2Dd& worldPos) const;
    static double DistanceToSegment(const Point2Dd& p, const Point2Dd& a, const Point2Dd& b);

    // =========================================================================
    // EVENTS
    // =========================================================================

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);
    bool HandleTextInput(const UCEvent& event);

    void NotifySelectionChange();
    void NotifyViewportChange();
    void ClampZoom();
    void InvalidateMeasurement();
    void InvalidateRouting();

    // =========================================================================
    // DATA
    // =========================================================================

    RequirementModel model;

    // Measurement cache, keyed by node / callout id.
    std::map<std::string, MeasuredNode> measuredNodes;
    std::map<std::string, MeasuredNode> measuredCallouts;
    bool measurementDirty = true;
    bool layoutDirty = false;
    bool fitPending = false;
    // Bumped whenever any box moves, so cached routes are discarded.
    mutable unsigned long routingGeneration = 1;
    mutable std::map<std::string, std::pair<unsigned long, std::vector<Point2Dd>>> routeCache;

    RequirementNodeTemplate nodeTemplate;
    RequirementDetailLevel defaultDetail = RequirementDetailLevel::Full;
    RequirementDiagramStyle style;
    RequirementPalette palette;
    RequirementPaletteKind paletteKind = RequirementPaletteKind::Classic;
    RequirementColorSource colorSource = RequirementColorSource::ByKind;
    RequirementLegendConfig legendConfig;
    RequirementTitleConfig titleConfig;
    RequirementFrameConfig frameConfig;
    std::map<std::string, Color> statusColors;
    std::vector<RequirementLegendEntry> customLegendEntries;

    RequirementLayoutMode layoutMode = RequirementLayoutMode::Manual;
    RequirementOrientation orientation = RequirementOrientation::TopDown;
    RequirementRouting defaultRouting = RequirementRouting::Orthogonal;
    std::set<RequirementRelationKind> layoutRelationKinds = {
        RequirementRelationKind::Containment
    };
    bool autoFitOnLayout = true;
    bool showRiskStripe = false;
    bool coverageOverlay = false;
    bool obstacleAvoidance = false;

    // Filters
    std::set<RequirementNodeKind> hiddenKinds;
    std::set<std::string> hiddenCategories;
    std::set<RequirementRisk> hiddenRisks;
    std::set<std::string> hiddenStatuses;

    // Trace highlight
    bool highlightActive = false;
    std::set<std::string> highlightedNodes;

    // Selection & hover
    std::set<std::string> selectedNodes;
    std::set<std::string> selectedRelations;
    std::string hoveredNodeId;
    std::string hoveredRelationId;
    std::string hoveredCalloutId;
    std::string hoveredToggleId;

    // Mouse state
    bool isDraggingNode = false;
    bool isDraggingCallout = false;
    bool isDraggingViewport = false;
    bool isSelectingBox = false;
    bool isConnecting = false;
    std::string connectionSourceId;
    Point2Dd connectionEndPoint;
    std::string draggedCalloutId;
    Point2Di dragStartPos;
    Point2Di lastMousePos;
    std::map<std::string, Point2Dd> dragStartPositions;
    Point2Dd calloutDragStart;
    Point2Dd selectionBoxStart;
    Point2Dd selectionBoxEnd;

    // Editing
    RequirementEditMode editMode = RequirementEditMode::Select;
    RequirementNodeKind pendingNodeKind = RequirementNodeKind::Requirement;
    RequirementRelationKind pendingRelationKind = RequirementRelationKind::Satisfy;
    RequirementDoubleClickAction doubleClickAction = RequirementDoubleClickAction::ToggleDetail;
    std::string renamingNodeId;
    std::string renameBuffer;
    // True until the first keystroke after BeginRename(). While set, typing a
    // character replaces the whole name instead of appending to it - the
    // familiar "rename" behaviour from filers and spreadsheets.
    bool renameFresh = false;

    // Viewport
    double zoomLevel = 1.0;
    Point2Dd panOffset;
    double minZoom = 0.1;
    double maxZoom = 5.0;

    RequirementSnapGrid snapGrid;
    bool isInteractive = true;
    bool nodesDraggable = true;
    bool panOnDrag = true;
    bool zoomOnScroll = true;
    bool tooltipsEnabled = true;
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasRequirementDiagram> CreateRequirementDiagram(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasRequirementDiagram>(id, x, y, width, height);
}

} // namespace UltraCanvas
