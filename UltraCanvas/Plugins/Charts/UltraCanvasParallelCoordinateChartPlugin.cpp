// Plugins/Charts/UltraCanvasParallelCoordinateChartPlugin.cpp
// Runtime-module entry for the parallel coordinate chart: registers the same
// descriptor the bundled build uses, but through the host vtable, so the
// module never resolves registry symbols itself. Compiled only into the
// ultracanvas-chart-parallel-coordinate MODULE target, never into the core.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "Plugins/Charts/UltraCanvasParallelCoordinateChart.h"

namespace {

bool InitParallelCoordinatePlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor descriptor =
        UltraCanvas::MakeParallelCoordinateChartDescriptor();
    host->RegisterElement(&descriptor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitParallelCoordinatePlugin)
