// core/UltraCanvasAudioRecorderElement.cpp
// Composite UI element. Render layout is sketched here; pixel-accurate drawing
// (icons, VU meter shading, waveform strip) will follow.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioRecorderElement.h"
#include <cstdio>

namespace UltraCanvas {

UltraCanvasAudioRecorderElement::UltraCanvasAudioRecorderElement(
        const std::string& identifier, long x, long y, long w, long h)
    : UltraCanvasUIElement(identifier, static_cast<int>(x), static_cast<int>(y),
                           static_cast<int>(w), static_cast<int>(h)) {
    recorder = std::make_shared<UltraCanvasAudioRecorder>();
    HookRecorderCallbacks();
    Relayout();
}

UltraCanvasAudioRecorderElement::~UltraCanvasAudioRecorderElement() = default;

void UltraCanvasAudioRecorderElement::StartRecording() {
    if (recorder->Start()) {
        if (onRecordStarted) onRecordStarted();
        RequestRedraw();
    }
}

void UltraCanvasAudioRecorderElement::PauseRecording()  { recorder->Pause();  RequestRedraw(); }
void UltraCanvasAudioRecorderElement::ResumeRecording() { recorder->Resume(); RequestRedraw(); }

void UltraCanvasAudioRecorderElement::StopRecording() {
    recorder->Stop();
    if (onRecordStopped) onRecordStopped();
    RefreshEmbeddedPlayer();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::ToggleRecording() {
    auto state = recorder->GetState();
    if (state == AudioRecordingState::Recording) StopRecording();
    else StartRecording();
}

std::shared_ptr<UCAudio> UltraCanvasAudioRecorderElement::TakeBuffer() {
    auto a = recorder->TakeBuffer();
    if (onBufferReady) onBufferReady(a);
    return a;
}

bool UltraCanvasAudioRecorderElement::SaveToFile(const std::string& filePath, AudioFormat format) {
    bool ok = recorder->SaveToFile(filePath, format);
    if (ok && onSaved) onSaved(filePath);
    return ok;
}

void UltraCanvasAudioRecorderElement::DiscardRecording() {
    recorder->Discard();
    if (onDiscarded) onDiscarded();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::SetCaptureConfig(const AudioCaptureConfig& cfg) {
    recorder->Open(cfg);
}

const AudioCaptureConfig& UltraCanvasAudioRecorderElement::GetCaptureConfig() const {
    return recorder->GetConfig();
}

void UltraCanvasAudioRecorderElement::SetInputDevice(const std::string& deviceId) {
    recorder->SetInputDevice(deviceId);
}

void UltraCanvasAudioRecorderElement::SetStyle(const AudioRecorderStyle& s) {
    style = s;
    Relayout();
    RefreshEmbeddedPlayer();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::HookRecorderCallbacks() {
    auto self = this;
    recorder->onRecordingStateChanged = [self](AudioRecordingState) { self->RequestRedraw(); };
    recorder->onLevelChanged = [self](float peak, float rms) {
        self->lastPeakLevel = peak;
        self->lastRmsLevel = rms;
        self->RequestRedraw();
    };
    recorder->onClipping = [self]() { self->clipping = true; self->RequestRedraw(); };
    recorder->onMaxDurationReached = [self]() { self->StopRecording(); };
    recorder->onPermissionChanged = [self](MicrophonePermission p) {
        if (self->onPermissionChanged) self->onPermissionChanged(p);
        self->RequestRedraw();
    };
}

void UltraCanvasAudioRecorderElement::Relayout() {
    // TODO: compute sub-rects based on style flags (compact, showDeviceSelect, etc.)
}

void UltraCanvasAudioRecorderElement::RefreshEmbeddedPlayer() {
    if (!style.showEmbeddedPlayer) { embeddedPlayer.reset(); return; }
    // TODO: place an UltraCanvasAudioPlayerElement below the recorder UI and feed
    //       it the latest UCAudio buffer when recording stops.
}

void UltraCanvasAudioRecorderElement::Render(IRenderContext* /*ctx*/, const Rect2Di& /*dirtyRect*/) {
    // TODO: draw background, record button (with blink when recording),
    //       VU meter (low/mid/high zones), elapsed time, optional device select
    //       and gain slider, optional scrolling waveform, optional save/discard
    //       row, optional embedded player.
}

bool UltraCanvasAudioRecorderElement::OnEvent(const UCEvent& /*event*/) {
    // TODO: hit-test record/pause/save/discard buttons, gain slider, device dropdown.
    return false;
}

std::string UltraCanvasAudioRecorderElement::FormatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return std::string(buf);
}

} // namespace UltraCanvas
