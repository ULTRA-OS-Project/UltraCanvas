// Apps/UltraDesktop/ui/UltraDesktopSettingsWindow.h
// ULTRA OS settings, the desktop's page of them: the taskbar's edge, the
// wallpaper, the RAM disc the pinned button opens, the file manager, and how
// many virtual desktops the organiser offers. Apply writes the settings
// file and rebuilds the desktop.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasDropdown;
    class UltraCanvasTextInput;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

class UltraDesktopWindow;

class UltraDesktopSettingsWindow {
public:
    explicit UltraDesktopSettingsWindow(UltraDesktopWindow* desktop);
    ~UltraDesktopSettingsWindow();

    void Open();
    void Raise();
    bool IsOpen() const { return open_; }

private:
    void Apply();
    void BrowseWallpaper();

    UltraDesktopWindow* desktop_;
    bool open_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> edge_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> wallpaper_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> ramDisc_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filer_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> desktops_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
};

} // namespace UltraDesktop
