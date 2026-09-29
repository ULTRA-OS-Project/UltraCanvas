// Apps/UltraDesktop/ui/UltraDesktopAppStarter.h
// The Apps window: every installed application as a tile (icon over name),
// a filter box above them, one click to start. The list comes from
// UltraCanvasDesktopShell::ListApplications, which reads the freedesktop
// entries the machine has - the same entries the application menu of any
// other desktop shows - so an application installed by its package appears
// here without being told about the ULTRA OS desktop.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDesktopEntry.h"

#include "UltraCanvasWindow.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasTextInput;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

class UltraDesktopAppStarter {
public:
    UltraDesktopAppStarter();
    ~UltraDesktopAppStarter();

    void Open();
    void Raise();
    bool IsOpen() const { return open_; }

private:
    void RebuildTiles();
    void Launch(const UltraCanvas::UCDesktopEntry& entry);

    bool open_ = false;
    std::string filter_;
    std::vector<UltraCanvas::UCDesktopEntry> apps_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> tiles_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
};

} // namespace UltraDesktop
