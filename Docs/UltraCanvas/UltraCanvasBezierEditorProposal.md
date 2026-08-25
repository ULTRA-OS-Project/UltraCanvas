# UltraCanvas Bézier Path Editor — Investigation & Proposal

Status: **proposal — nothing implemented yet.**
Author: UltraCanvas Framework
Last Modified: 2026-08-25

The tone curve editor shipped with the media viewer's *Curves* dialog
([`UltraCanvasCurveEditor.md`](UltraCanvasCurveEditor.md)) raised the obvious
question: the framework should offer programmers a **general Bézier path
editing element** — anchor points, tangent handles, closed shapes — not just a
one-dimensional tone curve. This document is the investigation of what exists,
where such an element belongs, and what it should look like.

---

## 1. Scope and method

What was examined:

- `UltraCanvas/Plugins/Vector/` — the internal vector document model
  (`VectorStorage`), its renderer, its display element and the XAR converter.
- `UltraCanvas/Plugins/Vectorizer/` — raster → SVG tracing (VTracer).
- `UltraCanvas/Plugins/SVG/`, `libspecific/Cairo/SvgDocumentCairo` — how SVG is
  rendered today.
- `IRenderContext` (`UltraCanvasRenderContext.h`) — the path-building API.
- `UltraCanvasDiagramViewport` — the shared pan / zoom / snap-grid / minimap
  component used by the node, mind-map and compositor diagrams.
- `UltraCanvasNodeDiagram` — the framework's existing "drag things on a canvas"
  interaction model (selection, marquee, drag-to-connect).
- `UltraCanvasCurveEditor` / `UltraCanvasToneCurve` — the precedent this
  proposal is modelled on (model split from element).

Everything claimed in §2 was checked against the code, and the build claims
were verified by actually building.

---

## 2. What exists today

### 2.1 There is a vector document model — and it does not compile

`UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h` defines a full SVG-like
document model: `VectorDocument` → `VectorLayer` → `VectorGroup` → elements
(`VectorPath`, `VectorText`, `VectorRectangle`, gradients, filters, markers…).
`VectorPath` holds `PathData` — an SVG command list
(`MoveTo / LineTo / CurveTo / QuadraticTo / ArcTo / ClosePath`, absolute or
relative) — with `GetBounds()`, `GetLength()`, `GetPointAtLength()` and
`Flatten(tolerance)`.

On paper that is exactly the storage a path editor would write into. In
practice:

```
cmake -DULTRACANVAS_PLUGIN_VECTOR=ON …
cmake --build . --target UltraCanvasVectorlugin
→ 233 compile errors
```

The module has rotted against the current core API. A sample of the failures:

| Failure | Count |
|---|---|
| `XARConverter::Impl has not been declared` (converter's pimpl is gone) | 46 |
| `importState` / `impl` / `currentOptions` not declared in this scope | 37 |
| `IRenderContext` has no member `SetFillGradient` (renamed to `SetFillPaint`) | 2 |
| `DrawText(const std::string&, double&, double&)` — signature changed | 1 |
| `PathData` has no member `Commands` (field is `commands`) | 10 |
| `GradientStop` has no member `StopColor` | 6 |
| `min/max(float&, double&)` — float/double drift in the geometry code | ~12 |
| … | rest |

It is `OFF` by default (`ULTRACANVAS_PLUGIN_VECTOR`), so nothing in the tree
builds it and the rot went unnoticed. **Consequence for this proposal: a new
editing element must not be built on `VectorStorage::PathData`**, or it starts
life depending on a module that does not compile.

Two side findings while checking this:

- `UltraCanvas/CMakeLists.txt:43` reads
  `option(ULTRACANVAS_PLUGIN_VECTOR, "Build Vector plugin" OFF)` — note the
  stray comma, which declares an option literally named
  `ULTRACANVAS_PLUGIN_VECTOR,`. It is harmless only because the top-level
  `CMakeLists.txt:107` declares the real one; still a one-character fix.
- The plugin's CMake names its output `UltraCanvasVectorlugin`
  (missing "P").

### 2.2 The framework can *render* Bézier paths, and nothing else

`IRenderContext` already has everything a path editor needs to draw:
`MoveTo`, `LineTo`, `BezierCurveTo`, `QuadraticCurveTo`, `ArcTo`, `ClosePath`,
`Fill`, `Stroke`, `FillPathPreserve`, `StrokePathPreserve`, `GetPathExtents`,
`SetLineDash`, plus `DrawLinePath` / `FillLinePath` for flattened polylines.

What it does *not* have: any hit-testing against the current path (no
`IsPointInFill` / `IsPointInStroke`). A path editor must flatten and measure
distances itself — which it needs anyway for handle picking.

### 2.3 SVG is consumed, never produced

- SVG **rendering** goes through librsvg (`SvgDocumentCairo`) — a black box
  that yields pixels, not structure.
- The **Vectorizer** plugin (ON by default, VTracer-backed) returns a complete
  `<svg>…</svg>` **string** with cubic-spline paths
  (`VectorizerPathMode::Spline`).
- The XAR and CDR importers parse their own binary formats into
  `VectorStorage`.

So: the framework can turn a bitmap into Bézier paths, and can draw Bézier
paths, but there is **no code anywhere that parses or writes an SVG `d`
string** into an editable structure. That missing pair is the practical gap
between "we traced this logo" and "let me fix that one corner".

### 2.4 There is already a canvas viewport component to reuse

`UltraCanvasDiagramViewport` (compiled into the core library, documented in
[`UltraCanvasDiagramViewport.md`](UltraCanvasDiagramViewport.md)) owns:
zoom + pan with clamping, `ScreenToWorld` / `WorldToScreen`, zoom-at-cursor,
`FitView` / `CenterOn`, a **snap grid** (`SetSnapToGrid`, `SnapPoint`), an
optional minimap and a controls overlay. Its coordinate convention is
element-local, which is what the framework dispatches.

A path editor needs precisely this and should not grow its own copy — the
mind-map, node and compositor diagrams already share it.

### 2.5 Interaction precedent

`UltraCanvasNodeDiagram` establishes the house conventions for canvas editing:
click to select, Shift+click to extend, marquee box from empty space,
drag to move, `onNodeDrag` / selection callbacks. A path editor should read as
the same family of tool.

### 2.6 The tone curve editor is *not* the thing to generalise

`UltraCanvasCurveEditor` edits a **function** `y = f(x)`: points sorted by
input, monotone interpolation, no handles, output = a 256-entry LUT. A Bézier
path editor edits **free-form 2-D geometry**: unordered anchors, two tangent
handles each, closed subpaths, self-intersection allowed.

Folding one into the other would cost both: the curve editor would lose its
monotonicity guarantee (the property that makes its LUT usable), and the path
editor would inherit a "sorted by x" model it must fight. **Recommendation:
keep them as siblings**, sharing conventions (grab radius, selection colours,
Delete-removes-point, arrow-key nudge) rather than code. If a shared base is
ever wanted, the honest extraction is a small `HandleDragController` helper,
not a common curve class.

---

## 3. Where it belongs

**Recommendation: the model and the element go in the core library**
(`UltraCanvas/{include,core}`), *not* into `Plugins/Vector`.

| | Core | `Plugins/Vector` |
|---|---|---|
| Builds today | ✅ | ❌ (233 errors, §2.1) |
| Enabled by default | ✅ | ❌ (`OFF`) |
| Available to charts / diagrams / the media viewer | ✅ | ❌ (they don't link it) |
| Extra dependencies | none | vips + zlib |

The user-facing framing of "under vector graphics support" is still honoured:
the element is *documented* under vector graphics, and it gets an **optional
adapter** to `VectorStorage::PathData` (a single header, compiled only when
the plugin is enabled) so the two meet if and when the Vector module is
repaired. The dependency direction matters: the plugin may depend on the core
model, never the other way round.

This mirrors the tone curve split that already proved itself: a dependency-free
model (`UltraCanvasToneCurve`, unit-testable without a UI stack), the element
on top, the host wiring last.

---

## 4. The model — `UltraCanvasBezierPath`

An editing model, not a rendering command list. The distinction is the whole
point: SVG's `d` is an *instruction stream* (relative/absolute, smooth
shorthands, implicit control points) that is miserable to edit directly, while
an editor wants **anchors that own their two handles**.

```cpp
// include/UltraCanvasBezierPath.h  — no UI, no image library

enum class BezierNodeType {
    Corner,     // handles independent (a hard corner)
    Smooth,     // handles collinear, lengths independent
    Symmetric,  // handles collinear and equal length
    Auto        // handles derived from the neighbours (Inkscape's "auto")
};

struct BezierNode {
    Point2Dd anchor;            // the on-curve point
    Point2Dd handleIn;          // control point of the incoming segment
    Point2Dd handleOut;         // control point of the outgoing segment
    BezierNodeType type = BezierNodeType::Corner;
    bool handleInActive  = false;   // false = the segment is a straight line
    bool handleOutActive = false;
};

class UltraCanvasBezierSubpath {
public:
    std::vector<BezierNode> nodes;
    bool closed = false;

    // Geometry
    Point2Dd Evaluate(double t) const;            // t over the whole subpath
    Point2Dd EvaluateSegment(int index, double t) const;
    double   Length(double tolerance = 0.25) const;
    Rect2Dd  Bounds(bool includeHandles = false) const;
    std::vector<Point2Dd> Flatten(double tolerance = 0.25) const;

    // Editing primitives (all index-returning, like the tone curve)
    int  InsertNodeAt(int segment, double t);     // split a segment, curve shape preserved
    bool RemoveNode(int index);                   // neighbours re-fitted, not just dropped
    void SetNodeType(int index, BezierNodeType);  // re-aligns handles to the rule
    void MoveAnchor(int index, const Point2Dd& to);
    void MoveHandle(int index, bool outgoing, const Point2Dd& to);  // honours the node type
    bool JoinTo(UltraCanvasBezierSubpath& other, int thisEnd, int otherEnd);
    void Reverse();

    // Queries the editor needs
    struct Hit { int segment; double t; double distance; Point2Dd point; };
    std::optional<Hit> HitTestOutline(const Point2Dd& p, double maxDistance) const;
    // BezierFillRule is new: the core has no fill-rule enum today (the one in
    // VectorStorage lives in the plugin that does not build).
    bool ContainsPoint(const Point2Dd& p, BezierFillRule rule) const;
};

class UltraCanvasBezierPath {
public:
    std::vector<UltraCanvasBezierSubpath> subpaths;

    Rect2Dd Bounds() const;
    void Transform(double a, double b, double c, double d, double e, double f);

    // ===== SVG INTEROP (the missing pair, §2.3) =====
    static std::optional<UltraCanvasBezierPath> FromSVGPathData(const std::string& d);
    std::string ToSVGPathData(int precision = 3, bool relative = false) const;

    // ===== RENDER =====
    // Emits the path into a render context (MoveTo/BezierCurveTo/ClosePath),
    // leaving Fill()/Stroke() to the caller — so a chart, a diagram edge or a
    // paint tool can use the same geometry with its own paint.
    void BuildPath(IRenderContext* ctx) const;
};
```

Notes on the design:

- **`FromSVGPathData` / `ToSVGPathData` are the interop story.** They make the
  Vectorizer's output editable, let a path be stored in JSON/settings as one
  string, and are what the (future) `VectorStorage` adapter is written in terms
  of. Arcs (`A`) are converted to cubics on import — the standard approach, and
  the only one that keeps the editing model uniform.
- **`InsertNodeAt(segment, t)` must preserve the shape** (de Casteljau split),
  not just drop a point on the curve. `RemoveNode` should re-fit the merged
  segment rather than leaving a kink; both are textbook and cheap.
- The node **type rules** (`Smooth` / `Symmetric` / `Auto`) live in the model,
  so the element does not re-implement handle mirroring — the same reason
  monotonicity lives in `UltraCanvasToneCurve`, not in the curve element.
- Everything is `Point2Dd` in **path space**; the element maps to pixels
  through the viewport. That keeps the model resolution-independent and the
  element's zoom free.

---

## 5. The element — `UltraCanvasBezierEditor`

```cpp
// include/UltraCanvasBezierEditor.h

enum class BezierEditorTool {
    Select,     // pick and move anchors / handles / whole subpaths
    Pen,        // click = corner node, click-drag = smooth node (the Pen tool)
    AddRemove,  // click on the outline inserts, click on a node removes
    Transform   // marquee + bounding-box scale/rotate of the selection
};

class UltraCanvasBezierEditor : public UltraCanvasUIElement {
public:
    // Content
    void SetPath(const UltraCanvasBezierPath& path);
    const UltraCanvasBezierPath& GetPath() const;

    // A backdrop to trace over: the picture being vectorised, a reference
    // photo, a screenshot. Drawn under the grid at a configurable opacity.
    void SetBackdropImage(std::shared_ptr<UCImage> img, const Rect2Dd& placement);
    void SetBackdropOpacity(float alpha);

    // Tools and view
    void SetTool(BezierEditorTool tool);
    UltraCanvasDiagramViewport& Viewport();      // zoom / pan / snap grid / minimap
    void FitView();

    // Selection
    struct Selection { std::vector<std::pair<int,int>> nodes; };  // {subpath, node}
    const Selection& GetSelection() const;
    void SelectAll();
    void ClearSelection();

    // Operations the host puts on a toolbar
    void SetSelectedNodeType(BezierNodeType type);
    void DeleteSelected();
    void CloseSubpath();
    void BreakAtSelected();
    void JoinSelected();
    void ReverseSubpath();
    bool Undo();  bool Redo();      // editor-local stack, see §8

    // Callbacks
    std::function<void(const UltraCanvasBezierPath&)> onPathChanged;    // live, per drag step
    std::function<void(const UltraCanvasBezierPath&)> onEditFinished;   // per completed edit
    std::function<void(const Selection&)> onSelectionChanged;
};
```

### Interaction (the part that decides whether it feels right)

| Input | Behaviour |
|---|---|
| Click an anchor | Select it; Shift+click extends the selection |
| Drag an anchor | Move it (grid/​guide snapping through the viewport) |
| Drag a handle | Reshape; `Smooth`/`Symmetric` mirror per the node type |
| `Alt`+drag a handle | Break the mirror for this drag (make it a corner) |
| Drag on the outline | Bend the segment (moves both handles — Inkscape's direct-curve drag) |
| Double-click the outline | Insert a node at that `t`, shape preserved |
| Right-click / `Delete` a node | Remove it, neighbouring segment re-fitted |
| Marquee from empty space | Rubber-band selection of nodes |
| Arrow keys | Nudge selection 1 px (`Shift` = 10 px), in path units at the current zoom |
| `Ctrl`+drag on empty space | Pan; wheel zooms at the cursor (viewport) |
| Pen tool: click / click-drag | Append a corner / smooth node; click the first node closes |
| `Esc` | Finish the current subpath (Pen), or clear the selection |

Rendering: backdrop → grid → filled preview (optional, `BezierFillRule`) → outline →
segments of the selected subpath highlighted → handles (lines + round knobs) →
anchors (squares, hollow = unselected, filled = selected) → marquee. Handle
knobs are only drawn for the selected nodes and their neighbours, which is what
keeps a 200-node path readable.

Style through a `BezierEditorStyle` struct (`Default()` / `Dark()`), matching
`CurveEditorStyle`.

---

## 6. What it is *not*

Kept deliberately out of v1, so the element stays an element and does not
become an application:

- **No boolean operations** (union / difference / intersection). That is a
  clipping library's job (a future `UltraCanvasPathOps` on top of the model).
- **No multi-object scene, layers, or z-order.** The element edits *one*
  `UltraCanvasBezierPath` (which may hold many subpaths). A drawing app owns
  the document; this owns a path.
- **No text-to-path, no strokes-to-path, no offsetting.** Model-level
  operations that can be added later without touching the element.
- **No file I/O.** `ToSVGPathData()` is a string; saving is the host's.

---

## 7. Who would use it on day one

| Caller | Use |
|---|---|
| **Vectorizer** (`Plugins/Vectorizer`) | Trace a bitmap, then fix the traced outline by hand — today its SVG output can only be re-rendered, never corrected |
| **Diagram edges** (`NodeDiagram`, `MindMap`, `Compositor`) | Let the user reshape a routed connector instead of accepting the router's curve |
| **Charts** | Author a custom shape / marker / annotation |
| **Media viewer** | A future crop / mask path over an image (the backdrop support in §5 exists for exactly this) |
| **Any app** | A reusable "draw a shape" control — the thing that today has to be hand-rolled |

---

## 8. Reuse map — what is already there vs what is new

| Need | Existing | New |
|---|---|---|
| Zoom / pan / snap grid / minimap | `UltraCanvasDiagramViewport` | — |
| Path drawing primitives | `IRenderContext::BezierCurveTo` etc. | — |
| Selection / marquee conventions | `UltraCanvasNodeDiagram` (pattern) | — |
| Toolbar, buttons, dropdowns for a host dialog | existing elements | — |
| Model + editing operations | — | `UltraCanvasBezierPath` (§4) |
| SVG `d` parse / write | — | in the model (§2.3) |
| Path hit-testing (outline + fill) | — | in the model |
| Undo / redo | *nothing shared exists* — `UltraCanvasTextArea` has its own private stack | editor-local stack of path snapshots (a path is small; snapshots are simpler and safer than command objects) |

The undo finding is worth flagging separately: the framework has **no shared
undo facility**. This element should not be the one to invent a framework-wide
one, but a second caller would justify extracting `UltraCanvasUndoStack<T>`
later.

---

## 9. Delivery plan

Sizes are calibrated against comparable code in this tree
(`UltraCanvasToneCurve.cpp` 270 lines, `UltraCanvasCurveEditor.cpp` 379,
`UltraCanvasDiagramViewport.cpp` 354, `UltraCanvasColorPicker.cpp` 1733).

| Phase | Deliverable | Est. |
|---|---|---|
| **P1** | `UltraCanvasBezierPath` model: nodes, node types, evaluation, flatten, bounds, split/remove/join, hit-test, SVG `d` in/out. `Tests/BezierPathTest.cpp` (no UI stack, like `ToneCurveTest`). | ~700 lines + ~250 test |
| **P2** | `UltraCanvasBezierEditor` element: rendering, Select tool, anchor/handle drag, node types, insert/remove, marquee, keyboard, viewport wiring, style struct. | ~900 lines |
| **P3** | Pen tool, subpath open/close/join/break/reverse, undo/redo, backdrop image. | ~400 lines |
| **P4** | Docs (`UltraCanvasBezierEditor.md`), DemoApp tab, catalogue rows, changelog. | ~300 lines + docs |
| **P5** *(optional)* | `VectorStorage::PathData` ⇄ model adapter — **only worth doing after the Vector plugin is repaired**; that repair is its own task (§2.1) and should not be bundled here. | ~150 lines |

P1 is independently useful the day it lands: `FromSVGPathData` /
`ToSVGPathData` alone give the framework something it has never had.

---

## 10. Open decisions

1. **Name.** `UltraCanvasBezierEditor` (as asked for) or `UltraCanvasPathEditor`
   (it also edits straight segments and arcs-as-cubics)? This document uses the
   former; the model is `UltraCanvasBezierPath` either way.
2. **One path or a small scene?** v1 edits one `UltraCanvasBezierPath` (§6). If
   multi-object editing is wanted soon, it changes the selection model, so it
   is better decided now than retrofitted.
3. **Does the Vector plugin get repaired?** It is 233 errors of bit-rot on an
   otherwise complete document model, and it gates the XAR/CDR import path
   being usable as *editable* documents rather than pictures. Independent of
   this element, but it decides whether P5 ever happens.
4. **Quadratic segments.** Keep cubic-only internally (converting quadratics on
   import, as SVG renderers do), or carry quadratics through for round-trip
   fidelity with fonts and TrueType-derived paths? Cubic-only is proposed.
5. **Where do the docs live?** Proposed:
   `Docs/UltraCanvas/UltraCanvasBezierEditor.md` plus a "vector graphics" row
   in the element catalogue, since the user framing is vector-graphics support.

---

## 11. Recommendation

Build P1 + P2 as core elements
(`include/core/UltraCanvasBezierPath`, `include/core/UltraCanvasBezierEditor`),
document them under vector graphics, and leave `Plugins/Vector` alone until
somebody decides whether to repair it. That gives programmers the element the
tone curve editor made them want, without inheriting a module that does not
build — and it gives the Vectorizer its missing second half.
