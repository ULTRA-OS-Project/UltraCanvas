// core/UltraCanvasVideoPlayerElement.cpp
// Composite UI control wrapping UltraCanvasVideoPlayer: video surface + transport bar
// Version: 0.1.5
// Last Modified: 2026-07-12
// V0.1.5: A replay after end-of-stream shows video again: the frame timer is no
//   longer stopped on EOS (Play also re-arms it), so frame uploads resume when
//   playback restarts instead of leaving a frozen surface with audio only.
// Author: UltraCanvas Framework

#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace UltraCanvas {

namespace {
    constexpr int kPadding = 10;
    constexpr int kGap = 8;
    constexpr int kTimeWidth = 100;
    constexpr int kVolumeWidth = 80;
    constexpr int kSeekKnobR = 6;
    constexpr double kControlsIdleHideSec = 2.5;
    // Throttle scrub seeks: each flushing seek flushes+refills the audio sink,
    // and a per-MouseMove storm of them eventually stalls the PulseAudio clock
    // (whole-pipeline freeze). Issue at most one per interval; the final drag
    // position is always applied on MouseUp.
    constexpr double kScrubSeekIntervalSec = 0.20;

    inline bool Hit(const Rect2Di& r, const Point2Di& p) {
        return r.width > 0 && r.height > 0 &&
               p.x >= r.x && p.x < r.x + r.width &&
               p.y >= r.y && p.y < r.y + r.height;
    }
    double NowSeconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
    // Resolve a transport icon (SVG) under the framework resources dir.
    std::string VideoIconPath(const std::string& name) {
        return NormalizePath(GetResourcesDir() + "media/icons/" + name);
    }
}

UltraCanvasVideoPlayerElement::UltraCanvasVideoPlayerElement(
        const std::string& identifier, long x, long y, long w, long h)
    : UltraCanvasUIElement(identifier, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(w), static_cast<float>(h)) {
    player = std::make_shared<UltraCanvasVideoPlayer>();
    HookPlayerCallbacks();
    Relayout();
}

UltraCanvasVideoPlayerElement::~UltraCanvasVideoPlayerElement() {
    StopFrameTimer();
}

bool UltraCanvasVideoPlayerElement::LoadFromFile(const std::string& filePath) {
    bool ok = player->LoadFromFile(filePath);
    if (ok) StartFrameTimer();          // timer runs while a source is loaded
    lastInteractionTime = NowSeconds();
    RequestRedraw();
    return ok;
}

bool UltraCanvasVideoPlayerElement::LoadFromUrl(const std::string& url) {
    bool ok = player->LoadFromUrl(url);
    if (ok) StartFrameTimer();          // timer runs while a source is loaded
    RequestRedraw();
    return ok;
}

void UltraCanvasVideoPlayerElement::ShowOpenDialog() {
    FileDialogOptions opts;
    opts.SetTitle("Open Video File")
        .AddFilter("Video files",
                   std::vector<std::string>{"mp4", "m4v", "mov", "mkv", "webm", "avi"})
        .AddFilter("MP4 video (*.mp4)", "mp4")
        .AddFilter("Matroska (*.mkv)", "mkv")
        .AddFilter("WebM (*.webm)", "webm")
        .AddFilter("QuickTime (*.mov)", "mov")
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

void UltraCanvasVideoPlayerElement::Play() {
    // The frame timer's lifetime is tied to a loaded source (started in
    // LoadFrom*, stopped in the destructor), not to playback state, so the time
    // readout keeps tracking the audio clock even when our optimistic engine
    // state lags the real pipeline. Re-arm it here anyway (no-op if running):
    // a replay after end-of-stream must resume frame uploads or the surface
    // stays frozen while the audio restarts.
    StartFrameTimer();
    player->Play();
    lastInteractionTime = NowSeconds();
    if (onPlay) onPlay();
}
void UltraCanvasVideoPlayerElement::Pause() {
    player->Pause();
    if (onPause) onPause();
    RequestRedraw();
}
void UltraCanvasVideoPlayerElement::Stop() {
    player->Stop();
    if (onStop) onStop();
    RequestRedraw();
}
void UltraCanvasVideoPlayerElement::Seek(double s) {
    player->Seek(s);
    if (onSeek) onSeek(s);
    RequestRedraw();
}
void UltraCanvasVideoPlayerElement::TogglePlayPause() {
    if (player->IsPlaying()) Pause(); else Play();
}

void UltraCanvasVideoPlayerElement::SetStyle(const VideoPlayerStyle& s) {
    style = s;
    Relayout();
    RequestRedraw();
}

void UltraCanvasVideoPlayerElement::HookPlayerCallbacks() {
    auto self = this;
    player->onPlaybackStateChanged = [self](VideoPlaybackState) {
        // Timer lifetime is tied to a loaded source (see LoadFrom*), not state.
        // Here we only refresh the play/pause icon.
        self->RequestRedraw();
    };
    player->onEnded = [self]() {
        // Keep the frame timer running: it's tied to the loaded source, and a
        // replay (Play/Seek after EOS) needs it to keep uploading frames. The
        // per-tick early-out makes an idle ended player as cheap as a paused one.
        if (self->onEnded) self->onEnded();
        self->RequestRedraw();
    };
    // Frame delivery happens on a backend thread; the frame timer (main thread)
    // is what actually uploads + repaints, so we don't touch the renderer here.
}

void UltraCanvasVideoPlayerElement::StartFrameTimer() {
    if (frameTimerId != InvalidTimerId) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    int fps = std::clamp(style.targetFps, 1, 120);
    unsigned int ms = static_cast<unsigned int>(1000 / fps);
    frameTimerId = app->StartTimer(ms, true, [this](TimerId) { OnFrameTick(); });
}

void UltraCanvasVideoPlayerElement::StopFrameTimer() {
    if (frameTimerId == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(frameTimerId);
    frameTimerId = InvalidTimerId;
}

void UltraCanvasVideoPlayerElement::OnFrameTick() {
    if (!IsVisible()) return;
    // Flush a throttled scrub target that stopped moving without a MouseUp yet.
    if (pendingScrubSeek && (NowSeconds() - lastScrubSeekTime) >= kScrubSeekIntervalSec) {
        lastScrubSeekTime = NowSeconds();
        pendingScrubSeek = false;
        Seek(pendingScrubSeconds);
    }
    // Only repaint when something actually changed, so a paused/idle player
    // (timer now runs for the whole loaded lifetime) doesn't repaint every tick.
    UCVideoFramePtr before = shownFrame;
    UploadCurrentFrame();
    bool frameChanged = (shownFrame != before);
    int sec = static_cast<int>(player->GetPosition());
    bool timeChanged = (sec != lastShownSecond);
    lastShownSecond = sec;
    if (frameChanged || timeChanged) RequestRedraw();
}

void UltraCanvasVideoPlayerElement::UploadCurrentFrame() {
    UCVideoFramePtr f = player->GetCurrentFrame();
    if (!f || !f->IsValid()) return;
    if (f == shownFrame) return;          // nothing new
    shownFrame = f;

    const int w = f->GetWidth();
    const int h = f->GetHeight();
    if (!framePixmap || frameW != w || frameH != h) {
        framePixmap = std::make_shared<UCPixmap>();
        framePixmap->Init(w, h);
        frameW = w; frameH = h;
    }
    if (!framePixmap || !framePixmap->IsValid()) return;

    cairo_surface_t* surf = framePixmap->GetSurface();
    if (!surf) return;
    unsigned char* dst = cairo_image_surface_get_data(surf);
    const int dstStride = cairo_image_surface_get_stride(surf);
    const uint8_t* src = f->GetData();
    const int srcStride = f->GetStride();
    const bool swapRB = (f->GetInfo().pixelFormat == VideoPixelFormat::RGBA32);

    for (int y = 0; y < h; ++y) {
        const uint8_t* sr = src + static_cast<size_t>(y) * srcStride;
        uint8_t* dr = dst + static_cast<size_t>(y) * dstStride;
        if (!swapRB) {
            std::memcpy(dr, sr, static_cast<size_t>(w) * 4);
        } else {
            for (int x = 0; x < w; ++x) {
                dr[x * 4 + 0] = sr[x * 4 + 2];  // B <- R
                dr[x * 4 + 1] = sr[x * 4 + 1];  // G
                dr[x * 4 + 2] = sr[x * 4 + 0];  // R <- B
                dr[x * 4 + 3] = sr[x * 4 + 3];  // A
            }
        }
    }
    framePixmap->MarkDirty();
    haveFrame = true;
}

Rect2Di UltraCanvasVideoPlayerElement::FitContain(const Rect2Di& dst, int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) return dst;
    double sx = static_cast<double>(dst.width) / srcW;
    double sy = static_cast<double>(dst.height) / srcH;
    double s = std::min(sx, sy);
    int w = static_cast<int>(srcW * s);
    int h = static_cast<int>(srcH * s);
    int x = dst.x + (dst.width - w) / 2;
    int y = dst.y + (dst.height - h) / 2;
    return Rect2Di(x, y, w, h);
}

void UltraCanvasVideoPlayerElement::Relayout() {
    Rect2Df b = GetLocalBounds();
    videoRect = Rect2Di(static_cast<int>(b.x), static_cast<int>(b.y),
                        static_cast<int>(b.width), static_cast<int>(b.height));

    int barH = style.controlBarHeight;
    controlBarRect = Rect2Di(videoRect.x, videoRect.y + videoRect.height - barH,
                             videoRect.width, barH);

    int cy = controlBarRect.y + barH / 2;
    int btn = style.buttonSize;
    int x = controlBarRect.x + kPadding;

    playButtonRect = Rect2Di(x, cy - btn / 2, btn, btn);
    x += btn + kGap;

    int rightX = controlBarRect.x + controlBarRect.width - kPadding;

    if (style.showVolumeSlider) {
        volumeBarRect = Rect2Di(rightX - kVolumeWidth, cy - style.sliderHeight / 2,
                                kVolumeWidth, style.sliderHeight);
        muteButtonRect = Rect2Di(volumeBarRect.x - btn - kGap, cy - btn / 2, btn, btn);
        rightX = muteButtonRect.x - kGap;
    } else {
        volumeBarRect = Rect2Di();
        muteButtonRect = Rect2Di();
    }

    if (style.showTimeLabels) {
        timeLabelRect = Rect2Di(rightX - kTimeWidth, cy - 8, kTimeWidth, 16);
        rightX = timeLabelRect.x - kGap;
    } else {
        timeLabelRect = Rect2Di();
    }

    int seekW = std::max(20, rightX - x);
    seekBarRect = Rect2Di(x, cy - style.sliderHeight / 2, seekW, style.sliderHeight);
}

std::string UltraCanvasVideoPlayerElement::FormatTime(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return std::string(buf);
}

bool UltraCanvasVideoPlayerElement::ControlsVisible() const {
    if (!style.showControls) return false;
    if (!style.autoHideControls) return true;
    if (!player->IsPlaying()) return true;
    return (NowSeconds() - lastInteractionTime) < kControlsIdleHideSec;
}

void UltraCanvasVideoPlayerElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    Relayout();
    DrawVideoSurface(ctx);
    if (ControlsVisible()) DrawControlBar(ctx);
}

void UltraCanvasVideoPlayerElement::DrawVideoSurface(IRenderContext* ctx) {
    // Letterbox background
    ctx->DrawFilledRectangle(Rect2Dd(videoRect.x, videoRect.y, videoRect.width, videoRect.height),
                             style.backgroundColor, 0.0f, Color(0, 0, 0, 0), 0);

    if (haveFrame && framePixmap && framePixmap->IsValid()) {
        Rect2Di fit = FitContain(videoRect, frameW, frameH);
        ctx->DrawPixmap(*framePixmap, Rect2Dd(fit.x, fit.y, fit.width, fit.height),
                        ImageFitMode::Fill);
    } else {
        // No-signal placeholder
        std::string msg = player->IsLoaded() ? "Buffering..." : "No video loaded";
        ctx->SetFontSize(14);
        ctx->SetTextPaint(Color(160, 160, 160));
        ctx->DrawText(msg, Point2Dd(videoRect.x + videoRect.width / 2 - 50,
                                    videoRect.y + videoRect.height / 2));
    }
}

void UltraCanvasVideoPlayerElement::DrawControlBar(IRenderContext* ctx) {
    ctx->DrawFilledRectangle(Rect2Dd(controlBarRect.x, controlBarRect.y,
                                     controlBarRect.width, controlBarRect.height),
                             style.controlBarColor, 0.0f, Color(0, 0, 0, 0), 0);

    // Play / Pause icon
    int cx = playButtonRect.x + playButtonRect.width / 2;
    int cy = playButtonRect.y + playButtonRect.height / 2;
    int r = playButtonRect.width / 3;
    ctx->SetFillPaint(hoverPlay ? style.iconHoverColor : style.iconColor);
    if (player->IsPlaying()) {
        int bw = r / 2, gap = bw / 2;
        ctx->FillRectangle(Rect2Dd(cx - gap - bw, cy - r, bw, r * 2));
        ctx->FillRectangle(Rect2Dd(cx + gap, cy - r, bw, r * 2));
    } else {
        std::vector<Point2Dd> tri = {
            Point2Dd(cx - r * 0.6, cy - r), Point2Dd(cx - r * 0.6, cy + r),
            Point2Dd(cx + r, cy)
        };
        ctx->FillLinePath(tri);
    }

    // Seek bar
    ctx->DrawFilledRectangle(Rect2Dd(seekBarRect.x, seekBarRect.y, seekBarRect.width, seekBarRect.height),
                             style.progressTrackColor, 0.0f, Color(0, 0, 0, 0), seekBarRect.height / 2);
    double dur = player->GetDuration();
    double pos = player->GetPosition();
    float pct = (dur > 0.0) ? static_cast<float>(pos / dur) : 0.0f;
    pct = std::clamp(pct, 0.0f, 1.0f);
    int fillW = static_cast<int>(seekBarRect.width * pct);
    if (fillW > 0) {
        ctx->DrawFilledRectangle(Rect2Dd(seekBarRect.x, seekBarRect.y, fillW, seekBarRect.height),
                                 style.progressFillColor, 0.0f, Color(0, 0, 0, 0), seekBarRect.height / 2);
    }
    ctx->SetFillPaint(style.knobColor);
    ctx->FillCircle(Point2Dd(seekBarRect.x + fillW, seekBarRect.y + seekBarRect.height / 2), kSeekKnobR);

    // Mute / volume icon (SVG, tinted via the icon-as-mask path)
    if (muteButtonRect.width > 0) {
        const std::string icon = player->IsMuted() ? "volume-x.svg" : "volume.svg";
        int isz = 18;
        Rect2Dd ir(muteButtonRect.x + (muteButtonRect.width - isz) / 2,
                   muteButtonRect.y + (muteButtonRect.height - isz) / 2, isz, isz);
        ctx->DrawMask(style.iconColor, VideoIconPath(icon), ir, ImageFitMode::Contain);
    }

    // Volume bar
    if (volumeBarRect.width > 0) {
        ctx->DrawFilledRectangle(Rect2Dd(volumeBarRect.x, volumeBarRect.y,
                                         volumeBarRect.width, volumeBarRect.height),
                                 style.progressTrackColor, 0.0f, Color(0, 0, 0, 0), volumeBarRect.height / 2);
        float v = player->IsMuted() ? 0.0f : player->GetVolume();
        int vw = static_cast<int>(volumeBarRect.width * v);
        if (vw > 0) {
            ctx->DrawFilledRectangle(Rect2Dd(volumeBarRect.x, volumeBarRect.y, vw, volumeBarRect.height),
                                     style.progressFillColor, 0.0f, Color(0, 0, 0, 0), volumeBarRect.height / 2);
        }
    }

    // Time label
    if (timeLabelRect.width > 0) {
        std::string label = FormatTime(pos) + " / " + FormatTime(dur);
        ctx->SetFontSize(11);
        ctx->SetTextPaint(style.textColor);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(label, Rect2Dd(timeLabelRect.x, timeLabelRect.y,
                                           timeLabelRect.width, timeLabelRect.height));
        ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    }
}

bool UltraCanvasVideoPlayerElement::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    Point2Di p = event.pointer;

    switch (event.type) {
        case UCEventType::MouseEnter:
            lastInteractionTime = NowSeconds();
            RequestRedraw();
            return true;

        case UCEventType::MouseLeave:
            hoverPlay = draggingSeek = draggingVolume = false;
            RequestRedraw();
            return true;

        case UCEventType::MouseDown: {
            if (event.button != UCMouseButton::Left) return false;
            lastInteractionTime = NowSeconds();
            if (Hit(playButtonRect, p)) { TogglePlayPause(); return true; }
            if (muteButtonRect.width > 0 && Hit(muteButtonRect, p)) {
                player->SetMute(!player->IsMuted()); RequestRedraw(); return true;
            }
            if (Hit(seekBarRect, p)) {
                draggingSeek = true;
                lastScrubSeekTime = NowSeconds();   // initial click seeks now; reset throttle
                pendingScrubSeek = false;
                float pct = std::clamp(static_cast<float>(p.x - seekBarRect.x) /
                                       std::max(1, seekBarRect.width), 0.0f, 1.0f);
                Seek(pct * player->GetDuration());
                return true;
            }
            if (volumeBarRect.width > 0 && Hit(volumeBarRect, p)) {
                draggingVolume = true;
                float pct = std::clamp(static_cast<float>(p.x - volumeBarRect.x) /
                                       std::max(1, volumeBarRect.width), 0.0f, 1.0f);
                player->SetVolume(pct); RequestRedraw();
                return true;
            }
            // Clicks on the control-bar background (anything not a control above)
            // must not toggle playback — consume them. Only the video surface
            // above the bar toggles. When controls are auto-hidden, the whole
            // surface is clickable.
            if (ControlsVisible() && Hit(controlBarRect, p)) return true;
            if (Hit(videoRect, p)) { TogglePlayPause(); return true; }
            return false;
        }

        case UCEventType::MouseMove: {
            lastInteractionTime = NowSeconds();
            bool nh = Hit(playButtonRect, p);
            if (nh != hoverPlay) { hoverPlay = nh; RequestRedraw(); }
            if (draggingSeek) {
                float pct = std::clamp(static_cast<float>(p.x - seekBarRect.x) /
                                       std::max(1, seekBarRect.width), 0.0f, 1.0f);
                double target = pct * player->GetDuration();
                pendingScrubSeconds = target;
                pendingScrubSeek = true;
                double now = NowSeconds();
                if (now - lastScrubSeekTime >= kScrubSeekIntervalSec) {
                    lastScrubSeekTime = now;
                    pendingScrubSeek = false;
                    Seek(target);                 // throttled: at most one per interval
                } else {
                    RequestRedraw();              // keep the knob tracking the cursor
                }
                return true;
            }
            if (draggingVolume) {
                float pct = std::clamp(static_cast<float>(p.x - volumeBarRect.x) /
                                       std::max(1, volumeBarRect.width), 0.0f, 1.0f);
                player->SetVolume(pct); RequestRedraw();
                return true;
            }
            return false;
        }

        case UCEventType::MouseUp:
            // Always land the exact release position, even if the last move fell
            // inside the throttle window.
            if (draggingSeek && pendingScrubSeek) {
                pendingScrubSeek = false;
                Seek(pendingScrubSeconds);
            }
            draggingSeek = draggingVolume = false;
            return false;

        default:
            return false;
    }
}

} // namespace UltraCanvas
