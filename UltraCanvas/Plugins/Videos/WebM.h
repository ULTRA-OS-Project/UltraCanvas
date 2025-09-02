// WebM.h
// WebM video format support with playback controls and rendering
// Version: 1.0.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <memory>
#include <functional>
#include <cstdint>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasDrawingSurface;
struct UltraCanvasRect;
struct UltraCanvasPoint;

// WebM video playback states
enum class WebMPlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    Error
};

// WebM video information structure
struct WebMVideoInfo {
    uint32_t Width;
    uint32_t Height;
    double Duration;         // Duration in seconds
    double FrameRate;        // Frames per second
    uint32_t BitRate;        // Bits per second
    bool HasAudio;           // Audio track present
    std::string CodecName;   // VP8, VP9, AV1
    std::string AudioCodec;  // Vorbis, Opus
};

// WebM playback control options
struct WebMPlaybackOptions {
    bool Loop = false;
    bool AutoPlay = false;
    double Volume = 1.0;     // 0.0 to 1.0
    bool Muted = false;
    double PlaybackSpeed = 1.0; // 0.25 to 4.0
    bool EnableHardwareAcceleration = true;
};

// WebM video event callbacks
using WebMVideoEventCallback = std::function<void(WebMPlaybackState state)>;
using WebMVideoProgressCallback = std::function<void(double currentTime, double duration)>;
using WebMVideoErrorCallback = std::function<void(const std::string& errorMessage)>;

// Main WebM video player class
class UltraCanvasWebMVideo {
public:
    // Constructor and destructor
    UltraCanvasWebMVideo();
    ~UltraCanvasWebMVideo();

    // File operations
    bool LoadFromFile(const std::string& filePath);
    bool LoadFromMemory(const uint8_t* data, size_t dataSize);
    bool LoadFromUrl(const std::string& url);
    void UnloadVideo();

    // Playback controls
    bool Play();
    bool Pause();
    bool Stop();
    bool SeekToTime(double timeSeconds);
    bool SeekToFrame(uint32_t frameNumber);

    // Video information
    const WebMVideoInfo& GetVideoInfo() const;
    WebMPlaybackState GetPlaybackState() const;
    double GetCurrentTime() const;
    double GetDuration() const;
    uint32_t GetCurrentFrame() const;
    uint32_t GetTotalFrames() const;

    // Playback options
    void SetPlaybackOptions(const WebMPlaybackOptions& options);
    const WebMPlaybackOptions& GetPlaybackOptions() const;
    
    // Volume and audio controls
    void SetVolume(double volume);
    double GetVolume() const;
    void SetMuted(bool muted);
    bool IsMuted() const;

    // Speed control
    void SetPlaybackSpeed(double speed);
    double GetPlaybackSpeed() const;

    // Rendering
    bool RenderToSurface(UltraCanvasDrawingSurface* surface, 
                        const UltraCanvasRect& destinationRect);
    bool RenderToSurface(UltraCanvasDrawingSurface* surface, 
                        const UltraCanvasPoint& position);
    
    // Frame extraction
    bool ExtractFrameAtTime(double timeSeconds, UltraCanvasDrawingSurface* outputSurface);
    bool ExtractCurrentFrame(UltraCanvasDrawingSurface* outputSurface);
    
    // Thumbnail generation
    bool GenerateThumbnail(UltraCanvasDrawingSurface* thumbnailSurface, 
                          uint32_t thumbnailWidth, uint32_t thumbnailHeight,
                          double timeSeconds = 0.0);

    // Event callbacks
    void SetStateChangeCallback(WebMVideoEventCallback callback);
    void SetProgressCallback(WebMVideoProgressCallback callback);
    void SetErrorCallback(WebMVideoErrorCallback callback);

    // Static utility functions
    static bool IsWebMFile(const std::string& filePath);
    static bool ValidateWebMData(const uint8_t* data, size_t dataSize);
    static std::string GetWebMFormatInfo(const std::string& filePath);

    // Subtitle support (if available in WebM)
    bool HasSubtitles() const;
    void EnableSubtitles(bool enable);
    bool AreSubtitlesEnabled() const;
    void SetSubtitleTrack(uint32_t trackIndex);
    uint32_t GetSubtitleTrackCount() const;

    // Chapter support
    bool HasChapters() const;
    uint32_t GetChapterCount() const;
    std::string GetChapterName(uint32_t chapterIndex) const;
    double GetChapterTime(uint32_t chapterIndex) const;
    bool SeekToChapter(uint32_t chapterIndex);

private:
    struct WebMVideoImpl;
    std::unique_ptr<WebMVideoImpl> m_Impl;

    // Private helper methods
    void InitializeDecoder();
    void CleanupDecoder();
    bool UpdateVideoFrame();
    void NotifyStateChange(WebMPlaybackState newState);
    void NotifyProgress(double currentTime);
    void NotifyError(const std::string& error);
};

// WebM video factory for plugin system integration
class UltraCanvasWebMVideoFactory {
public:
    static std::unique_ptr<UltraCanvasWebMVideo> CreateWebMPlayer();
    static bool RegisterWebMSupport();
    static void UnregisterWebMSupport();
    static std::string GetSupportedExtensions();
    static std::string GetSupportedMimeTypes();
};

} // namespace UltraCanvas
