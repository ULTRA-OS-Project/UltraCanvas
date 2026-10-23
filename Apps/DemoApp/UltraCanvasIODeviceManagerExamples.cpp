// Apps/DemoApp/UltraCanvasIODeviceManagerExamples.cpp
// The IODeviceManager page: DeviceExplorer itself. The panel is the
// application's (Apps/DeviceExplorer/ui/DeviceExplorerPanel), compiled into
// the demo from the same files, so the page shows exactly what the
// application does - the printers, scanners and cameras this computer has,
// grouped by category, connection or backend, with everything each backend
// reports, a printer's status and ink, and hot-plug changes as they happen.
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "DeviceExplorerPanel.h"

#ifndef DEVICEEXPLORER_VERSION
#error "DEVICEEXPLORER_VERSION is not defined: CMakeLists.txt sets it on this file from Docs/DeviceExplorer/CHANGELOG.md"
#endif

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateIODeviceManagerExamples() {
        auto page = DemoKit::CreatePage(
                "IODevPage", "IODeviceManager - DeviceExplorer",
                "The devices connected to this computer as IODeviceManager finds them: printers "
                "(CUPS, the Windows spooler, IPP), scanners (SANE, eSCL) and cameras (V4L2). This is "
                "the DeviceExplorer application's own panel. Pick a device for everything its backend "
                "reports; a printer is asked for its status and ink or toner. DeviceExplorer only looks: "
                "it never configures a device or prints.");
        // The tree and the property list scroll themselves: the page is the
        // area's height, not its content's.
        page->SetFillsArea(true);

        auto card = DemoKit::CreateCard("IODevCard");
        card->SetPadding(4);
        DemoKit::Add(page, card, 1);

        auto panel = std::make_shared<DeviceExplorer::DeviceExplorerPanel>(
                "IODevExplorer", DeviceExplorer::DeviceExplorerPanel::DescribeThisMachine(),
                DeviceExplorer::DeviceGrouping::Category, DEVICEEXPLORER_VERSION);
        DemoKit::Add(card, panel, 1);
        panel->Start();

        DemoKit::AddCaption(page, "IODevNote",
                "Source: Apps/DeviceExplorer/ui (the panel, the model and the printer query). "
                "Leaving the page stops the scan and the hot-plug watcher.");
        return page;
    }

} // namespace UltraCanvas
