# UltraCanvasMindMap Documentation

## Overview

**UltraCanvasMindMap** is an interactive mind map element: a rooted topic tree on
a pan/zoom canvas, with the central topic in the middle and branches radiating
outward. It covers both the *working-tool* case (deep editable maps with
collapse, drag-to-reparent and undo) and the *presentation* case (themes, node
shapes, images, icons and numbering).

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasMindMap.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.1.0

The topic model and the layout engine live separately in
`include/Plugins/Diagrams/UltraCanvasMindMapLayout.h`, and the interchange
formats in `include/Plugins/Diagrams/UltraCanvasMindMapIO.h`. Neither depends on
the render context, so both can be used — and tested — headlessly.
Pan/zoom, the minimap and the controls overlay come from the shared
[`UltraCanvasDiagramViewport`](UltraCanvasDiagramViewport.md).

```
UltraCanvasUIElement
    └── UltraCanvasMindMap        (element: render, events, editing)
            ├── MindMapModel                (topic tree)
            ├── UltraCanvasMindMapLayout    (pure layout)
            └── UltraCanvasMindMapIO        (interchange formats)
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasMindMap.h"
```

## Quick start

```cpp
auto map = CreateMindMap("ProjectMap", 10, 10, 900, 640);
map->SetTheme(MindMapTheme::Colorful);
map->SetStructure(MindMapStructure::Balanced);
map->SetControlsVisible(true);

std::string root = map->SetCentralTopic("Product launch");
std::string research = map->AddTopic(root, "Research");
map->AddTopic(research, "User interviews");
map->AddTopic(research, "Competitor scan");

map->FitView();
window->AddChild(map);
```

Or build a whole map from an indented outline in one call:

```cpp
map->BuildFromOutline({
    {0, "Product launch"},
    {1, "Research"},
    {2, "User interviews"},
    {2, "Competitor scan"},
    {1, "Design"},
    {2, "Wireframes"},
});
```

`BuildFromOutline` clamps an over-indented line to one level deeper than the
previous one, so a hand-written outline can never produce a broken tree.

---

## Structures

```cpp
map->SetStructure(MindMapStructure::Balanced);
```

| Structure | Shape |
|---|---|
| `Balanced` | Root centred, main topics split left and right (the default) |
| `LogicRight` / `LogicLeft` | The whole tree grows to one side |
| `OrgChartDown` / `OrgChartUp` | Top-down / bottom-up hierarchy |
| `Radial` | Branches distributed over a full circle, one ring per generation |
| `Manual` | Authored positions honoured; anything unset is auto-placed |

In `Balanced`, which side an `Auto` main topic lands on is decided by the balance
policy:

```cpp
map->SetBalancePolicy(MindMapBalancePolicy::MinimiseSubtreeWeight);
```

- `AlternateByIndex` — right, left, right, left (the default)
- `SplitInHalf` — first half right, second half left
- `MinimiseSubtreeWeight` — each branch goes to whichever side is currently
  lighter, measured in leaf count

An explicit side always wins over the policy:

```cpp
map->SetTopicSide(topicId, MindMapTopicSide::Left);
```

Side is **inherited by the whole subtree**: everything under a left-side main
topic keeps growing leftward. So is the branch index, which is what the colour
cascade keys off.

### Spacing

```cpp
map->SetSpacing(/*siblingGap*/ 12.0, /*levelGap*/ 46.0, /*rootGap*/ 60.0);
```

Sibling subtrees are packed by bottom-up extent accumulation, so **boxes never
overlap at any depth**, whatever their sizes. `Tests/MindMapLayoutTest.cpp`
verifies this over a 364-topic ragged tree in three structures.

### Alignment (the infographic look)

```cpp
MindMapLayoutOptions options = map->GetLayoutOptions();
options.alignLevelColumns = true;   // same depth => same distance from centre
options.alignSideRows = true;       // Nth left topic shares a row with Nth right
map->SetLayoutOptions(options);
```

Both are off by default. `alignSideRows` applies to the `Balanced` and `Manual`
structures only — the other structures have no two sides to pair up.

### Manual placement

```cpp
MindMapTopic* topic = map->GetTopic(id);
topic->hasManualPosition = true;
topic->manualX = 640.0;
topic->manualY = -275.0;
map->SetStructure(MindMapStructure::Manual);
```

Authored positions are stored on the topic and ignored by every other
structure, so a map can be switched back and forth without losing them. In
`Manual`, topics without an authored position keep the position the base pass
computed.

---

## Styling

### Style by level

Mind maps are styled by *level*, not per node — that is how all the reference
designs actually work. Level 0 is the central topic, 1 the main topics, and so
on; the deepest configured level applies to everything beyond it.

```cpp
MindMapTopicStyle leaf = map->GetLevelStyle(2);
leaf.shape = MindMapNodeShape::Underline;   // text with a rule beneath
leaf.maxWidth = 300.0;
map->SetLevelStyle(2, leaf);
```

### Branch colour cascade

Each main topic takes the next hue from the palette, and its whole subtree
inherits it. Text colour is chosen automatically for contrast against the fill.

```cpp
map->SetBranchPalette({
    Color(233, 86, 86), Color(247, 168, 40), Color(76, 175, 92),
    Color(45, 156, 219), Color(155, 89, 182), Color(26, 188, 156),
});
map->Style().depthLightenStep = 0.18;   // lighten 18% per level below the branch
```

`depthLightenStep` produces the cluster look where a saturated main circle is
surrounded by paler descendants.

### Per-topic override

```cpp
MindMapTopicStyle callout = map->GetLevelStyle(1);
callout.fillColor = Color(232, 245, 233);
map->SetTopicStyle(topicId, callout);   // wins over level and branch styles
map->ClearTopicStyle(topicId);          // back to the cascade
```

### Shapes

`RoundedRect`, `Rect`, `Pill`, `Circle`, `Ellipse`, `Diamond`, `Hexagon`,
`Cloud`, `Bang`, `Underline` and `NoShape` (text only).

> `NoShape` is spelled that way rather than `None` because X11's `Xlib.h`
> defines `None` as a macro. The same applies to
> `MindMapEndDecoration::NoDecoration`.

`outlineOnly` gives the transparent-fill, coloured-stroke, coloured-text look.

### Shadows, dimming and a background layer

```cpp
map->Style().showShadows = true;              // offset silhouette behind each node
map->Style().dimUnrelatedOnHover = true;      // fade branches other than the hovered one
map->Style().dimOpacity = 0.28;

// Drawn in WORLD space beneath the map, before the grid.
map->Style().backgroundRenderer = [](IRenderContext* ctx, const Rect2Dd& visible) {
    // visible is the world region currently on screen
};
```

### Themes

```cpp
map->SetTheme(MindMapTheme::Professional);
```

`Default`, `Professional`, `Colorful`, `Pastel`, `Dark`, `HandDrawn`. A theme
sets the palette, the background, the connector style and the four level styles
in one call — so apply it **before** any `SetLevelStyle` customisation, since it
replaces them.

---

## Connectors

```cpp
map->SetConnectorStyle(MindMapConnectorStyle::TaperedBranch);
```

| Style | Look |
|---|---|
| `Straight` | Direct line |
| `Curve` | Cubic bezier, the classic mind map S-curve (default) |
| `Elbow` | Orthogonal, sharp corners |
| `RoundedElbow` | Orthogonal with rounded corners |
| `TaperedBranch` | Organic branch, thick at the parent and thin at the child |

Connectors always terminate on the node's **perimeter**, computed per shape
(ellipse, diamond and rectangle each have their own intersection), so a line
never runs into the middle of a box.

```cpp
map->Style().connectorColorMode = MindMapConnectorColorMode::InheritFromBranch;
map->Style().endDecoration = MindMapEndDecoration::Dot;   // or Arrow, Square
map->Style().taperStartWidth = 7.0;
map->Style().taperEndWidth = 1.5;
```

### Connector badges

A circular badge on the connector carrying the child's icon, or its ordinal
when there is no icon:

```cpp
map->Style().showConnectorBadges = true;
map->Style().connectorBadgeT = 0.55;   // 0 at the parent, 1 at the child
map->Style().connectorBadgeRadius = 9.0;
```

---

## Node content

```cpp
map->SetTopicIcon(topicId, "media/icons/light 001.jpg");   // inline, before the label
map->SetTopicImage(topicId, "media/images/brain.png");     // image as the node body
map->SetTopicNote(topicId, "Longer note text");            // corner triangle indicator
map->SetTopicLink(topicId, "https://example.com");         // clickable ring indicator
map->SetTopicMarkers(topicId, {"media/icons/check.png"});  // badge row above the node
map->SetTopicWeight(topicId, 3.0);                          // with sizeFromWeight
```

Notes render as a small filled corner triangle, links as a clickable ring — a
click fires `onTopicLinkActivated(id, link)` — and markers as a row of small
icons above the node. Each can be turned off:

```cpp
map->Style().showNoteIndicator = false;
map->Style().showLinkIndicator = false;
map->Style().showMarkers = false;
```

Icons and images are **file paths**, matching the framework convention
(`UltraCanvasTreeView::TreeNodeIcon`, `UltraCanvasButton::SetIcon`,
`MenuItemData::Action`).

### Numbering

```cpp
map->SetNumbering(MindMapNumbering::Badge);   // or Prefix, Off
```

`Prefix` renders `1.2 Topic text`; `Badge` draws a filled circle in the branch
hue carrying the ordinal, just outside the node.

---

## Relationships

Non-hierarchical links between any two topics, drawn as dashed bowed arcs so
they read separately from the branch strokes:

```cpp
map->AddRelationship("connection_1", fromTopicId, toTopicId, "Connection");

MindMapRelationship rel("r2", a, b);
rel.color = Color(200, 80, 120);
rel.dashed = false;
rel.arrowAtEnd = true;
map->AddRelationship(rel);
```

Removing either endpoint removes the relationship with it.

---

## Collapse and navigation

```cpp
map->SetCollapsed(topicId, true);
map->ToggleCollapsed(topicId);
map->ExpandAll();
map->CollapseAll();        // collapses the main topics, never the centre
map->ExpandToLevel(2);

map->CenterOnTopic(topicId);
map->FitView(50.0);
map->RevealTopic(topicId);            // expands ancestors, then centres
auto hits = map->FindTopics("design"); // case-insensitive substring
```

A collapsed topic keeps its own box but its descendants are not laid out at all,
so collapsing genuinely frees the space.

---

## Editing

```cpp
map->SetEditable(true);
map->BeginEditTopic(topicId);
std::string childId = map->CreateChild(parentId);     // creates, selects, edits
std::string siblingId = map->CreateSibling(topicId);
map->DeleteSelected();
map->Undo();
map->Redo();
```

### Keyboard

| Key | Action |
|---|---|
| `Enter` | New sibling (a child, when the central topic is selected) |
| `Tab` | New child |
| `F2` | Rename the selected topic |
| `Delete` / `Backspace` | Delete the selection (never the central topic) |
| `Space` | Collapse / expand |
| Arrow keys | Move the selection geometrically |
| `Ctrl+A` | Select all |
| `Ctrl+Z` / `Ctrl+Shift+Z`, `Ctrl+Y` | Undo / redo |
| `Escape` | Deselect, or cancel an in-progress edit |

### Mouse

Click selects, Shift+click adds to the selection, double-click renames (or
collapses when the map is read-only), dragging a topic onto another reparents it
with a drop indicator, dragging empty canvas pans, and the wheel zooms at the
cursor. Double-clicking empty canvas fits the view.

In-place editing embeds an `UltraCanvasTextInput` positioned over the topic in
screen space, with its font scaled by the current zoom. While an edit is active
the editor receives keyboard events first, so `Tab` and `Enter` go to the text
field rather than to the authoring shortcuts.

### Clipboard

```cpp
map->CopySelection();                  // topmost selected subtrees
map->CutSelection();
map->PasteInto(targetParentId);        // fresh ids, so it can be pasted repeatedly
map->PasteOutlineInto(parentId, "Root\n  Child\n");
map->HasClipboardContent();
```

Copy only takes the *topmost* selected topics — a descendant already travels
with its ancestor, so copying both would duplicate it on paste. `Ctrl+C`,
`Ctrl+X` and `Ctrl+V` are wired to these.

Undo snapshots the whole map as JSON, so every structural and style mutation is
covered by one mechanism. The default limit is 100 entries
(`SetUndoLimit`).

---

## Exchange

```cpp
std::string json = map->ToJson();
map->FromJson(json);

std::string markdown = map->ToMarkdown();
map->FromMarkdown(markdown);

std::string plain = map->ToOutlineText();   // indented reading order
```

JSON round-trips the model, per-topic style overrides, relationships, layout
options and the viewport, using `UltraCanvasJSON`. Markdown import accepts both
headings (`#`, `##`, …) and nested list items (two spaces per level, the markmap
convention).

### Interchange formats

```cpp
map->FromMermaid(source);      map->ToMermaid();
map->FromFreeMind(mmXml);      map->ToFreeMind();      // FreeMind / Freeplane .mm
map->FromOpml(opmlXml);        map->ToOpml("Title");
map->FromIndentedText(text);   map->FromCsv(csv);      map->ToCsv();
map->FromXMindFile("plan.xmind");
map->ImportAuto(anyText);      // sniffs the format and dispatches
map->ExportSvg("map.svg", 2.0);
```

Every importer **replaces the whole map** and is undoable. Import runs into a
scratch model and only adopts it on success, so a malformed file can never
half-destroy the map you have open.

The underlying `UltraCanvasMindMapIO` works on a bare `MindMapModel` if you need
conversion without an element:

```cpp
MindMapModel model;
MindMapImportResult result = UltraCanvasMindMapIO::FromFreeMind(model, xml);
if (!result) { /* result.error explains why */ }
```

Notes on fidelity:

- **Mermaid** — indentation is ranked by distinct width, so 2-space, 4-space and
  tab sources import identically. Shape delimiters survive as style overrides
  and `::icon()` attaches to the preceding node. An identifier before the
  delimiter (`root((Text))`) is accepted, as Mermaid allows.
- **FreeMind** — `POSITION`, `FOLDED`, `LINK` and `BUILTIN` icons round-trip.
- **OPML** — a body with several top-level outlines keeps them all: the first
  becomes the centre, the rest its children.
- **CSV** — rows may appear in any order; parents are resolved after the whole
  file is read. Needs an id column and a parent column; the text column defaults
  to `text`/`label`/`title`/`name`.
- **XMind** — reads the modern `content.json` container. A legacy `content.xml`
  archive is reported as unsupported rather than failing obscurely.

No new third-party dependency is involved: tinyxml2, `UltraCanvasJSON` and
`UCZipPackageReader` were already in the framework.

---

## Callbacks

```cpp
map->onTopicClick        = [](const std::string& id) { /* ... */ };
map->onTopicDoubleClick  = [](const std::string& id) { /* ... */ };
map->onTopicTextChanged  = [](const std::string& id, const std::string& previous) { /* ... */ };
map->onTopicAdded        = [](const std::string& id) { /* ... */ };
map->onTopicRemoved      = [](const std::string& id) { /* ... */ };
map->onTopicMoved        = [](const std::string& id, const std::string& newParentId) { /* ... */ };
map->onCollapseChanged   = [](const std::string& id, bool collapsed) { /* ... */ };
map->onSelectionChanged  = [](const std::vector<std::string>& ids) { /* ... */ };
map->onViewportChanged   = [](double zoom, double panX, double panY) { /* ... */ };
map->onTopicRightClick   = [](const std::string& id, double worldX, double worldY) { /* ... */ };
map->onCanvasRightClick  = [](double worldX, double worldY) { /* ... */ };
```

---

## Working with the model directly

`MindMapModel` is usable on its own, with no element and no render context —
useful for import/export tools and tests:

```cpp
MindMapModel model;
std::string root = model.SetCentralTopic("Root");
std::string a = model.AddTopic(root, "A");

model.MoveTopic(a, otherId);          // false on a cycle
model.GetPath(id);                     // root-first, inclusive
model.GetSubtreeIds(id);               // pre-order, inclusive
model.CountDescendants(id);
model.IsHiddenByCollapse(id);
model.AssignOrdinals();

MindMapLayoutResult result;
UltraCanvasMindMapLayout::ComputeLayout(model, MindMapLayoutOptions(),
                                        nullptr, nullptr, result);
```

Passing `nullptr` for the measure function falls back to a built-in text-size
estimator, so layout works without a render context.

---

## Performance notes

Layout is **lazy**: mutations mark it dirty and `Render()` recomputes at most
once per frame. Real text metrics only exist during `Render()`, which is why the
layout pass runs from there rather than from the mutating setters.

---

## Demo

`Apps/DemoApp/UltraCanvasMindMapExamples.cpp` — six tabs: an editable map, a
logic chart with ordinal badges, a radial map, cross-branch relationships, a
presentation layout (aligned columns and rows, connector badges, shadows,
background layer, note/link/marker indicators), and a gallery for switching
theme, structure and connector style at runtime.
