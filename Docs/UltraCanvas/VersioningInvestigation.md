# Version Level vs. Intended Architecture — Investigation

Status: **Investigation.** No code change is proposed here. The question asked
was narrow — *is `0.3.108` the appropriate version level for what the framework
still intends to implement?* — and the answer has two independent halves:

1. **The major/minor level is right.** `0.x` is correct and `1.0` would be
   premature: three engine-level unifications are outstanding, and each one
   changes public API. §4–§6 measure them.
2. **The shape of the number is wrong.** 115 releases have been made inside
   `0.3` — including a whole new platform backend — with the *patch* field
   carrying every one of them. §2–§3.

Everything below is measured from the tree at `0.3.108` (2026-09-07), not
assumed.

Author: UltraCanvas Framework
Last Modified: 2026-09-08

---

## 1. Where the version comes from

The first line of `Docs/UltraCanvas/CHANGELOG.md` (`#### YYYY-MM-DD *x.y.z*`)
is the single source of truth. `cmake/UltraCanvasVersion.cmake` parses it at
configure time for `project()` and the `ULTRACANVAS_VERSION` compile
definition; the packaging scripts parse the same line for artefact names;
`set-version.sh` propagates it into the Windows `.rc`/`.manifest` files that
are read from disk. The mechanism is sound and cannot drift.

What does not exist anywhere in the tree is a statement of what the three
fields *mean* — no versioning policy, no release-readiness criteria, no `1.0`
definition. `set-version.sh` documents the plumbing, not the semantics. This
document is the first attempt at the semantics.

There are also **no git tags** (`git tag` is empty), so the changelog is not
merely the primary record of a release — it is the only one.

## 2. What the numbering has actually been used for

| Series | Releases | Span |
|---|---|---|
| `0.1.x` | 24 | 2026-04-06 … |
| `0.2.x` | 33 | 2026-06 … |
| `0.3.x` | **115** | 2026-07-04 … 2026-09-07 |

172 changelog entries in five months. The two minor bumps that did happen mark
module-scale additions — `0.3.0` is "Implemented UltraNet networking module".
By that standard the `0.3` series contains several changes that should have
bumped the minor field and did not:

- **A new platform.** `0.3.108` links real applications on WebAssembly —
  a new `OS/WASM/` backend with native dialogs, clipboard, file loader and a
  CI workflow. A new supported platform is not a patch.
- **New subsystems.** The chart engine model layer, the three-phase render
  driver, the element plugin registry and DSO loader, the video codec plugin
  contract, the selectable-chart CMake mechanism — all landed inside `0.3.x`.
- **New public headers** appear in `0.3.x` releases routinely.

Under any reading of semver (or of this repository's own precedent at `0.3.0`),
the patch field is carrying feature work. `0.3.108` therefore understates the
distance travelled since `0.3.0` and gives a downstream consumer no signal at
all about which releases were additive.

## 3. The applications contradict the framework

The apps built *on* this framework keep their own changelogs, and several are
already past `1.0`:

| Component | Version |
|---|---|
| UltraCanvas (framework) | **0.3.108** |
| UltraTexter | 1.41 |
| UltraFiler | 1.21.0 |
| UltraViewer | 1.0.0 |
| UltraCleaner | 0.53 |
| UltraMail | 0.6.0 |
| UltraPaint / UltraSocial / UltraAuthenticator | 0.1.0 |

Shipping `1.0` applications on a `0.3` framework is a defensible position — the
apps are finished products, the framework's API is not yet frozen — but it is
currently an *accident* rather than a stated policy, and three of these use a
two-field scheme (`1.41`, `0.53`) that the framework's own parser only tolerates
because it pads to four components. This is worth writing down before anyone
outside the project has to interpret it.

## 4. Outstanding item 1 — charts on the common engine

**Intended:** one chart engine supplies background, grid, axes, legend, label
plan and interaction; a chart type implements only its own content phase.
Documented in `UltraCanvasChartEngine.md`, specified in
`UltraCanvasChartEngineProposal.md`.

**Implemented:** the engine itself — axes, projections, label plan, themes,
highlights, the three-phase driver `UltraCanvasChartEngineElement`, and the
CMake mechanism for selectable charts.

**Adopted by:** one chart.

| Measurement | Count |
|---|---|
| Chart element classes under `include/Plugins/Charts/` | 40 |
| …deriving from `UltraCanvasChartEngineElement` | **1** (parallel coordinate) |
| …deriving directly from `UltraCanvasChartElementBase` | 31 |
| …deriving from `UltraCanvasHeatmapChartElement` | 4 |
| …deriving from `UltraCanvasUIElement` / `UltraCanvasGLSurface` | 2 |
| Entries in `UC_CHART_KEYS` (`UltraCanvas/CMakeLists.txt`) | **1** |
| Files referencing the engine axis model outside `Engine/` | 4, all parallel-coordinate |
| Files with their own legend renderer | 10 |
| Files with their own axis/tick rendering | 13 |
| Files with their own colour bar | 3 (heatmap, hexbin, contour) |

Additionally, **8 diagram classes** (`CircleDiagram`, `FishboneDiagram`,
`MatrixDiagram`, `SWOTDiagram`, `TimelineDiagram`, `TreeMapElement`,
`VennDiagram`, `WordCloudDiagram`) also derive from
`UltraCanvasChartElementBase`, so the engine's eventual scope is ~48 elements,
not 40.

The proposal states the position plainly: *"Not started: the Tier-1 adapter and
the legacy chart migration."* That is unchanged in the tree. Migration will
move ~47 elements off `UltraCanvasChartElementBase` onto a different base class
with a different virtual contract — **a breaking change to the public API of
every chart**, and the single strongest argument against `1.0` today.

## 5. Outstanding item 2 — one internal vector storage format

**Intended:** `VectorStorage::VectorDocument`
(`Plugins/Vector/UltraCanvasVectorStorage.h`) as the framework's single
in-memory vector model.

**Where it holds:** the ten file-format converters. SVG, XAR, EPS, CDR, PDF,
EMF, WMF, AI, DXF and DWG all import to and export from `VectorDocument`
through `IVectorFormatConverter`, sharing one document walk and one path
normaliser (`UltraCanvasVectorPathOps.h`). This part of the goal is met.

**Where it does not hold:**

- **A second, independent SVG model.** `Plugins/SVG/UltraCanvasSVGPlugin.{h,cpp}`
  (1 847 lines) defines its own `SVGDocument`, `SVGStyle`, `SVGTransform`,
  `SVGGradient`, `SVGFilter`, `PathCommand` and `SVGPathParser`, and its own
  `UltraCanvasSVGElement`. It includes `UltraCanvasVectorStorage.h` nowhere.
  `Plugins/Vector/UltraCanvasSVGConverter.cpp` (1 211 lines) parses SVG a
  second time, with tinyxml2 again, into `VectorDocument`. Two parsers, two
  models, two UI elements, one file format.
- **A third SVG path for rasterization.** `libspecific/Cairo/SvgDocumentCairo.{h,cpp}`
  retains a librsvg handle; `UltraCanvasSupportedFormats.cpp` advertises SVG as
  "librsvg (via libvips)".
- **The vectorizer does not produce the format.** `VectorizerResult::svg` is a
  `std::string` of SVG text, so a raster→vector result must be re-parsed to
  become a `VectorDocument`.
- **Five hand-written SVG emitters** exist outside the converter — QRCode,
  Sankey (`SaveToSVG`), GitGraph, GourceTree and MindMapIO each stream
  `<svg …>` to an `ofstream` themselves — and a sixth path, the CDR plugin's
  `ExportToSVG`, re-parses the source through librevenge's own SVG generator.
  None of the six goes through `VectorDocument`, so none of them benefits from
  the converter matrix: a Sankey diagram can be saved as SVG but not as PDF,
  EPS, DXF or EMF, which the shared model would have given it for free.
- **No vector output from the render context.** There is no
  `cairo_svg_surface`/`cairo_pdf_surface` anywhere in the tree, and no
  chart or diagram builds a `VectorDocument`. Every one of the ~48 chart and
  diagram elements paints immediate-mode into `IRenderContext`, which is why
  Sankey's "Export SVG" had to be hand-written and why no other chart has one
  at all.

The header still reads `Version: 2.0.0 · Last Modified: 2025-01-20` — it
predates most of what now needs to share it. Making it genuinely common means
retiring the SVG plugin's parallel model and giving `IRenderContext` a
recording backend, both of which change public types.

## 6. Outstanding item 3 — one internal 3D vector format

**Intended:** the same, for 3D.

**What exists:** `Vec3`, `Mat4`, `Mesh3D` and `BoundingBox3D` in
`UltraCanvas/Plugins/Models/STL/UltraCanvas3DTypes.h`. Its own README calls
them *"reusable by future 3D model plugins."*

**Why that is not the common format yet:**

- **It lives inside a file-type plugin.** `Masterfile_modules.md` states the
  rule for framework-wide facilities: they are core services *"usable by core,
  plugins and applications alike; never implemented inside a file-type
  plugin."* The 3D types are implemented inside the STL plugin.
- **Consumers reach across into it.** `UltraCanvasScatterPlot3D.h` and
  `UltraCanvasContourSurface3D.h` both `#include "Models/STL/UltraCanvas3DTypes.h"`
  — chart code depending on a file-format plugin's private directory.
- **Three cameras, three projections.** `UltraCanvasScatterPlot3DElement::Camera`,
  `UltraCanvasContourSurface3DElement::Camera` and
  `UltraCanvasContourSurfaceGLElement::OverlayCamera` are three separate
  structs, each with its own `Project()` and its own orbit handling. The chart
  engine proposal counted the same three in its §2.1 duplication table; none
  has been unified.
- **A fourth copy exists.** `Apps/DemoApp/UltraCanvasGLDemoSupport.h` declares
  its own `Vec3` and `Mat4`.
- **`Mesh3D` has two consumers** — the STL plugin and `UltraCanvasFilerWidget`
  (thumbnails). There is no scene, no material, no transform hierarchy, and
  STL is the only 3D format in `Plugins/Models/`.

So the 3D situation is one step behind the 2D one: 2D has a common format that
some code bypasses, while 3D has a *shared struct file* in the wrong place and
no common document at all.

## 7. Assessment

**Is `0.x` the appropriate major level? Yes — and `1.0` would be wrong today.**
`1.0` is a promise of API stability. All three items above are resolved by
changing public API:

| Item | The breaking change it requires |
|---|---|
| Charts on the engine | ~47 elements change base class and virtual contract |
| One vector format | the SVG plugin's `SVGDocument` model is retired; `IRenderContext` gains a vector-recording backend |
| One 3D format | `Vec3`/`Mat4`/`Mesh3D` move out of a plugin into core; three chart cameras collapse into one |

Publishing `1.0` before these land would either freeze the duplication in place
or make the first post-`1.0` release a `2.0`.

**Is `0.3.108` the appropriate *number*? No.** The patch field is doing minor
field work. The `0.3` series has absorbed a new platform backend, several new
subsystems and many new public headers across 115 releases.

## 8. Recommendation

1. **Stay in `0.x`.** Do not treat the size of the changelog as a reason to go
   to `1.0`; the architecture, not the volume, decides.
2. **Bump the minor field for the next feature release** — make it `0.4.0`
   rather than `0.3.109`, and thereafter reserve the patch field for fixes and
   documentation. On current practice a minor bump per month or two is honest.
3. **Write the policy down** (this file or a `Docs/Versioning.md`): what each
   field means, that applications version independently of the framework, and
   that the changelog's first line stays the source of truth.
4. **State the `1.0` gates explicitly.** The three items above are the natural
   candidates, plus whatever API-freeze commitment the project wants to make:
   - every chart and the 8 chart-derived diagrams render through
     `UltraCanvasChartEngineElement`, and `UC_CHART_KEYS` lists them all;
   - one vector model — the SVG plugin's parallel model retired, the
     vectorizer producing `VectorDocument`, the ad-hoc SVG writers replaced,
     and a vector-recording render context so charts and diagrams export
     without hand-written emitters;
   - one 3D model — the 3D types promoted out of `Plugins/Models/STL/` into
     core, one camera/projection shared by the 3D charts and the GL surface.
5. **Consider tagging releases in git.** With no tags, "what was in 0.3.42" is
   answerable only by reading the changelog.

## 9. Suggested milestone mapping

| Milestone | Content |
|---|---|
| `0.4.x` | chart engine migration — Tier-1 adapter, then charts in batches; each batch a minor or patch release as it lands |
| `0.5.x` | vector unification — one SVG model, vectorizer to `VectorDocument`, recording render context |
| `0.6.x` | 3D unification — core 3D types, one camera, room for a second model format |
| `1.0.0` | the three gates met and the public API declared stable |

This ordering is deliberate: the chart migration is the largest consumer of a
recording render context (chart→SVG export), and the 3D unification is
partly *inside* the chart migration (the three 3D charts are chart elements),
so doing charts first shrinks the other two.
