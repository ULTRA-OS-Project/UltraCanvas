# UltraPaint

Bitmap editor built on the UltraCanvas framework: layers, selections,
brushes and shapes on the framework's raster-editing layer
([`UltraCanvasPaintSurface.md`](../../Docs/UltraCanvas/UltraCanvasPaintSurface.md)),
adjustments and filters through
[PixelFX](../../Docs/Modules/PixelFX/README.md) (libvips).

This app versions itself: [`Docs/UltraPaint/CHANGELOG.md`](../../Docs/UltraPaint/CHANGELOG.md).
The investigation of what the framework lacked before it could host an
editor is [`Docs/UltraPaint/FeatureGapAnalysis.md`](../../Docs/UltraPaint/FeatureGapAnalysis.md).

## Layout

```
 menu bar:  File  Edit  Image  Layer  Select  Adjust  Filter  View  Help
 toolbar:   new open save | undo redo | zoom- zoom+ fit 100% | new layer export
 ┌────┬──────────────────────────────────────┬──────────────────┐
 │tool│                                      │ Colour           │
 │pal-│         paint surface                │  picker, swatches│
 │ette│   (zoom, pan, grid, marching ants)   │ Tool Options     │
 │    │                                      │ Layers           │
 └────┴──────────────────────────────────────┴──────────────────┘
 status: pointer x,y | zoom | size, layers | hint
```

## Tools

| Tool | Key | Notes |
|---|---|---|
| Move | M | Drags the selected pixels (or the whole layer) |
| Rectangle / Ellipse Select | R / E | Shift adds, Alt subtracts, Shift+Alt intersects, Ctrl constrains; feather in the options |
| Lasso | L | Free-hand outline |
| Magic Wand | W | Tolerance, contiguous / global, sample merged |
| Crop | C | Drag, then Enter (Escape cancels) |
| Eyedropper | I | Left = foreground, right = background; point or averaged sample, merged or layer |
| Pencil | N | Hard, aliased pixels |
| Paintbrush | B | Size, hardness, opacity, flow, spacing, round / square, pressure |
| Airbrush | A | Soft, low flow |
| Eraser | X (Shift) | To transparency |
| Clone Stamp | S | Ctrl+click sets the source |
| Smudge | U | Drags colour along |
| Dodge / Burn | O / K | Lighten / darken |
| Fill | F | Flood fill: tolerance, contiguous, sample merged, opacity |
| Gradient | G | Foreground → background; linear, radial, reflected |
| Line / Rectangle / Ellipse | D / Q / P | Outline in the foreground colour, fill in the background colour, anti-aliased, Shift constrains |
| Text | T | Click, type, choose font / size / bold |
| Zoom | Z | Click in, Alt/right-click out |
| Pan | H | Space+drag works with every tool |

`[` and `]` change the brush size; `X` swaps the colours, `Shift+D` resets
them; the wheel zooms about the pointer, `Ctrl+0` fits, `Ctrl+1` is 100 %.

## Menus

- **File:** New (presets, background), Open, Save, Save As, Export with
  Options (the framework's format dialog), Quit.
- **Edit:** Undo / Redo, Cut, Copy, Copy Merged, Paste as New Layer, Paste
  as New Image, Delete, Fill with Foreground / Background.
- **Image:** Scale Image, Canvas Size (with anchor), Crop to Selection,
  Flip, Rotate, Flatten.
- **Layer:** New, Duplicate, Delete, Merge Down, Move Up / Down,
  Properties (name, opacity, blend mode, visibility, lock).
- **Select:** All, None, Invert, From Layer Alpha, Feather, Grow, Shrink.
- **Adjust:** Brightness / Contrast, Hue / Saturation, Gamma, Levels,
  Curves, Posterize, Threshold, Solarize, Invert, Desaturate, Sepia,
  Equalize, Auto Contrast.
- **Filter:** Gaussian / Box / Motion Blur, Median; Sharpen, Unsharp Mask;
  Sobel, Laplacian, Canny, Emboss, Find Edges; Add Noise, Despeckle;
  Pixelate, Oil Paint; Erode, Dilate.
- **View:** zoom commands, pixel grid, selection outline.

Every adjustment and filter with parameters opens a dialog with sliders, a
Preview toggle and OK / Cancel; the preview runs on the canvas itself and is
one undo entry once accepted. All of them respect the selection. The
catalogue is data in `UltraPaintFilters.cpp` — one entry per filter, and
the menus are generated from it.

## Files

Opens what libvips loads (PNG, JPEG, WebP, AVIF, HEIC, TIFF, GIF, BMP, JXL,
TGA, PSD, camera RAW, SVG, …). *Save* / *Save As* write the flattened image
in the format of the extension; *Export with Options* adds the per-format
knobs. Layered work is kept in **`.ucraster`** — a ZIP with `document.json`
and one PNG per layer.

## Usage

```
UltraPaint                 # blank canvas
UltraPaint photo.jpg       # open an image
UltraPaint work.ucraster   # open a layered project
```

## Building

Configured by the root CMakeLists (`BUILD_ULTRAPAINT_APP`, default ON):

```bash
mkdir build && cd build && cmake .. && make UltraPaint
```

Needs libvips for filters and file formats; the editor, layers, selection
and brushes build without it.

## Source map

| File | Contents |
|---|---|
| `main.cpp` | Application bootstrap (same shape as UltraViewer) |
| `UltraPaintWindow.{h,cpp}` | Window composition, menus, panels, every command, filter preview |
| `UltraPaintTools.{h,cpp}` | The tools and their option panels |
| `UltraPaintFilters.{h,cpp}` | The PixelFX filter catalogue and the parameter dialog |
| `UltraPaintDialogs.{h,cpp}` | New Image, Scale / Canvas Size, Text, Layer Properties |
