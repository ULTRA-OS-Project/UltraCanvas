# UltraCanvas Vector Format Converters

<!-- doc-check: std::string dwgBytes; std::vector<uint8_t> svgBytes; -->

The Vector plugin (`ULTRACANVAS_PLUGIN_VECTOR`) converts between vector file formats and the framework's in-memory `VectorStorage::VectorDocument` model. Every converter implements `UltraCanvas::VectorConverter::IVectorFormatConverter` (`UltraCanvasVectorConverter.h`): `Import`/`ImportFromString`/`ImportFromStream` produce a `VectorDocument`, `Export`/`ExportToString`/`ExportToStream` serialize one, and `ValidateFile`/`ValidateData` sniff signatures.

**Headers:** `UltraCanvasVectorConverter.h` (interface, `SVGConverter`, `PDFVectorConverter`), `UltraCanvasXARConverter.h`, `UltraCanvasEPSConverter.h`, `UltraCanvasCDRConverter.h`, `UltraCanvasODGConverter.h` (`ODGConverter`), `UltraCanvasMetafileConverters.h` (`EMFConverter`, `WMFConverter`, `AIConverter`), `UltraCanvasCADConverters.h` (`DXFConverter`, `DWGConverter`), `UltraCanvasVectorFormatsPlugin.h` (the graphics plugin exposing the matrix to `LoadGraphicsFile` / `SaveGraphicsFile`).

## Contents

- [The matrix](#the-matrix)
- [The document model](#the-document-model)
- [Usage](#usage)
- [The DWG family](#the-dwg-family)
- [Previews: what the media viewer and the Filer show](#previews-what-the-media-viewer-and-the-filer-show)
- [Registration: automatic, once, for every application](#registration-automatic-once-for-every-application)
- [The graphics plugin (load and save through the registry)](#the-graphics-plugin-load-and-save-through-the-registry)
- [Tests](#tests)
- [See Also](#see-also)

## The matrix

| Format | Class | Read | Write | Fidelity notes |
|---|---|---|---|---|
| SVG | `SVGConverter` | ✓ | ✓ | Lossless both ways — the storage model is SVG-shaped. Gradients keep all stops, `gradientTransform` and `spreadMethod`, transforms stay attributes, `fill-rule` is kept per shape (see *Fill rule* below), text keeps spans. Reading also takes `<clipPath>` / `clip-path` (as `VectorClipPath` definitions) and `<image>`s that are themselves SVG (`data:image/svg+xml`), which become editable groups rather than pixels. `<pattern>` fills are read (tile, `patternUnits`, `viewBox`, `patternTransform`, content, the `href` chain) and written, so a bitmap fill - `<pattern><image/></pattern>` - goes both ways; see *Bitmap fills* below. |
| XAR | `XARConverter` | ✓ (XAR plugin) | ✓ | Spec-correct record grammar; native shape records when axis-aligned, paths otherwise; linear / radial / conical gradients with every stop (multistage records), flat and gradient transparency with Xara's mixes, line transparency, shadows (`TAG_SHADOWCONTROLLER`), feathers (`TAG_FEATHER`), dash patterns, text stories; clip views, contours, blends, moulds and bevels as Xara's own controller records (`TAG_CLIPVIEWCONTROLLER`, `TAG_CONTOURCONTROLLER`, `TAG_BLEND` with its blenders, `TAG_MOULD_ENVELOPE` / `_PERSPECTIVE` with the shape and sources, `TAG_BEVEL`) read back as the model's containers and effects; Xara's stock arrowheads as `TAG_ARROWHEAD` (start) / `TAG_ARROWTAIL` (end) line attributes with their reference and FIXED16 size, as Xara writes them, every other arrowhead, width profiles and brushes baked into plain shapes under a group carrying a `TAG_USERVALUE` marker (`UltraCanvas.LineGallery`) the reader rebuilds the stroke from. Reading is the XAR plugin's `XARDocument` (compressed files too) translated into the model — with `ULTRACANVAS_PLUGIN_XAR` off the converter only writes and `CanImport()` is false. Pictures go both ways as `TAG_DEFINEBITMAP_JPEG` / `_PNG` with `TAG_NODE_BITMAP` (a turned or skewed one keeps its turn as the image's `Transform`; a contone one is tinted), and bitmap and contone fills as `TAG_BITMAPFILL` - bitmap fills in the model, on the fill's parallelogram. Xara stores a PNG's transparency where PNG keeps opacity; the reader turns it round and the writer stores it Xara's way. Fractal and noise fills read as their first colour. Live effects, Xara brushes and pages are read as their contents or dropped, and reported; `TAG_DEFINEARROW` is a tag Xara never writes, and a reference to one reads as the straight arrow. |
| EPS | `EPSConverter` | – | ✓ | PostScript program restricted to operators the EPS plugin's interpreter knows. Opacity flattens toward white; gradients blend end stops. Reading via the EPS plugin. |
| CDR | `CDRConverter` | ✓ (CDR plugin) | ✓ | Writing: version-7 RIFF targeting libcdr's parser layouts (no public spec exists). Solid fills, outline state, fill opacity; text/bitmaps skipped. Reading: the CDR plugin's libcdr parse, turned into SVG by librevenge's generator and read by `SVGConverter`, first page only. libcdr is the patched copy in `third_party/libcdr` (see its README): it keeps bitmap transparency masks (drop shadows, cut-out overlays) and draws PowerClip contents clipped to their frames — both lost by upstream libcdr 0.1.7 and by LibreOffice. With only a system libcdr available the plugin links that instead; the converter then restores the masks itself from the file's raw bitmap records, and PowerClip contents stay missing. Built on Linux, macOS and Windows (MSYS2 CLANG64/CLANGARM64: librevenge, lcms2, ICU and Boost from MSYS2 packages). Without `ULTRACANVAS_PLUGIN_CDR`, or when librevenge is missing, `CanImport()` is false. |
| PDF | `PDFVectorConverter` | ✓ ‡ | ✓ | Hand-assembled PDF 1.4: base-14 fonts, ExtGState opacity, dashes, verified xref. Reading (‡ only with MuPDF, `ULTRACANVAS_PDF_MUPDF`) goes through the PDF plugin: `ImportPDFAsVectorDocument` (`UltraCanvasPDF.h`) runs the first page through a MuPDF device that builds the document - the page format (crop box, `/Rotate`, `/UserUnit`) as `Size` and `ViewBox`, paths with fill and stroke merged, text as lines of positioned words (outlines when a font has no character codes), JPEG/PNG streams embedded unchanged and every other picture as PNG, clip paths and clipping text, transparency groups, tiling patterns repeated over their area; shadings as pictures, soft masks reported and dropped. Opened for editing only: `pdf` is the provider's `ImportExtensions`, not a previewed or graphics-registry format, so viewers keep their own PDF view. |
| EMF | `EMFConverter` | ✓ | ✓ | [MS-EMF] records: GDI paths with real beziers, geometric pens (caps/joins/user-style dashes), `ExtTextOutW` text with GDI anchoring. No alpha in GDI — opacity flattens toward white. The reader parses the GDI object table, path records, immediate polygon/polyline/bezier records and text (`TA_UPDATECP` chains merge back into spans). It also plays EMF+ ([MS-EMFPLUS], what GDI+ writes, `UltraCanvasEMFPlus.h`): solid, hatch and linear-gradient brushes with their alpha, pens, paths, polygons, rectangles, ellipses, pies, arcs, curves, Beziers and `DrawString` text, through the world, page and container transforms. In a dual file each stretch of EMF+ drawing is followed by a GDI copy, opaque and sometimes elsewhere on the page: a stretch drawn from EMF+ skips its copy, and one holding a record the reader cannot draw (an image, a region fill, a driver string, a texture or path-gradient brush) is drawn from its copy instead and reported. GDI drawing after an EMF+ `GetDC` is drawn. Clipping is not applied on either side. |
| WMF | `WMFConverter` | ✓ | ✓ | [MS-WMF] 16-bit records with the placeable header (twips). No bezier record — curves flatten to polylines; dashes approximate as `PS_DASH`. The reader covers the object table, polygon/polyline/rect/ellipse records and TextOut/ExtTextOut. A WMF that carries its EMF in `META_ESCAPE_ENHANCED_METAFILE` records, as GDI+ writes one, is read from that EMF (EMF+ and its alpha included), as LibreOffice reads it; the WMF records are its copy for players that cannot. |
| AI | `AIConverter` | ✓ | ✓ | Writing: modern `.ai` is PDF-based, so the output is the PDF writer's under the `.ai` extension — valid for Illustrator and every PDF consumer. Reading (`UltraCanvasAIReader.cpp`): a `.ai` saved *without* "Create PDF Compatible File" has an empty PDF page and keeps all of its artwork in the private `/AIPrivateData` streams, so a PDF engine renders it blank. The reader decodes those streams (ASCIIHex / ASCII85 / Flate) and interprets Illustrator's art language — path construction, painting with closepath on the lowercase operators, clipping (`W`), compound paths (`*u`/`*U`), groups, named layers, the graphics state, the grey/CMYK/RGB/spot colour operators and the AI9 transparency operator `Xy`. Legacy (v8 and earlier) EPS-based `.ai` files carry the same language in the open and read through the same parser; Illustrator's y-down ruler space and PostScript's y-up space both map onto the document page. Gradients and text are reported, not imported. A PDF-compatible `.ai` has no private data and is declined, so the caller falls back to the PDF plugin. **Render trap:** Ghostscript, poppler, ImageMagick and every PDF viewer show such a file as a blank page, correctly — never use them as the reference for a private-data `.ai`; see [UltraCanvasAIExamples](UltraCanvasAIExamples.md#render-trap-do-not-check-an-ai-against-a-pdf-renderer). |
| DXF | `DXFConverter` | ✓ | ✓ | R2000 tagged ASCII per Autodesk's public reference. Layers map to real DXF layers, fills become solid HATCH entities with exact spline boundary edges, strokes become LWPOLYLINE/SPLINE (exact piecewise-bezier NURBS) with lineweights and dash linetypes, true colour + full-palette nearest-ACI fallback. Opacity is reported, not written. The reader parses LINE/CIRCLE/ARC/ELLIPSE/LWPOLYLINE (bulges)/POLYLINE (2D, 3D, polyface and polygon meshes)/SPLINE/HATCH/SOLID/3DFACE/LEADER/TEXT/ATTRIB/MTEXT with the LAYER/LTYPE/STYLE tables, plus the BLOCKS section: INSERT/MINSERT expand nested, scaled, rotated, arrayed and mirrored (OCS extrusion) blocks with "0"-layer and ByBlock inheritance, and DIMENSION draws through its rendered block. Off/frozen layers, invisible entities and paper-space entities (when model space has content) are not imported. The page is the drawing's real extents (block content included, reconciled with `$EXTMIN`/`$EXTMAX`), at physical size when `$INSUNITS` declares a unit and the result is a usable page, otherwise scaled to a sensible point size, since drawing units are arbitrary; the unit and scale are recorded on the document, and layer-table properties (colour, lineweight, linetype, frozen/locked/plot flags) land on the `VectorLayer`s. Piecewise-bezier splines reproduce exactly, general NURBS sample via de Boor. |
| DWG | `DWGConverter` | ✓ | ✓* | Reading is native: `UltraCanvasDWGDecoder` decodes the R13–R2018 drawing database (AC1012–AC1032: bit-coded objects, the R2004+ encrypted header and LZ77-compressed pages, the R2007 Reed-Solomon pages, object map, CLASSES, block definitions) and renders it as DXF for the DXF reader — LINE/POINT/CIRCLE/ARC/ELLIPSE/LWPOLYLINE/POLYLINE (2D, 3D, polyface and polygon meshes)/SPLINE/HATCH/SOLID/TRACE/3DFACE/TEXT/ATTRIB/MTEXT/LEADER/INSERT+MINSERT/DIMENSION with the LAYER/LTYPE/STYLE tables, true colours, lineweights and visibility; 3D solids, images, proxies, tables and multileaders are reported and skipped. `DWGConverter::DecodeToDxf()` exposes the conversion. Writing delegates to GNU LibreDWG's `dxf2dwg` (`ULTRACANVAS_DXF2DWG` or PATH) — DWG has no public specification and the only open implementation is GPL, so it stays an optional external process; without it the export warns and declines (use DXF, AutoCAD's own exchange format). `dwg2dxf` is only a fallback for files the native decoder declines (pre-R13 drawings). The reader answers for the whole drawing family, not just `.dwg` — see [The DWG family](#the-dwg-family). |
| ODG | `ODGConverter` | ✓ | ✓ | OpenDocument Drawing, LibreOffice Draw's format: the `.odg` package, the `.otg` template and the flat-XML `.fodg`. Reading takes the first page (others are reported): `draw:rect` / `ellipse` / `circle` (sections, cuts, arcs) / `line` / `polyline` / `polygon` / `path` / `regular-polygon` / `connector` / `measure` / `caption` / `frame` (pictures from `Pictures/` or inline, text boxes) / `g`, and `draw:custom-shape` by evaluating its `draw:enhanced-geometry` - every enhanced-path command (`M L C Q Z N F S T U A B W V X Y G`, the shading commands), the equations with their functions, the modifiers, stretch points and mirroring - so LibreOffice's basic shapes, stars, arrows, flowchart and callout shapes arrive as editable paths; the shading LibreOffice gives some stock shapes from its preset tables (a can's lid, a cube's faces, a bevel's sides) is applied by type. `draw:transform` in ODF's left-to-right order with LibreOffice's counter-clockwise rotation. Styles resolve through the default style, the parent chain and the automatic styles: solid fills, gradients (linear, axial and radial exactly, with LibreOffice's multi-colour stops; ellipsoid with its outline and angle - a radial gradient its `Transform` stretches and turns, written back as an ellipsoid - though LibreOffice narrows its middle to a line where a radial gradient's contours keep their shape; rectangular and square as the ellipse and the circle of their reach), hatches as lines clipped to the shape (a `VectorClipView`), bitmap fills (stretched, tiled or placed), fill opacity and opacity gradients (as `TransparencyData`), stroke colour / width / opacity / dashes / joins / caps, arrowheads mapped onto the model's gallery, shadows; shapes are filled even-odd (`VectorStyle::ShapeFillRule`), as LibreOffice fills them. Text in shapes and text boxes keeps its paragraphs, spans (font, size, weight, slant, colour, underline, strike-through), padding, alignment and LibreOffice's automatic font colour, wrapped to the frame on an estimate of the glyph widths. Layers keep their names, visibility and lock (one layer when their shapes interleave, to keep the stacking order); master-page objects are drawn beneath; page size, background and `meta.xml`'s title, description and author are read. Writing: one `draw:layer` per model layer, transforms folded into page coordinates (a turned rectangle or ellipse becomes a `draw:path`; a turned text or picture keeps a `draw:transform`), paths placed by their curve bounds and filled ones closed - LibreOffice fills a path only when every subpath is closed - text as auto-growing text boxes, pictures embedded under `Pictures/` (inline in `.fodg`), the page drawn 256 pixels on its long side as `Thumbnails/thumbnail.png` (what file managers and the Filer show without a reader), gradients with their stops moved onto ODF's axis, dashes, arrowheads (LibreOffice's marker names), flat and gradient transparency and shadows as named styles; straight lines in a clip view (a hatch read back) are clipped to the outline. Conical and mesh gradients, patterns, clip paths, masks, blends, moulds, width profiles and brushes are written as their nearest plain form and reported. |

All writers share the same document walk: styles resolve by inheritance down the tree, transforms accumulate and (except in SVG) bake into the emitted coordinates, and path normalisation — every `PathCommandType` down to absolute move/line/cubic segments, SVG arcs via endpoint-to-centre conversion — lives in `DataFormats/UltraCanvasVectorPathOps.h`.

Unsupported features never change meaning silently: each converter reports its fallbacks (gradient flattening, opacity flattening, skipped element types, encoding limits) through `ConversionOptions::WarningCallback`.

## The document model

`VectorStorage::VectorDocument` (`DataFormats/UltraCanvasVectorStorage.h`, in the core library — with `DataFormats/UltraCanvasVectorRenderer.h`, `DataFormats/UltraCanvasVectorPathOps.h` and the `UltraCanvasVectorElement` viewer, so a drawing can be held and drawn without this plugin) is the hub every converter meets: `Layers` → groups → elements, each element with an optional `Fill`/`Stroke` style, an optional `Matrix3x3` transform (double precision, row-major — `FromValues(a, b, c, d, e, f)` means x' = a·x + b·y + e, so SVG/PostScript `matrix()` values pass as `FromValues(a, c, b, d, e, f)`) and `GetBoundingBox()` in its parent's space. Group and document boxes are the union of their children with empty boxes skipped, then transformed; `HitTestDocument` carries the point through each ancestor's inverse transform so children of a transformed group (a block insert, a mirrored entity) are found where they are drawn.

**Bitmap fills.** A picture as a fill is a `PatternData` whose `Content` is one `VectorImage` covering its `ViewBox` (the picture's pixels) - SVG's `<pattern><image/></pattern>`. `MakeBitmapFill(source, mimeType, pixelWidth, pixelHeight, tile)` makes one with the tile as a rectangle in the element's own space, and the overload taking `topLeft`, `topRight`, `bottomLeft` with it as a parallelogram (a unit tile and the parallelogram as the pattern's `Transform`, which is how Xara's three fill points are held); `PatternTileCorners` reads any pattern's first tile back as those corners and `SetPatternTileCorners` moves it. `BitmapFillImage` gives a bitmap fill's picture (null for any other pattern) and `PatternOf` a fill's pattern. The renderer draws every pattern - a bitmap fill or a vector one - by drawing one tile offscreen at the resolution it is painted at (at most 4096 pixels a side, and a picture no finer than twice its own pixels) and repeating it in every direction; the tile is cached by content and size. In user-space units the tile lives in the element's own space, so it moves, scales and turns with the element's `Transform`.

<!-- doc-check: const std::string source; -->
```cpp
using namespace UltraCanvas::VectorStorage;
auto rect = std::make_shared<VectorRect>();
rect->Bounds = Rect2Dd(0, 0, 200, 100);
// a 64 x 64 picture, one copy every 50 units, repeated over the rectangle
rect->Style.Fill = MakeBitmapFill(source, "image/png", 64, 64, Rect2Dd(0, 0, 50, 50));
// the same picture turned a quarter: its top edge running down the page
rect->Style.Fill = MakeBitmapFill(source, "image/png", 64, 64,
                                  Point2Dd(50, 0), Point2Dd(50, 50), Point2Dd(0, 0));
```

**Gradients.** A linear or radial gradient's own `Transform` is SVG's `gradientTransform`: its geometry stays in gradient space and the renderer places the pattern by user = box × `Transform` × point, the box being the element's bounding box for `ObjectBoundingBox` units (so a radial gradient there stretches with a non-square box) and nothing for `UserSpaceOnUse`. That is how an elliptical gradient is held: a unit circle stretched and turned, as the ODG reader reads LibreOffice's ellipsoid. `SpreadMethod` (pad / reflect / repeat) is drawn too. An `ObjectBoundingBox` radial gradient *without* a `Transform` keeps the renderer's older reading - the circle of the box's longer side - which the readers that build one rely on.

**Fill rule.** `VectorStyle::ShapeFillRule` (non-zero by default, even-odd for a shape whose overlapping subpaths leave holes) is drawn by the renderer and carried by SVG (`fill-rule`, resolved through inheritance and the stylesheet on reading, since the model does not inherit it, and written on shapes only), PDF (`f*` / `B*`, and so AI), EPS (`eofill`), and EMF and WMF (`SETPOLYFILLMODE` per shape - ALTERNATE even-odd, WINDING non-zero - read back the same way, GDI's ALTERNATE being the default). XAR, CorelDRAW, DXF (and so DWG) and ODG fill only even-odd - Xara's default and its winding record's byte 0, libcdr's curves, a hatch's odd parity, LibreOffice - and their readers mark what they read so (a DXF hatch of style 2, "ignore islands", stays non-zero). Writing a non-zero shape to them, the writer keeps only the subpaths that bound its fill (`NonZeroAsEvenOdd`, `UltraCanvasVectorFillRule.h`): a subpath whose two sides differ in filled-ness under non-zero. Filled even-odd, those fill exactly what the shape fills non-zero while its subpaths do not cross (rings, holes, glyphs); crossing ones are reported. When subpaths are dropped from an outlined shape, the fill and the full outline are written as two objects. EMF and WMF do both - the WINDING mode and the bounding subpaths - because LibreOffice, among other readers, ignores WINDING; checked by rendering the writers' output in LibreOffice 24.2.

**Checked in LibreOffice.** Reading a writer's output back with our own reader cannot catch a file that only our reader draws as meant - before the fill-rule fix, every EMF, WMF, CDR and ODG round trip passed while LibreOffice drew the rings wrong. The `Vector output in LibreOffice` workflow (`.github/workflows/vector-libreoffice.yml`) runs on changes to this plugin: `VectorExportFixtures` (`Tests/VectorExportFixtures.cpp`) writes fixture drawings - the three fill-rule rings, and coloured shapes - through every writer with a manifest of probe points, LibreOffice 24.2 converts each file to PNG, and `scripts/check_libreoffice_render.py` finds the drawing on the page and checks each probe. EMF, WMF, SVG (and `.svgz`), ODG (and `.otg`, `.fodg`), PDF (and `.ai`), CDR and DXF are checked; LibreOffice does not read our XAR, EPS or PostScript. Four files LibreOffice's own import draws wrong are listed as expected failures with the reason, and each must keep failing: its PDF import fills every shape even-odd, and its DXF import drops a hatch whose boundary has curved edges. Run it locally with `python3 scripts/check_libreoffice_render.py --fixtures build/bin/VectorExportFixtures`.

**Units.** Coordinates and `Size` are always points (1/72 in). Readers of formats that carry a unit record it in `SourceUnit` (`LengthUnit`: `Point`, `Pixel`, `Inch`, `Foot`, `Yard`, `Mile`, `Mil`, `Millimeter`, `Centimeter`, `Decimeter`, `Meter`, `Kilometer`, `Micrometer`, `Nanometer`, or `Unspecified`) with the scale they applied in `PointsPerSourceUnit`, so `source = points / PointsPerSourceUnit` recovers the file's measurements; `PointsPerUnit()` and `LengthUnitSymbol()` are the helpers. The DXF reader sets them from `$INSUNITS`, using the physical scale when it is declared and yields a usable page (200–20 000 pt), and the DXF writer emits `$INSUNITS` and writes coordinates in the source unit when the document has one.

**CAD layers.** `VectorLayer` carries the layer-table properties of DXF/DWG drawings besides `Name`/`Visible`/`Locked`: `Frozen`, `Plottable`, `DefaultColor`, `DefaultStrokeWidth` (points), `LineTypeName` and `DefaultDashArray`. Entities keep their resolved ByLayer style, so these only feed writers with a layer table (the DXF writer emits colour, lineweight, linetype, frozen/locked/plot flags and marks hidden layers *off*).

**Effects, transparency and the line gallery** (phase 4 of [ArtCreatorVectorCanvasProposal](../Research/ArtCreatorVectorCanvasProposal.md)). `VectorElement::Effects` holds an optional `ShadowEffect` (`Kind` wall / floor / glow, `Offset`, `Blur` — the penumbra width, `Colour`, `Darkness`, and `FloorSquash` / `FloorShear` for floor shadows) and an optional `FeatherEffect` (`Radius`); they are per object, cloned with it, never inherited. `VectorStyle::Transparency` is an optional `TransparencyData`: a `Shape` (flat, linear, radial, conical), a flat `Level` or `Stops` of `{Position, Level}` along `Start`..`End` in the element's own space (Xara's convention: level 0 is opaque, 1 clear), and a `Mix` (`Mix`, `StainedGlass`, `Bleach`, `Contrast`, `Saturation`, `Darken`, `Lighten`, `Brightness`, `Luminosity`, `Hue`) that the renderer paints as the matching blend mode; `LevelAt(t)` interpolates. It sits beside the flat `Opacity`, which stays the plain normal-mix alpha every other format uses. `StrokeData` carries the line gallery: `StartArrow` / `EndArrow` (`ArrowheadData`: `Kind` triangle, open arrow, circle, square, diamond, bar - the tip on the line's end, `Scale` 1 = four line widths long - and Xara's eight stock arrowheads, straight, angled, rounded, spot, solid diamond, feather, feather 2, hollow diamond, with Xara's own geometry and placement, where `Scale` 1 is Xara's default size 3; `IsXaraArrowhead`), a `WidthProfile` of `WidthSample{T, Factor}` (two or more turn the stroke into a filled band; `WidthAt(t)`), and an optional `BrushData` (`Stamp` group drawn every `Spacing` stamp-widths, scaled to `Width * Scale`, rotated to the tangent). The geometry they draw is public — `BuildOutlinePath` (any shape's outline as path data), `FlattenPathData`, `PathEndpoints`, `ArrowheadOutline`, `VariableWidthOutline`, `BrushStampPlacements` (where a brush stamps along a flattened subpath, as matrices from the stamp's space) — so the renderer and the XAR and SVG writers produce the same shapes.

**Depth: containers, contour, bevel, booleans** (phase 5 of the proposal, first slice). Three group kinds (`IsGroupType`): `VectorClipView` (its first `Keyholes` children are the clip shapes, not drawn; the rest draw clipped to their union), `VectorBlend` (`Steps` intermediate shapes between each pair of children, outlines resampled to a common count and start-matched, colours run as `ColourBlendKind` fade / rainbow / alt rainbow / constant, strokes and opacity interpolated; `OneToOne`, `Antialiased`, `Tangential` and Xara's bias / gain profiles kept for the round trip) and `VectorMould` (`Kind` `Envelope` or `Perspective`; the children warped from `SourceBounds` — their joint bounds when empty — into `Shape`, a closed four-sided path starting at the source's top-left corner and running along its top, right, bottom and left edges: four cubics for an envelope, a Coons patch, four lines for a perspective, a projective map; `Warp`, `ShapeCorners`, `IdentityShape`; text and images move to their moulded anchor). Two more effects on `VectorElement::Effects`: `ContourEffect` (`Steps` rings between the outline and a copy `Width` units out — or in, when negative — each filled with the colour between the object's fill and `Colour`; outward rings draw behind the object, inward ones over it) and `BevelEffect` (`Kind`: Xara's fifteen profiles, `Indent`, `LightAngle` and `Tilt` in degrees, `Contrast`, `Outer`). The geometry under them is `DataFormats/UltraCanvasVectorGeometry.h`: `PolygonBoolean` / `PathBoolean` (union, subtract, intersect, exclude, each input with its own fill rule; a planar-map clipper that splits every edge at every crossing, classifies each piece by the winding numbers on its two sides and links the separating pieces into consistently wound rings), `SlicePath`, `OffsetPolygons` / `OffsetPath` (round, mitre or bevel joins through the same clipper), `FlattenToPolygons`, `PolygonsToPath`, `PolygonSetArea`, `WindingNumber`, `PolygonSetContains`. The editing layer's `CombineShapes` is Xara's Combine Shapes over it.

**Rendering effects.** `VectorRenderer` draws an element with effects through groups: the shadow and the feather need a raster of the element's silhouette, which it draws black into an offscreen context at the current device scale, blurs (three box passes approximating a gaussian whose penumbra spans about 3.3 σ), and caches per object (`EffectCacheSize()`, `ClearCaches()`; an entry is replaced when the geometry, blur or zoom changes and the cache empties itself past 256 entries). The shadow is the raster painted as a colour mask at its offset (squashed and sheared from the bottom edge for floor shadows, unshifted for glows); the feather masks the element's own group with it; a transparency ramp masks the group with an alpha gradient and paints it with the mix's operator. Arrowheads, width bands and brush stamps are drawn from the outline after the fill. Contour rings come from the offsetter and are cached with the geometry, width and step count; the bevel's highlight and shadow masks come from a distance transform of the silhouette shaped by the profile and lit from the effect's angle and tilt, cached like the effect rasters (`EffectCacheSize()` counts all three caches).

The survey of what each converter reads and writes, the dead model surface and the planned steps live in [UltraCanvasVectorModelProposal](../Research/UltraCanvasVectorModelProposal.md).

## Usage

```cpp
#include "UltraCanvasVectorConverter.h"      // SVGConverter, PDFVectorConverter
#include "UltraCanvasXARConverter.h"
#include "UltraCanvasEPSConverter.h"
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasMetafileConverters.h"   // EMFConverter, WMFConverter, AIConverter
#include "UltraCanvasCADConverters.h"        // DXFConverter, DWGConverter

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
  the same seam;
- `UltraCanvasImageElement` and `UCImage` show an SVG in a build whose libvips
  has no `svgload` - the WebAssembly sysroot's - by reading it through the
  seam and rendering the kept document with the core's vector renderer at
  each requested size; every SVG icon of a WebAssembly page comes from here.
  A libvips that refuses an SVG gets the same second reader on the desktop,
  and an SVG that arrives as bytes (`UCImage::LoadFromMemory`, a picture in
  a mail) takes the provider's `LoadFromMemory`, the converter's
  `ImportFromString`.

```cpp
RegisterVectorFormatsPlugin();                    // once, at startup

CanPreviewVectorExtension("dwg");                 // true
LoadVectorPreviewDocument("plan.dxf");            // a VectorDocument
LoadVectorPreviewDocumentFromMemory("svg", svgBytes.data(), svgBytes.size());   // from bytes
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
#include "UltraCanvasAllFormats.h"   // Plugins/

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

Loading covers the formats with readers and saving the formats with writers.
Neither is a list anyone maintains: `GetSupportedExtensions()` and
`GetSaveExtensions()` ask every converter in the plugin's one converter table
for `CanImport()` / `CanExport()` and collect the extensions it declares, and
`CreateConverterForExtension()` picks from the same table. A reader that
depends on an optional plugin (XAR, CDR) appears exactly when that plugin is
built. Today that reads SVG/SVGZ, XAR/WEB, CDR, EMF, WMF, AI, DXF and the DWG
family — `.dwg`, `.dwt`, `.dws`, `.sv$`, plus a `.bak` whose header says it is
a drawing (claimed on content, so never listed). Loading
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
installed), `ODGConverterTest` (the ODG writer and reader round trip as a
package and as flat XML, LibreOffice Draw's own files in `media/vector/ODG`
read with their custom shapes, rotation, gradients, hatch, transparency, text
and picture where LibreOffice draws them, and hand-written enhanced geometry;
`--render` draws any vector file to a PPM, `--convert` converts between
formats by extension), and `VectorFormatsPluginTest` (the whole matrix through the
plugin registry: save all ten formats via `SaveGraphicsFile`, load the
readable ones back via `LoadGraphicsFile` with geometry/style round-trip
checks, plus the supported-format inventory). `VectorModelTest` covers the shared model itself: `Matrix3x3` arithmetic and double precision, group/document bounds through nested transforms, hit testing through layer and group transforms, the unit helpers, and a DXF round trip of units and layer properties. `DWGReaderTest` covers the CAD import path: the DXF reader's block machinery on a synthetic drawing (nested, arrayed and mirrored inserts, dimension blocks, inheritance, visibility, geometry-derived extents) and the native DWG decoder on `Tests/DataFormats/cad-test-document.r2000.dwg`, the framework's own test document converted with dxf2dwg; extra `.dwg` files on its command line are decoded and reported (`DWG_TEST_SVG_DIR` exports them as SVG). It also covers the family dispatch above: the fixture's own bytes must load under each of `.dwg`, `.dwt`, `.dws`, `.sv$` and `.bak`, and a `.bak` holding anything else must not be claimed.

## See Also

- [UltraCanvasVectorRaster](UltraCanvasVectorRaster.md) — the other direction
  out of these formats: a drawing rasterized into an editable `UCRasterLayer`
  at a chosen pixel size (what a bitmap editor does with a dropped SVG)
- [UltraCanvasSVGExamples](UltraCanvasSVGExamples.md), [UltraCanvasXARExamples](UltraCanvasXARExamples.md), [UltraCanvasEPSExamples](UltraCanvasEPSExamples.md), [UltraCanvasCDRExamples](UltraCanvasCDRExamples.md) — the format plugins (rendering/UI elements)
