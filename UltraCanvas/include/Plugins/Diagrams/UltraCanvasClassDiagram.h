// include/Plugins/Diagrams/UltraCanvasClassDiagram.h
// UML class diagram element
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework
//
// Renders a UML static-structure model (UltraCanvasUMLModel) as a class
// diagram: compartment boxes whose height is derived from their member list,
// and typed relationships whose line style and end decorations follow the UML
// rules exactly.
//
// The three layers are deliberately separate — see
// Docs/UltraCanvas/UltraCanvasClassDiagramProposal.md:
//
//   UltraCanvasUMLModel      meaning     (what the classes and edges ARE)
//   UltraCanvasClassLayout   geometry    (where the boxes GO)
//   UltraCanvasClassDiagram  appearance  (this file: measuring and drawing)
//
// Only this file needs a window. The model and the layout are unit-tested
// without one.
//
// Text metrics: boxes are measured with the real render context on the first
// paint, so a box is exactly as wide as its widest member row (proposal M20).
// Marking the model dirty re-measures and re-lays out on the next frame; the
// caller never has to sequence that by hand.
//
// CHANGELOG 1.0.0:
//  - Initial implementation: proposal P1 items M1-M7, M10, M11, M20, R1-R11,
//    L1-L4, L8, L9, L11, E1-E4, E10, S1, S2, S4-S6, S8-S10, S13,
//    I1-I4, I13-I15, I19.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmoothScroll.h"
#include "Plugins/Diagrams/UltraCanvasUMLModel.h"
#include "Plugins/Diagrams/UltraCanvasClassLayout.h"
#include "Plugins/Diagrams/UltraCanvasDiagramRouting.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

enum class ClassDiagramTheme {
    Default,       // neutral grey headers on white
    Professional,  // muted blue-grey, thin borders
    Blueprint,     // reference image 1: yellow header band, navy border, graph paper
    Colorful,      // reference image 4: a different hue per class
    DataType,      // reference image 5: pale blue «DataType» boxes
    Minimal,       // no fills, hairline borders
    Dark
};

// How much of each class is shown (proposal M11).
enum class ClassDiagramDetail {
    NameOnly,    // header band only
    Signatures,  // names and types, no parameter lists
    Full         // everything
};

// How relationship lines are drawn.
enum class ClassDiagramRouting {
    Orthogonal,  // right angles, obstacle-aware (the UML default)
    Straight
};

// =============================================================================
// STYLE
// =============================================================================

// Per-diagram appearance. Every colour is settable, so a theme is just a
// preset that fills this in (proposal S5).
struct ClassDiagramStyle {
    // Canvas
    Color backgroundColor = Color(250, 250, 252, 255);
    Color gridColor = Color(232, 236, 242, 255);
    bool showGrid = true;
    double gridSpacing = 20.0;

    // Boxes
    Color headerColor = Color(255, 224, 130, 255);
    Color bodyColor = Color(255, 255, 255, 255);
    Color borderColor = Color(45, 62, 80, 255);
    Color separatorColor = Color(45, 62, 80, 255);
    double borderWidth = 2.0;
    double separatorWidth = 1.0;
    double cornerRadius = 6.0;
    bool drawShadow = false;
    Color shadowColor = Color(0, 0, 0, 40);

    // Text
    std::string nameFontFamily = "Arial";
    std::string memberFontFamily = "monospace";
    double nameFontSize = 14.0;
    double memberFontSize = 12.0;
    double stereotypeFontSize = 11.0;
    Color nameTextColor = Color(30, 35, 45, 255);
    Color memberTextColor = Color(45, 50, 60, 255);
    Color stereotypeTextColor = Color(80, 90, 105, 255);

    // Relationships
    Color lineColor = Color(45, 62, 80, 255);
    double lineWidth = 1.6;
    double cornerRounding = 8.0;      // rounded elbows (proposal E2)
    double decorationSize = 12.0;     // triangle / diamond / arrow size
    Color labelTextColor = Color(50, 55, 65, 255);
    Color labelBackgroundColor = Color(250, 250, 252, 220);
    double labelFontSize = 11.0;

    // Selection / hover
    Color selectionColor = Color(0, 122, 204, 255);
    double selectionWidth = 2.5;
    Color hoverColor = Color(0, 122, 204, 90);
    Color rowHighlightColor = Color(0, 122, 204, 35);

    // Title block (reference image 1)
    Color titleColor = Color(40, 60, 90, 255);
    double titleFontSize = 20.0;

    // Geometry
    double compartmentPaddingX = 10.0;
    double compartmentPaddingY = 6.0;
    double minBoxWidth = 120.0;
    double minCompartmentHeight = 14.0;
    // Draw an attribute/operation band even when it is empty. Reference
    // images 1 and 2 do; switching this off collapses the box instead.
    bool drawEmptyCompartments = true;
};

// A style rule keyed on a stereotype (proposal S4): «interface» boxes orange,
// «DataType» boxes blue, and so on. The first matching rule wins.
struct ClassStereotypeStyle {
    std::string stereotype;
    Color headerColor;
    Color bodyColor;
    Color borderColor;
    bool hasHeaderColor = false;
    bool hasBodyColor = false;
    bool hasBorderColor = false;
};

// =============================================================================
// ELEMENT
// =============================================================================

class UltraCanvasClassDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasClassDiagram(const std::string& id, int x, int y, int width, int height);

    bool AcceptsFocus() const override { return true; }

    // ---- model ----------------------------------------------------------

    UltraCanvasUMLModel& Model() { return model; }
    const UltraCanvasUMLModel& Model() const { return model; }
    // Replaces the model wholesale; sizes and positions are recomputed on the
    // next paint.
    void SetModel(const UltraCanvasUMLModel& value);
    void Clear();

    // Convenience forwarders so simple diagrams read well at the call site.
    // Each marks the diagram dirty.
    std::string AddClass(const std::string& name,
                         const std::vector<std::string>& attributes = {},
                         const std::vector<std::string>& operations = {});
    std::string AddAbstractClass(const std::string& name,
                                 const std::vector<std::string>& attributes = {},
                                 const std::vector<std::string>& operations = {});
    std::string AddInterface(const std::string& name,
                             const std::vector<std::string>& operations = {});
    std::string AddEnumeration(const std::string& name,
                               const std::vector<std::string>& literals = {});

    UMLRelationshipHandle AddAssociation(const std::string& source, const std::string& target);
    UMLRelationshipHandle AddDirectedAssociation(const std::string& source, const std::string& target);
    UMLRelationshipHandle AddAggregation(const std::string& whole, const std::string& part);
    UMLRelationshipHandle AddComposition(const std::string& whole, const std::string& part);
    UMLRelationshipHandle AddGeneralization(const std::string& child, const std::string& parent);
    UMLRelationshipHandle AddRealization(const std::string& implementer, const std::string& interfaceName);
    UMLRelationshipHandle AddDependency(const std::string& client, const std::string& supplier,
                                        const std::string& stereotype = std::string());

    // Call after mutating the model directly through Model().
    void MarkModelChanged();

    // ---- presentation ---------------------------------------------------

    void SetTheme(ClassDiagramTheme theme);
    ClassDiagramTheme GetTheme() const { return theme; }

    ClassDiagramStyle& Style() { return style; }
    const ClassDiagramStyle& Style() const { return style; }
    void SetStyle(const ClassDiagramStyle& value);

    void SetGridVisible(bool visible, double spacing = 20.0);
    void SetBackgroundColor(const Color& color);
    void SetTitle(const std::string& title);
    const std::string& GetTitle() const { return model.Title(); }

    void SetDetailLevel(ClassDiagramDetail detail);
    ClassDiagramDetail GetDetailLevel() const { return detail; }

    // Per-classifier overrides.
    void SetClassifierColors(const std::string& idOrName, const Color& header, const Color& body);
    void SetClassifierCollapsed(const std::string& idOrName, bool collapsed);

    // Stereotype-driven styling. Rules are matched in insertion order.
    void AddStereotypeStyle(const ClassStereotypeStyle& rule);
    void ClearStereotypeStyles();
    // Assigns a distinct header colour to every classifier from a built-in
    // qualitative palette (reference image 4).
    void ApplyPaletteByClassifier();
    // Same, but one colour per package/source file, so a subsystem reads as a
    // block of colour.
    void ApplyPaletteByPackage();

    // Hide private members, operations, and so on (proposal I18, partial).
    void SetShowPrivateMembers(bool show);
    void SetShowOperations(bool show);
    void SetShowAttributes(bool show);

    // ---- layout ---------------------------------------------------------

    void SetLayout(ClassLayoutKind kind,
                   ClassLayoutDirection direction = ClassLayoutDirection::TopToBottom);
    ClassLayoutKind GetLayoutKind() const { return layoutOptions.kind; }
    ClassLayoutOptions& LayoutOptions() { return layoutOptions; }
    // Re-runs the layout on the next paint (measurement needs a render
    // context, so the work itself happens there).
    void RunLayout();
    // Move a single class by hand; pins it so RunLayout leaves it alone.
    void SetClassifierPosition(const std::string& idOrName, double x, double y, bool pin = true);

    void SetRouting(ClassDiagramRouting routing);
    ClassDiagramRouting GetRouting() const { return routing; }

    // ---- view -----------------------------------------------------------

    void SetZoomLevel(double zoom);
    double GetZoomLevel() const { return zoomLevel; }
    void SetPanOffset(double x, double y);
    Point2Dd GetPanOffset() const { return panOffset; }
    // Fits the whole drawing into the element on the next paint.
    void FitView();
    void ResetView();

    // Read-only presentation mode: zoom and pan still work, editing does not
    // (proposal I19).
    void SetInteractive(bool interactive);
    bool IsInteractive() const { return interactive; }

    // ---- selection ------------------------------------------------------

    void SelectClassifier(const std::string& idOrName);
    void SelectRelationship(const std::string& relationshipId);
    void DeselectAll();
    const std::string& GetSelectedClassifierId() const { return selectedClassifierId; }
    const std::string& GetSelectedRelationshipId() const { return selectedRelationshipId; }

    // ---- element --------------------------------------------------------

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // ---- callbacks ------------------------------------------------------

    std::function<void(const std::string&)> onClassClick;
    std::function<void(const std::string&)> onClassDoubleClick;
    // (classifierId, compartment, memberIndex) — compartment is
    // UMLMemberKind::Attribute or ::Operation (proposal I4).
    std::function<void(const std::string&, UMLMemberKind, size_t)> onMemberClick;
    std::function<void(const std::string&)> onRelationshipClick;
    std::function<void(const std::string&)> onSelectionChanged;
    std::function<void(double, double)> onCanvasRightClick;

private:
    // =========================================================================
    // CACHED GEOMETRY
    // =========================================================================

    // One measured row of a class box.
    struct RowMetrics {
        std::string text;      // visibility glyph + signature
        bool isStatic = false;
        bool isAbstract = false;
        UMLMemberKind kind = UMLMemberKind::Attribute;
        size_t memberIndex = 0;
        double width = 0.0;
    };

    // Everything needed to draw and hit-test one class box.
    struct BoxMetrics {
        std::string classifierId;
        Rect2Dd bounds;
        double headerHeight = 0.0;
        double attributeHeight = 0.0;
        double operationHeight = 0.0;
        double rowHeight = 0.0;
        bool hasStereotypeLine = false;
        double stereotypeHeight = 0.0;
        std::string stereotypeText;
        std::vector<RowMetrics> attributeRows;
        std::vector<RowMetrics> operationRows;
        Color headerColor;
        Color bodyColor;
        Color borderColor;
        bool nameItalic = false;
    };

    // A routed relationship, cached per frame.
    struct EdgeGeometry {
        std::string relationshipId;
        std::vector<Point2Dd> path;
        DiagramCardinalSide sourceSide = DiagramCardinalSide::Right;
        DiagramCardinalSide targetSide = DiagramCardinalSide::Left;
    };

    // =========================================================================
    // DATA
    // =========================================================================

    UltraCanvasUMLModel model;
    ClassDiagramStyle style;
    ClassDiagramTheme theme = ClassDiagramTheme::Default;
    ClassDiagramDetail detail = ClassDiagramDetail::Full;
    ClassDiagramRouting routing = ClassDiagramRouting::Orthogonal;
    ClassLayoutOptions layoutOptions;
    std::vector<ClassStereotypeStyle> stereotypeStyles;

    bool showPrivateMembers = true;
    bool showOperations = true;
    bool showAttributes = true;
    bool interactive = true;

    bool metricsDirty = true;
    bool layoutDirty = true;
    bool edgesDirty = true;
    bool fitPending = false;

    std::vector<BoxMetrics> boxes;                 // rebuilt when metricsDirty
    std::map<std::string, size_t> boxIndexById;
    std::vector<EdgeGeometry> edges;               // rebuilt with the boxes
    Rect2Dd contentBounds = Rect2Dd(0, 0, 0, 0);

    std::string selectedClassifierId;
    std::string selectedRelationshipId;
    std::string hoveredClassifierId;
    size_t hoveredRowIndex = static_cast<size_t>(-1);
    UMLMemberKind hoveredRowKind = UMLMemberKind::Attribute;

    double zoomLevel = 1.0;
    // Wheel zoom eases in instead of stepping: the animator hands out a series
    // of small factors which ApplyZoomFactorAtCursor() applies with the same
    // maths a single one used (see UltraCanvasSmoothScroll.h).
    UltraCanvasSmoothZoom zoomAnim;
    Point2Di zoomCursor;
    Point2Dd panOffset = Point2Dd(0, 0);
    bool isPanning = false;
    bool isDraggingBox = false;
    std::string draggedClassifierId;
    Point2Dd dragStartWorld;
    Point2Dd dragStartBoxOrigin;
    Point2Di panStartScreen;
    Point2Dd panStartOffset;

    // =========================================================================
    // MEASUREMENT AND LAYOUT
    // =========================================================================

    void RebuildMetrics(IRenderContext* ctx);
    void BuildRows(IRenderContext* ctx, const UMLClassifier& classifier, BoxMetrics& box) const;
    void ResolveBoxColors(const UMLClassifier& classifier, BoxMetrics& box) const;
    void ApplyLayout();
    void RebuildEdges();
    void ApplyFit();

    std::string RowText(const UMLMember& member) const;
    bool IsMemberVisible(const UMLMember& member) const;

    // =========================================================================
    // RENDERING
    // =========================================================================

    void RenderGrid(IRenderContext* ctx);
    void RenderTitle(IRenderContext* ctx);
    void RenderBox(IRenderContext* ctx, const BoxMetrics& box);
    void RenderRows(IRenderContext* ctx, const BoxMetrics& box,
                    const std::vector<RowMetrics>& rows, double top, double height) const;
    void RenderEdge(IRenderContext* ctx, const UMLRelationship& relationship,
                    const EdgeGeometry& geometry);
    void RenderEdgeLabels(IRenderContext* ctx, const UMLRelationship& relationship,
                          const EdgeGeometry& geometry);

    // Path drawing with rounded elbows (proposal E2).
    void DrawRoundedPolyline(IRenderContext* ctx, const std::vector<Point2Dd>& path,
                             double radius) const;
    // UML end decorations. `angle` points along the direction of travel INTO
    // the tip, matching UltraCanvasDiagramRouter::ComputeApproachAngle.
    void DrawClosedTriangle(IRenderContext* ctx, const Point2Dd& tip, double angle,
                            const Color& fill, const Color& stroke) const;
    void DrawDiamond(IRenderContext* ctx, const Point2Dd& tip, double angle,
                     bool filled) const;
    void DrawOpenArrow(IRenderContext* ctx, const Point2Dd& tip, double angle) const;
    void DrawLabelChip(IRenderContext* ctx, const std::string& text,
                       const Point2Dd& centre) const;

    std::string TruncateToWidth(IRenderContext* ctx, const std::string& text,
                                double maxWidth) const;

    // =========================================================================
    // HIT TESTING
    // =========================================================================

    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    const BoxMetrics* FindBoxAt(const Point2Dd& worldPos) const;
    // Returns the row under `worldPos` inside `box`, or npos.
    size_t FindRowAt(const BoxMetrics& box, const Point2Dd& worldPos,
                     UMLMemberKind& outKind) const;
    std::string FindRelationshipAt(const Point2Dd& worldPos) const;
    static double DistanceToPolyline(const Point2Dd& point, const std::vector<Point2Dd>& path);

    // =========================================================================
    // EVENTS
    // =========================================================================

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    void ApplyZoomFactorAtCursor(double factor, const Point2Di& cursor);
    bool HandleDoubleClick(const UCEvent& event);

    void ApplyThemeColors(ClassDiagramTheme value);
    void RegisterDefaultStereotypeStyles();
};

// =============================================================================
// FACTORY
// =============================================================================

inline std::shared_ptr<UltraCanvasClassDiagram> CreateClassDiagram(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasClassDiagram>(id, x, y, width, height);
}

} // namespace UltraCanvas
