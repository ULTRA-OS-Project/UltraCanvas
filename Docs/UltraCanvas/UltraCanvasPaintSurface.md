# UltraCanvas Raster Editing: PaintSurface, RasterDocument, Brush Engine

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
| `UCBrushStroke`, `RasterPaint` | The brush engine: interactive strokes (dabs with spacing, hardness, opacity vs flow, paint / erase / clone / smudge / dodge / burn) and one-shot operations (anti-aliased shapes, flood fill, magic wand, gradients, mask stamping, eyedropper) | `include/UltraCanvasBrushEngine.h`, `core/UltraCanvasBrushEngine.cpp` |
| `UltraCanvasPaintSurface` | The element: zoom / pan view of a document with checkerboard, pixel grid, selection ants and brush cursor; forwards pointer events in image coordinates to the tool the host installs | `include/UltraCanvasPaintSurface.h`, `core/UltraCanvasPaintSurface.cpp` |

**Version**: 1.0.0
**Last Modified**: 2026-09-06
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
doc->SaveToFile("out.png", err);               // flattened, format by extension
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

### Notifications

| Callback | Fired when |
|---|---|
| `onPixelsChanged(rect)` | pixels of `rect` changed (a stroke calls it per dab) |
| `onStructureChanged()` | layer list, attributes, active layer or canvas size changed |
| `onSelectionChanged()` | the selection changed |
| `onStateChanged()` | modified flag or undo / redo availability changed |

`UltraCanvasPaintSurface::SetDocument()` installs the first three; a host
that needs them too chains its own handler after the surface's (see
`UltraPaintWindow::SetDocument`).

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
anti-aliasing through `FillCoverage`), `FloodFill` (tolerance, contiguous or
global, optionally sampling a merged layer), `MagicWandMask` (the same
region test as a coverage map for `UCRasterSelection::SetMask`),
`FillGradient` (linear / radial / reflected, interpolated in premultiplied
space), `StampMask` (a coverage map in a colour — what the text tool uses
with a `PixelFX::Generate::Text` mask), `SampleColour`.

None of this needs libvips.

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
- **Checkerboard** behind transparent pixels, a drop shadow and border, the
  **pixel grid** from 8× (`SetPixelGridThreshold`), the **selection
  outline** as marching ants (animated on an application timer only while
  a selection exists), and a **brush cursor** circle / square of
  `SetCursorRadius()`.
- Keyboard events go to `onToolKey` first, then to the zoom keys.
- Dropped files arrive through `onFilesDropped`.

It is distinct from `UltraCanvasZoomPanImage` (a read-only viewer of a
`UCImage`) and `UltraCanvasMediaSurface` (the media viewer's display): this
one edits, and its picture is the document's live composite.

## `IRenderContext::SetImageSmoothing`

Added with this layer: `SetImageSmoothing(false)` makes `DrawPixmap` and
`DrawPartOfPixmap` sample nearest-neighbour instead of bilinear (Cairo
backend: `CAIRO_FILTER_NEAREST`). Default on; the surface turns it off above
200 % zoom and back on afterwards.

## Building without libvips

The layer, selection, document, brush engine and surface compile without
`HAS_LIBVIPS`. What disappears is `ToPixelFX` / `FromPixelFX`,
`ApplyFilter` / `PreviewFilter`, file load / save and `.ucraster` projects;
`ScaleImage` falls back to the layer's bilinear resample.

## Tests

`Tests/RasterEditingTest.cpp` (built with `BUILD_TESTS=ON`, target
`RasterEditingTest`) covers the layer and blend arithmetic, the selection
shapes and set algebra, strokes (opacity cap, erase, selection clipping, the
one-pixel pencil), shapes / fills / gradients / wand, document undo / redo
across pixel and structural edits, selection-aware `ApplyFilter`, and the
PNG and `.ucraster` round trips.

## See also

- [`UltraCanvasCurveEditor.md`](UltraCanvasCurveEditor.md) — the Curves
  dialog UltraPaint uses with `PreviewFilter`
- [`Docs/Modules/PixelFX/README.md`](../Modules/PixelFX/README.md)
- [`Apps/UltraPaint/README.md`](../../Apps/UltraPaint/README.md)
