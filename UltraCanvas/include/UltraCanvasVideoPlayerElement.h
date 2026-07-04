// include/UltraCanvasVideoPlayerElement.h
// Composite UI control wrapping UltraCanvasVideoPlayer: video surface + transport bar
// Version: 0.1.2
// Last Modified: 2026-06-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasVideoPlayer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== VISUAL STYLE =====
struct VideoPlayerStyle {
    Color backgroundColor = Color(16, 16, 16);          // letterbox / no-signal fill
    Color controlBarColor = Color(0, 0, 0, 160);        // translucent overlay
    Color progressTrackColor = Color(90, 90, 90);
    Color progressFillColor = Color(0, 150, 230);
    Color knobColor = Colors::White;
    Color textColor = Color(235, 235, 235);
    Color iconColor = Color(235, 235, 235);
    Color iconHoverColor = Color(0, 170, 255);

    int   controlBarHeight = 40;
    int   buttonSize = 28;
    int   sliderHeight = 5;
    bool  showVolumeSlider = true;
    bool  showTimeLabels = true;
    bool  autoHideControls = true;     // fade the bar out while playing + idle
    bool  showControls = true;
    int   targetFps = 30;              // frame-pull / redraw cadence
};

// ===== VIDEO PLAYER UI ELEMENT =====
class UltraCanvasVideoPlayerElement : public UltraCanvasUIElement {
public:
    UltraCanvasVideoPlayerElement(const std::string& identifier = "VideoPlayer",
                                  long x = 0, long y = 0, long w = 640, long h = 400);
    ~UltraCanvasVideoPlayerElement() override;

    // ===== SOURCE =====
    bool LoadFromFile(const std::string& filePath);
    bool LoadFromUrl(const std::string& url);

    // Opens the platform's native open dialog pre-filled with video filters.
    void ShowOpenDialog();

    // ===== TRANSPORT =====
    void Play();
    void Pause();
    void Stop();
    void Seek(double seconds);
    void TogglePlayPause();

    // ===== STYLE =====
    void SetStyle(const VideoPlayerStyle& s);
    const VideoPlayerStyle& GetStyle() const { return style; }

    // ===== ACCESS =====
    std::shared_ptr<UltraCanvasVideoPlayer> GetPlayer() const { return player; }

    // ===== EVENTS =====
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void(double)> onSeek;
    std::function<void()> onEnded;
    std::function<void(const std::string& filePath)> onFileOpened;
    std::function<void()> onOpenCancelled;

    // ===== UIElement OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    std::shared_ptr<UltraCanvasVideoPlayer> player;
    VideoPlayerStyle style;

    // Frame upload target. Re-inited when frame dimensions change.
    std::shared_ptr<UCPixmap> framePixmap;
    UCVideoFramePtr shownFrame;        // keeps the uploaded frame alive
    bool haveFrame = false;
    int  frameW = 0, frameH = 0;
    int  lastShownSecond = -1;         // OnFrameTick early-out: last second drawn

    // Frame/redraw timer (main thread)
    TimerId frameTimerId = InvalidTimerId;

    // Scrub-seek throttle: coalesce per-MouseMove seeks to one per interval (the
    // backend flushes the audio sink per seek; a storm stalls its clock). The
    // last target is held and applied on MouseUp / next eligible tick.
    bool   pendingScrubSeek = false;
    double pendingScrubSeconds = 0.0;
    double lastScrubSeekTime = 0.0;

    // Cached hit-test regions
    Rect2Di videoRect;
    Rect2Di controlBarRect;
    Rect2Di playButtonRect;
    Rect2Di seekBarRect;
    Rect2Di volumeBarRect;
    Rect2Di muteButtonRect;
    Rect2Di timeLabelRect;

    bool draggingSeek = false;
    bool draggingVolume = false;
    bool hoverPlay = false;
    double lastInteractionTime = 0.0;

    void HookPlayerCallbacks();
    void Relayout();
    void StartFrameTimer();
    void StopFrameTimer();
    void OnFrameTick();
    void UploadCurrentFrame();
    void DrawVideoSurface(IRenderContext* ctx);
    void DrawControlBar(IRenderContext* ctx);
    bool ControlsVisible() const;
    static std::string FormatTime(double seconds);
    static Rect2Di FitContain(const Rect2Di& dst, int srcW, int srcH);
};

// ===== FACTORIES =====
inline std::shared_ptr<UltraCanvasVideoPlayerElement> CreateVideoPlayer(
        const std::string& identifier, long x, long y, long w, long h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasVideoPlayerElement>(identifier, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasVideoPlayerElement> CreateVideoPlayerFromFile(
        const std::string& identifier, long x, long y, long w, long h,
        const std::string& filePath) {
    auto el = CreateVideoPlayer(identifier, x, y, w, h);
    el->LoadFromFile(filePath);
    return el;
}

} // namespace UltraCanvas
