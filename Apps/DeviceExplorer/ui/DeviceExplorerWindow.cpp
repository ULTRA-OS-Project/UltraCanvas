// Apps/DeviceExplorer/ui/DeviceExplorerWindow.cpp
// Version: 0.3.0 - hosts DeviceExplorerPanel
// Author: UltraCanvas Framework / ULTRA OS
#include "DeviceExplorerWindow.h"

// DEVICEEXPLORER_VERSION comes from the build alone: CMake reads the first
// line of Docs/DeviceExplorer/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build that
// lost it fails instead of reporting a wrong number.
#ifndef DEVICEEXPLORER_VERSION
#error "DEVICEEXPLORER_VERSION is not defined: build through CMake, which reads it from Docs/DeviceExplorer/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace DeviceExplorer {

bool DeviceExplorerWindow::Initialize(const MachineSummary& machine, DeviceGrouping grouping) {
    WindowConfig config;
    config.title  = std::string("DeviceExplorer ") + DEVICEEXPLORER_VERSION;
    config.width  = 1100;
    config.height = 700;
    config.minWidth  = 760;
    config.minHeight = 460;
    window_ = CreateWindow(config);
    if (!window_) return false;

    // The panel fills the window: the window is a one-item column.
    window_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    panel_ = std::make_shared<DeviceExplorerPanel>("dePanel", machine, grouping, DEVICEEXPLORER_VERSION);
    panel_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    panel_->layoutItem.SetFlexGrow(1);
    panel_->layoutItem.SetFlexShrink(1);
    window_->AddChild(panel_);
    panel_->Start();
    return true;
}

void DeviceExplorerWindow::Show() {
    if (window_) window_->Show();
}

} // namespace DeviceExplorer
