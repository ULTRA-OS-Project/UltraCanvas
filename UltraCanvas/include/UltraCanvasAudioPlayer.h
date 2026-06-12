// include/UltraCanvasAudioPlayer.h
// Non-visual cross-platform audio playback engine
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASAUDIOPLAYER_H
#define ULTRACANVASAUDIOPLAYER_H

#include "UltraCanvasAudio.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== PLAYBACK STATE =====
enum class AudioPlaybackState {
    Idle,         // No source loaded
    Loading,      // Source loading / decoding
    Stopped,      // Source loaded but not playing
    Playing,
    Paused,
    Buffering,    // Stream underrun
    Error
};

// ===== PLAYBACK CONFIG =====
struct AudioPlaybackConfig {
    std::string deviceId;          // empty = system default output
    float volume = 1.0f;           // 0..1 linear
    float playbackRate = 1.0f;     // 0.25..4.0
    bool  loop = false;
    bool  mute = false;
    int   positionUpdateHz = 10;   // Frequency of onPositionChanged
};

// ===== AUDIO PLAYER (NON-VISUAL) =====
// Owns a backend stream and pumps audio from a UCAudio buffer or a file.
class UltraCanvasAudioPlayer {
public:
    UltraCanvasAudioPlayer();
    explicit UltraCanvasAudioPlayer(const AudioPlaybackConfig& cfg);
    ~UltraCanvasAudioPlayer();

    UltraCanvasAudioPlayer(const UltraCanvasAudioPlayer&) = delete;
    UltraCanvasAudioPlayer& operator=(const UltraCanvasAudioPlayer&) = delete;

    // ===== SOURCE =====
    bool LoadFromFile(const std::string& filePath);
    bool LoadFromAudio(std::shared_ptr<UCAudio> audio);
    void Unload();

    // ===== TRANSPORT =====
    bool Play();
    bool Pause();
    bool Stop();
    bool Seek(double seconds);

    // ===== STATE =====
    AudioPlaybackState GetState() const;
    double GetPosition() const;          // seconds
    double GetDuration() const;          // seconds
    bool   IsPlaying() const { return GetState() == AudioPlaybackState::Playing; }
    const std::string& GetLastError() const;

    // ===== PROPERTIES =====
    void SetVolume(float v);             // 0..1, clamped
    float GetVolume() const;
    void SetMute(bool mute);
    bool IsMuted() const;
    void SetLoop(bool loop);
    bool IsLoop() const;
    void SetPlaybackRate(float rate);    // 1.0 = normal
    float GetPlaybackRate() const;
    void SetOutputDevice(const std::string& deviceId);

    // ===== EVENTS =====
    std::function<void()> onLoaded;
    std::function<void(AudioPlaybackState)> onPlaybackStateChanged;
    std::function<void(double seconds)> onPositionChanged;
    std::function<void()> onEnded;
    std::function<void(const std::string& message)> onError;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasAudioPlayer> CreateAudioPlayer() {
    return std::make_shared<UltraCanvasAudioPlayer>();
}

inline std::shared_ptr<UltraCanvasAudioPlayer> CreateAudioPlayerFromFile(const std::string& path) {
    auto p = std::make_shared<UltraCanvasAudioPlayer>();
    p->LoadFromFile(path);
    return p;
}

} // namespace UltraCanvas

#endif // ULTRACANVASAUDIOPLAYER_H
