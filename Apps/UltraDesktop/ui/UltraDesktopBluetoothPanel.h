// Apps/UltraDesktop/ui/UltraDesktopBluetoothPanel.h
// The Bluetooth panel: what a click on the info panel's Bluetooth icon
// opens. The radio on a switch (where the system lets a program switch it;
// on Windows a button opens its settings instead), the paired devices with
// Connect / Disconnect and Remove, and "Add device", which looks for devices
// in pairing mode for a minute and lists them with Pair - through
// UltraCanvasWireless (BlueZ on Linux, the Bluetooth API on Windows,
// IOBluetooth on macOS). Every call runs off the UI thread.
// While a device pairs, a prompt row shows the code to type on it, asks
// whether the code it shows matches, or takes its PIN - the panel is the
// process's pairing handler (UltraCanvasWireless::SetPairingHandler).
// Version: 0.2.0 - the pairing prompt
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

class UltraDesktopBluetoothPanel : public UltraDesktopPopupPanel {
public:
    // `onChanged` runs after a change, so the bar can refresh its icon.
    explicit UltraDesktopBluetoothPanel(std::function<void()> onChanged);
    ~UltraDesktopBluetoothPanel() override;

protected:
    void Build(UltraCanvas::UltraCanvasWindow& window) override;
    void Unbuild() override;

private:
    void Refresh();
    void Apply(const UltraCanvas::WirelessBluetoothStatus& status);
    void RebuildList();
    void AddDeviceRow(const UltraCanvas::WirelessBluetoothDevice& device, int index);
    void AddSectionLabel(const std::string& id, const std::string& text);
    void Act(const std::string& busyText, std::function<std::string()> action);
    void SetMessage(const std::string& text, bool error);
    void Later(std::function<void()> task);
    void RestartTimer(unsigned intervalMs);

    // The pairing prompt. HandlePairing runs on a backend thread; the rest
    // on the UI thread.
    struct PairingPrompt;
    // Static: a call may still be under way when the panel goes, so it
    // holds only the shared state, and reaches the panel on the UI thread
    // after that state says the panel is alive.
    static UltraCanvas::WirelessPairingAnswer HandlePairing(const std::shared_ptr<PairingPrompt>& prompt,
                                                            UltraDesktopBluetoothPanel* panel,
                                                            const UltraCanvas::WirelessPairingRequest& request);
    void ShowPrompt(const UltraCanvas::WirelessPairingRequest& request);
    void HidePrompt();
    void AnswerPrompt(bool accept);

    std::function<void()> onChanged_;
    UltraDesktopPanelWork work_;
    UltraCanvas::WirelessBluetoothStatus status_;
    bool haveStatus_ = false;
    bool busy_ = false;
    bool reading_ = false;
    bool loading_ = false;
    std::string listSignature_;
    UltraCanvas::TimerId timer_ = 0;
    unsigned timerInterval_ = 0;

    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> power_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> state_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> message_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> discover_;

    std::shared_ptr<PairingPrompt> pairing_;
    UltraCanvas::WirelessPairingStep promptStep_ = UltraCanvas::WirelessPairingStep::Finished;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> promptRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> promptText_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> promptCode_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> promptInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> promptYes_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> promptNo_;
};

} // namespace UltraDesktop
