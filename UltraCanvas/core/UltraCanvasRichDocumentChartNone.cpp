// core/UltraCanvasRichDocumentChartNone.cpp
// The document-chart bridge (UltraCanvasRichDocumentChart.h) of a core built
// without the chart engine (ULTRACANVAS_ENABLE_CHARTS=OFF): there is no chart
// to make, and a caller draws the chart's shapes instead, as
// UltraCanvasRichTextEdit does when DrawRichDocumentChart returns false.
// UltraCanvas/CMakeLists.txt builds this file in place of
// UltraCanvasRichDocumentChart.cpp there; the engine's charts that file calls
// are not in that core, and no program could link it.
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasRichDocumentChart.h"

namespace UltraCanvas {

std::shared_ptr<UltraCanvasChartEngineElement> CreateRichDocumentChart(const RichChart& chart, int width, int height,
                                                                       const Color& ink) {
    (void)chart; (void)width; (void)height; (void)ink;
    return nullptr;
}

bool DrawRichDocumentChart(IRenderContext* ctx, const RichChart& chart, const Rect2Dd& target, const Size2Di& natural,
                           std::shared_ptr<UltraCanvasChartEngineElement>* cached, const Color& ink) {
    (void)ctx; (void)chart; (void)target; (void)natural; (void)ink;
    if (cached) cached->reset();
    return false;
}

} // namespace UltraCanvas
