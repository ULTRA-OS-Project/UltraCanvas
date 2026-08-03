# UltraCanvasRequirementDiagram Documentation

## Overview

**UltraCanvasRequirementDiagram** renders a SysML **requirement diagram**
(`req`): requirements as compartmented boxes carrying `id`, `text`, `source`,
`risk` and `verifyMethod`, the containment hierarchy that decomposes them, and
the typed relationships that trace design elements and test cases back to the
requirements they satisfy or verify.

The element owns a requirement **model** that is valid and queryable
independently of geometry — layout writes positions into it, rendering reads
it. Containment defines the hierarchy used by the tidy-tree layout; the six
dependency relationships plus generalisation are routed and labelled
separately.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasRequirementDiagram.h`
**Model header:** `include/Plugins/Diagrams/UltraCanvasRequirementModel.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 3.0.0

Research write-up and the full phased feature list:
[`UltraCanvasRequirementDiagramProposal.md`](UltraCanvasRequirementDiagramProposal.md).
This document covers the shipped phase-1, phase-2 and phase-3 API.

### Model and view

The data, semantics and text interchange live in **`RequirementModel`**, which
depends on nothing but `Color` and the standard library. The element owns one
model and forwards its API, so you can use either level:

```cpp
auto req = CreateRequirementDiagram("systemReq", 0, 0, 900, 600);
req->AddRequirement("R1", "Braking", "…");        // through the element
req->GetModel().AddRequirement("R2", "Steering", "…");  // straight at the model
req->NotifyModelChanged();   // after direct model edits, so the view re-measures
```

`RequirementModel` runs headless — it is what
`Tests/RequirementModelTests.cpp` exercises without a render context.

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasRequirementDiagram   (view: layout, rendering, interaction)
            └── owns RequirementModel   (data, semantics, analysis, interchange)
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
```

## Features

- **Requirement model**: `id`, `name`, `stereotype`, `text`, `source`, `risk`,
  `verifyMethod`, `status`, `owner`, `priority`, `docRef` plus arbitrary
  user-defined properties
- **Nine node kinds**: requirement, block, test case, use case, actor, package,
  rationale, problem, note — each with a default shape and palette entry
- **Eight relationship kinds**: containment, deriveReqt, satisfy, verify,
  refine, trace, copy, generalization — each with its correct SysML notation
- **Compartment templates**: ordered `key="value"` property rows bound to model
  fields, diagram-wide or per node, with empty rows auto-hidden
- **Detail levels**: `Collapsed` (name only), `Standard` (id + text), `Full`,
  `Custom`
- **Real word wrap** of long `text` values with a per-row line cap and ellipsis
- **Auto node sizing** from measured text, clamped to a min/max width range
- **Containment bus routing**: one drop from the parent, one shared spine, one
  riser per child, with the ⊕ crosshair at the parent end
- **Tidy tree layout** over variable-size boxes, in four orientations
- **Callouts**: free-floating, draggable detail notes anchored to an element
- **Legend panel** auto-populated from the categories, node kinds or
  relationship kinds actually present
- **Title banner**, six built-in palettes, five colour sources
- **Pan / zoom / fit**, multi-select, rubber-band selection, node dragging,
  snap-to-grid, hover tooltips
- **Containment cycle detection** and model validation with a warning callback
- **Endpoint semantics** per relationship kind, with automatic correction of a
  backwards `satisfy` / `verify` / `refine`
- **Compartment notation**: `satisfiedBy`, `verifiedBy`, `derived`, … computed
  from the relations and listed inside the box instead of drawn as lines
- **Coverage analysis**: uncovered / unverified / orphan requirements, coverage
  statistics, and a corner-badge overlay
- **Trace highlighting**, expand/collapse sub-trees, and filters by kind,
  category, risk or status
- **Layered (Sugiyama) layout** for non-tree topologies, and obstacle-aware
  orthogonal routing
- **Editing**: create node, drag-to-connect, delete, inline rename
- **SysML diagram frame** with the `req [Package] Name [Diagram]` pentagon tab
- **Copy semantics**: a copy mirrors its master's id and text, and is badged
  as suspect when the master is edited afterwards
- **Package grouping regions**, status chips, «block» port nubs and
  relationship rationale notes
- **Self-relations and multi-edges** fanned apart so parallel arrows stay
  distinguishable
- **Force-directed layout** for dense trace webs
- **Minimap and controls overlays**, config-compatible with
  `UltraCanvasNodeDiagram`
- **Search** with focus-on-result, and **traceability-matrix** export
- **JSON save/load** covering model, layout and style, plus **Mermaid**, **CSV**
  and **ReqIF** import/export

## Quick Start

```cpp
#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
using namespace UltraCanvas;

auto req = CreateRequirementDiagram("systemReq", 10, 10, 900, 600);
req->SetPalette(RequirementPaletteKind::Pastel);

req->AddRequirement("R1", "Eco-Friendliness",
                    "The vehicle shall be eco-friendly.",
                    RequirementRisk::High, RequirementVerifyMethod::Analysis);
req->AddRequirement("R1.1", "Emissions",
                    "The vehicle shall meet Ultra-Low Emissions Vehicle standards.");
req->AddNode(RequirementNodeKind::TestCase, "TC1", "Emissions Test");

req->AddContainment("R1", "R1.1");                                  // ⊕ solid
req->AddRelation(RequirementRelationKind::Verify, "TC1", "R1.1");   // dashed «verify»

req->SetLayoutMode(RequirementLayoutMode::ContainmentTree);
req->RunLayout();     // auto-fits the view by default
```

`RunLayout()` requests a pass; node sizes depend on real text metrics, which
are only available inside `Render()`, so the pass completes at the start of the
next frame. Positions read back before that come from a heuristic measurement,
so calling `RunLayout()` during construction is safe.

## Nodes

### Adding nodes

```cpp
// Verbose form — full control
RequirementNode node("UR1.2", "Eco-Friendliness");
node.kind         = RequirementNodeKind::Requirement;
node.stereotype   = "performanceRequirement";   // "" = derived from the kind
node.text         = "The vehicle shall be eco-friendly.";
node.source       = "Marketing";
node.risk         = RequirementRisk::High;
node.verifyMethod = RequirementVerifyMethod::Analysis;
node.category     = "Environment";
node.detail       = RequirementDetailLevel::Full;
node.customProperties["allocatedTo"] = "Powertrain";
req->AddNode(node);

// Convenience forms
req->AddRequirement("UR1.3", "Performance", "The vehicle shall …");
req->AddRequirement("UR1.4", "Braking", "…", RequirementRisk::Medium,
                    RequirementVerifyMethod::Test);
req->AddNode(RequirementNodeKind::Block,    "PWR", "Powertrain");
req->AddNode(RequirementNodeKind::TestCase, "TC1", "Emissions Test");
```

`id` is both the unique model key used by relationships and the value shown in
the `id` property row. Adding a duplicate id is rejected and reported through
`onValidationWarning`.

### Node kinds and default shapes

| Kind | Stereotype | Default shape |
|---|---|---|
| `Requirement` | `«requirement»` | Rectangle |
| `Block` | `«block»` | Rectangle |
| `TestCase` | `«testCase»` | Rounded rectangle |
| `UseCase` | `«useCase»` | Oval |
| `Actor` | `«actor»` | Stick figure |
| `Package` | `«package»` | Folder (tab + body) |
| `Rationale` / `Problem` / `Note` | `«rationale»` / … | Folded-corner note |

Set `RequirementNode::shape` to override with any of `Rectangle`,
`RoundedRectangle`, `Oval`, `Folder`, `FoldedNote` or `StickFigure`.

### Requirement specialisations

The SysML requirement specialisations are plain stereotype strings, so any
profile works:

```cpp
node.stereotype = "functionalRequirement";   // also: interfaceRequirement,
                                             // performanceRequirement,
                                             // physicalRequirement,
                                             // designConstraint, …
```

### Detail levels

| Level | Shows |
|---|---|
| `Collapsed` | `«stereotype»` + bold name only |
| `Standard` | header + `id` and `text` |
| `Full` | header + every non-empty row of the diagram template |
| `Custom` | header + the node's own template |

```cpp
req->SetDefaultDetailLevel(RequirementDetailLevel::Collapsed);  // new nodes
req->SetNodeDetail("UR1.2", RequirementDetailLevel::Full);      // one node
```

Rows whose value is empty are dropped automatically, so a leaf with no
properties renders as a plain name box with no compartment.

## Compartment templates

A template is an ordered list of rows, each bound to a model field or to a
user-defined property:

```cpp
RequirementNodeTemplate tpl;
tpl.AddPropertyRow("id",           RequirementField::Id);
tpl.AddPropertyRow("source",       RequirementField::Source);
tpl.AddPropertyRow("text",         RequirementField::Text);
tpl.AddPropertyRow("verifyMethod", RequirementField::VerifyMethod);
tpl.AddPropertyRow("risk",         RequirementField::Risk);
tpl.AddCustomRow("allocatedTo", "allocatedTo");     // customProperties key
tpl.SetPropertyFormat(RequirementPropertyFormat::KeyEqualsQuotedValue);
req->SetNodeTemplate(tpl);

req->SetNodeTemplate("UR1.2", specialTemplate);     // per-node override
```

Two ready-made templates:

```cpp
RequirementNodeTemplate::Extended();   // id, source, text, verifyMethod, risk
RequirementNodeTemplate::Standard();   // id, text
```

Formats: `KeyEqualsQuotedValue` renders `text="The vehicle shall …"` (the SysML
tool convention); `KeyColonValue` renders `text: The vehicle shall …`.

Bindable fields: `Id`, `Name`, `Stereotype`, `Text`, `Source`, `Risk`,
`VerifyMethod`, `Status`, `Owner`, `Priority`, `DocRef`, `Custom`.

## Relationships

```cpp
req->AddContainment("UR1", "UR1.2");    // parent owns child

req->AddRelation(RequirementRelationKind::DeriveReqt, "UR1.3", "UR1.2");
req->AddRelation(RequirementRelationKind::Satisfy,    "PWR", "UR1.3");
req->AddRelation(RequirementRelationKind::Verify,     "TC1", "UR1.2");
req->AddRelation(RequirementRelationKind::Refine,     "UC1", "UR1.5");
req->AddRelation(RequirementRelationKind::Trace,      "A1",  "UR1.5");
req->AddRelation(RequirementRelationKind::Generalization, "ACT", "BEHAV");

// Verbose form
RequirementRelation rel(RequirementRelationKind::Satisfy, "PWR", "UR1.3");
rel.rationale = "Chosen after the trade study in TR-114.";
rel.routing = RequirementRouting::Curved;
rel.useDefaultRouting = false;
const std::string relId = req->AddRelation(rel);
```

`AddRelation` returns the relation id (generated when left empty), or an empty
string when an endpoint is unknown.

### Notation

| Kind | Line | End decoration | Label |
|---|---|---|---|
| `Containment` | Solid, shared bus | ⊕ crosshair at the **parent** | — |
| `DeriveReqt` | Dashed | Open arrowhead | `«deriveReqt»` |
| `Satisfy` | Dashed | Open arrowhead | `«satisfy»` |
| `Verify` | Dashed | Open arrowhead | `«verify»` |
| `Refine` | Dashed | Open arrowhead | `«refine»` |
| `Trace` | Dashed | Open arrowhead | `«trace»` |
| `Copy` | Dashed | Open arrowhead | `«copy»` |
| `Generalization` | Solid | Hollow closed triangle | — |

**Direction matters.** Every dependency points *from the dependent element to
the requirement*: a test case verifies a requirement, a block satisfies a
requirement. Only containment points from parent to child.

### Hiding a relationship

A hidden relation still takes part in the model — layout, `GetParentId`,
`GetChildIds`, validation — but is not drawn or hit-tested. Use it when
containment defines the hierarchy and a different relationship carries the
visible notation:

```cpp
req->AddContainment("BEHAV", "ACT");                              // layout
req->AddRelation(RequirementRelationKind::Generalization, "ACT", "BEHAV");
req->SetRelationKindVisible(RequirementRelationKind::Containment, false);
req->SetRelationVisible(relId, false);                            // one relation
```

### Routing

```cpp
req->SetDefaultRouting(RequirementRouting::Orthogonal);   // Straight, Curved, Bus
req->SetRelationRouting(relId, RequirementRouting::Curved);
```

Connectors leave the face of a box that actually points at the other box, and
connectors sharing a face are distributed evenly along it so parallel arrows
never overlap. `Bus` is meaningful only for containment; elsewhere it falls
back to orthogonal.

## Model queries and integrity

```cpp
const std::string parent = req->GetParentId("UR1.2");
const std::vector<std::string> children = req->GetChildIds("UR1");
const std::vector<std::string> roots    = req->GetRootIds();
const std::vector<std::string> touching = req->GetRelationsOf("UR1.2");

if (req->HasContainmentCycle()) {
    for (const auto& id : req->GetContainmentCycleNodes()) { /* … */ }
}

req->onValidationWarning = [](const RequirementWarning& w) {
    printf("[%s] %s\n", w.nodeId.c_str(), w.message.c_str());
};
const std::vector<RequirementWarning> warnings = req->Validate();
```

`RequirementWarning::Kind` covers `DuplicateNodeId`, `UnknownEndpoint`,
`ContainmentCycle`, `MultipleParents` and `DuplicateRelation`. A cyclic
containment graph cannot be laid out as a tree, so `ContainmentTree` falls back
to a grid arrangement and reports the cycle.

### Building a whole diagram at once

```cpp
std::vector<RequirementNode> nodes = LoadNodes();
std::vector<RequirementRelation> relations = LoadRelations();
req->BuildFromRelations(nodes, relations);   // clears, adds, switches to
                                             // ContainmentTree and lays out
```

## Layout

```cpp
req->SetLayoutMode(RequirementLayoutMode::ContainmentTree);   // or Manual
req->SetLayoutOrientation(RequirementOrientation::TopDown);   // BottomUp,
                                                              // LeftRight, RightLeft
req->SetLevelGap(60.0);      // between tree levels
req->SetSiblingGap(22.0);    // between siblings
req->SetSubtreeGap(30.0);    // between separate root trees
req->SetAutoFitOnLayout(true);
req->RunLayout();
```

The tree layout is a tidy tree over **variable-size** boxes: each subtree gets a
contiguous band on the sibling axis, and a parent is centred over the span of
its children. Boxes never overlap regardless of how much text they carry.

A node with `pinned = true` (set explicitly, or automatically when the user
drags it) keeps its position through the next auto-layout:

```cpp
req->SetNodePosition("UR1.2", 320.0, 140.0);
req->SetNodePinned("UR1.2", true);
```

## Callouts

A callout is a draggable note showing a subset of one element's properties,
joined to it by a dashed leader line:

```cpp
req->AddCallout("emissionsNote", "UR1.2.1",
                {RequirementField::Id, RequirementField::Text},
                /*x*/ 40.0, /*y*/ 520.0);

RequirementCallout callout("note2", "UR1.3", 600.0, 480.0);
callout.fields = {RequirementField::Risk, RequirementField::VerifyMethod};
callout.headerText = "Verification";
callout.width = 240.0;
req->AddCallout(callout);
```

An empty `fields` list defaults to `id` + `text`.

## Categories, colours and the legend

```cpp
req->AddCategory("Environment", Color(212, 240, 226), Color(110, 165, 135));
req->AddCategory("Performance", Color(206, 236, 240), Color(100, 155, 165));
req->SetNodeCategory("UR1.2", "Environment");
req->SetColorSource(RequirementColorSource::ByCategory);

req->SetLegendVisible(true, RequirementPanelPosition::BottomLeft);
req->SetLegendSource(RequirementLegendSource::Categories);
```

Colour sources: `ByKind` (default), `ByCategory`, `ByRisk`, `ByStatus`,
`Explicit`. Per-node overrides always win:

```cpp
req->SetNodeColors("UR1.2", Color(255, 240, 200), Color(180, 150, 60));
req->SetStatusColor("Approved", Color(210, 240, 210));   // for ByStatus
req->SetRiskStripeVisible(true);                          // risk-coloured edge stripe
```

Legend sources: `Categories`, `NodeKinds`, `RelationKinds` (drawn as line
samples with the right dash pattern), `Custom`:

```cpp
req->SetLegendEntries({{"Approved", Color(210, 240, 210)},
                       {"Proposed", Color(240, 235, 200)}});
```

Only categories/kinds actually present on the diagram are listed, so the legend
never advertises an unused colour.

## Palettes and style

```cpp
req->SetPalette(RequirementPaletteKind::Pastel);
```

| Palette | Look |
|---|---|
| `Classic` | White boxes, black lines — textbook SysML |
| `Pastel` | Soft yellow/green fills |
| `Vibrant` | Saturated colour-by-kind |
| `Professional` | Muted blue-grey with a header band |
| `Dark` | Dark canvas, light boxes |
| `Monochrome` | Greyscale, for print |

```cpp
RequirementPalette custom = RequirementPalette::BuiltIn(RequirementPaletteKind::Classic);
custom.requirementFill = Color(240, 248, 255);
custom.satisfyColor    = Color(0, 140, 90);
req->SetCustomPalette(custom);

req->SetFontFamily("Arial");
req->SetFontSize(/*property*/ 10.0, /*name*/ 12.0);
req->SetNodeWidthRange(130.0, 230.0);
req->SetGridVisible(true, 25.0);
req->SetBackgroundColor(Color(252, 252, 253));

RequirementDiagramStyle style = req->GetStyle();
style.maxTextLines = 4;        // per property row before the ellipsis
style.arrowSize = 10.0;
style.crosshairRadius = 7.0;
req->SetStyle(style);
```

## Title banner

```cpp
req->SetTitle("Smart Home Automation — System Requirement Diagram");
req->SetTitle("HSV Specification", "Revision C, 2026-07");

RequirementTitleConfig cfg = req->GetTitleConfig();
cfg.backgroundColor = Color(64, 96, 160);
cfg.alignment = TextAlignment::Center;
req->SetTitleConfig(cfg);
```

## Viewport and interaction

```cpp
req->ZoomIn();  req->ZoomOut();
req->FitView(30.0);
req->CenterOn(400.0, 300.0);
req->SetZoomLevel(1.5);
req->SetMinZoom(0.2);  req->SetMaxZoom(4.0);

req->SetInteractive(true);
req->SetNodesDraggable(true);
req->SetPanOnDrag(true);
req->SetZoomOnScroll(true);
req->SetSnapToGrid(true);
req->SetSnapGrid(10.0, 10.0);
req->SetTooltipsEnabled(true);
```

| Input | Action |
|---|---|
| Click a box / connector | Select it |
| Shift + click | Add to / remove from the selection |
| Drag a box | Move it (and the rest of the selection); pins it |
| Drag empty canvas | Pan |
| Shift + drag empty canvas | Rubber-band select |
| Middle-drag | Pan, even when the diagram is locked |
| Mouse wheel | Zoom at the cursor |
| `Ctrl+A` / `Escape` | Select all / deselect all |

### Selection API

```cpp
req->SelectNode("UR1.2");
req->SelectNode("UR1.3", /*addToSelection*/ true);
req->SelectRelation(relId);
req->SelectAll();
req->DeselectAll();

const std::vector<std::string> nodeIds = req->GetSelectedNodeIds();
const std::vector<std::string> relIds  = req->GetSelectedRelationIds();
bool selected = req->IsNodeSelected("UR1.2");
```

### Callbacks

```cpp
req->onNodeClick        = [](const std::string& id) { /* … */ };
req->onNodeDoubleClick  = [](const std::string& id) { /* … */ };
req->onNodeHover        = [](const std::string& id) { /* … */ };
req->onNodeDrag         = [](const std::string& id, double x, double y) { /* … */ };
req->onRelationClick    = [](const std::string& id) { /* … */ };
req->onSelectionChange  = [](const std::vector<std::string>& nodes,
                             const std::vector<std::string>& rels) { /* … */ };
req->onViewportChange   = [](double zoom, double panX, double panY) { /* … */ };
req->onCanvasRightClick = [](double worldX, double worldY) { /* … */ };
req->onValidationWarning = [](const RequirementWarning& w) { /* … */ };
```

## Endpoint semantics

Each relationship kind has legal endpoint kinds. A backwards `satisfy`,
`verify` or `refine` — where the reverse pairing *is* legal — is flipped and
reported rather than refused, because that is nearly always a typo:

```cpp
req->AddRelation(RequirementRelationKind::Verify, "R1", "TC1");   // backwards
// stored as TC1 -> R1, with a DirectionCorrected warning
```

Anything that is illegal in both directions is kept and flagged (drawn in the
warning colour) in the default lenient mode, or refused outright in strict
mode:

```cpp
req->SetSemanticsMode(RequirementSemanticsMode::Strict);
// RequirementModel::IsLegalEndpointPair() is the rule table, and is static:
bool ok = RequirementModel::IsLegalEndpointPair(
    RequirementRelationKind::Verify,
    RequirementNodeKind::TestCase, RequirementNodeKind::Requirement);   // true
```

| Kind | Legal source → target |
|---|---|
| `Containment` | requirement / package / block → any non-note |
| `DeriveReqt`, `Copy` | requirement → requirement |
| `Satisfy`, `Refine` | any non-requirement, non-note → requirement |
| `Verify` | test case → requirement |
| `Trace` | anything → anything (deliberately unconstrained) |
| `Generalization` | same kind → same kind |

## Hierarchical id numbering

```cpp
const int renamed = req->AssignHierarchicalIds("R");
// roots become R1, R2 …; children R1.1, R1.1.1 …
```

Every relation endpoint, callout target and note anchor follows the rename. A
cyclic containment graph is refused (returns 0) rather than half-renumbered.

## Compartment notation

Instead of drawing every trace as a line, list the related elements inside the
box — the SysML alternative notation, and the only readable option on a dense
diagram:

```cpp
RequirementNodeTemplate tpl = RequirementNodeTemplate::Standard();
tpl.AddDerivedCompartment(RequirementDerivedList::SatisfiedBy);
tpl.AddDerivedCompartment(RequirementDerivedList::VerifiedBy);
req->SetNodeTemplate(tpl);

// …and hide the lines those compartments now stand in for
req->SetRelationKindVisible(RequirementRelationKind::Satisfy, false);
req->SetRelationKindVisible(RequirementRelationKind::Verify, false);
```

Or use the ready-made `RequirementNodeTemplate::WithTraceCompartments()`.

Available lists: `Derived`, `DerivedFrom`, `SatisfiedBy`, `VerifiedBy`,
`RefinedBy`, `TracedTo`, `Master`. Empty compartments are not drawn. Query them
directly with `GetModel().GetDerivedElements(id, list)`.

## Coverage analysis

```cpp
for (const auto& id : req->GetUnverifiedRequirements()) { /* no test case */ }
for (const auto& id : req->GetUncoveredRequirements())  { /* nothing satisfies */ }
for (const auto& id : req->GetOrphanRequirements())     { /* no relations */ }

RequirementCoverage coverage = req->GetCoverage();
printf("%d requirements, %.0f%% satisfied, %.0f%% verified, %d orphans\n",
       coverage.requirementCount, coverage.satisfiedPercent,
       coverage.verifiedPercent, coverage.orphanCount);

RequirementCoverage safety = req->GetCoverageForCategory("Safety");

req->SetCoverageOverlayVisible(true);   // green / amber / red corner badges
req->SetLegendSource(RequirementLegendSource::Coverage);
```

Only `Requirement` nodes are counted — blocks and test cases are not
requirements and never appear in the denominator.

## Trace highlighting and filters

```cpp
req->HighlightTraceChain("R1.2", RequirementTraceDirection::Both, /*depth*/ 2);
req->ClearHighlight();                    // Escape does this too

req->SetKindVisible(RequirementNodeKind::TestCase, false);
req->SetCategoryVisible("Environment", false);
req->SetRiskVisible(RequirementRisk::Low, false);
req->SetStatusVisible("Rejected", false);
req->ClearFilters();
bool shown = req->IsNodeDisplayed("R1.2");
```

Filtered nodes are hidden **and excluded from the layout**, so the remaining
boxes close up rather than leaving holes.

## Expand / collapse

Any box with children carries a ⊖ toggle at its bottom-right corner (the
bottom-centre is where the containment bus leaves, and carries the ⊕
crosshair). Collapsing hides the whole sub-tree and shows a count badge.

```cpp
req->SetNodeCollapsed("R1.2", true);
req->ToggleNodeCollapsed("R1.2");
req->CollapseAll();
req->ExpandAll();
```

## Layout modes and routing

```cpp
req->SetLayoutMode(RequirementLayoutMode::Layered);   // non-tree topologies
req->SetLayoutRelationKinds({RequirementRelationKind::Containment,
                             RequirementRelationKind::DeriveReqt});
req->SetObstacleAvoidance(true);        // A* around the boxes
```

`Layered` assigns longest-path layers over the layout relation kinds, reduces
crossings with barycentre sweeps, then places boxes by their real sizes — use
it when the diagram is a trace web rather than a tree.

## Editing

```cpp
req->SetEditMode(RequirementEditMode::CreateNode);
req->SetPendingNodeKind(RequirementNodeKind::Requirement);

req->SetEditMode(RequirementEditMode::CreateRelation);
req->SetPendingRelationKind(RequirementRelationKind::Satisfy);

req->SetDoubleClickAction(RequirementDoubleClickAction::ToggleDetail);
req->BeginRename("R1.2");   // or press F2 with one node selected
req->DeleteSelected();      // or press Delete
```

| Key | Action |
|---|---|
| `F2` | Rename the selected node (first keystroke replaces the name) |
| `Enter` / `Escape` | Commit / cancel the rename |
| `Delete` | Delete the selection |
| `Escape` | Clear a trace highlight, then the selection |

Callbacks: `onNodeCreated`, `onRelationCreated`, `onNodeRenamed`.

## Diagram frame

```cpp
req->SetFrame("Package", "HSVSpecification", "Requirements Diagram");
// draws: req [Package] HSVSpecification [Requirements Diagram]
```

## Copy semantics

A `Copy` relation reads "source is a read-only copy of target". `SyncCopies()`
pulls each master's id and text into its copies and records a snapshot; a later
edit to the master makes the copy **suspect**, which the diagram badges with an
orange exclamation disc.

```cpp
req->AddRelation(RequirementRelationKind::Copy, "R1-COPY", "R1");
req->SyncCopies();                       // copy now mirrors R1

req->GetNode("R1")->text = "…revised…";  // master moves on
for (const auto& id : req->GetSuspectCopies()) { /* stale */ }
req->RefreshCopy("R1-COPY");             // re-sync one copy
req->SetSuspectBadgeVisible(false);      // if you'd rather not see the badge
```

`Validate()` reports stale copies as `RequirementWarning::Kind::SuspectCopy`.
The badge is refreshed every frame, so an edit made anywhere shows up without
the caller having to re-validate.

## Search

```cpp
for (const auto& hit : req->FindNodes("brak")) { /* hit.nodeId, hit.field */ }

req->FocusOnNode("R1.2");                 // centres, selects, expands ancestors
const std::string found = req->FindAndFocus("cockpit");   // search + focus
```

Search is case-insensitive over id, `externalId`, name, text and the custom
properties. Exact id/name matches rank first, then id substrings, then names,
then text, then properties.

## Traceability matrix

```cpp
RequirementTraceMatrix matrix = req->BuildTraceMatrix();
for (size_t r = 0; r < matrix.RowCount(); ++r) {
    for (size_t c = 0; c < matrix.ColumnCount(); ++c) {
        for (auto kind : matrix.Cell(r, c)) { /* satisfy / verify / … */ }
    }
}
const std::string csv = req->ToTraceMatrixCsv();
```

Rows are **every** requirement — including the uncovered ones, which is the
point of the matrix. Columns are only the elements that actually link to a
requirement. It is produced as data, so it can be rendered by
`UltraCanvasTable`, exported, or asserted on in a test.

## Package regions, overlays and decorations

```cpp
req->SetPackageRegionsVisible(true);      // dashed band around a «package»
req->SetMinimapVisible(true);
req->SetControlsVisible(true);            // zoom / fit / lock buttons
req->SetRationaleNotesVisible(true);
req->SetRelationRationale(relationId, "Chosen after the trade study in TR-114.");
req->SetNodePortCount("PWR", 3);          // «block» port nubs on the top edge
```

A node's `status` renders as a chip hanging above the box; give a status a
colour with `SetStatusColor()` and the chip uses it. Rationale notes place
themselves around the relationship's midpoint, avoiding the boxes.

## Force-directed layout

```cpp
req->SetLayoutMode(RequirementLayoutMode::ForceDirected);
RequirementDiagramStyle style = req->GetStyle();
style.forceLinkDistance = 150.0;
style.forceCharge = -900.0;
style.forceIterations = 320;
req->SetStyle(style);
req->RunLayout();
```

Every visible relation acts as a spring. The seed is a deterministic circle
rather than random placement, so repeated runs give the same arrangement, and a
final pass pushes apart any pair the simulation left overlapping. Pinned nodes
are anchors.

## Serialization

```cpp
const std::string json = req->ToJson(/*pretty*/ true);
SaveToFile("requirements.json", json);

UltraCanvasRequirementDiagram loaded("req", 0, 0, 900, 600);
if (!loaded.FromJson(json)) { /* malformed input */ }
```

The document covers the model (nodes, relations, callouts, categories), the
layout (positions, sizes, pinned flags) and the style selection (palette,
colour source, layout mode, orientation, title, legend). Serializing a
round-tripped diagram reproduces the original document byte for byte. An
explicit `viewport` block in the file wins over the auto-fit that a
`ContainmentTree` layout would otherwise apply.

### Mermaid

```cpp
const std::string text = req->ToMermaid("TB");    // TB | BT | LR | RL

std::string error;
if (!req->FromMermaid(text, &error)) { /* error describes the offending line */ }
```

The full `requirementDiagram` dialect is supported: the six requirement types,
`id` / `text` / `risk` / `verifymethod`, `element { type, docref }` blocks, all
seven relationship keywords in both arrow directions (`a - satisfies -> b` and
`b <- satisfies - a`), `%%` comments and the `direction` directive — which maps
onto the layout orientation on import. Importing replaces the contents and
re-runs the layout. A relationship naming an undeclared requirement creates a
bare box for it, as Mermaid does.

Mermaid references a requirement by its **name**, and carries `id` as a
separate property, so the name becomes the model key and the authored id lands
in `RequirementNode::externalId` — which is then what the `id` compartment row
displays. `ToMermaid()` output round-trips unchanged.

### CSV

```cpp
const std::string csv = req->ToCsv();

RequirementCsvSchema schema;         // column names, all overridable
schema.parentColumn = "parent_id";
schema.delimiter = ';';
std::string error;
req->FromCsv(csvText, schema, &error);
```

Import is header-driven: the first row names the columns, matched against the
schema case-insensitively; only the id column is required. Quoted fields,
embedded delimiters, escaped `""` quotes and newlines inside quotes are all
handled. The `parent` column wires containment after every row exists, so
forward references work.

### ReqIF

```cpp
std::string error;
if (!req->FromReqIf(xmlText, &error)) { /* error says what stopped the parse */ }
```

Reads `SPEC-OBJECTS` and the `SPEC-HIERARCHY` nesting of every `SPECIFICATION`,
mapping the standard `ReqIF.ForeignID` / `ReqIF.Name` / `ReqIF.Text` attribute
definitions onto the model, `Risk` / `Status` / `Source` / `Owner` / `Priority`
where present, and everything else onto custom properties. XHTML values are
read as text with entities decoded; enumeration values resolve through their
`ENUM-VALUE` long names.

This is a deliberate **subset**: requirements, their attributes and the
hierarchy. `SPEC-RELATIONS`, relation groups, datatype definitions, embedded
objects and tool extensions are not read — full ReqIF is a project of its own.
A well-formed file with no `SPEC-OBJECTS` is reported as an error rather than
silently producing an empty diagram.

## Complete example — HybridSUV requirement tree

```cpp
auto req = CreateRequirementDiagram("hsv", 10, 10, 990, 650);
req->SetPalette(RequirementPaletteKind::Pastel);
req->SetColorSource(RequirementColorSource::ByCategory);
req->SetNodeTemplate(RequirementNodeTemplate::Extended());
req->SetNodeWidthRange(130.0, 210.0);

req->AddCategory("Capacity",    Color(252, 244, 200), Color(190, 170, 90));
req->AddCategory("Environment", Color(212, 240, 226), Color(110, 165, 135));

RequirementNode root("UR1", "HybridSUV");
root.detail = RequirementDetailLevel::Collapsed;
req->AddNode(root);

RequirementNode eco("UR1.2", "Eco-Friendliness");
eco.source       = "Marketing";
eco.text         = "The vehicle shall be eco-friendly.";
eco.verifyMethod = RequirementVerifyMethod::Analysis;
eco.risk         = RequirementRisk::High;
eco.category     = "Environment";
req->AddNode(eco);

RequirementNode emissions("UR1.2.1", "Emissions");
emissions.stereotype   = "performanceRequirement";
emissions.source       = "Regulation";
emissions.text         = "The vehicle shall meet Ultra-Low Emissions Vehicle standards.";
emissions.verifyMethod = RequirementVerifyMethod::Test;
emissions.risk         = RequirementRisk::High;
emissions.category     = "Environment";
req->AddNode(emissions);

req->AddNode(RequirementNodeKind::TestCase, "TC1", "Emissions Test");

req->AddContainment("UR1", "UR1.2");
req->AddContainment("UR1.2", "UR1.2.1");
req->AddRelation(RequirementRelationKind::Verify, "TC1", "UR1.2.1");

req->SetLayoutMode(RequirementLayoutMode::ContainmentTree);
req->SetLevelGap(58.0);
req->SetSiblingGap(20.0);
req->SetLegendVisible(true, RequirementPanelPosition::BottomLeft);
req->RunLayout();

window->AddChild(req);
```

## Live demo

`Apps/DemoApp/UltraCanvasRequirementDiagramExamples.cpp` — four tabs:

| Tab | Demonstrates |
|---|---|
| **HybridSUV (compartments)** | Colour-by-category containment tree, full `key="value"` compartments, mixed requirement specialisations, category legend |
| **Specification + callout** | A `«block»` root owning collapsed requirement boxes, with a draggable detail callout |
| **SysML taxonomy + legend** | Generalisation notation over a hidden containment hierarchy, auto-generated legend |
| **Smart Home traceability** | Heterogeneous node kinds, colour-by-kind, dashed `«deriveReqt»` / `«satisfy»` / `«verify»` / `«refine»` / `«trace»` relationships, title banner, relation-kind legend |
| **Coverage + compartments** | SysML compartment notation, coverage badges and legend, the diagram frame, obstacle-aware routing, click-to-trace |
| **Mermaid import + editing** | A diagram parsed from Mermaid text, expand/collapse toggles, create-node / drag-to-connect / rename, Mermaid export |
| **ReqIF + overlays** | A specification imported from ReqIF XML, package grouping region, copy with suspect badge, rationale note, status chips, block ports, minimap + controls, search, matrix export, force layout |

## Notes and limitations

- Node sizes come from real text metrics, which are only available during
  `Render()`. `RunLayout()` therefore completes on the next frame; positions
  read back earlier come from a heuristic estimate.
- Obstacle-aware routing is **opt-in** (`SetObstacleAvoidance(true)`) because
  the A\* search costs more than the direct Z route. It falls back silently to
  the direct route when no path fits in the cell budget
  (`RequirementDiagramStyle::routingMaxCells`).
- Mermaid has no generalisation relationship, so `Generalization` exports as
  `traces`. Mermaid requirement *names* are the model keys; the authored `id:`
  round-trips through `RequirementNode::externalId`.
- ReqIF import covers requirements and hierarchy only (see above);
  `SPEC-RELATIONS` do not become traceability links, so a ReqIF file's
  satisfy/verify relationships must be added separately.
- There is no ReqIF *export* — the format's type system needs authoring
  decisions (which attribute definitions to emit, under which datatypes) that
  belong to the exporting application, not to a diagram widget.
- Force-directed layout is O(n²) per iteration; it is meant for the tens of
  boxes a trace web has, not for thousands.
- The box-and-typed-relation machinery is still private to this element. The
  proposal's open question 1 recommends extracting a shared `UltraCanvasUmlCore`
  once a second consumer exists (class / ER / block-definition diagrams are
  still `NotImplemented` placeholders), so that remains deliberately deferred.

## Related Documentation

- [`UltraCanvasRequirementDiagramProposal.md`](UltraCanvasRequirementDiagramProposal.md) — research and the full feature roadmap
- [`UltraCanvasNodeDiagramExamples.md`](UltraCanvasNodeDiagramExamples.md) — general graph/flow editor
- [`UltraCanvasFlowChartExamples.md`](UltraCanvasFlowChartExamples.md) — flow charts with A\* orthogonal routing
- [`UltraCanvasBlockDiagramExamples.md`](UltraCanvasBlockDiagramExamples.md) — block diagrams
- [`UltraCanvasJSON.md`](UltraCanvasJSON.md) — the JSON module used by `ToJson`/`FromJson`
- `Tests/RequirementModelTests.cpp` — headless unit tests for the model layer
