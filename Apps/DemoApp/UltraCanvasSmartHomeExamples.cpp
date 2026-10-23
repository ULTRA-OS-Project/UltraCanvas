// Apps/DemoApp/UltraCanvasSmartHomeExamples.cpp
// The Smart Home module on its simulated demo home: the module's dashboard
// widget (SmartHomePanel) over 30 simulated devices in 8 rooms; its own
// toolbar switches between the device grid, rooms, list, scenes,
// automations and the network. Nothing here touches
// real hardware. The full application on the same module is SmartHome
// Center (Apps/SmartHomeCenter).
// Version: 1.1.0 - the panel's own toolbar picks the view
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

// The module's headers first: the demo's own pull in X11, whose `None`
// macro would eat SmartHomeSecurityLevel::NoSecurity.
#include "UltraCanvasSmartHome.h"
#include "UltraCanvasSmartHomePanel.h"

#include "UltraCanvasDemo.h"
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

        auto container = std::make_shared<UltraCanvasContainer>("SmartHomeExamples", 0, 0, 1000, 700);
        container->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        container->SetPadding(10, 20, 10, 20);

        auto title = CreateLabel("SmartHomeTitle", 0, 0, 0, 30, "Smart Home - the demo home");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(30));
        container->AddChild(title);
        auto subtitle = CreateLabel("SmartHomeSubtitle", 0, 0, 0, 36,
            "The module's dashboard widget (SmartHomePanel) on the simulated home: 30 devices in 8 rooms that "
            "answer like real ones. A press on a card switches it; the toolbar picks the view and searches. "
            "The full application is SmartHome Center.");
        subtitle->SetFontSize(12);
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(36));
        container->AddChild(subtitle);

        auto panel = std::make_shared<SmartHome::SmartHomePanel>("SmartHomeDemoPanel", 960, 560);
        panel->Initialize();
        panel->RefreshDevices();
        panel->RefreshScenes();

        panel->layoutItem.SetFlexGrow(1);
        panel->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        container->AddChild(panel);
        return container;
    }

} // namespace UltraCanvas
