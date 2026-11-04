- **Ternary chart, Phase 2: regions, paths, bubbles, curves, files.**
  - Regions are authored areas of the triangle. They are filled under the
    grid and named inside themselves; a long name wraps or steps its font
    down to fit. Each region answers hover and `onRegionClick`.
    `TernaryModel::RegionOf` classifies a composition by the drawn geometry,
    and a point's tooltip names its region.
  - `TernarySamples::SoilTexture()` is the USDA soil texture triangle. Its
    twelve class outlines are derived from the class rules, and the model
    test checks them against those rules on a 19,900-point grid.
  - Paths (`TernarySeriesKind::Path`, with arrowheads) show a composition
    changing over time. A point's `size` draws it as a bubble.
    `TernaryCurve` adds reference lines that appear in the legend, such as the
    Hardy-Weinberg parabola of the new `DeFinetti()` sample.
  - `SetReadingGuide(true)` draws the three grid lines through the pointer,
    with its shares.
  - `ExportTernaryJson` / `ImportTernaryJson` round-trip the whole model as
    `"format": "ultracanvas-ternary"`. `ExportTernaryCsv` /
    `ImportTernaryCsv` carry the points, with the part columns named by the
    axis titles.

  The demo's Ternary Chart page gains Vote swings, Soil texture and
  De Finetti tabs, a reading-guide control, class clicks that count the
  samples, and Open / Save JSON and Import / Export CSV through
  `UltraCanvasFileLoader`.
