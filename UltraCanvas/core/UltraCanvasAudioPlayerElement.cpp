// core/UltraCanvasAudioPlayerElement.cpp
// Composite UI element. Render layout is sketched here; pixel-accurate drawing
// (icons, slider knobs, hit testing) will follow the same patterns as
// UltraCanvasSlider / UltraCanvasButton.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioPlayerElement.h"
#include <cstdio>

namespace UltraCanvas {

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
    // TODO: compute playButtonRect / seekBarRect / volumeBarRect / timeLabelRect
    //       based on style.compact, style.showVolumeSlider, style.showTimeLabels.
    //       For now hit-test rects remain default-constructed.
}

void UltraCanvasAudioPlayerElement::Render(IRenderContext* /*ctx*/, const Rect2Di& /*dirtyRect*/) {
    // TODO: draw background, transport buttons, seek bar w/ progress, volume bar,
    //       time labels, optional title and waveform.
}

bool UltraCanvasAudioPlayerElement::OnEvent(const UCEvent& /*event*/) {
    // TODO: hit-test against cached rects; toggle play/pause, drag seek/volume.
    return false;
}

std::string UltraCanvasAudioPlayerElement::FormatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return std::string(buf);
}

} // namespace UltraCanvas
