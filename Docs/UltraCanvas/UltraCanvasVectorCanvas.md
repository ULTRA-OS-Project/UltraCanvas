# UltraCanvas Vector Editing: VectorCanvas, VectorEdit, BezierPath

## Overview

The **vector-editing layer** is what a vector drawing editor needs on top of
the vector document model: a selection, undo / redo, geometric hit testing,
the operations an editor's commands are made of, a node model for editing
paths, and an element that displays the document with rulers, guides, grid,
snapping and selection handles and hands pointer events to the active tool.
It is the vector twin of the raster-editing layer in
[UltraCanvasPaintSurface](UltraCanvasPaintSurface.md) and follows the same
split: the model and its editing companions know nothing about windows,
one element shows them, and an application owns the tools. The
investigation behind it is
[`Docs/Research/ArtCreatorVectorCanvasProposal.md`](../Research/ArtCreatorVectorCanvasProposal.md).

| Class | What it is | Files |
|---|---|---|
| `VectorStorage::VectorDocument` | The drawing: layers → groups → elements with styles, transforms and paths; read and written by every vector format (see [UltraCanvasVectorConverters](UltraCanvasVectorConverters.md)) | `include/DataFormats/UltraCanvasVectorStorage.h` |
| `VectorRenderer` | Draws a document, layer or element into any `IRenderContext`; `BuildVectorElementOutline` puts one element's outline on a context | `include/DataFormats/UltraCanvasVectorRenderer.h` |
| `VectorEdit::VectorSelection` | Ordered selected elements, their document-space bounds, listeners, re-binding by Id after an undo | `include/DataFormats/UltraCanvasVectorEdit.h`, `core/DataFormats/UltraCanvasVectorEdit.cpp` |
| `VectorEdit::VectorHistory` | Labelled undo / redo by document snapshots, coalescing, a memory budget | same |
| `VectorEdit::VectorHitTester` | Geometric fill / stroke hit testing with a tolerance, through every ancestor transform; rectangle queries | same |
| `VectorEdit` operations | Transform about a pivot, bake a transform, z-order, group / ungroup / reparent, delete, duplicate, align, distribute, convert to path | same |
| `UltraCanvasBezierPath` | Anchors that own their handles: the editing model of a path, with split / node types / hit tests / polyline fitting and a lossless round trip to `PathData` | `include/UltraCanvasBezierPath.h`, `core/UltraCanvasBezierPath.cpp` |
| `UltraCanvasVectorCanvas` | The element: pasteboard, page, grid, rulers, guides, snapping, selection handles, tool hooks in document coordinates | `include/UltraCanvasVectorCanvas.h`, `core/UltraCanvasVectorCanvas.cpp` |
| `UltraCanvasGradientEditor` | The stops of a gradient on a strip | [UltraCanvasGradientEditor](UltraCanvasGradientEditor.md) |

**Version**: 1.0.0
**Last Modified**: 2026-09-15
**Author**: UltraCanvas Framework

Everything here is in the core library. The Vector plugin
(`ULTRACANVAS_PLUGIN_VECTOR`) adds the file formats; without it a document
can still be created, edited, drawn and rasterized.

## The document and its Ids

Coordinates are points. An element's `Transform` maps its own space into
its parent's; `GetGlobalTransform()` composes every ancestor. Selection and
history find elements again **by Id** after an undo (the objects are
replaced), so an editor gives every element an Id:

```cpp
#include "DataFormats/UltraCanvasVectorEdit.h"
using namespace UltraCanvas;
using namespace UltraCanvas::VectorEdit;

auto doc = std::make_shared<VectorStorage::VectorDocument>();
doc->Size = {595, 842};                         // A4 in points
auto layer = doc->AddLayer("Layer 1");
auto rect = std::make_shared<VectorStorage::VectorRect>();
rect->Bounds = {100, 100, 200, 120};
rect->Style.Fill = Color(220, 60, 40, 255);
rect->Id = GenerateId();                        // or EnsureIds(*doc) after loading a file
layer->AddChild(rect);
```

Tree helpers: `ParentOf`, `IndexInParent`, `LayerOf`, `TopLevelOf` (the
layer's child that contains an element — what a click selects unless the
user goes "inside" a group), `ParentToDocument` (the matrix from an
element's parent space to the document), `DocumentBounds` (an element's
box in document space), `AllElements`, `Detach`.

## Selection

```cpp
VectorSelection selection;
selection.Set(rect);                // or Set({a, b}), Add, Remove, Toggle, Clear
selection.Bounds();                 // union of DocumentBounds, document units
selection.AddListener([&] { canvas->Refresh(); });
```

`Rebind(doc)` re-resolves the selected Ids against the document — the
history's `onChanged` is the place to call it.

## History

`VectorHistory` snapshots the whole document on `BeginEdit(label)` and again
on `EndEdit()`, keeps the pair, and restores a snapshot into the *live*
document object on `Undo()` / `Redo()` — the `shared_ptr` the canvas holds
stays valid, the elements inside are new. It is the simple, always-correct
choice; a per-subtree scheme can replace it behind the same API when
documents grow large enough to need one.

```cpp
VectorHistory history(doc);
history.onChanged = [&] { selection.Rebind(*doc); canvas->Refresh(); };

history.Record("Move", [&] { TranslateElements(selection.Elements(), 10, 0); });

// A drag: one entry from press to release.
history.BeginEdit("Scale");
... ScaleElements(...) on every drag event ...
history.EndEdit();

// Repeated nudges collapse into one step.
history.Record("Nudge", [&] { TranslateElements(selection.Elements(), 1, 0); }, /*coalesce*/ true);

history.Undo();  history.Redo();  history.UndoLabel();
history.SetMemoryLimit(256 * 1024 * 1024);
```

`CancelEdit()` restores the state at `BeginEdit` (Escape during a drag).

## Hit testing

```cpp
VectorHitTester tester;
auto hit = tester.HitTest(*doc, docPoint, /*tolerance in doc units*/ 2.0);
if (hit) { hit->element; hit->topLevel; hit->layer; hit->onFill; hit->onStroke; }
auto inside = tester.ElementsIn(*doc, marqueeRect, /*fullyInside*/ true);
```

Fills are tested with the element's fill rule, strokes with the element's
width grown by the tolerance (so a hairline is still clickable), text and
images by their boxes; locked and hidden layers are skipped. The tester
keeps a small geometry-only render context, so it works with no window,
and the canvas wraps it with the tolerance given in screen pixels.

## Operations

All edit the model in place; wrap them in the history.

```cpp
TranslateElements(els, dx, dy);
ScaleElements(els, sx, sy, pivot);            // pivot in document units
RotateElements(els, radians, pivot);
SkewElements(els, radX, radY, pivot);
TransformElements(els, Matrix3x3);            // any document-space matrix
BakeTransform(el);                            // write the transform into the geometry

ReorderElements(els, ZOrderMove::ToFront);    // ToFront / Forward / Backward / ToBack
auto group = GroupElements(els);              // into the topmost member's parent
auto freed = UngroupElements({group});        // placement preserved both ways
ReparentElement(el, otherGroup, index);

DeleteElements(els);
auto copies = DuplicateElements(els, 10, 10); // above the originals, fresh Ids
AlignElements(els, AlignMode::Left);          // to the selection, or a reference rect (the page)
DistributeElements(els, DistributeMode::HorizontalCenters);
auto path = ConvertToPath(el);                // rect / circle / ellipse / line / polygon → path, in place
```

A document-space matrix `M` lands on an element as `T' = P⁻¹·M·P·T`, `P`
being its parent-to-document map, so children of transformed groups move
where the user expects.

## The Bézier node model

`VectorStorage::PathData` is a command stream (relative forms, H / V,
smooth shorthands, arcs) — right for files, wrong to edit. A shape editor
converts a path to `UltraCanvasBezierPath` on entry and back on every edit:

```cpp
#include "UltraCanvasBezierPath.h"

auto bezier = UltraCanvasBezierPath::FromPathData(path->Path);   // all command kinds accepted
auto& sp = bezier.subpaths[0];
int n = sp.InsertNodeAt(/*segment*/ 1, /*t*/ 0.5);   // de Casteljau: the shape is unchanged
sp.SetNodeType(n, BezierNodeType::Symmetric);        // Corner / Smooth / Symmetric rules
sp.MoveHandle(n, /*outgoing*/ true, p);              // the other handle follows the rule
sp.DragSegment(1, 0.3, delta);                       // "drag the line" gesture
auto hit = bezier.HitTestOutline(p, tolerance);      // subpath, segment, t, distance
auto node = bezier.HitTestNode(p, tolerance);
path->Path = bezier.ToPathData();                    // absolute M / L / C / Z
path->Path.InvalidateCache();
```

Every segment is a line or a cubic. `FromPolyline(points, closed,
tolerance)` turns a freehand stroke into a smooth path (Douglas-Peucker
simplification, Catmull-Rom tangents); `BuildPath(ctx)` emits the geometry
for a preview; `FromSVGPathData` / `ToSVGPathData` are the string forms.
The design is the one in
[UltraCanvasBezierEditorProposal](UltraCanvasBezierEditorProposal.md).

## The canvas element

```cpp
#include "UltraCanvasVectorCanvas.h"

auto canvas = CreateVectorCanvas("canvas");
canvas->SetDocument(doc);
canvas->SetSelection(selectionPtr);            // shared with the tools and panels
canvas->SetShowRulers(true);
canvas->SetRulerUnit(72.0 / 25.4, "mm");
VectorGridSpec grid; grid.visible = true; grid.spacing = 10; grid.subdivisions = 2;
canvas->SetGrid(grid);
VectorSnapOptions snap; snap.toGrid = true; snap.toGuides = true; snap.toObjects = true;
canvas->SetSnapOptions(snap);

canvas->onToolPress   = [&](const VectorPointerEvent& e) { tool->OnPress(e); };
canvas->onToolDrag    = [&](const VectorPointerEvent& e) { tool->OnDrag(e); };
canvas->onToolRelease = [&](const VectorPointerEvent& e) { tool->OnRelease(e); };
canvas->onToolKey     = [&](const UCEvent& k) { return tool->OnKey(k); };
canvas->onDrawOverlay = [&](IRenderContext* ctx, const VectorViewTransform& v) { tool->DrawOverlay(ctx, v); };
canvas->onViewChanged = [&] { statusBar->SetZoom(canvas->GetZoom()); };
```

What the element does by itself:

- **View.** Wheel zooms about the cursor (shift + wheel pans), space or the
  middle button pans, `+` / `-`, Ctrl+0 (page), Ctrl+1 (100 %).
  `SetZoom` / `SetZoomAt` / `ZoomStep` / `ZoomToPage` / `ZoomToDrawing` /
  `ZoomToSelection` / `ZoomToRect` / `CenterOn` / `PanBy`; `DocToView` / `ViewToDoc` /
  `PixelsToDoc`; `VisibleDocRect`, `CanvasArea`.
- **Page** on a pasteboard with a shadow; **grid** with subdivisions that
  hide when too dense; **rulers** in any unit with the pointer marked;
  **guides** pulled out of the rulers, alt-dragged to move, dropped back
  on a ruler to delete (`SetGuidesDraggable`, `AddGuide`, `SetGuides`,
  `onGuidesChanged`).
- **Snapping** — `Snap(docPoint, &result)` to guides, page edges and
  centre, other objects' box edges and centres, and the grid, within
  `radiusPixels`; guides beat the page, the page beats objects, objects
  beat the grid at equal distance. Every pointer event carries both the
  raw and the snapped point.
- **Selection handles** — eight scale handles, or in `Rotate` mode round
  rotate handles at the corners, skew diamonds at the edges and a movable
  rotation centre (`SetHandleMode`, `SetRotationCenter`); `HitTestHandle`
  tells a selector tool which one is under the pointer, `Body` when the
  pointer is inside the bounds, `NoHandle` otherwise.
- **Hit testing** — `HitTest(docPoint, tolerancePixels)` and
  `ElementsIn(rect, fullyInside)` over the document.
- **Tool hooks** — `onToolPress / Drag / Release / Hover / DoubleClick`
  with a `VectorPointerEvent` (`doc`, `snapped`, `snap`, `view`, button,
  modifiers, pressure, `insidePage`), `onToolKey`, `onDrawOverlay` in view
  coordinates after the handles and before the rulers, `onViewChanged`,
  `onFilesDropped`; `SetToolCursor`, `SetPanMode`.
- **Invalidation** — `InvalidateDoc(rect)` after an edit, or `Refresh()`.

The element never edits the document and owns no tool: a selector, a pen,
a shape tool are application classes that receive these events and call
`VectorEdit` inside a `VectorHistory` edit. It is registered in the element
catalogue ([UltraCanvasUIElements](UltraCanvasUIElements.md)) alongside
`UltraCanvasPaintSurface`.

## Tests

`Tests/VectorEditTest.cpp` (CTest `VectorEditTest`) covers the node model,
selection, history, hit testing, every operation, the canvas's view maths,
snapping and handles, and the gradient editor — all without a window.
`Tests/VectorModelTest.cpp` covers the model and the renderer.
