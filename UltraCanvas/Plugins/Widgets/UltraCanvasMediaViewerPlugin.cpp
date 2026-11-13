// Plugins/Widgets/UltraCanvasMediaViewerPlugin.cpp
// Runtime-module entry for UltraCanvasMediaViewer: registers the same
// descriptor a core with MediaViewer compiled in registers from
// UltraCanvas_RegisterSelectedWidgets(), but through the host vtable, so the
// module never resolves registry symbols itself. Compiled only into the
// ultracanvas-widget-media-viewer MODULE target, never into the core.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasMediaViewer.h"

namespace {

bool InitMediaViewerPlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor descriptor = UltraCanvas::MakeMediaViewerDescriptor();
    host->RegisterElement(&descriptor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitMediaViewerPlugin)
