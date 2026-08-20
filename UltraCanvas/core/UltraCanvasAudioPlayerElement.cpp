// core/UltraCanvasAudioPlayerElement.cpp
// Composite UI control wrapping UltraCanvasAudioPlayer, built from child widgets
// (icon buttons, sliders, label) arranged by a flex row layout.
// Version: 0.3.1
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework

#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cstdio>

namespace UltraCanvas {

namespace {
    constexpr int kPadding = 8;
    constexpr int kGap = 8;
    constexpr int kTimeWidth = 90;     // "00:00 / 00:00"
    constexpr int kVolumeWidth = 90;
    constexpr int kVolumeMinWidth = 48; // volume bar may shrink down to this
    constexpr int kMinSeekWidth = 60;  // below this a seek bar isn't usable
    constexpr int kCtrlHeight = 20;    // height of sliders/label inside the row
    constexpr int kIconSize = 18;      // transport icon size inside the buttons
}

UltraCanvasAudioPlayerElement::UltraCanvasAudioPlayerElement(
        const std::string& identifier, long x, long y, long w, long h)
    : UltraCanvasContainer(identifier, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(w), static_cast<float>(h)) {
    player = std::make_shared<UltraCanvasAudioPlayer>();

    // Fixed-height bar: never show scrollbars.
    ContainerStyle cs;
    cs.autoShowScrollbars = false;
    cs.forceShowVerticalScrollbar = false;
    cs.forceShowHorizontalScrollbar = false;
    SetContainerStyle(cs);

    // Horizontal flex row with all children vertically centred — this is what
    // makes the time label and controls line up without hand-tuned offsets.
    SetPadding(static_cast<float>(kPadding));
    layout.SetFlexRow().SetFlexGap(static_cast<float>(kGap))
          .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    BuildChildren();
    HookPlayerCallbacks();
    ApplyStyleToChildren();
    UpdatePlayIcon();
    UpdateMuteIcon();
    SyncTimeAndSeek();
}

UltraCanvasAudioPlayerElement::~UltraCanvasAudioPlayerElement() {
    uiAlive->store(false);   // cancel queued cross-thread UI tasks
    // The player can outlive this element via GetPlayer() — disconnect the
    // callbacks that capture `this`.
    player->onPlaybackStateChanged = nullptr;
    player->onPositionChanged = nullptr;
    player->onEnded = nullptr;
    StopPosTimer();
}

void UltraCanvasAudioPlayerElement::BuildChildren() {
    const int btn = style.buttonSize;
    auto fixed = [](const std::shared_ptr<UltraCanvasUIElement>& e) {
        e->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                     .SetAlignSelf(CSSLayout::AlignSelf::Center);
    };

    // ----- Play / Pause -----
    playButton = MakeAudioIconButton(GetIdentifier() + ".Play", btn, kIconSize,
                                     "play.svg", style.iconColor);
    playButton->onClick = [this]() { TogglePlayPause(); };
    fixed(playButton);
    AddChild(playButton);

    // ----- Stop -----
    stopButton = MakeAudioIconButton(GetIdentifier() + ".Stop", btn, kIconSize,
                                     "circle-stop.svg", style.iconColor);
    stopButton->onClick = [this]() { Stop(); };
    fixed(stopButton);
    AddChild(stopButton);

    // ----- Seek bar (absorbs the slack) -----
    seekSlider = std::make_shared<UltraCanvasSlider>(GetIdentifier() + ".Seek", 0, 0, 0, kCtrlHeight - 4);
    seekSlider->SetSliderStyle(SliderStyle::Horizontal);
    seekSlider->SetRange(0.0f, 1.0f);
    seekSlider->SetStep(0.0f);            // continuous (default step 1.0 would snap to ends)
    seekSlider->SetValue(0.0f);
    seekSlider->SetTrackHeight(static_cast<float>(style.sliderHeight));
    seekSlider->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetFlexBasis(CSSLayout::Dimension::Px(0))
                          .SetAlignSelf(CSSLayout::AlignSelf::Center);
    {
        auto cb = [this](float v) {
            if (suppressSeekCallback) return;
            double dur = player->GetDuration();
            if (dur > 0.0) Seek(v * dur);
        };
        seekSlider->onValueChanging = cb;
        seekSlider->onValueChanged  = cb;
    }
    AddChild(seekSlider);

    // ----- Time label -----
    timeLabel = std::make_shared<UltraCanvasLabel>(GetIdentifier() + ".Time",
                                                   0, 0, kTimeWidth, kCtrlHeight, "0:00 / 0:00");
    timeLabel->SetFontSize(11);
    timeLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    fixed(timeLabel);
    AddChild(timeLabel);

    // ----- Mute toggle -----
    muteButton = MakeAudioIconButton(GetIdentifier() + ".Mute", btn, kIconSize,
                                     "volume-2.svg", style.iconColor);
    muteButton->onClick = [this]() {
        player->SetMute(!player->IsMuted());
        UpdateMuteIcon();
        RequestRedraw();
    };
    fixed(muteButton);
    AddChild(muteButton);

    // ----- Volume bar -----
    volumeSlider = std::make_shared<UltraCanvasSlider>(GetIdentifier() + ".Vol",
                                                       0, 0, kVolumeWidth, kCtrlHeight - 4);
    volumeSlider->SetSliderStyle(SliderStyle::Horizontal);
    volumeSlider->SetRange(0.0f, 1.0f);
    volumeSlider->SetStep(0.0f);
    volumeSlider->SetValue(player->GetVolume());
    volumeSlider->SetTrackHeight(static_cast<float>(style.sliderHeight));
    {
        auto cb = [this](float v) {
            player->SetVolume(v);
            if (player->IsMuted() && v > 0.0f) { player->SetMute(false); UpdateMuteIcon(); }
        };
        volumeSlider->onValueChanging = cb;
        volumeSlider->onValueChanged  = cb;
    }
    // Unlike the other fixed children the volume bar may give up width (down
    // to kVolumeMinWidth) when the row runs out of space, instead of pushing
    // itself past the right edge; ApplyResponsiveVisibility() hides it
    // entirely when even the minimum doesn't fit.
    volumeSlider->layoutItem.SetFlexGrow(0).SetFlexShrink(1)
                            .SetFlexBasis(CSSLayout::Dimension::Px(kVolumeWidth))
                            .SetAlignSelf(CSSLayout::AlignSelf::Center);
    CSSLayout::BoxConstraints volumeLimits;
    volumeLimits.minWidth = CSSLayout::Dimension::Px(kVolumeMinWidth);
    volumeSlider->boxConstraints = volumeLimits;
    AddChild(volumeSlider);
}

void UltraCanvasAudioPlayerElement::ApplyStyleToChildren() {
    // Visibility from style toggles. SetVisible(false) drops the child from flex
    // flow, so the seek slider re-absorbs the freed space automatically.
    if (stopButton)   stopButton->SetVisible(!style.compact);
    if (timeLabel)    timeLabel->SetVisible(style.showTimeLabels);
    const bool showVol = style.showVolumeSlider && !style.compact;
    if (muteButton)   muteButton->SetVisible(showVol);
    if (volumeSlider) volumeSlider->SetVisible(showVol);

    // Colors from style. The icon buttons tint via their (masked) text colour.
    if (playButton)   playButton->SetTextColors(style.iconColor, style.iconColor);
    if (stopButton)   stopButton->SetTextColors(style.iconColor, style.iconColor);
    if (muteButton)   muteButton->SetTextColors(style.iconColor, style.iconColor);
    if (timeLabel)    timeLabel->SetTextColor(style.textColor);
    if (seekSlider)   seekSlider->SetColors(style.progressTrackColor,
                                            style.progressFillColor, style.knobColor);
    if (volumeSlider) volumeSlider->SetColors(style.progressTrackColor,
                                              style.progressFillColor, style.knobColor);
    ApplyResponsiveVisibility();   // re-apply the width limits on top of style
    InvalidateLayout();
}

bool UltraCanvasAudioPlayerElement::ComputeResponsiveWants(bool& wantVol,
                                                           bool& wantTime) const {
    if (!volumeSlider || !timeLabel) return false;
    const float w = GetWidth();
    if (w <= 0.0f) return false;   // not laid out yet

    const bool styleVol  = style.showVolumeSlider && !style.compact;
    const bool styleTime = style.showTimeLabels;

    // Width the always-present controls need beside a still-usable seek bar.
    float base = 2.0f * kPadding + style.buttonSize + kGap + kMinSeekWidth; // play + seek
    if (!style.compact) base += style.buttonSize + kGap;                    // stop
    if (styleVol)       base += style.buttonSize + kGap;                    // mute
    const float needTime   = base + kTimeWidth + kGap;
    // Volume appears only when it fits at full width beside a usable seek
    // bar (its shrink-to-kVolumeMinWidth remains as a safety net between
    // layout passes) — otherwise the seek bar would get shorter with a wider
    // row around the threshold.
    const float needVolume = needTime + kVolumeWidth + kGap;

    // Drop the volume slider first, then the time label. The mute button
    // stays visible so the sound can still be silenced from a narrow pane.
    wantVol  = styleVol  && w >= needVolume;
    wantTime = styleTime && w >= needTime;
    return true;
}

void UltraCanvasAudioPlayerElement::ApplyResponsiveVisibility() {
    bool wantVol = false, wantTime = false;
    if (!ComputeResponsiveWants(wantVol, wantTime)) return;
    bool changed = false;
    if (volumeSlider->IsVisible() != wantVol) { volumeSlider->SetVisible(wantVol); changed = true; }
    if (timeLabel->IsVisible() != wantTime)   { timeLabel->SetVisible(wantTime); changed = true; }
    // SetVisible invalidates layout but doesn't schedule a frame; without a
    // repaint request the new arrangement would only appear with the next
    // unrelated event.
    if (changed) RequestRedraw();
}

void UltraCanvasAudioPlayerElement::Arrange(const Rect2Df& finalRect,
                                            const CSSLayout::LayoutContext& ctx) {
    UltraCanvasContainer::Arrange(finalRect, ctx);
    // With every control at a fixed width, a narrow host (the UltraFiler
    // preview pane can go down to ~260px) used to overflow the flex row and
    // clip the volume slider at the right edge. Re-check what fits against
    // the arranged width. The visibility flip cannot happen inside this pass:
    // the unwinding Arrange recursion re-marks ancestors layout-valid, which
    // swallows the invalidation — queue it to run right after the pass.
    bool wantVol = false, wantTime = false;
    if (ComputeResponsiveWants(wantVol, wantTime) &&
        (volumeSlider->IsVisible() != wantVol || timeLabel->IsVisible() != wantTime)) {
        RunOnUIThread([this]() { ApplyResponsiveVisibility(); });
    }
}

void UltraCanvasAudioPlayerElement::UpdatePlayIcon() {
    if (!playButton) return;
    std::string icon = AudioIconPath(player->IsPlaying() ? "pause.svg" : "play.svg");
    if (icon == appliedPlayIcon) return;
    appliedPlayIcon = icon;
    playButton->SetIcon(icon);
}

void UltraCanvasAudioPlayerElement::UpdateMuteIcon() {
    if (!muteButton) return;
    std::string icon = AudioIconPath(player->IsMuted() ? "volume-x.svg" : "volume-2.svg");
    if (icon == appliedMuteIcon) return;
    appliedMuteIcon = icon;
    muteButton->SetIcon(icon);
}

void UltraCanvasAudioPlayerElement::SyncTimeAndSeek() {
    double pos = player->GetPosition();
    double dur = player->GetDuration();
    if (timeLabel) timeLabel->SetText(FormatTime(pos) + " / " + FormatTime(dur));
    // Don't fight the user's drag, and suppress the re-entrant Seek that SetValue
    // would otherwise trigger via onValueChanged.
    if (seekSlider && !seekSlider->IsDragging()) {
        suppressSeekCallback = true;
        seekSlider->SetValue(dur > 0.0 ? static_cast<float>(pos / dur) : 0.0f);
        suppressSeekCallback = false;
    }
    // Runs on every position tick (UI thread), so the transport icons heal
    // even if a state-change notification was lost; the applied-icon guard
    // makes this free when nothing changed.
    UpdatePlayIcon();
    UpdateMuteIcon();
}

void UltraCanvasAudioPlayerElement::StartPosTimer() {
    if (posTimerId != InvalidTimerId) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    posTimerId = app->StartTimer(200, true, [this](TimerId) { SyncTimeAndSeek(); });
}

void UltraCanvasAudioPlayerElement::StopPosTimer() {
    if (posTimerId == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(posTimerId);
    posTimerId = InvalidTimerId;
}

void UltraCanvasAudioPlayerElement::RunOnUIThread(std::function<void()> fn) {
    // Player callbacks arrive on the audio backend thread (position pumping,
    // end-of-stream). Child widgets, the layout caches and the window's
    // dirty-rect list are not thread-safe, so all UI work is queued to the
    // event loop; `uiAlive` cancels tasks that would land after this element
    // is destroyed. Without an application (headless use) run inline — there
    // is no event loop to race against.
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) { fn(); return; }
    app->PostToUIThread([alive = uiAlive, fn = std::move(fn)]() {
        if (alive->load()) fn();
    });
}

void UltraCanvasAudioPlayerElement::HookPlayerCallbacks() {
    player->onPlaybackStateChanged = [this](AudioPlaybackState) {
        RunOnUIThread([this]() {
            UpdatePlayIcon();
            if (player->IsPlaying()) StartPosTimer(); else StopPosTimer();
            SyncTimeAndSeek();
            RequestRedraw();
        });
    };
    player->onPositionChanged = [this](double) {
        RunOnUIThread([this]() { SyncTimeAndSeek(); });
    };
    player->onEnded = [this]() {
        RunOnUIThread([this]() {
            if (player->IsPlaying()) return;   // restarted before this task ran
            StopPosTimer();
            // Halt the output device: after end-of-stream it keeps running
            // (feeding silence) until a transport call stops it.
            player->Stop();
            UpdatePlayIcon();
            SyncTimeAndSeek();
            if (onEnded) onEnded();
            RequestRedraw();
        });
    };
}

bool UltraCanvasAudioPlayerElement::LoadFromFile(const std::string& filePath) {
    bool ok = player->LoadFromFile(filePath);
    if (volumeSlider) volumeSlider->SetValue(player->GetVolume());
    SyncTimeAndSeek();
    RequestRedraw();
    return ok;
}

bool UltraCanvasAudioPlayerElement::LoadFromAudio(std::shared_ptr<UCAudio> audio) {
    bool ok = player->LoadFromAudio(std::move(audio));
    if (volumeSlider) volumeSlider->SetValue(player->GetVolume());
    SyncTimeAndSeek();
    RequestRedraw();
    return ok;
}

void UltraCanvasAudioPlayerElement::ShowOpenDialog() {
    FileDialogOptions opts;
    opts.SetTitle("Open Audio File")
        .AddFilter("Audio files",
                   std::vector<std::string>{"wav", "mp3", "flac", "ogg", "oga"})
        .AddFilter("Wave audio (*.wav)", "wav")
        .AddFilter("MP3 audio (*.mp3)", "mp3")
        .AddFilter("FLAC audio (*.flac)", "flac")
        .AddFilter("Ogg Vorbis (*.ogg)", "ogg")
        .AddFilter("All files (*.*)", "*")
        .SetParentWindow(GetWindow());

    auto self = this;
    UltraCanvasFileLoader::OpenFileDialog(opts,
        [self](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) {
                if (self->LoadFromFile(path)) {
                    if (self->onFileOpened) self->onFileOpened(path);
                }
            } else {
                if (self->onOpenCancelled) self->onOpenCancelled();
            }
        });
}

void UltraCanvasAudioPlayerElement::Play()   { player->Play();   if (onPlay)  onPlay(); }
void UltraCanvasAudioPlayerElement::Pause()  { player->Pause();  if (onPause) onPause(); }
void UltraCanvasAudioPlayerElement::Stop()   { player->Stop();   if (onStop)  onStop(); }
void UltraCanvasAudioPlayerElement::Seek(double s) { player->Seek(s); if (onSeek) onSeek(s); }

void UltraCanvasAudioPlayerElement::TogglePlayPause() {
    if (player->IsPlaying()) Pause(); else Play();
}

void UltraCanvasAudioPlayerElement::SetStyle(const AudioPlayerStyle& s) {
    style = s;
    ApplyStyleToChildren();
    RequestRedraw();
}

void UltraCanvasAudioPlayerElement::SetTrackTitle(const std::string& title) {
    trackTitle = title;
    RequestRedraw();
}

std::string UltraCanvasAudioPlayerElement::FormatTime(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return std::string(buf);
}

void UltraCanvasAudioPlayerElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;
    Rect2Df b = GetLocalBounds();

    // Background panel (rounded, with border) drawn before the children.
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height),
                             style.backgroundColor, 1.0f,
                             style.borderColor, style.cornerRadius);

    // Container renders the child widgets (with clipping + translation).
    UltraCanvasContainer::Render(ctx, dirtyRect);
}

} // namespace UltraCanvas
