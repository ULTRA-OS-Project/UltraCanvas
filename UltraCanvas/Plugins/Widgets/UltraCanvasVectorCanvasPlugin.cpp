// Plugins/Widgets/UltraCanvasVectorCanvasPlugin.cpp
// Runtime-module entry for UltraCanvasVectorCanvas: registers the same descriptor a
// core with VectorCanvas compiled in registers from UltraCanvas_RegisterSelectedWidgets(),
// but through the host vtable, so the module never resolves registry symbols
// itself. Compiled only into the ultracanvas-widget-vector-canvas MODULE target, never
// into the core.
//
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasVectorCanvas.h"

namespace {

bool InitVectorCanvasPlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor descriptor = UltraCanvas::MakeVectorCanvasDescriptor();
    host->RegisterElement(&descriptor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitVectorCanvasPlugin)
