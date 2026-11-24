// Apps/UltraSnip/ui/UltraSnipRecordingBar.cpp
// See the header.
// Version: 0.2.1 - the bar is as wide as its content at its widest in the language shown
// Version: 0.2.0 - the texts go through Tr (UltraLocalisation)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipRecordingBar.h"

#include "UltraSnipCapture.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace UltraCanvas;

namespace UltraSnip {

namespace {
constexpr int kBarWidth = 280;
constexpr int kBarHeight = 44;
constexpr int kGap = 12;
constexpr int kPauseWidth = 76;
constexpr int kStopWidth = 64;
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
    config.title = Tr("Recording");
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
    window_->SetBackgroundColor(kBarBackground);

    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    window->SetPadding(6, 12, 6, 12);

    mark_ = CreateLabel("snip.bar.mark", "\xE2\x97\x8F");       // ●
    mark_->SetFontSize(16);
    mark_->SetTextColor(kRecording);
    mark_->SetTooltip(Tr("Recording"));
    mark_->layoutItem.SetFlexShrink(0);
    window->AddChild(mark_);

    time_ = CreateLabel("snip.bar.time", "0:00");
    time_->SetFontSize(15);
    time_->SetTextColor(kText);
    time_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    window->AddChild(time_);

    pause_ = CreateButton("snip.bar.pause", 0, 0, kPauseWidth, 30, Tr("Pause"));
    pause_->SetTooltip(Tr("Pause the recording; Resume goes on where it stopped"));
    pause_->onClick = [this]() {
        if (paused_) { if (onResume) onResume(); }
        else if (onPause) onPause();
    };
    window->AddChild(pause_);

    stop_ = CreateButton("snip.bar.stop", 0, 0, kStopWidth, 30, Tr("Stop"));
    stop_->SetTooltip(Tr("Finish the recording"));
    stop_->onClick = [this]() { if (onStop) onStop(); };
    window->AddChild(stop_);

    // As wide as the bar is at its widest in this language - an hour's time
    // with "paused", the longer of Pause and Resume - and placed with that
    // width: at 280 px the time lost "paused" even in English, and German
    // kept only "1:02:...".
    const int barWidth = BarWidth();
    window_->SetWindowSize(barWidth, kBarHeight);
    const int physW = window_->LogicalToPhysical(barWidth);
    const int physH = window_->LogicalToPhysical(kBarHeight);
    const int physGap = window_->LogicalToPhysical(kGap);
    const Rect2Di at = PlaceBarOutside(area, screen, physW, physH, physGap);
    window_->SetWindowPosition(at.x, at.y);

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

int UltraSnipRecordingBar::BarWidth() const {
    IRenderContext* rc = window_ ? window_->GetRenderContext() : nullptr;
    if (!rc || !mark_ || !time_ || !pause_ || !stop_) return kBarWidth;
    // A button's width for the longest of its texts, padding and border
    // included, and never less than it was made with.
    auto buttonWidth = [rc](UltraCanvasButton& button, int least, std::initializer_list<std::string> texts) {
        const ButtonStyle& style = button.GetStyle();
        rc->PushState();
        rc->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
        rc->SetFontSize(style.fontSize);
        int text = 0;
        for (const std::string& t : texts) text = std::max(text, rc->GetTextLineWidth(t));
        rc->PopState();
        const float needed = text + button.GetTotalPaddingHorizontal() + button.GetTotalBorderHorizontal() + 1.0f;
        return std::max(least, static_cast<int>(std::ceil(needed)));
    };
    const std::string shown = time_->GetText();
    time_->SetText(FormatElapsed(9 * 3600 + 59 * 60 + 59) + "  " + Tr("paused"));
    const float timeWidth = time_->GetNaturalTextWidth() + time_->GetTotalPaddingHorizontal();
    time_->SetText(shown);
    const float markWidth = mark_->GetNaturalTextWidth() + mark_->GetTotalPaddingHorizontal();
    const float padding = window_->GetTotalPaddingHorizontal();
    const float gaps = 3 * 8.0f;
    const float needed = padding + gaps + markWidth + timeWidth +
                         buttonWidth(*pause_, kPauseWidth, {Tr("Pause"), Tr("Resume")}) +
                         buttonWidth(*stop_, kStopWidth, {Tr("Stop")});
    return std::max(kBarWidth, static_cast<int>(std::ceil(needed)) + 2);
}

void UltraSnipRecordingBar::SetElapsed(double seconds, bool paused) {
    if (!window_) return;
    if (time_) time_->SetText(FormatElapsed(seconds) + (paused ? "  " + Tr("paused") : std::string()));
    if (paused != paused_) {
        paused_ = paused;
        if (pause_) pause_->SetText(paused ? Tr("Resume") : Tr("Pause"));
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
