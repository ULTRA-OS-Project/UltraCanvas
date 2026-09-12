# Vector artwork → pixels

`UltraCanvasVectorRaster.h` turns a vector file into an editable
[`UCRasterLayer`](UltraCanvasPaintSurface.md) at a size the caller chooses.
It is the answer to "the user dropped an SVG on a bitmap editor" — and to
anything else that needs a drawing as pixels: a thumbnail, a texture, a
layer to paint over.

A vector file has no pixels, only instructions. Deciding *how many* pixels is
therefore part of opening one, and the point of this header is that the
drawing is rendered **at** the size asked for rather than rendered small and
scaled up.

```cpp
#include "UltraCanvasVectorRaster.h"

if (IsVectorGraphicsPath(path)) {
    const VectorSourceInfo info = InspectVectorFile(path);   // natural size, pages
    VectorRasterOptions options;
    options.width  = info.naturalWidth * 4;                  // 4x for print
    options.height = info.naturalHeight * 4;
    std::string error;
    if (auto layer = RasterizeVectorFile(path, options, error)) {
        document->AddLayer(layer);
    } else {
        ShowError(error);
    }
}
```

## The two rasterizers

One call, two paths, picked per file. `InspectVectorFile()` reports which one
applies (`VectorSourceInfo::source`) and who does the work
(`VectorSourceInfo::provider`).

| `VectorRasterSource` | Formats | How |
|---|---|---|
| `ImagePipeline` | `.svg`, `.svgz`, `.pdf`, `.ai`, `.eps`, `.ps` | libvips: `svgload` (librsvg), `pdfload` (poppler/pdfium), the PostScript delegate. Rendered at the resolution that produces the requested size — `PixelFX::FileIO::LoadSvg` / `LoadPdf` underneath. |
| `GraphicsPlugin` | whatever a registered `IGraphicsPlugin` claims and `GraphicsFormatDetector` files as `Vector` — the [Vector plugin](UltraCanvasVectorConverters.md)'s DXF / DWG / EMF / WMF / XAR, the CDR and XAR viewer plugins | the plugin's element is given the target bounds and rendered into an offscreen `IRenderContext`; the premultiplied ARGB32 pixels are read back and un-premultiplied into the layer. |
| `Unsupported` | everything else | nothing in this build reads it; `VectorSourceInfo::error` says so. |

Both halves are **runtime** answers. The libvips half depends on how libvips
was built (a build without librsvg cannot rasterize SVG), the plugin half on
which plugins the application registered — `ULTRACANVAS_PLUGIN_VECTOR` is off
by default, so DXF and friends appear only once a host turns it on and calls
`RegisterVectorFormatsPlugin()`. `GetVectorRasterExtensions()` lists what this
particular process can actually do, which is what a file filter should show.

## Sizing

`VectorRasterOptions::width` / `height` are pixels:

| Given | Result |
|---|---|
| both 0 | the natural size (`VectorSourceInfo::naturalWidth/Height`) |
| one of them | the other follows the natural aspect ratio |
| both | exactly that, aspect ratio ignored |

The natural size is what the artwork asks for: SVG user units, PDF points at
72 dpi. A print-resolution raster is that size scaled — `width =
naturalWidth * dpi / 72` for a PDF page, so a 595 × 842 pt A4 page at 300 dpi
is 2480 × 3508.

`maxPixels` (256 Mpx by default) is the guard that turns a mistyped size into
an error instead of a multi-gigabyte allocation. It is counted on the target
size, before anything is rendered.

`background` is painted *under* the drawing, so an SVG with no backdrop keeps
its transparency by default and `RasterPixel(255, 255, 255, 255)` flattens it
onto white without touching what is drawn on top.

`page` selects the page of a paged source; `VectorSourceInfo::pageCount` says
how many there are (1 for everything but PDF).

## What it is not

- **Not a vector editor.** The document model, the converters between vector
  formats and the editable `VectorDocument` are
  [UltraCanvasVectorConverters](UltraCanvasVectorConverters.md); this header
  throws the vector data away and hands back pixels.
- **Not the other direction.** Pixels → SVG is the
  [Vectorizer](../Modules/Vectorizer/README.md) plugin.
- **Not a display path.** An element that shows a drawing on screen should
  stay vector and re-render on zoom (`UltraCanvasVectorElement`,
  `UCSvgDocument::RenderPixmap` for SVG with its parse-once cache). Rasterize
  when the pixels are the point — when they are going to be edited, filtered
  or saved as a bitmap.

## Consumers

UltraPaint: dropping or opening a drawing asks for the raster size and then
comes through here (`Apps/UltraPaint/UltraPaintWindow.cpp`,
`UltraPaintImportDialog`).

## Tests

`Tests/VectorRasterTest.cpp` (CTest target `VectorRasterTest`): the natural
size is the size the artwork asks for, a requested size is delivered exactly,
one dimension keeps the aspect ratio, the background composites under the
drawing, and an absurd size is refused rather than allocated. It reports
`[SKIP]` on a build with no SVG rasterizer instead of failing.

## See also

- [UltraCanvasVectorConverters](UltraCanvasVectorConverters.md) — the vector
  format matrix and the `VectorDocument` model
- [UltraCanvasPaintSurface](UltraCanvasPaintSurface.md) — `UCRasterLayer`,
  `UCRasterDocument` and the editing surface
- [BitmapHandlingArchitecture](BitmapHandlingArchitecture.md) — why a vector
  source is rasterized at display size and re-rasterized when that changes
- [PixelFX](../Modules/PixelFX/README.md) — the libvips-backed image pipeline
