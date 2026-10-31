// Apps/UltraDesktop/ui/UltraDesktopDevicePanel.h
// The small panel the info panel's device icons open - USB, keyboard,
// webcam, microphone, battery: what is attached or in use, as a list of
// entries read off the UI thread (a USB walk can take a moment), and a row
// of buttons to the application that goes further (DeviceExplorer, the Task
// Manager). What to read and which buttons to show is the caller's
// (UltraDesktopWindow::OpenDevicePanel); this class only shows it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopPanelWork.h"
#include "UltraDesktopPopupPanel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

struct DevicePanelEntry {
    std::string title;          // "Logitech USB Receiver"
    std::string detail;         // "Human Interface Device · 12 Mbit/s"
    bool highlighted = false;   // in use, connected, the default
};

struct DevicePanelContent {
    std::string summary;                // the line under the title
    std::vector<DevicePanelEntry> entries;
    std::string empty;                  // shown when there are no entries
};

struct DevicePanelAction {
    std::string label;
    std::string tooltip;
    std::function<void()> run;          // the panel closes after it
};

class UltraDesktopDevicePanel : public UltraDesktopPopupPanel {
public:
    // `read` runs on a worker thread every time the panel opens, and must
    // capture what it needs by value.
    UltraDesktopDevicePanel(std::string title, std::function<DevicePanelContent()> read,
                            std::vector<DevicePanelAction> actions);

protected:
    void Build(UltraCanvas::UltraCanvasWindow& window) override;
    void Unbuild() override;

private:
    void Apply(const DevicePanelContent& content);

    std::string title_;
    std::function<DevicePanelContent()> read_;
    std::vector<DevicePanelAction> actions_;
    UltraDesktopPanelWork work_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> summary_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> list_;
};

} // namespace UltraDesktop
