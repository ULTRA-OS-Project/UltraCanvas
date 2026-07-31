// include/Plugins/Diagrams/UltraCanvasERDiagram.h
// Entity-Relationship diagram component (Chen, Chen min-max/ISO, Crow's Foot)
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// See Docs/UltraCanvas/UltraCanvasERDiagramProposal.md for the research
// write-up and the full phased feature list; Docs/UltraCanvas/
// UltraCanvasERDiagram.md for the API documentation.
//
// DESIGN NOTE - why this is not a UltraCanvasNodeDiagram subclass:
// in classical (Chen) ER modelling a *relationship is a node*, not an edge.
// "Customer -(Places)- Order" is three nodes and two edges. Modelling
// relationships as edges - the crow's-foot shortcut - makes n-ary
// relationships, relationship attributes and identifying relationships
// inexpressible. This element therefore keeps a relationship-as-node core
// model, and renders crow's foot as a *projection* of that model.

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

#undef None

namespace UltraCanvas {

// =============================================================================
// ER DIAGRAM ENUMERATIONS
// =============================================================================

// Notation family. All three render the same model; they differ in where
// attributes live and how cardinality is drawn.
enum class ERNotation {
    Chen,        // Rectangles / diamonds / satellite ovals, 1-N-M edge letters
    ChenMinMax,  // As Chen, with (min,max) ISO cardinality pairs on every leg
    CrowsFoot    // Attribute-row boxes, relationship drawn as the edge itself
};

enum class ERDiagramEntityKind {
    Strong,       // Rectangle
    Weak,         // Double rectangle - identified through an owner
    Associative   // Rectangle enclosing a diamond - resolves an M:N
};

enum class ERAttributeKind {
    Simple,
    Composite,    // Has child attributes on their own spokes
    MultiValued,  // Double oval
    Derived       // Dashed oval
};

enum class ERKeyRole {
    NoKey,        // Named NoKey, not None - X11's Xlib.h defines None as a macro
    Primary,      // Solid underline
    Partial,      // Dashed underline - weak entity's discriminator
    Foreign,
    Unique
};

enum class ERRelationshipKind {
    Regular,      // Diamond
    Identifying   // Double diamond - owner of a weak entity
};

enum class ERParticipation {
    Partial,      // Single line
    Total,        // Double line
    Optional      // Dashed line
};

// The four cardinalities every notation agrees on. Anything else is expressed
// through the leg's explicit minCard/maxCard.
enum class ERCardinality {
    ExactlyOne,   // (1,1)   ||
    ZeroOrOne,    // (0,1)   o|
    OneOrMany,    // (1,N)   |{
    ZeroOrMany    // (0,N)   o{
};

// Marker drawn at the entity end of a leg. Deliberately independent of the
// notation so Chen bodies can carry crow's-foot ends (reference image 1).
enum class ERLineEndStyle {
    NoMarker,
    Tick,             // Single crossbar - "one"
    DoubleTick,       // Two crossbars - "exactly one"
    CrowsFoot,        // Three-pronged fan - "many"
    CircleCrowsFoot,  // Circle + fan - "zero or many, optional"
    Circle,           // Circle - "zero or one"
    Arrow,
    FilledDot
};

enum class ERCardinalityLabels {
    NoLabels,
    Letters,   // 1 / N / M / P
    MinMax,    // (1,1) (1,N) (0,N)
    UML        // 1 / 0..1 / 1..* / 0..*
};

enum class ERDiagramLayout {
    Manual,
    AttributeSatellite,  // Ovals fanned around their entity, away from its legs
    Spine,               // entity-relationship-entity chains on one axis
    ForceDirected,       // Entities + relationship nodes as bodies
    Symmetric            // Two entities, relationship centred between them
};

enum class ERConnectorStyle {
    Straight,
    Orthogonal,  // Elbow with rounded corners
    Bezier
};

enum class ERDiagramTheme {
    Default,
    Professional,
    Colorful,
    Pastel,
    Dark,
    Blueprint,
    Minimal,
    Print
};

// Which side of a crow's-foot row the PK/FK/UK badge column sits on. Real
// diagrams put it on either side depending on house style.
enum class ERRowMarkerSide {
    Left,
    Right
};

enum class ERPanelPosition {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

enum class ERAnnotationSide {
    Left,
    Right,
    Top,
    Bottom
};

// Node roles the theme palette colours independently.
enum class ERNodeRole {
    Entity,
    WeakEntity,
    AssociativeEntity,
    Relationship,
    IdentifyingRelationship,
    Attribute,
    KeyAttribute,
    DerivedAttribute,
    MultiValuedAttribute,
    AttributeRow            // Crow's foot: rows inside the entity box
};

// Sentinel for "many" in a leg's maxCard.
inline constexpr int ERCardinalityN = -1;

// =============================================================================
// ER DIAGRAM DATA STRUCTURES
// =============================================================================

struct ERDiagramAttribute {
    std::string id;            // Unique within its entity/relationship
    std::string name;
    std::string dataType;      // Physical level: "VARCHAR(64)", "INTEGER", ...
    std::string comment;

    ERAttributeKind kind = ERAttributeKind::Simple;
    ERKeyRole keyRole = ERKeyRole::NoKey;
    bool nullable = true;
    std::string defaultValue;

    // Composite attributes carry children on their own spokes.
    std::vector<ERDiagramAttribute> children;

    // Satellite geometry (Chen). Owned by the layout, persisted as a hint so a
    // hand-placed oval survives a notation switch within a session.
    double x = 0.0;
    double y = 0.0;
    double width = 96.0;
    double height = 34.0;
    bool hasPlacement = false;

    // Optional per-attribute overrides (fall back to the role palette).
    bool useCustomColors = false;
    Color fillColor = Color(220, 60, 90, 255);
    Color borderColor = Color(160, 30, 60, 255);
    Color textColor = Color(255, 255, 255, 255);

    // Crow's-foot rows: draws the badge cell in the palette's highlight colour.
    bool highlighted = false;
    // 0 = auto-number in order of appearance among this entity's foreign keys.
    int  foreignKeyIndex = 0;

    bool visible = true;
    bool isSelected = false;

    ERDiagramAttribute() = default;
    ERDiagramAttribute(const std::string& attrName, ERKeyRole role = ERKeyRole::NoKey,
                       ERAttributeKind attrKind = ERAttributeKind::Simple)
        : id(attrName), name(attrName), kind(attrKind), keyRole(role) {}
};

struct ERDiagramEntity {
    std::string id;
    std::string name;
    std::string alias;
    std::string comment;
    ERDiagramEntityKind kind = ERDiagramEntityKind::Strong;

    std::vector<ERDiagramAttribute> attributes;

    double x = 0.0;
    double y = 0.0;
    double width = 140.0;
    double height = 56.0;

    bool useCustomColors = false;
    Color fillColor = Color(30, 160, 175, 255);
    Color borderColor = Color(20, 110, 120, 255);
    Color textColor = Color(255, 255, 255, 255);
    double borderWidth = 2.0;

    bool collapsed = false;    // Crow's foot: header only, rows hidden
    bool draggable = true;
    bool selectable = true;
    bool isSelected = false;
    bool isPinned = false;

    // Force-directed layout velocity
    double vx = 0.0;
    double vy = 0.0;

    ERDiagramEntity() = default;
    ERDiagramEntity(const std::string& entityId, const std::string& entityName,
                    ERDiagramEntityKind entityKind = ERDiagramEntityKind::Strong)
        : id(entityId), name(entityName), kind(entityKind) {}
};

// One participation of an entity in a relationship. Two legs = binary,
// three or more = n-ary (reference image 5).
struct ERDiagramLeg {
    std::string entityId;
    std::string roleName;      // Disambiguates the ends of a recursive relationship

    int minCard = 1;
    int maxCard = ERCardinalityN;
    ERParticipation participation = ERParticipation::Partial;

    // Marker override; NoMarker means "derive from min/max and the diagram's
    // default line-end style".
    ERLineEndStyle lineEnd = ERLineEndStyle::NoMarker;

    ERDiagramLeg() = default;
    ERDiagramLeg(const std::string& eid, const std::string& role,
                 int minC, int maxC,
                 ERParticipation part = ERParticipation::Partial)
        : entityId(eid), roleName(role), minCard(minC), maxCard(maxC),
          participation(part) {}
};

struct ERDiagramRelationship {
    std::string id;
    std::string name;          // Verb phrase: "Places", "Contains"
    std::string inverseName;   // Shown at the far end when set
    ERRelationshipKind kind = ERRelationshipKind::Regular;

    std::vector<ERDiagramLeg> legs;
    std::vector<ERDiagramAttribute> attributes;

    double x = 0.0;
    double y = 0.0;
    double width = 110.0;
    double height = 62.0;
    bool hasPlacement = false;  // false = park at the centroid of the legs

    bool useCustomColors = false;
    Color fillColor = Color(25, 60, 110, 255);
    Color borderColor = Color(15, 40, 75, 255);
    Color textColor = Color(255, 255, 255, 255);

    ERConnectorStyle connectorStyle = ERConnectorStyle::Straight;
    bool connectorStyleOverride = false;

    bool selectable = true;
    bool isSelected = false;

    ERDiagramRelationship() = default;
    ERDiagramRelationship(const std::string& relId, const std::string& relName)
        : id(relId), name(relName) {}

    bool IsRecursive() const {
        return legs.size() == 2 && legs[0].entityId == legs[1].entityId;
    }
};

// Free text anchored to a node with a leader line (reference image 3).
struct ERDiagramAnnotation {
    std::string id;
    std::string text;
    std::string anchorRef;     // "entityId", "relationshipId" or "entityId.attributeId"
    ERAnnotationSide side = ERAnnotationSide::Left;
    double distance = 90.0;    // Leader length from the anchor

    double x = 0.0;            // Resolved by the layout
    double y = 0.0;
    double width = 0.0;        // 0 = auto-size to the text
    double height = 0.0;

    bool useCustomColors = false;
    Color textColor = Color(60, 60, 70, 255);
    Color leaderColor = Color(140, 140, 150, 255);
    double fontSize = 11.0;

    ERDiagramAnnotation() = default;
    ERDiagramAnnotation(const std::string& annId, const std::string& annText,
                        const std::string& anchor,
                        ERAnnotationSide annSide = ERAnnotationSide::Left)
        : id(annId), text(annText), anchorRef(anchor), side(annSide) {}
};

// Fill/border/text per node role - the reference images colour by role, not
// per node.
struct ERDiagramRolePalette {
    Color entityFill        = Color(30, 160, 175, 255);
    Color entityBorder      = Color(20, 110, 120, 255);
    Color entityText        = Color(255, 255, 255, 255);

    Color weakEntityFill    = Color(40, 175, 190, 255);
    Color weakEntityBorder  = Color(20, 110, 120, 255);
    Color weakEntityText    = Color(255, 255, 255, 255);

    Color relationshipFill      = Color(25, 60, 110, 255);
    Color relationshipBorder    = Color(15, 40, 75, 255);
    Color relationshipText      = Color(255, 255, 255, 255);

    Color attributeFill     = Color(205, 40, 75, 255);
    Color attributeBorder   = Color(155, 25, 55, 255);
    Color attributeText     = Color(255, 255, 255, 255);

    Color keyAttributeFill  = Color(180, 25, 60, 255);
    Color derivedAttributeFill   = Color(228, 120, 145, 255);
    Color multiValuedAttributeFill = Color(215, 70, 100, 255);

    // Crow's-foot attribute rows live inside the entity box, so they need a
    // neutral surface of their own - the oval fill is a signal colour and is
    // unreadable behind a column of typed rows.
    Color rowFill           = Color(248, 249, 251, 255);
    Color rowStripeFill     = Color(236, 239, 244, 255);   // Alternate rows
    Color rowHighlightFill  = Color(120, 205, 210, 255);   // Highlighted badge cell
    Color rowText           = Color(40, 45, 55, 255);
    Color rowKeyText        = Color(20, 25, 35, 255);
    Color rowDivider        = Color(150, 158, 172, 255);   // Key-compartment rule

    Color connectorColor    = Color(90, 95, 105, 255);
    Color cardinalityText   = Color(55, 60, 70, 255);
    Color roleText          = Color(105, 110, 120, 255);
};

struct ERDiagramStyle {
    std::string fontFamily = "Arial";
    double baseFontSize = 12.0;
    double attributeFontSize = 10.0;
    double cardinalityFontSize = 10.0;
    double rowFontSize = 10.0;          // Crow's-foot attribute rows

    Color backgroundColor = Color(252, 252, 253, 255);
    Color gridColor = Color(233, 235, 240, 255);
    bool showGrid = false;
    double gridSpacing = 25.0;

    Color selectionColor = Color(255, 120, 20, 255);
    double selectionWidth = 3.0;
    Color selectionBoxFill = Color(0, 120, 215, 40);
    Color selectionBoxStroke = Color(0, 120, 215, 200);

    ERDiagramRolePalette palette;

    // Crow's-foot row layout. The reference diagrams disagree on every one of
    // these, so they are settings rather than a house style.
    ERRowMarkerSide rowMarkerSide = ERRowMarkerSide::Left;
    bool   stripeRows = false;         // Alternate row background
    bool   keyCompartment = false;     // Key columns hoisted above a divider
    bool   underlineKeyRows = false;   // IDEF1X: underline the key column names
    bool   numberForeignKeys = false;  // FK1 / FK2 / FK3 instead of a bare FK
    double rowMarkerColumnWidth = 26.0;

    double connectorWidth = 1.6;
    double cornerRadius = 4.0;           // Crow's-foot boxes / rounded entities
    bool roundedEntities = false;
    double doubleLineGap = 4.0;          // Weak entity / total participation offset
    double elbowRadius = 8.0;

    // Attribute satellite layout
    double attributeOrbit = 120.0;       // Distance oval centre <- entity centre
    double attributeSpread = 250.0;      // Angular span (degrees) of the fan
    double attributeRingStep = 46.0;     // Extra radius per overflow ring

    // Force-directed layout parameters
    double linkDistance = 140.0;
    double linkStrength = 0.1;
    double chargeStrength = -900.0;
    int iterations = 260;
};

struct ERDiagramTitleConfig {
    bool visible = false;
    std::string text;
    std::string subtitle;
    double height = 44.0;
    double fontSize = 16.0;
    Color textColor = Color(35, 40, 50, 255);
    Color subtitleColor = Color(110, 115, 125, 255);
    Color backgroundColor = Color(255, 255, 255, 235);
    bool showSeparator = true;
};

// Auto-generated from the active notation (reference image 1).
struct ERDiagramLegendConfig {
    bool visible = false;
    ERPanelPosition position = ERPanelPosition::TopLeft;
    std::string title;
    double padding = 10.0;
    double rowHeight = 20.0;
    double swatchWidth = 34.0;
    double fontSize = 10.0;
    Color backgroundColor = Color(255, 255, 255, 235);
    Color borderColor = Color(205, 208, 215, 255);
    Color textColor = Color(55, 60, 70, 255);
    bool showShapes = true;
    bool showMarkers = true;
};

struct ERDiagramControlsConfig {
    bool visible = false;
    ERPanelPosition position = ERPanelPosition::BottomLeft;
    double buttonSize = 28.0;
    double padding = 10.0;
    double gap = 4.0;
    Color backgroundColor = Color(255, 255, 255, 235);
    Color borderColor = Color(205, 208, 215, 255);
    Color iconColor = Color(75, 80, 90, 255);
    Color hoverColor = Color(232, 234, 238, 255);
    bool showZoom = true;
    bool showFit = true;
    bool showLock = true;
};

// What was hit, for selection and callbacks.
enum class ERHitKind {
    NoHit,
    Entity,
    Relationship,
    Attribute,
    Annotation
};

struct ERHitResult {
    ERHitKind kind = ERHitKind::NoHit;
    std::string ownerId;      // Entity/relationship id (owner for attributes)
    std::string elementId;    // Attribute/annotation id; == ownerId otherwise

    bool IsHit() const { return kind != ERHitKind::NoHit; }
};

// =============================================================================
// ER DIAGRAM COMPONENT CLASS
// =============================================================================

class UltraCanvasERDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasERDiagram(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }

    // =========================================================================
    // ENTITIES
    // =========================================================================

    void AddEntity(const std::string& id, const std::string& name,
                   ERDiagramEntityKind kind = ERDiagramEntityKind::Strong);
    void AddEntity(const std::string& id, const std::string& name,
                   double x, double y,
                   ERDiagramEntityKind kind = ERDiagramEntityKind::Strong);
    void AddEntity(const ERDiagramEntity& entity);

    void RemoveEntity(const std::string& id);
    void SetEntityPosition(const std::string& id, double x, double y);
    void SetEntitySize(const std::string& id, double width, double height);
    void SetEntityName(const std::string& id, const std::string& name);
    void SetEntityKind(const std::string& id, ERDiagramEntityKind kind);
    void SetEntityColors(const std::string& id, const Color& fill, const Color& border);
    void SetEntityCollapsed(const std::string& id, bool collapsed);
    void PinEntity(const std::string& id, bool pinned);

    ERDiagramEntity* GetEntity(const std::string& id);
    const ERDiagramEntity* GetEntity(const std::string& id) const;
    std::vector<std::string> GetAllEntityIds() const;
    size_t GetEntityCount() const { return entities.size(); }

    // =========================================================================
    // ATTRIBUTES
    // =========================================================================

    void AddAttribute(const std::string& entityId, const std::string& name,
                      ERKeyRole keyRole = ERKeyRole::NoKey,
                      ERAttributeKind kind = ERAttributeKind::Simple);
    void AddAttribute(const std::string& entityId, const ERDiagramAttribute& attribute);
    // Physical level: name + type in one call.
    void AddTypedAttribute(const std::string& entityId, const std::string& name,
                           const std::string& dataType,
                           ERKeyRole keyRole = ERKeyRole::NoKey,
                           bool nullable = true);
    // Child of a composite attribute.
    void AddChildAttribute(const std::string& entityId, const std::string& parentAttributeId,
                           const ERDiagramAttribute& child);
    // Attribute hanging off a relationship (Chen: oval on the diamond).
    void AddRelationshipAttribute(const std::string& relationshipId,
                                  const ERDiagramAttribute& attribute);

    void RemoveAttribute(const std::string& entityId, const std::string& attributeId);
    ERDiagramAttribute* GetAttribute(const std::string& entityId,
                                     const std::string& attributeId);
    void SetAttributePosition(const std::string& entityId, const std::string& attributeId,
                              double x, double y);

    // =========================================================================
    // RELATIONSHIPS
    // =========================================================================

    void AddRelationship(const std::string& id, const std::string& name,
                         const std::string& sourceEntityId,
                         const std::string& targetEntityId,
                         ERCardinality sourceCardinality = ERCardinality::ExactlyOne,
                         ERCardinality targetCardinality = ERCardinality::ZeroOrMany);
    void AddRelationship(const ERDiagramRelationship& relationship);

    void AddIdentifyingRelationship(const std::string& id, const std::string& name,
                                    const std::string& ownerEntityId,
                                    const std::string& weakEntityId);
    // Self relationship; the two role names disambiguate the ends.
    void AddRecursiveRelationship(const std::string& id, const std::string& name,
                                  const std::string& entityId,
                                  const std::string& roleA, const std::string& roleB,
                                  ERCardinality cardA = ERCardinality::ZeroOrOne,
                                  ERCardinality cardB = ERCardinality::ZeroOrMany);
    // n-ary: one diamond, three or more legs.
    void AddNaryRelationship(const std::string& id, const std::string& name,
                             const std::vector<ERDiagramLeg>& legs);

    void RemoveRelationship(const std::string& id);
    void SetRelationshipPosition(const std::string& id, double x, double y);
    void SetRelationshipName(const std::string& id, const std::string& name);
    void SetRelationshipKind(const std::string& id, ERRelationshipKind kind);
    void SetRelationshipColors(const std::string& id, const Color& fill, const Color& border);
    void SetRelationshipConnectorStyle(const std::string& id, ERConnectorStyle style);
    void SetLegCardinality(const std::string& relationshipId, size_t legIndex,
                           int minCard, int maxCard);
    void SetLegParticipation(const std::string& relationshipId, size_t legIndex,
                             ERParticipation participation);
    void SetLegRoleName(const std::string& relationshipId, size_t legIndex,
                        const std::string& roleName);

    ERDiagramRelationship* GetRelationship(const std::string& id);
    const ERDiagramRelationship* GetRelationship(const std::string& id) const;
    std::vector<std::string> GetAllRelationshipIds() const;
    size_t GetRelationshipCount() const { return relationships.size(); }

    // =========================================================================
    // ANNOTATIONS
    // =========================================================================

    void AddAnnotation(const std::string& id, const std::string& text,
                       const std::string& anchorRef,
                       ERAnnotationSide side = ERAnnotationSide::Left);
    void AddAnnotation(const ERDiagramAnnotation& annotation);
    void RemoveAnnotation(const std::string& id);
    ERDiagramAnnotation* GetAnnotation(const std::string& id);

    // =========================================================================
    // NOTATION
    // =========================================================================

    void SetNotation(ERNotation notation);
    ERNotation GetNotation() const { return notation; }

    // Marker set, deliberately independent of the notation (image 1 mixes
    // Chen bodies with crow's-foot ends).
    void SetLineEndStyle(ERLineEndStyle style);
    ERLineEndStyle GetLineEndStyle() const { return lineEndStyle; }

    void SetCardinalityLabelStyle(ERCardinalityLabels style);
    ERCardinalityLabels GetCardinalityLabelStyle() const { return cardinalityLabels; }

    void SetShowRoleNames(bool show);
    void SetShowRelationshipNames(bool show);
    void SetShowDataTypes(bool show);

    // =========================================================================
    // LAYOUT
    // =========================================================================

    void SetLayout(ERDiagramLayout layout);
    ERDiagramLayout GetLayout() const { return layoutMode; }
    void RunLayout();
    void RunForceDirectedLayout(int iterations = 0);   // 0 = style.iterations
    void ApplyAttributeSatelliteLayout();
    void ApplySpineLayout(bool horizontal = true);
    void ApplySymmetricLayout();
    void SetAutoFitOnLayout(bool autoFit) { autoFitOnLayout = autoFit; }
    bool GetAutoFitOnLayout() const { return autoFitOnLayout; }

    void SetSnapToGrid(bool enabled);
    void SetSnapGrid(double snapX, double snapY);
    bool IsSnapToGridEnabled() const { return snapEnabled; }

    // =========================================================================
    // STYLING & THEME
    // =========================================================================

    void SetTheme(ERDiagramTheme theme);
    ERDiagramTheme GetTheme() const { return currentTheme; }
    void SetStyle(const ERDiagramStyle& newStyle);
    const ERDiagramStyle& GetStyle() const { return style; }
    void SetRolePalette(const ERDiagramRolePalette& palette);
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible, double spacing = 25.0);
    void SetFontFamily(const std::string& fontFamily);
    void SetFontSize(double size);
    void SetConnectorStyle(ERConnectorStyle style);
    ERConnectorStyle GetConnectorStyle() const { return connectorStyle; }

    // Crow's-foot row layout
    void SetRowMarkerSide(ERRowMarkerSide side);
    void SetStripeRows(bool stripe);
    void SetKeyCompartment(bool enabled);
    void SetUnderlineKeyRows(bool underline);
    void SetNumberForeignKeys(bool numbered);
    void SetAttributeHighlighted(const std::string& entityId,
                                 const std::string& attributeId, bool highlighted);

    // Role colours resolved through the palette and any per-node override.
    Color GetRoleFill(ERNodeRole role) const;
    Color GetRoleBorder(ERNodeRole role) const;
    Color GetRoleText(ERNodeRole role) const;

    // =========================================================================
    // CANVAS CHROME
    // =========================================================================

    void SetTitle(const std::string& text, const std::string& subtitle = std::string());
    void SetTitleConfig(const ERDiagramTitleConfig& config);
    ERDiagramTitleConfig GetTitleConfig() const { return titleConfig; }

    void SetLegendVisible(bool visible,
                          ERPanelPosition position = ERPanelPosition::TopLeft);
    void SetLegendConfig(const ERDiagramLegendConfig& config);
    ERDiagramLegendConfig GetLegendConfig() const { return legendConfig; }

    void SetControlsVisible(bool visible);
    void SetControlsPosition(ERPanelPosition position);
    void SetControlsConfig(const ERDiagramControlsConfig& config);
    ERDiagramControlsConfig GetControlsConfig() const { return controlsConfig; }

    // =========================================================================
    // VIEWPORT
    // =========================================================================

    void SetZoomLevel(double zoom);
    double GetZoomLevel() const { return zoomLevel; }
    void SetPanOffset(double x, double y);
    Point2Dd GetPanOffset() const { return panOffset; }
    void ZoomIn(double factor = 1.2);
    void ZoomOut(double factor = 1.2);
    void FitView(double padding = 40.0);
    void CenterOn(double worldX, double worldY);
    void SetMinZoom(double minZ) { minZoom = minZ; }
    void SetMaxZoom(double maxZ) { maxZoom = maxZ; }
    Rect2Dd GetContentBounds() const;

    // =========================================================================
    // SELECTION & INTERACTION
    // =========================================================================

    void SelectEntity(const std::string& id, bool addToSelection = false);
    void SelectRelationship(const std::string& id, bool addToSelection = false);
    void SelectAll();
    void DeselectAll();
    void Clear();

    std::vector<std::string> GetSelectedEntityIds() const;
    std::vector<std::string> GetSelectedRelationshipIds() const;
    bool IsEntitySelected(const std::string& id) const;

    void SetInteractive(bool interactive) { isInteractive = interactive; }
    bool IsInteractive() const { return isInteractive; }
    void SetPanOnDrag(bool pan) { panOnDrag = pan; }
    void SetZoomOnScroll(bool zoom) { zoomOnScroll = zoom; }
    // Hovering an entity dims everything not directly related to it.
    void SetHighlightRelatedOnHover(bool enabled) { highlightRelatedOnHover = enabled; }

    // =========================================================================
    // LABEL MEASUREMENT
    // =========================================================================

    // Heuristic (no render context needed) - same contract as NodeDiagram's.
    void MeasureLabel(const std::string& label, double fontSize,
                      int& outWidth, int& outHeight) const;
    void SuggestEntitySizeForName(const std::string& name, double& outWidth,
                                  double& outHeight) const;
    // Sizes every entity/relationship/attribute to fit its own text.
    void AutoSizeAll();

    // =========================================================================
    // SERIALIZATION
    // =========================================================================

    std::string ToJson() const;
    bool FromJson(const std::string& json);

    // =========================================================================
    // RENDERING & EVENTS
    // =========================================================================

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    std::function<void(const std::string&)> onEntityClick;
    std::function<void(const std::string&)> onEntityDoubleClick;
    std::function<void(const std::string&, double, double)> onEntityDrag;
    std::function<void(const std::string&)> onRelationshipClick;
    // (entityId, attributeId)
    std::function<void(const std::string&, const std::string&)> onAttributeClick;
    // (entityIds, relationshipIds)
    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> onSelectionChange;
    std::function<void(double zoom, double panX, double panY)> onViewportChange;
    std::function<void(double worldX, double worldY)> onCanvasRightClick;
    std::function<void()> onModelChanged;

private:
    // =========================================================================
    // MODEL
    // =========================================================================

    std::map<std::string, ERDiagramEntity> entities;
    std::vector<std::string> entityOrder;
    std::map<std::string, ERDiagramRelationship> relationships;
    std::vector<std::string> relationshipOrder;
    std::map<std::string, ERDiagramAnnotation> annotations;
    std::vector<std::string> annotationOrder;

    // =========================================================================
    // CONFIGURATION
    // =========================================================================

    ERNotation notation = ERNotation::Chen;
    ERLineEndStyle lineEndStyle = ERLineEndStyle::NoMarker;
    ERCardinalityLabels cardinalityLabels = ERCardinalityLabels::Letters;
    ERDiagramLayout layoutMode = ERDiagramLayout::Manual;
    ERConnectorStyle connectorStyle = ERConnectorStyle::Straight;
    ERDiagramTheme currentTheme = ERDiagramTheme::Default;
    ERDiagramStyle style;
    ERDiagramTitleConfig titleConfig;
    ERDiagramLegendConfig legendConfig;
    ERDiagramControlsConfig controlsConfig;

    // Set once SetTitleConfig supplies explicit colours, so a later SetTheme
    // does not silently overwrite the caller's choice.
    bool titleColorsCustomized = false;

    bool showRoleNames = true;
    bool showRelationshipNames = true;
    bool showDataTypes = true;
    bool autoFitOnLayout = true;
    bool snapEnabled = false;
    double snapX = 25.0;
    double snapY = 25.0;

    // =========================================================================
    // VIEWPORT / INTERACTION STATE
    // =========================================================================

    double zoomLevel = 1.0;
    double minZoom = 0.1;
    double maxZoom = 5.0;
    Point2Dd panOffset = Point2Dd(0.0, 0.0);

    bool isInteractive = true;
    bool panOnDrag = true;
    bool zoomOnScroll = true;
    bool highlightRelatedOnHover = false;

    bool isPanning = false;
    bool isDraggingNode = false;
    bool isSelectingBox = false;
    bool isMultiSelectKeyHeld = false;

    Point2Di lastMousePos = Point2Di(0, 0);
    Point2Di dragStartPos = Point2Di(0, 0);
    Point2Dd dragNodeOffset = Point2Dd(0.0, 0.0);
    Point2Dd selectionBoxStart = Point2Dd(0.0, 0.0);
    Point2Dd selectionBoxEnd = Point2Dd(0.0, 0.0);

    ERHitResult draggedNode;
    ERHitResult hoveredNode;
    int hoveredControlButton = -1;

    // =========================================================================
    // GEOMETRY HELPERS
    // =========================================================================

    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    Point2Di WorldToScreen(const Point2Dd& worldPos) const;
    Point2Dd SnapPoint(const Point2Dd& p) const;
    void ClampZoom();

    Rect2Dd GetEntityRect(const ERDiagramEntity& entity) const;
    Rect2Dd GetRelationshipRect(const ERDiagramRelationship& rel) const;
    Point2Dd GetRelationshipCenter(const ERDiagramRelationship& rel) const;
    Point2Dd GetEntityCenter(const ERDiagramEntity& entity) const;
    // Where a line aimed at 'towards' leaves the entity's border.
    Point2Dd GetEntityBorderPoint(const ERDiagramEntity& entity,
                                  const Point2Dd& towards) const;
    Point2Dd GetDiamondBorderPoint(const ERDiagramRelationship& rel,
                                   const Point2Dd& towards) const;
    double MeasuredEntityHeight(const ERDiagramEntity& entity) const;
    // Row order for the crow's-foot projection: with keyCompartment on, key
    // columns are hoisted above the divider. Display order only - the model
    // keeps the order the caller supplied.
    std::vector<const ERDiagramAttribute*> RowOrder(const ERDiagramEntity& entity,
                                                    size_t& outKeyCount) const;
    std::string RowMarkerText(const ERDiagramEntity& entity,
                              const ERDiagramAttribute& attribute) const;
    // The badge column has to hold "FK1", not just "FK", when foreign keys are
    // numbered. Shared by AutoSizeAll and the renderer so the width they assume
    // can never disagree.
    double EffectiveMarkerColumnWidth() const {
        return style.rowMarkerColumnWidth + (style.numberForeignKeys ? 14.0 : 0.0);
    }

    // =========================================================================
    // ROUTING
    // =========================================================================

    void BuildConnectorPath(const Point2Dd& from, const Point2Dd& to,
                            ERConnectorStyle routeStyle,
                            std::vector<Point2Dd>& outPath) const;
    void BuildSelfLoopPath(const ERDiagramEntity& entity,
                           const ERDiagramRelationship& rel,
                           int legIndex, std::vector<Point2Dd>& outPath) const;

    // =========================================================================
    // LAYOUT HELPERS
    // =========================================================================

    void PlaceUnplacedRelationships();
    void ResolveEntityOverlaps();
    void ResolveAttributeOverlaps();
    void ResolveAnnotationPlacement();
    std::vector<std::string> GetRelationshipIdsForEntity(const std::string& entityId) const;
    double PreferredAttributeDirection(const ERDiagramEntity& entity) const;

    // =========================================================================
    // RENDERING
    // =========================================================================

    void RenderGrid(IRenderContext* ctx);
    void RenderConnectors(IRenderContext* ctx);
    void RenderRelationshipConnectors(IRenderContext* ctx,
                                      const ERDiagramRelationship& rel);
    void RenderAttributeSpokes(IRenderContext* ctx, const ERDiagramEntity& entity);
    void RenderEntities(IRenderContext* ctx);
    void RenderEntityChen(IRenderContext* ctx, const ERDiagramEntity& entity);
    void RenderEntityCrowsFoot(IRenderContext* ctx, const ERDiagramEntity& entity);
    void RenderRelationships(IRenderContext* ctx);
    void RenderAttributes(IRenderContext* ctx);
    void RenderAttributeOval(IRenderContext* ctx, const ERDiagramAttribute& attribute,
                             bool ownedBySelection);
    void RenderAnnotations(IRenderContext* ctx);
    void RenderSelectionBox(IRenderContext* ctx);

    void RenderLineEndMarker(IRenderContext* ctx, const Point2Dd& tip,
                             const Point2Dd& inward, const ERDiagramLeg& leg,
                             const Color& color);
    // 'inward' is the next point along the leg, so the label can be placed
    // relative to the connector itself - crow's foot has no diamond to aim at.
    void RenderCardinalityLabel(IRenderContext* ctx, const Point2Dd& at,
                                const Point2Dd& inward,
                                const ERDiagramRelationship& rel, size_t legIndex);
    void RenderDoubleLinePath(IRenderContext* ctx, const std::vector<Point2Dd>& path,
                              double gap);
    void RenderUnderline(IRenderContext* ctx, const Point2Dd& textOrigin,
                         int textWidth, int textHeight, bool dashed,
                         const Color& color);
    void RenderCenteredText(IRenderContext* ctx, const std::string& text,
                            const Rect2Dd& box, const Color& color,
                            double fontSize, FontWeight weight);

    // Screen-space chrome (drawn after the viewport transform is popped)
    void RenderTitle(IRenderContext* ctx);
    void RenderLegend(IRenderContext* ctx);
    void RenderControls(IRenderContext* ctx);
    Rect2Dd GetPanelRect(ERPanelPosition position, double width, double height,
                         double padding) const;

    // Dimming for hover highlight
    double AlphaForEntity(const std::string& entityId) const;
    double AlphaForRelationship(const std::string& relationshipId) const;
    bool IsRelatedToHover(const std::string& entityId) const;

    // =========================================================================
    // HIT TESTING
    // =========================================================================

    ERHitResult HitTest(const Point2Di& screenPos) const;
    int FindControlButtonAt(const Point2Di& screenPos) const;
    bool PointInEntity(const ERDiagramEntity& entity, const Point2Dd& worldPos) const;
    bool PointInDiamond(const ERDiagramRelationship& rel, const Point2Dd& worldPos) const;
    bool PointInEllipse(const Rect2Dd& rect, const Point2Dd& worldPos) const;

    // =========================================================================
    // EVENT SUB-HANDLERS
    // =========================================================================

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);

    // =========================================================================
    // MISC
    // =========================================================================

    void ApplyThemeColors();
    void NotifySelectionChange();
    void NotifyViewportChange();
    void NotifyModelChanged();
    void MoveEntityBy(ERDiagramEntity& entity, double dx, double dy);
    std::string CardinalityText(const ERDiagramRelationship& rel, size_t legIndex) const;
    ERLineEndStyle ResolveLineEnd(const ERDiagramLeg& leg) const;
};

// =============================================================================
// FACTORY
// =============================================================================

inline std::shared_ptr<UltraCanvasERDiagram> CreateERDiagram(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasERDiagram>(id, x, y, width, height);
}

// Converts the four common cardinalities to an explicit (min,max) pair.
inline void ERCardinalityToMinMax(ERCardinality cardinality, int& outMin, int& outMax) {
    switch (cardinality) {
        case ERCardinality::ExactlyOne: outMin = 1; outMax = 1; break;
        case ERCardinality::ZeroOrOne:  outMin = 0; outMax = 1; break;
        case ERCardinality::OneOrMany:  outMin = 1; outMax = ERCardinalityN; break;
        case ERCardinality::ZeroOrMany: outMin = 0; outMax = ERCardinalityN; break;
    }
}

} // namespace UltraCanvas
