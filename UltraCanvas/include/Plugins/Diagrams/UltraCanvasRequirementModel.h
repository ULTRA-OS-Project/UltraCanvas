// include/Plugins/Diagrams/UltraCanvasRequirementModel.h
// SysML requirement model — the data, semantics, traceability analysis and
// text interchange behind UltraCanvasRequirementDiagram
// Version: 1.2.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// Phase 2 of Docs/UltraCanvas/UltraCanvasRequirementDiagramProposal.md split
// the model out of the element, following the proposal's "model first, view
// second" principle (§5.1). Everything here is valid and queryable without a
// render context, a window or a layout pass, so it can be unit-tested
// headless (Tests/RequirementModelTests.cpp) and reused by any future view.
//
// The element (UltraCanvasRequirementDiagram) owns one RequirementModel and
// forwards its model API, so callers may use either level.
//
// Depends only on UltraCanvasCommonTypes.h (for Color) and the standard
// library — no UI, no rendering, no platform code.

#pragma once

#include "UltraCanvasCommonTypes.h"

// UltraCanvasCommonTypes.h pulls in X11/Xlib.h on Linux, whose None/Status
// macros collide with the enum members below.
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

// What an element on the diagram is. Drives the default shape and the palette
// entry; the «stereotype» text drawn in the header comes from
// RequirementNode::stereotype (defaulted from the kind).
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

// Box silhouette.
enum class RequirementNodeShape {
    Auto,               // pick from the node kind
    Rectangle,
    RoundedRectangle,
    Oval,
    Folder,             // package: tab + body
    FoldedNote,         // note: folded top-right corner
    StickFigure         // actor
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
    ExternalId,
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

// Routing of a relationship connector. Stored on the relation because it is a
// property of the model element, not of one particular view.
enum class RequirementRouting {
    Orthogonal,   // right-angle route (default)
    Straight,     // direct line
    Curved,       // cubic bezier
    Bus           // shared spine - containment only, ignored elsewhere
};

// SysML compartment notation: a list of related elements computed from the
// relationships instead of being drawn as lines.
enum class RequirementDerivedList {
    Derived,       // requirements derived FROM this one
    DerivedFrom,   // requirements this one derives from
    SatisfiedBy,   // elements that satisfy this requirement
    VerifiedBy,    // test cases that verify this requirement
    RefinedBy,     // elements that refine this requirement
    TracedTo,      // elements this one traces to
    Master         // the master of a copy
};

// How strictly the endpoint rules of each relationship kind are enforced.
enum class RequirementSemanticsMode {
    Lenient,   // illegal relations are accepted and reported (default)
    Strict     // illegal relations are rejected
};

// Direction of a trace-chain walk.
enum class RequirementTraceDirection {
    Upstream,     // toward the requirements this element depends on
    Downstream,   // toward the elements that depend on this one
    Both
};

// =============================================================================
// STRING CONVERSION
// =============================================================================

const char* RequirementRiskToString(RequirementRisk risk);
RequirementRisk RequirementRiskFromString(const std::string& text);

const char* RequirementVerifyMethodToString(RequirementVerifyMethod method);
RequirementVerifyMethod RequirementVerifyMethodFromString(const std::string& text);

const char* RequirementNodeKindToString(RequirementNodeKind kind);
RequirementNodeKind RequirementNodeKindFromString(const std::string& text);

const char* RequirementRelationKindToString(RequirementRelationKind kind);
RequirementRelationKind RequirementRelationKindFromString(const std::string& text);

const char* RequirementDerivedListToString(RequirementDerivedList list);

// The «stereotype» text a kind defaults to ("requirement", "testCase", ...).
const char* RequirementDefaultStereotype(RequirementNodeKind kind);

// The mid-line label a relationship kind defaults to ("deriveReqt",
// "satisfy", ...). Containment and generalisation have no label.
const char* RequirementRelationLabel(RequirementRelationKind kind);

// =============================================================================
// COMPARTMENT TEMPLATE
// =============================================================================

// One row of a property compartment. The displayed text is `key="value"` (or
// `key: value`), with `value` resolved from `field` against the node — or from
// `literal` when field == RequirementField::Custom and `customKey` is empty.
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

// An ordered set of property rows and derived-element compartments plus the
// formatting choice. Applies diagram-wide via SetNodeTemplate() and per node
// via RequirementNode::customTemplate.
struct RequirementNodeTemplate {
    std::vector<RequirementPropertyRow> rows;
    // SysML compartment notation: lists of related elements computed from the
    // relationships, drawn under their own headings below the property rows.
    std::vector<RequirementDerivedList> derivedCompartments;
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
    void AddDerivedCompartment(RequirementDerivedList list) {
        derivedCompartments.push_back(list);
    }
    void SetPropertyFormat(RequirementPropertyFormat f) { format = f; }
    void Clear() { rows.clear(); derivedCompartments.clear(); }
    bool IsEmpty() const { return rows.empty() && derivedCompartments.empty(); }

    // The SysML extendedRequirement default: id, source, text, verifyMethod,
    // risk - the compartment used by most SysML tools.
    static RequirementNodeTemplate Extended();
    // id + text only.
    static RequirementNodeTemplate Standard();
    // id + text plus the satisfiedBy / verifiedBy compartments - the
    // "compartment notation" alternative to drawing every trace as a line.
    static RequirementNodeTemplate WithTraceCompartments();
};

// =============================================================================
// NODE
// =============================================================================

struct RequirementNode {
    // ----- identity -----
    // `id` is the unique model key used by relationships. It is also what the
    // `id` property row shows, unless `externalId` is set (see below).
    std::string id;
    std::string name;
    RequirementNodeKind kind = RequirementNodeKind::Requirement;

    // «stereotype» header text. Empty = derived from `kind`. Set it to
    // "functionalRequirement", "performanceRequirement", "designConstraint",
    // ... for the SysML requirement specialisations.
    std::string stereotype;

    // ----- SysML properties -----
    std::string text;
    std::string source;
    RequirementRisk risk = RequirementRisk::Unspecified;
    RequirementVerifyMethod verifyMethod = RequirementVerifyMethod::Unspecified;
    std::string status;
    std::string owner;
    std::string priority;
    std::string docRef;
    // The id as authored in the source document, when it differs from the
    // model key. Mermaid and CSV imports use it: Mermaid references a
    // requirement by its *name* but carries a separate `id:` property, so the
    // name becomes the model key and the authored id lands here. When set, it
    // is what RequirementField::Id displays.
    std::string externalId;
    std::map<std::string, std::string> customProperties;

    // ----- presentation -----
    std::string category;                              // drives ByCategory colour + legend
    RequirementDetailLevel detail = RequirementDetailLevel::Full;
    RequirementNodeShape shape = RequirementNodeShape::Auto;
    RequirementNodeTemplate customTemplate;            // used when detail == Custom
    bool hasCustomTemplate = false;
    // For Rationale / Problem / Note nodes: the node or relation this note is
    // attached to, drawn with a dashed leader line. Empty = free-floating.
    std::string anchorId;

    bool hasFillColor = false;
    Color fillColor;
    bool hasBorderColor = false;
    Color borderColor;
    bool hasTextColor = false;
    Color textColor;
    double fontSize = 0.0;                             // 0 = use the diagram font size
    double borderWidth = 0.0;                          // 0 = use the style default

    // ----- geometry (written by the layout, read by the view) -----
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;    // 0 until measured; set explicitly to pin a width
    double height = 0.0;   // 0 until measured
    bool hasExplicitWidth = false;
    bool pinned = false;   // excluded from auto-layout

    // ----- Copy semantics -----
    // A slave requirement (the source of a Copy relation) mirrors its master's
    // id and text and is read-only. The snapshot records what the master said
    // at the last sync, so a later edit to the master makes the copy suspect.
    bool isCopy = false;              // set by SyncCopies()
    bool suspect = false;             // master changed since the last sync
    std::string copySnapshotId;
    std::string copySnapshotText;

    // ----- state -----
    bool isSelected = false;
    bool isHovered = false;
    bool collapsed = false;   // sub-tree hidden behind a ⊕ toggle
    bool filtered = false;    // hidden by a filter; excluded from layout
    bool dimmed = false;      // outside the active trace highlight

    RequirementNode() = default;
    RequirementNode(const std::string& nodeId, const std::string& nodeName)
        : id(nodeId), name(nodeName) {}

    // The id as it should be displayed: the authored id when there is one.
    const std::string& DisplayId() const { return externalId.empty() ? id : externalId; }
};

// =============================================================================
// RELATIONSHIP
// =============================================================================

struct RequirementRelation {
    std::string id;
    RequirementRelationKind kind = RequirementRelationKind::Containment;
    std::string sourceId;   // the dependent element (parent, for containment)
    std::string targetId;   // the requirement (child, for containment)

    std::string label;      // empty = the kind's default «stereotype» label
    std::string rationale;  // shown in the tooltip; may carry a «rationale» note

    bool hasColor = false;
    Color color;
    double lineWidth = 0.0;                     // 0 = style default
    RequirementRouting routing = RequirementRouting::Orthogonal;
    bool useDefaultRouting = true;              // false = honour `routing`
    bool showLabel = true;
    // A hidden relation still participates in the model (layout, parent/child
    // queries, validation) but is not drawn. Use it when containment defines
    // the hierarchy and another relationship carries the visible notation.
    bool visible = true;

    bool isSelected = false;
    bool isHovered = false;
    bool dimmed = false;    // outside the active trace highlight
    bool illegal = false;   // endpoints violate this kind's rules (lenient mode)

    RequirementRelation() = default;
    RequirementRelation(RequirementRelationKind relKind,
                        const std::string& from, const std::string& to)
        : kind(relKind), sourceId(from), targetId(to) {}
};

// =============================================================================
// CALLOUT & CATEGORY
// =============================================================================

// A free-floating note carrying a subset of one element's properties, joined
// to it with a dashed leader line.
struct RequirementCallout {
    std::string id;
    std::string targetNodeId;
    std::vector<RequirementField> fields;   // empty = id + text
    std::string headerText;                 // empty = the target's name
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

// =============================================================================
// VALIDATION & ANALYSIS RESULTS
// =============================================================================

struct RequirementWarning {
    enum class Kind {
        DuplicateNodeId,
        UnknownEndpoint,
        ContainmentCycle,
        MultipleParents,
        DuplicateRelation,
        IllegalEndpointKind,   // e.g. a block "verifying" a requirement
        DirectionCorrected,    // a backwards satisfy/verify/refine was flipped
        SuspectCopy            // a copy's master changed since the last sync
    };
    Kind kind = Kind::DuplicateNodeId;
    std::string nodeId;
    std::string relationId;
    std::string message;
};

struct RequirementCoverage {
    int requirementCount = 0;
    int satisfiedCount = 0;
    int verifiedCount = 0;
    int derivedCount = 0;
    int orphanCount = 0;
    double satisfiedPercent = 0.0;
    double verifiedPercent = 0.0;
    double derivedPercent = 0.0;
};

// =============================================================================
// CSV INTERCHANGE
// =============================================================================

// Column-name mapping for CSV import/export. Matching is case-insensitive and
// ignores surrounding whitespace; an empty name disables that column.
struct RequirementCsvSchema {
    std::string idColumn = "id";
    std::string nameColumn = "name";
    std::string parentColumn = "parent";
    std::string textColumn = "text";
    std::string sourceColumn = "source";
    std::string riskColumn = "risk";
    std::string verifyMethodColumn = "verifyMethod";
    std::string kindColumn = "kind";
    std::string stereotypeColumn = "stereotype";
    std::string categoryColumn = "category";
    std::string statusColumn = "status";
    std::string ownerColumn = "owner";
    char delimiter = ',';

    static RequirementCsvSchema Default() { return RequirementCsvSchema(); }
};

// =============================================================================
// TRACEABILITY MATRIX
// =============================================================================

// The requirement table real projects review: one row per requirement, one
// column per element that satisfies / verifies / refines something, and the
// relationship kinds in the cells. Produced as data so it can be rendered by
// UltraCanvasTable, exported as CSV, or inspected programmatically.
struct RequirementTraceMatrix {
    std::vector<std::string> rowIds;      // requirements, in model order
    std::vector<std::string> columnIds;   // linking elements, in model order
    // cells[row][column] - empty when the pair is unrelated.
    std::vector<std::vector<std::vector<RequirementRelationKind>>> cells;

    size_t RowCount() const { return rowIds.size(); }
    size_t ColumnCount() const { return columnIds.size(); }
    const std::vector<RequirementRelationKind>& Cell(size_t row, size_t column) const;

private:
    static const std::vector<RequirementRelationKind> emptyCell;
};

// A search hit, ranked so exact id matches come before text matches.
struct RequirementSearchHit {
    std::string nodeId;
    enum class Field { Id, Name, Text, Property } field = Field::Id;
    bool exact = false;
};

// =============================================================================
// MODEL
// =============================================================================

class RequirementModel {
public:
    // =========================================================================
    // NODES
    // =========================================================================

    // Returns false when the id is empty or already taken (reported through
    // onWarning).
    bool AddNode(const RequirementNode& node);
    bool AddNode(RequirementNodeKind kind, const std::string& id, const std::string& name);
    bool AddRequirement(const std::string& id, const std::string& name,
                        const std::string& text);
    bool AddRequirement(const std::string& id, const std::string& name,
                        const std::string& text, RequirementRisk risk,
                        RequirementVerifyMethod verifyMethod);

    bool RemoveNode(const std::string& id);
    // Changes a node's model key, rewriting every relation endpoint, callout
    // target and note anchor that referenced it. Fails when the new id is
    // empty or already taken.
    bool RenameNode(const std::string& oldId, const std::string& newId);
    void Clear();

    RequirementNode* GetNode(const std::string& id);
    const RequirementNode* GetNode(const std::string& id) const;
    bool HasNode(const std::string& id) const;
    const std::vector<std::string>& GetNodeOrder() const { return nodeOrder; }
    size_t GetNodeCount() const { return nodes.size(); }
    std::string GenerateNodeId(const std::string& prefix = "REQ");

    // =========================================================================
    // RELATIONSHIPS
    // =========================================================================

    // Returns the relation id (generated when empty), or "" when rejected.
    std::string AddRelation(const RequirementRelation& relation);
    std::string AddRelation(RequirementRelationKind kind,
                            const std::string& sourceId, const std::string& targetId);
    std::string AddContainment(const std::string& parentId, const std::string& childId);

    bool RemoveRelation(const std::string& id);
    RequirementRelation* GetRelation(const std::string& id);
    const RequirementRelation* GetRelation(const std::string& id) const;
    std::vector<RequirementRelation>& GetRelations() { return relations; }
    const std::vector<RequirementRelation>& GetRelations() const { return relations; }
    std::vector<std::string> GetAllRelationIds() const;
    size_t GetRelationCount() const { return relations.size(); }

    // =========================================================================
    // CALLOUTS & CATEGORIES
    // =========================================================================

    bool AddCallout(const RequirementCallout& callout);
    bool RemoveCallout(const std::string& id);
    RequirementCallout* GetCallout(const std::string& id);
    std::vector<RequirementCallout>& GetCallouts() { return callouts; }
    const std::vector<RequirementCallout>& GetCallouts() const { return callouts; }

    void AddCategory(const RequirementCategory& category);
    const RequirementCategory* GetCategory(const std::string& name) const;
    const std::vector<std::string>& GetCategoryOrder() const { return categoryOrder; }
    const std::map<std::string, RequirementCategory>& GetCategories() const { return categories; }

    // =========================================================================
    // STRUCTURAL QUERIES
    // =========================================================================

    std::string GetParentId(const std::string& nodeId) const;
    std::vector<std::string> GetChildIds(const std::string& nodeId) const;
    std::vector<std::string> GetRootIds() const;
    std::vector<std::string> GetRelationsOf(const std::string& nodeId) const;
    // Every node under `nodeId` in the containment tree (excluding itself).
    std::vector<std::string> GetDescendants(const std::string& nodeId) const;
    // SysML compartment notation: the elements listed under one heading.
    std::vector<std::string> GetDerivedElements(const std::string& nodeId,
                                                 RequirementDerivedList list) const;

    // =========================================================================
    // INTEGRITY
    // =========================================================================

    void SetSemanticsMode(RequirementSemanticsMode mode) { semanticsMode = mode; }
    RequirementSemanticsMode GetSemanticsMode() const { return semanticsMode; }

    // Whether `sourceKind -> targetKind` is a legal pairing for `kind`.
    static bool IsLegalEndpointPair(RequirementRelationKind kind,
                                    RequirementNodeKind sourceKind,
                                    RequirementNodeKind targetKind);

    bool HasContainmentCycle() const;
    std::vector<std::string> GetContainmentCycleNodes() const;
    std::vector<RequirementWarning> Validate() const;

    // Walks the containment tree from every root and rewrites each node's id
    // to its hierarchical position: prefix + "1", prefix + "1.1", ... Relation
    // endpoints, callout targets and note anchors follow. Nodes outside the
    // containment tree keep their ids. Returns the number of nodes renamed.
    int AssignHierarchicalIds(const std::string& prefix = "R");

    // =========================================================================
    // TRACEABILITY ANALYSIS
    // =========================================================================

    // Requirements with no incoming Satisfy relation.
    std::vector<std::string> GetUncoveredRequirements() const;
    // Requirements with no incoming Verify relation.
    std::vector<std::string> GetUnverifiedRequirements() const;
    // Requirements with no relations at all.
    std::vector<std::string> GetOrphanRequirements() const;
    RequirementCoverage GetCoverage() const;
    RequirementCoverage GetCoverageForCategory(const std::string& category) const;

    // Every element reachable from `nodeId` through relations, up to `depth`
    // hops (depth <= 0 = unlimited). The result includes `nodeId`.
    std::set<std::string> GetTraceChain(const std::string& nodeId,
                                         RequirementTraceDirection direction,
                                         int depth = 0) const;

    // =========================================================================
    // COPY SEMANTICS
    // =========================================================================

    // A Copy relation means `source` is a read-only copy of `target`. Pulls
    // each master's id and text into its slaves, marks the slaves isCopy, and
    // records the snapshot used. Returns the number of slaves updated.
    int SyncCopies();
    // Slaves whose master's id or text changed since the last SyncCopies().
    std::vector<std::string> GetSuspectCopies() const;
    // Re-syncs one slave and clears its suspect flag; false when `nodeId` is
    // not the source of a Copy relation.
    bool RefreshCopy(const std::string& nodeId);
    // Recomputes the suspect flags without changing any value. Called
    // automatically by Validate().
    void RefreshSuspectFlags();

    // =========================================================================
    // SEARCH
    // =========================================================================

    // Case-insensitive substring search over id, externalId, name, text and
    // the custom properties. Exact id/name matches rank first.
    std::vector<RequirementSearchHit> FindNodes(const std::string& query) const;

    // =========================================================================
    // TRACEABILITY MATRIX
    // =========================================================================

    RequirementTraceMatrix BuildTraceMatrix() const;
    std::string ToTraceMatrixCsv(const RequirementTraceMatrix& matrix,
                                 char delimiter = ',') const;

    // =========================================================================
    // TEXT INTERCHANGE
    // =========================================================================

    // Mermaid `requirementDiagram` dialect. Export produces text that
    // round-trips through Mermaid unchanged; import accepts the full dialect
    // (six requirement types, id/text/risk/verifymethod, element blocks with
    // type/docref, all seven relationship keywords in both arrow directions,
    // and the `direction` directive, returned through outDirection).
    std::string ToMermaid(const std::string& direction = "TB") const;
    bool FromMermaid(const std::string& text, std::string* outError = nullptr,
                     std::string* outDirection = nullptr);

    // Flat CSV, one row per node. Import is header-driven: the first row names
    // the columns, which are matched against the schema case-insensitively.
    std::string ToCsv(const RequirementCsvSchema& schema = RequirementCsvSchema()) const;
    bool FromCsv(const std::string& text,
                 const RequirementCsvSchema& schema = RequirementCsvSchema(),
                 std::string* outError = nullptr);

    // Splits one CSV line into fields, honouring "" quoting and embedded
    // delimiters/newlines-in-quotes. Exposed for callers doing their own
    // column mapping.
    static std::vector<std::string> ParseCsvLine(const std::string& line, char delimiter);

    // ReqIF (OMG requirements interchange format) import. Reads SPEC-OBJECTS
    // and the SPEC-HIERARCHY nesting of every SPECIFICATION, mapping the
    // standard ReqIF.ForeignID / ReqIF.Name / ReqIF.Text attribute definitions
    // onto the model and everything else onto custom properties.
    //
    // This is a deliberate SUBSET: requirements, their attributes and the
    // hierarchy. SPEC-RELATIONS, relation groups, datatype enumerations,
    // embedded objects and tool extensions are not read - a full ReqIF
    // implementation is a project of its own. `outError` reports what stopped
    // the parse; a file with no SPEC-OBJECTS is an error, not an empty model.
    bool FromReqIf(const std::string& xml, std::string* outError = nullptr);

    // =========================================================================
    // FIELD RESOLUTION
    // =========================================================================

    // The displayed value of one field for one node ("" = the row is skipped).
    static std::string ResolveField(const RequirementNode& node, RequirementField field,
                                    const std::string& customKey = std::string(),
                                    const std::string& literal = std::string());
    static std::string FormatPropertyRow(const std::string& key, const std::string& value,
                                         RequirementPropertyFormat format);

    // =========================================================================
    // NOTIFICATION
    // =========================================================================

    std::function<void(const RequirementWarning&)> onWarning;

private:
    void ReportWarning(const RequirementWarning& warning) const;
    // Rewrites every reference to `oldId` after the map key has moved.
    void RewriteReferences(const std::string& oldId, const std::string& newId);
    void CollectHierarchicalIds(const std::string& nodeId, const std::string& assignedId,
                                std::vector<std::pair<std::string, std::string>>& out,
                                std::set<std::string>& visited) const;

    std::map<std::string, RequirementNode> nodes;
    std::vector<std::string> nodeOrder;              // stable draw / layout order
    std::vector<RequirementRelation> relations;
    std::vector<RequirementCallout> callouts;
    std::map<std::string, RequirementCategory> categories;
    std::vector<std::string> categoryOrder;

    RequirementSemanticsMode semanticsMode = RequirementSemanticsMode::Lenient;
    int nextNodeSerial = 1;
    int nextRelationSerial = 1;
};

} // namespace UltraCanvas
