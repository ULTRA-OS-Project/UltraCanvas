# UltraCanvasRenderContext — compositing, hit testing and paint sources

`IRenderContext` (`UltraCanvasRenderContext.h`) is the drawing interface every
element renders through. Its everyday surface — paths, fills, strokes, text
layouts, pixmaps, state and transforms — is used throughout the element
docs. This page documents the part added for vector editing in 0.8.50:
blend modes, groups and masks, geometric hit testing, transform readback,
conic / mesh / pixmap paint sources, pattern placement, antialias control
and text outlines. One backend implements all of it (Cairo, on every
platform); every method has a base-class default so a backend that lacks a
feature still compiles and degrades predictably.

Coordinates are user space (the element's local space inside `Render()`),
angles are radians, and nothing here changes the coordinate contract in
[UltraCanvasCoordinateSystemGuide](UltraCanvasCoordinateSystemGuide.md).

## Blend modes

```cpp
enum class BlendMode { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge,
                       ColorBurn, HardLight, SoftLight, Difference, Exclusion,
                       Hue, Saturation, Color, Luminosity };
void      SetBlendMode(BlendMode mode);   // until PopState() or the next call
BlendMode GetBlendMode() const;
```

The PDF / CSS / SVG set. It applies to every fill, stroke, text, image and
group paint that follows, and `PushState()` / `PopState()` save and restore
it with the rest of the state.

```cpp
ctx->SetFillPaint(Color(255, 255, 0));   ctx->FillRectangle(a);
ctx->SetBlendMode(BlendMode::Multiply);
ctx->SetFillPaint(Color(0, 255, 255));   ctx->FillRectangle(b);   // overlap is green
```

Xara's transparency mixes map directly: *Stained glass* is `Multiply`,
*Bleach* is `Screen`, the rest keep their names.

## Groups and masks

```cpp
void BeginGroup();
void EndGroup(double opacity = 1.0);
std::shared_ptr<IPaintPattern> EndGroupAsPattern();
void EndGroupMasked(std::shared_ptr<IPaintPattern> mask);
void PaintPattern(std::shared_ptr<IPaintPattern> pattern, double opacity = 1.0);
```

Everything drawn between `BeginGroup()` and the matching `End…` call goes
to an intermediate surface and is composited **as one**:

- `EndGroup(opacity)` paints it with a uniform alpha, so two overlapping
  children at 50 % do not stack to 75 % — the correct semantics for a
  layer's or a group's opacity — under the blend mode in force at
  `BeginGroup()`.
- `EndGroupAsPattern()` returns the rendered group as a paint source
  instead of painting it: a mask, a pattern fill made of other content, or
  the input of a cached effect.
- `EndGroupMasked(mask)` paints the group through the alpha of `mask`,
  which is usually a pattern from `EndGroupAsPattern()`.
- `PaintPattern(pattern, opacity)` paints any pattern over the current
  clip — the way to draw a captured group again.

Groups nest, and each is a `PushState()` / `PopState()` pair from the
caller's side. A backend without groups draws straight through: `EndGroup`
and `EndGroupMasked` paint nothing extra and `EndGroupAsPattern` returns
`nullptr`.

```cpp
// Layer at 40 %: children composite normally inside, the result fades once.
ctx->BeginGroup();
for (auto& child : layer.Children) child->Render(ctx, dirty);
ctx->EndGroup(0.4);

// A soft-edged clip: whatever is opaque in the mask shows.
ctx->BeginGroup();
ctx->SetFillPaint(maskGradient); ctx->FillRectangle(bounds);
auto mask = ctx->EndGroupAsPattern();
ctx->BeginGroup();
DrawContent(ctx);
ctx->EndGroupMasked(mask);
```

## Geometric hit testing

```cpp
bool    IsPointInFill(double x, double y);     // inside the current path's fill
bool    IsPointInStroke(double x, double y);   // under its stroke
Rect2Dd GetStrokeExtents();                    // path extents grown by the stroke
```

All three consult the **current path** with the current fill rule, stroke
width, caps, joins and dashes, so a selector tool builds the shape's path
exactly as the renderer does and asks. `GetPathExtents()` remains the
outline's box without the stroke. The base answers `false` and the path
extents.

```cpp
ctx->Rect(10, 10, 20, 20);
ctx->SetStrokeWidth(6);
bool inside = ctx->IsPointInFill(15, 15);    // true
bool onEdge = ctx->IsPointInStroke(8, 20);   // true: within 3 of the edge
ctx->ClearPath();
```

## Transform readback

```cpp
void     GetTransform(double& a, double& b, double& c, double& d, double& e, double& f) const;
Point2Dd UserToDevice(const Point2Dd& p) const;
Point2Dd DeviceToUser(const Point2Dd& p) const;
double   DeviceToUserDistance(double devicePixels) const;
```

The current transformation matrix in the layout `SetTransform` takes
(`x' = a·x + c·y + e`, `y' = b·x + d·y + f`). `DeviceToUserDistance(1)` is
the length of one device pixel in user units — what a hairline stroke or a
handle sized in pixels needs under a zoomed view — averaged over the two
axes when the scale is not uniform. The base returns the identity.

## Paint sources

```cpp
std::shared_ptr<IPaintPattern> CreateConicGradientPattern(double cx, double cy,
        double startAngle, double endAngle, const std::vector<GradientStop>& stops);
std::shared_ptr<IPaintPattern> CreateMeshGradientPattern(const std::vector<MeshGradientPatch>& patches);
std::shared_ptr<IPaintPattern> CreatePixmapPattern(UCPixmap& pixmap, const Rect2Dd& anchorRect,
        PatternExtend extend = PatternExtend::Pad);
```

joining the existing linear, radial, elliptical and file-backed image
patterns.

- **Conic** — the stops run round the centre from `startAngle` to
  `endAngle` (a full turn when they are equal). Cairo has no conic
  primitive; the backend builds a fan of mesh patches, one per 15° and
  one per stop boundary, so hard stops stay hard. The pattern is
  transparent beyond a very large radius rather than infinite.
- **Mesh** — any number of Coons patches. `MeshGradientPatch` holds four
  corners in order round the patch (repeat one for a triangle), two
  control points per side in the same order (used when `hasControls` is
  set, straight sides otherwise) and a colour per corner. Xara's diamond,
  three- and four-colour fills and SVG 2 mesh gradients are made of these.
- **Pixmap** — an image already in memory (a decoded picture, a rendered
  group read back, a raster layer) stretched to `anchorRect`, extending
  outside it by `extend`; it follows `SetImageSmoothing` for its filter.

### Placing a pattern

```cpp
// No enumerator may be named None: X11's Xlib.h defines None as a macro
    // and this header is reachable from platform code that includes X11.
    enum class PatternExtend { Pad, Repeat, Reflect, NoExtend };
void IPaintPattern::SetMatrix(double a, double b, double c, double d, double e, double f);
void IPaintPattern::SetExtend(PatternExtend extend);
```

`SetMatrix` maps pattern space (the coordinates the pattern was created
in) into user space — SVG's `gradientTransform` / `patternTransform` — on
top of the CTM in force when the pattern is painted. `SetExtend` chooses
what shows beyond the pattern's own extent: the edge colour, tiles,
mirrored tiles, or nothing. Both are no-ops on a backend without pattern
matrices.

## Antialiasing

```cpp
enum class AntialiasMode { DefaultQuality, NoAntialias, Gray, Subpixel, Fast, Good, Best };
void SetAntialias(AntialiasMode mode);
```

Geometry antialiasing for the fills and strokes that follow (text keeps
its own hinting settings). `NoAntialias` is what a pixel-exact tool wants.

## Text outlines

```cpp
void AppendTextPath(const std::string& text, const Point2Dd& pos);
void AppendTextLayoutPath(ITextLayout& layout, const Point2Dd& pos);
```

Appends the glyph outlines of `text` in the current font — or of a
prepared layout — to the current path, with the layout origin at `pos`
exactly as `DrawText` / `DrawTextLayout` would place it. The text is then
geometry: fill it with a gradient or a pixmap pattern, stroke it, clip to
it, hit-test it, or read the path back to convert text to curves.

```cpp
ctx->SetFontFace("Sans", FontWeight::Bold, FontSlant::Normal);
ctx->SetFontSize(48);
ctx->AppendTextPath("Vector", Point2Dd(20, 20));
ctx->SetFillPaint(ctx->CreateLinearGradientPattern(20, 0, 220, 0, stops));
ctx->FillPathPreserve();
ctx->SetStrokePaint(Colors::Black); ctx->SetStrokeWidth(1);
ctx->StrokePathPreserve();
ctx->ClearPath();
```

## Tests

`Tests/RenderContextTest.cpp` (CTest `RenderContextTest`) exercises every
method above on an offscreen surface and samples the pixels back, so it
runs without a display.
