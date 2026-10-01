// include/UltraCanvasAudioPlayer.h
// Non-visual cross-platform audio playback engine
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASAUDIOPLAYER_H
#define ULTRACANVASAUDIOPLAYER_H

#include "UltraCanvasAudio.h"
#include "UltraCanvasAudioStreaming.h"
#include <string>
#include <memory>
#include <functional>
#include <cstdint>

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

// ===== STREAMING SINK CONFIG =====
// A sink plays PCM the caller pushes as it is produced — a decoded call,
// synthesised speech, a network stream — instead of a file or buffer loaded
// up front. The device clock pulls from a bounded ring; the caller keeps it
// topped up and reads GetSinkQueuedSeconds() to pace itself.
struct AudioSinkConfig {
    int sampleRate = 48000;
    int channels = 1;
    int bufferMs = 200;      // Ring capacity. The upper bound on added latency;
                             // frames pushed beyond it are dropped and counted.
};

// ===== AUDIO PLAYER (NON-VISUAL) =====
// Owns a backend stream and pumps audio from a UCAudio buffer, a file, or a
// streaming sink the caller pushes frames into.
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

    // ===== STREAMING SINK =====
    // OpenSink replaces any loaded source, opens the output device at the
    // sink's rate and channel count and starts playing at once; Pause / Play
    // / Stop then apply to the sink, Seek does not. CloseSink (or Unload)
    // releases the device. Push from any one thread; the audio thread pops.
    bool OpenSink(const AudioSinkConfig& cfg);
    bool IsSinkOpen() const;
    // Interleaved float PCM at the sink's rate and channel count. Returns
    // the frames queued; fewer than `frames` means the ring was full and the
    // rest were dropped (see GetSinkDroppedFrames).
    size_t PushSinkFrames(const float* interleaved, size_t frames);
    size_t PushSinkFrames(const AudioLiveFrame& frame) {
        return PushSinkFrames(frame.samples, frame.frameCount);
    }
    size_t GetSinkQueuedFrames() const;
    double GetSinkQueuedSeconds() const;
    void   ClearSink();                   // Drop queued frames (a seek, a hang-up)
    void   CloseSink();
    uint64_t GetSinkUnderrunCount() const; // Device callbacks that found the ring short
    uint64_t GetSinkDroppedFrames() const; // Pushed frames that did not fit

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
    // Sink only: the device asked for frames the ring did not have; silence
    // was played in their place. Once per underrun episode, audio thread.
    std::function<void()> onSinkUnderrun;

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
