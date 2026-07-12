// include/UltraCanvasVideoPlayer.h
// Non-visual cross-platform video playback engine
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEOPLAYER_H
#define ULTRACANVASVIDEOPLAYER_H

#include "UltraCanvasVideo.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== PLAYBACK STATE =====
enum class VideoPlaybackState {
    Idle,         // No source loaded
    Loading,      // Source opening / prerolling
    Stopped,      // Source loaded but not playing
    Playing,
    Paused,
    Buffering,    // Stream underrun
    Error
};

// ===== PLAYBACK CONFIG =====
struct VideoPlaybackConfig {
    float volume = 1.0f;           // 0..1 linear (audio track)
    float playbackRate = 1.0f;     // 0.25..4.0
    bool  loop = false;
    bool  mute = false;
    bool  autoPlay = false;
    // Open the source without wiring its audio track at all (no audio decode,
    // no audio device opened / occupied). Stronger than mute for always-silent
    // playback such as hover previews — a muted session still opens the
    // platform audio sink, which can stall or fail on machines without a
    // running audio server. Applied when the source is opened (LoadFrom*), not
    // switchable mid-session. Backends that can't drop the audio branch treat
    // it as a hard mute.
    bool  disableAudio = false;
};

// ===== VIDEO PLAYER (NON-VISUAL) =====
// Owns a backend decode session. Audio (if present) plays through the platform
// sink for automatic A/V sync; decoded video frames are delivered via
// onFrameReady and the most recent one is cached for the UI thread.
class UltraCanvasVideoPlayer {
public:
    UltraCanvasVideoPlayer();
    explicit UltraCanvasVideoPlayer(const VideoPlaybackConfig& cfg);
    ~UltraCanvasVideoPlayer();

    UltraCanvasVideoPlayer(const UltraCanvasVideoPlayer&) = delete;
    UltraCanvasVideoPlayer& operator=(const UltraCanvasVideoPlayer&) = delete;

    // ===== SOURCE =====
    bool LoadFromFile(const std::string& filePath);
    bool LoadFromUrl(const std::string& url);
    void Unload();
    bool IsLoaded() const;

    // ===== TRANSPORT =====
    bool Play();
    bool Pause();
    bool Stop();
    bool Seek(double seconds);

    // ===== STATE =====
    VideoPlaybackState GetState() const;
    double GetPosition() const;          // seconds
    double GetDuration() const;          // seconds
    bool   IsPlaying() const { return GetState() == VideoPlaybackState::Playing; }
    const VideoStreamInfo& GetStreamInfo() const;
    const std::string& GetLastError() const;

    // ===== FRAME ACCESS (UI thread) =====
    // Returns the most recently decoded frame, or null if none yet. Thread-safe.
    UCVideoFramePtr GetCurrentFrame() const;

    // ===== PROPERTIES =====
    void SetVolume(float v);             // 0..1, clamped
    float GetVolume() const;
    void SetMute(bool mute);
    bool IsMuted() const;
    void SetLoop(bool loop);
    bool IsLoop() const;
    void SetPlaybackRate(float rate);
    float GetPlaybackRate() const;

    // ===== EVENTS =====
    std::function<void()> onLoaded;
    std::function<void(VideoPlaybackState)> onPlaybackStateChanged;
    std::function<void(UCVideoFramePtr)> onFrameReady;   // a new frame is available
    std::function<void(double seconds)> onPositionChanged;
    std::function<void()> onEnded;
    std::function<void(const std::string& message)> onError;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasVideoPlayer> CreateVideoPlayerEngine() {
    return std::make_shared<UltraCanvasVideoPlayer>();
}

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEOPLAYER_H
