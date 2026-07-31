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
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.0

Research write-up and the full phased feature list:
[`UltraCanvasRequirementDiagramProposal.md`](UltraCanvasRequirementDiagramProposal.md).
This document covers the shipped phase-1 API.

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasRequirementDiagram
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
- **JSON save/load** covering model, layout and style

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
| `Actor` | `«actor»` | Rounded rectangle |
| `Package` | `«package»` | Rectangle |
| `Rationale` / `Problem` / `Note` | `«rationale»` / … | Rectangle |

Set `RequirementNode::shape` to override; `RequirementNodeShape::Folder`,
`FoldedNote` and `StickFigure` are reserved for phase 2 and currently fall back
to a rectangle.

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

## Notes and limitations

- Node sizes come from real text metrics, which are only available during
  `Render()`. `RunLayout()` therefore completes on the next frame; positions
  read back earlier come from a heuristic estimate.
- Orthogonal routing does not yet avoid passing through intervening boxes;
  obstacle-aware A\* routing is a phase-2 item.
- Only `ContainmentTree` and `Manual` layouts ship in phase 1. Layered
  (Sugiyama) layout for non-tree topologies is phase 2 — until then, position
  trace webs manually as the Smart Home demo does.
- Compartment notation for derived elements (`satisfiedBy`, `verifiedBy`, …),
  coverage analysis, expand/collapse, trace highlighting and Mermaid/CSV/ReqIF
  interchange are phase-2 items; see the proposal for the full list.

## Related Documentation

- [`UltraCanvasRequirementDiagramProposal.md`](UltraCanvasRequirementDiagramProposal.md) — research and the full feature roadmap
- [`UltraCanvasNodeDiagramExamples.md`](UltraCanvasNodeDiagramExamples.md) — general graph/flow editor
- [`UltraCanvasFlowChartExamples.md`](UltraCanvasFlowChartExamples.md) — flow charts with A\* orthogonal routing
- [`UltraCanvasBlockDiagramExamples.md`](UltraCanvasBlockDiagramExamples.md) — block diagrams
- [`UltraCanvasJSON.md`](UltraCanvasJSON.md) — the JSON module used by `ToJson`/`FromJson`
