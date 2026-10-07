# SVG in UltraCanvas — showing, rasterizing, reading and writing

<!-- doc-check: std::shared_ptr<VectorStorage::VectorDocument> document; std::shared_ptr<UltraCanvasContainer> container; std::string path; -->

SVG goes through three parts of the framework, each for one job:

| Job | Component | Behind it |
|---|---|---|
| Show an SVG file on screen, or a thumbnail of one | `UltraCanvasImageElement`, `UltraCanvasMediaViewer`, `UCImage` | librsvg, through the parse-once `UCSvgDocument` cache (`libspecific/Cairo/SvgDocumentCairo.h`) |
| Turn an SVG into pixels at a chosen size | `UltraCanvasVectorRaster` (`InspectVectorFile`, `RasterizeVectorFile`) | librsvg via libvips, so the result is rendered *at* the requested size |
| Read an SVG into an editable drawing, or write one | `VectorConverter::SVGConverter` in the Vector plugin | tinyxml2 and the `VectorStorage::VectorDocument` model; `<style>` sheets through the HTMLReader's CSS parser |

There is no SVG element of its own: an SVG is a picture, and the picture
elements show it. (`Plugins/SVG/UltraCanvasSVGPlugin` - a separate
`UltraCanvasSVGElement` with its own SVG model, renderer and `style=""`
parser - was never in the build and was removed in 2026-10; the demo page
that carried its name had been showing SVG files through
`UltraCanvasImageElement` all along.)

## Showing an SVG

```cpp
#include "UltraCanvasImageElement.h"
#include "UltraCanvasMediaViewerWindow.h"

auto logo = std::make_shared<UltraCanvasImageElement>("logo", 10, 10, 200, 175);
logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/vector/SVG/robot.svg"));
container->AddChild(logo);

// The full viewer in a window of its own: zoom, the folder's other files, ESC closes.
auto viewer = std::make_shared<UltraCanvasMediaViewerWindow>();
viewer->Show(path, UltraCanvasApplication::GetInstance()->GetFocusedWindow());
```

The file is parsed once and the parsed document is cached by path
(`UCSvgDocument::Get`), so zooming, HiDPI and relayout only pay for the
vector render. `UCImageRaster::ClearCache` / `UCSvgDocument::RemoveFromCache`
drop a document when the file changed on disk. CSS inside the file - a
`<style>` block, `class=""` attributes - is applied by librsvg.

`Apps/DemoApp/UltraCanvasSVGExamples.cpp` is the demo page: one tile per
sample under `media/vector/SVG/`, each a click away from the media viewer.

## Rasterizing an SVG

`UltraCanvasVectorRaster.h` turns vector artwork into an editable
`UCRasterLayer` at a pixel size you choose - what UltraPaint's "open an SVG"
and the Filer's thumbnails do. `InspectVectorFile(path)` reports the natural
size, whether this build can rasterize the file and through which pipeline;
`RasterizeVectorFile(path, width, height)` renders it. See
[UltraCanvasVectorRaster](UltraCanvasVectorRaster.md).

## Reading and writing SVG (`UltraCanvasVectorConverter.h`)

The Vector plugin implements `UltraCanvas::VectorConverter::SVGConverter`, converting between SVG markup and the `VectorStorage::VectorDocument` model in both directions — the only vector format converter with lossless fidelity, because the storage model is essentially SVG-shaped.

- **Export** keeps everything: groups and layers stay `<g>` elements (a document's layers become top-level groups), transforms stay `matrix()` attributes, gradients keep all their stops in `<defs>` (linear and radial; conical approximates as radial with a warning), patterns serialize their content, text keeps its `<tspan>` spans with `xml:space="preserve"`, dash arrays, opacity and `<use>`/`<symbol>`/`<image>` references all round-trip. The line gallery's arrowheads (`StrokeData::StartArrow` / `EndArrow`) are written as `<marker>`s that every SVG reader draws: the outline the renderer draws (`ArrowheadOutline`), in the line's units (`markerUnits="userSpaceOnUse"`) and colour, `orient="auto"`, on lines, polylines and paths whose ends are open (where the renderer draws them). Each marker carries `data-ultracanvas-arrowhead` / `-scale` / `-end`, so the importer gives it back as the arrowhead itself rather than as shapes; the same arrowhead in the same colour is written once. SVG has no variable-width stroke and no brush, so a shape with a `WidthProfile` or a `Brush` is written as what the renderer draws: a `<g>` holding the shape with its fill and `stroke="none"`, then the band (`VariableWidthOutline`, filled even-odd in the stroke's paint) or the stamps (`<use>`s of the stamp, written once in `<defs>`, placed by `BrushStampPlacements`), then the arrowheads. The group carries `data-ultracanvas-stroke` (the stroke as a style declaration list), `data-ultracanvas-width-profile` and `data-ultracanvas-brush`, from which the importer gives back the shape with its stroke and the stamp, so the line stays editable. `SVGOptions` controls pretty-printing, minification and the viewBox.
- **Import** parses with tinyxml2: all basic shapes, paths (via `ParsePathString`), groups, presentation attributes and inline `style=""`, gradients resolved through `url(#id)` (with one level of `href` inheritance), text with tspans, unit conversion (pt/mm/cm/in/pc at CSS's 96 dpi), and entities. When every top-level drawable is a `<g>`, each imports as a `VectorLayer` — the shape this exporter and layered editors produce. CSS `<style>` sheets apply too, wherever they sit (`<defs>` included, CDATA or not, a `media` attribute honoured): they are parsed by the HTMLReader's CSS subset (`HTML::StyleSheet`) and matched against the SVG tree — type, class, id, attribute and structural pseudo-class selectors, descendant chains (`>` read as a descendant), specificity and source order. The cascade is SVG 2's: presentation attribute, then the sheets, then `style=""`, with `!important` reversing the last two. The SVG 2 geometry properties `x`, `y`, `width`, `height`, `rx`, `ry`, `cx`, `cy` and `r` can come from CSS on rects, circles and ellipses (`.card { rx: 8 }`). Sibling combinators and `:hover`-style pseudo-classes are dropped by the parser. `marker-start`, `marker-mid` and `marker-end` (and the `marker` shorthand; all four inherit) are drawn: the model has no marker-by-reference, so each `<marker>`'s content is read as ordinary shapes at the path's vertices (SVG 2's rules: each subpath start and segment end, curves and arcs by their tangents) — placed by `refX`/`refY`, mapped from its `viewBox` onto `markerWidth` × `markerHeight`, scaled by the stroke width unless `markerUnits="userSpaceOnUse"`, turned by `orient` (`auto`, `auto-start-reverse` or an angle) and clipped to its viewport when the content reaches past it. The shape and its markers become one `VectorGroup`, so an arrow stays one object in an editor; `context-fill` / `context-stroke` in marker content take the shape's paint.

```cpp
#include "UltraCanvasVectorConverter.h"   // in UltraCanvas/Plugins/Vector

using namespace UltraCanvas::VectorConverter;
SVGConverter svgc;
svgc.Export(*document, "drawing.svg");
auto doc = svgc.Import("drawing.svg");      // returns a VectorStorage::VectorDocument
```

`Tests/SVGConverterTest.cpp` round-trips a document both ways and rasterizes the export through the framework's real SVG pipeline (librsvg) with pixel checks. Vector PDF export (`PDFVectorConverter`) lives in the same header: PDF 1.4 with base-14 fonts and ExtGState opacity, validated against ghostscript in `Tests/PDFVectorWriterTest.cpp`.

## See Also

- [UltraCanvasUIElements](UltraCanvasUIElements.md) - the element catalogue; `UltraCanvasImageElement` shows an SVG like any other picture
- [UltraCanvasMediaViewer](UltraCanvasMediaViewer.md) - the viewer the demo opens drawings in
- [UltraCanvasVectorRaster](UltraCanvasVectorRaster.md) - SVG into pixels at a chosen size
- [UltraCanvasVectorConverters](UltraCanvasVectorConverters.md) - the format converters around `VectorDocument`
- [UltraCanvasHTMLReader](UltraCanvasHTMLReader.md) - the CSS parser and selector matcher the SVG reader applies `<style>` sheets with
