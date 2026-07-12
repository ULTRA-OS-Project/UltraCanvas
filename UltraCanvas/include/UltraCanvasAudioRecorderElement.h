// include/UltraCanvasAudioRecorderElement.h
// Composite UI control wrapping UltraCanvasAudioRecorder, built from child widgets
// (icon buttons, level meter, label, dropdown, buttons) arranged by a flex row
// layout so the framework owns alignment/centering.
// Version: 0.3.0
// Last Modified: 2026-06-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasAudioRecorder.h"
#include "UltraCanvasAudioPlayerElement.h"   // brings AudioIconButton helpers + widget headers
#include "UltraCanvasDropdown.h"
#include <algorithm>
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace UltraCanvas {

// ===== LEVEL METER / WAVEFORM (custom child) =====
// A small self-contained element that paints either a three-zone VU meter or a
// scrolling waveform strip inside its own (flex-provided) bounds. There is no
// stock widget for this, so it stays custom-drawn — but as a child element the
// flex layout owns its geometry, keeping it aligned with its siblings.
class UltraCanvasLevelMeter : public UltraCanvasUIElement {
public:
    UltraCanvasLevelMeter(const std::string& id, float w, float h)
        : UltraCanvasUIElement(id, -1, -1, w, h) {}

    void SetColors(const Color& bg, const Color& low, const Color& mid, const Color& high) {
        bgColor = bg; lowColor = low; midColor = mid; highColor = high; RequestRedraw();
    }
    void SetWaveformMode(bool on) { waveformMode = on; RequestRedraw(); }
    void SetLevel(float peak, float rms) { peakLevel = peak; rmsLevel = rms; RequestRedraw(); }
    void PushWaveformSample(float peak) {
        if (history.empty()) history.assign(kHistory, 0.0f);
        std::rotate(history.begin(), history.begin() + 1, history.end());
        history.back() = peak;
        RequestRedraw();
    }
    void Reset() {
        peakLevel = 0.0f; rmsLevel = 0.0f;
        if (history.empty()) history.assign(kHistory, 0.0f);
        std::fill(history.begin(), history.end(), 0.0f);
        RequestRedraw();
    }

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
        if (!ctx) return;
        if (waveformMode) DrawWaveform(ctx);
        else              DrawMeter(ctx);
    }

private:
    static constexpr int kHistory = 200;
    Color bgColor   = Color(220, 220, 220);
    Color lowColor  = Color(60, 180, 80);
    Color midColor  = Color(240, 200, 50);
    Color highColor = Color(220, 30, 30);
    float peakLevel = 0.0f, rmsLevel = 0.0f;
    bool  waveformMode = false;
    std::vector<float> history;

    void DrawMeter(IRenderContext* ctx) {
        Rect2Df b = GetLocalBounds();
        ctx->DrawFilledRectangle(Rect2Df(b.x, b.y, b.width, b.height),
                                 bgColor, 0.0f, Color(0, 0, 0, 0), b.height / 2);
        float peak = std::clamp(peakLevel, 0.0f, 1.0f);
        float totalW = b.width;
        float fillW = totalW * peak;
        auto zoneRect = [&](float a, float c) -> Rect2Df {
            float x0 = b.x + totalW * a;
            float x1 = b.x + totalW * c;
            float xf = b.x + fillW;
            if (xf < x0) return Rect2Df(0, 0, 0, 0);
            float xend = std::min(xf, x1);
            return Rect2Df(x0, b.y, xend - x0, b.height);
        };
        Rect2Df low  = zoneRect(0.0f, 0.6f);
        Rect2Df mid  = zoneRect(0.6f, 0.85f);
        Rect2Df high = zoneRect(0.85f, 1.0f);
        if (low.width  > 0) { ctx->SetFillPaint(lowColor);  ctx->FillRectangle(low); }
        if (mid.width  > 0) { ctx->SetFillPaint(midColor);  ctx->FillRectangle(mid); }
        if (high.width > 0) { ctx->SetFillPaint(highColor); ctx->FillRectangle(high); }
        float rms = std::clamp(rmsLevel, 0.0f, 1.0f);
        float rmsX = b.x + totalW * rms;
        ctx->SetStrokePaint(Color(40, 40, 40));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Df(rmsX, b.y), Point2Df(rmsX, b.y + b.height));
    }

    void DrawWaveform(IRenderContext* ctx) {
        Rect2Df b = GetLocalBounds();
        ctx->DrawFilledRectangle(Rect2Df(b.x, b.y, b.width, b.height),
                                 bgColor, 0.0f, Color(0, 0, 0, 0), 4);
        if (history.empty()) return;
        int n = static_cast<int>(history.size());
        float midY = b.y + b.height / 2;
        float halfH = b.height / 2 - 2;
        float barW = b.width / n;
        ctx->SetFillPaint(lowColor);
        for (int i = 0; i < n; ++i) {
            float v = std::clamp(history[i], 0.0f, 1.0f);
            float hh = halfH * v;
            if (hh <= 0) continue;
            float x = b.x + i * barW;
            ctx->FillRectangle(Rect2Df(x, midY - hh, std::max(1.0f, barW - 1), hh * 2));
        }
    }
};

// ===== VISUAL STYLE =====
struct AudioRecorderStyle {
    Color backgroundColor = Color(245, 245, 245);
    Color borderColor = Color(200, 200, 200);
    Color recordButtonColor = Color(220, 30, 30);
    Color recordingPulseColor = Color(255, 80, 80);
    Color levelMeterBgColor = Color(220, 220, 220);
    Color levelMeterLowColor = Color(60, 180, 80);
    Color levelMeterMidColor = Color(240, 200, 50);
    Color levelMeterHighColor = Color(220, 30, 30);  // clipping zone
    Color textColor = Color(40, 40, 40);
    Color iconColor = Color(60, 60, 60);
    Color disabledColor = Color(180, 180, 180);

    int   cornerRadius = 4;
    int   buttonSize = 36;
    int   meterHeight = 10;

    bool  showDeviceSelect = true;
    bool  showGainSlider = false;
    bool  showWaveform = false;          // Scrolling live waveform strip
    bool  showElapsedTime = true;
    bool  showSaveDiscard = true;        // Buttons appear after Stop()
    bool  showEmbeddedPlayer = false;    // Preview just-recorded audio inline
    bool  compact = false;
};

// ===== AUDIO RECORDER UI ELEMENT =====
class UltraCanvasAudioRecorderElement : public UltraCanvasContainer {
public:
    UltraCanvasAudioRecorderElement(const std::string& identifier = "AudioRecorder",
                                    float x = 0, float y = 0, float w = 400, float h = 72);
    ~UltraCanvasAudioRecorderElement() override;

    // ===== TRANSPORT (proxies to underlying recorder) =====
    void StartRecording();
    void PauseRecording();
    void ResumeRecording();
    void StopRecording();
    void ToggleRecording();

    // ===== OUTPUT =====
    std::shared_ptr<UCAudio> TakeBuffer();
    bool SaveToFile(const std::string& filePath,
                    AudioFormat format = AudioFormat::WAV);
    void DiscardRecording();

    // Opens the platform's native save dialog (via UltraCanvasFileLoader),
    // pre-filled with sensible WAV filters, and writes the captured buffer
    // to the chosen path. Async: result delivered via onSaved / onSaveCancelled.
    void ShowSaveDialog();

    // ===== CONFIG =====
    void SetCaptureConfig(const AudioCaptureConfig& cfg);
    const AudioCaptureConfig& GetCaptureConfig() const;
    void SetInputDevice(const std::string& deviceId);

    // ===== STYLE =====
    void SetStyle(const AudioRecorderStyle& s);
    const AudioRecorderStyle& GetStyle() const { return style; }

    // ===== ACCESS =====
    std::shared_ptr<UltraCanvasAudioRecorder> GetRecorder() const { return recorder; }
    std::shared_ptr<UltraCanvasAudioPlayerElement> GetEmbeddedPlayer() const { return embeddedPlayer; }

    // ===== EVENTS (forwarded) =====
    std::function<void()> onRecordStarted;
    std::function<void()> onRecordStopped;
    std::function<void(std::shared_ptr<UCAudio>)> onBufferReady;
    std::function<void(const std::string& filePath)> onSaved;
    std::function<void()> onSaveCancelled;
    std::function<void()> onDiscarded;
    std::function<void(MicrophonePermission)> onPermissionChanged;

    // ===== UIElement OVERRIDES =====
    // Render paints the background panel, then the container renders the child
    // widgets. Events are routed to children by UltraCanvasContainer (the device
    // dropdown opens its own popup), so no OnEvent override is needed.
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

private:
    std::shared_ptr<UltraCanvasAudioRecorder> recorder;
    std::shared_ptr<UltraCanvasAudioPlayerElement> embeddedPlayer;
    AudioRecorderStyle style;

    // Child widgets (composited UI, arranged by a flex row). The record/pause
    // buttons are plain UltraCanvasButtons carrying SVG icons (record/stop,
    // pause/resume) tinted via the icon-as-mask path.
    std::shared_ptr<UltraCanvasButton>      recordButton;
    std::shared_ptr<UltraCanvasButton>      pauseButton;
    std::shared_ptr<UltraCanvasLabel>       timeLabel;
    std::shared_ptr<UltraCanvasLevelMeter>  levelMeter;
    std::shared_ptr<UltraCanvasSlider>      gainSlider;
    std::shared_ptr<UltraCanvasDropdown>    deviceDropdown;
    std::shared_ptr<UltraCanvasButton>      saveButton;
    std::shared_ptr<UltraCanvasButton>      discardButton;

    bool    pulsePhase = false;   // recording-indicator blink phase
    TimerId elapsedTimerId = InvalidTimerId;

    void BuildChildren();
    void ApplyStyleToChildren();   // visibility + colors derived from `style`
    void PopulateDevices();
    void UpdateRecordIcon();
    void UpdatePauseButton();
    void SyncElapsedAndButtons();
    void StartElapsedTimer();
    void StopElapsedTimer();
    void HookRecorderCallbacks();
    void RefreshEmbeddedPlayer();
    static std::string FormatTime(double seconds);
};

// ===== FACTORIES =====
inline std::shared_ptr<UltraCanvasAudioRecorderElement> CreateAudioRecorder(
        const std::string& identifier, float x, float y, float w, float h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasAudioRecorderElement>(identifier, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasAudioRecorderElement> CreateCompactAudioRecorder(
        const std::string& identifier, float x, float y, float w, float h) {
    auto el = CreateAudioRecorder(identifier, x, y, w, h);
    AudioRecorderStyle s = el->GetStyle();
    s.compact = true;
    s.showDeviceSelect = false;
    s.showGainSlider = false;
    s.showSaveDiscard = false;
    el->SetStyle(s);
    return el;
}

inline std::shared_ptr<UltraCanvasAudioRecorderElement> CreateAudioRecorderWithPlayback(
        const std::string& identifier, float x, float y, float w, float h) {
    auto el = CreateAudioRecorder(identifier, x, y, w, h);
    AudioRecorderStyle s = el->GetStyle();
    s.showEmbeddedPlayer = true;
    el->SetStyle(s);
    return el;
}

} // namespace UltraCanvas
