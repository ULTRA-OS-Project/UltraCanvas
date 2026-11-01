// Apps/UltraDesktop/ui/UltraDesktopTasksWindow.h
// The Task Manager window: the windows other applications have open - each
// with its icon, the display it is on (and its virtual desktop where there
// are several), and Show, Close and Kill task - and the machine itself on a second tab as
// the framework's UltraCanvasHardwareInfoPanel shows it (CPU load and
// temperature, memory, storage, the network interfaces and the rest).
// Version: 0.3.0 - the display column, Show onto a display, Kill task
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasTimer.h"

#include "UltraCanvasWindow.h"

#include <cstdint>
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
    static std::shared_ptr<UltraCanvas::UltraCanvasContainer> BadgeCell(const std::string& id, int width,
                                                                       const std::string& text,
                                                                       const std::string& tooltip);
    static void ShowAndBringOnScreen(uint64_t id);
    // Asks first: the program is ended without being asked to close.
    void KillTask(uint64_t id, const std::string& title);

    bool open_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> windowRows_;
    std::shared_ptr<UltraCanvas::UltraCanvasHardwareInfoPanel> hardware_;
    UltraCanvas::TimerId timer_ = 0;
    int ticks_ = 0;
    std::string lastListing_;
    bool showDesktops_ = false;     // more than one virtual desktop: their column shows
};

} // namespace UltraDesktop
