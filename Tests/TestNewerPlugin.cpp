// Tests/TestNewerPlugin.cpp
// A plugin DSO built for a newer host table than this framework passes: it
// would call members the host does not have, so the handshake in
// ULTRACANVAS_DEFINE_ELEMENT_PLUGIN must refuse it before it registers
// anything. (It asked only for version 1 until 2026-11, whatever it was
// built for.)
//
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

// What a plugin built by a framework one table version ahead requires.
#define ULTRACANVAS_PLUGIN_BUILT_FOR_ABI (::UltraCanvas::ULTRACANVAS_PLUGIN_HOST_ABI_VERSION + 1)

#include "UltraCanvasElementPlugins.h"
#include "ElementPluginTestSupport.h"

using namespace UltraCanvas;

namespace {

bool InitNewerPlugin(const UltraCanvasPluginHost* host) {
    UCElementDescriptor d;
    d.typeName = "newer-widget";
    d.category = UCPluginCategory::Widget;
    d.displayName = "Newer Widget";
    d.version = "1.0.0";
    d.create = [](const std::string& id, int x, int y, int w, int h) {
        return std::static_pointer_cast<UltraCanvasUIElement>(std::make_shared<TestWidget>(id, x, y, w, h));
    };
    host->RegisterElement(&d);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitNewerPlugin)
