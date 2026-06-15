// include/UltraCanvasAudioPlayerElement.h
// Composite UI control wrapping UltraCanvasAudioPlayer with play/pause, seek, volume
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasAudioPlayer.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== VISUAL STYLE =====
struct AudioPlayerStyle {
    Color backgroundColor = Color(245, 245, 245);
    Color borderColor = Color(200, 200, 200);
    Color progressTrackColor = Color(220, 220, 220);
    Color progressFillColor = Color(0, 120, 215);
    Color knobColor = Colors::White;
    Color knobBorderColor = Color(100, 100, 100);
    Color textColor = Color(40, 40, 40);
    Color iconColor = Color(60, 60, 60);
    Color iconHoverColor = Color(0, 120, 215);

    int   cornerRadius = 4;
    int   buttonSize = 32;
    int   sliderHeight = 6;
    bool  showVolumeSlider = true;
    bool  showTimeLabels = true;
    bool  showLoopButton = false;
    bool  showWaveform = false;
    bool  compact = false;             // Icon-only layout
};

// ===== AUDIO PLAYER UI ELEMENT =====
class UltraCanvasAudioPlayerElement : public UltraCanvasUIElement {
public:
    UltraCanvasAudioPlayerElement(const std::string& identifier = "AudioPlayer",
                                  long x = 0, long y = 0, long w = 400, long h = 56);
    ~UltraCanvasAudioPlayerElement() override;

    // ===== SOURCE =====
    bool LoadFromFile(const std::string& filePath);
    bool LoadFromAudio(std::shared_ptr<UCAudio> audio);

    // Human-readable reason for the most recent failed load (forwarded from the
    // underlying player): e.g. file locked, missing, or unsupported format.
    const std::string& GetLastError() const { return player->GetLastError(); }

    // Opens the platform's native open dialog (via UltraCanvasFileLoader)
    // pre-filled with audio format filters and loads the chosen file.
    // Async: result delivered via onLoaded / onLoadCancelled.
    void ShowOpenDialog();

    // ===== TRANSPORT (proxies to underlying player) =====
    void Play();
    void Pause();
    void Stop();
    void Seek(double seconds);
    void TogglePlayPause();

    // ===== STYLE =====
    void SetStyle(const AudioPlayerStyle& s);
    const AudioPlayerStyle& GetStyle() const { return style; }
    void SetTrackTitle(const std::string& title);
    const std::string& GetTrackTitle() const { return trackTitle; }

    // ===== ACCESS =====
    std::shared_ptr<UltraCanvasAudioPlayer> GetPlayer() const { return player; }

    // ===== EVENTS (forwarded from player) =====
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
    std::shared_ptr<UltraCanvasAudioPlayer> player;
    AudioPlayerStyle style;
    std::string trackTitle;

    // Cached hit-test regions (recomputed on layout change)
    Rect2Di playButtonRect;
    Rect2Di stopButtonRect;
    Rect2Di seekBarRect;
    Rect2Di volumeBarRect;
    Rect2Di muteButtonRect;
    Rect2Di loopButtonRect;
    Rect2Di timeLabelRect;

    bool draggingSeek = false;
    bool draggingVolume = false;
    bool hoverPlay = false;

    void Relayout();
    void DrawTransportButtons(IRenderContext* ctx);
    void DrawSeekBar(IRenderContext* ctx);
    void DrawVolumeBar(IRenderContext* ctx);
    void DrawTimeLabels(IRenderContext* ctx);
    void HookPlayerCallbacks();
    static std::string FormatTime(double seconds);
};

// ===== FACTORIES =====
inline std::shared_ptr<UltraCanvasAudioPlayerElement> CreateAudioPlayer(
        const std::string& identifier, long x, long y, long w, long h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasAudioPlayerElement>(identifier, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasAudioPlayerElement> CreateAudioPlayerFromFile(
        const std::string& identifier, long x, long y, long w, long h,
        const std::string& filePath) {
    auto el = CreateAudioPlayer(identifier, x, y, w, h);
    el->LoadFromFile(filePath);
    return el;
}

inline std::shared_ptr<UltraCanvasAudioPlayerElement> CreateCompactAudioPlayer(
        const std::string& identifier, long x, long y, long w, long h) {
    auto el = CreateAudioPlayer(identifier, x, y, w, h);
    AudioPlayerStyle s = el->GetStyle();
    s.compact = true;
    s.showTimeLabels = false;
    s.showVolumeSlider = false;
    el->SetStyle(s);
    return el;
}

} // namespace UltraCanvas
