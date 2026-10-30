// Apps/UltraDesktop/ui/UltraDesktopAppStarter.h
// The Apps window: every installed application as a tile (icon over name),
// a filter box above them, one click to start. The list comes from
// UltraCanvasDesktopShell::ListApplications, which reads the freedesktop
// entries the machine has - the same entries the application menu of any
// other desktop shows - so an application installed by its package appears
// here without being told about the ULTRA OS desktop. A right click on a
// tile pins the application to the taskbar, or unpins it.
// Version: 0.2.0 - pin to the taskbar
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDesktopEntry.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasMenu;
    class UltraCanvasTextInput;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

class UltraDesktopAppStarter {
public:
    // The taskbar's pins, which the tiles' right-click menu reads and sets.
    struct Pins {
        std::function<bool(const UltraCanvas::UCDesktopEntry&)> isPinned;
        std::function<void(const UltraCanvas::UCDesktopEntry&, bool pinned)> setPinned;
    };

    explicit UltraDesktopAppStarter(Pins pins = {});
    ~UltraDesktopAppStarter();

    void Open();
    void Raise();
    bool IsOpen() const { return open_; }

private:
    void RebuildTiles();
    void Launch(const UltraCanvas::UCDesktopEntry& entry);
    void ShowTileMenu(const UltraCanvas::UCDesktopEntry& entry, int windowX, int windowY);

    Pins pins_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    bool open_ = false;
    std::string filter_;
    std::vector<UltraCanvas::UCDesktopEntry> apps_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> tiles_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
};

} // namespace UltraDesktop
