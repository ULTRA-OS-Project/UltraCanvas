// Plugins/Widgets/UltraCanvasVideoTimelinePlugin.cpp
// Runtime-module entry for the video timeline: registers the same descriptor
// a core with VideoTimeline compiled in registers from
// UltraCanvas_RegisterSelectedWidgets(), but through the host vtable, so the
// module never resolves registry symbols itself. Compiled only into the
// ultracanvas-widget-video-timeline MODULE target, never into the core.
//
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasVideoTimeline.h"

namespace {

bool InitVideoTimelinePlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor descriptor = UltraCanvas::MakeVideoTimelineDescriptor();
    host->RegisterElement(&descriptor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitVideoTimelinePlugin)
