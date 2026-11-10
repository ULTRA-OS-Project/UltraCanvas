// Tests/ElementModulesTest.cpp
// The selectable widgets (ULTRACANVAS_WIDGETS) and charts (ULTRACANVAS_CHARTS)
// through the element registry, by type name (UltraCanvasElementPlugins.h).
// One compiled into the core is registered by
// UltraCanvas_RegisterSelectedWidgets() / UltraCanvas_RegisterSelectedCharts();
// one left out of a lean build comes from its runtime module
// (ultracanvas-widget-<name>, ultracanvas-chart-<name>), loaded from
// UC_ELEMENT_MODULE_DIR on the first Create - which also proves the module
// carries the framework headers id the loader insists on. Each is created by
// name with its id and has the expected category. UC_EXPECTED_ELEMENTS (from
// CMake) lists "type:Widget" / "type:Chart" for what this build provides. No
// display needed.
//
// Version: 2.0.0 - every selectable widget and chart, not only the video timeline (was VideoTimelinePluginTest)
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasUIElement.h"

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what.c_str());
    if (!cond) ++failures;
}

int main() {
    struct Expected { std::string type; UCPluginCategory category; };
    std::vector<Expected> expected;
    {
        std::stringstream list(UC_EXPECTED_ELEMENTS);
        std::string entry;
        while (std::getline(list, entry, ',')) {
            const size_t colon = entry.find(':');
            if (colon == std::string::npos) continue;
            expected.push_back({entry.substr(0, colon),
                                entry.substr(colon + 1) == "Chart" ? UCPluginCategory::Chart
                                                                   : UCPluginCategory::Widget});
        }
    }
    std::printf("ElementModulesTest: %zu element(s) expected\n", expected.size());

    // A host that loads widget modules has a UI of its own, so the element
    // base (UltraCanvasUIElement and the CSS layout box under it) is in its
    // executable for the modules to resolve. This test has none - it only
    // talks to the registry - so it makes one plain element; without it a
    // static core leaves the base out and a module fails at dlopen with
    // "undefined symbol: ...CSSLayout::Element...".
    auto hostElement = std::make_shared<UltraCanvasUIElement>("host", 0, 0, 10, 10);
    Check(hostElement != nullptr, "the host has a UI element of its own");

    // The documented startup calls: register what is compiled in, and in a
    // static core keep what the modules need (the chart engine, the timeline
    // model).
    UltraCanvas_RegisterSelectedWidgets();
    UltraCanvas_RegisterSelectedCharts();
#ifdef UC_ELEMENT_MODULE_DIR
    UltraCanvas_AddPluginDirectory(UC_ELEMENT_MODULE_DIR);
#endif

    for (const Expected& e : expected) {
        const std::string& type = e.type;
        // A miss on Create refreshes the plugin directories and retries, so
        // for a module this is what loads it.
        std::shared_ptr<UltraCanvasUIElement> element =
            UltraCanvasElementRegistry::Create(type, "w-" + type, 0, 0, 320, 200);
        if (!element) std::printf("  (last plugin error: %s)\n", UltraCanvas_GetLastPluginError().c_str());
        Check(element != nullptr, "Create(\"" + type + "\") makes an element");
        Check(element && element->GetIdentifier() == "w-" + type, type + ": with the id it was given");
        const UCElementDescriptor* d = UltraCanvasElementRegistry::Find(type);
        Check(d != nullptr && d->category == e.category && !d->displayName.empty(),
              type + ": in the registry under its category, with a display name");
    }

    if (failures) std::printf("%d check(s) failed\n", failures);
    else std::printf("all checks passed\n");
    return failures ? 1 : 0;
}
