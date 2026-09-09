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

Bézier is the *storage* answer, not the whole answer: §5 covers the other curve
mechanisms (quadratics, elliptical arcs, Catmull-Rom, B-splines, NURBS, spiro),
which of them the model must keep as themselves, which are editing styles over
the same nodes, and which are deliberately out of scope.

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

// What a segment actually IS, so a round-trip does not rewrite it (§5.2).
// Everything emits cubics for rendering; only Arc is an approximation there.
enum class BezierSegmentKind {
    Line,        // straight
    Cubic,       // two handles — the default
    Quadratic,   // one shared control point (TrueType glyphs, SVG `Q`)
    Arc          // elliptical arc (SVG `A`): rx, ry, rotation, largeArc, sweep
};

// SVG arc parameters, kept so an `A` segment round-trips as an `A` (§5.2).
struct ArcParameters {
    double rx = 0, ry = 0;      // radii
    double rotationDeg = 0;     // x-axis rotation
    bool largeArc = false;
    bool sweep = false;
};

// How a subpath's node positions produce its geometry (§5.3). The nodes are
// the same list in every style; the style decides what the handles mean, so
// switching is non-destructive and the output is always cubic.
enum class BezierCurveStyle {
    BezierHandles,  // the user drags the handles (Illustrator/Inkscape pen)
    CatmullRom,     // interpolating: no handles, the curve passes through the
                    // nodes; `tension` shapes it (chart smoothing, motion paths)
    BSpline         // approximating: uniform cubic B-spline, C2, local control
};

class UltraCanvasBezierSubpath {
public:
    std::vector<BezierNode> nodes;
    bool closed = false;
    BezierCurveStyle style = BezierCurveStyle::BezierHandles;
    double tension = 0.5;                       // CatmullRom only
    std::vector<BezierSegmentKind> segmentKinds;  // per segment; Cubic by default
    std::vector<ArcParameters> arcs;              // rx/ry/rotation/flags, Arc segments only

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

    // ===== CURVE STYLE CONVERSION (§5.3) =====
    // Every style resolves to the same cubic geometry, so a caller that only
    // wants the maths never touches the editor. This is what the ad-hoc
    // Catmull-Rom implementations in the charts (§5.1) would call instead.
    static UltraCanvasBezierPath FromPoints(const std::vector<Point2Dd>& pts,
                                            BezierCurveStyle style,
                                            double tension = 0.5,
                                            bool closed = false);
    // Bakes the current style into explicit handles (BezierHandles), so the
    // user can take over a generated curve by hand.
    void FlattenStyleToHandles();
};
```

Notes on the design:

- **`FromSVGPathData` / `ToSVGPathData` are the interop story.** They make the
  Vectorizer's output editable, let a path be stored in JSON/settings as one
  string, and are what the (future) `VectorStorage` adapter is written in terms
  of. Arcs and quadratics are **kept as what they are** (`BezierSegmentKind`)
  rather than silently rewritten as cubics — see §5.2 for why that matters.
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

---

## 5. Other curve families — Bézier is not the only mechanism

The question "are other curve types supported?" splits into two very different
questions, and conflating them is the classic mistake in path models:

- **What is a segment made of?** (line, quadratic, cubic, elliptical arc,
  rational/NURBS) — this decides *fidelity*: whether a file round-trips
  unchanged.
- **How does the user shape it?** (drag handles, drag points the curve passes
  through, drag a loose control polygon) — this decides *feel*, and every one
  of these produces cubic geometry in the end.

The model in §4 answers the first with `BezierSegmentKind` and the second with
`BezierCurveStyle`. The rest of this section is why those two axes, and where
the line is drawn.

### 5.1 The framework already needs the other families — five times over

This is not speculative. Searching the tree for smoothing turns up **five
independent, ad-hoc implementations** of interpolating splines and smoothers,
none of them shared:

| Where | What it does |
|---|---|
| `Plugins/Charts/UltraCanvasSpecificChartElements.cpp:59` (`DrawSmoothLine`) | Catmull-Rom, sampled to 20 straight lines per span, drawn with `DrawLine` |
| `Plugins/Charts/UltraCanvasSpecificChartElements.cpp:460` (`CalculateSmoothPath`) | the same maths again, this time returning the sampled points |
| `Plugins/Charts/UltraCanvasPolarChart.cpp:1548` | Hermite basis with a tension factor, sampled to points |
| `Plugins/Charts/UltraCanvasParallelCoordinateChart.cpp:368` | Catmull-Rom converted to **one cubic Bézier per segment** — the correct conversion, written once, locally |
| `Plugins/Charts/Engine/UltraCanvasChartHighlights.cpp:121` | Chaikin subdivision smoothing of cluster blobs |

Three of those five sites (`DrawSmoothLine`, `CalculateSmoothPath`, the polar
chart) sample the curve into 20 short straight lines per span — which is why a
smoothed chart line looks faceted when the chart is zoomed or exported to a
vector format; the Chaikin one is a polygon filter rather than a curve at all;
and only the parallel-coordinate chart emits real cubics, using exactly the
`tension/6` Catmull-Rom → Bézier formula a shared model would own. Add the **`quadTo()`** already coming out of the MicroTeX
glyph backend (`Plugins/LaTeX/UltraCanvasLaTeXBackend.h:88` — TrueType outlines
are quadratic), and the picture is clear: the tree already carries three curve
families, each handled locally and lossily.

So the answer to "is it useful to support other styles?" is not "it would be
nice" — it is **it deletes code that already exists**.

### 5.2 Segment kinds — what must be *stored*, not just drawn

| Kind | Where it comes from | Exactly representable as cubic? | Verdict |
|---|---|---|---|
| **Line** | everything | yes (trivially) | **P1** |
| **Cubic Bézier** | SVG `C`/`S`, PostScript, PDF, CFF fonts, Cairo, the Vectorizer | native | **P1** |
| **Quadratic Bézier** | SVG `Q`/`T`, TrueType glyphs, the LaTeX backend | **yes** — degree elevation is exact | **P1** — keep the kind so a glyph round-trips as `Q`, edit it as one handle |
| **Elliptical arc** | SVG `A`, PowerPoint/ODF shapes, CAD, rounded rectangles | **no** — best-fit cubics, ~1e-4 relative error per 90° span | **P1 (stored), P2 (edit gizmo)** — see below |
| **Rational Bézier / NURBS** | CAD (STEP, IGES, DWG), Rhino, Fusion | **no** — a true circle is not a polynomial curve | **out of scope** — see §5.4 |

The arc is the interesting one. Converting `A` to cubics on import (what the
first draft of this proposal said, and what many toy editors do) is lossy in a
way users notice:

- an SVG that came in with `A` goes out as `C` — a diff on a hand-maintained
  icon file becomes unreadable;
- the semantic "this is a circle of radius r" is gone, so the editor can no
  longer offer *radius* / *sweep* handles, only four anonymous control points;
- re-importing and re-exporting repeatedly accumulates approximation error.

Storing the arc parameters and converting **only at render time** costs one
struct and one converter, and keeps `ToSVGPathData()` honest. The same argument
applies to quadratics, where the conversion is exact but the *shape of the
edit* (one handle, not two) is what the user expects on a glyph.

### 5.3 Curve styles — how the user shapes it

All three of these are the same node list under different rules, and all three
emit cubic Béziers. That is what makes them a *mode*, not a separate element:

| Style | Curve passes through the nodes? | Handles | Continuity | Where it fits |
|---|---|---|---|---|
| **BezierHandles** (default) | yes | two per node, dragged | as authored (C0/C1/G1 per node type) | Illustrator / Inkscape pen work, tracing, icons |
| **CatmullRom** | **yes** | none — the nodes *are* the curve | C1 | chart series smoothing, motion paths, "draw through these points"; `tension` = the existing charts' knob |
| **BSpline** | no (approximating) | the nodes form a loose control polygon | **C2** | organic shapes, road/pipe-like curves, anything where curvature should not kink |

Two properties make this cheap:

- **Conversion is one-way and exact.** Catmull-Rom → cubic and uniform
  B-spline → cubic are both closed-form per segment (the second is Boehm's knot
  insertion). The model computes handles from the nodes; nothing is
  approximated.
- **`FlattenStyleToHandles()` is the escape hatch.** A user who wants to take
  a generated curve over by hand converts it once to `BezierHandles` and keeps
  editing. The reverse (fitting arbitrary handles back into a Catmull-Rom) is
  *not* well-defined, and the model should not pretend otherwise — the
  conversion is deliberately one-directional.

For the charts this is the payoff: `UltraCanvasBezierPath::FromPoints(pts,
CatmullRom, tension)` replaces four hand-rolled Catmull-Rom loops, and the
result draws as real cubics at any zoom instead of 20 line segments per span.

### 5.4 What is deliberately left out

- **NURBS / rational segments.** They are the right answer for CAD, and the
  only exact way to represent a circle. But: they need weights and a knot
  vector, the editing UI grows a weight-per-node concept, Cairo cannot render
  them anyway (they would be approximated by cubics at draw time), and **no
  CAD format is imported by this framework today** — there is no DXF, STEP or
  DWG reader in the tree. The cost lands now and the benefit lands never.
  Revisit if a CAD importer is ever scheduled; the `BezierSegmentKind` enum is
  the extension point.
- **Spiro / clothoid (Euler-spiral) curves.** Curvature-continuous and
  genuinely lovely for typeface work (FontForge, Inkscape's spiro mode). It
  needs either a vendored `libspiro` — which per the house rules means
  `Docs/Dependencies.md`, `master_dependencies.yaml` and
  `THIRD_PARTY_LICENSES.md` entries — or ~600 lines of clothoid solver. It is a
  clean later addition as a fourth `BezierCurveStyle`, because it too resolves
  to cubics. Not P1.
- **Subdivision smoothing (Chaikin).** Already present for chart blobs
  (§5.1); it is a polygon filter, not an editable curve. It should stay a
  utility function, not become a style.
- **Monotone cubic (PCHIP).** That is the tone curve's interpolation
  (`UltraCanvasToneCurve`) and it is a *function* spline — one y per x. It
  belongs where it is (§2.6), not in a 2-D path model.

### 5.5 Cost of each addition

| Addition | Est. | Buys |
|---|---|---|
| Quadratic segments preserved | ~60 lines | glyph / TrueType round-trip, one-handle editing |
| Elliptical arcs preserved + arc↔cubic | ~250 lines | SVG `A` round-trip, true circles, radius editing |
| `CatmullRom` style + `FromPoints()` | ~80 lines | deletes 4 ad-hoc chart implementations, smooth motion paths |
| `BSpline` style | ~120 lines | C2 organic curves |
| Spiro style | ~600 lines **or** a vendored dependency | typeface-grade smoothness |
| NURBS / rational | ~800 lines + UI concepts | CAD interop the framework cannot use yet |

The first three are the ones that pay for themselves immediately.

## 6. The element — `UltraCanvasBezierEditor`

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
    // Curve style of the subpath being edited (§5.3): handles, Catmull-Rom
    // through the nodes, or a B-spline control polygon. Switching to
    // BezierHandles bakes the generated handles so the user can take over.
    void SetSubpathStyle(int subpath, BezierCurveStyle style, double tension = 0.5);
    // Turn the selected segment into a line / quadratic / cubic / arc (§5.2).
    void SetSelectedSegmentKind(BezierSegmentKind kind);
    void DeleteSelected();
    void CloseSubpath();
    void BreakAtSelected();
    void JoinSelected();
    void ReverseSubpath();
    bool Undo();  bool Redo();      // editor-local stack, see §9

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
| Drag an arc segment's radius grip | Change `rx` / `ry`; `Shift` keeps them equal (a true circle) |
| In a Catmull-Rom / B-spline subpath | No handles are drawn — the nodes (or the control polygon) are dragged directly |
| `Esc` | Finish the current subpath (Pen), or clear the selection |

Rendering: backdrop → grid → filled preview (optional, `BezierFillRule`) → outline →
segments of the selected subpath highlighted → handles (lines + round knobs) →
anchors (squares, hollow = unselected, filled = selected) → marquee. Handle
knobs are only drawn for the selected nodes and their neighbours, which is what
keeps a 200-node path readable.

Style through a `BezierEditorStyle` struct (`Default()` / `Dark()`), matching
`CurveEditorStyle`.

---

## 7. What it is *not*

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

## 8. Who would use it on day one

| Caller | Use |
|---|---|
| **Vectorizer** (`Plugins/Vectorizer`) | Trace a bitmap, then fix the traced outline by hand — today its SVG output can only be re-rendered, never corrected |
| **Diagram edges** (`NodeDiagram`, `MindMap`, `Compositor`) | Let the user reshape a routed connector instead of accepting the router's curve |
| **Charts** | Author a custom shape / marker / annotation — and, through `FromPoints(…, CatmullRom, tension)`, replace the four hand-rolled Catmull-Rom loops in the chart plugins with one shared conversion that draws real cubics (§5.1) |
| **Media viewer** | A future crop / mask path over an image (the backdrop support in §6 exists for exactly this) |
| **Any app** | A reusable "draw a shape" control — the thing that today has to be hand-rolled |

---

## 9. Reuse map — what is already there vs what is new

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

## 10. Delivery plan

Sizes are calibrated against comparable code in this tree
(`UltraCanvasToneCurve.cpp` 270 lines, `UltraCanvasCurveEditor.cpp` 379,
`UltraCanvasDiagramViewport.cpp` 354, `UltraCanvasColorPicker.cpp` 1733).

| Phase | Deliverable | Est. |
|---|---|---|
| **P1** | `UltraCanvasBezierPath` model: nodes, node types, **segment kinds (line / quadratic / cubic / arc, kept as themselves)**, evaluation, flatten, bounds, split/remove/join, hit-test, SVG `d` in/out, **`FromPoints()` with the `CatmullRom` style**. `Tests/BezierPathTest.cpp` (no UI stack, like `ToneCurveTest`). | ~1000 lines + ~300 test |
| **P2** | `UltraCanvasBezierEditor` element: rendering, Select tool, anchor/handle drag, node types, insert/remove, marquee, keyboard, viewport wiring, style struct. | ~900 lines |
| **P3** | Pen tool, subpath open/close/join/break/reverse, undo/redo, backdrop image, arc radius gizmo, the `BSpline` style. | ~550 lines |
| **P4** | Docs (`UltraCanvasBezierEditor.md`), DemoApp tab, catalogue rows, changelog. | ~300 lines + docs |
| **P5** *(optional)* | Charts migrated onto `FromPoints()` — deletes the four hand-rolled Catmull-Rom implementations (§5.1) and stops smoothed series being drawn as 20 straight lines per span. | ~150 lines, mostly deletions |
| **P6** *(optional)* | `VectorStorage::PathData` ⇄ model adapter — **only worth doing after the Vector plugin is repaired**; that repair is its own task (§2.1) and should not be bundled here. | ~150 lines |

P1 is independently useful the day it lands, with no UI at all:
`FromSVGPathData` / `ToSVGPathData` give the framework something it has never
had, and `FromPoints()` is immediately callable by the charts.

---

## 11. Open decisions

1. **Name.** `UltraCanvasBezierEditor` (as asked for) or `UltraCanvasPathEditor`
   (it also edits straight segments and arcs-as-cubics)? This document uses the
   former; the model is `UltraCanvasBezierPath` either way.
2. **One path or a small scene?** v1 edits one `UltraCanvasBezierPath` (§7). If
   multi-object editing is wanted soon, it changes the selection model, so it
   is better decided now than retrofitted.
3. **Does the Vector plugin get repaired?** It is 233 errors of bit-rot on an
   otherwise complete document model, and it gates the XAR/CDR import path
   being usable as *editable* documents rather than pictures. Independent of
   this element, but it decides whether P5 ever happens.
4. **How far do the segment kinds go?** Proposed: line + cubic + quadratic +
   elliptical arc stored as themselves (§5.2), NURBS/rational out of scope
   until a CAD importer exists. The alternative — cubic-only, converting
   everything on import — is simpler but rewrites `A` as `C` on every SVG
   round-trip.
5. **Which curve styles ship?** Proposed: `BezierHandles` + `CatmullRom` in P1
   (the second pays for itself by replacing the chart code), `BSpline` in P3,
   spiro only if somebody wants a vendored `libspiro` (§5.4).
6. **Do the charts migrate?** P5 is optional and touches three chart files (four call sites). It
   is the payoff for the shared model, but it is also a behaviour change
   (smoothed lines become real curves instead of 20-segment polylines), so it
   deserves its own review.
7. **Where do the docs live?** Proposed:
   `Docs/UltraCanvas/UltraCanvasBezierEditor.md` plus a "vector graphics" row
   in the element catalogue, since the user framing is vector-graphics support.

---

## 12. Recommendation

Two axes, one model: **what a segment is** (`BezierSegmentKind` — keep arcs and
quadratics as themselves) and **how the user shapes it** (`BezierCurveStyle` —
handles, Catmull-Rom, B-spline), with everything resolving to cubics for
rendering and SVG output. That covers the other curve mechanisms without a
second element and without a second model.

Build P1 + P2 as core elements
(`include/core/UltraCanvasBezierPath`, `include/core/UltraCanvasBezierEditor`),
document them under vector graphics, and leave `Plugins/Vector` alone until
somebody decides whether to repair it. That gives programmers the element the
tone curve editor made them want, without inheriting a module that does not
build — and it gives the Vectorizer its missing second half.
