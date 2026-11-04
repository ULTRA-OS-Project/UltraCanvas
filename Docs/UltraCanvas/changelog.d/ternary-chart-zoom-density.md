- **Ternary chart, Phase 3: zoom and density.**
  - `UltraCanvasTernaryChart::SetZoom` shows only the sub-triangle where each
    part is at least a minimum share. The minimums are kept in the model
    (`TernaryAxis::minimum`) and saved in the JSON.
  - When zoomed, everything follows the zoom. The ticks cover the visible
    range, switching to a finer step when needed. Drawing is clipped to the
    triangle. Regions are cut at its edges, so their names stay inside, and
    points outside it are left out.
  - Dragging across the triangle zooms to the smallest sub-triangle that holds
    both ends of the drag, shown as a preview while dragging. A double click
    shows the whole triangle again. A click on a region now fires on release.
  - `SetDensity(n)` bins the visible points into n * n small triangles and
    colours each through a colour map, with a colour bar.
    `SetDensityContours` draws contour lines over the counts, and
    `SetShowPoints(false)` hides the markers under them.
  - New sample: `TernarySamples::FeldsparCloud()`, 2,400 invented analyses.
    The demo gains a Density tab, a density control, a zoom-out button and a
    read-out of the zoom.
