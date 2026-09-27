- **Bar connections for bar charts on the chart engine.** A line from the
  value end of each bar to the same series' bar in the next category - it
  traces every series across the groups of a clustered chart, and anchored at
  the bar edges it is the series line of a stacked chart.
  - `BuildBarConnections(projection, spans, options)`
    (`Engine/UltraCanvasChartSeries.h`) builds one screen polyline per
    unbroken run of each series. `ChartBarConnectionShape::Straight` or
    `Curved`; the curve is a monotone cubic, so it is smooth, stays flat
    between equal bars and never overshoots the bars it joins.
    `ChartBarConnectionAnchor::BarCenter` meets the middle of each bar's top,
    `BarEdges` runs corner to corner along it. A missing value breaks the line
    unless `bridgeGaps` is set. The line is sampled through the projection, so
    it follows the rings under Polar.
  - `UltraCanvasChartEngineElement::RenderBarConnections` strokes them in the
    series colours with a `ChartBarConnectionStyle`: width, a wider stroke for
    an emphasised (hovered) series, dashes, dot markers at every bar, and a
    halo in the plot-area colour so a line stays readable across a bar of its
    own colour.
  - DemoApp's Bar Charts page shows it: the Clustered tab joins the four
    fruit series (Off / Straight / Curved, and "From bar edges"), and the
    Stacked tab offers the same controls for series lines. Hovering a bar
    widens its series' line.
