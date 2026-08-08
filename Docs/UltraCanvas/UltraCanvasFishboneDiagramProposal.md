# UltraCanvasFishboneDiagram — Research & Feature Proposal

**Status:** **Phases 1–3 are implemented** as `UltraCanvasFishboneDiagram` — see
[`UltraCanvasFishboneDiagram.md`](UltraCanvasFishboneDiagram.md) for the guide.
This document remains the research write-up and the roadmap for the rest.

Delivered: all eight designs of §6 (not just the Phase 1 four), the data model
of §5 including sub-causes, the trimming rib solver of §7, side policies, rib
angle control, head/tail shapes, effect placement, cause markers, palettes, dark
theme, per-category icons, wrapping and chip ellipsis, hover/selection/tooltips
with the `FishboneRef` handle, collapse/expand, weight and root-cause markers,
the seven category presets (C2), three samples (C3), validation, and the whole
of Phase 3: Mermaid `ishikawa-beta` and indented-outline import/export with
`Tests/FishboneModelTest.cpp` covering both codecs.

One deviation from §9 Q4 as written: the answer there was "no separate model".
The data structs and the text codecs did move into `UltraCanvasFishboneModel.h`
/ `…Model.cpp` / `…Text.cpp` after all — not to separate concerns, but because a
UI-free header is what lets the test compile without the widget stack, exactly
as `UltraCanvasSequenceModel` does. The element still owns all geometry.

Still open (Phase 4): the pan/zoom viewport, entrance animation, keyboard
navigation, JSON/CSV interchange (X3/X4), and the re-scoped mind map L5.

The original finding, for the record: a repository-wide search for `fishbone` /
`ishikawa` / `cause-and-effect` returned no element, no header, no demo slot and
no test — the only hits were
`Docs/UltraCanvas/UltraCanvasMindMapProposal.md` (feature **L5**, deferred to
its Phase 3) and unrelated text in vendored `third_party/curl` files.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-08-07
**Related:** `UltraCanvasSWOTDiagram` (implemented — the closest architectural
sibling), `UltraCanvasTimelineDiagram` (implemented — the closest *geometric*
sibling: a decorated spine with alternating side content),
`UltraCanvasMindMap` (implemented; owns feature L5 — see §4),
`Docs/UltraCanvas/SWOTDiagramDesignVariants.md` (the same kind of survey for
SWOT).

---

## 1. Summary — the headline question

The five reference images are **not** five different diagrams. They are one
diagram — an Ishikawa cause-and-effect chart — drawn five ways, and the
variation is entirely in *decoration and rib geometry*, never in the data. All
five encode exactly the same thing:

> one **effect**, 3–7 **cause categories**, and 3 **causes** per category.

That is the same finding the SWOT survey reached (`SWOTDiagramDesignVariants.md`
§Observations 3) and the opposite of the finding in
`UltraCanvasTimelineDiagramProposal.md` §1, where the reference images turned out
to hide two structurally incompatible data models and forced a two-element split.
Here there is no split to make.

**Recommendation: one new element,
`UltraCanvasFishboneDiagram`, in `Plugins/Diagrams/`, modelled on
`UltraCanvasSWOTDiagram` (a `UltraCanvasChartElementBase` subclass whose
`FishboneDesign` enum selects the geometry and whose decoration options combine
freely with every geometry).** The rib/spine solver is the only genuinely new
code; everything else is a re-application of patterns the Timeline and SWOT
elements already established.

Secondary recommendation: **re-scope, do not delete, mind map feature L5** — see
§4, which resolves the overlap.

---

## 2. What a fishbone diagram is

Kaoru Ishikawa's cause-and-effect diagram (Kawasaki, 1960s; also *herringbone*,
*Ishikawa*, *fishbone*) is the standard root-cause-analysis picture:

```
   causes                                             effect
   \                     \                     \
    \                     \                     \
  ---+---------------------+---------------------+------->  [ Problem ]
    /                     /                     /
   /                     /                     /
```

- **Head** — the effect/problem being analysed; the spine points at it.
- **Spine** — the horizontal backbone from tail to head.
- **Ribs (bones, "major causes")** — angled lines meeting the spine, one per
  **cause category**, alternating above and below.
- **Twigs** — individual causes attached to a rib.
- **Sub-twigs** — sub-causes; conventionally driven by 5-Whys questioning, and
  in practice rarely deeper than 3 levels.

### Standard category frameworks

The categories are usually taken from a named checklist, and shipping these as
built-in presets is cheap, high-value and unambiguous:

| Preset | Categories | Domain |
|---|---|---|
| **6M** (classic) | Man/Manpower, Machine, Material, Method, Measurement, Mother Nature (Environment) | Manufacturing |
| **5M+E** | as 6M without Measurement | Manufacturing (older form) |
| **8P** | Product, Price, Place, Promotion, People, Process, Physical Evidence, Productivity | Marketing / service |
| **4S** | Surroundings, Suppliers, Systems, Skills | Service industries |
| **5S / Service** | Surroundings, Suppliers, Systems, Skills, Safety | Service |
| **PEMPEM** | People, Equipment, Materials, Process, Environment, Management | Healthcare / general |
| **Software** | Requirements, Design, Code, Test, Process, Tooling, People, Environment | Engineering (proposed) |

Reference images 3 and 5 both use the **4S** set verbatim, image 1 uses a subset
of **6M**, and images 2 and 4 are free-form — so a preset list plus free-form
categories covers all five.

---

## 3. What the five reference images demand

Each image is read for what it *forces* on the design, in the same style as
`UltraCanvasTimelineDiagramProposal.md` §3.

### Image 1 — "Quality Control Issues": chips on a solid spine arrow

Dark navy; 3 categories (Man / Machines / Materials).

- The spine is a **thick tapered arrow shape drawn behind everything** in a
  near-background tone (chevron tail at the left, large arrowhead at the right),
  not a stroked line.
- Each category is a **colored rounded pill sitting on the spine**, terminated at
  its head-side end by a large **circular icon badge with a white ring**.
- Causes leave the badge on a short rib at roughly 60° and terminate in a
  **filled dot**; the label is set outboard of the dot, right-aligned to it.
- Sides alternate **per category** (above / below / above), not per cause.
- One accent color per category drives pill, badge ring and dot.

**Forces:** spine-as-shape (not a line); category chips anchored *on* the spine
rather than at the rib tip; icon badges; dot-terminated twigs with outboard,
edge-aligned labels.

### Image 2 — "Sales Performance": ribs that cross the spine

Dark grey; 4 categories arranged as 2 crossing ribs.

- Each rib is a **single full-length straight line that crosses the spine**, so
  one line serves an above-category and a below-category.
- Category chips sit at **both outer ends** of the rib.
- Causes are **bullet-marked text lines stacked parallel to the spine**,
  progressively indented so the block follows the rib's slope — the text is not
  attached to the rib by leader lines at all.
- A circular icon badge marks each **rib/spine crossing**.
- One cause is emphasised (bold, brighter) — a *highlighted cause* state.

**Forces:** rib-pairing (2 categories per rib line); cause lists laid out as a
sheared text block; per-cause highlight styling; head chevron + tail triangle.

### Image 3 — "Employee Performance": bracket bones

Cream; 4 categories (4S); the closest of the five to the textbook form.

- Each bone is a **parallelogram outline**: a slanted edge plus a horizontal
  shelf running back to the spine — an orthogonal/bracket rib, not a bare line.
- Causes attach to the slanted edge by **short horizontal leader lines with
  round dot terminators**, label set above the leader.
- Category chips sit at the **outer corner** of the bone (top for upper bones,
  bottom for lower).
- The spine carries its own **label** ("Potential Causes") — an inline caption
  on the backbone.
- Head is a large triangle; tail is a decorative pastel parallelogram cluster.
- Bones are monochrome; only the chips are colored.

**Forces:** bracket rib geometry; horizontal leaders (so cause text is
horizontal regardless of rib angle); a spine caption; decoupling bone color from
chip color.

### Image 4 — "Team Productivity": chevron-ribbon spine, 7 categories

Blue gradient; 7 categories (4 above, 3 below).

- The spine is a **chain of chevron/arrow blocks** in alternating fills, each
  carrying a pictogram, ending in a pointed fish head.
- Category pills float well away from the spine and are connected by **long thin
  diagonal leader lines** — the rib is a hairline, the pill is the payload.
- Each pill has a compact **3-item bullet list** stacked outboard of it.
- Highest category count of the five, which is what forces the leader-line
  treatment: at 7 categories there is no room for full-length ribs.

**Forces:** a segmented/chevron spine renderer (already present in spirit as
`TimelineDesign::Chevron`); category-count-driven degradation; per-segment icons;
pill + list as one movable unit.

### Image 5 — "Customer Churn": category columns straddling the spine

Cream; 4 categories (4S).

- Categories are **vertical tinted panels (columns)** straddling the spine,
  alternating up and down — there are no ribs at all.
- Each panel holds a **circular icon badge** at its far end and a centered
  bullet list.
- The category name is printed **beside the spine**, colored to match the panel.
- The spine is a thin arrow with **small chevron waypoint markers**, a blobby
  **fish tail** at the left and a large triangular head at the right.

**Forces:** a rib-less "column" geometry; decorative tail shapes; waypoint
markers; label placement adjacent to the spine rather than at a rib tip.

### Cross-image constants

These hold in **all five** images and should be the element's defaults:

1. **3–7 categories, exactly 3 causes each.** Realistic range: 3–8 categories,
   3–6 causes. Sub-causes appear in none of the five (but the domain and the
   Mermaid notation both support them — see §6 Import/export).
2. **Sides alternate per category**, never per cause.
3. **Color is per category** — one accent driving chip, badge, marker and, where
   present, panel tint. No image colors an individual cause.
4. **Every image has a directional head**; 3 of 5 add an explicit tail
   decoration.
5. **4 of 5 give each category an icon.** Icons are not optional garnish in this
   diagram family; they are the primary category identifier at a glance.
6. **The effect text is in the page title, not in a head box, in all five.**
   The textbook form puts the problem statement in a box at the head; the
   infographic form promotes it to the title and leaves the head purely
   decorative. The element must support **both** (§5, `FishboneEffectPlacement`)
   — assuming the head box is mandatory would make every one of the five
   reference designs unreachable.
7. **None of the five is the plain textbook herringbone** — which still has to
   ship, because it is what an engineering or QA user expects when they ask for
   a fishbone.

---

## 4. Where this belongs in the repository

### 4.1 The overlap: mind map feature L5

`UltraCanvasMindMapProposal.md` reserves **L5 —
`MindMapStructure::Fishbone` — angled ribs off a horizontal spine**, Phase 3,
with this reasoning (§8.5):

> Fishbone (L5) — no home anywhere. […] But its data *is* a rooted tree with a
> spine — a mind map whose root is at one end and whose branches meet the spine
> at a fixed angle. It is the cheapest of the three to express as a structure […]
> essentially one alternative `ComputeLayout` case.

That reasoning is sound *for what it claims*: a fishbone's topology is a rooted
tree, and `MindMapStructure` is not yet built for it (the enum today is
`Balanced, LogicRight, LogicLeft, OrgChartDown, OrgChartUp, Radial, Manual` —
Phase 3 has not landed, so nothing is lost either way).

But it is not sufficient. The proposal's own test — *"a variant belongs in this
element when it is the same topic tree drawn differently, and belongs in its own
element when it introduces a new axis or new data semantics"* — cuts both ways
here, because a fishbone **does** add semantics that a topic tree does not carry:

- Depth is not free: depth 0 is *the effect*, depth 1 is *a cause category*,
  depth 2+ are *causes*. Each level has a different meaning, a different default
  shape and a different renderer.
- The spine is **directed**; the head means something (the effect), and the tail
  and head are decorated independently.
- Side assignment is a property of the *category*, and category count drives a
  global geometry choice (image 4's leader lines exist only because it has 7).
- Category presets (6M/8P/4S) are domain vocabulary with no mind-map analogue.

And decisively: **none of the five reference images can be produced by a mind map
layout case.** Chevron spines, column panels, bracket bones, icon badges on the
spine and pill-plus-list units are renderer features, not layout features. L5
delivers the skeleton in the ASCII sketch of §2 and nothing beyond it.

### 4.2 The precedent already set twice

This repository has resolved exactly this question twice, and both times in
favour of a dedicated element:

| Concept | Analytic element | Infographic element |
|---|---|---|
| SWOT | `UltraCanvasQuadrantChart` (`QuadrantType::SWOT`) | `UltraCanvasSWOTDiagram` (6 design presets) |
| Timeline | `UltraCanvasTimelineChart` / `UltraCanvasGanttChart` | `UltraCanvasTimelineDiagram` (9 design presets) |

`UltraCanvasSWOTDiagram` is the architectural template — same shape of problem
(a fixed handful of labelled lists, drawn a dozen different ways), same base
class, same design-enum-plus-orthogonal-toggles structure.
`UltraCanvasTimelineDiagram` is the geometric template — it already solves *a
decorated spine with content alternating above and below*, including a chevron
spine design, side policies (`Alternate` / `AllAbove` / `AllBelow` / `PerItem`),
node shapes, connector styles and palettes. A fishbone is that, plus angled ribs
and one more level of nesting.

### 4.3 Recommendation

1. **Build `UltraCanvasFishboneDiagram`** as a new element under
   `UltraCanvas/Plugins/Diagrams/` with the header at
   `UltraCanvas/include/Plugins/Diagrams/UltraCanvasFishboneDiagram.h`. It owns
   the fishbone as a *product*.
2. **Keep L5 in the mind map, re-scoped and demoted.** Unlike timeline L6 —
   which the mind map proposal dropped because a separate `timelinediagram`
   element slot already existed and would have collided — L5 has a legitimate
   residual use: *"I already have a mind map; show it as ribs."* It should be
   implemented as a plain layout case with no fishbone decoration, moved to the
   back of Phase 3, and documented as *not* the canonical fishbone.
3. **Cross-link both documents** so neither is read in isolation.

### 4.4 Registration checklist (what "done" touches)

Following the SWOT and Sequence-diagram changes verbatim:

- `UltraCanvas/include/Plugins/Diagrams/UltraCanvasFishboneDiagram.h`
- `UltraCanvas/Plugins/Diagrams/UltraCanvasFishboneDiagram.cpp` (+ a separate
  `…Layout.cpp` if the solver grows past ~400 lines, as the mind map and
  requirement diagram both did)
- `Apps/DemoApp/UltraCanvasFishboneDiagramExamples.cpp` + the declaration in
  `Apps/DemoApp/UltraCanvasDemo.h`
- `Apps/DemoApp/UltraCanvasDemo.cpp` — an `infoBuilder.AddItem("fishbonediagram",
  …)` entry with one `AddVariant` per design (there is **no** reserved slot
  today; the id is free)
- `CMakeLists.txt` — the demo source, alongside `UltraCanvasSWOTDiagramExamples.cpp`
- `Tests/FishboneModelTest.cpp` if the outline/Mermaid parsing lands (the
  `SequenceModelTest` precedent)
- `Docs/UltraCanvas/UltraCanvasFishboneDiagram.md`, a `CHANGELOG.md` entry, and
  `python3 scripts/generate_llms_txt.py`

---

## 5. Data model sketch

Layout-independent, in the sense of `SWOTDiagramDesignVariants.md`
§Observations 3 — every geometry in §6 renders from this and nothing else.

```cpp
struct FishboneCause {
    std::string text;
    std::vector<FishboneCause> subCauses;   // 5-Whys depth; usually empty
    Color        markerColor = Color(0,0,0,0);  // transparent = category accent
    std::string  tooltip;                   // overrides the generated tooltip
    bool         highlighted = false;       // image 2's emphasised cause
    double       weight      = 0.0;         // optional score; 0 = unweighted
    bool         isRootCause = false;       // verified root cause marker
};

struct FishboneCategory {
    std::string title;                      // "Machines (Equipment)"
    std::string iconPath;                   // media/icons/... (DrawImage)
    std::string iconGlyph;                  // short text fallback
    Color       accentColor = Color(0,0,0,0);   // transparent = next palette entry
    int         side = 0;                   // -1 above, +1 below, 0 = policy
    std::vector<FishboneCause> causes;
};

// Hit-test / selection handle, mirroring SWOTItemRef.
struct FishboneRef {
    int category = -1;
    int cause    = -1;   // -1 = the category itself
    int subCause = -1;
    bool IsValid() const { return category >= 0; }
};
```

The element then carries `std::string effectText`, `std::vector<FishboneCategory>
categories`, a `FishboneDesign`, a palette, and the option block of §6.

Two deliberate choices:

- **`subCauses` is a nested vector, not a flat list with parent ids.** Depth is
  bounded in practice (§2) and every renderer walks it top-down; the mind map's
  id-based model exists because mind maps are *edited* — a fishbone is
  authored once and drawn.
- **`weight` and `isRootCause` are on the cause, not a parallel array**, so a
  Pareto-linked or "verified cause" rendering stays a pure style decision.

---

## 6. Feature list

IDs follow the `UltraCanvasTimelineDiagramProposal.md` convention so the two can
be phased together.

### Designs (D — the preset enum)

Each maps to a reference image except `Classic`, which is the textbook form none
of the five shows but every engineering user expects (§3 constant 7).

| ID | `FishboneDesign` | Source | Description |
|---|---|---|---|
| D1 | `Classic` | textbook | Thin spine + arrowhead into an effect box; straight angled ribs; category boxes at the rib tips; cause text set along the twigs |
| D2 | `SpineChips` | image 1 | Solid tapered spine arrow; category pills *on* the spine ending in circular icon badges; dot-terminated twigs with outboard labels |
| D3 | `CrossedRibs` | image 2 | Full-length ribs crossing the spine, one above-category and one below-category per line; chips at both outer ends; sheared bullet blocks; icon badge at each crossing |
| D4 | `Bracket` | image 3 | Parallelogram bones (slanted edge + horizontal shelf); horizontal dot leaders; chips at the outer corner; optional spine caption |
| D5 | `ChevronSpine` | image 4 | Spine as a chain of chevron blocks with per-segment icons; hairline diagonal leaders to floating pills; bullet list per pill |
| D6 | `Columns` | image 5 | Rib-less: category as a vertical tinted panel straddling the spine, icon badge at the far end, bullet list inside; name printed beside the spine |
| D7 | `Vertical` | — | D1 rotated: spine runs top-to-bottom, ribs left and right (portrait slides; the `TimelineDesign::Vertical` precedent) |
| D8 | `Compact` | — | All categories on one side of the spine; halves the height, doubles the length (wide dashboards) |

### Data & content (C)

| ID | Feature |
|---|---|
| C1 | `SetEffect(text)` + `AddCategory` / `SetCategories` / `AddCause(category, text)` |
| C2 | `LoadCategoryPreset(FishboneCategoryPreset::SixM \| EightP \| FourS \| PEMPEM \| Software \| …)` — populates titles, colors and icons, leaving causes empty |
| C3 | `LoadSampleData()` — the `SWOTDiagramSamples::BusinessExample()` precedent |
| C4 | Sub-causes to depth 3, rendered as an indented second tier off the twig |
| C5 | Per-category icon (`iconPath` via `ctx->DrawImage`, or `iconGlyph`) |
| C6 | Per-cause highlight, root-cause marker and weight badge |

### Layout (L)

| ID | Feature |
|---|---|
| L1 | **Rib solver** — see §7; the only genuinely new algorithm here |
| L2 | Side policy: `Alternate` / `AllAbove` / `AllBelow` / `PerCategory` |
| L3 | Rib angle control (`ribAngleDegrees`, default 60°) with automatic reduction when the height budget is tight |
| L4 | Automatic spine-length and root-spacing solve from measured label widths |
| L5 | Head box sizing when `FishboneEffectPlacement::HeadBox` is active; reserved tail width |
| L6 | Category-count degradation: past `leaderThreshold` categories (default 6) fall back to D5-style hairline leaders even in other designs |
| L7 | `FitToContent()` / geometry query API (`GetCategoryRect`, `GetSpineLine`) |

### Style (S)

All orthogonal to the design enum — the `SWOTDiagramDesignVariants.md`
§Cross-cutting principle.

| ID | Feature |
|---|---|
| S1 | `FishboneHeadShape`: `Arrow` / `Triangle` / `Box` / `FishHead` / `Hidden` |
| S2 | `FishboneTailShape`: `Hidden` / `Chevron` / `Triangle` / `FishTail` |
| S3 | `FishboneEffectPlacement`: `HeadBox` / `Title` / `Hidden` (§3 constant 6) |
| S4 | `FishboneCauseMarker`: `Dot` / `Bullet` / `Numbered` / `Hidden` |
| S5 | `FishboneLabelPlacement`: `RibTip` / `OnSpine` / `BesideSpine` / `PanelHeader` |
| S6 | Palettes, reusing the `TimelinePalette` names (CorporateBlue, Vibrant, Pastel, Ocean, Sunset, Forest, Slate, Mono, Custom) |
| S7 | Dark theme (`SetDarkTheme`, as SWOT) — images 1 and 2 are dark, 3 and 5 light |
| S8 | Spine caption (image 3's "Potential Causes") and waypoint markers (image 5) |
| S9 | Gradient spine fills via `CreateLinearGradientPattern` (image 4) |

### Text (T)

| ID | Feature |
|---|---|
| T1 | Wrapping and ellipsis for cause labels at a computed max width |
| T2 | Edge alignment: labels outboard of a dot are right-aligned to it above the spine and left-aligned below (image 1) |
| T3 | Auto-shrink of cause font when a rib is over capacity, floored at a minimum size |
| T4 | Chip text measured to size the pill, not the pill fixed and the text clipped |

### Interaction (I)

| ID | Feature |
|---|---|
| I1 | Hover + selection of a category or a cause, via `FishboneRef` |
| I2 | `onCategorySelect` / `onCauseSelect` / `onCauseHover` / `onSelectionChange` callbacks (SWOT's signature set) |
| I3 | Structured tooltips through `UltraCanvasTooltipManager` — category, cause, sub-cause count, weight |
| I4 | Collapse/expand a category's causes |
| I5 | Optional pan/zoom by embedding `UltraCanvasDiagramViewport` (only needed for large boards; the five reference images all fit a frame) |

### Import / export (X)

| ID | Feature |
|---|---|
| X1 | **Mermaid `ishikawa-beta` import/export.** Mermaid added Ishikawa diagrams in v11.13.0 (April 2026): the first line after the keyword is the effect, and categories/causes are declared purely by indentation, nesting at least 3 deep. This is now the de-facto text notation and the element should read and write it |
| X2 | Indented-outline build helper — `BuildFromOutline(std::vector<std::pair<int,std::string>>)`, mirroring mind map D12; X1 is a thin layer over it |
| X3 | JSON persistence via `UltraCanvasJSON` (the `UltraCanvasMindMapIO` precedent) |
| X4 | CSV (`category,cause[,subcause]`) for spreadsheet-sourced QA data |

PlantUML has an open feature request for Ishikawa notation but has not shipped
one, so there is no second dialect to support — unlike the sequence diagram,
where `UltraCanvasSequenceTextExport` had to emit both.

---

## 7. The layout solver — the one hard part

Everything else in §6 is decoration over existing primitives. The rib solver is
new, so it is worth writing down before implementation.

Let the spine run along `y = ySpine` from `xTail` to `xHead`, with usable
half-height `H` above and below.

**Rib capacity.** A rib carrying `n` causes at slot spacing `s` needs length
`L ≥ (n + 1)·s`. Its vertical reach is `L·sin θ ≤ H`, so for a fixed angle the
maximum causes per rib is `n_max = floor(H / (s·sin θ)) − 1`. When a category
exceeds `n_max` the solver must, in order: reduce `s` to its floor, reduce `θ`
(L3), shrink the font (T3), and only then wrap the overflow into a second column
beside the rib.

**Root spacing.** A rib rooted at `x_i` extends *backwards* (toward the tail) by
`L·cos θ`, and its labels extend further by their measured width `w_label`.
Adjacent **same-side** categories must therefore satisfy
`Δ_same ≥ L·cos θ + w_label + gap`. With alternating sides, consecutive roots sit
`Δ_same / 2` apart, which is precisely why alternation is universal in the
reference images — it halves the spine length for a given category count.

**Spine length.** `xHead − xTail ≥ (c − 1)·Δ_same/2 + L·cos θ + w_label + headWidth
+ tailWidth`. Because `w_label` comes from `GetTextDimensions`, the solve must
run inside `UpdateLayout(ctx)` with a render context, cached and invalidated on
any data or style change — exactly how `UltraCanvasSWOTDiagram::UpdateLayout`
already works.

**Design-specific overrides.** D3 pairs categories onto one rib line (so the
root count halves); D5 replaces `L·cos θ` with a hairline leader whose footprint
is just the pill; D6 has no ribs at all and reduces to a column packer, which is
the SWOT `Columns` design with an alternating vertical offset.

**Degenerate cases to handle explicitly:** 1 category; 0 causes in a category
(draw the bone, no twigs); a cause label wider than the whole element; and more
categories than the spine can seat, which is L6's fallback.

---

## 8. What already exists to build on

| Need | Existing asset |
|---|---|
| Base class, tooltips, selection flags, animation, title | `Plugins/Charts/UltraCanvasChartElementBase.h` |
| Element to copy structurally (designs + toggles + hit rects + samples + factory) | `UltraCanvasSWOTDiagram.{h,cpp}` (955 lines — the whole shape of the job) |
| Spine + alternating sides + chevron spine + palettes + node shapes + connector styles | `UltraCanvasTimelineDiagram.h` (9 designs, `TimelineSidePolicy`, `TimelineConnectorStyle`, `TimelinePalette`) |
| Shapes: rounded rects, arcs, arbitrary paths, `FillLinePath`, linear/radial gradients, image patterns | `IRenderContext` (`UltraCanvasRenderContext.h`) |
| Icons | `ctx->DrawImage(path, rect, ImageFitMode::Contain)`, as `UltraCanvasMindMap` draws topic icons; assets in `media/icons/` |
| Text measurement | `ctx->GetTextDimensions()` / `GetTextLineDimensions()` |
| Structured tooltips (title + table + bullets) | `UltraCanvasTooltipManager` 2.3.0 |
| Pan/zoom/minimap, if I5 is wanted | `UltraCanvasDiagramViewport` |
| Mermaid parsing precedent | `UltraCanvasGitGraphMermaid.cpp`; `UltraCanvasSequenceTextExport` for the emit side |
| JSON persistence precedent | `UltraCanvasMindMapIO.cpp` |

**Two gaps worth noting.** There is no shared arrowhead helper — `FlowChart`,
`PertChart`, `NodeDiagram`, `ArcDiagram`, `AdjacencyDiagram`, `PacketDiagram` and
`RequirementDiagram` each carry a private one — and no shared text-wrap helper
either (`SWOTDiagram`, `PyramidChart` and `FilerWidget` each have their own
`WrapText`). This element will need both. Adding a seventh and fourth private
copy is the path of least resistance; extracting `DiagramPrimitives.h` alongside
`UltraCanvasDiagramRouting.h` would be the better trade if a second element is
ever refactored onto it. Recommendation: **write them private first, extract only
if a second consumer appears** — the `UltraCanvasDiagramViewport` extraction
happened exactly that way, after two elements had duplicated it.

---

## 9. Open questions (with recommendations)

1. **Element or mind map structure?** → Element (§4). L5 stays, re-scoped and
   demoted.
2. **How many designs in Phase 1?** → Four: `Classic`, `SpineChips`, `Bracket`,
   `Columns`. They span the geometry space (line ribs / chips-on-spine / bracket
   bones / no ribs); `CrossedRibs` and `ChevronSpine` are variations on solved
   geometry and can follow.
3. **Do sub-causes ship in Phase 1?** → The data model carries them from day one;
   rendering can land in Phase 2. Retrofitting a depth level into a solver that
   assumed two levels is the expensive mistake here.
4. **Is a separate `FishboneModel` warranted, as with the sequence diagram?** →
   No. That split earned its keep because the sequence model computes
   activations, message numbers and validation independently of rendering. A
   fishbone's model is a tree of strings; keep it in the element and put the
   parsing (X1/X2) in a small free-function header instead.
5. **Weighted causes — style or new feature?** → Style. `weight` renders as a bar
   or badge on the twig; anything more (a linked Pareto chart) belongs to the
   chart engine, not here.
6. **Should the demo reuse the SWOT/Venn/Timeline demo scaffold?** → Yes —
   absolute-positioned children in a tabbed page, as
   `UltraCanvasSequenceDiagramExamples.cpp` notes it does.

---

## 10. Suggested phasing

**Phase 1 — the element.** Data model (§5), the rib solver (§7), designs
`Classic` / `SpineChips` / `Bracket` / `Columns`, side policy, palettes, dark
theme, head/tail shapes, effect placement, per-category icons, wrapping +
ellipsis, hover/selection/tooltips, `LoadCategoryPreset` (6M, 8P, 4S),
`LoadSampleData`, factory helpers, a demo page with per-design tabs and live
toggles, `Docs/UltraCanvas/UltraCanvasFishboneDiagram.md`, a CHANGELOG entry and
regenerated `llms.txt`.

**Phase 2 — coverage.** `CrossedRibs`, `ChevronSpine`, `Vertical`, `Compact`;
sub-cause rendering; collapse/expand; weight and root-cause markers; category
overflow fallback (L6); spine caption and waypoint markers.

**Phase 3 — interop.** Mermaid `ishikawa-beta` import/export, `BuildFromOutline`,
JSON persistence, CSV import, and a `FishboneParseTest` covering the notation
corners.

**Phase 4 — nice to have.** Pan/zoom viewport, entrance animation, keyboard
navigation, and the re-scoped mind map L5 layout case.
