// include/Plugins/Diagrams/UltraCanvasRequirementDiagram.h
// SysML requirement diagram (req) component — requirements, traceability
// relationships, containment trees, callouts and legends
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// Phase 1 of Docs/UltraCanvas/UltraCanvasRequirementDiagramProposal.md.
// Implements the P1 feature set (Q1-Q6, Q9, Q12-Q18, Q20 rect/rounded, Q21,
// Q25-Q31, Q33, Q36-Q38, Q41, Q43, Q46, Q47, Q49-Q54, Q56-Q60, Q73, Q77,
// Q83). Phase 2/3 items (compartment notation for derived elements, coverage
// analysis, Mermaid/CSV/ReqIF interchange, A* routing, layered layout,
// editing) are listed in the proposal and are additive to this API.
//
// A requirement diagram is the structural view of a system's requirements:
// requirements as compartmented boxes, the containment hierarchy that
// decomposes them, and the typed relationships that trace design elements and
// test cases back to the requirements they satisfy or verify.
//
// The element owns a small requirement MODEL that is valid and queryable
// independently of geometry — layout writes positions into it, rendering
// reads it. That separation is what lets the later phases add coverage
// queries and text interchange without touching the view code.
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
// requirement (a test case verifies a requirement, a block satisfies a
// requirement). Only containment points from parent to child.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <functional>
#include <cmath>

// UltraCanvasCommonTypes.h pulls in X11/Xlib.h on Linux, whose None/Status
// macros collide with the RequirementField enum members below.
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

// What an element on the diagram is. Drives the default shape and the palette
// entry; the «stereotype» text drawn in the header comes from
// RequirementNode::stereotype (defaulted from the kind and category).
enum class RequirementNodeKind {
    Requirement,   // «requirement» and its specialisations
    Block,         // «block» - a design element that satisfies requirements
    TestCase,      // «testCase» - verifies requirements
    UseCase,       // «useCase» - refines requirements
    Actor,         // an external actor
    Package,       // a namespace that contains requirements
    Rationale,     // «rationale» note
    Problem,       // «problem» note
    Note           // a plain annotation
};

// Box silhouette. Phase 1 renders Rectangle / RoundedRectangle / Oval;
// Folder, FoldedNote and StickFigure fall back to a rectangle until phase 2.
enum class RequirementNodeShape {
    Auto,               // Pick from the node kind
    Rectangle,
    RoundedRectangle,
    Oval,
    Folder,             // Phase 2
    FoldedNote,         // Phase 2
    StickFigure         // Phase 2
};

// SysML extendedRequirement risk property.
enum class RequirementRisk {
    Unspecified,
    Low,
    Medium,
    High
};

// SysML extendedRequirement verifyMethod property.
enum class RequirementVerifyMethod {
    Unspecified,
    Analysis,
    Inspection,
    Test,
    Demonstration
};

// The seven SysML requirement relationships plus generalisation (needed
// whenever the diagram shows a requirement-type or profile hierarchy).
enum class RequirementRelationKind {
    Containment,
    DeriveReqt,
    Satisfy,
    Verify,
    Refine,
    Trace,
    Copy,
    Generalization
};

// A model field a compartment row can bind to.
enum class RequirementField {
    Id,
    Name,
    Stereotype,
    Text,
    Source,
    Risk,
    VerifyMethod,
    Status,
    Owner,
    Priority,
    DocRef,
    Custom          // value looked up in RequirementNode::customProperties
};

// How a property row is written out.
enum class RequirementPropertyFormat {
    KeyEqualsQuotedValue,   // id="R1.2.1"      (the SysML tool convention)
    KeyColonValue           // id: R1.2.1
};

// One-call presets over the compartment template.
enum class RequirementDetailLevel {
    Collapsed,   // header only - stereotype + name
    Standard,    // header + id + text
    Full,        // header + every non-empty property in the template
    Custom       // use the node's own template verbatim
};

enum class RequirementLayoutMode {
    Manual,           // positions set by the caller / by dragging
    ContainmentTree   // tidy tree over the containment relationships
};

enum class RequirementOrientation {
    TopDown,
    BottomUp,
    LeftRight,
    RightLeft
};

// Routing of a relationship connector.
enum class RequirementRouting {
    Orthogonal,   // right-angle Z route (default)
    Straight,     // direct line
    Curved,       // cubic bezier
    Bus           // shared spine - containment only, ignored elsewhere
};

enum class RequirementPaletteKind {
    Classic,       // white boxes, black lines - textbook SysML
    Pastel,        // soft yellow/green fills (reference images 1-2)
    Vibrant,       // saturated per-kind colours (reference image 4)
    Professional,  // muted blue-grey
    Dark,
    Monochrome,    // print / greyscale
    Custom
};

// Where a node's fill colour comes from.
enum class RequirementColorSource {
    ByKind,        // palette entry for the node kind
    ByCategory,    // the category's colour (reference images 1 and 3)
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
    Custom          // rows supplied via SetLegendEntries()
};

// =============================================================================
// STRING CONVERSION (Q4 - round-trip for serialization and property rows)
// =============================================================================

const char* RequirementRiskToString(RequirementRisk risk);
RequirementRisk RequirementRiskFromString(const std::string& text);

const char* RequirementVerifyMethodToString(RequirementVerifyMethod method);
RequirementVerifyMethod RequirementVerifyMethodFromString(const std::string& text);

const char* RequirementNodeKindToString(RequirementNodeKind kind);
RequirementNodeKind RequirementNodeKindFromString(const std::string& text);

const char* RequirementRelationKindToString(RequirementRelationKind kind);
RequirementRelationKind RequirementRelationKindFromString(const std::string& text);

// The «stereotype» text a kind defaults to ("requirement", "testCase", ...).
const char* RequirementDefaultStereotype(RequirementNodeKind kind);

// The mid-line label a relationship kind defaults to ("deriveReqt",
// "satisfy", ...). Containment and generalisation have no label.
const char* RequirementRelationLabel(RequirementRelationKind kind);

// =============================================================================
// COMPARTMENT TEMPLATE (Q13, Q14)
// =============================================================================

// One row of a property compartment. The displayed text is
// `key="value"` (or `key: value`), with `value` resolved from `field` against
// the node — or from `literal` when field == RequirementField::Custom and
// `customKey` is empty.
struct RequirementPropertyRow {
    std::string key;                                    // shown before the '='
    RequirementField field = RequirementField::Custom;
    std::string customKey;                              // key into customProperties
    std::string literal;                                // used when customKey is empty
    bool bold = false;
    bool hasColor = false;
    Color textColor;

    RequirementPropertyRow() = default;
    RequirementPropertyRow(const std::string& rowKey, RequirementField boundField)
        : key(rowKey), field(boundField) {}

    RequirementPropertyRow& WithColor(const Color& c) {
        hasColor = true;
        textColor = c;
        return *this;
    }
    RequirementPropertyRow& WithBold(bool b = true) {
        bold = b;
        return *this;
    }
};

// An ordered set of property rows plus the formatting choice. Applies
// diagram-wide via SetNodeTemplate() and per node via
// RequirementNode::customTemplate.
struct RequirementNodeTemplate {
    std::vector<RequirementPropertyRow> rows;
    RequirementPropertyFormat format = RequirementPropertyFormat::KeyEqualsQuotedValue;

    void AddPropertyRow(const std::string& key, RequirementField field) {
        rows.emplace_back(key, field);
    }
    void AddCustomRow(const std::string& key, const std::string& customPropertyKey) {
        RequirementPropertyRow row;
        row.key = key;
        row.field = RequirementField::Custom;
        row.customKey = customPropertyKey;
        rows.push_back(row);
    }
    void SetPropertyFormat(RequirementPropertyFormat f) { format = f; }
    void Clear() { rows.clear(); }
    bool IsEmpty() const { return rows.empty(); }

    // The SysML extendedRequirement default: id, source, text, verifyMethod,
    // risk - exactly the compartment in reference image 1.
    static RequirementNodeTemplate Extended();
    // id + text only.
    static RequirementNodeTemplate Standard();
};

// =============================================================================
// NODE (Q1, Q2, Q21)
// =============================================================================

struct RequirementNode {
    // ----- identity -----
    // `id` is the unique model key used by relationships AND the value shown
    // for the SysML `id` property row ("UR1.2", "R1.2.1", ...).
    std::string id;
    std::string name;
    RequirementNodeKind kind = RequirementNodeKind::Requirement;

    // «stereotype» header text. Empty = derived from `kind`. Set it to
    // "functionalRequirement", "performanceRequirement", "designConstraint",
    // ... for the SysML requirement specialisations (Q3).
    std::string stereotype;

    // ----- SysML properties (Q1) -----
    std::string text;
    std::string source;
    RequirementRisk risk = RequirementRisk::Unspecified;
    RequirementVerifyMethod verifyMethod = RequirementVerifyMethod::Unspecified;
    std::string status;
    std::string owner;
    std::string priority;
    std::string docRef;
    std::map<std::string, std::string> customProperties;

    // ----- presentation -----
    std::string category;                              // drives ByCategory colour + legend
    RequirementDetailLevel detail = RequirementDetailLevel::Full;
    RequirementNodeShape shape = RequirementNodeShape::Auto;
    RequirementNodeTemplate customTemplate;            // used when detail == Custom
    bool hasCustomTemplate = false;

    bool hasFillColor = false;
    Color fillColor;
    bool hasBorderColor = false;
    Color borderColor;
    bool hasTextColor = false;
    Color textColor;
    double fontSize = 0.0;                             // 0 = use the diagram font size
    double borderWidth = 0.0;                          // 0 = use the style default

    // ----- geometry -----
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;    // 0 until measured; set explicitly to pin a width
    double height = 0.0;   // 0 until measured
    bool hasExplicitWidth = false;
    bool pinned = false;   // excluded from auto-layout (Q41)

    // ----- state -----
    bool isSelected = false;
    bool isHovered = false;

    RequirementNode() = default;
    RequirementNode(const std::string& nodeId, const std::string& nodeName)
        : id(nodeId), name(nodeName) {}
};

// =============================================================================
// RELATIONSHIP (Q5, Q6)
// =============================================================================

struct RequirementRelation {
    std::string id;
    RequirementRelationKind kind = RequirementRelationKind::Containment;
    std::string sourceId;   // the dependent element (parent, for containment)
    std::string targetId;   // the requirement (child, for containment)

    std::string label;      // empty = the kind's default «stereotype» label
    std::string rationale;  // shown in the tooltip; drawn as a note in phase 3

    bool hasColor = false;
    Color color;
    double lineWidth = 0.0;                     // 0 = style default
    RequirementRouting routing = RequirementRouting::Orthogonal;
    bool useDefaultRouting = true;              // false = honour `routing`
    bool showLabel = true;
    // A hidden relation still participates in the model (layout, parent/child
    // queries, validation) but is not drawn. Use it when containment defines
    // the hierarchy and another relationship carries the visible notation -
    // e.g. a generalisation tree, where drawing both would double every line.
    bool visible = true;

    bool isSelected = false;
    bool isHovered = false;

    RequirementRelation() = default;
    RequirementRelation(RequirementRelationKind relKind,
                        const std::string& from, const std::string& to)
        : kind(relKind), sourceId(from), targetId(to) {}
};

// =============================================================================
// CALLOUT (Q47)
// =============================================================================

// A free-floating note carrying a subset of one element's properties, joined
// to it with a dashed leader line (reference image 2).
struct RequirementCallout {
    std::string id;
    std::string targetNodeId;
    std::vector<RequirementField> fields;   // empty = id + text
    std::string headerText;                 // empty = the target's stereotype + name
    double x = 0.0;
    double y = 0.0;
    double width = 220.0;
    double height = 0.0;                    // 0 = measured from the content
    bool hasFillColor = false;
    Color fillColor;
    bool isSelected = false;

    RequirementCallout() = default;
    RequirementCallout(const std::string& calloutId, const std::string& target,
                       double px, double py)
        : id(calloutId), targetNodeId(target), x(px), y(py) {}
};

// =============================================================================
// CATEGORY & LEGEND (Q49, Q50)
// =============================================================================

struct RequirementCategory {
    std::string name;
    Color fillColor = Color(220, 230, 245, 255);
    Color borderColor = Color(120, 140, 170, 255);
    bool hasTextColor = false;
    Color textColor;

    RequirementCategory() = default;
    RequirementCategory(const std::string& categoryName, const Color& fill,
                        const Color& border)
        : name(categoryName), fillColor(fill), borderColor(border) {}
};

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

// =============================================================================
// TITLE BANNER (Q46)
// =============================================================================

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

// =============================================================================
// PALETTE (Q51, Q52)
// =============================================================================

struct RequirementPalette {
    Color backgroundColor = Color(252, 252, 253, 255);
    Color gridColor = Color(234, 236, 240, 255);

    // Per-kind box colours, indexed by RequirementNodeKind.
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
    Color dividerColor = Color(180, 184, 194, 255);

    // Relationship colours, indexed by RequirementRelationKind.
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
    int maxTextLines = 6;                // per property row before ellipsis (Q15)

    double relationLineWidth = 1.3;
    double arrowSize = 9.0;
    double crosshairRadius = 7.0;        // containment ⊕
    double trianglSize = 11.0;           // generalisation triangle

    bool showGrid = false;
    double gridSpacing = 25.0;

    // Layout
    double levelGap = 60.0;              // between tree levels
    double siblingGap = 22.0;            // between siblings
    double subtreeGap = 30.0;            // between adjacent subtrees

    double selectionWidth = 2.5;
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
// VALIDATION FEEDBACK (Q9, Q73)
// =============================================================================

struct RequirementWarning {
    enum class Kind {
        DuplicateNodeId,
        UnknownEndpoint,
        ContainmentCycle,
        MultipleParents,
        DuplicateRelation
    };
    Kind kind = Kind::DuplicateNodeId;
    std::string nodeId;
    std::string relationId;
    std::string message;
};

// =============================================================================
// REQUIREMENT DIAGRAM COMPONENT
// =============================================================================

class UltraCanvasRequirementDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasRequirementDiagram(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }

    // =========================================================================
    // NODE MANAGEMENT (Q1, Q2, Q9)
    // =========================================================================

    // Verbose form. Returns false when the id is empty or already taken
    // (reported through onValidationWarning).
    bool AddNode(const RequirementNode& node);

    // Convenience for non-requirement elements: blocks, test cases, actors.
    bool AddNode(RequirementNodeKind kind, const std::string& id, const std::string& name);

    // Convenience for the common requirement case.
    bool AddRequirement(const std::string& id, const std::string& name,
                        const std::string& text);
    bool AddRequirement(const std::string& id, const std::string& name,
                        const std::string& text, RequirementRisk risk,
                        RequirementVerifyMethod verifyMethod);

    bool RemoveNode(const std::string& id);
    void Clear();

    RequirementNode* GetNode(const std::string& id);
    const RequirementNode* GetNode(const std::string& id) const;
    std::vector<std::string> GetAllNodeIds() const;
    size_t GetNodeCount() const { return nodes.size(); }

    void SetNodePosition(const std::string& id, double x, double y);
    void SetNodeSize(const std::string& id, double width, double height);
    void SetNodeDetail(const std::string& id, RequirementDetailLevel detail);
    void SetNodeCategory(const std::string& id, const std::string& category);
    void SetNodeColors(const std::string& id, const Color& fill, const Color& border);
    void SetNodePinned(const std::string& id, bool pinned);
    void SetNodeTemplate(const std::string& nodeId, const RequirementNodeTemplate& tpl);

    // Unique id generator for programmatic adds ("REQ1", "REQ2", ...).
    std::string GenerateNodeId(const std::string& prefix = "REQ");

    // =========================================================================
    // RELATIONSHIP MANAGEMENT (Q5, Q6)
    // =========================================================================

    // Verbose form; the relation id is generated when empty.
    std::string AddRelation(const RequirementRelation& relation);
    std::string AddRelation(RequirementRelationKind kind,
                            const std::string& sourceId, const std::string& targetId);
    // Containment: parent owns child.
    std::string AddContainment(const std::string& parentId, const std::string& childId);

    bool RemoveRelation(const std::string& id);
    RequirementRelation* GetRelation(const std::string& id);
    const RequirementRelation* GetRelation(const std::string& id) const;
    std::vector<std::string> GetAllRelationIds() const;
    size_t GetRelationCount() const { return relations.size(); }

    void SetRelationLabel(const std::string& id, const std::string& label);
    void SetRelationColor(const std::string& id, const Color& color);
    void SetRelationRouting(const std::string& id, RequirementRouting routing);
    void SetRelationVisible(const std::string& id, bool visible);
    // Hides (or shows) every relation of one kind in one call - the common
    // case for containment used purely as a layout hierarchy.
    void SetRelationKindVisible(RequirementRelationKind kind, bool visible);

    // Model queries used by the tree layout and available to callers.
    std::string GetParentId(const std::string& nodeId) const;
    std::vector<std::string> GetChildIds(const std::string& nodeId) const;
    std::vector<std::string> GetRootIds() const;
    std::vector<std::string> GetRelationsOf(const std::string& nodeId) const;

    // Builds the whole diagram from flat lists and lays it out (Q83).
    void BuildFromRelations(const std::vector<RequirementNode>& nodeList,
                            const std::vector<RequirementRelation>& relationList);

    // =========================================================================
    // MODEL INTEGRITY (Q73)
    // =========================================================================

    // True when the containment relationships contain a cycle. A cyclic model
    // cannot be laid out as a tree; ContainmentTree layout falls back to a
    // grid arrangement and reports a ContainmentCycle warning.
    bool HasContainmentCycle() const;
    // Ids taking part in a containment cycle (empty when the model is acyclic).
    std::vector<std::string> GetContainmentCycleNodes() const;
    // Re-runs every integrity check and dispatches onValidationWarning.
    std::vector<RequirementWarning> Validate() const;

    // =========================================================================
    // COMPARTMENT TEMPLATE (Q13, Q14, Q17, Q18)
    // =========================================================================

    void SetNodeTemplate(const RequirementNodeTemplate& tpl);
    const RequirementNodeTemplate& GetNodeTemplate() const { return nodeTemplate; }
    void SetPropertyFormat(RequirementPropertyFormat format);
    void SetDefaultDetailLevel(RequirementDetailLevel detail);
    RequirementDetailLevel GetDefaultDetailLevel() const { return defaultDetail; }

    // =========================================================================
    // CALLOUTS (Q47)
    // =========================================================================

    bool AddCallout(const RequirementCallout& callout);
    bool AddCallout(const std::string& calloutId, const std::string& targetNodeId,
                    const std::vector<RequirementField>& fields, double x, double y);
    bool RemoveCallout(const std::string& id);
    RequirementCallout* GetCallout(const std::string& id);
    std::vector<std::string> GetAllCalloutIds() const;

    // =========================================================================
    // CATEGORIES & LEGEND (Q49, Q50)
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
    // TITLE (Q46)
    // =========================================================================

    void SetTitle(const std::string& title);
    void SetTitle(const std::string& title, const std::string& subtitle);
    void SetTitleVisible(bool visible);
    void SetTitleConfig(const RequirementTitleConfig& config);
    RequirementTitleConfig GetTitleConfig() const { return titleConfig; }

    // =========================================================================
    // STYLING & THEME (Q51-Q54)
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
    // LAYOUT (Q36-Q38, Q41, Q43)
    // =========================================================================

    void SetLayoutMode(RequirementLayoutMode mode);
    RequirementLayoutMode GetLayoutMode() const { return layoutMode; }
    void SetLayoutOrientation(RequirementOrientation orientation);
    RequirementOrientation GetLayoutOrientation() const { return orientation; }
    void SetLevelGap(double gap);
    void SetSiblingGap(double gap);
    void SetSubtreeGap(double gap);

    void SetDefaultRouting(RequirementRouting routing);
    RequirementRouting GetDefaultRouting() const { return defaultRouting; }

    // Requests a layout pass. Node sizes depend on real text metrics, which
    // are only available inside Render(), so the pass runs at the start of the
    // next frame; positions read back before that come from a heuristic
    // measurement made here.
    void RunLayout();
    void SetAutoFitOnLayout(bool autoFit) { autoFitOnLayout = autoFit; }
    bool GetAutoFitOnLayout() const { return autoFitOnLayout; }

    // World bounding box of every node and callout.
    Rect2Dd GetContentBounds() const;

    // =========================================================================
    // VIEWPORT (Q56)
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
    // SELECTION & INTERACTION (Q57, Q58)
    // =========================================================================

    void SelectNode(const std::string& id, bool addToSelection = false);
    void SelectRelation(const std::string& id, bool addToSelection = false);
    void SelectAll();
    void DeselectAll();
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
    // SERIALIZATION (Q77)
    // =========================================================================

    // Model, layout and the style/palette selection. Uses the framework JSON
    // module (DataFormats/UltraCanvasJSON.h).
    std::string ToJson(bool pretty = true) const;
    bool FromJson(const std::string& json);

    // =========================================================================
    // RENDERING & EVENTS
    // =========================================================================

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // =========================================================================
    // CALLBACKS (Q60)
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

private:
    // =========================================================================
    // MEASUREMENT & LAYOUT INTERNALS
    // =========================================================================

    // One laid-out text line of a node's body.
    struct MeasuredLine {
        std::string text;
        double fontSize = 10.0;
        bool bold = false;
        bool centered = false;
        Color color;
    };

    struct MeasuredNode {
        std::vector<MeasuredLine> headerLines;   // stereotype + name
        std::vector<MeasuredLine> bodyLines;     // property rows, already wrapped
        double headerHeight = 0.0;
        double bodyHeight = 0.0;
        bool hasDivider = false;
    };

    void MeasureAllNodes(IRenderContext* ctx);
    void MeasureNode(IRenderContext* ctx, RequirementNode& node, MeasuredNode& out);
    void MeasureCallout(IRenderContext* ctx, RequirementCallout& callout,
                        MeasuredNode& out);
    // Greedy word wrap against real text metrics. Appends an ellipsis to the
    // last line when the text needs more than maxLines.
    std::vector<std::string> WrapText(IRenderContext* ctx, const std::string& text,
                                      double maxWidth, int maxLines) const;
    // Heuristic width/height used by RunLayout() before the first frame.
    void EstimateNodeSize(RequirementNode& node) const;

    // Resolved value of a property row for a node ("" = row is skipped).
    std::string ResolveField(const RequirementNode& node, RequirementField field,
                             const std::string& customKey,
                             const std::string& literal) const;
    std::string FormatPropertyRow(const std::string& key, const std::string& value,
                                  RequirementPropertyFormat format) const;
    const RequirementNodeTemplate& TemplateForNode(const RequirementNode& node) const;
    std::vector<RequirementPropertyRow> RowsForNode(const RequirementNode& node) const;

    void ApplyContainmentTreeLayout();
    // Tidy tree over variable-size boxes. Returns the extent of the subtree
    // along the sibling axis; positions are written into the nodes.
    double LayoutSubtree(const std::string& nodeId, int depth, double crossOffset,
                         std::map<int, double>& levelExtent,
                         std::set<std::string>& visited);
    void ComputeLevelDepths(const std::string& nodeId, int depth,
                            std::map<int, double>& levelSize,
                            std::set<std::string>& visited);
    void ApplyGridFallbackLayout();
    void OrientPositions();

    // =========================================================================
    // RENDERING HELPERS
    // =========================================================================

    void RenderGrid(IRenderContext* ctx);
    void RenderRelations(IRenderContext* ctx);
    void RenderContainmentBuses(IRenderContext* ctx);
    void RenderRelation(IRenderContext* ctx, const RequirementRelation& relation);
    void RenderRelationLabel(IRenderContext* ctx, const RequirementRelation& relation,
                             const std::vector<Point2Dd>& path);
    void RenderNodes(IRenderContext* ctx);
    void RenderNode(IRenderContext* ctx, const RequirementNode& node);
    void RenderNodeBody(IRenderContext* ctx, const RequirementNode& node,
                        const MeasuredNode& measured);
    void RenderCallouts(IRenderContext* ctx);
    void RenderSelectionBox(IRenderContext* ctx);
    void RenderTitle(IRenderContext* ctx);
    void RenderLegend(IRenderContext* ctx);

    void DrawCrosshair(IRenderContext* ctx, const Point2Dd& center, const Color& color);
    void DrawOpenArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                           double dirX, double dirY, const Color& color);
    void DrawHollowTriangle(IRenderContext* ctx, const Point2Dd& tip,
                            double dirX, double dirY, const Color& color,
                            const Color& fill);
    void DrawTextLine(IRenderContext* ctx, const MeasuredLine& line,
                      double x, double y, double boxWidth);

    Color ResolveFillColor(const RequirementNode& node) const;
    Color ResolveBorderColor(const RequirementNode& node) const;
    Color ResolveTextColor(const RequirementNode& node) const;
    RequirementNodeShape ResolveShape(const RequirementNode& node) const;
    std::vector<RequirementLegendEntry> BuildLegendEntries() const;

    // =========================================================================
    // GEOMETRY
    // =========================================================================

    // Which side of a box a connector leaves from.
    enum class NodeFace { Top, Right, Bottom, Left };

    NodeFace ChooseFaceTowards(const RequirementNode& from, const RequirementNode& to) const;
    // Distributes connectors that share a face evenly along it (Q31).
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

    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    Point2Di WorldToScreen(const Point2Dd& worldPos) const;
    Point2Dd SnapPoint(const Point2Dd& p) const;
    Rect2Dd NodeRect(const RequirementNode& node) const;

    std::string FindNodeAt(const Point2Di& screenPos) const;
    std::string FindRelationAt(const Point2Di& screenPos) const;
    std::string FindCalloutAt(const Point2Di& screenPos) const;
    bool PointInNode(const RequirementNode& node, const Point2Dd& worldPos) const;
    static double DistanceToSegment(const Point2Dd& p, const Point2Dd& a,
                                    const Point2Dd& b);

    // =========================================================================
    // EVENT SUB-HANDLERS
    // =========================================================================

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);

    void NotifySelectionChange();
    void NotifyViewportChange();
    void ReportWarning(const RequirementWarning& warning) const;
    void ClampZoom();
    void InvalidateMeasurement();

    // =========================================================================
    // DATA
    // =========================================================================

    std::map<std::string, RequirementNode> nodes;
    std::vector<std::string> nodeOrder;              // stable draw / layout order
    std::vector<RequirementRelation> relations;
    std::vector<RequirementCallout> callouts;
    std::map<std::string, RequirementCategory> categories;
    std::vector<std::string> categoryOrder;
    std::map<std::string, Color> statusColors;
    std::vector<RequirementLegendEntry> customLegendEntries;

    // Measurement cache, keyed by node / callout id. Rebuilt whenever the
    // model, the template or the font changes.
    std::map<std::string, MeasuredNode> measuredNodes;
    std::map<std::string, MeasuredNode> measuredCallouts;
    bool measurementDirty = true;
    bool layoutDirty = false;
    // FitView()/RunLayout() called before the first frame cannot compute real
    // bounds yet (node sizes are heuristic until text has been measured), so
    // the fit is deferred to the end of the next Render().
    bool fitPending = false;

    RequirementNodeTemplate nodeTemplate;
    RequirementDetailLevel defaultDetail = RequirementDetailLevel::Full;
    RequirementDiagramStyle style;
    RequirementPalette palette;
    RequirementPaletteKind paletteKind = RequirementPaletteKind::Classic;
    RequirementColorSource colorSource = RequirementColorSource::ByKind;
    RequirementLegendConfig legendConfig;
    RequirementTitleConfig titleConfig;

    RequirementLayoutMode layoutMode = RequirementLayoutMode::Manual;
    RequirementOrientation orientation = RequirementOrientation::TopDown;
    RequirementRouting defaultRouting = RequirementRouting::Orthogonal;
    bool autoFitOnLayout = true;
    bool showRiskStripe = false;

    // Selection & hover
    std::set<std::string> selectedNodes;
    std::set<std::string> selectedRelations;
    std::string hoveredNodeId;
    std::string hoveredRelationId;
    std::string hoveredCalloutId;

    // Mouse state
    bool isDraggingNode = false;
    bool isDraggingCallout = false;
    bool isDraggingViewport = false;
    bool isSelectingBox = false;
    std::string draggedCalloutId;
    Point2Di dragStartPos;
    Point2Di lastMousePos;
    std::map<std::string, Point2Dd> dragStartPositions;
    Point2Dd calloutDragStart;
    Point2Dd selectionBoxStart;
    Point2Dd selectionBoxEnd;

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

    int nextNodeSerial = 1;
    int nextRelationSerial = 1;
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasRequirementDiagram> CreateRequirementDiagram(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasRequirementDiagram>(id, x, y, width, height);
}

} // namespace UltraCanvas
