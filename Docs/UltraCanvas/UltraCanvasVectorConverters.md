# UltraCanvas Vector Format Converters

The Vector plugin (`ULTRACANVAS_PLUGIN_VECTOR`) converts between vector file formats and the framework's in-memory `VectorStorage::VectorDocument` model. Every converter implements `UltraCanvas::VectorConverter::IVectorFormatConverter` (`UltraCanvasVectorConverter.h`): `Import`/`ImportFromString`/`ImportFromStream` produce a `VectorDocument`, `Export`/`ExportToString`/`ExportToStream` serialize one, and `ValidateFile`/`ValidateData` sniff signatures.

**Headers:** `UltraCanvasVectorConverter.h` (interface, `SVGConverter`, `PDFVectorConverter`), `UltraCanvasXARConverter.h`, `UltraCanvasEPSConverter.h`, `UltraCanvasCDRConverter.h`, `UltraCanvasMetafileConverters.h` (`EMFConverter`, `WMFConverter`, `AIConverter`), `UltraCanvasCADConverters.h` (`DXFConverter`, `DWGConverter`), `UltraCanvasVectorFormatsPlugin.h` (the graphics plugin exposing the matrix to `LoadGraphicsFile` / `SaveGraphicsFile`).

## The matrix

| Format | Class | Read | Write | Fidelity notes |
|---|---|---|---|---|
| SVG | `SVGConverter` | ✓ | ✓ | Lossless both ways — the storage model is SVG-shaped. Gradients keep all stops, transforms stay attributes, text keeps spans. |
| XAR | `XARConverter` | ✓ (XAR plugin) | ✓ | Spec-correct record grammar; native shape records when axis-aligned, paths otherwise; linear / radial / conical gradients with every stop (multistage records), flat and gradient transparency with Xara's mixes, line transparency, shadows (`TAG_SHADOWCONTROLLER`), feathers (`TAG_FEATHER`), dash patterns, text stories; Xara's stock arrowheads as `TAG_ARROWHEAD` (start) / `TAG_ARROWTAIL` (end) line attributes with their reference and FIXED16 size, as Xara writes them, every other arrowhead, width profiles and brushes baked into plain shapes under a group carrying a `TAG_USERVALUE` marker (`UltraCanvas.LineGallery`) the reader rebuilds the stroke from. Reading is the XAR plugin's `XARDocument` (compressed files too) translated into the model — with `ULTRACANVAS_PLUGIN_XAR` off the converter only writes and `CanImport()` is false. Bevel, contour, blend, mould, ClipView, live effects, Xara brushes and pages are read as their contents or dropped, and reported; `TAG_DEFINEARROW` is a tag Xara never writes, and a reference to one reads as the straight arrow. |
| EPS | `EPSConverter` | – | ✓ | PostScript program restricted to operators the EPS plugin's interpreter knows. Opacity flattens toward white; gradients blend end stops. Reading via the EPS plugin. |
| CDR | `CDRConverter` | – | ✓ | Version-7 RIFF targeting libcdr's parser layouts (no public spec exists). Solid fills, outline state, fill opacity; text/bitmaps skipped. Reading via the CDR plugin. |
| PDF | `PDFVectorConverter` | – | ✓ | Hand-assembled PDF 1.4: base-14 fonts, ExtGState opacity, dashes, verified xref. Reading via the MuPDF PDF plugin. |
| EMF | `EMFConverter` | ✓ | ✓ | [MS-EMF] records: GDI paths with real beziers, geometric pens (caps/joins/user-style dashes), `ExtTextOutW` text with GDI anchoring. No alpha in GDI — opacity flattens toward white. The reader parses the GDI object table, path records, immediate polygon/polyline/bezier records and text (`TA_UPDATECP` chains merge back into spans). |
| WMF | `WMFConverter` | ✓ | ✓ | [MS-WMF] 16-bit records with the placeable header (twips). No bezier record — curves flatten to polylines; dashes approximate as `PS_DASH`. The reader covers the object table, polygon/polyline/rect/ellipse records and TextOut/ExtTextOut. |
| AI | `AIConverter` | ✓ | ✓ | Writing: modern `.ai` is PDF-based, so the output is the PDF writer's under the `.ai` extension — valid for Illustrator and every PDF consumer. Reading (`UltraCanvasAIReader.cpp`): a `.ai` saved *without* "Create PDF Compatible File" has an empty PDF page and keeps all of its artwork in the private `/AIPrivateData` streams, so a PDF engine renders it blank. The reader decodes those streams (ASCIIHex / ASCII85 / Flate) and interprets Illustrator's art language — path construction, painting with closepath on the lowercase operators, clipping (`W`), compound paths (`*u`/`*U`), groups, named layers, the graphics state, the grey/CMYK/RGB/spot colour operators and the AI9 transparency operator `Xy`. Legacy (v8 and earlier) EPS-based `.ai` files carry the same language in the open and read through the same parser; Illustrator's y-down ruler space and PostScript's y-up space both map onto the document page. Gradients and text are reported, not imported. A PDF-compatible `.ai` has no private data and is declined, so the caller falls back to the PDF plugin. |
| DXF | `DXFConverter` | ✓ | ✓ | R2000 tagged ASCII per Autodesk's public reference. Layers map to real DXF layers, fills become solid HATCH entities with exact spline boundary edges, strokes become LWPOLYLINE/SPLINE (exact piecewise-bezier NURBS) with lineweights and dash linetypes, true colour + full-palette nearest-ACI fallback. Opacity is reported, not written. The reader parses LINE/CIRCLE/ARC/ELLIPSE/LWPOLYLINE (bulges)/POLYLINE (2D, 3D, polyface and polygon meshes)/SPLINE/HATCH/SOLID/3DFACE/LEADER/TEXT/ATTRIB/MTEXT with the LAYER/LTYPE/STYLE tables, plus the BLOCKS section: INSERT/MINSERT expand nested, scaled, rotated, arrayed and mirrored (OCS extrusion) blocks with "0"-layer and ByBlock inheritance, and DIMENSION draws through its rendered block. Off/frozen layers, invisible entities and paper-space entities (when model space has content) are not imported. The page is the drawing's real extents (block content included, reconciled with `$EXTMIN`/`$EXTMAX`), at physical size when `$INSUNITS` declares a unit and the result is a usable page, otherwise scaled to a sensible point size, since drawing units are arbitrary; the unit and scale are recorded on the document, and layer-table properties (colour, lineweight, linetype, frozen/locked/plot flags) land on the `VectorLayer`s. Piecewise-bezier splines reproduce exactly, general NURBS sample via de Boor. |
| DWG | `DWGConverter` | ✓ | ✓* | Reading is native: `UltraCanvasDWGDecoder` decodes the R13–R2018 drawing database (AC1012–AC1032: bit-coded objects, the R2004+ encrypted header and LZ77-compressed pages, the R2007 Reed-Solomon pages, object map, CLASSES, block definitions) and renders it as DXF for the DXF reader — LINE/POINT/CIRCLE/ARC/ELLIPSE/LWPOLYLINE/POLYLINE (2D, 3D, polyface and polygon meshes)/SPLINE/HATCH/SOLID/TRACE/3DFACE/TEXT/ATTRIB/MTEXT/LEADER/INSERT+MINSERT/DIMENSION with the LAYER/LTYPE/STYLE tables, true colours, lineweights and visibility; 3D solids, images, proxies, tables and multileaders are reported and skipped. `DWGConverter::DecodeToDxf()` exposes the conversion. Writing delegates to GNU LibreDWG's `dxf2dwg` (`ULTRACANVAS_DXF2DWG` or PATH) — DWG has no public specification and the only open implementation is GPL, so it stays an optional external process; without it the export warns and declines (use DXF, AutoCAD's own exchange format). `dwg2dxf` is only a fallback for files the native decoder declines (pre-R13 drawings). The reader answers for the whole drawing family, not just `.dwg` — see [The DWG family](#the-dwg-family). |

All writers share the same document walk: styles resolve by inheritance down the tree, transforms accumulate and (except in SVG) bake into the emitted coordinates, and path normalisation — every `PathCommandType` down to absolute move/line/cubic segments, SVG arcs via endpoint-to-centre conversion — lives in `DataFormats/UltraCanvasVectorPathOps.h`.

Unsupported features never change meaning silently: each converter reports its fallbacks (gradient flattening, opacity flattening, skipped element types, encoding limits) through `ConversionOptions::WarningCallback`.

## The document model

`VectorStorage::VectorDocument` (`DataFormats/UltraCanvasVectorStorage.h`, in the core library — with `DataFormats/UltraCanvasVectorRenderer.h`, `DataFormats/UltraCanvasVectorPathOps.h` and the `UltraCanvasVectorElement` viewer, so a drawing can be held and drawn without this plugin) is the hub every converter meets: `Layers` → groups → elements, each element with an optional `Fill`/`Stroke` style, an optional `Matrix3x3` transform (double precision, row-major — `FromValues(a, b, c, d, e, f)` means x' = a·x + b·y + e, so SVG/PostScript `matrix()` values pass as `FromValues(a, c, b, d, e, f)`) and `GetBoundingBox()` in its parent's space. Group and document boxes are the union of their children with empty boxes skipped, then transformed; `HitTestDocument` carries the point through each ancestor's inverse transform so children of a transformed group (a block insert, a mirrored entity) are found where they are drawn.

**Units.** Coordinates and `Size` are always points (1/72 in). Readers of formats that carry a unit record it in `SourceUnit` (`LengthUnit`: `Point`, `Pixel`, `Inch`, `Foot`, `Yard`, `Mile`, `Mil`, `Millimeter`, `Centimeter`, `Decimeter`, `Meter`, `Kilometer`, `Micrometer`, `Nanometer`, or `Unspecified`) with the scale they applied in `PointsPerSourceUnit`, so `source = points / PointsPerSourceUnit` recovers the file's measurements; `PointsPerUnit()` and `LengthUnitSymbol()` are the helpers. The DXF reader sets them from `$INSUNITS`, using the physical scale when it is declared and yields a usable page (200–20 000 pt), and the DXF writer emits `$INSUNITS` and writes coordinates in the source unit when the document has one.

**CAD layers.** `VectorLayer` carries the layer-table properties of DXF/DWG drawings besides `Name`/`Visible`/`Locked`: `Frozen`, `Plottable`, `DefaultColor`, `DefaultStrokeWidth` (points), `LineTypeName` and `DefaultDashArray`. Entities keep their resolved ByLayer style, so these only feed writers with a layer table (the DXF writer emits colour, lineweight, linetype, frozen/locked/plot flags and marks hidden layers *off*).

**Effects, transparency and the line gallery** (phase 4 of [ArtCreatorVectorCanvasProposal](../Research/ArtCreatorVectorCanvasProposal.md)). `VectorElement::Effects` holds an optional `ShadowEffect` (`Kind` wall / floor / glow, `Offset`, `Blur` — the penumbra width, `Colour`, `Darkness`, and `FloorSquash` / `FloorShear` for floor shadows) and an optional `FeatherEffect` (`Radius`); they are per object, cloned with it, never inherited. `VectorStyle::Transparency` is an optional `TransparencyData`: a `Shape` (flat, linear, radial, conical), a flat `Level` or `Stops` of `{Position, Level}` along `Start`..`End` in the element's own space (Xara's convention: level 0 is opaque, 1 clear), and a `Mix` (`Mix`, `StainedGlass`, `Bleach`, `Contrast`, `Saturation`, `Darken`, `Lighten`, `Brightness`, `Luminosity`, `Hue`) that the renderer paints as the matching blend mode; `LevelAt(t)` interpolates. It sits beside the flat `Opacity`, which stays the plain normal-mix alpha every other format uses. `StrokeData` carries the line gallery: `StartArrow` / `EndArrow` (`ArrowheadData`: `Kind` triangle, open arrow, circle, square, diamond, bar - the tip on the line's end, `Scale` 1 = four line widths long - and Xara's eight stock arrowheads, straight, angled, rounded, spot, solid diamond, feather, feather 2, hollow diamond, with Xara's own geometry and placement, where `Scale` 1 is Xara's default size 3; `IsXaraArrowhead`), a `WidthProfile` of `WidthSample{T, Factor}` (two or more turn the stroke into a filled band; `WidthAt(t)`), and an optional `BrushData` (`Stamp` group drawn every `Spacing` stamp-widths, scaled to `Width * Scale`, rotated to the tangent). The geometry they draw is public — `BuildOutlinePath` (any shape's outline as path data), `FlattenPathData`, `PathEndpoints`, `ArrowheadOutline`, `VariableWidthOutline` — so the renderer and the XAR writer produce the same shapes.

**Rendering effects.** `VectorRenderer` draws an element with effects through groups: the shadow and the feather need a raster of the element's silhouette, which it draws black into an offscreen context at the current device scale, blurs (three box passes approximating a gaussian whose penumbra spans about 3.3 σ), and caches per object (`EffectCacheSize()`, `ClearCaches()`; an entry is replaced when the geometry, blur or zoom changes and the cache empties itself past 256 entries). The shadow is the raster painted as a colour mask at its offset (squashed and sheared from the bottom edge for floor shadows, unshifted for glows); the feather masks the element's own group with it; a transparency ramp masks the group with an alpha gradient and paints it with the mix's operator. Arrowheads, width bands and brush stamps are drawn from the outline after the fill.

The survey of what each converter reads and writes, the dead model surface and the planned steps live in [UltraCanvasVectorModelProposal](../Research/UltraCanvasVectorModelProposal.md).

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
auto fromDwg = DWGConverter().Import("plan.dwg");   // native, no external tool
std::string dxfText = DWGConverter::DecodeToDxf(dwgBytes);   // DWG -> DXF text

auto fromCad = DXFConverter().Import("plan.dxf");
auto fromEmf = EMFConverter().Import("clip.emf");
// null (with a warning) when the .ai keeps its artwork in its PDF page:
// that file is the PDF plugin's to render.
auto fromAi  = AIConverter().Import("artwork.ai");
```

## The DWG family

A drawing does not always arrive named `.dwg`. AutoCAD writes the *same*
drawing database — same `AC10xx` version header, same object map — to four
suffixes, and copies it verbatim to a fifth:

| Suffix | What AutoCAD writes | How it is recognised |
|---|---|---|
| `.dwg` | the drawing | by extension |
| `.dwt` | drawing template | by extension |
| `.dws` | drawing standards | by extension |
| `.sv$` | automatic save | by extension |
| `.bak` | backup — a verbatim copy of the drawing | by content |

The first four are advertised extensions: `DWGConverter::GetFileExtensions()`
lists them, the Vector plugin's `GetSupportedExtensions()` reports them as
loadable, `GraphicsFormatDetector` files them as `Vector`, and the Filer
names them (AutoCAD Template / Standards / Autosave). They decode through the
same native reader as a `.dwg`, so they open, preview and rasterize
identically.

`.bak` is deliberately not on that list. AutoCAD's backup *is* a drawing, but
the suffix belongs to no format — editors, package managers and databases all
write `.bak` files — so claiming every one of them as CAD would be wrong. It
is settled by content instead: the plugin reads the file's first six bytes and
takes it only when they carry the `AC10xx` magic. That check is what
`DWGConverter::ValidateFile()` / `ValidateData()` already do, and the registry
reaches it because a suffix no plugin advertises falls through to the plugins'
own `CanHandle()`.

```cpp
using namespace UltraCanvas::VectorConverter;

DWGConverter::IsDrawingExtension("plan.dwt");            // true  (also .dwg/.dws/.sv$)
DWGConverter::IsAmbiguousDrawingExtension("plan.bak");   // true  - ask ValidateFile()
DWGConverter::IsDrawingExtension("plan.dxf");            // false - a different format

UltraCanvasVectorFormatsPlugin::LoadVectorDocument("Plan_1_1_8421.sv$");   // a drawing
LoadGraphicsFile("Plan.bak");   // a drawing when its header says so, else null
```

Writing is unchanged: the save list names `.dwg` alone, because the alias
suffixes are what drawings arrive under, not what a "save as" offers.

## Previews: what the media viewer and the Filer show

Core owns the vector document model and the renderer that draws one, but no
reader — every reader is in this plugin, which links *against* core. So the
media viewer's preview pane and the Filer's thumbnails could only show a
drawing two ways: rasterized by libvips (svg/svgz, and eps/ps where the
libvips build has a PostScript loader), or as the preview bitmap some formats
store inside themselves. A DXF or a DWG is neither, so it showed nothing —
in a build whose Vector plugin had just read the same drawing for the
FileLoader.

`RegisterVectorFormatsPlugin()` now also installs a **vector preview
provider** (`UltraCanvasVectorPreview.h`), the same seam
`UltraCanvasModelPreview.h` is for 3D formats: core asks "turn this path into
a `VectorDocument`" and "is this one you read", and the plugin answers. With
it registered:

- `UltraCanvasMediaViewer` opens a drawing in `UltraCanvasVectorElement` — the
  document itself, sharp at any zoom, not a bitmap of it;
- the Filer's thumbnail workers render the document at the tile's size
  (serialized: unlike the other preview producers, drawing a document touches
  the process-wide font machinery);
- the Filer's **Display > Thumbnails** and **Display > Detail view** settings
  pages stop greying the formats out, because `GetPreviewableFormats()` asks
  the same seam.

```cpp
RegisterVectorFormatsPlugin();                    // once, at startup

CanPreviewVectorExtension("dwg");                 // true
LoadVectorPreviewDocument("plan.dxf");            // a VectorDocument
RenderVectorPreviewPixmap("plan.dwg", 256, 256);  // a UCPixmap for a tile
```

An application that registers no plugin is unchanged: the seam answers no to
everything and the old two ways are all there is.

## Registration: automatic, once, for every application

Nothing above happens until the plugin is registered, and that used to be each
application's job — which is why UltraFiler showed none of these formats while
carrying every reader for them.

Linking the **`UltraCanvasAllFormats`** object library is now the whole of it:
every format plugin the build produced registers itself before `main()`, with
no application code. CMake defines `ULTRACANVAS_HAS_<PLUGIN>` for each plugin
target it actually built, and the registrar
(`UltraCanvas/Plugins/UltraCanvasAllFormats.cpp`) calls what those defines
admit exists. Adding a plugin to the framework reaches every application that
links the library, instead of every application having to learn its name.

It is an `OBJECT` library on purpose — a linker keeps only the object files of
a static library that something references, and nothing references a
registrar, so in a `.a` it would be dropped and the registration would
silently never happen.

Registration order is ownership order: the dedicated CDR, XAR and EPS viewer
plugins register *after* this one, because the registry lets the last
registration win a shared extension and for those formats they are the better
reader. This plugin keeps what only it reads (DXF, the DWG family, EMF, WMF)
and remains the only writer, since save dispatch matches on
`GetSaveExtensions` rather than the extension map.

```cpp
// An application does this and nothing else:
//     target_link_libraries(MyApp PRIVATE ${ULTRACANVAS_LIBRARIES}
//                                         UltraCanvasAllFormats)

AutoRegisteredFormatPlugins();   // {"Vector","Models","CDR","XAR","EPS"}
RegisterAllFormatPlugins();      // harmless: the automatic pass already ran
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

Loading covers the formats with readers (SVG, XAR, EMF, WMF, DXF and the DWG
family — `.dwg`, `.dwt`, `.dws`, `.sv$`, and a `.bak` whose header says it is
a drawing) and
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
checks, plus the supported-format inventory). `VectorModelTest` covers the shared model itself: `Matrix3x3` arithmetic and double precision, group/document bounds through nested transforms, hit testing through layer and group transforms, the unit helpers, and a DXF round trip of units and layer properties. `DWGReaderTest` covers the CAD import path: the DXF reader's block machinery on a synthetic drawing (nested, arrayed and mirrored inserts, dimension blocks, inheritance, visibility, geometry-derived extents) and the native DWG decoder on `Tests/DataFormats/cad-test-document.r2000.dwg`, the framework's own test document converted with dxf2dwg; extra `.dwg` files on its command line are decoded and reported (`DWG_TEST_SVG_DIR` exports them as SVG). It also covers the family dispatch above: the fixture's own bytes must load under each of `.dwg`, `.dwt`, `.dws`, `.sv$` and `.bak`, and a `.bak` holding anything else must not be claimed.

## See Also

- [UltraCanvasVectorRaster](UltraCanvasVectorRaster.md) — the other direction
  out of these formats: a drawing rasterized into an editable `UCRasterLayer`
  at a chosen pixel size (what a bitmap editor does with a dropped SVG)
- [UltraCanvasSVGExamples](UltraCanvasSVGExamples.md), [UltraCanvasXARExamples](UltraCanvasXARExamples.md), [UltraCanvasEPSExamples](UltraCanvasEPSExamples.md), [UltraCanvasCDRExamples](UltraCanvasCDRExamples.md) — the format plugins (rendering/UI elements)
