// Tests/VideoTimelinePluginTest.cpp
// The video timeline through the element registry, by its type name
// "video-timeline" (UltraCanvasElementPlugins.h). With VideoTimeline compiled
// into the core (ULTRACANVAS_WIDGETS, "all" by default) the descriptor comes
// from UltraCanvas_RegisterSelectedWidgets(); in a lean build it comes from the
// ultracanvas-widget-video-timeline runtime module, loaded from
// UC_WIDGET_MODULE_DIR on the first Create. Either way the element is created
// by name, is a Widget, and is the timeline. No display needed.
//
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasUIElement.h"
#if UC_VIDEO_TIMELINE_BUNDLED
#include "UltraCanvasVideoTimeline.h"
#endif

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int failures = 0;
#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (cond) std::printf("  ok   %s\n", what);                              \
        else { std::printf("  FAIL %s\n", what); ++failures; }                   \
    } while (0)

int main() {
    std::printf("VideoTimelinePluginTest (%s)\n",
                UC_VIDEO_TIMELINE_BUNDLED ? "compiled into the core" : "runtime module");

    // A host that loads widget modules has a UI of its own, so the element
    // base (UltraCanvasUIElement and the CSS layout box under it) is in its
    // executable for the module to resolve. This test has none - it only
    // talks to the registry - so it makes one plain element; without it a
    // static core leaves the base out and the module fails at dlopen with
    // "undefined symbol: ...CSSLayout::Element...".
    auto hostElement = std::make_shared<UltraCanvasUIElement>("host", 0, 0, 10, 10);
    CHECK(hostElement != nullptr, "the host has a UI element of its own");

    // The documented startup call: registers what is compiled in, and in a
    // static core anchors what the widget modules need.
    UltraCanvas_RegisterSelectedWidgets();

#if UC_VIDEO_TIMELINE_BUNDLED
    const UCElementDescriptor* bundled = UltraCanvasElementRegistry::Find("video-timeline");
    CHECK(bundled != nullptr, "registered by UltraCanvas_RegisterSelectedWidgets");
#else
  #ifdef UC_WIDGET_MODULE_DIR
    UltraCanvas_AddPluginDirectory(UC_WIDGET_MODULE_DIR);
  #else
    std::printf("  skip the module was not built on this platform (Windows static core)\n");
    return 0;
  #endif
#endif

    // A miss on Create refreshes the plugin directories and retries, so in
    // the module build this is what loads it.
    std::shared_ptr<UltraCanvasUIElement> element =
        UltraCanvasElementRegistry::Create("video-timeline", "timeline", 0, 0, 640, 200);
    if (!element) std::printf("  (last plugin error: %s)\n", UltraCanvas_GetLastPluginError().c_str());
    CHECK(element != nullptr, "Create(\"video-timeline\") makes an element");
    if (element) {
        CHECK(element->GetIdentifier() == "timeline", "with the id it was given");
    }

    const UCElementDescriptor* d = UltraCanvasElementRegistry::Find("video-timeline");
    CHECK(d != nullptr && d->category == UCPluginCategory::Widget, "a Widget in the registry");
    CHECK(d != nullptr && d->displayName == "Video Timeline", "under its display name");

#if UC_VIDEO_TIMELINE_BUNDLED
    CHECK(std::dynamic_pointer_cast<UltraCanvasVideoTimeline>(element) != nullptr,
          "the element is the timeline (typed API available)");
#endif

    if (failures) std::printf("%d check(s) failed\n", failures);
    else std::printf("all checks passed\n");
    return failures ? 1 : 0;
}
