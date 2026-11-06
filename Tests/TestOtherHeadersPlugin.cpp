// Tests/TestOtherHeadersPlugin.cpp
// Two plugin DSOs the loader must not call (built twice, once per define):
//   ULTRACANVAS_TEST_OTHER_HEADERS  - compiled "against other framework
//                                     headers": another headers id
//   ULTRACANVAS_TEST_NO_HEADERS_ID  - as plugins were built before the id
//                                     existed: UltraCanvas_PluginInit alone
// The classes either would hand the host are laid out by headers this
// framework was not built from, so the loader refuses both before it calls
// UltraCanvas_PluginInit; each would register an element if it were called.
//
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#if defined(ULTRACANVAS_TEST_OTHER_HEADERS)
#define ULTRACANVAS_PLUGIN_BUILT_WITH_HEADERS "0123456789abcdef"
#endif

#include "UltraCanvasElementPlugins.h"
#include "ElementPluginTestSupport.h"

using namespace UltraCanvas;

namespace {

bool InitOtherHeadersPlugin(const UltraCanvasPluginHost* host) {
    UCElementDescriptor d;
#if defined(ULTRACANVAS_TEST_OTHER_HEADERS)
    d.typeName = "other-headers-widget";
#else
    d.typeName = "no-headers-id-widget";
#endif
    d.category = UCPluginCategory::Widget;
    d.displayName = "Stale Widget";
    d.version = "1.0.0";
    d.create = [](const std::string& id, int x, int y, int w, int h) {
        return std::static_pointer_cast<UltraCanvasUIElement>(std::make_shared<TestWidget>(id, x, y, w, h));
    };
    host->RegisterElement(&d);
    return true;
}

} // namespace

#if defined(ULTRACANVAS_TEST_OTHER_HEADERS)
ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitOtherHeadersPlugin)
#else
// What ULTRACANVAS_DEFINE_ELEMENT_PLUGIN wrote before plugins said which
// headers they were compiled against: the entry point and nothing else.
extern "C" ULTRACANVAS_PLUGIN_EXPORT bool UltraCanvas_PluginInit(const UltraCanvasPluginHost* host) {
    if (!host || !host->RegisterElement) return false;
    return InitOtherHeadersPlugin(host);
}
#endif
