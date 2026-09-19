# Adobe Illustrator (`.ai`) Artwork

## Overview

Since **Illustrator 9** an `.ai` file is a PDF — but that alone does not make
it readable by a PDF viewer. Illustrator's **"Create PDF Compatible File"**
option decides whether the PDF page carries the artwork at all:

| Saved with PDF compatibility | What the file contains | What renders it |
|---|---|---|
| **On** | A normal PDF page that draws the artwork, *plus* the private `AIPrivateData` streams Illustrator edits from | Any PDF engine |
| **Off** | A valid PDF whose page content stream draws **nothing** — the artwork exists only in `AIPrivateData` | The private data has to be read |

Both samples under `media/vector/AI/` are the second kind (CorelDRAW exports;
several other applications write them too). Their page content stream is 47
bytes that set a transform and a graphics state and draw nothing, so a PDF
viewer correctly renders a blank page — which is what UltraCanvas, and the
online `.ai` viewers this was checked against, used to show.

So UltraCanvas reads `.ai` through the Vector plugin's
`VectorConverter::AIConverter`, which goes after the private data, and falls
back to the PDF engine for a file whose artwork really is in its page.

| Direction | Component | Notes |
|---|---|---|
| Read | `VectorConverter::AIConverter` (`UltraCanvasAIReader.cpp`) | Decodes the `/AIPrivateData` streams and interprets Illustrator's art language into a `VectorStorage::VectorDocument` |
| Read (fallback) | `UltraCanvasPDFView` / `PDFEngineFactory` (`ULTRACANVAS_PLUGIN_PDF`) | A PDF-compatible `.ai` carries no private data; `AIConverter` declines it and the PDF engine renders the page |
| Write | `VectorConverter::AIConverter` | Export-only: emits the plugin's PDF output under the `.ai` extension — valid for Illustrator and every PDF consumer |

**Demo source:** `Apps/DemoApp/UltraCanvasAIExamples.cpp`
**Reader:** `UltraCanvas/Plugins/Vector/UltraCanvasAIReader.cpp`
**Test:** `Tests/AIReaderTest.cpp`
**Demo page:** Vector Elements → *AI Artwork*
**Namespace:** `UltraCanvas`

## Reading an `.ai`

```cpp
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasVectorElement.h"

VectorConverter::AIConverter converter;
VectorConverter::ConversionOptions options;
options.WarningCallback = [](const std::string& message) { /* report */ };

auto document = converter.Import(
        NormalizePath(GetResourcesDir() + "media/vector/AI/turtle.ai"), options);

if (document) {
    auto view = CreateVectorElement("AIView", 0, 0, 800, 600);
    view->SetDocument(document);
    view->ZoomToFit();
}
```

`Import()` returns null for a `.ai` that holds no private artwork, with a
warning that says so — that is the signal to hand the file to the PDF engine
rather than an error. `UltraCanvasVectorFormatsPlugin::LoadVectorDocument()`
and `CreateConverterForExtension("ai")` reach the same converter.

### What the reader understands

The art language is the same PostScript-flavoured operator set that legacy
(v8 and earlier) EPS-based `.ai` files carry in the open, so a legacy file
reads through the same parser with no container step.

| Area | Operators | Notes |
|---|---|---|
| Path construction | `m`, `l`/`L`, `c`/`C`, `v`/`V`, `y`/`Y` | The case distinction marks a corner or smooth anchor — editing information; both draw the same segment. `v` and `y` are the shorthands whose control point coincides with an endpoint |
| Painting | `n`/`N`, `f`/`F`, `s`/`S`, `b`/`B` | The lowercase form closes the subpath first |
| Clipping | `W` | Clips the rest of the enclosing group; becomes a `VectorClipPath` definition and a group carrying it |
| Compound paths | `*u` … `*U` | All subpaths become one object, so a filled shape keeps its holes |
| Structure | `u`/`U`, `Lb`/`Ln`/`LB` | Groups and named layers |
| Graphics state | `w`, `J`, `j`, `M`, `d`, `XR` | Width, cap, join, miter limit, dashes, winding rule |
| Colour | `g`/`G`, `k`/`K`, `Xa`/`XA`, `x`/`X`, `Xx`/`XX`, `p`/`P` | Grey, CMYK, RGB, spot (converted to its CMYK equivalent) and patterns (flat colour) |
| Transparency | `Xy` | The AI9 operator; blend mode and opacity survive, isolation and knockout do not |
| Gradients, text | `Bd`…`BB`, `To`…`TE` | Not imported; reported through `WarningCallback` rather than dropped silently, and the paths around them still come through |

Anything else is counted and listed in one summary warning, so a file that
displays wrong says which operators it needed.

### Coordinates

Illustrator's ruler space puts the origin at the **top-left** of the artboard
with y running down as negative numbers; a legacy EPS-based `.ai` uses
PostScript's **bottom-left** origin with y running up. Both map onto the
document's y-down page, and the reader picks between them from the art's own
extent. The page size comes from `%AI5_ArtSize`, falling back to
`%%BoundingBox`.

## Example: the demo page

The page is a flex column — title, format note, toolbar, viewer stage, status
row, notes panel. The stage holds both viewers and shows whichever route the
file took, so the toolbar's zoom buttons drive the active one:

```cpp
auto vectorView = CreateVectorElement("AIVectorView", 0, 0, 0, 0);
VectorElementOptions options = vectorView->GetOptions();
options.InteractionMode = VectorInteractionMode::PanZoom;
options.BackgroundColor = Colors::White;
vectorView->SetOptions(options);

AILoadResult loaded = LoadAIDocument(path);
if (loaded.document) {
    vectorView->SetDocument(loaded.document);
    vectorView->ZoomToFit();          // route: Vector plugin
} else {
    pdfView->LoadFromPath(path);      // route: PDF engine
}
```

The page is built when the Vector plugin is (`ULTRACANVAS_HAS_VECTOR_PLUGIN`);
the PDF fallback compiles in only where `ULTRACANVAS_PLUGIN_PDF` is also set.

## Writing `.ai`

```cpp
// Any VectorStorage::VectorDocument can be saved as .ai; the bytes are the
// Vector plugin's PDF output.
UltraCanvasVectorFormatsPlugin::SaveVectorDocument(document, "artwork.ai");
```

The writer produces a PDF-compatible file — which is the kind the PDF engine
renders — so a document written by UltraCanvas and read back takes the
fallback route, not the private-data one.

## Shipped samples

`media/vector/AI/`:

| File | Notes |
|---|---|
| `turtle.ai` | 319 KB, `%PDF-1.5`, empty page, 868 stroked paths in the private data |
| `mandalorian-star-wars.ai` | 74 KB, `%PDF-1.5`, empty page, 72 stroked paths in the private data |

## See also

- [`UltraCanvasVectorConverters.md`](UltraCanvasVectorConverters.md) — the converter matrix `AIConverter` belongs to
- [`UltraCanvasVectorRaster.md`](UltraCanvasVectorRaster.md) — the `UltraCanvasVectorElement` viewer
- [`UltraCanvasPDFExamples.md`](UltraCanvasPDFExamples.md) — the PDF viewer used for the fallback route
- [`UltraCanvasEPSExamples.md`](UltraCanvasEPSExamples.md) — the PostScript interpreter, a separate engine for `.eps`
