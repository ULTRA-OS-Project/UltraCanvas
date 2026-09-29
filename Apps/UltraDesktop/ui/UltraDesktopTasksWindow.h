// Apps/UltraDesktop/ui/UltraDesktopTasksWindow.h
// The Task Manager window: the windows other applications have open, with
// Activate and Close for each, and the machine itself on a second tab as
// the framework's UltraCanvasHardwareInfoPanel shows it (CPU load and
// temperature, memory, storage, the network interfaces and the rest).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasTimer.h"

#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasHardwareInfoPanel;
}

namespace UltraDesktop {

class UltraDesktopTasksWindow {
public:
    UltraDesktopTasksWindow();
    ~UltraDesktopTasksWindow();

    void Open();
    void Raise();
    bool IsOpen() const { return open_; }

private:
    void RebuildWindowList();
    void OnTimer();

    bool open_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> windowRows_;
    std::shared_ptr<UltraCanvas::UltraCanvasHardwareInfoPanel> hardware_;
    UltraCanvas::TimerId timer_ = 0;
    int ticks_ = 0;
    std::string lastListing_;
};

} // namespace UltraDesktop
