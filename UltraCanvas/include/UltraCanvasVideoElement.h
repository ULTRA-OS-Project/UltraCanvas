// UltraCanvasVideoElement.h
// Video playback component with controls, streaming support, and multiple format handling
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include <functional>
#include <string>
#include <vector>
#include <chrono>
#include <memory>

namespace UltraCanvas {

// ===== VIDEO FORMAT DEFINITIONS =====
enum class VideoFormat {
    MP4,            // MPEG-4 Part 14
    AVI,            // Audio Video Interleave
    MOV,            // QuickTime Movie
    WMV,            // Windows Media Video
    MKV,            // Matroska Video
    WEBM,           // WebM
    FLV,            // Flash Video
    M4V,            // iTunes Video
    _3GP,           // 3GPP
    OGV,            // Ogg Video
    HEVC,           // High Efficiency Video Coding (H.265)
    VP9,            // VP9 Codec
    AV1,            // AV1 Codec
    Unknown         // Unrecognized format
};

enum class VideoCodec {
    H264,           // H.264/AVC
    H265,           // H.265/HEVC
    VP8,            // VP8
    VP9,            // VP9
    AV1,            // AV1
    MPEG4,          // MPEG-4
    MPEG2,          // MPEG-2
    Theora,         // Theora
    Unknown
};

enum class AudioCodec {
    AAC,            // Advanced Audio Coding
    MP3,            // MPEG Audio Layer III
    Vorbis,         // Ogg Vorbis
    Opus,           // Opus
    FLAC,           // Free Lossless Audio Codec
    PCM,            // Pulse Code Modulation
    AC3,            // Dolby Digital
    Unknown
};

enum class VideoState {
    Stopped,        // Video is stopped
    Playing,        // Video is playing
    Paused,         // Video is paused
    Buffering,      // Video is buffering
    Loading,        // Video is loading
    Error,          // Error occurred
    EndOfMedia      // Reached end of video
};

enum class VideoScaleMode {
    NoScale,           // Original size
    Stretch,        // Stretch to fit (may distort)
    Uniform,        // Scale uniformly (maintain aspect ratio)
    UniformToFill,  // Scale to fill (may crop)
    Center,         // Center in available space
    Zoom            // Zoom to fit with cropping
};

enum class VideoQuality {
    Auto,           // Automatic quality selection
    Low,            // 480p or lower
    Medium,         // 720p
    High,           // 1080p
    Ultra,          // 4K or higher
    Custom          // Custom bitrate/resolution
};

// ===== VIDEO INFORMATION STRUCTURES =====
struct VideoMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string description;
    std::string copyright;
    int year = 0;
    float duration = 0.0f;          // Duration in seconds
    
    // Technical information
    int width = 0;
    int height = 0;
    float frameRate = 0.0f;
    int bitrate = 0;                // In kbps
    VideoCodec videoCodec = VideoCodec::Unknown;
    AudioCodec audioCodec = AudioCodec::Unknown;
    
    // File information
    std::string fileName;
    std::string filePath;
    size_t fileSize = 0;            // In bytes
    VideoFormat format = VideoFormat::Unknown;
    
    bool hasVideo = true;
    bool hasAudio = true;
    bool hasSubtitles = false;
};

struct VideoFrame {
    std::vector<uint8_t> data;      // Frame pixel data (RGBA format)
    int width = 0;
    int height = 0;
    float timestamp = 0.0f;         // Timestamp in seconds
    int frameNumber = 0;
    bool isKeyFrame = false;
};

struct SubtitleTrack {
    std::string language;
    std::string title;
    std::string filePath;           // Path to subtitle file (.srt, .vtt, etc.)
    bool isEnabled = false;
};

struct AudioTrack {
    std::string language;
    std::string title;
    AudioCodec codec = AudioCodec::Unknown;
    int channels = 2;               // Number of audio channels
    int sampleRate = 44100;         // Sample rate in Hz
    bool isEnabled = true;
};

// ===== VIDEO CONTROLS STYLING =====
struct VideoControlsStyle {
    bool showControls = true;
    bool autoHideControls = true;
    float autoHideDelay = 3.0f;     // Seconds of inactivity before hiding
    
    Color backgroundColor = Color(0, 0, 0, 180);
    Color buttonColor = Colors::White;
    Color buttonHoverColor = Color(200, 200, 200);
    Color sliderColor = Color(255, 255, 255, 200);
    Color progressColor = Color(50, 150, 250);
    Color textColor = Colors::White;
    
    float controlsHeight = 60.0f;
    float buttonSize = 40.0f;
    float spacing = 8.0f;
    float cornerRadius = 4.0f;
    
    static VideoControlsStyle Default();
    static VideoControlsStyle Minimal();
    static VideoControlsStyle Dark();
    static VideoControlsStyle Light();
};

// ===== VIDEO PLUGIN INTERFACE =====
class IVideoPlugin {
public:
    virtual ~IVideoPlugin() = default;
    
    // Plugin information
    virtual std::string GetPluginName() = 0;
    virtual std::string GetPluginVersion() = 0;
    virtual std::vector<VideoFormat> GetSupportedFormats() = 0;
    virtual bool CanHandle(const std::string& filePath) = 0;
    virtual bool CanHandle(VideoFormat format) = 0;
    
    // Video loading and playback
    virtual bool LoadVideo(const std::string& filePath) = 0;
    virtual bool LoadFromStream(const std::string& url) = 0;
    virtual VideoMetadata GetMetadata() = 0;
    virtual bool IsLoaded() = 0;
    
    // Playback control
    virtual bool Play() = 0;
    virtual bool Pause() = 0;
    virtual bool Stop() = 0;
    virtual bool Seek(float position) = 0;  // Position in seconds
    virtual float GetPosition() = 0;         // Current position in seconds
    virtual float GetDuration() = 0;         // Total duration in seconds
    virtual VideoState GetState() = 0;
    
    // Frame access
    virtual VideoFrame GetCurrentFrame() = 0;
    virtual bool HasNewFrame() = 0;
    
    // Audio/Video settings
    virtual void SetVolume(float volume) = 0;    // 0.0 to 1.0
    virtual float GetVolume() = 0;
    virtual void SetMuted(bool muted) = 0;
    virtual bool IsMuted() = 0;
    virtual void SetPlaybackRate(float rate) = 0;  // 0.5 = half speed, 2.0 = double speed
    virtual float GetPlaybackRate() = 0;
    
    // Track management
    virtual std::vector<AudioTrack> GetAudioTracks() = 0;
    virtual std::vector<SubtitleTrack> GetSubtitleTracks() = 0;
    virtual void SetActiveAudioTrack(int index) = 0;
    virtual void SetActiveSubtitleTrack(int index) = 0;
    
    // Advanced features
    virtual bool SupportsStreaming() = 0;
    virtual bool SupportsHardwareAcceleration() = 0;
    virtual void SetVideoQuality(VideoQuality quality) = 0;
    virtual VideoQuality GetVideoQuality() = 0;
};

// ===== MAIN VIDEO ELEMENT COMPONENT =====
class UltraCanvasVideoElement : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== VIDEO STATE =====
    std::shared_ptr<IVideoPlugin> videoPlugin;
    VideoMetadata metadata;
    VideoFrame currentFrame;
    VideoState currentState = VideoState::Stopped;
    
    // ===== PLAYBACK PROPERTIES =====
    std::string currentFilePath;
    std::string currentStreamUrl;
    float volume = 1.0f;
    bool isMuted = false;
    float playbackRate = 1.0f;
    VideoScaleMode scaleMode = VideoScaleMode::Uniform;
    VideoQuality quality = VideoQuality::Auto;
    
    // ===== DISPLAY PROPERTIES =====
    bool showControls = true;
    bool controlsVisible = true;
    VideoControlsStyle controlsStyle;
    std::chrono::steady_clock::time_point lastInteraction;
    
    // ===== CONTROL ELEMENTS =====
    std::shared_ptr<UltraCanvasButton> playPauseButton;
    std::shared_ptr<UltraCanvasButton> stopButton;
    std::shared_ptr<UltraCanvasButton> fullscreenButton;
    std::shared_ptr<UltraCanvasButton> muteButton;
    std::shared_ptr<UltraCanvasSlider> progressSlider;
    std::shared_ptr<UltraCanvasSlider> volumeSlider;
    
    // ===== LAYOUT CACHE =====
    Rect2D videoRect;
    Rect2D controlsRect;
    bool layoutDirty = true;
    bool frameUpdateNeeded = true;
    
    // ===== TRACKS =====
    std::vector<AudioTrack> audioTracks;
    std::vector<SubtitleTrack> subtitleTracks;
    int activeAudioTrack = 0;
    int activeSubtitleTrack = -1;   // -1 = disabled
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasVideoElement(const std::string& identifier = "VideoElement", 
                           long id = 0, long x = 0, long y = 0, long w = 640, long h = 480)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object2D;
        
        // Initialize controls style
        controlsStyle = VideoControlsStyle::Default();
        
        // Initialize timing
        lastInteraction = std::chrono::steady_clock::now();
        
        // Create control elements
        CreateControls();
    }

    // ===== VIDEO LOADING =====
    bool LoadFromFile(const std::string& filePath) {
        // Find appropriate plugin for this file
        videoPlugin = FindVideoPlugin(filePath);
        if (!videoPlugin) {
            SetError("No suitable video plugin found for: " + filePath);
            return false;
        }
        
        currentFilePath = filePath;
        currentStreamUrl.clear();
        
        if (videoPlugin->LoadVideo(filePath)) {
            metadata = videoPlugin->GetMetadata();
            audioTracks = videoPlugin->GetAudioTracks();
            subtitleTracks = videoPlugin->GetSubtitleTracks();
            
            currentState = VideoState::Stopped;
            frameUpdateNeeded = true;
            
            if (onVideoLoaded) onVideoLoaded(metadata);
            return true;
        } else {
            SetError("Failed to load video: " + filePath);
            return false;
        }
    }
    
    bool LoadFromStream(const std::string& streamUrl) {
        // Find plugin that supports streaming
        videoPlugin = FindStreamingPlugin();
        if (!videoPlugin || !videoPlugin->SupportsStreaming()) {
            SetError("No streaming plugin available");
            return false;
        }
        
        currentStreamUrl = streamUrl;
        currentFilePath.clear();
        
        if (videoPlugin->LoadFromStream(streamUrl)) {
            metadata = videoPlugin->GetMetadata();
            audioTracks = videoPlugin->GetAudioTracks();
            subtitleTracks = videoPlugin->GetSubtitleTracks();
            
            currentState = VideoState::Stopped;
            frameUpdateNeeded = true;
            
            if (onVideoLoaded) onVideoLoaded(metadata);
            return true;
        } else {
            SetError("Failed to load stream: " + streamUrl);
            return false;
        }
    }
    
    // ===== PLAYBACK CONTROL =====
    void Play() {
        if (!videoPlugin || currentState == VideoState::Error) return;
        
        if (videoPlugin->Play()) {
            currentState = VideoState::Playing;
            UpdatePlayPauseButton();
            
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
        }
    }
    
    void Pause() {
        if (!videoPlugin || currentState != VideoState::Playing) return;
        
        if (videoPlugin->Pause()) {
            currentState = VideoState::Paused;
            UpdatePlayPauseButton();
            
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
        }
    }
    
    void Stop() {
        if (!videoPlugin) return;
        
        if (videoPlugin->Stop()) {
            currentState = VideoState::Stopped;
            UpdatePlayPauseButton();
            
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
        }
    }
    
    void TogglePlayPause() {
        if (currentState == VideoState::Playing) {
            Pause();
        } else if (currentState == VideoState::Paused || currentState == VideoState::Stopped) {
            Play();
        }
    }
    
    void Seek(float position) {
        if (!videoPlugin) return;
        
        float clampedPosition = std::clamp(position, 0.0f, GetDuration());
        if (videoPlugin->Seek(clampedPosition)) {
            frameUpdateNeeded = true;
            UpdateProgressSlider();
            
            if (onPositionChanged) onPositionChanged(clampedPosition);
        }
    }
    
    void SeekRelative(float delta) {
        Seek(GetPosition() + delta);
    }
    
    // ===== PLAYBACK INFORMATION =====
    float GetPosition() const {
        return videoPlugin ? videoPlugin->GetPosition() : 0.0f;
    }
    
    float GetDuration() const {
        return videoPlugin ? videoPlugin->GetDuration() : 0.0f;
    }
    
    float GetProgress() const {
        float duration = GetDuration();
        return (duration > 0) ? (GetPosition() / duration) : 0.0f;
    }
    
    VideoState GetState() const { return currentState; }
    const VideoMetadata& GetMetadata() const { return metadata; }
    bool IsLoaded() const { return videoPlugin && videoPlugin->IsLoaded(); }
    bool IsPlaying() const { return currentState == VideoState::Playing; }
    bool IsPaused() const { return currentState == VideoState::Paused; }
    bool IsStopped() const { return currentState == VideoState::Stopped; }
    
    // ===== AUDIO CONTROL =====
    void SetVolume(float vol) {
        volume = std::clamp(vol, 0.0f, 1.0f);
        if (videoPlugin) {
            videoPlugin->SetVolume(isMuted ? 0.0f : volume);
        }
        UpdateVolumeSlider();
        
        if (onVolumeChanged) onVolumeChanged(volume);
    }
    
    float GetVolume() const { return volume; }
    
    void SetMuted(bool muted) {
        isMuted = muted;
        if (videoPlugin) {
            videoPlugin->SetMuted(muted);
        }
        UpdateMuteButton();
        
        if (onMuteChanged) onMuteChanged(isMuted);
    }
    
    bool IsMuted() const { return isMuted; }
    
    void ToggleMute() {
        SetMuted(!isMuted);
    }
    
    // ===== DISPLAY PROPERTIES =====
    void SetScaleMode(VideoScaleMode mode) {
        scaleMode = mode;
        layoutDirty = true;
    }
    
    VideoScaleMode GetScaleMode() const { return scaleMode; }
    
    void SetShowControls(bool show) {
        showControls = show;
        if (!show) controlsVisible = false;
        layoutDirty = true;
    }
    
    bool GetShowControls() const { return showControls; }
    
    void SetControlsStyle(const VideoControlsStyle& style) {
        controlsStyle = style;
        showControls = style.showControls;
        layoutDirty = true;
    }
    
    const VideoControlsStyle& GetControlsStyle() const { return controlsStyle; }
    
    // ===== PLAYBACK RATE =====
    void SetPlaybackRate(float rate) {
        playbackRate = std::clamp(rate, 0.1f, 4.0f);
        if (videoPlugin) {
            videoPlugin->SetPlaybackRate(playbackRate);
        }
        
        if (onPlaybackRateChanged) onPlaybackRateChanged(playbackRate);
    }
    
    float GetPlaybackRate() const { return playbackRate; }
    
    // ===== TRACK MANAGEMENT =====
    const std::vector<AudioTrack>& GetAudioTracks() const { return audioTracks; }
    const std::vector<SubtitleTrack>& GetSubtitleTracks() const { return subtitleTracks; }
    
    void SetActiveAudioTrack(int index) {
        if (index >= 0 && index < static_cast<int>(audioTracks.size())) {
            activeAudioTrack = index;
            if (videoPlugin) {
                videoPlugin->SetActiveAudioTrack(index);
            }
            
            if (onAudioTrackChanged) onAudioTrackChanged(index);
        }
    }
    
    void SetActiveSubtitleTrack(int index) {
        if (index >= -1 && index < static_cast<int>(subtitleTracks.size())) {
            activeSubtitleTrack = index;
            if (videoPlugin) {
                videoPlugin->SetActiveSubtitleTrack(index);
            }
            
            if (onSubtitleTrackChanged) onSubtitleTrackChanged(index);
        }
    }
    
    int GetActiveAudioTrack() const { return activeAudioTrack; }
    int GetActiveSubtitleTrack() const { return activeSubtitleTrack; }
    
    // ===== QUALITY CONTROL =====
    void SetVideoQuality(VideoQuality qual) {
        quality = qual;
        if (videoPlugin) {
            videoPlugin->SetVideoQuality(quality);
        }
        
        if (onQualityChanged) onQualityChanged(quality);
    }
    
    VideoQuality GetVideoQuality() const { return quality; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout();
            layoutDirty = false;
        }
        
        // Update video frame if needed
        UpdateVideoFrame();
        
        // Draw video frame
        RenderVideoFrame();
        
        // Update and draw controls
        UpdateControlsVisibility();
        if (controlsVisible && showControls) {
            RenderControls();
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        // Track user interaction for auto-hide controls
        if (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseDown) {
            lastInteraction = std::chrono::steady_clock::now();
            if (showControls && !controlsVisible) {
                controlsVisible = true;
            }
        }
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseDoubleClick:
                if (Contains(event.x, event.y)) {
                    ToggleFullscreen();
                }
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
        
        // Let controls handle their events
        if (controlsVisible && showControls) {
            if (playPauseButton) playPauseButton->OnEvent(event);
            if (stopButton) stopButton->OnEvent(event);
            if (fullscreenButton) fullscreenButton->OnEvent(event);
            if (muteButton) muteButton->OnEvent(event);
            if (progressSlider) progressSlider->OnEvent(event);
            if (volumeSlider) volumeSlider->OnEvent(event);
        }
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(const VideoMetadata&)> onVideoLoaded;
    std::function<void(VideoState)> onPlaybackStateChanged;
    std::function<void(float)> onPositionChanged;
    std::function<void(float)> onVolumeChanged;
    std::function<void(bool)> onMuteChanged;
    std::function<void(float)> onPlaybackRateChanged;
    std::function<void(int)> onAudioTrackChanged;
    std::function<void(int)> onSubtitleTrackChanged;
    std::function<void(VideoQuality)> onQualityChanged;
    std::function<void(const std::string&)> onError;
    std::function<void()> onFullscreenToggle;

private:
    // ===== PLUGIN MANAGEMENT =====
    std::shared_ptr<IVideoPlugin> FindVideoPlugin(const std::string& filePath) {
        // This would interface with the video plugin registry
        // For now, return nullptr (would need actual plugin implementation)
        return nullptr;
    }
    
    std::shared_ptr<IVideoPlugin> FindStreamingPlugin() {
        // Find plugin that supports streaming
        return nullptr;
    }
    
    void SetError(const std::string& message) {
        currentState = VideoState::Error;
        if (onError) onError(message);
    }
    
    // ===== LAYOUT CALCULATION =====
    void CalculateLayout() {
        Rect2D bounds = GetBounds();
        
        if (showControls) {
            // Reserve space for controls at bottom
            videoRect = Rect2D(bounds.x, bounds.y, bounds.width, 
                             bounds.height - controlsStyle.controlsHeight);
            controlsRect = Rect2D(bounds.x, bounds.y + bounds.height - controlsStyle.controlsHeight,
                                bounds.width, controlsStyle.controlsHeight);
        } else {
            videoRect = bounds;
            controlsRect = Rect2D();
        }
        
        // Update control positions
        UpdateControlLayout();
    }
    
    void UpdateControlLayout() {
        if (!showControls || controlsRect.width <= 0) return;
        
        float buttonY = controlsRect.y + (controlsRect.height - controlsStyle.buttonSize) / 2;
        float currentX = controlsRect.x + controlsStyle.spacing;
        
        // Play/Pause button
        if (playPauseButton) {
            playPauseButton->SetPosition(static_cast<long>(currentX), static_cast<long>(buttonY));
            playPauseButton->SetSize(static_cast<long>(controlsStyle.buttonSize), 
                                   static_cast<long>(controlsStyle.buttonSize));
            currentX += controlsStyle.buttonSize + controlsStyle.spacing;
        }
        
        // Stop button
        if (stopButton) {
            stopButton->SetPosition(static_cast<long>(currentX), static_cast<long>(buttonY));
            stopButton->SetSize(static_cast<long>(controlsStyle.buttonSize), 
                              static_cast<long>(controlsStyle.buttonSize));
            currentX += controlsStyle.buttonSize + controlsStyle.spacing;
        }
        
        // Progress slider (takes remaining space minus volume and fullscreen)
        float remainingWidth = controlsRect.x + controlsRect.width - currentX - 
                              (controlsStyle.buttonSize * 2 + controlsStyle.spacing * 4 + 100); // Volume slider width
        
        if (progressSlider && remainingWidth > 100) {
            progressSlider->SetPosition(static_cast<long>(currentX), static_cast<long>(buttonY));
            progressSlider->SetSize(static_cast<long>(remainingWidth), 
                                  static_cast<long>(controlsStyle.buttonSize));
            currentX += remainingWidth + controlsStyle.spacing;
        }
        
        // Volume slider
        if (volumeSlider) {
            volumeSlider->SetPosition(static_cast<long>(currentX), static_cast<long>(buttonY));
            volumeSlider->SetSize(100, static_cast<long>(controlsStyle.buttonSize));
            currentX += 100 + controlsStyle.spacing;
        }
        
        // Mute button
        if (muteButton) {
            muteButton->SetPosition(static_cast<long>(currentX), static_cast<long>(buttonY));
            muteButton->SetSize(static_cast<long>(controlsStyle.buttonSize), 
                              static_cast<long>(controlsStyle.buttonSize));
            currentX += controlsStyle.buttonSize + controlsStyle.spacing;
        }
        
        // Fullscreen button
        if (fullscreenButton) {
            fullscreenButton->SetPosition(static_cast<long>(currentX), static_cast<long>(buttonY));
            fullscreenButton->SetSize(static_cast<long>(controlsStyle.buttonSize), 
                                    static_cast<long>(controlsStyle.buttonSize));
        }
    }
    
    // ===== CONTROL CREATION =====
    void CreateControls() {
        // Create play/pause button
        playPauseButton = CreateButton("playPause", 0, 0, 0, 40, 40, "⏵");
        playPauseButton->onClicked = [this]() { TogglePlayPause(); };
        
        // Create stop button
        stopButton = CreateButton("stop", 0, 0, 0, 40, 40, "⏹");
        stopButton->onClicked = [this]() { Stop(); };
        
        // Create fullscreen button
        fullscreenButton = CreateButton("fullscreen", 0, 0, 0, 40, 40, "⛶");
        fullscreenButton->onClicked = [this]() { ToggleFullscreen(); };
        
        // Create mute button
        muteButton = CreateButton("mute", 0, 0, 0, 40, 40, "🔊");
        muteButton->onClicked = [this]() { ToggleMute(); };
        
        // Create progress slider
        progressSlider = CreateHorizontalSlider("progress", 0, 0, 0, 200, 40, 0.0f, 100.0f, 0.0f);
        progressSlider->onValueChanged = [this](float value) {
            float duration = GetDuration();
            if (duration > 0) {
                Seek((value / 100.0f) * duration);
            }
        };
        
        // Create volume slider
        volumeSlider = CreateHorizontalSlider("volume", 0, 0, 0, 100, 40, 0.0f, 100.0f, volume * 100.0f);
        volumeSlider->onValueChanged = [this](float value) {
            SetVolume(value / 100.0f);
        };
    }
    
    // ===== CONTROL UPDATES =====
    void UpdatePlayPauseButton() {
        if (!playPauseButton) return;
        
        switch (currentState) {
            case VideoState::Playing:
                playPauseButton->SetText("⏸");  // Pause symbol
                break;
            case VideoState::Paused:
            case VideoState::Stopped:
                playPauseButton->SetText("⏵");  // Play symbol
                break;
            default:
                playPauseButton->SetText("⏵");
                break;
        }
    }
    
    void UpdateMuteButton() {
        if (!muteButton) return;
        
        muteButton->SetText(isMuted ? "🔇" : "🔊");
    }
    
    void UpdateProgressSlider() {
        if (!progressSlider) return;
        
        float progress = GetProgress() * 100.0f;
        progressSlider->SetValue(progress);
    }
    
    void UpdateVolumeSlider() {
        if (!volumeSlider) return;
        
        volumeSlider->SetValue(volume * 100.0f);
    }
    
    void UpdateControlsVisibility() {
        if (!showControls || !controlsStyle.autoHideControls) {
            controlsVisible = showControls;
            return;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastInteraction);
        
        if (elapsed.count() > controlsStyle.autoHideDelay * 1000) {
            controlsVisible = false;
        }
    }
    
    // ===== VIDEO FRAME HANDLING =====
    void UpdateVideoFrame() {
        if (!videoPlugin || !frameUpdateNeeded) return;
        
        if (videoPlugin->HasNewFrame()) {
            currentFrame = videoPlugin->GetCurrentFrame();
            frameUpdateNeeded = false;
        }
    }
    
    void RenderVideoFrame() {
        if (currentFrame.data.empty() || currentFrame.width <= 0 || currentFrame.height <= 0) {
            // Draw placeholder or black background
            SetFillColor(Colors::Black);
            DrawRectangle(videoRect);
            return;
        }
        
        // Calculate scaled video rectangle based on scale mode
        Rect2D scaledRect = CalculateScaledVideoRect();
        
        // Render the video frame (this would need platform-specific implementation)
        RenderVideoFrameData(currentFrame, scaledRect);
    }
    
    Rect2D CalculateScaledVideoRect() const {
        if (currentFrame.width <= 0 || currentFrame.height <= 0) {
            return videoRect;
        }
        
        float videoAspect = static_cast<float>(currentFrame.width) / currentFrame.height;
        float containerAspect = videoRect.width / videoRect.height;
        
        switch (scaleMode) {
            case VideoScaleMode::NoScale:
                return Rect2D(videoRect.x, videoRect.y, 
                             static_cast<float>(currentFrame.width), 
                             static_cast<float>(currentFrame.height));
                
            case VideoScaleMode::Stretch:
                return videoRect;
                
            case VideoScaleMode::Uniform: {
                if (videoAspect > containerAspect) {
                    // Video is wider, fit to width
                    float height = videoRect.width / videoAspect;
                    float offsetY = (videoRect.height - height) / 2;
                    return Rect2D(videoRect.x, videoRect.y + offsetY, videoRect.width, height);
                } else {
                    // Video is taller, fit to height
                    float width = videoRect.height * videoAspect;
                    float offsetX = (videoRect.width - width) / 2;
                    return Rect2D(videoRect.x + offsetX, videoRect.y, width, videoRect.height);
                }
            }
            
            case VideoScaleMode::UniformToFill: {
                if (videoAspect > containerAspect) {
                    // Video is wider, fit to height and crop sides
                    float width = videoRect.height * videoAspect;
                    float offsetX = (videoRect.width - width) / 2;
                    return Rect2D(videoRect.x + offsetX, videoRect.y, width, videoRect.height);
                } else {
                    // Video is taller, fit to width and crop top/bottom
                    float height = videoRect.width / videoAspect;
                    float offsetY = (videoRect.height - height) / 2;
                    return Rect2D(videoRect.x, videoRect.y + offsetY, videoRect.width, height);
                }
            }
            
            case VideoScaleMode::Center: {
                float width = static_cast<float>(currentFrame.width);
                float height = static_cast<float>(currentFrame.height);
                float offsetX = (videoRect.width - width) / 2;
                float offsetY = (videoRect.height - height) / 2;
                return Rect2D(videoRect.x + offsetX, videoRect.y + offsetY, width, height);
            }
            
            default:
                return videoRect;
        }
    }
    
    void RenderVideoFrameData(const VideoFrame& frame, const Rect2D& destRect) {
        // This would need platform-specific implementation to render video frame data
        // For now, just draw a placeholder
        SetFillColor(Color(64, 64, 64));
        DrawRectangle(destRect);
        
        // Draw frame info text (for debugging)
        SetTextColor(Colors::White);
        std::string frameInfo = "Frame " + std::to_string(frame.frameNumber) + 
                               " (" + std::to_string(frame.width) + "x" + std::to_string(frame.height) + ")";
        DrawText(frameInfo, Point2D(destRect.x + 10, destRect.y + 30));
    }
    
    void RenderControls() {
        if (controlsRect.width <= 0) return;
        
        // Draw controls background
        SetFillColor(controlsStyle.backgroundColor);
        if (controlsStyle.cornerRadius > 0) {
            DrawRoundedRectangle(controlsRect, controlsStyle.cornerRadius);
        } else {
            DrawRectangle(controlsRect);
        }
        
        // Render control elements
        if (playPauseButton) playPauseButton->Render();
        if (stopButton) stopButton->Render();
        if (progressSlider) progressSlider->Render();
        if (volumeSlider) volumeSlider->Render();
        if (muteButton) muteButton->Render();
        if (fullscreenButton) fullscreenButton->Render();
        
        // Draw time display
        DrawTimeDisplay();
    }
    
    void DrawTimeDisplay() {
        float currentPos = GetPosition();
        float duration = GetDuration();
        
        std::string timeText = FormatTime(currentPos) + " / " + FormatTime(duration);
        
        SetTextColor(controlsStyle.textColor);
        SetFont("Arial", 12);
        
        // Position time display in controls area
        Point2D textPos(controlsRect.x + controlsRect.width - 120, 
                       controlsRect.y + controlsRect.height / 2);
        DrawText(timeText, textPos);
    }
    
    std::string FormatTime(float seconds) const {
        int totalSeconds = static_cast<int>(seconds);
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int secs = totalSeconds % 60;
        
        if (hours > 0) {
            return std::to_string(hours) + ":" + 
                   (minutes < 10 ? "0" : "") + std::to_string(minutes) + ":" +
                   (secs < 10 ? "0" : "") + std::to_string(secs);
        } else {
            return std::to_string(minutes) + ":" + 
                   (secs < 10 ? "0" : "") + std::to_string(secs);
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (videoRect.Contains(Point2D(event.x, event.y))) {
            // Click on video area
            if (onVideoClicked) onVideoClicked(Point2D(event.x - videoRect.x, event.y - videoRect.y));
        }
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        switch (event.virtualKey) {
            case UCKeys::Space:
                TogglePlayPause();
                break;
            case UCKeys::Left:
                SeekRelative(-10.0f); // Seek back 10 seconds
                break;
            case UCKeys::Right:
                SeekRelative(10.0f); // Seek forward 10 seconds
                break;
            case 'M':
                ToggleMute();
                break;
            case 'F':
                ToggleFullscreen();
                break;
            case UCKeys::Up:
                SetVolume(GetVolume() + 0.1f);
                break;
            case UCKeys::Down:
                SetVolume(GetVolume() - 0.1f);
                break;
        }
    }
    
    void ToggleFullscreen() {
        if (onFullscreenToggle) onFullscreenToggle();
    }

public:
    // ===== ADDITIONAL EVENT CALLBACKS =====
    std::function<void(const Point2D&)> onVideoClicked;
};

// ===== COLOR CONVERSION IMPLEMENTATIONS =====
inline VideoControlsStyle VideoControlsStyle::Default() {
    return VideoControlsStyle(); // Use default constructor values
}

inline VideoControlsStyle VideoControlsStyle::Minimal() {
    VideoControlsStyle style;
    style.controlsHeight = 40.0f;
    style.buttonSize = 30.0f;
    style.backgroundColor = Color(0, 0, 0, 100);
    return style;
}

inline VideoControlsStyle VideoControlsStyle::Dark() {
    VideoControlsStyle style;
    style.backgroundColor = Color(32, 32, 32, 200);
    style.buttonColor = Color(220, 220, 220);
    style.textColor = Color(220, 220, 220);
    return style;
}

inline VideoControlsStyle VideoControlsStyle::Light() {
    VideoControlsStyle style;
    style.backgroundColor = Color(240, 240, 240, 200);
    style.buttonColor = Color(64, 64, 64);
    style.textColor = Color(64, 64, 64);
    style.sliderColor = Color(100, 100, 100, 200);
    return style;
}

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasVideoElement> CreateVideoElement(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasVideoElement>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasVideoElement> CreateVideoFromFile(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const std::string& filePath) {
    auto video = CreateVideoElement(identifier, id, x, y, w, h);
    video->LoadFromFile(filePath);
    return video;
}

inline std::shared_ptr<UltraCanvasVideoElement> CreateVideoStream(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const std::string& streamUrl) {
    auto video = CreateVideoElement(identifier, id, x, y, w, h);
    video->LoadFromStream(streamUrl);
    return video;
}

// ===== BUILDER PATTERN =====
class VideoElementBuilder {
private:
    std::string identifier = "VideoElement";
    long id = 0;
    long x = 0, y = 0, w = 640, h = 480;
    std::string filePath;
    std::string streamUrl;
    VideoControlsStyle controlsStyle = VideoControlsStyle::Default();
    VideoScaleMode scaleMode = VideoScaleMode::Uniform;
    bool autoPlay = false;
    float volume = 1.0f;
    std::function<void(const VideoMetadata&)> loadedHandler;
    std::function<void(VideoState)> stateHandler;
    
public:
    VideoElementBuilder& SetIdentifier(const std::string& id) { identifier = id; return *this; }
    VideoElementBuilder& SetID(long elementId) { id = elementId; return *this; }
    VideoElementBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    VideoElementBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    VideoElementBuilder& SetFilePath(const std::string& path) { filePath = path; return *this; }
    VideoElementBuilder& SetStreamUrl(const std::string& url) { streamUrl = url; return *this; }
    VideoElementBuilder& SetControlsStyle(const VideoControlsStyle& style) { controlsStyle = style; return *this; }
    VideoElementBuilder& SetScaleMode(VideoScaleMode mode) { scaleMode = mode; return *this; }
    VideoElementBuilder& SetAutoPlay(bool enable) { autoPlay = enable; return *this; }
    VideoElementBuilder& SetVolume(float vol) { volume = vol; return *this; }
    VideoElementBuilder& OnVideoLoaded(std::function<void(const VideoMetadata&)> handler) { loadedHandler = handler; return *this; }
    VideoElementBuilder& OnStateChanged(std::function<void(VideoState)> handler) { stateHandler = handler; return *this; }
    
    std::shared_ptr<UltraCanvasVideoElement> Build() {
        auto video = CreateVideoElement(identifier, id, x, y, w, h);
        
        video->SetControlsStyle(controlsStyle);
        video->SetScaleMode(scaleMode);
        video->SetVolume(volume);
        
        if (loadedHandler) video->onVideoLoaded = loadedHandler;
        if (stateHandler) video->onPlaybackStateChanged = stateHandler;
        
        if (!filePath.empty()) {
            video->LoadFromFile(filePath);
            if (autoPlay) video->Play();
        } else if (!streamUrl.empty()) {
            video->LoadFromStream(streamUrl);
            if (autoPlay) video->Play();
        }
        
        return video;
    }
};

// ===== LEGACY C-STYLE API (BACKWARD COMPATIBLE) =====
extern "C" {
    void* CreateVideoElementC(int x, int y, int width, int height);
    bool LoadVideoFromFileC(void* handle, const char* filePath);
    bool LoadVideoFromStreamC(void* handle, const char* streamUrl);
    void PlayVideoC(void* handle);
    void PauseVideoC(void* handle);
    void StopVideoC(void* handle);
    void SeekVideoC(void* handle, float position);
    float GetVideoPositionC(void* handle);
    float GetVideoDurationC(void* handle);
    void SetVideoVolumeC(void* handle, float volume);
    float GetVideoVolumeC(void* handle);
    void SetVideoMutedC(void* handle, bool muted);
    bool IsVideoMutedC(void* handle);
    void SetVideoScaleModeC(void* handle, int mode);
    void DestroyVideoElement(void* handle);
}

} // namespace UltraCanvas