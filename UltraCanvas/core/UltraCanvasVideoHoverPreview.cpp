// core/UltraCanvasVideoHoverPreview.cpp
// Reusable hover-triggered inline video preview engine
// Version: 0.1.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework

#include "UltraCanvasVideoHoverPreview.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cstring>

namespace UltraCanvas {

UltraCanvasVideoHoverPreview::UltraCanvasVideoHoverPreview() = default;

UltraCanvasVideoHoverPreview::~UltraCanvasVideoHoverPreview() {
    End();
}

void UltraCanvasVideoHoverPreview::Begin(const std::string& src) {
    End();
    if (src.empty()) return;

    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    const int fps = std::clamp(config.targetFps, 1, 60);
    tickTimerId = app->StartTimer(static_cast<unsigned int>(1000 / fps), true,
                                  [this](TimerId) { OnTick(); });
    if (tickTimerId == InvalidTimerId) return;

    source = src;
    armTime = std::chrono::steady_clock::now();
    state = VideoHoverPreviewState::Pending;
    if (config.startDelayMs <= 0) StartPlayback();
}

void UltraCanvasVideoHoverPreview::End() {
    Teardown();
    state = VideoHoverPreviewState::Idle;
    source.clear();
}

void UltraCanvasVideoHoverPreview::StartPlayback() {
    if (state != VideoHoverPreviewState::Pending) return;

    // A fresh player per preview: disableAudio is an open-time option, so it
    // must be baked into the config before LoadFrom*. A silent preview opens
    // its source without wiring audio at all — no audio decode, no audio
    // device occupied (and no preroll stall on machines without one).
    VideoPlaybackConfig pc;
    pc.mute = config.muted;
    pc.disableAudio = config.muted;
    pc.loop = config.loop;
    player = std::make_shared<UltraCanvasVideoPlayer>(pc);
    endedFlag = false;
    errorFlag = false;
    player->onEnded = [this]() { endedFlag = true; };
    player->onError = [this](const std::string&) { errorFlag = true; };

    const bool isUrl = source.find("://") != std::string::npos;
    const bool ok = isUrl ? player->LoadFromUrl(source)
                          : player->LoadFromFile(source);
    if (!ok) {                       // null backend / unopenable source
        Finish();
        return;
    }
    if (config.startOffsetSec > 0.0) player->Seek(config.startOffsetSec);
    player->Play();
    playbackStart = std::chrono::steady_clock::now();
    state = VideoHoverPreviewState::Playing;
}

void UltraCanvasVideoHoverPreview::OnTick() {
    if (state == VideoHoverPreviewState::Pending) {
        const std::chrono::duration<double, std::milli> dwell =
                std::chrono::steady_clock::now() - armTime;
        if (dwell.count() >= config.startDelayMs) StartPlayback();
        return;
    }
    if (state != VideoHoverPreviewState::Playing) return;

    if (errorFlag || (endedFlag && !config.loop)) {
        Finish();
        return;
    }
    if (config.durationSec > 0.0f) {
        const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - playbackStart;
        if (elapsed.count() >= config.durationSec) {
            Finish();
            return;
        }
    }

    UCVideoFramePtr before = shownFrame;
    UploadCurrentFrame();
    if (shownFrame != before && onFrame) onFrame();
}

void UltraCanvasVideoHoverPreview::Finish() {
    Teardown();
    state = VideoHoverPreviewState::Finished;
    if (onStopped) onStopped();
}

void UltraCanvasVideoHoverPreview::Teardown() {
    StopTickTimer();
    if (player) {
        player->onEnded = nullptr;
        player->onError = nullptr;
        player->Unload();
    }
    shownFrame.reset();
    haveFrame = false;
}

// Same BGRA-into-cairo upload the video player element uses (see
// UltraCanvasVideoPlayerElement::UploadCurrentFrame).
void UltraCanvasVideoHoverPreview::UploadCurrentFrame() {
    UCVideoFramePtr f = player ? player->GetCurrentFrame() : nullptr;
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

void UltraCanvasVideoHoverPreview::StopTickTimer() {
    if (tickTimerId == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(tickTimerId);
    tickTimerId = InvalidTimerId;
}

} // namespace UltraCanvas
