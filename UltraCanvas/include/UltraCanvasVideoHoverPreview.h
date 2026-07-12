// include/UltraCanvasVideoHoverPreview.h
// Reusable hover-triggered inline video preview: dwell delay, muted playback,
// duration limit, frames delivered as a ready-to-draw UCPixmap.
// Version: 0.1.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEOHOVERPREVIEW_H
#define ULTRACANVASVIDEOHOVERPREVIEW_H

#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasVideoPlayer.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== HOVER PREVIEW =====
//
// Drives the common "hover a video thumbnail to see a few seconds of it"
// interaction as a standalone, widget-agnostic engine so any element that
// paints video covers (UltraCanvasAlbum, file browsers, custom galleries) can
// offer it. The host widget only has to:
//
//   1. call Begin(source) when the cursor settles on a video tile and End()
//      when it leaves (or the tile goes away),
//   2. repaint on onFrame / onStopped, and
//   3. while GetFramePixmap() returns non-null, draw that pixmap in place of
//      the static thumbnail.
//
// Playback starts only after the configured dwell delay (so a cursor merely
// passing across a grid never spins up a decoder), runs muted by default, and
// ends by itself after durationSec. One decode session exists at a time; a new
// Begin() replaces the previous preview. With the null video backend (or a
// source that fails to open) the preview silently never starts and the host's
// static thumbnail simply stays — no special-casing needed.

struct VideoHoverPreviewConfig {
    int    startDelayMs   = 400;    // cursor dwell before the preview starts
    float  durationSec    = 5.0f;   // preview length; 0 = play until End()
    bool   loop           = false;  // restart a clip shorter than the duration
    double startOffsetSec = 0.0;    // skip into the clip (e.g. past intro black)
    bool   muted          = true;   // previews are silent unless opted in
    int    targetFps      = 24;     // frame-poll / repaint cadence
};

enum class VideoHoverPreviewState {
    Idle,      // nothing armed
    Pending,   // dwell delay running, playback not started yet
    Playing,   // decode session live, frames arriving
    Finished   // ended by itself (duration / clip end / error); host shows the
               // static thumbnail again until the next Begin()
};

class UltraCanvasVideoHoverPreview {
public:
    UltraCanvasVideoHoverPreview();
    ~UltraCanvasVideoHoverPreview();

    UltraCanvasVideoHoverPreview(const UltraCanvasVideoHoverPreview&) = delete;
    UltraCanvasVideoHoverPreview& operator=(const UltraCanvasVideoHoverPreview&) = delete;

    // ===== CONFIG =====
    void SetConfig(const VideoHoverPreviewConfig& cfg) { config = cfg; }
    const VideoHoverPreviewConfig& GetConfig() const { return config; }

    // ===== CONTROL =====
    // Arm the preview for a video file path or URL ("scheme://..." sources are
    // opened as URLs). Any previous preview is ended first. Playback begins
    // after config.startDelayMs of the caller keeping the preview armed.
    void Begin(const std::string& source);

    // Stop and unload immediately (cursor left / tile removed / widget hidden).
    // Safe to call in any state; does NOT fire onStopped.
    void End();

    // ===== STATE =====
    VideoHoverPreviewState GetState() const { return state; }
    bool IsIdle() const { return state == VideoHoverPreviewState::Idle; }
    const std::string& GetSource() const { return source; }

    // ===== FRAME ACCESS (main thread) =====
    // The most recent uploaded frame, or null while there is nothing to show
    // (idle / dwell delay / still prerolling / finished).
    std::shared_ptr<UCPixmap> GetFramePixmap() const {
        return haveFrame ? framePixmap : nullptr;
    }
    int GetFrameWidth() const { return frameW; }
    int GetFrameHeight() const { return frameH; }

    // ===== EVENTS (main thread) =====
    std::function<void()> onFrame;    // a new frame was uploaded — repaint
    std::function<void()> onStopped;  // ended by itself (duration/end/error) — repaint

private:
    VideoHoverPreviewConfig config;
    VideoHoverPreviewState state = VideoHoverPreviewState::Idle;
    std::string source;

    // Recreated per preview: disableAudio (derived from config.muted) is an
    // open-time option baked into the player's config.
    std::shared_ptr<UltraCanvasVideoPlayer> player;

    // Backend callbacks may fire off the main thread; they only raise these
    // flags, which the main-thread frame tick acts on.
    std::atomic<bool> endedFlag{false};
    std::atomic<bool> errorFlag{false};

    // One periodic tick timer drives everything (dwell countdown, frame
    // polling, duration limit), so a fire never has to start another timer
    // from inside its own callback.
    TimerId tickTimerId = InvalidTimerId;
    std::chrono::steady_clock::time_point armTime;        // Begin() called
    std::chrono::steady_clock::time_point playbackStart;  // playback started

    // Frame upload target (see UltraCanvasVideoPlayerElement for the pattern).
    std::shared_ptr<UCPixmap> framePixmap;
    UCVideoFramePtr shownFrame;        // keeps the uploaded frame alive
    bool haveFrame = false;
    int  frameW = 0, frameH = 0;

    void StartPlayback();              // dwell elapsed — open + play
    void Finish();                     // self-initiated stop; fires onStopped
    void Teardown();                   // shared cleanup for End()/Finish()
    void OnTick();
    void UploadCurrentFrame();
    void StopTickTimer();
};

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEOHOVERPREVIEW_H
