// include/UltraCanvasAudioRecorderElement.h
// Composite UI control wrapping UltraCanvasAudioRecorder with record/stop, level meter, device select
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasAudioRecorder.h"
#include "UltraCanvasAudioPlayerElement.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

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
class UltraCanvasAudioRecorderElement : public UltraCanvasUIElement {
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
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    std::shared_ptr<UltraCanvasAudioRecorder> recorder;
    std::shared_ptr<UltraCanvasAudioPlayerElement> embeddedPlayer;
    AudioRecorderStyle style;

    // Cached UI rects
    Rect2Df recordButtonRect;
    Rect2Df pauseButtonRect;
    Rect2Df levelMeterRect;
    Rect2Df timeLabelRect;
    Rect2Df deviceSelectRect;
    Rect2Df gainSliderRect;
    Rect2Df saveButtonRect;
    Rect2Df discardButtonRect;
    Rect2Df waveformRect;

    // Live state for the meter / waveform
    float lastPeakLevel = 0.0f;
    float lastRmsLevel = 0.0f;
    std::vector<float> waveformHistory;  // Ring buffer of peaks
    bool  pulsePhase = false;            // Recording indicator blink
    bool  clipping = false;

    void Relayout();
    void DrawRecordButton(IRenderContext* ctx);
    void DrawLevelMeter(IRenderContext* ctx);
    void DrawTimeLabel(IRenderContext* ctx);
    void DrawWaveformStrip(IRenderContext* ctx);
    void DrawDeviceSelect(IRenderContext* ctx);
    void DrawSaveDiscardButtons(IRenderContext* ctx);
    void HookRecorderCallbacks();
    void RefreshEmbeddedPlayer();
    void OpenDevicePicker();
    static std::string FormatTime(double seconds);

    // Cached label for the device chip
    std::string currentDeviceLabel = "Default mic";
    // Held to keep the popup menu alive while it's open
    std::shared_ptr<class UltraCanvasMenu> devicePopupMenu;
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
