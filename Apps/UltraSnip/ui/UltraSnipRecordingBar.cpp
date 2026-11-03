// Apps/UltraSnip/ui/UltraSnipRecordingBar.cpp
// See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipRecordingBar.h"

#include "UltraSnipCapture.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

using namespace UltraCanvas;

namespace UltraSnip {

namespace {
constexpr int kBarWidth = 280;
constexpr int kBarHeight = 44;
constexpr int kGap = 12;
const Color kBarBackground(28, 28, 30, 255);
const Color kRecording(230, 60, 60, 255);
const Color kPausedMark(180, 180, 180, 255);
const Color kText(245, 245, 245, 255);
} // namespace

UltraSnipRecordingBar::UltraSnipRecordingBar() = default;

UltraSnipRecordingBar::~UltraSnipRecordingBar() { Close(); }

bool UltraSnipRecordingBar::Open(const Rect2Di& area, const Rect2Di& screen) {
    Close();
    WindowConfig config;
    config.title = "Recording";
    config.type = WindowType::Notification;
    config.width = kBarWidth;
    config.height = kBarHeight;
    config.resizable = false;
    config.minimizable = false;
    config.maximizable = false;
    config.minWidth = kBarWidth;
    config.minHeight = kBarHeight;
    config.backgroundColor = kBarBackground;
    config.alwaysOnTop = true;
    window_ = CreateWindow(config);
    if (!window_) return false;
    const int physW = window_->LogicalToPhysical(kBarWidth);
    const int physH = window_->LogicalToPhysical(kBarHeight);
    const int physGap = window_->LogicalToPhysical(kGap);
    const Rect2Di at = PlaceBarOutside(area, screen, physW, physH, physGap);
    window_->SetWindowPosition(at.x, at.y);
    window_->SetBackgroundColor(kBarBackground);

    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    window->SetPadding(6, 12, 6, 12);

    mark_ = CreateLabel("snip.bar.mark", "\xE2\x97\x8F");       // ●
    mark_->SetFontSize(16);
    mark_->SetTextColor(kRecording);
    mark_->SetTooltip("Recording");
    window->AddChild(mark_);

    time_ = CreateLabel("snip.bar.time", "0:00");
    time_->SetFontSize(15);
    time_->SetTextColor(kText);
    time_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    window->AddChild(time_);

    pause_ = CreateButton("snip.bar.pause", 0, 0, 76, 30, "Pause");
    pause_->SetTooltip("Pause the recording; Resume goes on where it stopped");
    pause_->onClick = [this]() {
        if (paused_) { if (onResume) onResume(); }
        else if (onPause) onPause();
    };
    window->AddChild(pause_);

    stop_ = CreateButton("snip.bar.stop", 0, 0, 64, 30, "Stop");
    stop_->SetTooltip("Finish the recording");
    stop_->onClick = [this]() { if (onStop) onStop(); };
    window->AddChild(stop_);

    paused_ = false;
    window_->onWindowClosed = [this]() {
        window_.reset();
        mark_.reset();
        time_.reset();
        pause_.reset();
        stop_.reset();
    };
    window_->Show();
    return true;
}

void UltraSnipRecordingBar::SetElapsed(double seconds, bool paused) {
    if (!window_) return;
    if (time_) time_->SetText(FormatElapsed(seconds) + (paused ? "  paused" : ""));
    if (paused != paused_) {
        paused_ = paused;
        if (pause_) pause_->SetText(paused ? "Resume" : "Pause");
        if (mark_) mark_->SetTextColor(paused ? kPausedMark : kRecording);
    }
}

void UltraSnipRecordingBar::Close() {
    if (!window_) return;
    window_->onWindowClosed = nullptr;
    window_->Close();
    window_.reset();
    mark_.reset();
    time_.reset();
    pause_.reset();
    stop_.reset();
}

} // namespace UltraSnip
