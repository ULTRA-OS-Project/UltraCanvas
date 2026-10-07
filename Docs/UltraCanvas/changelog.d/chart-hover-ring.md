- Charts: the hover ring (`SetEnableSelection`) is drawn around the point
  where the chart draws it. In `XAxisLabelMode::DataLabel` the points are
  spaced by index, but the ring was placed by the x value, so it could sit
  beside the point; it is also no longer drawn for a point the zoom has
  scrolled out of the plot.
