# UltraPaint — feature gap analysis

What a bitmap editor of the Paint Shop Pro / GIMP class needs, what the
UltraCanvas framework and PixelFX already provided when UltraPaint was
started (2026-09-06), and what had to be built. This is the investigation
behind the raster-editing layer described in
[`Docs/UltraCanvas/UltraCanvasPaintSurface.md`](../UltraCanvas/UltraCanvasPaintSurface.md).

## What PixelFX already covered

PixelFX (`UltraCanvas/include/PixelFX/PixelFX.h`, libvips) is a complete
**whole-image** engine and UltraPaint uses it as such. Everything in the
Adjust and Filter menus is one PixelFX call chain:

| Need | PixelFX |
|---|---|
| Load / save every raster format | `FileIO::Load`, `FileIO::Save`, `SaveToBuffer`, `LoadFromMemory`; `ExportVImage` with the framework's format-options dialog |
| Colour adjustments | `Colour::Brightness / Contrast / Saturation / Gamma / Invert / Grayscale / Sepia / HistEqual / MapLut`, `SrgbToHsv` / `HsvToSrgb` |
| Blur / sharpen | `Convolution::GaussianBlur / BoxBlur / Sharpen / UnsharpMask`, custom kernels via `conv` |
| Edges | `Convolution::Sobel / Laplacian / Canny / Prewitt / Scharr` |
| Noise, morphology, stylise | `Generate::Gaussnoise`, `Morphology::Median / Rank / Erode / Dilate`, `Resample::Shrink` + `Conversion::Zoom` (pixelate) |
| Geometry | `Resample::ResizeTo / Rotate / Flip`, `Conversion::Embed / Crop / ExtractArea`, `Autorot` |
| Alpha | `Colour::Premultiply / Unpremultiply / AddAlpha / Flatten / HasAlpha` |
| Text rasterisation | `Generate::Text` (Pango) — the text tool stamps its mask |
| Region growing | `Draw::FloodFillTolerance`, `Draw::MagicWandMask` (kept as reference; the editor has its own so it works without libvips and on merged samples) |
| Histogram / curves | `Colour::HistFind`, `MapLut` with `UltraCanvasToneCurve` LUTs |

## What the UI framework already covered

| Need | Element (reused as-is) |
|---|---|
| Foreground / background colour with wheel, sliders, hex, eyedropper | `UltraCanvasColorPicker` |
| Palette strip | `UltraCanvasColorSwatchBar` |
| Curves dialog with histogram | `UltraCanvasCurvesDialog` / `UltraCanvasCurveEditor` / `ToneCurveSet` |
| Export with per-format options | `UltraCanvasImageExportDialog` |
| Menus with shortcuts and icons, toolbars (horizontal and vertical), sliders, spinners, dropdowns, checkboxes, text input, labels, scrollable panels, modal / native dialogs, clipboard (text, files, images as encoded bytes), timers, event filters | `UltraCanvasMenu`, `UltraCanvasToolbar`, `UltraCanvasSlider`, `UltraCanvasSpinner`, `UltraCanvasDropdown`, `UltraCanvasCheckbox`, `UltraCanvasTextInput`, `UltraCanvasContainer`, `UltraCanvasDialogManager`, `UltraCanvasClipboard`, `UltraCanvasApplication::StartTimer`, `InstallEventFilter` |
| Read-only zoom / pan of an image | `UltraCanvasZoomPanImage`, `UltraCanvasMediaSurface` — view only, no editing hooks |

## What did not exist — and was added to the framework

These were the real gaps. Each is a framework addition
(`UltraCanvas/{include,core}`), not application code, so the next
application (an icon editor, a screenshot annotator, a sprite tool) gets
them too.

| Gap | Why PixelFX / the elements did not cover it | Added |
|---|---|---|
| **An editable pixel model.** PixelFX images are lazy libvips pipelines: fine for "blur the whole image", wrong for "change 40 pixels under the brush 200 times a second". Nothing held a mutable layer. | Interactive painting needs direct, in-place pixel writes and cheap partial recomposites. | `UCRasterLayer` — straight-RGBA buffer with in-place access, blend arithmetic, partial compositing to the Cairo pixmap, and a lossless PixelFX round trip so filters still run on it. |
| **Layers.** No layer stack, blend modes, opacity, visibility anywhere in the framework. | `UCImage` / `UCPixmap` are single pictures. | `UCRasterDocument` — ordered stack, per-layer attributes, add / duplicate / delete / move / merge / flatten, cached composite with dirty rectangles, layered `.ucraster` project file. |
| **Selection.** Nothing represented "the part of the image an operation applies to". `MagicWandMask` returned a byte map but nothing consumed one. | Filters, fills, strokes, copy / paste all need coverage per pixel, with soft (feathered) edges. | `UCRasterSelection` — coverage mask, shape setters, set algebra, feather / grow / shrink / invert, outline segments for marching ants. Every brush op and `ApplyFilter` consult it. |
| **Undo / redo for pixels.** The framework had undo only inside text controls and the spreadsheet. | An editor needs rect-snapshot undo for strokes and structural undo for layer operations, under a memory budget. | The document's `BeginEdit` / `EndEdit` / `RecordEdit` brackets and copy-on-write structural snapshots. |
| **Brush surface / brush engine.** `PixelFX::Draw::Line / Circle` are hard-edged, single-shot libvips draw ops with no softness, opacity accumulation, spacing, pressure, erase, clone or smudge. | Painting is dab stamping with sub-pixel interpolation and the opacity-vs-flow model; it also must not depend on libvips for the interactive path. | `UCBrushStroke` (round / square dabs, hardness, opacity, flow, spacing, anti-aliasing, pressure, paint / erase / clone / smudge / dodge / burn) and `RasterPaint` (anti-aliased line / rectangle / ellipse / polygon, flood fill with tolerance and global mode, magic wand, gradients, mask stamping, eyedropper). |
| **An editing canvas element.** `UltraCanvasZoomPanImage` zooms a `UCImage`; it has no tool callbacks, no image-coordinate mapping for the host, no overlay hook, no selection display, no pixel grid, and it draws with bilinear filtering so zoomed pixels blur. | The tool needs sub-pixel image coordinates, the host needs to draw rubber bands and the brush outline, and pixel art needs nearest-neighbour. | `UltraCanvasPaintSurface` — zoom ladder, pan, checkerboard, grid, marching ants, brush cursor, `onToolPress / Drag / Release / Hover / Key`, `onDrawOverlay`. |
| **Nearest-neighbour image drawing.** `IRenderContext` had no way to choose the sampling filter. | Above 200 % an editor must show square pixels. | `IRenderContext::SetImageSmoothing(bool)` with the Cairo implementation. |
| **New Image dialog.** `UltraCanvasNewDocumentDialog` chooses a file *type*; an image editor needs width / height / background with presets. | Different question. | `UltraPaintNewImageDialog` (app-level; small enough to stay there for now). |

## What is still missing (candidates for later releases)

Listed so the next session does not rediscover them:

- **Tablet pressure on Linux/X11.** `UCEvent::pressure` exists and the brush
  honours it, but the X11 backend reports 1.0 (no XInput2 valuators yet).
- **Airbrush time accumulation.** The airbrush deposits per pointer
  movement; a true airbrush also deposits while the pointer rests (needs a
  timer-driven `AddPoint` in the tool).
- **Brush tips beyond round / square** (texture / image brushes), brush
  dynamics (scatter, jitter, angle), and a brush preset library.
- **Selection transform** (scale / rotate the floating selection), the
  perspective / free-transform tool, and content-aware fill.
- **Adjustment layers and layer masks.** Blend modes and opacity exist; a
  non-destructive adjustment stack does not.
- **Vector / text layers.** Text is rasterised on placement; there is no
  editable text object. The Vectorizer plugin (raster → SVG) could be
  offered as *Image → Trace to SVG*.
- **Colour management.** PixelFX has `IccImport / IccExport`; the editor
  works in sRGB only.
- **16-bit / HDR editing.** Layers are 8-bit; PixelFX handles 16-bit and
  float, so a `UCRasterLayer` with a selectable band format is the natural
  extension.
- **Batch / scripting.** The document is UI-free by design; a command-line
  driver (`ultrapaint --apply gaussian-blur=3 in.png out.png`) is a small
  addition.
- **System clipboard images.** `UltraCanvasClipboard::GetImage / SetImage`
  exist, and UltraPaint uses them (PNG); the X11 backend's image target
  support should be verified on a real desktop (this session ran under Xvfb
  only).
- **Animation frames** (GIF / WebP animation as a frame timeline).
- **Plugins.** A filter-plugin interface so third parties can add entries to
  `PaintFilterCatalogue()` without recompiling the app.
