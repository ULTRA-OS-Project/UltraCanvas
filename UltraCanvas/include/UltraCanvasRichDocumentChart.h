// include/UltraCanvasRichDocumentChart.h
// A word-processing document's chart (RichChart, UltraCanvasRichDocument.h)
// as a chart of the chart engine: columns and bars (UltraCanvasEngineBarChart),
// lines and areas (UltraCanvasEngineLineChart), pies and doughnuts
// (UltraCanvasEnginePieChart) and scatter charts (UltraCanvasEngineScatterChart,
// or the line chart over numeric x where the points are joined).
//
// The document model and its file formats stay free of the UI stack: they
// carry the chart's data. This is the bridge the view draws it with - and
// any application can use to show a document's chart as a live element.
//
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"
#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <memory>

namespace UltraCanvas {

// The engine chart showing `chart`, `width` x `height` pixels, with the
// look of a chart on a page: no background of its own, the series in the
// file's colours (the office palette where it gives none), no entrance
// animation. nullptr when the chart has nothing to show.
std::shared_ptr<UltraCanvasChartEngineElement> CreateRichDocumentChart(const RichChart& chart, int width, int height);

// Draws `chart` into `target` of `ctx` (the view's page, a print, a PDF).
// `natural` is the chart's own size in pixels: it is laid out for that size
// and scaled into the target, so its text keeps its proportion under zoom.
// `cached` (optional) keeps the element between calls; it is rebuilt when
// null or of another size. Returns false when there is nothing to draw.
bool DrawRichDocumentChart(IRenderContext* ctx, const RichChart& chart, const Rect2Dd& target, const Size2Di& natural,
                           std::shared_ptr<UltraCanvasChartEngineElement>* cached = nullptr);

} // namespace UltraCanvas
