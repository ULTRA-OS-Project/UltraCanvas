# UltraCanvas Vector Format Converters

The Vector plugin (`ULTRACANVAS_PLUGIN_VECTOR`) converts between vector file formats and the framework's in-memory `VectorStorage::VectorDocument` model. Every converter implements `UltraCanvas::VectorConverter::IVectorFormatConverter` (`UltraCanvasVectorConverter.h`): `Import`/`ImportFromString`/`ImportFromStream` produce a `VectorDocument`, `Export`/`ExportToString`/`ExportToStream` serialize one, and `ValidateFile`/`ValidateData` sniff signatures.

**Headers:** `UltraCanvasVectorConverter.h` (interface, `SVGConverter`, `PDFVectorConverter`), `UltraCanvasXARConverter.h`, `UltraCanvasEPSConverter.h`, `UltraCanvasCDRConverter.h`, `UltraCanvasMetafileConverters.h` (`EMFConverter`, `WMFConverter`, `AIConverter`), `UltraCanvasCADConverters.h` (`DXFConverter`, `DWGConverter`), `UltraCanvasVectorFormatsPlugin.h` (the graphics plugin exposing the matrix to `LoadGraphicsFile` / `SaveGraphicsFile`).

## The matrix

| Format | Class | Read | Write | Fidelity notes |
|---|---|---|---|---|
| SVG | `SVGConverter` | ✓ | ✓ | Lossless both ways — the storage model is SVG-shaped. Gradients keep all stops, transforms stay attributes, text keeps spans. |
| XAR | `XARConverter` | ✓ | ✓ | Spec-correct record grammar; native shape records when axis-aligned, paths otherwise; linear/radial gradients (end stops), flat transparency, dash patterns, text stories. The converter's reader covers uncompressed files (its writer's grammar); compressed Xara files read via the XAR plugin. |
| EPS | `EPSConverter` | – | ✓ | PostScript program restricted to operators the EPS plugin's interpreter knows. Opacity flattens toward white; gradients blend end stops. Reading via the EPS plugin. |
| CDR | `CDRConverter` | – | ✓ | Version-7 RIFF targeting libcdr's parser layouts (no public spec exists). Solid fills, outline state, fill opacity; text/bitmaps skipped. Reading via the CDR plugin. |
| PDF | `PDFVectorConverter` | – | ✓ | Hand-assembled PDF 1.4: base-14 fonts, ExtGState opacity, dashes, verified xref. Reading via the MuPDF PDF plugin. |
| EMF | `EMFConverter` | ✓ | ✓ | [MS-EMF] records: GDI paths with real beziers, geometric pens (caps/joins/user-style dashes), `ExtTextOutW` text with GDI anchoring. No alpha in GDI — opacity flattens toward white. The reader parses the GDI object table, path records, immediate polygon/polyline/bezier records and text (`TA_UPDATECP` chains merge back into spans). |
| WMF | `WMFConverter` | ✓ | ✓ | [MS-WMF] 16-bit records with the placeable header (twips). No bezier record — curves flatten to polylines; dashes approximate as `PS_DASH`. The reader covers the object table, polygon/polyline/rect/ellipse records and TextOut/ExtTextOut. |
| AI | `AIConverter` | – | ✓ | Modern `.ai` is PDF-based, so the output is the PDF writer's under the `.ai` extension — valid for Illustrator and every PDF consumer. Reading is the PDF plugin's job. |
| DXF | `DXFConverter` | ✓ | ✓ | R2000 tagged ASCII per Autodesk's public reference. Layers map to real DXF layers, fills become solid HATCH entities with exact spline boundary edges, strokes become LWPOLYLINE/SPLINE (exact piecewise-bezier NURBS) with lineweights and dash linetypes, true colour + full-palette nearest-ACI fallback. Opacity is reported, not written. The reader parses LINE/CIRCLE/ARC/ELLIPSE/LWPOLYLINE (bulges)/POLYLINE/SPLINE/HATCH/SOLID/TEXT/MTEXT with the LAYER/LTYPE/STYLE tables; piecewise-bezier splines reproduce exactly, general NURBS sample via de Boor. |
| DWG | `DWGConverter` | ✓* | ✓* | DWG is proprietary and undocumented; both directions delegate to GNU LibreDWG's tools — writing through `dxf2dwg` (`ULTRACANVAS_DXF2DWG` or PATH), reading through `dwg2dxf` (`ULTRACANVAS_DWG2DXF` or PATH) plus the DXF reader. Without the tools the converter warns and declines — use DXF instead, AutoCAD's own exchange format. LibreDWG's DXF output carries ACI colours only, so colours quantise to the nearest palette entry. |

All writers share the same document walk: styles resolve by inheritance down the tree, transforms accumulate and (except in SVG) bake into the emitted coordinates, and path normalisation — every `PathCommandType` down to absolute move/line/cubic segments, SVG arcs via endpoint-to-centre conversion — lives in `UltraCanvasVectorPathOps.h`.

Unsupported features never change meaning silently: each converter reports its fallbacks (gradient flattening, opacity flattening, skipped element types, encoding limits) through `ConversionOptions::WarningCallback`.

## Usage

```cpp
using namespace UltraCanvas::VectorConverter;

SVGConverter svg;
auto doc = svg.Import("drawing.svg");        // VectorStorage::VectorDocument

XARConverter().Export(*doc, "drawing.xar");
EPSConverter().Export(*doc, "drawing.eps");
CDRConverter().Export(*doc, "drawing.cdr");
PDFVectorConverter().Export(*doc, "drawing.pdf");
EMFConverter().Export(*doc, "drawing.emf");
WMFConverter().Export(*doc, "drawing.wmf");
AIConverter().Export(*doc, "drawing.ai");
DXFConverter().Export(*doc, "drawing.dxf");
DWGConverter().Export(*doc, "drawing.dwg");   // needs LibreDWG's dxf2dwg

auto fromCad = DXFConverter().Import("plan.dxf");
auto fromEmf = EMFConverter().Import("clip.emf");
```

## The graphics plugin (load and save through the registry)

`UltraCanvasVectorFormatsPlugin` exposes the matrix to the framework's
graphics plugin registry so vector files work through the same entry points
as every other media type. `RegisterVectorFormatsPlugin()` once at startup
(the DemoApp does), then:

```cpp
auto element = LoadGraphicsFile("plan.dxf");   // UltraCanvasVectorElement
SaveGraphicsFile(element, "plan.svg");         // any of the ten formats
```

Loading covers the formats with readers (SVG, XAR, EMF, WMF, DXF, DWG*) and
yields an `UltraCanvasVectorElement` holding the editable `VectorDocument`;
saving covers the whole matrix from any such element. The
`IGraphicsPlugin` save interface (`GetSaveExtensions`/`SaveGraphics`) is new
with this plugin, and `UltraCanvasSupportedFormats` /
`UltraCanvasFileLoader::GetSupportedSaveExtensions` report the per-extension
load/save capabilities from it. The dedicated XAR/EPS/CDR viewer plugins
keep their richer rendering for shared load extensions — registration order
decides ownership (last registration wins), while saving always resolves to
this plugin.

## Tests

Each converter has a round-trip or independent-consumer test in `Tests/`
(all registered with CTest): `SVGConverterTest` (both directions plus
rasterization through the framework's librsvg pipeline), `XARWriterTest`
(through the XAR plugin reader), `EPSWriterTest` (through the EPS plugin's
PostScript interpreter, cross-checked with ghostscript), `CDRWriterTest`
(through the CDR plugin / libcdr), `PDFVectorWriterTest` (structure plus
ghostscript), `MetafileWriterTest` (EMF/WMF record-walk structure plus
LibreOffice rasterization, AI through ghostscript), `CADWriterTest` (DXF
through ezdxf — the reference DXF implementation: strict read, clean audit,
structure — plus LibreOffice; DWG through LibreDWG's dxf2dwg/dwgread when
installed), and `VectorFormatsPluginTest` (the whole matrix through the
plugin registry: save all ten formats via `SaveGraphicsFile`, load the
readable ones back via `LoadGraphicsFile` with geometry/style round-trip
checks, plus the supported-format inventory).

## See Also

- [UltraCanvasSVGExamples](UltraCanvasSVGExamples.md), [UltraCanvasXARExamples](UltraCanvasXARExamples.md), [UltraCanvasEPSExamples](UltraCanvasEPSExamples.md), [UltraCanvasCDRExamples](UltraCanvasCDRExamples.md) — the format plugins (rendering/UI elements)
