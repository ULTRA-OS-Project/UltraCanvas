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

- **File:** New (presets, background), New Window, Open, Import Image
  (merge into this image or open a new window), Save, Save As, Export with
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
  Equalize, Auto Contrast, Colour to Alpha.
- **Filter:** Gaussian / Box / Motion Blur, Median; Sharpen, Unsharp Mask;
  Sobel, Laplacian, Canny, Emboss, Find Edges; Add Noise, Despeckle;
  Pixelate, Oil Paint; Erode, Dilate.
- **View:** zoom commands, pixel grid, selection outline.

Every adjustment and filter with parameters opens a dialog with sliders, a
Preview toggle and OK / Cancel; the preview runs on the canvas itself and is
one undo entry once accepted. All of them respect the selection. The
catalogue is data in `UltraPaintFilters.cpp` — one entry per filter, and
the menus are generated from it.

### Colour to Alpha

*Adjust ▸ Colour to Alpha…* turns one colour into transparency across the
layer — knocking a white page out from behind a scanned logo, dropping a
flat backdrop, or just fading a colour back.

| Control | What it does |
|---|---|
| Colour | The colour to key out. Starts at the foreground colour; the picker's eyedropper takes it straight off the canvas |
| Tolerance | How far a pixel may be from that colour and still count as it (0–255, the same distance the magic wand and the fill use) |
| Softness | The width of the ramp past the tolerance. A few steps of softness is what keeps an anti-aliased edge smooth instead of jagged |
| Transparency % | How transparent the colour becomes: 100 removes it, 40 takes 40 % of its opacity away and leaves the rest |
| Remove colour fringe | Un-mixes the key colour out of the pixels left partly transparent, so a logo keyed off white keeps no white halo |

Alpha is only ever scaled down, so pixels that were already transparent stay
that way, and a layer with no transparency gains it. Like every other
adjustment it previews live, respects the selection and lands as one undo
entry.

For a hard-edged cut instead, the Magic Wand with *Contiguous* off selects
every pixel of a colour and `Del` erases them.

## Files

Opens what libvips loads (PNG, JPEG, WebP, AVIF, HEIC, TIFF, GIF, BMP, JXL,
TGA, PSD, camera RAW, …). *Save* / *Save As* write the flattened image
in the format of the extension; *Export with Options* adds the per-format
knobs. Layered work is kept in **`.ucraster`** — a ZIP with `document.json`
and one PNG per layer.

### Dropping a file on the canvas

A file dragged onto the canvas asks what to do with it rather than replacing
the open image:

| Answer | What happens |
|---|---|
| **Merge image** | it lands as a new layer, centred, with the Move tool selected. A bitmap bigger than the canvas offers *Scale to fit the canvas*, ticked by default — otherwise it would be cropped to the canvas without saying so |
| **Open new window** | it gets an editor of its own; the current image is untouched |
| **Cancel** | nothing happens |

Several files dropped together ask once and all follow the same answer.
*File ▸ Import Image…* asks the same question for a file picked from the
file dialog, and *File ▸ New Window* (`Ctrl+Alt+N`) opens an empty one.
The application exits when its last window closes.

### Vector drawings

A drawing has no pixels until someone picks a resolution, so opening or
dropping one asks for the raster size — starting at the drawing's natural
size, and at whatever fits the canvas when it is being merged — plus the page
for a multi-page PDF. It is rendered at that size (not scaled up from a
thumbnail) and opens as an unsaved image, so *Save* asks where to put it
rather than overwriting the drawing with pixels.

**SVG**, **SVGZ**, **PDF** and **AI** work out of the box, and **EPS** / **PS**
where libvips was built with a PostScript delegate. The rest arrive with the
graphics plugins the application registers: **DXF**, **DWG**, **EMF** and
**WMF** from the Vector plugin (`-DULTRACANVAS_PLUGIN_VECTOR=ON`), **XAR**,
**CDR** and **EPS** from their own viewer plugins. The framework side is
[`UltraCanvasVectorRaster`](../../Docs/UltraCanvas/UltraCanvasVectorRaster.md);
`GetVectorRasterExtensions()` is what the running build can actually
rasterize, and it is what the Open dialog's filter lists.

## Usage

```
UltraPaint                 # blank canvas
UltraPaint photo.jpg       # open an image
UltraPaint work.ucraster   # open a layered project
UltraPaint logo.svg        # asks for the raster size, then opens it
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
| `UltraPaintWindow.{h,cpp}` | Window composition, the open-window registry, menus, panels, every command, import / drop handling, filter preview |
| `UltraPaintTools.{h,cpp}` | The tools and their option panels |
| `UltraPaintFilters.{h,cpp}` | The PixelFX filter catalogue and the parameter dialog |
| `UltraPaintDialogs.{h,cpp}` | New Image, Scale / Canvas Size, Text, Layer Properties, Colour to Alpha, Import (drop / vector raster size) |
