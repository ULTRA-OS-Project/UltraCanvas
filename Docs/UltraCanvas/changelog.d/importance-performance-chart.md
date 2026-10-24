- **New chart: the importance–performance chart (Phase 1).**
  `UltraCanvasImportancePerformanceChart` plots the factors of a service, by
  how much customers care about each one and how it compares with
  competitors, over authored zones that name the action. It is built on
  `UltraCanvasEngineScatterChart`, so groups, markers, point labels, the
  legend and hover come from there. The chart adds:
  - filled zones, stepped or smoothed, each with a title and an optional
    icon. A title is placed at the zone's pole of inaccessibility in the part
    later zones leave visible, and is left out when it does not fit.
  - labelled bands, such as Slack's lower bound of acceptability. The label
    is turned along the band's longest straight stretch, and factor names
    steer around it and around the zone titles.
  - named level ranges on both axes, with the end captions at the screen's
    ends even when an axis is inverted for Slack's "1 = best" scales.
  - the zone of each factor in its tooltip, and a zone's title, action and
    factor count on a bare stretch of it.

  The model (`UltraCanvasImportancePerformanceModel.h`) is UI-free.
  `ZoneOf` and `ClassifyFactors` test the same smoothed polygon the chart
  draws, so a factor drawn in a zone is always reported in it. It also has
  `Validate()` and the `SlackZones(smooth)` and `SampleFactors()` presets.
  On the demo page **Charts → Importance-Performance Chart**, the two Slack
  forms list the factors by zone.
  `Tests/ImportancePerformanceModelTest.cpp` and
  `Tests/ImportancePerformanceChartRenderTest.cpp` cover the model and the
  drawing; the render test writes PNGs when `ULTRACANVAS_SCREENSHOT_DIR` is
  set. Guide: `Docs/UltraCanvas/UltraCanvasImportancePerformanceChart.md`.
- **Chart engine: `ChartHighlightShape::Polygon`.** An explicit outline in
  value space, taken from `members`. It is rounded by `smoothing` the way a
  `Blob` is, but in value space before the mapping, so a caller that smooths
  the same outline tests exactly the drawn shape. Under Polar its edges are
  sampled along the rings.
- **Engine scatter chart: a derived chart can keep highlights of its own.**
  Every data, theme and legend change clears *all* highlights before it
  rebuilds the confidence ellipses, so a subclass's highlights would have been
  wiped on the first change. The rebuild now calls the protected
  `AddChartHighlights()` first, and `RefreshHighlights()` reruns it.
