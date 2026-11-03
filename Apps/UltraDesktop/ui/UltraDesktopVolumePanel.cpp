// Apps/UltraDesktop/ui/UltraDesktopVolumePanel.cpp
// The volume panel. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopVolumePanel.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasWindow.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {
constexpr int kPanelWidth = 300;
constexpr int kPanelHeight = 150;
const Color kText(242, 242, 242);
const Color kMuted(160, 160, 160);
} // namespace

UltraDesktopVolumePanel::UltraDesktopVolumePanel(std::function<void()> onChanged)
    : UltraDesktopPopupPanel("Loudspeaker", kPanelWidth, kPanelHeight), onChanged_(std::move(onChanged)) {}

UltraDesktopVolumePanel::~UltraDesktopVolumePanel() {
    if (writeTimer_) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(writeTimer_);
    }
}

void UltraDesktopVolumePanel::Build(UltraCanvasWindow& window) {
    window.layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window.SetPadding(12, 14, 12, 14);

    auto title = CreateLabel("vol.title", 0, 0, kPanelWidth - 28, 22, "Loudspeaker");
    title->SetFontSize(11.5f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(kText);
    title->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(title);

    device_ = CreateLabel("vol.device", 0, 0, kPanelWidth - 28, 18, "");
    device_->SetFontSize(9.5f);
    device_->SetTextColor(kMuted);
    device_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(device_);

    auto row = std::make_shared<UltraCanvasContainer>("vol.row");
    row->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0.0f);
    slider_ = CreateHorizontalSlider("vol.slider", 0, 0, 190, 28, 0.0f, 100.0f);
    slider_->SetStep(1.0f);
    // The percentage is said in the status line: the slider's own label
    // would sit over the device's name.
    slider_->SetValueDisplay(SliderValueDisplay::NoDisplay);
    slider_->layoutItem.SetFlexGrow(1.0f);
    slider_->onValueChanged = [this](float value) {
        if (loading_) return;
        ScheduleVolume(static_cast<int>(value + 0.5f));
    };
    row->AddChild(slider_);
    mute_ = std::make_shared<UltraCanvasSwitch>("vol.mute", 0, 0, 80, 28, "Mute");
    mute_->layoutItem.SetFlexShrink(0.0f);
    mute_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (loading_) return;
        std::string error;
        if (!UltraCanvasDesktopShell::SetAudioOutputMuted(now == CheckedState::Checked, &error)) {
            if (status_) status_->SetText(error);
            return;
        }
        if (status_) {
            status_->SetText(now == CheckedState::Checked
                                 ? "Muted"
                                 : std::to_string(static_cast<int>(slider_->GetValue() + 0.5f)) + "%");
        }
        if (onChanged_) onChanged_();
    };
    row->AddChild(mute_);
    window.AddChild(row);

    status_ = CreateLabel("vol.status", 0, 0, kPanelWidth - 28, 34, "");
    status_->SetFontSize(9.0f);
    status_->SetTextColor(kMuted);
    status_->layoutItem.SetFlexGrow(1.0f);
    window.AddChild(status_);

    Reload();
}

void UltraDesktopVolumePanel::Unbuild() {
    device_.reset();
    status_.reset();
    slider_.reset();
    mute_.reset();
}

void UltraDesktopVolumePanel::Reload() {
    const DesktopAudioOutput output = UltraCanvasDesktopShell::ReadAudioOutput();
    loading_ = true;
    if (output.available) {
        device_->SetText(output.device.empty() ? "Default output" : output.device);
        slider_->SetValue(static_cast<float>(std::clamp(output.volumePercent, 0, 100)));
        slider_->SetDisabled(false);
        mute_->SetChecked(output.muted);
        mute_->SetDisabled(false);
        status_->SetText(output.muted ? "Muted" : output.volumePercent > 100
                                                     ? std::to_string(output.volumePercent) + "% - above the normal range"
                                                     : std::to_string(output.volumePercent) + "%");
    } else {
        device_->SetText("No sound server");
        slider_->SetValue(0.0f);
        slider_->SetDisabled(true);
        mute_->SetChecked(false);
        mute_->SetDisabled(true);
        status_->SetText(output.error);
        debugOutput << "UltraDesktop: loudspeaker: " << output.error << std::endl;
    }
    loading_ = false;
}

void UltraDesktopVolumePanel::ScheduleVolume(int percent) {
    pendingPercent_ = percent;
    if (writeTimer_) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    writeTimer_ = app->StartTimer(60, false, [this](TimerId) {
        writeTimer_ = 0;
        WriteVolume();
    });
}

void UltraDesktopVolumePanel::WriteVolume() {
    if (pendingPercent_ < 0) return;
    const int percent = pendingPercent_;
    pendingPercent_ = -1;
    std::string error;
    if (!UltraCanvasDesktopShell::SetAudioOutputVolume(percent, &error)) {
        if (status_) status_->SetText(error);
        return;
    }
    if (status_ && !(mute_ && mute_->IsChecked())) status_->SetText(std::to_string(percent) + "%");
    if (onChanged_) onChanged_();
}

} // namespace UltraDesktop
