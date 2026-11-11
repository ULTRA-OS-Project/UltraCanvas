// Plugins/Widgets/UltraCanvasAlbumPlugin.cpp
// Runtime-module entry for UltraCanvasAlbum: registers the same descriptor a
// core with Album compiled in registers from UltraCanvas_RegisterSelectedWidgets(),
// but through the host vtable, so the module never resolves registry symbols
// itself. Compiled only into the ultracanvas-widget-album MODULE target, never
// into the core.
//
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasAlbum.h"

namespace {

bool InitAlbumPlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor descriptor = UltraCanvas::MakeAlbumDescriptor();
    host->RegisterElement(&descriptor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitAlbumPlugin)
