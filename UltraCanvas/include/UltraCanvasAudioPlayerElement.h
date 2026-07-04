// include/UltraCanvasAudioPlayerElement.h
// Composite UI control wrapping UltraCanvasAudioPlayer, built from child widgets
// (icon buttons, sliders, label) arranged by a flex row layout so the framework
// owns alignment/centering instead of hand-computed draw offsets.
// Version: 0.3.0
// Last Modified: 2026-06-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasAudioPlayer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== TRANSPORT ICON HELPERS (shared by player & recorder) =====
// Resolve a transport icon (SVG) under the framework resources dir.
inline std::string AudioIconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + name);
}

// Build a borderless, icon-only button whose SVG icon is tinted via the
// icon-as-mask path (the mask colour follows the button's text colour, so
// re-tinting later is just SetTextColors()).
inline std::shared_ptr<UltraCanvasButton> MakeAudioIconButton(
        const std::string& id, int boxSize, int iconSize,
        const std::string& svg, const Color& tint) {
    auto b = std::make_shared<UltraCanvasButton>(id, -1.0f, -1.0f,
                                                 static_cast<float>(boxSize),
                                                 static_cast<float>(boxSize), "");
    ButtonStyle st = b->GetStyle();
    st.normalColor      = Color(0, 0, 0, 0);    // borderless / transparent
    st.hoverColor       = Color(0, 0, 0, 20);   // subtle hover affordance
    st.pressedColor     = Color(0, 0, 0, 35);
    st.disabledColor    = Color(0, 0, 0, 0);
    st.borderWidth      = 0.0f;
    st.useIconAsMask    = true;                 // tint the icon with the text colour
    st.normalTextColor  = tint;
    st.hoverTextColor   = tint;
    st.pressedTextColor = tint;
    b->SetStyle(st);
    b->SetIcon(AudioIconPath(svg));
    b->SetIconSize(iconSize, iconSize);
    b->SetIconPosition(ButtonIconPosition::Center);
    b->SetAcceptsFocus(false);
    return b;
}

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
class UltraCanvasAudioPlayerElement : public UltraCanvasContainer {
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
    // Render paints the background panel, then the container renders the child
    // widgets. Events are routed to the children by UltraCanvasContainer, so no
    // OnEvent override / manual hit-testing is needed.
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

private:
    std::shared_ptr<UltraCanvasAudioPlayer> player;
    AudioPlayerStyle style;
    std::string trackTitle;

    // Child widgets (composited UI, arranged by a flex row). The transport
    // buttons are plain UltraCanvasButtons carrying SVG icons (play/pause/stop,
    // volume on/off) tinted via the icon-as-mask path.
    std::shared_ptr<UltraCanvasButton> playButton;
    std::shared_ptr<UltraCanvasButton> stopButton;
    std::shared_ptr<UltraCanvasSlider> seekSlider;
    std::shared_ptr<UltraCanvasLabel>  timeLabel;
    std::shared_ptr<UltraCanvasButton> muteButton;
    std::shared_ptr<UltraCanvasSlider> volumeSlider;

    TimerId posTimerId = InvalidTimerId;
    // Guards the SetValue()->onValueChanged feedback loop when we push playback
    // position into the seek slider (see UltraCanvasSlider::SetValue).
    bool suppressSeekCallback = false;

    void BuildChildren();
    void ApplyStyleToChildren();   // visibility + colors derived from `style`
    void UpdatePlayIcon();
    void UpdateMuteIcon();
    void SyncTimeAndSeek();
    void StartPosTimer();
    void StopPosTimer();
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
