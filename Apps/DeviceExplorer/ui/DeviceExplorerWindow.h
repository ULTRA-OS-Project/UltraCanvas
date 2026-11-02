// Apps/DeviceExplorer/ui/DeviceExplorerWindow.h
// DeviceExplorer's window: one DeviceExplorerPanel filling it. The panel is
// the whole user interface (the UltraCanvas demo's IODeviceManager page shows
// the same panel); the window gives it a title, a size and a place on screen.
// Version: 0.3.0 - hosts DeviceExplorerPanel, which holds what this was
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DeviceExplorerPanel.h"

#include "UltraCanvasWindow.h"

#include <memory>

namespace DeviceExplorer {

class DeviceExplorerWindow {
public:
    // `machine` describes the computer node; `grouping` is the initial
    // choice (--group on the command line).
    bool Initialize(const MachineSummary& machine, DeviceGrouping grouping);
    void Show();

private:
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<DeviceExplorerPanel> panel_;
};

} // namespace DeviceExplorer
