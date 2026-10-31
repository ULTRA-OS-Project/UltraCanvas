// Apps/UltraDesktop/ui/UltraDesktopWifiPanel.h
// The Wi-Fi panel: what a click on the info panel's Wi-Fi icon opens. The
// radio on a switch, the network joined, and the networks in reach - each
// with Connect (asking for the password when the network needs one and has
// none saved), Disconnect and Forget - through UltraCanvasWireless
// (NetworkManager on Linux, the Native Wifi API on Windows, CoreWLAN on
// macOS). Every call runs off the UI thread (UltraDesktopPanelWork); the
// list is read again every few seconds while the panel is open.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopPanelWork.h"
#include "UltraDesktopPopupPanel.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasWireless.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasButton;
    class UltraCanvasContainer;
    class UltraCanvasLabel;
    class UltraCanvasSwitch;
    class UltraCanvasTextInput;
}

namespace UltraDesktop {

class UltraDesktopWifiPanel : public UltraDesktopPopupPanel {
public:
    // `onChanged` runs after the panel joined or left a network, so the bar
    // can refresh its icon; `openMonitor` opens the traffic monitor.
    UltraDesktopWifiPanel(std::function<void()> onChanged, std::function<void()> openMonitor);
    ~UltraDesktopWifiPanel() override;

protected:
    void Build(UltraCanvas::UltraCanvasWindow& window) override;
    void Unbuild() override;

private:
    // Read the state again; with `scan`, ask the adapter to scan first.
    void Refresh(bool scan);
    void Apply(const UltraCanvas::WirelessWifiStatus& status);
    void RebuildList();
    // A change: `busyText` while it runs, then its error (or nothing) and a
    // fresh read.
    void Act(const std::string& busyText, std::function<std::string()> action);
    void Connect(const UltraCanvas::WirelessNetwork& network);
    void AskPassword(const std::string& ssid);
    void HidePassword();
    void SubmitPassword();
    void SetMessage(const std::string& text, bool error);
    // After the click that asked for it has finished: a row's own button
    // must not be destroyed while its handler runs.
    void Later(std::function<void()> task);

    std::function<void()> onChanged_;
    std::function<void()> openMonitor_;
    UltraDesktopPanelWork work_;
    UltraCanvas::WirelessWifiStatus status_;
    bool haveStatus_ = false;
    bool busy_ = false;
    bool reading_ = false;
    bool loading_ = false;
    std::string passwordFor_;
    std::string listSignature_;
    UltraCanvas::TimerId timer_ = 0;

    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> power_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> state_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> passwordRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> passwordLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> password_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> message_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> scan_;
};

} // namespace UltraDesktop
