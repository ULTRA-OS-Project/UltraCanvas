// Apps/UltraDesktop/ui/UltraDesktopTasksWindow.h
// The Task Manager window: the windows other applications have open - each
// with its icon, the display it is on (and its virtual desktop where there
// are several), and Show, Close and Kill task - and the machine itself on a second tab as
// the framework's UltraCanvasHardwareInfoPanel shows it (CPU load and
// temperature, memory, storage, the network interfaces and the rest).
// Version: 0.6.0 - the colours from the theme in force, again when it changes (ApplyLook)
// Version: 0.5.0 - the desktop's own windows left out; Windows' display numbers
// Version: 0.4.0 - the state as an icon and a word
// Version: 0.3.0 - the display column, Show onto a display, Kill task
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasTimer.h"

#include "UltraCanvasWindow.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasHardwareInfoPanel;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

class UltraDesktopTasksWindow {
public:
    UltraDesktopTasksWindow();
    ~UltraDesktopTasksWindow();

    void Open();
    void Raise();
    bool IsOpen() const { return open_; }
    // A window of another program: not the desktop's own (its window, its
    // dialogs, panels and Sticker board), and not one that keeps out of
    // taskbars. The Task Manager lists these; a desktop icon dragged over
    // one leaves the desktop as a file drag.
    static bool Listed(const UltraCanvas::DesktopWindowInfo& w);

private:
    void RebuildWindowList();
    // The theme changed: the header in the new colours, the rows made again.
    void ApplyLook();
    void OnTimer();
    static std::shared_ptr<UltraCanvas::UltraCanvasContainer> BadgeCell(const std::string& id, int width,
                                                                       const std::string& text,
                                                                       const std::string& tooltip);
    static void ShowAndBringOnScreen(uint64_t id);
    // Asks first: the program is ended without being asked to close.
    void KillTask(uint64_t id, const std::string& title);

    bool open_ = false;
    int themeHandler_ = 0;   // UltraCanvasTheme::AddChangeHandler's id, removed in the destructor
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> header_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasLabel>> headerLabels_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> windowRows_;
    std::shared_ptr<UltraCanvas::UltraCanvasHardwareInfoPanel> hardware_;
    UltraCanvas::TimerId timer_ = 0;
    int ticks_ = 0;
    std::string lastListing_;
    bool showDesktops_ = false;
    std::string iconsDir_;          // media/icons/desktop/: the state icons     // more than one virtual desktop: their column shows
};

} // namespace UltraDesktop
