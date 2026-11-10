// Plugins/Widgets/UltraCanvasColorPaletteManagerPlugin.cpp
// Runtime-module entry for UltraCanvasColorPaletteManager: registers the same descriptor a
// core with ColorPaletteManager compiled in registers from UltraCanvas_RegisterSelectedWidgets(),
// but through the host vtable, so the module never resolves registry symbols
// itself. Compiled only into the ultracanvas-widget-color-palette-manager MODULE target, never
// into the core.
//
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasColorPaletteManager.h"

namespace {

bool InitColorPaletteManagerPlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor descriptor = UltraCanvas::MakeColorPaletteManagerDescriptor();
    host->RegisterElement(&descriptor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitColorPaletteManagerPlugin)
