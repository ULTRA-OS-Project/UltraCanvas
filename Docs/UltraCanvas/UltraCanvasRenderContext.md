# UltraCanvasRenderContext — compositing, hit testing and paint sources

<!-- doc-check: void DrawContent(IRenderContext* ctx); -->

`IRenderContext` (`UltraCanvasRenderContext.h`) is the drawing interface every
element renders through. Its everyday surface — paths, fills, strokes, text
layouts, pixmaps, state and transforms — is used throughout the element
docs. This page documents the part added for vector editing in 0.8.51:
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

<!-- doc-check: void DrawContent(IRenderContext* ctx); -->

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

### Crisp borders

A stroke is centred on its path, so a 1px outline along a rectangle with
whole-pixel edges lies half outside it and is antialiased over two rows of
pixels on each side. `DrawFilledRectangle` and `DrawFilledCircle` therefore
inset their path by half the border width: the outline sits on whole
pixels, its outer edge is the rectangle's edge (or the circle's radius),
and the centre does not move. A rounded corner keeps its outer radius. To
stroke a path of your own the same way:

```cpp
Rect2Dd path = IRenderContext::InsetForStroke(rect, strokeWidth);
ctx->Rect(path.x, path.y, path.width, path.height);
ctx->SetStrokeWidth(strokeWidth);
ctx->Stroke();
```

## Wrapping

```cpp
auto layout = ctx->CreateTextLayout(text, false);
layout->SetExplicitWidth(300);              // wraps to 300 px
layout->SetWrap(TextWrap::WrapWord);        // only to change the default
```

Every text layout wraps the same way unless told otherwise, whether it came
from `CreateTextLayout` or from `DrawText` / `DrawTextInRect` (whose mode is
`TextStyle::wrap`): at word boundaries first, and between two characters
where a word alone is wider than the line (`TextWrap::WrapWordChar`). A URL,
a file path or a hash therefore stays inside the width it was given. A
layout from `CreateTextLayout` used to wrap at words only (Pango's own
default), so such a word ran on past its width - a tooltip drew a tracking
link over its own border (framework changelog, "New text layouts wrap a long
word between characters"). `WrapWord` keeps a long word whole (it overflows); `WrapChar`
breaks anywhere; `WrapNone` keeps one line per paragraph and ellipsizes it.

## Centring text on its capitals

```cpp
double GetCapCentreOffset(const FontStyle& font);                    // layout top → middle of a capital
int    TextTopCentredOnCaps(const Rect2Dd& row, const FontStyle& font);
double ITextLayout::GetCapHeight();                                  // baseline → cap top, pixels
```

A line box holds the ascender and descender space, so centring it (or the
font's ascent + descent band) puts a mixed-case label a shade below a box
or icon centred beside it. The framework centres single-line text on the
middle of a capital letter instead: half-way between the cap top and the
baseline. A layout drawn with `VerticalAlignment::Middle` into a box of
known height does this by itself, which covers `DrawTextInRect` and so
buttons, list cells, dropdowns and tabs. Text placed at a point with
`DrawText` gets the same line from `TextTopCentredOnCaps`:

```cpp
ctx->SetFontStyle(font);
int y = ctx->TextTopCentredOnCaps(rowRect, font);   // same centre line as a box centred on rowRect
ctx->DrawText(label, Point2Di(x, y));
```

The cap height is measured once per font from the ink of a capital H and
cached, so asking per row costs a map lookup. The caches belong to the
context (and, for layouts, to its Pango context), not to the process: two
contexts can measure the same font differently, and a backend calls
`InvalidateFontMetricsCache()` on a context whenever its resolution, font
options, hinting or device scale change, so nothing measured under the old
settings survives them. Callers never need to call it. A font with no
measurable ink falls back to the line box's middle.

### The line box a caret or selection covers

```cpp
double GetLineBoxHeight(const FontStyle& font);   // ascent + descent, fractional pixels
```

A caret or a selection band that should cover a line's glyphs needs the
font's line height as `DrawText` draws it. `GetTextLineHeight` returns whole
pixels with the fraction cut off, so a box that tall ends up to a pixel above
the descenders and the bottoms of parentheses. `GetLineBoxHeight` keeps the
fraction, and is measured once per font and cached like the cap height
(`InvalidateFontMetricsCache` clears it too). Round such a box outwards when
it becomes pixels - top down, bottom up - so it covers every row the glyphs
touch:

```cpp
const double top = ctx->TextTopCentredOnCaps(textArea, font);
const double bottom = top + ctx->GetLineBoxHeight(font);
Rect2Di caret(x, static_cast<int>(std::floor(top)), 1,
              static_cast<int>(std::ceil(bottom)) - static_cast<int>(std::floor(top)));
```

`UltraCanvasTextInput` places its caret and selection this way.

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

## Putting a surface on another

```cpp
void FlushToSurface(NativeSurfacePtr target, const Point2Dd& pos);
void FlushToSurfaceWithOpacity(NativeSurfacePtr target, const Point2Dd& pos, double opacity);
void CompositeToSurface(NativeSurfacePtr target, const Point2Dd& pos);
void CompositeToSurfaceWithOpacity(NativeSurfacePtr target, const Point2Dd& pos, double opacity);
```

A context's surface goes onto another surface (a window's, a pixmap's) in
one of two ways. `FlushToSurface` copies it: every pixel, transparent ones
included, replaces what is there. `CompositeToSurface` blends it over: a
transparent pixel leaves the destination as it was, a translucent one is
mixed with it. Each has an `opacity` form for fading: the copy mixes each
destination pixel towards the source's (`FlushToSurfaceWithOpacity`, which
fades transparent pixels in as transparent), the blend scales the source's
alpha (`CompositeToSurfaceWithOpacity`, which leaves the destination under
transparent pixels alone at every step). A window puts its popups on with
`CompositeToSurfaceWithOpacity`, so a popup with rounded corners - a menu -
shows the window behind its corners while it fades in and after.

## Tests

`Tests/RenderContextTest.cpp` (CTest `RenderContextTest`) exercises every
method above on an offscreen surface and samples the pixels back, so it
runs without a display.
