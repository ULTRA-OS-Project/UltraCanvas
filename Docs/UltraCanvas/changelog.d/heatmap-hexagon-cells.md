- **Hexagon cells in the heat map.** `HeatmapCellShape::Hexagon`
  (`UltraCanvasHeatmapChartElement`) and `ChartHeatmapCellShape::Hexagon`
  (`UltraCanvasEngineHeatmapChart`) draw a matrix as regular pointy-top
  hexagons in an offset grid - odd rows shifted right by half a cell - as
  large as fit the chart, so the hexagons keep their shape whatever its
  proportions. Gaps, borders, values, row order, masks and the colour bar
  apply as for the other shapes; the row and column labels are left out, and
  the cell under the pointer is the hexagon drawn there. Demo: **Charts >
  Heat map > Interactive Heatmap**, *Cell shape: Hexagon*. Test:
  `HeatmapHexagonRenderTest`.
