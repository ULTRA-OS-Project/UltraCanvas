// Apps/DemoApp/UltraCanvasSmartHomeExamples.cpp
// The Smart Home module on its simulated demo home: the module's dashboard
// widget (SmartHomePanel) over 30 simulated devices in 8 rooms; its own
// toolbar switches between the device grid, rooms, list, scenes,
// automations and the network. Nothing here touches
// real hardware. The full application on the same module is SmartHome
// Center (Apps/SmartHomeCenter).
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): the panel in
//                  a card that fills the page area, title and description sized
//                  to their text
// Version: 1.1.0 - the panel's own toolbar picks the view
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

// The module's headers first: the demo's own pull in X11, whose `None`
// macro would eat SmartHomeSecurityLevel::NoSecurity.
#include "UltraCanvasSmartHome.h"
#include "UltraCanvasSmartHomePanel.h"

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

namespace UltraCanvas {

    namespace {

    // Starts the demo home once, and stops the module before the process
    // ends (its threads must not outlive main).
    struct DemoHome {
        DemoHome() {
            auto& api = SmartHome::SmartHomeAPI::Instance();
            api.Initialize();
            api.EnableProtocol(SmartHome::SmartHomeProtocolType::Simulated);
        }
        ~DemoHome() { SmartHome::SmartHomeAPI::Instance().Shutdown(); }
    };

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSmartHomeExamples() {
        static DemoHome home;   // after SmartHomeAPI's own static, so it goes first

        auto page = DemoKit::CreatePage("SmartHomeExamples", "Smart Home - the demo home",
            "The module's dashboard widget (SmartHomePanel) on the simulated home: 30 devices in 8 rooms that "
            "answer like real ones. A press on a card switches it; the toolbar picks the view and searches. "
            "The full application is SmartHome Center.");
        // The panel scrolls its own views: the page is the area's height.
        page->SetFillsArea(true);

        auto card = DemoKit::CreateCard("SmartHomeCard");
        card->SetPadding(4);
        DemoKit::Add(page, card, 1);
        card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

        auto panel = std::make_shared<SmartHome::SmartHomePanel>("SmartHomeDemoPanel", 960, 560);
        panel->Initialize();
        panel->RefreshDevices();
        panel->RefreshScenes();

        DemoKit::AddStretched(card, panel);
        panel->size.height = CSSLayout::Dimension::Auto();
        panel->layoutItem.SetFlexGrow(1);
        panel->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        return page;
    }

} // namespace UltraCanvas
