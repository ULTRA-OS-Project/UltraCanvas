// Apps/UltraDesktop/ui/UltraDesktopVolumePanel.h
// The volume panel: what a click on the info panel's loudspeaker opens - the
// default output's name, its volume on a slider and its mute on a switch,
// read from and written to the sound server through
// UltraCanvasDesktopShell (PulseAudio or PipeWire on Linux, Core Audio on
// Windows). Where no sound server answers the panel says so instead.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopPopupPanel.h"
#include "UltraCanvasTimer.h"

#include <functional>
#include <memory>

namespace UltraCanvas {
    class UltraCanvasLabel;
    class UltraCanvasSlider;
    class UltraCanvasSwitch;
}

namespace UltraDesktop {

class UltraDesktopVolumePanel : public UltraDesktopPopupPanel {
public:
    // `onChanged` runs after a change was written, so the bar can refresh
    // what it shows about the speaker.
    explicit UltraDesktopVolumePanel(std::function<void()> onChanged = nullptr);
    ~UltraDesktopVolumePanel() override;

protected:
    void Build(UltraCanvas::UltraCanvasWindow& window) override;
    void Unbuild() override;

private:
    void Reload();
    // The slider moves many times a second while dragged; the server is
    // told the last value a moment after the hand stops.
    void ScheduleVolume(int percent);
    void WriteVolume();

    std::function<void()> onChanged_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> device_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> slider_;
    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> mute_;
    UltraCanvas::TimerId writeTimer_ = 0;
    int pendingPercent_ = -1;
    bool loading_ = false;
};

} // namespace UltraDesktop
