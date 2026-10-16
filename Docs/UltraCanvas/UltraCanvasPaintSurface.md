# UltraCanvas Raster Editing: PaintSurface, RasterDocument, Brush Engine

<!-- doc-check: void RebuildLayerPanel(); void UpdateUndoButtons(); std::shared_ptr<UCRasterLayer> layer; std::shared_ptr<UltraCanvasPaintSurface> surface; std::shared_ptr<UltraCanvasRichTextEdit> editor; int layerIndex; -->

## Overview

The **raster-editing layer** is what a bitmap editor needs on top of the
image pipeline: an editable pixel model with layers, a selection, undo, a
brush engine, and an element that displays and edits it. PixelFX (libvips)
remains the engine for whole-image work — filters, colour adjustments,
resampling, file formats — and the pieces below hand pixels to it and take
them back. The first application built on it is **UltraPaint**
(`Apps/UltraPaint`); the gap analysis that led to these classes is in
[`Docs/UltraPaint/FeatureGapAnalysis.md`](../UltraPaint/FeatureGapAnalysis.md).

| Class | What it is | Files |
|---|---|---|
| `UCRasterLayer` | One straight-RGBA 8-bit pixel buffer with name / visibility / lock / opacity / blend mode; blend arithmetic; compositing onto a premultiplied pixmap; PixelFX round trip | `include/UltraCanvasRasterLayer.h`, `core/UltraCanvasRasterLayer.cpp` |
| `UCRasterSelection` | Soft coverage mask (0..255 per pixel): rectangle, ellipse, polygon, arbitrary mask; replace / add / subtract / intersect; feather, grow, shrink, invert; marching-ants outline | `include/UltraCanvasRasterSelection.h`, `core/UltraCanvasRasterSelection.cpp` |
| `UCRasterDocument` | The layer stack of one canvas: layer operations, whole-image geometry, pixel-edit brackets and structural snapshots for undo / redo, selection-aware filter application, cached composite pixmap, file load / save, layered `.ucraster` projects | `include/UltraCanvasRasterDocument.h`, `core/UltraCanvasRasterDocument.cpp` |
| `RasterTextContent`, `RenderTextLayer` | Text layers: words kept as a rich document in a box on the canvas (`UCRasterLayer::text`), drawn into the layer through the WYSIWYG element that also edits them on the canvas, editable until the layer is merged or rasterized | `include/UltraCanvasRasterText.h`, `core/UltraCanvasRasterText.cpp` |
| `UCBrushStroke`, `RasterPaint` | The brush engine: interactive strokes (dabs with spacing, hardness, opacity vs flow, paint / erase / clone / smudge / dodge / burn) and one-shot operations (anti-aliased shapes, flood fill, magic wand, gradients, mask stamping, eyedropper) | `include/UltraCanvasBrushEngine.h`, `core/UltraCanvasBrushEngine.cpp` |
| `UltraCanvasPaintSurface` | The element: zoom / pan view of a document with checkerboard, pixel grid, selection ants and brush cursor; forwards pointer events in image coordinates to the tool the host installs | `include/UltraCanvasPaintSurface.h`, `core/UltraCanvasPaintSurface.cpp` |

**Version**: 1.1.0 - text layers
**Last Modified**: 2026-10-15
**Author**: UltraCanvas Framework

None of the model classes touch a window; `UltraCanvasPaintSurface` is the
only element. A batch tool can open a document, run a stroke or a filter and
save it with no display at all.

## Pixel conventions

- A layer stores **straight (non-premultiplied) RGBA**, one byte per channel,
  `R G B A` order, row-major, no row padding — exactly what libvips uses for
  a 4-band `uchar` sRGB image, so `UCRasterLayer::ToPixelFX()` is a memory
  copy and `FromPixelFX()` the reverse (with any needed cast, band trim and
  alpha fill).
- Compositing to the screen converts once, in `CompositeOnto()`, into
  Cairo's premultiplied ARGB32 (`UCPixmap`). The document caches that
  pixmap and re-composites only the rectangles that changed.
- Blend modes (`RasterBlendMode`: Normal, Multiply, Screen, Overlay,
  Darken, Lighten, Difference, Addition, Subtract, Soft Light, Hard Light)
  use the separable W3C / Photoshop formulas on straight RGB, then the usual
  alpha "over". `RasterBlendPixel()` exposes the same arithmetic to the brush
  engine so a brush painting in Multiply matches a layer set to Multiply.
  `RasterReplacePixel()` is the other way to put a colour down: it moves the
  pixel toward the colour, alpha included, so a transparent colour clears it
  where "over" would leave it as it was.

## The document

```cpp
#include "UltraCanvasRasterDocument.h"

auto doc = std::make_shared<UCRasterDocument>(1024, 768, RasterPixel(255, 255, 255, 255));
doc->AddLayer("Sketch");                       // transparent, above the active layer
doc->GetSelection().SetEllipse(Rect2Di(100, 100, 400, 300));
doc->CommitSelectionChange("Ellipse Select");  // undoable, notifies views

// A filter over the active layer, written back only inside the selection
doc->ApplyFilter("Gaussian Blur", [](const PixelFX::PFXImage& img) {
    return PixelFX::Convolution::GaussianBlur(img, 4.0);
});

// Turn one colour into transparency. The distance metric is the one the
// magic wand and the flood fill use, so a tolerance means the same thing in
// all three; the last two arguments are how transparent the colour becomes
// (1.0 = gone, 0.4 = 40% of its opacity taken away) and whether to un-mix it
// out of the part-transparent edge pixels.
doc->ApplyFilter("Colour to Alpha", [](const PixelFX::PFXImage& img) {
    return PixelFX::Colour::ColourToAlpha(img, {255, 255, 255}, 12, 32, 1.0, true);
});

doc->Undo();
std::string err;
doc->SaveToFile("out.png", err);               // flattened, format by extension, with the
                                               // user's PNG save settings once they set any
doc->SaveProject("work.ucraster", err);        // layers kept
```

### Undo

Two kinds of history entry keep memory in check:

- **Pixel edits** snapshot one rectangle of one layer before and after.
  Bracket a change with `BeginEdit(label, layerIndex, rect)` … `EndEdit()`,
  or, when the extent is only known at the end (a brush stroke), keep a
  copy of the layer yourself and call `RecordEdit(label, layerIndex, rect,
  before)` — `UCBrushStroke` hands out exactly that copy through
  `GetBefore()`.
- **Structural edits** (add / remove / move / merge layers, canvas size,
  scale, flip, rotate, selection changes, layer attributes) record the
  layer *list* before and after. Structural operations never modify a layer
  object in place — they replace it — so unchanged layers are shared
  between snapshots and cost nothing.

`SetUndoMemoryLimit()` bounds the history; the oldest entries go first.

A selection belongs to the canvas it was made on. The whole-image geometry
operations (`CropTo`, `ScaleImage`, `ResizeCanvas`, `Rotate90`) re-shape it,
which is a selection change like any other: they fire `onSelectionChanged()`
and move the history's committed baseline with them. Undo and redo drop a
recorded selection whose size does not match the canvas being restored,
rather than installing a mask of the wrong shape.

### Notifications

| Callback | Fired when |
|---|---|
| `onPixelsChanged(rect)` | pixels of `rect` changed (a stroke calls it per dab) |
| `onStructureChanged()` | layer list, attributes, active layer or canvas size changed |
| `onSelectionChanged()` | the selection changed, including when a canvas-geometry change dropped it |
| `onStateChanged()` | modified flag or undo / redo availability changed |

Those four are single slots, for the one-observer case. Several observers -
an editing view, a magnifying glass on the same document, the window's
panels - each add a **listener** of their own and remove it when they let
the document go, so none has to chain itself behind another's slot:

```cpp
UCRasterDocument::Listener l;
l.pixelsChanged    = [&](const Rect2Di& r) { view->InvalidateRect(r); };
l.structureChanged = [&]() { RebuildLayerPanel(); };
l.selectionChanged = [&]() { /* ... */ };
l.stateChanged     = [&]() { UpdateUndoButtons(); };
const UCRasterDocument::ListenerId id = doc->AddListener(std::move(l));
// ...
doc->RemoveListener(id);    // an id that is not listening is ignored
```

Every notification fires the slot first, then every listener, on the list
as it was when the notification began - a callback may remove its own
listener. `UltraCanvasPaintSurface::SetDocument()` adds the surface's
listener and `SetDocument(nullptr)` or the surface's destruction removes
it; `UltraPaintWindow::SetDocument` adds the window's beside it.

## The brush engine

```cpp
#include "UltraCanvasBrushEngine.h"

UCBrushSettings s;
s.size = 24; s.hardness = 0.7f; s.opacity = 0.8f; s.flow = 1.0f; s.spacing = 0.15f;

UCBrushStroke stroke;
stroke.Begin(layer, &doc->GetSelection(), s, BrushMode::Paint, RasterPixel(200, 30, 30, 255));
for (auto& p : pointerPath) {
    Rect2Di changed = stroke.AddPoint(p.x, p.y, p.pressure);
    if (changed.width > 0) doc->NotifyChanged(changed);
}
doc->RecordEdit("Paintbrush", doc->GetActiveLayerIndex(), stroke.GetDirtyBounds(), *stroke.GetBefore());
stroke.End();
```

- **Opacity vs flow.** `flow` is what each dab deposits; `opacity` caps the
  whole stroke. Overlapping dabs of one stroke never exceed `opacity`
  because the stroke keeps a coverage mask and re-composites from the
  "before" copy — the Photoshop convention.
- **Spacing** is a fraction of the size; dabs are laid along the pointer
  path at that interval with the remainder carried across segments, so a
  fast stroke has the same density as a slow one.
- **Modes:** `Paint` (blend mode applies), `Erase` (alpha), `Clone`
  (`SetCloneSource(layer, dx, dy)`), `Smudge` (carries colour), `Dodge`,
  `Burn`.
- Everything is clipped to the layer and scaled by the selection's coverage.

`RasterPaint` holds the one-shot operations: `DrawLine`, `DrawRectangle`,
`DrawEllipse`, `DrawPolygon` (outline and fill, 4×4 supersampled
anti-aliasing through `FillCoverage`; open, it is a polyline whose joins are
painted once), `DrawCurve` (a cubic Bezier curve; `FlattenCubicBezier` gives
its points for a preview), `DrawArrow` (a shaft with a filled head at the
tip, or at both ends, in one pass; `ArrowShape` gives its geometry),
`FloodFill` (tolerance, contiguous or
global, optionally sampling a merged layer; `FillCompositing::Replace` sets
the region to the colour, alpha included, where the default `Blend` paints
over it - the way to fill with transparency), `MagicWandMask` (the same
region test as a coverage map for `UCRasterSelection::SetMask`),
`FillGradient` (linear / radial / reflected, interpolated in premultiplied
space), `StampMask` (a coverage map in a colour, such as a
`PixelFX::Generate::Text` mask), `SampleColour`.

None of this needs libvips.

## Text layers

A bitmap editor's text should stay text until the user says otherwise: the
words re-editable, the font, size and colour changeable, the box movable -
and only then pixels. A **text layer** is a `UCRasterLayer` that carries the
words it shows, `UCRasterLayer::text`, a `RasterTextContent`:

| Field | What it is |
|---|---|
| `document` | the words, as a `UCRichDocument`: runs with their own font, size, colour, bold / italic / underline; paragraphs with their alignment. Shared and immutable - an edit makes a new one |
| `x`, `y`, `width` | the box on the canvas, in pixels: its corner and the width the lines wrap at |
| `height` | what the text took when it was last laid out (`RenderTextLayer` fills it in) |
| `fontFamily`, `fontSizePx`, `colour` | what a run without a font, size or colour of its own is set in |

The layer's pixels are a rendering of that, so everything that only reads
pixels - compositing, blend modes, opacity, `Flatten`, saving a PNG -
works on a text layer unchanged.

### Drawing one

`RenderTextLayer` (`UltraCanvasRasterText.h`) lays the words out and draws
them into a new canvas-sized layer:

```cpp
#include "UltraCanvasRasterText.h"
#include "UltraCanvasRasterDocument.h"

auto doc = std::make_shared<UCRasterDocument>(1024, 768, RasterPixel(255, 255, 255, 255));

auto words = std::make_shared<UCRichDocument>();
RichDocBlock paragraph;
RichTextRun run;
run.text = "Hello";
run.bold = true;
paragraph.runs.push_back(run);
paragraph.align = RichTextAlign::Center;
words->blocks.push_back(paragraph);

RasterTextContent text;
text.document = words;
text.x = 40;
text.y = 30;
text.width = 400;                      // the lines wrap at 400 px, centred in them
text.fontFamily = "Sans";
text.fontSizePx = 48;
text.colour = RasterPixel(20, 20, 20, 255);

std::string error;
if (auto layer = RenderTextLayer(text, doc->GetWidth(), doc->GetHeight(), error)) {
    doc->AddLayer(layer, -1, "Text");  // above the active layer, one undo step
}
```

It draws through `UltraCanvasRichTextEdit` - the framework's WYSIWYG element
- offscreen at 1:1, with the style `TextLayerEditStyle` gives it (no
background, border, padding or scrolling; the content's default font, size
and colour). The box's corner is put on whole pixels. The layer's copy of the
document has its paragraph spacing **stated** (the gaps it was laid out with,
written out), so it reads back from a file laid out exactly the same.

### Editing one on the canvas

Because the renderer *is* the editing element, the element can edit the
words on the canvas and show exactly what the layer will hold. It lays text
out in its own document space and draws its zoom as a scale, so an editor at
the canvas's zoom puts every glyph where the layer's pixels will be - at any
zoom (`SetZoomRange` lifts its 0.25-5 limit for this):

```cpp
#include "UltraCanvasRasterText.h"
#include "UltraCanvasPaintSurface.h"

auto doc = surface->GetDocument();
PrepareTextLayerEditor(*editor, *layer->text, surface->GetZoom());
editor->SetDocument(std::make_shared<UCRichDocument>(*layer->text->document));
doc->SetInPlaceEditLayer(layer);       // its own pixels would show under the editor
// Place the editor over the box: its corner at surface->ImageToView(box.x, box.y),
// box.width * zoom wide, at least editor->GetContentHeight() * zoom tall.

// ... the user types; then, to commit:
RasterTextContent edited = *layer->text;
edited.document = std::make_shared<UCRichDocument>(*editor->GetDocument());
doc->SetInPlaceEditLayer(nullptr);
std::string error;
if (auto redrawn = RenderTextLayerLike(*layer, edited, error)) {
    doc->ReplaceLayer(layerIndex, redrawn, "Edit Text");   // one undo step
}
```

`SetInPlaceEditLayer` leaves the layer out of the composite the views show
(not out of `Flatten` or saving) while the host draws it itself.
`RenderTextLayerLike` keeps the old layer's name, visibility, lock, opacity
and blend mode. UltraPaint's Text tool is this, with the editor in a frame the
size of the canvas so it is cut off where the box runs past the view.

### When it stops being text

A text layer stays text through everything that keeps its pixels as the text
draws them, and becomes a bitmap at the first change that does not:

| Stays text | Becomes a bitmap (the text is taken off) |
|---|---|
| attributes (name, opacity, blend, visibility, lock), moving it in the stack, `DuplicateLayer` | a pixel edit on it - `BeginEdit` / `EndEdit`, `RecordEdit`, `ApplyFilter`, `FillSelection`, `DeleteSelection` - whose **undo makes it text again** |
| `ResizeCanvas` and `CropTo` - the box moves with the pixels | `MergeLayerDown` (onto or into it), `FlattenImage` |
| `ReplaceLayer` with a layer drawn again (`RenderTextLayerLike`) | `ScaleImage`, `FlipHorizontal` / `FlipVertical`, `Rotate90` / `Rotate180` |
| | `RasterizeLayer(index)` - the explicit way, one undo step |

`CopySelection` hands out pixels, never a text layer. A pixel edit takes the
text off in place and keeps it in its undo entry
(`RasterUndoEntry::textBefore`); an edit that changes nothing, or is
cancelled, leaves the text where it was. An editor asks before a tool paints
on a text layer; the model does not rely on it.

### In a project

`SaveProject` keeps a text layer as text: its PNG as for any layer, its box
and default font in `document.json`, and the words beside the PNG as
`layers/<n>.odt` - an OpenDocument text, which keeps every run's font, size
and colour and the paragraphs' alignment and spacing, and which any word
processor opens. `LoadProject` shows the PNG (so a font missing on another
machine changes nothing until the text is edited) and makes the layer text
again; a project whose text cannot be read still opens, with the layer as
the bitmap it shows.

### The fonts on offer

`GetInstalledFontFamilies()` (`UltraCanvasApplication.h`) lists every
font family this process can set text in - the system's installed fonts
(TrueType, OpenType, Type 1) with the bundled and registered ones, as the
text engine sees them, scalable only, sorted regardless of case. UltraPaint's
Text tool offers all of them, and the dropdown's type-ahead
([`UltraCanvasDropDownExamples.md`](UltraCanvasDropDownExamples.md)) makes a
long list quick to pick from.

## The element

```cpp
#include "UltraCanvasPaintSurface.h"

auto surface = CreatePaintSurface("canvas");
surface->SetDocument(doc);
surface->onToolPress   = [&](const PaintPointerEvent& e) { tool->OnPress(e); };
surface->onToolDrag    = [&](const PaintPointerEvent& e) { tool->OnDrag(e); };
surface->onToolRelease = [&](const PaintPointerEvent& e) { tool->OnRelease(e); };
surface->onDrawOverlay = [&](IRenderContext* ctx, const PaintViewTransform& v) {
    tool->DrawPreview(ctx, v);        // rubber bands, in view coordinates
};
surface->SetCursorRadius(brush.size * 0.5);
```

`PaintPointerEvent` carries the pointer in **image coordinates** (sub-pixel
`x`, `y`), the view position, button, modifiers, pressure and whether it is
over the canvas. The surface handles what every tool shares:

- **Zoom** with the wheel (about the pointer), `+` / `-`, `Ctrl+0` fit,
  `Ctrl+1` 100 %, a preset ladder for `ZoomStep()`; nearest-neighbour above
  200 % (see `IRenderContext::SetImageSmoothing`) so pixels read as pixels.
- **Pan** with the middle button or Space+drag; `SetPanMode(true)` makes
  every drag a pan (a Pan tool).
- **Resizing** (the window, a split pane) keeps the view on what it showed:
  the image point at the centre of the view stays at its centre, at the same
  zoom, and an image smaller than the view is centred in it again. It
  happens in the frame that shows the new size - or at the next view call
  (`SetZoom`, `ScrollToImagePoint`, ...) if that comes first - and
  `onViewChanged` reports it, so overlays placed in view coordinates (an
  editor over a text layer, the status bar's zoom) follow.
- **Checkerboard** behind transparent pixels, a drop shadow and border, the
  **pixel grid** from 8× (`SetPixelGridThreshold`), the **selection
  outline** as marching ants (animated on an application timer only while
  a selection exists - one clock per document, shared by every surface
  showing it, so a loupe's ants march in step with the editing view's), and
  a **brush cursor** circle / square of
  `SetCursorRadius()`.
- Keyboard events go to `onToolKey` first, then to the zoom keys.
- Dropped files arrive through `onFilesDropped`.

It is distinct from `UltraCanvasZoomPanImage` (a read-only viewer of a
`UCImage`) and `UltraCanvasMediaSurface` (the media viewer's display): this
one edits, and its picture is the document's live composite.

### Loupe mode: a magnifying glass on the same document

A second surface on the same document can show the pixels around a point
magnified - the magnifying glass a bitmap editor keeps beside its editing
view while a tool places things to the pixel. In loupe mode the host alone
sets the view and nothing else moves it: the view is never clamped to the
element or fitted on the first frame, the pointer, wheel and keyboard are
ignored, and the element takes neither focus nor hit-tests
(`SetInteractive(false)`), so it can lie over the editing view - an
absolutely positioned child with `OverlayZOrder::Overlays` - without taking
its events.

```cpp
auto loupe = CreatePaintSurface("loupe");
loupe->SetLoupeMode(true);
loupe->SetDocument(doc);
loupe->onDrawOverlay = surface->onDrawOverlay;   // the tool's rubber band, magnified too
surface->onToolHover = [&](const PaintPointerEvent& e) {
    // three times the editing view's zoom, the pointer's pixel at the centre
    loupe->SetLoupeView(surface->GetZoom() * 3.0, e.x, e.y);
};
loupe->SetLoupeCrosshair(true, 8.0);   // the default: two diagonals, 8 px clear of the centre
```

- `SetLoupeView(zoom, imageX, imageY)` puts that image point at the centre
  of the element at that zoom (clamped to the surface's own 1 %..12800 %);
  it stays at the centre when the loupe is resized.
- The **crosshair** is two translucent diagonals from the corners that stop
  `gap` pixels short of the centre, so the pixel under the pointer stays
  clear; a light line under a dark one reads on dark and light pixels alike.
  `SetLoupeCrosshair(false)` hides it; it is never drawn outside loupe mode.
- A loupe listens to the document like the editing view does
  (`UCRasterDocument::AddListener`), so pixels, the selection and its
  marching ants (in step with the editing view's, on the one clock both
  share), and the layers follow by themselves.
- The checkerboard, the pixel grid from 8x and `onDrawOverlay` work as in
  the editing view, in the loupe's own transform.

UltraPaint shows one in a corner of the canvas while the rectangle selection
is active (Settings > Display > Mouse pointer magnification glass: size and
2x / 3x / 4x) and moves it to another corner when the pointer comes near it.

## `IRenderContext::SetImageSmoothing`

Added with this layer: `SetImageSmoothing(false)` makes `DrawPixmap` and
`DrawPartOfPixmap` sample nearest-neighbour instead of bilinear (Cairo
backend: `CAIRO_FILTER_NEAREST`). Default on; the surface turns it off above
200 % zoom and back on afterwards.

## Building without libvips

The layer, selection, document, brush engine and surface compile without
`HAS_LIBVIPS`. What disappears is `ToPixelFX` / `FromPixelFX`,
`ApplyFilter` / `PreviewFilter`, file load / save and `.ucraster` projects;
`ScaleImage` falls back to the layer's bilinear resample. Text layers are
drawn without libvips (through the render context); only keeping them in a
project needs it, as every project does.

## Tests

`Tests/RasterEditingTest.cpp` (built with `BUILD_TESTS=ON`, target
`RasterEditingTest`) covers the layer and blend arithmetic, the selection
shapes and set algebra, strokes (opacity cap, erase, selection clipping, the
one-pixel pencil), shapes / fills / gradients / wand, document undo / redo
across pixel and structural edits, selection-aware `ApplyFilter`, text
layers (drawn inside their box in their runs' colours; the editor at 1:1
drawing exactly the layer's pixels; staying text through attribute edits,
canvas resizes and crops and becoming bitmaps through pixel edits, merges,
flips, rotations and scaling, with undo making them text again; a project
round trip that draws again to the same pixels), the installed font list, the
surface's view through a resize (an image smaller than the view centred
again in the next frame, a larger one keeping its centre point, zooming about
the pointer between a resize and the frame, and a loupe keeping its point), the
PNG and `.ucraster` round trips, and saving back over the file a document was
opened from -- that the image survives the round trip, that the temporary
file the save goes through leaves no trace, and that neither a save nor an export
which fails inside the encoder leaves a mark on the file that was already
there.

## See also

- [`UltraCanvasCurveEditor.md`](UltraCanvasCurveEditor.md) — the Curves
  dialog UltraPaint uses with `PreviewFilter`
- [`Docs/Modules/PixelFX/README.md`](../Modules/PixelFX/README.md)
- [`Apps/UltraPaint/README.md`](../../Apps/UltraPaint/README.md)
