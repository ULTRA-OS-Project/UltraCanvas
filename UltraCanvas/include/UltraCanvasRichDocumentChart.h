// include/UltraCanvasRichDocumentChart.h
// A word-processing document's chart (RichChart, UltraCanvasRichDocument.h)
// as a chart of the chart engine: columns and bars (UltraCanvasEngineBarChart),
// lines and areas (UltraCanvasEngineLineChart), pies and doughnuts
// (UltraCanvasEnginePieChart), scatter and bubble charts
// (UltraCanvasEngineScatterChart, its groups joined where the series have
// lines), radar charts (UltraCanvasEngineRadarChart) and stock charts
// (UltraCanvasEngineFinancialChart).
//
// The document model and its file formats stay free of the UI stack: they
// carry the chart's data. This is the bridge the view draws it with - and
// any application can use to show a document's chart as a live element.
//
// Version: 1.4.0 - every chart of the framework a table can feed: CreateRichChartElement and DrawRichChart
//                  build the element of any RichChart::Kind (waterfall, Sankey, Gantt, heatmap ...);
//                  CreateRichDocumentChart makes the engine's among them
// Version: 1.3.0 - radar, bubble and stock charts; smoothed lines
// Version: 1.2.0 - a scatter chart is always the engine's scatter chart
// Version: 1.1.0 - `ink`: the chart's furniture on a dark page
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"
#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <memory>

namespace UltraCanvas {

class UltraCanvasUIElement;

// The engine chart showing `chart`, `width` x `height` pixels, with the
// look of a chart on a page: no background of its own, the series in the
// file's colours (the office palette where it gives none), no entrance
// animation. nullptr when the chart has nothing to show.
// `ink` (alpha 0 = a light page's own) is what its axes, grid and words are
// drawn in on a dark page.
std::shared_ptr<UltraCanvasChartEngineElement> CreateRichDocumentChart(const RichChart& chart, int width, int height,
                                                                       const Color& ink = Color(0, 0, 0, 0));

// Draws `chart` into `target` of `ctx` (the view's page, a print, a PDF).
// `natural` is the chart's own size in pixels: it is laid out for that size
// and scaled into the target, so its text keeps its proportion under zoom.
// `cached` (optional) keeps the element between calls; it is rebuilt when
// null or of another size. Returns false when there is nothing to draw.
bool DrawRichDocumentChart(IRenderContext* ctx, const RichChart& chart, const Rect2Dd& target, const Size2Di& natural,
                           std::shared_ptr<UltraCanvasChartEngineElement>* cached = nullptr,
                           const Color& ink = Color(0, 0, 0, 0));

// ===== EVERY KIND =====
// What each kind reads from the chart's categories and series (the
// spreadsheet's columns, left to right, after a column of names):
//   Column, Bar, Line, Area, Pie, Doughnut, Radar, Polar, RadialBar, Funnel,
//   Pyramid, NestedArea, WordCloud, Parliament, CircularProgress:
//                     names, then one or more series of values
//   Waterfall:        names and the changes (a name "Total" or "Subtotal"
//                     stands on zero at the running total)
//   DivergingBar:     names, then the answers' counts - the first half of the
//                     series negative, a middle one neutral, the rest positive
//   PopulationPyramid, Dumbbell:  names, then two series (left and right,
//                     before and after)
//   Mekko:            names (the columns), then a series per segment
//   CumulativeFlow:   periods, then a series per stage
//   Gauge:            the first value (a second series' first value is the
//                     maximum)
//   Scatter, Bubble:  each series' xValues, values (and sizes)
//   Quadrant, ImportancePerformance, Hexbin: names, x, y
//   Ternary, Scatter3D: names, then three series (a, b, c / x, y, z)
//   Histogram, BoxWhisker, Jitter, Ridgeline: raw values, a group per
//                     series (or per name, with one series)
//   Stock:            names, (open,) high, low, close
//   Sankey:           sources (names), targets (a series' labels), amounts
//   Chord, Heatmap, Contour, Surface: a matrix - names down, series across
//   Treemap, Sunburst: names, deeper levels (series' labels), values
//   Gantt:            tasks, starts, ends (dates as day numbers: a
//                     spreadsheet's serials; an end before the start is a
//                     number of days)
//   Timeline:         names, dates (and end dates for spans)
//   CalendarHeatmap:  two series: dates and values

// Whether `kind` is one of the chart engine's (CreateRichDocumentChart
// makes it); the others are the framework's other chart elements.
bool IsRichChartEngineKind(RichChart::Kind kind);

// The element showing `chart`, `width` x `height` pixels, for every kind:
// the engine's chart (CreateRichDocumentChart) or the chart element of the
// kind - a funnel, a Sankey diagram, a Gantt chart ... - with its data,
// laid out for that size, no entrance animation, no tooltips. nullptr when
// the chart has nothing to show.
std::shared_ptr<UltraCanvasUIElement> CreateRichChartElement(const RichChart& chart, int width, int height,
                                                             const Color& ink = Color(0, 0, 0, 0));

// DrawRichDocumentChart for every kind: `cached` holds the element of any
// kind. (DrawRichDocumentChart draws every kind too, but keeps only an
// engine chart between calls.)
bool DrawRichChart(IRenderContext* ctx, const RichChart& chart, const Rect2Dd& target, const Size2Di& natural,
                   std::shared_ptr<UltraCanvasUIElement>* cached = nullptr, const Color& ink = Color(0, 0, 0, 0));

} // namespace UltraCanvas
