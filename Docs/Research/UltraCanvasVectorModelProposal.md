# UltraCanvas Vector Document Model — Survey and Proposal

**Status:** in progress (first step merged with this document)
**Scope:** `UltraCanvas/Plugins/Vector/` — `VectorStorage::VectorDocument` and
the converters, readers and renderer that share it.

The Vector plugin converts ten file formats through one in-memory model,
`VectorStorage::VectorDocument` (`UltraCanvasVectorStorage.h`). The model was
designed around SVG; the CAD (DXF/DWG), metafile (EMF/WMF), Xara and print
(EPS/PDF/AI/CDR) converters were added later and each meets the model at a
different point. This document records what each converter actually reads
from and writes into the model today, which model features are dead, where
formats lose information because the model has no home for it, and what
to change — in small, testable steps — so the model becomes the universal
exchange structure for all supported vector formats.

The survey was taken from the source on 2026-09-08 (branch
`claude/vector-model-unification`).

## 1. How import and export work today

Two families of vector support coexist:

- **The converter matrix** (`IVectorFormatConverter`): SVG, XAR, EMF, WMF,
  DXF and DWG read into `VectorDocument`; all ten formats write from it.
  `UltraCanvasVectorFormatsPlugin` exposes the matrix to `LoadGraphicsFile`
  / `SaveGraphicsFile`, and `UltraCanvasVectorElement` renders a document
  through `VectorRenderer`.
- **Dedicated viewer plugins** (XAR, EPS, CDR, PDF via MuPDF) with their
  own private document models and richer rendering. They own the load
  extension when registered after the Vector plugin; saving always
  resolves to the matrix.

Every writer shares one walk: styles resolve by inheritance down the tree,
transforms accumulate and (except in SVG) bake into emitted coordinates,
and `UltraCanvasVectorPathOps.h` normalises every path command to absolute
move/line/cubic segments.

## 2. What the converters use — feature matrix

Legend: **y** supported · **→path** accepted but emitted as a path ·
**flat** container flattened, children still emitted · **n** dropped
(with a `WarningCallback` message where noted) · **–** not applicable.

### 2.1 Element types

| | Rect | Round rect | Circle | Ellipse | Line | Polyline | Polygon | Path | Text | Group | Layer | Symbol | Use | Image |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SVG write | y | y | y | y | y | y | y | y | y | y | y | y | y | y |
| SVG read | y | y | y | y | y | y | y | y | y | y | y | y | y | y |
| XAR write | y | y | →ellipse | y | →path | →path | →path | y | y | y | y | flat | n | n |
| XAR read | y | y | – | y | – | – | – | y | y | y | y | – | – | – |
| EPS write | →path | →path | →path | →path | →path | →path | →path | y | y | flat | flat | flat | n | n |
| CDR write | →path | →path | →path | →path | →path | →path | →path | y | **n** | flat | flat | flat | n | n |
| PDF / AI write | →path | →path | →path | →path | →path | →path | →path | y | y | flat | flat | flat | n | n |
| EMF write | →path | →path | →path | →path | →path | →path | →path | y | y | flat | flat | flat | n | n |
| EMF read | y | – | – | y | y | y | y | y | y | – | one | – | – | – |
| WMF write | →path | →path | →path | →path | →path | →path | →path | y | y | flat | flat | flat | n | n |
| WMF read | y | – | – | y | y | y | y | y | y | – | one | – | – | – |
| DXF write | →path | →path | →path | →path | →path | →path | →path | y | y | flat | **layer table** | flat | n | n |
| DXF / DWG read | – | – | y | →path | y | y | y | y | y | y (blocks, OCS) | **from table** | – | – | – |

Element types that no converter and not the renderer ever dispatch:
`Star`, `RegularPolygon`, `Arc`, `TextPath`, `TextSpan`, `ClipPath`,
`Mask`, `Pattern`, `Marker`, `Filter` and the four gradient element types.
Their classes (`VectorTextPath`, `VectorPattern`, `VectorFilter`,
`VectorClipPath`, `VectorMask`, `VectorMarker`) are never instantiated.

Path commands: `PathOps::NormalizePath` handles all ten
`PathCommandType`s (quadratics and arcs become cubics), so every writer
except SVG (which serialises verbatim) has full coverage. Readers produce
only move/line/cubic/close. The **renderer** degrades `ArcTo` to a
straight line and drops `SmoothQuadraticTo` entirely (`BuildPath`).

### 2.2 Style

| | solid fill | linear/radial gradient | pattern | stroke width | dash | dash offset | cap/join | miter | fill/stroke opacity | opacity | visibility | fill rule / clip / mask / filter / blend |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SVG write | y | y (all stops) | y | y | y | y | y | y | y | y | y | **n** |
| SVG read | y | y (href inheritance, spread, transform) | n | y | y | y | y | y | y | y | y | **n** |
| XAR write | y | first/last stop only | flat black | y | y | n | y | y | n | y | y | n |
| XAR read | y | two stops | – | y | y | – | y | y | – | y | layer only | – |
| EPS write | y | end-stop blend | flat black | y | y | y | y | y | y | toward white | y | n |
| CDR write | y | end-stop blend | flat black | y | y | n | y | n | n | y | y | n |
| PDF / AI write | y | **n** (no shadings) | n | y | y | y | y | y | fill only (ExtGState) | y | y | n |
| EMF write | y | n | n | y | y | n | y | n | y | toward white | y | n |
| EMF read | y | n | – | y | y | – | y | n | – | – | – | – |
| WMF write | y | n | n | y | fixed PS_DASH | n | n | n | y | toward white | y | n |
| WMF read | y | n | – | y | fixed {6,3} | – | n | n | – | – | – | – |
| DXF write | y (ACI + true colour) | n | n | y (snapped lineweight) | y (LTYPE) | n | n | n | **n** | **n** | y | n |
| DXF / DWG read | y (HATCH/SOLID) | n | pattern hatch → solid | y | y | – | n | n | – | – | layers | – |
| Renderer | y | **partial** — object-bbox gradients resolve against a hard-coded 100×100 box | **n** | y | y | y | y | y | **n** | y | y | **n** |

### 2.3 Text

| | family / size | weight / slant | anchor | per-span styles | letter spacing | underline / strike | rotation |
|---|---|---|---|---|---|---|---|
| SVG | y | y | y | y (`<tspan>`) | y | y | via transform |
| XAR | y | bold / italic | justification | y | n | underline | **dropped** |
| EPS | y | y | single-chunk only | y | n | n | **dropped** |
| PDF / AI | y | y | estimated from average glyph width | y | n | n | **dropped** |
| EMF / WMF | y | y | single-chunk only | y | n | y | **dropped** |
| DXF | family (STYLE), size | n | y | flattened to base | n | n | **dropped** |
| CDR | text is not written at all | | | | | | |
| Renderer | y | y | **n** | font set once from `BaseStyle` | n | n | via transform |

`TextPathData` / `VectorTextPath` have zero references anywhere.

### 2.4 Structure and metadata

| | element Transform | Id / Classes | layer name | layer visible | layer locked | layer opacity | Size | ViewBox | background | title / description | units |
|---|---|---|---|---|---|---|---|---|---|---|---|
| SVG | attribute | y | as `id` | `display` | n | y | y | y | bg rect | y | – |
| XAR | baked | n | y | y | y | n | y | n | n | n | millipoints |
| EPS / PDF / AI | baked | n | comment / n | skip | n | n | y | n | n | title | points |
| CDR / EMF / WMF | baked | n | n | skip | n | n | y | n | n | n | 0.01 mm / twips |
| DXF write | baked | n | **real layer** | skip / **off flag** | **y** | n | y | n | n | n | **$INSUNITS** |
| DXF / DWG read | y | n | **from table** | **y** | **y** | n | y | n | n | n | **$INSUNITS** |

(Bold entries in the DXF rows are new with this branch.)

### 2.5 Model features nothing uses

Verified by searching every converter, reader and the renderer for the
member name:

- `VectorDocument::NamedStyles`, `::Metadata`, `::Author`;
  `::PreserveAspectRatio` (renderer only).
- `VectorLayer::LayerBlendMode`.
- `VectorStyle::ClipRule`, `::ClipPath`, `::Mask`, `::Blend`, `::Filters`
  — only `VectorStyle::Inherit` copies them between parties that never set
  them. `FillRule`, `TextBaseline`, `MarkerOrientation`, `FilterType` enums.
- `RadialGradientData::FocalRadius`, `MeshGradientData`,
  `ConicalGradientData` (consumers only test for the variant and warn),
  `MarkerData`, `FilterData`, `ClipPathData`, `MaskData`, `TextPathData`.
- `VectorElement::GetGlobalTransform`, `LocalToGlobal`, `GlobalToLocal`,
  `HasClass`; `VectorDocument::FindElementsByClass`, `RemoveLayer`.
- SVG-only: `Classes`, `Id`, `PatternData`, `LetterSpacing`,
  `GradientSpreadMethod`, `VectorImage::EmbeddedData`/`MimeType`.

### 2.6 What formats need that the model lacks

1. **Text rotation as a property of the run.** Six writers warn "rotated/
   skewed text is exported without its rotation": text emission collapses
   to an anchor point plus an axis-aligned test on the CTM. The model has
   only `VectorElement::Transform`; text needs a baseline direction the
   writers can emit natively (DXF group 50, EMF `lfEscapement`, XAR story
   matrices, PDF/EPS text matrix).
2. **Glyph advance / measurement hook.** PDF estimates anchoring from an
   average glyph width; EPS/EMF/WMF give up on centred multi-style lines.
   The model has no advance widths and no measurement callback.
3. **Multi-stop gradients in XAR** — the writer keeps first/last stop.
4. **NURBS control data.** DXF rational splines lose their weights;
   general NURBS are sampled. `PathData` has no weighted control points.
5. **Hatch patterns** — pattern hatches fill solid; `PatternData` exists
   but only the SVG writer consumes it.
6. **Per-span text styles in DXF** flatten (a format limit — DXF TEXT has
   one style; MTEXT formatting codes would carry them).
7. **Non-Latin-1 text** in PDF/WMF — no encoding/font-coverage metadata.
8. **XAR effects** (feather, shadow, bevel, contour, blend, mould) have no
   model home; the XAR code that warns about them is in an unreachable
   second reader implementation (`XARConverter::Impl::ProcessRecord`).
9. **Capability flags lie.** `SVGConverter::GetCapabilities` claims
   clipping; `XARConverter` claims blend modes, filters and masking; none
   is implemented. Anything choosing a target format by capability will be
   misled.

## 3. Defects found in the model and fixed on this branch

- **Matrix precision.** `Matrix3x3` stored `float`. CAD drawings carry
  offsets of 10⁶ units with 10⁻³ detail; the DXF reader had already grown
  its own double `Affine` struct to work around it. The matrix is now
  `double` throughout (`Inverse` no longer treats a determinant below
  1e-10 as singular), and an `IsIdentity()` query was added.
- **Empty bounds polluted unions.** `VectorGroup::GetBoundingBox` and
  `VectorDocument::GetBoundingBox` unioned children starting from
  `{0,0,0,0}`, so any group containing an empty group, an unsupported
  element or a definition-only symbol reported a box dragged to the
  origin — visible as oversized "fit to content" pages and hit areas. Empty
  boxes are now skipped; an empty group stays empty even with a transform.
  A document with no content reports its page.
- **Hit testing ignored ancestor transforms.** `HitTestDocument` tested
  every child against the document-space point, while child boxes are in
  their parent's space. Anything inside a transformed group (every CAD
  block insert, every mirrored entity) could not be hit where it was
  drawn. The point is now carried through each layer's and group's
  inverse transform; an element without bounds never hits.
- **Dead declarations.** `UltraCanvasVectorConverter.h` declared a
  `VectorConverterFactory`, a `VectorConversionManager` and eleven helper
  functions that were never implemented (the linker would fail on first
  use); the working registry is `UltraCanvasVectorFormatsPlugin`. Removed.
- **XAR matrix helper transposed.** `FromXARMatrix` passed XAR's
  PostScript-ordered `(a,b,c,d)` straight into the row-major
  `Matrix3x3::FromValues`, the mirror of what `ToXARMatrix` writes.
  Corrected (the helper is header-only and currently unused by the live
  reader).

## 4. Model additions on this branch

- **Units.** `LengthUnit` (`Unspecified`, `Point`, `Pixel`, `Inch`, …,
  `Nanometer`) with `PointsPerUnit()` and `LengthUnitSymbol()`.
  `VectorDocument::SourceUnit` and `::PointsPerSourceUnit` record the unit
  a file measured in and the scale the reader applied, so a consumer can
  recover source measurements (`source = points / PointsPerSourceUnit`)
  and a writer with a unit field can round-trip them. The DXF reader sets
  them from `$INSUNITS` and, when the unit is declared and the physical
  page lands between 200 and 20 000 pt, uses the physical scale (an A4
  plan in millimetres becomes 842 × 595 pt) instead of the fit-to-page
  heuristic. The DXF writer emits `$INSUNITS` and writes coordinates in
  the source unit when the document has one; lineweights stay physical.
- **CAD layer properties.** `VectorLayer` gains `Frozen`, `Plottable`,
  `DefaultColor`, `DefaultStrokeWidth`, `LineTypeName` and
  `DefaultDashArray` (ByLayer defaults from the LAYER table). Entities keep
  their resolved style, so these do not affect rendering; the DXF reader
  fills them (`Locked` and `Visible` too), and the DXF writer emits the
  layer table from them — colour (ACI + true colour), lineweight, a
  registered LTYPE for the default dash pattern, frozen/locked flags,
  plot flag, and hidden layers as *off* (negative colour) rather than
  unnamed.
- **Tests.** `Tests/VectorModelTest.cpp` covers matrix arithmetic and
  precision, nested-transform bounds, empty-box handling, hit testing
  through layer and group transforms, the unit helpers, and a DXF round
  trip of units and layer properties.

## 5. Proposal — next steps, in order

Each step is independently mergeable and comes with a test.

1. **Text baseline direction.** Add `VectorText::Rotation` (degrees,
   counter-clockwise about the anchor) — or, more generally, derive the
   run matrix from the CTM once in `PathOps` and give writers a
   `TextRun{anchor, angle, scale}` helper. DXF (group 50), EMF/WMF
   (`lfEscapement`), PDF/EPS (`Tm` / `rotate`) and XAR (story matrix) can
   all emit it natively; the DXF/DWG reader already knows the angle and
   currently folds it into a transform. Removes six writer warnings.
2. **Text measurement callback** in `ConversionOptions`
   (`std::function<double(const VectorTextStyle&, std::string_view)>`),
   defaulting to the framework's font engine when available. Fixes
   centred/right-anchored text in PDF, EPS, EMF and WMF and lets the
   renderer place spans with real advances.
3. **Renderer parity.** Object-bounding-box gradients against the real
   element bounds; `ArcTo` through `PathOps::AppendArc`;
   `SmoothQuadraticTo`; fill/stroke opacity; per-span fonts. The renderer
   should emit warnings like the writers do instead of a silent `default:`.
4. **Honest capabilities.** `FormatCapabilities` computed from what each
   writer implements; a unit test that asserts each flag against a probe
   document. Fix the SVG clipping and XAR blend/filter/mask claims.
5. **Prune or implement the dead model surface.** Keep `ClipPath`,
   `Mask`, `Pattern`, `Marker` and `FillRule` only if the SVG converter and
   renderer implement them (all are SVG 1.1 core and the SVG converter
   claims losslessness); drop `Star`, `RegularPolygon`, `Arc`,
   `ConicalGradient`, `MeshGradient`, `Filter`, `TextPath`, `NamedStyles`,
   `Author`, `LayerBlendMode` and the unused traversal helpers, or move
   them behind a clearly marked "reserved" section. A field that exists
   but is never honoured is worse than one that does not exist.
6. **CAD fidelity.** Rational spline weights in `PathData` (an optional
   weight per control point, consumed by the DXF writer's SPLINE and
   sampled by everyone else); hatch pattern names carried on the fill as
   `PatternData` with a CAD pattern reference; MTEXT formatting codes for
   per-span styles; `$INSUNITS` from the DWG header variables (the decoder
   currently emits only `$ACADVER`).
7. **XAR.** Delete the unreachable `Impl::ProcessRecord` family, then give
   the live reader/writer multi-stop gradients (XAR's `MultiStageFill`
   records) and story matrices for rotated text.
8. **Reader/writer symmetry tests.** A single fixture document exercising
   every model feature, exported through each writer and re-imported
   where a reader exists, with a per-format expectation table generated
   from `FormatCapabilities` — so a capability flag, a warning string and
   a test row always agree.

## 6. Non-goals

- Replacing `VectorDocument` with a new structure. The survey shows the
  shape is right (layers → groups → elements, optional fill/stroke,
  transforms); what is missing is precision, units, CAD layer metadata,
  text orientation and honesty about unsupported features. The older
  `VectorConverterFactory` / `VectorConversionManager` design and the
  factory-style `UltraCanvasVectorElement` helpers are superseded by the
  plugin registry and are not revived.
- Merging the dedicated viewer plugins' private models into this one.
  They stay the rendering path for compressed XAR, EPS, CDR and PDF; the
  converter matrix is the editing and exchange path.
