# ArtCreator and a public `UltraCanvasVectorCanvas` — Investigation and Proposal

**Status:** phases 0–4 implemented (framework 0.8.84, ArtCreator 0.2.0); phase 5 open
**Date:** 2026-09-15
**Scope:** a new application, `Apps/ArtCreator`, a vector graphics editor of
the Xara Designer / ArtWorks class, and the question of whether the editing
surface it needs should be a public framework element rather than application
code.

This is the vector counterpart of
[`Docs/UltraPaint/FeatureGapAnalysis.md`](../UltraPaint/FeatureGapAnalysis.md),
written before the application rather than after it. It builds on
[`UltraCanvasVectorModelProposal.md`](UltraCanvasVectorModelProposal.md)
(the model survey of 2026-09-08) and on
[`Docs/UltraCanvas/UltraCanvasBezierEditorProposal.md`](../UltraCanvas/UltraCanvasBezierEditorProposal.md)
(2026-08-25), and corrects the parts of the latter that the model work has
since overtaken. Everything below was verified against the source on this
date; file references are to the current tree.

---

## 1. Recommendation

**Yes — build a public `UltraCanvasVectorCanvas` element, and build it the
way `UltraCanvasPaintSurface` was built for UltraPaint: model and history in
the core library with no UI in them, one editing element that shows the
document and hands tools their events, and only the tools, panels and
dialogs in the application.**

Three findings drive that:

1. **The document model already exists and is the right shape.**
   `VectorStorage::VectorDocument` (layers → groups → elements, double
   precision transforms, cubic paths, gradients, dashes, spans-based text,
   embedded images, units) is read by six format converters and written by
   ten, including Xara's own `.xar`. The 2026-09-08 survey judged the shape
   right and the missing parts enumerable. A second model for the editor
   would be the mistake the 3D work avoided.
2. **Every viewer in the tree is view-only.** `UltraCanvasVectorElement`
   zooms, pans and hit-tests bounding boxes; the XAR, SVG, CDR and EPS
   plugin elements do not take input at all; the diagram elements edit
   rectangles of their own kind. Nothing selects a shape, shows a handle,
   edits a node, or undoes. That is the whole editing layer, and it is what
   an icon editor, a diagram designer, a chart annotator or a PDF markup
   tool would need too — the same argument that put `UltraCanvasPaintSurface`
   in `UltraCanvas/{include,core}` rather than in UltraPaint.
3. **`AGENTS.md` already decides it.** "If it takes input, shows a picture,
   or presents a value, it is an element … add a new one to
   `UltraCanvas/{include,core}` so the next caller finds it too." A canvas
   that takes input and shows a picture is the definition.

What the element must **not** be: a Xara-in-a-box. Tools, the tool palette,
the infobar, effect dialogs and the file menu are application code. The
element owns the view transform, rulers, guides, grid, snapping, selection
display and handles, the overlay hook, pointer events in document
coordinates, and dirty-rect redraw. That split is what lets a second
application reuse it.

The rest of this document is the evidence: what a Designer-class editor
needs (§2), what the framework has (§3), the proposed shape of the element
and its core companions (§4), what has to be added to the rendering layer
and the model first (§5), a phased plan (§6), and the open decisions (§7).

---

## 2. What a Xara Designer-class editor needs

Xara Designer Pro+ (and Photo & Graphic Designer, the same engine) is the
descendant of ArtWorks on RISC OS. Its defining traits are a small, fast tool
set, *solid* drag (objects render fully while being moved), fully
anti-aliased display, and effects that stay live and editable. The feature
inventory below is what a user of that class of program expects; the last
two columns say what UltraCanvas has and where the missing part belongs.

Legend: **y** exists and usable · **p** partial · **n** absent ·
*layer* = render context (RC), model (M), core editing layer (E), canvas
element (C), application (A).

### 2.1 Tools

| Feature | Xara | UltraCanvas today | Belongs |
|---|---|---|---|
| Selector: click / marquee / shift-extend, move, scale, rotate, skew via handles; centre of rotation; nudge | y | n — bbox hit test only, no selection display | E + C |
| Solid drag with full render, anti-aliased | y | y (Cairo, dirty rects) | C |
| Shape editor: move / add / delete nodes, cusp ↔ smooth, drag a segment, break / join | y | n — `VectorPath` stores commands, no node model | E |
| Pen (click-drag Béziers) and straight line | y | n | A over E |
| Freehand with smoothing slider, pressure | y | n (pressure exists in `UCEvent`) | A over E |
| Rectangle / ellipse / QuickShape (polygon, star, live corner rounding) | y | model has `Rect` radius, `Polygon`; `Star` / `RegularPolygon` declared, never used | M + A |
| Text: artistic text, text areas / columns, text on a curve, tracking / kerning, styles | y | `VectorText` spans, `TextPath` declared and dead; renderer sets font once; no text→curves | M + RC |
| Fill tool: flat, linear, circular, elliptical, conical, diamond, three / four colour, bitmap, fractal cloud / plasma; multi-stage; fill profile; repeating | y | flat, linear, radial, elliptical: y. Conical, mesh: declared in model, no renderer / RC. Bitmap fill: file-path only. Fractal: n | RC + M |
| Transparency tool: same shapes as fills; 11 mix modes | y | element opacity only; no blend modes in RC, none in renderer | RC + M |
| Shadow (wall / floor / glow, blur), feather | y | n in model; XAR plugin parses and approximates; no blur primitive in RC | RC + M |
| Bevel, contour, blend (object-to-object), mould (envelope / perspective) | y | n in model; XAR plugin parses, does not render | M + E |
| Live effects (bitmap effects on vectors, still editable), 3D extrude | y | PixelFX has the filters offline | M + A |
| Photo tool (crop, enhance, clip), eraser / mask | y | UltraPaint's raster layer exists; `UltraCanvasVectorRaster` bridges vector→pixels | A |
| Boolean shape ops: add, subtract, intersect, slice; join shapes; break apart | y | n — `CombinePaths` is a concatenating stub, undeclared | E |
| Line gallery: dashes, arrowheads, brush strokes, variable width, stroke shapes | y | dash / caps / joins: y. Arrows, brushes, variable width: n | M + RC |
| Zoom / push (pan); zoom to page / drawing / selection | y | y in `UltraCanvasVectorElement` and `UltraCanvasDiagramViewport` | C |
| QR code tool | y | `Plugins/QRCode` exists | A |

### 2.2 Document and workspace

| Feature | Xara | UltraCanvas today | Belongs |
|---|---|---|---|
| Layers: visible, locked, solid (edit-only), reorder, rename | y | `VectorLayer` has Name / Locked / Visible / Opacity; no UI | M: y, A: n |
| Multi-page documents, spreads | y | n in `VectorDocument`; the XAR plugin's private model has pages | M |
| Rulers; guides dragged from rulers; guide layer | y | n — no ruler element anywhere | C |
| Grid; magnetic snap to grid / guides / object points | y | snap-to-grid only, in `UltraCanvasDiagramViewport` | C |
| Page size, orientation, units, bleed | y | `Size`, `SourceUnit`, `PointsPerSourceUnit`: y. Orientation / bleed: n | M + A |
| Object names; find by name | y | `Id` and `Classes` only | M |
| Alignment and distribution; arrange (to front / back / forward / back); group / ungroup | y | `VectorGroup` exists; no operations | E + A |
| Duplicate, clone, repeat (smart duplicate) | y | `Clone()` on every element: y; commands: n | E + A |
| Infobar numeric transform (x, y, w, h, angle, scale) | y | `UltraCanvasSpinner` / `UltraCanvasFormLayout` exist | A |
| Colour gallery: named colours, linked shades / tints; colour line | y | `UltraCanvasColorPicker`, `UltraCanvasColorSwatchBar`; `NamedStyles` in model unused | M + A |
| Gradient editor (stops on a strip, drag on the object) | y | n — no multi-stop gradient editor element | new element |
| Object properties inspector | y | n — `UltraCanvasFormLayout` is the building block | A |
| Unlimited undo with history | y | n for vector; MindMap has JSON-snapshot undo, raster has rect-snapshot undo | E |
| ClipView (clip to a shape) | y | `ClipPath` declared in model, never honoured; RC clips to any path | RC: y, M + renderer: n |
| Symbols / repeating objects / designs gallery | y | `VectorSymbol` / `VectorUse` in model, SVG only | M: p |

### 2.3 Import / export

| Format | Xara | UltraCanvas today |
|---|---|---|
| `.xar` | native | read (uncompressed) + write via the converter; compressed read only by the viewer plugin's private model |
| SVG | y | read + write, lossless against the model (`UltraCanvasSVGConverter.cpp`) |
| PDF, AI, EPS | import + export | write only; MuPDF / viewer plugins render |
| CDR | import | write only (v7 RIFF); libcdr viewer renders |
| DXF, DWG, EMF, WMF | import / export | read + write |
| PSD, PNG, JPEG, WebP, GIF, TIFF … | import + export | PixelFX loads; `UltraCanvasVectorRaster` renders any vector to pixels |
| Bitmap trace to vector | y | Vectorizer plugin (VTracer) returns an SVG string; `SVGConverter::ImportFromString` makes it a document |

The converter matrix is the strongest asset here: an editor that speaks
`VectorDocument` gets ten export formats and six import formats on day one.

---

## 3. What exists today

### 3.1 The model — `VectorStorage::VectorDocument`

`UltraCanvas/include/DataFormats/UltraCanvasVectorStorage.h` (715 lines; in
`Plugins/Vector/` until 0.8.51, when phase 0 of §6 moved it into core).
`VectorDocument{Size, ViewBox, BackgroundColor, Layers, Definitions,
SourceUnit, PointsPerSourceUnit, …}`; `VectorLayer : VectorGroup`;
`VectorElement{Id, Classes, Style, optional<Matrix3x3> Transform,
GetBoundingBox(), Clone(), GetGlobalTransform()}`; concrete `VectorRect`,
`VectorCircle`, `VectorEllipse`, `VectorLine`, `VectorPolyline`,
`VectorPolygon`, `VectorPath` (`MoveTo/LineTo/CurveTo/QuadraticTo/ArcTo/
ClosePath`, `GetLength`, `GetPointAtLength`, `GetAngleAtLength`,
`Flatten`), `VectorText` (spans), `VectorGroup`, `VectorImage`
(`EmbeddedData`). `VectorStyle{Fill, Stroke, Opacity, FillOpacity,
StrokeOpacity, Blend, ClipPath, Mask, Filters}`. Fills are a variant of
colour / linear / radial / conical / mesh gradient / pattern.
`Matrix3x3` is double throughout (since 0.3.110).

Verified limits, in addition to §2.5–2.6 of the model survey:

- **No editing surface at all** — no selection, no history, no node model.
  `VectorPath` holds SVG-style commands with `float` parameters while the
  matrix is `double`; a node editor needs anchors owning two handles, which
  is the `UltraCanvasBezierPath` design from the Bézier proposal. That
  design is right; only its home has changed (§4.3).
- **Hit testing is bounding-box only.** `HitTestElement`
  (`UltraCanvasVectorRenderer.cpp:485-489`) tests the box; the ray-cast
  `IsPointInPath`, `SimplifyPath`, `OffsetPath` and a `CombinePaths` stub
  at `UltraCanvasVectorStorage.cpp:1441-1673` are declared in no header and
  unreachable.
- **No Xara effects, no pages, no object names**, no arrowheads, brushes or
  variable-width strokes. The XAR converter therefore drops feather,
  shadow, bevel, contour, blend, mould, ClipView, multi-stage fills and all
  non-flat transparency on the way in (`XarReader`, header comment at
  `UltraCanvasXARConverter.cpp:367-379`), and its `GetCapabilities()` still
  claims several of them.
- **Renderer parity** (model survey step 3) landed with 0.8.51: arcs and
  smooth quadratics through `PathOps`, object-bounding-box gradients
  against the shape's own extents, fill / stroke opacity, per-span fonts
  and text anchors, clip paths. Still not drawn: patterns, masks, filters,
  blend modes and true group opacity — all waiting on the render-context
  additions of §5.1.
- **It is a plugin that is OFF by default.** `ULTRACANVAS_PLUGIN_VECTOR`
  defaults `OFF` (`CMakeLists.txt:117`), needs vips + zlib + tinyxml2, and
  the CI workflow does not switch it on, so `VectorModelTest`,
  `XARWriterTest` and `VectorFormatsPluginTest` are registered but skipped
  in CI. `UltraCanvas/CMakeLists.txt:43` still declares the option with a
  stray comma in its name. The Bézier proposal's "233 compile errors" is
  stale — it was repaired in 0.3.76 (2026-08-26) — see §3.6 for the check
  made today.

### 3.2 The converters

`IVectorFormatConverter` (`UltraCanvasVectorConverter.h:184-236`) with
`Import*` / `Export*`, `ConversionOptions` (curve tolerance, text and
gradient modes, `WarningCallback`, `ProgressCallback`) and
`FormatCapabilities`. Dispatch is `CreateConverterForExtension` in
`UltraCanvasVectorFormatsPlugin.cpp:41-58`; `CreateGraphics()` there
(`:101-114`) makes an empty one-layer document — the nearest thing to
"New drawing" in the tree.

| | SVG | XAR | EMF | WMF | DXF | DWG | PDF | AI | EPS | CDR |
|---|---|---|---|---|---|---|---|---|---|---|
| read | y | y (uncompressed) | y | y | y | y | n | n | n | n |
| write | y | y | y | y | y | y* | y | y | y | y |

\* DWG write delegates to LibreDWG's `dxf2dwg`.

### 3.3 The viewers

- **`UltraCanvasVectorElement`** (`include/UltraCanvasVectorElement.h`, in core since 0.8.51,
  160 + 368 lines): `SetDocument`, zoom ladder with cursor-anchored smooth
  wheel zoom, drag pan, `ScreenToDocument` / `DocumentToScreen`, layer
  visibility, `VectorInteractionMode::Select` that stores a single element
  id. `Render()` draws background → document → border; the selected id is
  never drawn, there are no handles, no keyboard, no focus. It is the
  read-only analogue of `UltraCanvasPaintSurface`.
- **`UltraCanvasXARElement`** (`Plugins/Vector/XAR/`, 1362 + 3365 lines):
  the element that opens real, compressed Xara files. Its private node tree
  knows 13 fill kinds, multi-stage stops with profiles, 11 transparency mix
  modes, arrows, dashes, pages per spread, and node classes for shadow,
  bevel, contour, blend, mould, ClipView, feather, live effect and brush.
  Shadows render as approximated silhouettes; the other effects are parsed
  and not drawn (`Docs/UltraCanvas/UltraCanvasXARExamples.md`). It does
  not produce a `VectorDocument` and takes no input.
- **`UltraCanvasSVGElement`** keeps the tinyxml2 DOM and re-walks it per
  frame; no structure, no writer, no input. Rasterisation of SVG goes
  through librsvg (`SvgDocumentCairo`).
- **Diagram elements** (`Plugins/Diagrams/`, compiled into core): the only
  editable canvases in the tree. `UltraCanvasDiagramViewport` (shared
  zoom / pan / snap grid / minimap / controls, element-local coordinates)
  is directly reusable. Their editing models are not: rectangles, single
  selection (NodeDiagram: multi-select and marquee), connection ports
  rather than transform handles, and undo only in `UltraCanvasMindMap`
  (JSON snapshot stacks, `UltraCanvasMindMap.h:348-352`).

### 3.4 The rendering layer

One 2D backend, Cairo + Pango, on every platform (`libspecific/Cairo/`;
the `OS/<Platform>/` directories hand Cairo a native surface). What the
`IRenderContext` interface exposes and what a Designer-class editor needs:

| Need | `IRenderContext` today |
|---|---|
| Cubic / quadratic paths, arcs, close, fill rule | y (`MoveTo`, `BezierCurveTo`, `QuadraticCurveTo` → cubic, `Arc`, `ArcTo` tangent form, `SetFillRule`). No SVG elliptical-arc primitive; `PathOps::AppendArc` converts |
| Stroke width / caps / joins / miter / dash + offset | y |
| Solid, linear, radial, elliptical (arbitrary matrix) gradient | y — the elliptical form was added for XAR / SVG `gradientTransform` |
| Conical and mesh gradients | n — `GradientType::Conic` declared, no `Create…` |
| Pattern fill | file path only; `CAIRO_EXTEND_PAD` fixed; no pattern matrix on `IPaintPattern` |
| Full affine transforms, state stack | y; **no CTM readback**, so a hairline stroke or device-space handle size must track the view matrix by hand (what `PaintSurface` and `DiagramViewport` do) |
| Clip to any path | y (`ClipPath()`) |
| Group with opacity, soft mask | **n** — no `cairo_push_group` / `cairo_mask` exposure; `CompositeToSurface` is plain OVER with no alpha |
| Blend modes | **n** — `cairo_set_operator` is not surfaced (Cairo has the whole PDF set) |
| Geometric hit test (`in_fill` / `in_stroke`), stroke extents | **n** |
| Reusable path object | **n** — the path is Cairo's implicit current path, rebuilt every frame |
| Antialias control for geometry | n (text only, as backend statics) |
| Pango / HarfBuzz shaped text, ink + logical extents, attributes | y (`ITextLayout`) |
| Text → outlines | **n** — `cairo_text_path` is used once, inside `StrokeText`; the LaTeX plugin has a FreeType outline walker (`UltraCanvasMathFont.cpp:584-610`) that could be generalised |
| Offscreen surface, pixel readback | y, coarse: an offscreen buffer is a second `IRenderContext`; readback via `UCPixmap` (`UltraCanvasVectorRaster.cpp:200-280`) |
| Blur / shadow / feather at draw time | **n** — only offline through PixelFX (libvips), too slow for a live drag |

Every "n" above is a feature Cairo has and the wrapper does not surface.
None requires a new backend.

### 3.5 Editing precedents worth copying

- `UltraCanvasPaintSurface` (`include/UltraCanvasPaintSurface.h`): the
  hook set `onToolPress / Drag / Release / Hover / DoubleClick / Key`,
  `onDrawOverlay(ctx, viewTransform)`, `onViewChanged`, `onFilesDropped`;
  `PaintViewTransform` with `ImageToView` / `ViewToImage`; zoom ladder,
  space / middle-button pan, `SetToolCursor`. `Apps/UltraPaint/
  UltraPaintTools.h` shows the app-side `PaintTool` base with
  `BuildOptions(panel)` and a `PaintToolContext` of closures.
- `UCRasterDocument`'s undo: labelled entries, rect snapshots for pixel
  edits, whole-structure snapshots for layer operations, a memory budget,
  `onStructureChanged / onStateChanged` hooks (single-slot; the app chains
  them by hand — a real observer list is the improvement to make).
- `UltraCanvasDiagramViewport`: `ScreenToWorld`, `ZoomAtPoint` with eased
  wheel zoom, `DiagramSnapGrid` / `SnapPoint`, `FitView`, minimap.
- `UltraCanvasCurveEditor`: the one element that drags control points on
  a strip; its header names "a colour ramp editor" as the intended next
  caller — the seed of the gradient editor.

### 3.6 Build check made for this document

The plugin was repaired in 0.3.76 (2026-08-26, "compiles again after
storage-model drift"), rebuilt on the `claude/vector-model-unification`
branch on 2026-09-08 with `Tests/VectorModelTest.cpp`, and last edited on
2026-09-14 (XAR shading and winding-rule fixes). CI has never built it: the
workflow passes `-DULTRACANVAS_PLUGIN_XAR=ON` but not
`-DULTRACANVAS_PLUGIN_VECTOR=ON`, so `VectorModelTest`, `XARWriterTest` and
`VectorFormatsPluginTest` register and are skipped. A local build with
`-DULTRACANVAS_PLUGIN_VECTOR=ON -DULTRACANVAS_PLUGIN_XAR=ON -DBUILD_TESTS=ON`
(Ubuntu 24.04, GCC, Ninja) was started for this document; its result is
recorded in §3.7 once it finished.

---

## 4. The proposed shape

Mirror the raster stack one-for-one. The names below are proposals; the
split is the point.

| Raster (exists) | Vector (proposed) | Where |
|---|---|---|
| `UCRasterLayer` / `UCRasterDocument` | `VectorStorage::VectorDocument` (moved to core, §5.2) | `core/DataFormats/` |
| `UCRasterSelection` | `VectorSelection` | core |
| `RasterUndoEntry` + `BeginEdit/EndEdit` | `VectorHistory` | core |
| `UCBrushStroke` / `RasterPaint` | `VectorEditOps` (transform, arrange, node ops), `VectorHitTest`, `UltraCanvasBezierPath` | core |
| `UltraCanvasPaintSurface` | **`UltraCanvasVectorCanvas`** | core element |
| — | `UltraCanvasGradientEditor` | core element |
| `Apps/UltraPaint` | `Apps/ArtCreator` | app |

### 4.1 `UltraCanvasVectorCanvas` (the element)

```cpp
struct VectorViewTransform {           // like PaintViewTransform
    double zoom = 1.0, originX = 0, originY = 0;
    Point2Dd DocToView(const Point2Dd&) const;  Point2Dd ViewToDoc(const Point2Dd&) const;
    Rect2Dd  DocToView(const Rect2Dd&) const;   double   DevicePixelsToDoc(double px) const;
};

struct VectorPointerEvent {            // document coordinates first
    Point2Dd doc, view;  UCMouseButton button;  bool shift, ctrl, alt;
    double pressure;  Point2Dd snappedDoc;  // after guides / grid / object snap
};

class UltraCanvasVectorCanvas : public UltraCanvasUIElement {
public:
    void SetDocument(std::shared_ptr<VectorStorage::VectorDocument>);
    void SetSelection(std::shared_ptr<VectorSelection>);   // drawn as handles
    // view
    void SetZoom(double); void SetZoomAt(double, const Point2Dd& view);
    void ZoomIn(); void ZoomOut(); void ZoomToPage(); void ZoomToDrawing(); void ZoomToSelection();
    void PanBy(double dx, double dy); void SetPage(int index);
    const VectorViewTransform& View() const;
    // workspace
    void SetShowRulers(bool); void SetShowGrid(bool); void SetGrid(const VectorGridSpec&);
    void SetShowGuides(bool); std::vector<VectorGuide>& Guides();
    void SetSnapping(const VectorSnapOptions&);       // grid, guides, object points, angle
    Point2Dd Snap(const Point2Dd& doc, VectorSnapResult* why = nullptr) const;
    void SetShowPageShadow(bool); void SetPasteboardColor(const Color&);
    // hit testing (geometric, tolerance in device pixels)
    VectorHit HitTest(const Point2Dd& doc, double tolerancePx) const;
    std::vector<std::shared_ptr<VectorStorage::VectorElement>> ElementsIn(const Rect2Dd& doc, bool fullyInside) const;
    // the tool hooks, exactly PaintSurface's shape
    std::function<void(const VectorPointerEvent&)> onToolPress, onToolDrag, onToolRelease, onToolHover, onToolDoubleClick;
    std::function<bool(const UCEvent&)> onToolKey;
    std::function<void(IRenderContext*, const VectorViewTransform&)> onDrawOverlay;
    std::function<void()> onViewChanged, onGuidesChanged;
    std::function<void(const std::vector<std::string>&)> onFilesDropped;
    // invalidation in document space
    void InvalidateDoc(const Rect2Dd& doc);
};
```

The element renders: pasteboard, page(s) with shadow, grid, the document
through `VectorRenderer` (with a per-element pixmap cache for effect
objects once those exist), guides, the selection's handles and rotation
centre, then the tool overlay, then rulers. Space / middle-button pan and
wheel zoom are built in, as in `PaintSurface`. It does **not** know what a
tool is, does not mutate the document, and does not own undo.

### 4.2 UI-free companions in core

- **`VectorSelection`** — an ordered set of element pointers with their
  layer, the union bounds in document space, the handle geometry
  (8 scale handles, rotation handles on a second click as in Xara, skew on
  the edges), and a `onChanged` observer list. Node-editing mode selects
  `(path, nodeIndex)` pairs instead.
- **`VectorHistory`** — labelled entries; for a pixel-free model the
  cheap and correct choice is *subtree snapshots*: clone the affected
  elements (`Clone()` exists on all of them) before the edit and store
  before / after, plus whole-document snapshots for structural operations
  (layer add / remove / reorder), under a memory budget and with
  coalescing for drags. Same API as the raster document:
  `BeginEdit(label) / EndEdit() / Undo() / Redo() / CanUndo() / Labels()`.
  Command objects are not needed until scripting, and the MindMap
  precedent shows snapshots hold up.
- **`VectorHitTest`** — geometric fill and stroke tests with a tolerance,
  using `PathOps::NormalizePath` flattening for paths and the analytic
  forms for rects / ellipses / lines; returns element, and for paths the
  segment index and parameter (the pen and shape-editor tools need both).
  Implemented over the model; later accelerated by
  `IRenderContext::IsPointInFill/Stroke` (§5.1).
- **`UltraCanvasBezierPath`** — the node model of the Bézier proposal
  (`BezierNode{anchor, handleIn, handleOut, type}`, `Corner / Smooth /
  Symmetric / Auto`, `InsertNodeAt` by de Casteljau, `HitTestOutline`,
  `ToSVGPathData / FromSVGPathData`) as the *editing view* of a
  `VectorPath`: convert on entering the shape editor, write back on each
  edit. Its `BuildPath(IRenderContext*)` is what the pen and shape tools
  draw their previews with.
- **`VectorEditOps`** — translate / scale / rotate / skew a selection
  about a point (composing `Transform`, or baking into geometry for paths
  when the user asks), z-order, group / ungroup, align / distribute,
  duplicate with offset, convert-to-path, and later the boolean
  operations.

### 4.3 What moves from the Bézier proposal

That document proposed building the path model in core *because* the
Vector plugin did not compile and was off by default. The first reason is
gone; the second is the thing to fix rather than route around. Keep its
model design; drop its "do not build on `VectorStorage::PathData`"
conclusion — the editor edits `VectorPath` through `UltraCanvasBezierPath`
as a transient view, so there is one storage type and one converter matrix.

### 4.4 `UltraCanvasGradientEditor`

A new catalogue element: a horizontal strip with draggable stops, double-
click to add, drag off to delete, a selected-stop colour bound to
`UltraCanvasColorPicker`, and callbacks for `onStopsChanged`. It edits
`std::vector<GradientStop>`; the on-object handles for the fill's start /
end points are the fill tool's overlay in the app. `UltraCanvasCurveEditor`
is the precedent for the interaction, not a base class.

### 4.5 `Apps/ArtCreator`

Shaped like UltraPaint (`Apps/UltraPaint/README.md` is the template):
`main.cpp`, `ArtCreatorWindow.{h,cpp}` (menus, toolbars, tool palette,
right panel with colour / properties / layers, status bar, multi-window
registry), `ArtCreatorTools.{h,cpp}` (a `VectorTool` base with
`OnPress / OnDrag / OnRelease / OnHover / OnKey / DrawOverlay /
BuildOptions`), `ArtCreatorDialogs.{h,cpp}` (New Drawing: page size,
units, orientation; Document Setup; Text; Import), `Docs/ArtCreator/
CHANGELOG.md` (required before CMake configures), `media/appicon/
ArtCreator.{png,svg}`, `media/icons/artcreator/`. Registration points are
the same eleven places UltraPaint touched: `cmake/UltraCanvasVersion.cmake`
(`_ultracanvas_declare_product`), `CMakeLists.txt` (option, Android
default-off block, target block, `WIN32_EXECUTABLE`, `copy_assets`),
`AGENTS.md` changelog table, `scripts/generate_llms_txt.py` `APP_DOC_DIRS`,
`package-linux.sh` `APPS`, `README.md`, `Masterfile_modules.md`.

The app's tools for version 0.1: Selector, Shape editor, Pen, Freehand,
Line, Rectangle, Ellipse, QuickShape (polygon / star), Text, Fill,
Transparency, Zoom, Push. Panels: colour (picker + swatch bar + gradient
editor), object properties (`UltraCanvasFormLayout`), layers, pages.
Menus: File (New / Open / Import / Save / Save As / Export / Print),
Edit (undo history, clipboard, duplicate, select all), Arrange (z-order,
group, align, combine), Utilities (trace bitmap via Vectorizer, options).

---

## 5. Gaps to close, by layer

### 5.1 Rendering layer (`IRenderContext` + `RenderContextCairo`)

In order of leverage. All are surfacing of existing Cairo capability; none
changes the backend model.

1. **Blend modes** — `SetBlendMode(BlendMode)` over `cairo_set_operator`.
   Unlocks the model's `VectorStyle::Blend` and Xara's transparency mixes
   (`Multiply`, `Screen`, `Darken`, `Lighten`, `Hue`, `Saturation`,
   `Luminosity` map directly; `Stained glass` = multiply, `Bleach` =
   screen).
2. **Groups with opacity and masks** — `BeginGroup() / EndGroup(opacity)`
   and `EndGroupAsMask()` over `cairo_push_group / pop_group_to_source /
   cairo_mask`. Unlocks layer opacity, group opacity, `Mask`, and is the
   substrate for feather and shadow.
3. **Geometric hit testing** — `IsPointInFill(x,y)`, `IsPointInStroke(x,y)`,
   `GetStrokeExtents()` over `cairo_in_fill / in_stroke / stroke_extents`.
4. **CTM readback** — `GetTransform()` / `DeviceToUser()`, so handles and
   hairlines can be sized in device pixels without a parallel matrix.
5. **Pattern from a pixmap, pattern matrix, extend mode** on
   `IPaintPattern` — bitmap fills from the document's embedded images,
   and gradients that transform independently of the CTM.
6. **Conic and mesh gradients** — `CreateConicGradientPattern`,
   `CreateMeshGradientPattern(MeshPatch[])` over `cairo_pattern_create_mesh`.
   Xara's conical, diamond, three- and four-colour fills are all mesh
   patterns.
7. **Text to outlines** — `AppendTextLayoutPath(ITextLayout&)` over
   `pango_cairo_layout_path`, for text-on-a-curve preview, convert-to-
   curves and text with gradient / bitmap fills.
8. **Geometry antialias control** and a **retained path handle**
   (`cairo_copy_path` / `append_path`) for large documents; deferred until
   profiling says so.

Draw-time blur (shadow, feather, glow) is the one thing Cairo does not
have. The plan is per-object raster caches: render the object to an
offscreen context at the current zoom, blur with a small separable
box-blur in core (not libvips — the interactive path must not depend on
it, as the brush engine established), and draw the cache until the object
or zoom changes. Xara does exactly this.

### 5.2 Model

1. **Move `VectorStorage` into core** — `UltraCanvasVectorStorage.{h,cpp}`,
   `UltraCanvasVectorRenderer.{h,cpp}`, `UltraCanvasVectorPathOps.h` and
   `UltraCanvasVectorElement` go to `core/DataFormats/` and `core/`, as
   `ModelStorage` already lives in `DataFormats/UltraCanvasModelStorage.h`
   ("core rather than plugin-owned because both the 2D vector converters
   and the 3D model converters resolve ACI" is the precedent in
   `Masterfile_modules.md`). The plugin keeps the converters and their
   tinyxml2 / zlib dependencies. Then a core element can depend on the
   model, the model tests run in CI, and the `Plugins/Vector` option only
   gates file formats. Fix the stray comma in the option name while there.
2. **Renderer parity** — steps 3 and 4 of the model survey: real
   `ArcTo`, `SmoothQuadraticTo`, object-bbox gradients against real
   bounds, fill / stroke opacity, per-span fonts, honest capability flags.
3. **Editing metadata** — `Name` on `VectorElement`; `Locked` and
   `Solid` (edit-only wireframe) on elements as well as layers; per-layer
   `Guide` flag.
4. **Pages** — `VectorDocument::Pages` (size, origin, name) with layers
   spanning pages as Xara does, or a document-per-page with a spread
   wrapper; the XAR plugin already models spreads, so follow it.
5. **Xara effects as first-class element data** — `VectorEffect` variant
   on `VectorElement`: `Shadow{kind, offset, blur, colour, opacity}`,
   `Feather{radius}`, `Bevel{…}`, `Contour{steps, width, …}`,
   `Blend{target, steps, …}`, `Mould{envelope|perspective, 4 or 8 control
   points}`, `ClipView{path}`, `LiveEffect{PixelFX op, params}`; multi-
   stage fills and the 11 transparency mixes on `FillData` /
   `VectorStyle`. This is what turns the XAR converter's "parsed and
   dropped" into a round trip, and lets `XarReader` and the XAR plugin's
   parser converge on one model (survey step 7).
6. **Line gallery** — arrowheads, brush strokes, variable width
   (pressure profile) on `StrokeData`.
7. **Native save format** — see §7.

### 5.3 Editing layer (core)

`VectorSelection`, `VectorHistory`, `VectorHitTest`, `UltraCanvasBezierPath`,
`VectorEditOps`, `UltraCanvasVectorCanvas`, `UltraCanvasGradientEditor`
(§4). Plus a ruler renderer (no ruler element exists; the packet diagram's
`PacketRulerMode` is not one), guides with snapping, object-point snapping
(node, centre, bounding-box corners, path intersection when cheap), and
angle constraint. Later: path booleans (a real clipper — evaluate a
vendored polygon-clipping library against `Docs/Dependencies.md` policy
before writing one), offsetting for contour, and text on path.

### 5.4 Application

Tools, panels, dialogs, menus, import / export UI, printing (via the PDF
writer and the OS print dialog), icons, changelog, docs, packaging.

---

## 6. Phased plan

Calibration: UltraPaint went from nothing to 0.2.4 in a week with about
3 000 framework lines and 4 850 application lines. The vector stack starts
further ahead on the model and further behind on the render context.

| Phase | Deliverable | Rough size | Depends on |
|---|---|---|---|
| **0. Make the model first-class** | `VectorStorage` + renderer + `VectorElement` into core; option comma fix; model tests in default CI; renderer parity; honest capabilities | 1–2 k lines moved, ~600 changed | — |
| **1. Render context** | blend modes, groups with opacity / mask, `IsPointInFill/Stroke`, stroke extents, CTM readback, pattern-from-pixmap + matrix, conic / mesh gradients, text path | ~900 lines + tests | — |
| **2. Core editing layer** | `VectorSelection`, `VectorHistory`, `VectorHitTest`, `UltraCanvasBezierPath`, `VectorEditOps` (transform, arrange, group, align), `UltraCanvasVectorCanvas` with rulers / guides / grid / snap / handles, `UltraCanvasGradientEditor`; catalogue entries, docs, a DemoApp page | ~5 k lines | 0, 1 (parts) |
| **3. ArtCreator 0.1** | the 13 tools of §4.5, panels, layers, pages UI (single page), import via converters, save as SVG, export via the matrix and `UltraCanvasVectorRaster` | ~5–6 k lines | 2 |
| **4. Xara-class effects** — *done in 0.8.84 / ArtCreator 0.2.0* | effects in the model, per-object raster caches with blur, shadow / feather / transparency tools, multi-stage fills, XAR round trip of effects, unify the two XAR readers (the converter now reads through the XAR plugin's `XARDocument`), arrowheads / brushes / variable width (arrowheads and width profiles bake into XAR as shapes; brushes render but are not written) | ~4 k lines | 1, 3 |
| **5. Depth** | bevel, contour (offsetting), blend, mould, ClipView tool, booleans, text on path and text areas, pages and spreads, colour gallery with linked shades, symbols, photo tool over the raster bridge, live effects via PixelFX, trace-to-vector | open-ended | 4 |

Phases 0 and 1 are independent and small; they are also useful on their
own (every diagram and chart gets blend modes and group opacity). Phase 2
is the answer to the question this document was asked: it is the public
element, and it ships with a demo page before any application uses it.

---

## 7. Open decisions

1. **Name.** `UltraCanvasVectorCanvas` is the name proposed with the
   request; the existing precedent is `UltraCanvasPaintSurface`
   ("surface" = the element that *edits*, "viewer" / "element" = the ones
   that show). `UltraCanvasVectorSurface` would match; `VectorCanvas`
   reads better to a newcomer. This document uses `VectorCanvas`.
2. **Native file format.** Three candidates: (a) **SVG** — lossless
   against the model today, universally readable, but cannot carry the
   phase-4 effects except as private namespaced attributes; (b) **XAR** —
   Xara itself reads it, the writer exists, and once the effects are in
   the model it carries them natively; compressed reading still has to
   come out of the viewer plugin; (c) the specified-but-unimplemented
   **UCD v2 `UCVector` section** (`Docs/UltraCanvas/UCD-FileFormat-v2.md`),
   a container that can also hold the raster layers and thumbnails.
   Proposal: SVG for 0.1, XAR as the interchange target, UCD when effects
   land — decide before phase 4. **Decided with phase 4: XAR is the native
   format** (ArtCreator 0.2.0 saves it first); it carries the effects,
   multistage fills and transparency ramps natively and Xara opens it.
   SVG stays the interchange format for shapes, fills and text. UCD is
   not needed for the drawing alone; it remains the option if raster
   layers and thumbnails join the file.
3. **Where the XAR viewer goes.** Once the model holds Xara's effects and
   the converter reads compressed files, `UltraCanvasXARElement` is a
   second renderer of the same data. Keep it as the fidelity reference
   (`XARFidelityTest` compares against Designer Pro exports) or retire it.
   *After phase 4:* the reader is shared (the converter translates the
   plugin's node tree), so the only duplication left is the viewer's own
   renderer; it stays as the fidelity reference for now.
4. **Path booleans.** Write a clipper or vendor one. The dependency policy
   (`Docs/Dependencies.md`) and licence record decide; a Vatti / Martinez
   implementation is a bounded piece of work if vendoring is refused.
5. **Multi-document.** UltraPaint opens one document per window;
   `UltraCanvasTabbedContainer` supports tear-off tabs. Xara uses tabs.

---

## 8. Non-goals

- A second vector model, or a private one inside the application.
- GPU rendering. Cairo is the single backend; the plan surfaces what it
  has. If large documents need more, a retained path handle and per-object
  caches come first.
- Web / HTML export, animation, presentations, Xara's web-designer half.
- Replacing the Vectorizer, the raster editor, or the chart and diagram
  plugins with the new element. They may adopt it later; nothing here
  requires that.
