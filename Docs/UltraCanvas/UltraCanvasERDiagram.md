# UltraCanvasERDiagram Documentation

## Overview

**UltraCanvasERDiagram** is an entity-relationship diagram component. It renders
one shared model in three notations — **Chen**, **Chen (min,max)/ISO** and
**Crow's Foot** — and covers strong/weak/associative entities, n-ary and
recursive relationships, the four Chen attribute kinds, key underlines,
cardinality and participation constraints, annotation callouts, an
auto-generated legend, five layouts, three connector routings, eight themes and
JSON round-tripping.

The core model keeps **relationships as nodes**, not edges. In classical ER,
`Customer —(Places)— Order` is three nodes and two edges; modelling
relationships as edges — the crow's-foot shortcut — makes n-ary relationships,
relationship attributes and identifying relationships inexpressible. Crow's Foot
is therefore rendered as a *projection* of the node model, not as a second
model.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasERDiagram.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.0

Research write-up and the full phased feature list:
[`UltraCanvasERDiagramProposal.md`](UltraCanvasERDiagramProposal.md).
Worked examples: [`UltraCanvasERDiagramExamples.md`](UltraCanvasERDiagramExamples.md).

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasERDiagram
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasERDiagram.h"
```

## Features

- **Three notations over one model**: `Chen`, `ChenMinMax`, `CrowsFoot`, switchable at runtime
- **Entity kinds**: strong (rectangle), weak (double rectangle), associative (rectangle + inscribed diamond)
- **Relationships as nodes**: binary, **n-ary** (one diamond, 3+ legs), **recursive** (self-loop with per-leg role names), and **identifying** (double diamond)
- **Attribute kinds**: simple, composite (nested spokes), multivalued (double oval), derived (dashed oval)
- **Key roles**: primary (solid underline), partial (dashed underline), foreign, unique; `PK`/`FK`/`UK` markers in crow's-foot rows
- **Row layout options** for the table projection: badge column on either side, striped rows, an IDEF1X key compartment with underlined key names, numbered foreign keys (`FK1`/`FK2`), and per-column highlighting
- **Cardinality**: `ExactlyOne` / `ZeroOrOne` / `OneOrMany` / `ZeroOrMany`, or explicit `(min,max)` per leg
- **Participation**: partial (single line), total (double line), optional (dashed)
- **Line-end markers** selectable independently of the notation, so Chen bodies can carry crow's-foot ends
- **Cardinality label styles**: `Letters` (1/N/M/P), `MinMax` ((1,N)), `UML` (0..*), or none
- **Layouts**: Manual, AttributeSatellite, Spine, ForceDirected, Symmetric — with overlap resolution
- **Connector routing**: Straight, Orthogonal (elbow), Bezier, plus self-loop routing
- **Canvas chrome**: title band, auto-generated legend, annotation callouts with leader lines, controls overlay
- **Themes**: Default, Professional, Colorful, Pastel, Dark, Blueprint, Minimal, Print — with a per-role colour palette
- **Interaction**: zoom/pan, select, multi-select, rubber band, drag entities (attributes follow), keyboard nudge, hover isolation
- **JSON serialization**: `ToJson()` / `FromJson()` through `UltraCanvasJSON`

## Data Structures

### Enumerations

```cpp
enum class ERNotation           { Chen, ChenMinMax, CrowsFoot };
enum class ERDiagramEntityKind  { Strong, Weak, Associative };
enum class ERAttributeKind      { Simple, Composite, MultiValued, Derived };
enum class ERKeyRole            { NoKey, Primary, Partial, Foreign, Unique };
enum class ERRelationshipKind   { Regular, Identifying };
enum class ERParticipation      { Partial, Total, Optional };
enum class ERCardinality        { ExactlyOne, ZeroOrOne, OneOrMany, ZeroOrMany };

enum class ERLineEndStyle {
    NoMarker, Tick, DoubleTick, CrowsFoot, CircleCrowsFoot,
    Circle, Arrow, FilledDot
};

enum class ERCardinalityLabels  { NoLabels, Letters, MinMax, UML };

enum class ERDiagramLayout {
    Manual, AttributeSatellite, Spine, ForceDirected, Symmetric
};

enum class ERConnectorStyle     { Straight, Orthogonal, Bezier };

enum class ERDiagramTheme {
    Default, Professional, Colorful, Pastel, Dark, Blueprint, Minimal, Print
};

enum class ERRowMarkerSide      { Left, Right };
enum class ERPanelPosition      { TopLeft, TopRight, BottomLeft, BottomRight };
enum class ERAnnotationSide     { Left, Right, Top, Bottom };
```

`ERKeyRole::NoKey` is spelled that way — not `None` — because X11's `Xlib.h`
defines `None` as a preprocessor macro, and the demo application includes both.

`ERCardinalityN` (`-1`) is the sentinel for "many" in a leg's `maxCard`.

### `ERDiagramAttribute`

```cpp
struct ERDiagramAttribute {
    std::string id;          // Unique within its owner; defaults to name
    std::string name;
    std::string dataType;    // Physical level: "VARCHAR(64)", "INTEGER", ...
    std::string comment;

    ERAttributeKind kind    = ERAttributeKind::Simple;
    ERKeyRole       keyRole = ERKeyRole::NoKey;
    bool            nullable = true;
    std::string     defaultValue;

    std::vector<ERDiagramAttribute> children;   // Composite only

    double x, y, width = 96.0, height = 34.0;   // Satellite geometry (Chen)
    bool   hasPlacement = false;                // true = hand-placed, layout leaves it alone

    bool  useCustomColors = false;
    Color fillColor, borderColor, textColor;

    bool highlighted = false;    // Crow's foot: tint this column's badge cell
    int  foreignKeyIndex = 0;    // 0 = auto-number among the entity's foreign keys

    bool visible = true;
};
```

### `ERDiagramEntity`

```cpp
struct ERDiagramEntity {
    std::string id, name, alias, comment;
    ERDiagramEntityKind kind = ERDiagramEntityKind::Strong;

    std::vector<ERDiagramAttribute> attributes;

    double x, y, width = 140.0, height = 56.0;

    bool  useCustomColors = false;
    Color fillColor, borderColor, textColor;
    double borderWidth = 2.0;

    bool collapsed = false;   // Crow's foot: header only
    bool draggable = true, selectable = true, isPinned = false;
};
```

### `ERDiagramLeg` and `ERDiagramRelationship`

```cpp
struct ERDiagramLeg {
    std::string entityId;
    std::string roleName;     // Disambiguates the ends of a recursive relationship

    int minCard = 1;
    int maxCard = ERCardinalityN;
    ERParticipation participation = ERParticipation::Partial;
    ERLineEndStyle  lineEnd = ERLineEndStyle::NoMarker;   // NoMarker = derive

    ERDiagramLeg(const std::string& entityId, const std::string& role,
                 int minCard, int maxCard,
                 ERParticipation = ERParticipation::Partial);
};

struct ERDiagramRelationship {
    std::string id, name, inverseName;
    ERRelationshipKind kind = ERRelationshipKind::Regular;

    std::vector<ERDiagramLeg>       legs;         // 2 = binary, 3+ = n-ary
    std::vector<ERDiagramAttribute> attributes;   // Attributes on the diamond

    double x, y, width = 110.0, height = 62.0;
    bool   hasPlacement = false;   // false = park at the centroid of the legs

    bool IsRecursive() const;      // Both legs on the same entity
};
```

### `ERDiagramAnnotation`

```cpp
struct ERDiagramAnnotation {
    std::string id, text;
    std::string anchorRef;   // "entityId", "relationshipId" or "entityId.attributeId"
    ERAnnotationSide side = ERAnnotationSide::Left;
    double distance = 90.0;  // Leader length from the anchor
    double fontSize = 11.0;
};
```

### `ERDiagramRolePalette`

Every node role is coloured independently — the reference ER diagrams colour by
role, not per node.

```cpp
struct ERDiagramRolePalette {
    Color entityFill, entityBorder, entityText;
    Color weakEntityFill, weakEntityBorder, weakEntityText;
    Color relationshipFill, relationshipBorder, relationshipText;
    Color attributeFill, attributeBorder, attributeText;
    Color keyAttributeFill, derivedAttributeFill, multiValuedAttributeFill;
    Color rowFill, rowStripeFill, rowHighlightFill;   // Crow's-foot rows
    Color rowText, rowKeyText, rowDivider;
    Color connectorColor, cardinalityText, roleText;
};
```

### `ERDiagramStyle`

```cpp
struct ERDiagramStyle {
    std::string fontFamily = "Arial";
    double baseFontSize = 12.0, attributeFontSize = 10.0;
    double cardinalityFontSize = 10.0, rowFontSize = 10.0;

    Color  backgroundColor, gridColor;
    bool   showGrid = false;
    double gridSpacing = 25.0;

    Color  selectionColor;
    double selectionWidth = 3.0;

    ERDiagramRolePalette palette;

    // Crow's-foot row layout
    ERRowMarkerSide rowMarkerSide = ERRowMarkerSide::Left;
    bool   stripeRows = false;
    bool   keyCompartment = false;
    bool   underlineKeyRows = false;
    bool   numberForeignKeys = false;
    double rowMarkerColumnWidth = 26.0;

    double connectorWidth = 1.6;
    double cornerRadius = 4.0;
    bool   roundedEntities = false;
    double doubleLineGap = 4.0;        // Weak entity / total participation offset

    double attributeOrbit = 120.0;     // Clear gap between entity and its ovals
    double attributeSpread = 250.0;    // Angular span (degrees) of the fan
    double attributeRingStep = 46.0;   // Extra radius per overflow ring

    double linkDistance = 140.0, linkStrength = 0.1, chargeStrength = -900.0;
    int    iterations = 260;
};
```

## Constructor and Factory

```cpp
UltraCanvasERDiagram(const std::string& id, int x, int y, int width, int height);

std::shared_ptr<UltraCanvasERDiagram> CreateERDiagram(
        const std::string& id, int x, int y, int width, int height);
```

## API Reference

### Entities

```cpp
void AddEntity(const std::string& id, const std::string& name,
               ERDiagramEntityKind kind = ERDiagramEntityKind::Strong);
void AddEntity(const std::string& id, const std::string& name,
               double x, double y,
               ERDiagramEntityKind kind = ERDiagramEntityKind::Strong);
void AddEntity(const ERDiagramEntity& entity);

void RemoveEntity(const std::string& id);          // Also drops its relationships
void SetEntityPosition(const std::string& id, double x, double y);
void SetEntitySize(const std::string& id, double width, double height);
void SetEntityName(const std::string& id, const std::string& name);
void SetEntityKind(const std::string& id, ERDiagramEntityKind kind);
void SetEntityColors(const std::string& id, const Color& fill, const Color& border);
void SetEntityCollapsed(const std::string& id, bool collapsed);
void PinEntity(const std::string& id, bool pinned);   // Held fixed by ForceDirected

ERDiagramEntity*         GetEntity(const std::string& id);
std::vector<std::string> GetAllEntityIds() const;
size_t                   GetEntityCount() const;
```

`SetEntityPosition` moves the entity's attribute ovals with it.

### Attributes

```cpp
void AddAttribute(const std::string& entityId, const std::string& name,
                  ERKeyRole keyRole = ERKeyRole::NoKey,
                  ERAttributeKind kind = ERAttributeKind::Simple);
void AddAttribute(const std::string& entityId, const ERDiagramAttribute& attribute);

// Physical level: name + type in one call. A primary key is forced NOT NULL.
void AddTypedAttribute(const std::string& entityId, const std::string& name,
                       const std::string& dataType,
                       ERKeyRole keyRole = ERKeyRole::NoKey, bool nullable = true);

// Child of a composite attribute; promotes the parent to Composite.
void AddChildAttribute(const std::string& entityId,
                       const std::string& parentAttributeId,
                       const ERDiagramAttribute& child);

// Attribute hanging off a relationship (Chen: an oval on the diamond).
void AddRelationshipAttribute(const std::string& relationshipId,
                              const ERDiagramAttribute& attribute);

void RemoveAttribute(const std::string& entityId, const std::string& attributeId);
ERDiagramAttribute* GetAttribute(const std::string& entityId,
                                 const std::string& attributeId);
void SetAttributePosition(const std::string& entityId,
                          const std::string& attributeId, double x, double y);
```

`SetAttributePosition` sets `hasPlacement`, which excludes the oval from the
satellite layout — hand placement survives a re-layout and a notation switch.

### Relationships

```cpp
void AddRelationship(const std::string& id, const std::string& name,
                     const std::string& sourceEntityId,
                     const std::string& targetEntityId,
                     ERCardinality sourceCardinality = ERCardinality::ExactlyOne,
                     ERCardinality targetCardinality = ERCardinality::ZeroOrMany);
void AddRelationship(const ERDiagramRelationship& relationship);

// Double diamond; also marks the weak entity as ERDiagramEntityKind::Weak.
void AddIdentifyingRelationship(const std::string& id, const std::string& name,
                                const std::string& ownerEntityId,
                                const std::string& weakEntityId);

// Self relationship; the role names disambiguate the two ends.
void AddRecursiveRelationship(const std::string& id, const std::string& name,
                              const std::string& entityId,
                              const std::string& roleA, const std::string& roleB,
                              ERCardinality cardA = ERCardinality::ZeroOrOne,
                              ERCardinality cardB = ERCardinality::ZeroOrMany);

// One diamond, three or more legs.
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

ERDiagramRelationship*   GetRelationship(const std::string& id);
std::vector<std::string> GetAllRelationshipIds() const;
size_t                   GetRelationshipCount() const;
```

The two-entity `AddRelationship` overload derives participation from the
cardinality: `minCard > 0` becomes `ERParticipation::Total`. Set it explicitly
through `SetLegParticipation` or the verbose struct when that is not what you
mean.

### Annotations

```cpp
void AddAnnotation(const std::string& id, const std::string& text,
                   const std::string& anchorRef,
                   ERAnnotationSide side = ERAnnotationSide::Left);
void AddAnnotation(const ERDiagramAnnotation& annotation);
void RemoveAnnotation(const std::string& id);
ERDiagramAnnotation* GetAnnotation(const std::string& id);
```

Annotations are placed by `RunLayout()`, which slides each callout further along
its own side until it clears every entity, oval, diamond and other callout.

### Notation

```cpp
void SetNotation(ERNotation notation);
ERNotation GetNotation() const;

// Independent of the notation - image-1-style hybrids need this.
void SetLineEndStyle(ERLineEndStyle style);
void SetCardinalityLabelStyle(ERCardinalityLabels style);

void SetShowRoleNames(bool show);
void SetShowRelationshipNames(bool show);
void SetShowDataTypes(bool show);
```

`SetNotation` re-sizes entities for the target notation (crow's-foot boxes grow
to hold their rows; Chen boxes shrink back to a name plate). It does **not**
touch the cardinality label style or the marker set — the relational/MERISE
style is a table notation carrying `(min,max)` labels and plain arrowheads, so
neither can be inferred from the notation alone.

`ERLineEndStyle` resolution order per leg:

1. An explicit `leg.lineEnd` wins.
2. Otherwise, if the diagram-wide `lineEndStyle` is a crow's-foot-family marker
   (`Tick`, `DoubleTick`, `CrowsFoot`, `CircleCrowsFoot`, `Circle`), or it is
   unset and the notation is `CrowsFoot`, the glyph is derived from
   `(minCard, maxCard)`.
3. Otherwise the diagram-wide `lineEndStyle` is used verbatim — so
   `SetLineEndStyle(ERLineEndStyle::Arrow)` really does draw arrowheads, in
   every notation.

### Layout

```cpp
void SetLayout(ERDiagramLayout layout);
void RunLayout();                                   // Auto-fits unless disabled
void RunForceDirectedLayout(int iterations = 0);    // 0 = style.iterations
void ApplyAttributeSatelliteLayout();
void ApplySpineLayout(bool horizontal = true);
void ApplySymmetricLayout();

void SetAutoFitOnLayout(bool autoFit);
void SetSnapToGrid(bool enabled);
void SetSnapGrid(double snapX, double snapY);
```

| Layout | Behaviour |
|---|---|
| `Manual` | Keeps hand-set positions; still parks unplaced diamonds and fans the attribute haloes |
| `AttributeSatellite` | As `Manual`; the fan is the point of it |
| `Spine` | Lays entity–relationship–entity chains along one axis |
| `ForceDirected` | Entities and diamonds as bodies, legs as springs; ellipse seeding and aspect-scaled centring keep the result in the viewport's proportions, then an overlap pass guarantees no two bodies touch |
| `Symmetric` | Two entities left and right, diamond centred — the textbook figure |

Attribute ovals are fanned onto the arc facing *away* from their entity's
relationships, at a radius derived from the entity's half-diagonal plus half the
oval, so an oval can never land on its own entity. A de-collision pass then
pushes any oval clear of neighbouring haloes, boxes and diamonds.

### Styling and themes

```cpp
void SetTheme(ERDiagramTheme theme);
void SetStyle(const ERDiagramStyle& style);
const ERDiagramStyle& GetStyle() const;
void SetRolePalette(const ERDiagramRolePalette& palette);
void SetBackgroundColor(const Color& color);
void SetGridVisible(bool visible, double spacing = 25.0);
void SetFontFamily(const std::string& fontFamily);
void SetFontSize(double size);
void SetConnectorStyle(ERConnectorStyle style);

Color GetRoleFill(ERNodeRole role) const;
Color GetRoleBorder(ERNodeRole role) const;
Color GetRoleText(ERNodeRole role) const;
```

### Crow's-foot row layout

House styles disagree on every one of these, so they are settings rather than a
fixed convention.

```cpp
void SetRowMarkerSide(ERRowMarkerSide side);   // PK/FK badge column left or right
void SetStripeRows(bool stripe);               // Alternate row background
void SetKeyCompartment(bool enabled);          // Key columns above a divider rule
void SetUnderlineKeyRows(bool underline);      // IDEF1X: underline key column names
void SetNumberForeignKeys(bool numbered);      // FK1 / FK2 instead of a bare FK
void SetAttributeHighlighted(const std::string& entityId,
                             const std::string& attributeId, bool highlighted);
```

`SetKeyCompartment(true)` hoists the key columns above the rule for display
only — the model keeps whatever order the caller supplied, and the divider is
drawn only when there are columns on both sides of it.

`SetNumberForeignKeys(true)` numbers foreign keys in order of appearance within
the entity, unless the attribute sets `foreignKeyIndex` explicitly. The badge
column widens automatically to hold the longer text, so `AutoSizeAll()` and the
renderer never disagree about the row width.

`SetTheme` also recolours the title band, unless `SetTitleConfig` has already
supplied explicit colours.

### Canvas chrome

```cpp
void SetTitle(const std::string& text, const std::string& subtitle = "");
void SetTitleConfig(const ERDiagramTitleConfig& config);

void SetLegendVisible(bool visible, ERPanelPosition position = ERPanelPosition::TopLeft);
void SetLegendConfig(const ERDiagramLegendConfig& config);

void SetControlsVisible(bool visible);
void SetControlsPosition(ERPanelPosition position);
void SetControlsConfig(const ERDiagramControlsConfig& config);
```

The legend is generated from the active notation and marker set, so it cannot
drift out of sync with what is drawn.

### Viewport

```cpp
void     SetZoomLevel(double zoom);
double   GetZoomLevel() const;
void     SetPanOffset(double x, double y);
Point2Dd GetPanOffset() const;
void     ZoomIn(double factor = 1.2);
void     ZoomOut(double factor = 1.2);
void     FitView(double padding = 40.0);
void     CenterOn(double worldX, double worldY);
void     SetMinZoom(double minZ);
void     SetMaxZoom(double maxZ);
Rect2Dd  GetContentBounds() const;
```

### Selection and interaction

```cpp
void SelectEntity(const std::string& id, bool addToSelection = false);
void SelectRelationship(const std::string& id, bool addToSelection = false);
void SelectAll();
void DeselectAll();
void Clear();

std::vector<std::string> GetSelectedEntityIds() const;
std::vector<std::string> GetSelectedRelationshipIds() const;
bool IsEntitySelected(const std::string& id) const;

void SetInteractive(bool interactive);
void SetPanOnDrag(bool pan);
void SetZoomOnScroll(bool zoom);
void SetHighlightRelatedOnHover(bool enabled);
```

| Input | Action |
|---|---|
| Wheel | Zoom at cursor |
| Drag empty canvas | Pan (or rubber-band select with Shift, or when `SetPanOnDrag(false)`) |
| Middle-drag | Pan, even when locked |
| Click node | Select; Shift+click toggles |
| Drag node | Move it; an entity carries its attribute ovals |
| Right-click canvas | `onCanvasRightClick(worldX, worldY)` |
| `Ctrl/Cmd+A` | Select all |
| `Escape` | Deselect / cancel |
| `Delete` / `Backspace` | Delete the selection |
| Arrow keys | Nudge 1px, or 10px with Shift |

### Measurement

```cpp
void MeasureLabel(const std::string& label, double fontSize,
                  int& outWidth, int& outHeight) const;
void SuggestEntitySizeForName(const std::string& name,
                              double& outWidth, double& outHeight) const;
void AutoSizeAll();
```

`AutoSizeAll()` sizes every entity, diamond and oval to its own text. Call it
before `RunLayout()` when the model was built from data.

### Serialization

```cpp
std::string ToJson() const;
bool        FromJson(const std::string& json);
```

Round-trips entities (with nested composite attributes), relationships (with all
legs), annotations, the notation and the viewport, through `UltraCanvasJSON`.
`FromJson` re-runs relationship placement and the attribute layout.

### Callbacks

```cpp
std::function<void(const std::string&)> onEntityClick;
std::function<void(const std::string&)> onEntityDoubleClick;
std::function<void(const std::string&, double, double)> onEntityDrag;
std::function<void(const std::string&)> onRelationshipClick;
std::function<void(const std::string&, const std::string&)> onAttributeClick;
std::function<void(const std::vector<std::string>&,
                   const std::vector<std::string>&)> onSelectionChange;
std::function<void(double zoom, double panX, double panY)> onViewportChange;
std::function<void(double worldX, double worldY)> onCanvasRightClick;
std::function<void()> onModelChanged;
```

## Quick Start

```cpp
#include "Plugins/Diagrams/UltraCanvasERDiagram.h"

auto er = CreateERDiagram("SalesERD", 0, 0, 900, 620);

er->SetNotation(ERNotation::Chen);
er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);   // Chen bodies, crow's-foot ends
er->SetTheme(ERDiagramTheme::Colorful);
er->SetTitle("Internet Sales Model");
er->SetLegendVisible(true, ERPanelPosition::TopLeft);

er->AddEntity("customer", "Customer");
er->AddAttribute("customer", "Name", ERKeyRole::Primary);   // Underlined
er->AddAttribute("customer", "E-mail");

er->AddEntity("order", "Order");
er->AddAttribute("order", "Order Number", ERKeyRole::Primary);

er->AddRelationship("places", "Orders", "customer", "order",
                    ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

er->AutoSizeAll();
er->SetLayout(ERDiagramLayout::AttributeSatellite);
er->RunLayout();                                  // Auto-fits

er->onEntityClick = [](const std::string& id) { /* ... */ };
```

## Notes and limits

- Crow's Foot renders binary relationships only; an n-ary relationship keeps its
  legs in the model but has no crow's-foot projection, so switch back to a Chen
  notation to see it. Nothing is lost from the model either way.
- Generalization/specialization (ISA), aggregation, subject-area groups,
  drag-to-connect editing, obstacle-avoiding routing, model validation and
  SQL/Mermaid interchange are Phase 2/3 in
  [the proposal](UltraCanvasERDiagramProposal.md) and are not in 1.0.0.
- `MeasureLabel` / `SuggestEntitySizeForName` / `AutoSizeAll` use the
  average-glyph-width approximation, because real measurement needs an
  `IRenderContext`, which is only available during `Render()`. This is the same
  contract as `UltraCanvasNodeDiagram::MeasureLabel`.
