// Tests/TestElementPlugin.cpp
// A real element plugin DSO used by ElementPluginTest: registers one widget
// through the host vtable, exactly as a shipped plugin would.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "ElementPluginTestSupport.h"

using namespace UltraCanvas;

namespace {

bool InitTestPlugin(const UltraCanvasPluginHost* host) {
    UCElementDescriptor d;
    d.typeName = "plugin-widget";
    d.category = UCPluginCategory::Widget;
    d.displayName = "Plugin Widget";
    d.description = "Widget provided by the ElementPluginTest DSO";
    d.version = "1.0.0";
    d.propertyKeys = {"columns", "title", "compact"};
    d.create = [](const std::string& id, int x, int y, int w, int h) {
        auto widget = std::make_shared<TestWidget>(id, x, y, w, h);
        widget->tag = "plugin-widget";
        return std::static_pointer_cast<UltraCanvasUIElement>(widget);
    };
    d.textKeywords = {"pluginboard"};
    d.createFromText = [](const std::string& id, int x, int y, int w, int h,
                          const std::string& text, std::string*) {
        auto widget = std::make_shared<TestWidget>(id, x, y, w, h);
        widget->tag = "plugin-widget";
        widget->textPayload = text;
        return std::static_pointer_cast<UltraCanvasUIElement>(widget);
    };
    host->RegisterElement(&d);
    if (host->Log) host->Log(0, "test element plugin registered plugin-widget");
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitTestPlugin)
