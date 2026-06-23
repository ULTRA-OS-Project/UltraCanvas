// core/UltraCanvasAudioPlayerElement.cpp
// Composite UI control wrapping UltraCanvasAudioPlayer with play/pause, seek, volume
// Version: 0.2.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <cstdio>

namespace UltraCanvas {

namespace {
    constexpr int kPadding = 8;
    constexpr int kGap = 8;
    constexpr int kTimeWidth = 90;     // "00:00 / 00:00"
    constexpr int kVolumeWidth = 90;
    constexpr int kVolumeKnobR = 5;
    constexpr int kSeekKnobR = 6;

    inline bool Hit(const Rect2Di& r, const Point2Di& p) {
        return p.x >= r.x && p.x < r.x + r.width &&
               p.y >= r.y && p.y < r.y + r.height;
    }

    // Resource path for the speaker/volume icon shipped with the framework.
    const std::string& VolumeIconPath() {
        static const std::string path =
            NormalizePath(GetResourcesDir() + "media/icons/volume.svg");
        return path;
    }
}

UltraCanvasAudioPlayerElement::UltraCanvasAudioPlayerElement(
        const std::string& identifier, long x, long y, long w, long h)
    : UltraCanvasUIElement(identifier, static_cast<int>(x), static_cast<int>(y),
                           static_cast<int>(w), static_cast<int>(h)) {
    player = std::make_shared<UltraCanvasAudioPlayer>();
    HookPlayerCallbacks();
    Relayout();
}

UltraCanvasAudioPlayerElement::~UltraCanvasAudioPlayerElement() = default;

bool UltraCanvasAudioPlayerElement::LoadFromFile(const std::string& filePath) {
    bool ok = player->LoadFromFile(filePath);
    RequestRedraw();
    return ok;
}

bool UltraCanvasAudioPlayerElement::LoadFromAudio(std::shared_ptr<UCAudio> audio) {
    bool ok = player->LoadFromAudio(std::move(audio));
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
    Relayout();
    RequestRedraw();
}

void UltraCanvasAudioPlayerElement::SetTrackTitle(const std::string& title) {
    trackTitle = title;
    RequestRedraw();
}

void UltraCanvasAudioPlayerElement::HookPlayerCallbacks() {
    auto self = this;
    player->onPlaybackStateChanged = [self](AudioPlaybackState) { self->RequestRedraw(); };
    player->onPositionChanged = [self](double) { self->RequestRedraw(); };
    player->onEnded = [self]() {
        if (self->onEnded) self->onEnded();
        self->RequestRedraw();
    };
}

void UltraCanvasAudioPlayerElement::Relayout() {
    Rect2Di b = GetLocalBounds();
    int btn = style.buttonSize;
    int cy = b.y + b.height / 2;

    int x = b.x + kPadding;

    // Play / Pause (always)
    playButtonRect = Rect2Di(x, cy - btn / 2, btn, btn);
    x += btn + kGap;

    // Stop (only when not compact)
    if (!style.compact) {
        stopButtonRect = Rect2Di(x, cy - btn / 2, btn, btn);
        x += btn + kGap;
    } else {
        stopButtonRect = Rect2Di();
    }

    int rightX = b.x + b.width - kPadding;

    // Volume bar on the right
    if (style.showVolumeSlider && !style.compact) {
        volumeBarRect = Rect2Di(rightX - kVolumeWidth,
                                cy - style.sliderHeight / 2,
                                kVolumeWidth, style.sliderHeight);
        muteButtonRect = Rect2Di(volumeBarRect.x - btn - kGap,
                                 cy - btn / 2, btn, btn);
        rightX = muteButtonRect.x - kGap;
    } else {
        volumeBarRect = Rect2Di();
        muteButtonRect = Rect2Di();
    }

    // Time labels
    if (style.showTimeLabels) {
        timeLabelRect = Rect2Di(rightX - kTimeWidth, cy - 8, kTimeWidth, 16);
        rightX = timeLabelRect.x - kGap;
    } else {
        timeLabelRect = Rect2Di();
    }

    // Seek bar fills remaining space
    int seekW = rightX - x;
    if (seekW < 20) seekW = 20;
    seekBarRect = Rect2Di(x, cy - style.sliderHeight / 2,
                          seekW, style.sliderHeight);
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

void UltraCanvasAudioPlayerElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    Rect2Di b = GetLocalBounds();
    Relayout();

    // Background panel
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height),
                             style.backgroundColor, 1.0f,
                             style.borderColor, style.cornerRadius);

    DrawTransportButtons(ctx);
    DrawSeekBar(ctx);
    if (style.showTimeLabels) DrawTimeLabels(ctx);
    if (style.showVolumeSlider && !style.compact) DrawVolumeBar(ctx);
}

void UltraCanvasAudioPlayerElement::DrawTransportButtons(IRenderContext* ctx) {
    // ----- Play / Pause -----
    Color btnColor = hoverPlay ? style.iconHoverColor : style.iconColor;
    ctx->DrawFilledRectangle(Rect2Dd(playButtonRect.x, playButtonRect.y,
                                     playButtonRect.width, playButtonRect.height),
                             Color(255, 255, 255, 0), 0.0f,
                             Color(0, 0, 0, 0), style.cornerRadius);
    int cx = playButtonRect.x + playButtonRect.width / 2;
    int cy = playButtonRect.y + playButtonRect.height / 2;
    int r = playButtonRect.width / 3;

    ctx->SetFillPaint(btnColor);
    if (player->IsPlaying()) {
        // Pause icon: two vertical bars
        int bw = r / 2;
        int gap = bw / 2;
        ctx->FillRectangle(Rect2Dd(cx - gap - bw, cy - r, bw, r * 2));
        ctx->FillRectangle(Rect2Dd(cx + gap,      cy - r, bw, r * 2));
    } else {
        // Play icon: right-pointing triangle
        std::vector<Point2Dd> tri = {
            Point2Dd(cx - r * 0.6f, cy - r),
            Point2Dd(cx - r * 0.6f, cy + r),
            Point2Dd(cx + r,        cy)
        };
        ctx->FillLinePath(tri);
    }

    // ----- Stop -----
    if (stopButtonRect.width > 0) {
        int sx = stopButtonRect.x + stopButtonRect.width / 2;
        int sy = stopButtonRect.y + stopButtonRect.height / 2;
        int sr = stopButtonRect.width / 3;
        ctx->SetFillPaint(style.iconColor);
        ctx->FillRectangle(Rect2Dd(sx - sr, sy - sr, sr * 2, sr * 2));
    }

    // ----- Mute toggle -----
    if (muteButtonRect.width > 0) {
        int mx = muteButtonRect.x + muteButtonRect.width / 2;
        int my = muteButtonRect.y + muteButtonRect.height / 2;
        int mr = muteButtonRect.width / 3;

        // Speaker icon rendered from media/icons/volume.svg, tinted to match
        // the player's icon color (DrawMask uses the SVG alpha as a stencil).
        int iconSize = mr * 2;
        Rect2Dd iconRect(mx - iconSize / 2.0, my - iconSize / 2.0,
                         iconSize, iconSize);
        ctx->DrawMask(style.iconColor, VolumeIconPath(), iconRect,
                      ImageFitMode::Contain);

        if (player->IsMuted()) {
            // Red "X" through the speaker
            ctx->SetStrokePaint(Color(220, 30, 30));
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLine(Point2Dd(mx - mr, my - mr), Point2Dd(mx + mr, my + mr));
            ctx->DrawLine(Point2Dd(mx + mr, my - mr), Point2Dd(mx - mr, my + mr));
        }
    }
}

void UltraCanvasAudioPlayerElement::DrawSeekBar(IRenderContext* ctx) {
    // Track
    ctx->DrawFilledRectangle(
        Rect2Dd(seekBarRect.x, seekBarRect.y,
                seekBarRect.width, seekBarRect.height),
        style.progressTrackColor, 0.0f, Color(0, 0, 0, 0),
        seekBarRect.height / 2);

    double dur = player->GetDuration();
    double pos = player->GetPosition();
    float pct = (dur > 0.0) ? static_cast<float>(pos / dur) : 0.0f;
    pct = std::clamp(pct, 0.0f, 1.0f);

    int fillW = static_cast<int>(seekBarRect.width * pct);
    if (fillW > 0) {
        ctx->DrawFilledRectangle(
            Rect2Dd(seekBarRect.x, seekBarRect.y,
                    fillW, seekBarRect.height),
            style.progressFillColor, 0.0f, Color(0, 0, 0, 0),
            seekBarRect.height / 2);
    }

    // Knob
    int knobX = seekBarRect.x + fillW;
    int knobY = seekBarRect.y + seekBarRect.height / 2;
    ctx->SetFillPaint(style.knobColor);
    ctx->FillCircle(Point2Dd(knobX, knobY), kSeekKnobR);
    ctx->SetStrokePaint(style.knobBorderColor);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(Point2Dd(knobX, knobY), kSeekKnobR);
}

void UltraCanvasAudioPlayerElement::DrawVolumeBar(IRenderContext* ctx) {
    ctx->DrawFilledRectangle(
        Rect2Dd(volumeBarRect.x, volumeBarRect.y,
                volumeBarRect.width, volumeBarRect.height),
        style.progressTrackColor, 0.0f, Color(0, 0, 0, 0),
        volumeBarRect.height / 2);

    float v = player->IsMuted() ? 0.0f : player->GetVolume();
    int fillW = static_cast<int>(volumeBarRect.width * v);
    if (fillW > 0) {
        ctx->DrawFilledRectangle(
            Rect2Dd(volumeBarRect.x, volumeBarRect.y,
                    fillW, volumeBarRect.height),
            style.progressFillColor, 0.0f, Color(0, 0, 0, 0),
            volumeBarRect.height / 2);
    }
    int knobX = volumeBarRect.x + fillW;
    int knobY = volumeBarRect.y + volumeBarRect.height / 2;
    ctx->SetFillPaint(style.knobColor);
    ctx->FillCircle(Point2Dd(knobX, knobY), kVolumeKnobR);
    ctx->SetStrokePaint(style.knobBorderColor);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(Point2Dd(knobX, knobY), kVolumeKnobR);
}

void UltraCanvasAudioPlayerElement::DrawTimeLabels(IRenderContext* ctx) {
    std::string label = FormatTime(player->GetPosition()) + " / " +
                        FormatTime(player->GetDuration());
    ctx->SetFontSize(11);
    ctx->SetTextPaint(style.textColor);
    ctx->DrawText(label, Point2Dd(timeLabelRect.x, timeLabelRect.y + 12));
}

bool UltraCanvasAudioPlayerElement::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    // Pointer is in window-local coords; subtract our origin to get local.
    Point2Di p = event.pointer;

    switch (event.type) {
        case UCEventType::MouseEnter:
            return true;

        case UCEventType::MouseLeave:
            hoverPlay = false;
            draggingSeek = false;
            draggingVolume = false;
            RequestRedraw();
            return true;

        case UCEventType::MouseDown: {
            if (event.button != UCMouseButton::Left) return false;
            if (Hit(playButtonRect, p)) {
                TogglePlayPause();
                RequestRedraw();
                return true;
            }
            if (stopButtonRect.width > 0 && Hit(stopButtonRect, p)) {
                Stop();
                RequestRedraw();
                return true;
            }
            if (muteButtonRect.width > 0 && Hit(muteButtonRect, p)) {
                player->SetMute(!player->IsMuted());
                RequestRedraw();
                return true;
            }
            if (Hit(seekBarRect, p)) {
                draggingSeek = true;
                float pct = static_cast<float>(p.x - seekBarRect.x) /
                            std::max(1, seekBarRect.width);
                pct = std::clamp(pct, 0.0f, 1.0f);
                Seek(pct * player->GetDuration());
                RequestRedraw();
                return true;
            }
            if (volumeBarRect.width > 0 && Hit(volumeBarRect, p)) {
                draggingVolume = true;
                float pct = static_cast<float>(p.x - volumeBarRect.x) /
                            std::max(1, volumeBarRect.width);
                player->SetVolume(std::clamp(pct, 0.0f, 1.0f));
                RequestRedraw();
                return true;
            }
            return false;
        }

        case UCEventType::MouseMove: {
            bool newHoverPlay = Hit(playButtonRect, p);
            if (newHoverPlay != hoverPlay) {
                hoverPlay = newHoverPlay;
                RequestRedraw();
            }
            if (draggingSeek) {
                float pct = static_cast<float>(p.x - seekBarRect.x) /
                            std::max(1, seekBarRect.width);
                pct = std::clamp(pct, 0.0f, 1.0f);
                Seek(pct * player->GetDuration());
                RequestRedraw();
                return true;
            }
            if (draggingVolume) {
                float pct = static_cast<float>(p.x - volumeBarRect.x) /
                            std::max(1, volumeBarRect.width);
                player->SetVolume(std::clamp(pct, 0.0f, 1.0f));
                RequestRedraw();
                return true;
            }
            return false;
        }

        case UCEventType::MouseUp:
            draggingSeek = false;
            draggingVolume = false;
            return false;

        default:
            return false;
    }
}

} // namespace UltraCanvas
