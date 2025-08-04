// UltraCanvasAudioElement.h
// Advanced audio playback component with multimedia controls and plugin architecture
// Version: 1.0.0
// Last Modified: 2025-01-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasFileDialog.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include <unordered_set>
#include <unordered_map>

namespace UltraCanvas {

// ===== AUDIO FORMAT DEFINITIONS =====
enum class AudioFormat {
    Unknown, WAV, MP3, FLAC, AAC, OGG, OPUS, M4A, AIFF, WMA, 
    APE, ALAC, MQA, DSD, PCM, AC3, DTS, SPEEX, VORBIS
};

enum class AudioCodec {
    Unknown, PCM, MP3, AAC, FLAC, Vorbis, Opus, ALAC, APE, 
    DSD, AC3, DTS, WMA, SPEEX
};

enum class AudioState {
    Stopped, Playing, Paused, Buffering, Loading, Error, EndOfMedia
};

enum class AudioChannelLayout {
    Unknown, Mono, Stereo, Surround_2_1, Surround_5_1, Surround_7_1, Atmos
};

// ===== AUDIO METADATA =====
struct AudioMetadata {
    std::string title, artist, album, genre, composer, year;
    std::string comment, albumArtist, trackNumber, discNumber;
    float duration = 0.0f;
    int bitrate = 0, sampleRate = 0, channels = 0, bitsPerSample = 0;
    AudioFormat format = AudioFormat::Unknown;
    AudioCodec codec = AudioCodec::Unknown;
    AudioChannelLayout channelLayout = AudioChannelLayout::Unknown;
    std::string fileName, filePath;
    size_t fileSize = 0;
    bool hasAlbumArt = false;
    std::vector<uint8_t> albumArtData;
    std::string albumArtMimeType;
    
    bool IsValid() const { return duration > 0.0f && !filePath.empty(); }
    std::string GetDisplayTitle() const {
        if (!title.empty()) return title;
        if (!fileName.empty()) return fileName;
        return "Unknown Track";
    }
    
    std::string GetDisplayArtist() const {
        if (!artist.empty()) return artist;
        if (!albumArtist.empty()) return albumArtist;
        return "Unknown Artist";
    }
    
    std::string GetFormatString() const {
        std::string result;
        switch (format) {
            case AudioFormat::MP3: result = "MP3"; break;
            case AudioFormat::FLAC: result = "FLAC"; break;
            case AudioFormat::WAV: result = "WAV"; break;
            case AudioFormat::AAC: result = "AAC"; break;
            case AudioFormat::OGG: result = "OGG"; break;
            case AudioFormat::OPUS: result = "OPUS"; break;
            default: result = "Unknown"; break;
        }
        
        if (bitrate > 0) {
            result += " " + std::to_string(bitrate) + "kbps";
        }
        if (sampleRate > 0) {
            result += " " + std::to_string(sampleRate) + "Hz";
        }
        
        return result;
    }
};

// ===== AUDIO PLUGIN INTERFACE =====
class IAudioPlugin {
public:
    virtual ~IAudioPlugin() = default;
    
    // Plugin Information
    virtual std::string GetPluginName() = 0;
    virtual std::string GetPluginVersion() = 0;
    virtual std::vector<AudioFormat> GetSupportedFormats() = 0;
    virtual std::vector<std::string> GetSupportedExtensions() = 0;
    
    // Format Support
    virtual bool CanHandle(const std::string& filePath) = 0;
    virtual bool CanHandle(AudioFormat format) = 0;
    
    // Audio Loading
    virtual bool LoadAudio(const std::string& filePath) = 0;
    virtual bool LoadFromStream(const std::string& url) = 0;
    virtual AudioMetadata GetMetadata() = 0;
    virtual bool IsLoaded() = 0;
    
    // Playback Control
    virtual bool Play() = 0;
    virtual bool Pause() = 0;
    virtual bool Stop() = 0;
    virtual bool Seek(float position) = 0;
    virtual float GetPosition() = 0;
    virtual float GetDuration() = 0;
    virtual AudioState GetState() = 0;
    
    // Audio Properties
    virtual void SetVolume(float volume) = 0;
    virtual float GetVolume() = 0;
    virtual void SetMuted(bool muted) = 0;
    virtual bool IsMuted() = 0;
    virtual void SetPlaybackRate(float rate) = 0;
    virtual float GetPlaybackRate() = 0;
    
    // Advanced Features
    virtual bool SupportsStreaming() = 0;
    virtual bool SupportsGaplessPlayback() = 0;
    virtual bool SupportsEqualizer() = 0;
    virtual bool SupportsVisualization() = 0;
    
    // Visualization Data
    virtual std::vector<float> GetSpectrumData(int bands = 32) = 0;
    virtual std::vector<float> GetWaveformData(int samples = 512) = 0;
    virtual float GetPeakLevel() = 0;
    virtual float GetRMSLevel() = 0;
};

// ===== AUDIO CONTROLS STYLE =====
struct AudioControlsStyle {
    Color backgroundColor = Color(40, 40, 40);
    Color controlColor = Color(200, 200, 200);
    Color controlHoverColor = Color(255, 255, 255);
    Color progressBarColor = Color(0, 150, 255);
    Color progressBackgroundColor = Color(80, 80, 80);
    Color textColor = Color(220, 220, 220);
    Color titleColor = Color(255, 255, 255);
    
    std::string fontFamily = "Arial";
    float fontSize = 11.0f;
    float titleFontSize = 13.0f;
    
    float controlPadding = 8.0f;
    float buttonSize = 24.0f;
    float progressHeight = 6.0f;
    float volumeSliderWidth = 80.0f;
    
    bool showAlbumArt = true;
    bool showMetadata = true;
    bool showProgress = true;
    bool showVolume = true;
    bool showPlaybackRate = false;
    bool showVisualization = false;
    
    static AudioControlsStyle Default() { return AudioControlsStyle(); }
    static AudioControlsStyle Minimal() {
        AudioControlsStyle style;
        style.showMetadata = false;
        style.showPlaybackRate = false;
        style.showVisualization = false;
        return style;
    }
    static AudioControlsStyle Full() {
        AudioControlsStyle style;
        style.showPlaybackRate = true;
        style.showVisualization = true;
        return style;
    }
};

// ===== MAIN AUDIO ELEMENT =====
class UltraCanvasAudioElement : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== AUDIO STATE =====
    std::shared_ptr<IAudioPlugin> audioPlugin;
    AudioMetadata metadata;
    AudioState currentState = AudioState::Stopped;
    
    // ===== PLAYBACK PROPERTIES =====
    float volume = 1.0f;
    bool muted = false;
    float playbackRate = 1.0f;
    float position = 0.0f;
    float duration = 0.0f;
    
    // ===== UI PROPERTIES =====
    AudioControlsStyle controlsStyle;
    bool showControls = true;
    bool autoPlay = false;
    bool loop = false;
    
    // ===== VISUALIZATION =====
    bool visualizationEnabled = false;
    std::vector<float> spectrumData;
    std::vector<float> waveformData;
    float peakLevel = 0.0f;
    float rmsLevel = 0.0f;
    
    // ===== INTERACTION STATE =====
    bool isDraggingProgress = false;
    bool isDraggingVolume = false;
    Point2D lastMousePos;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasAudioElement(const std::string& identifier = "AudioElement", 
                           long id = 0, long x = 0, long y = 0, long w = 400, long h = 120)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Input;
        
        // Initialize default style
        controlsStyle = AudioControlsStyle::Default();
    }
    

    // ===== AUDIO LOADING =====
    bool LoadFromFile(const std::string& filePath) {
        if (!audioPlugin) {
            audioPlugin = FindAudioPlugin(filePath);
            if (!audioPlugin) {
                if (onError) onError("No suitable audio plugin found for: " + filePath);
                return false;
            }
        }
        
        bool success = audioPlugin->LoadAudio(filePath);
        if (success) {
            metadata = audioPlugin->GetMetadata();
            duration = metadata.duration;
            position = 0.0f;
            currentState = AudioState::Stopped;
            
            if (onAudioLoaded) onAudioLoaded(metadata);
            
            if (autoPlay) {
                Play();
            }
        } else {
            if (onError) onError("Failed to load audio: " + filePath);
        }
        
        return success;
    }
    
    bool LoadFromStream(const std::string& streamUrl) {
        if (!audioPlugin) {
            // Try to find a plugin that supports streaming
            audioPlugin = FindStreamingAudioPlugin();
            if (!audioPlugin) {
                if (onError) onError("No streaming audio plugin available");
                return false;
            }
        }
        
        bool success = audioPlugin->LoadFromStream(streamUrl);
        if (success) {
            metadata = audioPlugin->GetMetadata();
            duration = metadata.duration;
            position = 0.0f;
            currentState = AudioState::Loading;
            
            if (onAudioLoaded) onAudioLoaded(metadata);
        } else {
            if (onError) onError("Failed to load stream: " + streamUrl);
        }
        
        return success;
    }
    
    // ===== PLAYBACK CONTROL =====
    void Play() {
        if (!audioPlugin || !audioPlugin->IsLoaded()) return;
        
        bool success = audioPlugin->Play();
        if (success) {
            currentState = AudioState::Playing;
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
            if (onPlay) onPlay();
        }
    }
    
    void Pause() {
        if (!audioPlugin) return;
        
        bool success = audioPlugin->Pause();
        if (success) {
            currentState = AudioState::Paused;
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
            if (onPause) onPause();
        }
    }
    
    void Stop() {
        if (!audioPlugin) return;
        
        bool success = audioPlugin->Stop();
        if (success) {
            currentState = AudioState::Stopped;
            position = 0.0f;
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
            if (onStop) onStop();
        }
    }
    
    void TogglePlayPause() {
        if (currentState == AudioState::Playing) {
            Pause();
        } else {
            Play();
        }
    }
    
    void Seek(float newPosition) {
        if (!audioPlugin || duration <= 0.0f) return;
        
        float clampedPosition = std::max(0.0f, std::min(newPosition, duration));
        bool success = audioPlugin->Seek(clampedPosition);
        if (success) {
            position = clampedPosition;
            if (onPositionChanged) onPositionChanged(position);
        }
    }
    
    void SeekRelative(float delta) {
        Seek(position + delta);
    }
    
    // ===== AUDIO PROPERTIES =====
    void SetVolume(float newVolume) {
        volume = std::max(0.0f, std::min(newVolume, 1.0f));
        if (audioPlugin) {
            audioPlugin->SetVolume(muted ? 0.0f : volume);
        }
        if (onVolumeChanged) onVolumeChanged(volume);
    }
    
    float GetVolume() const { return volume; }
    
    void SetMuted(bool shouldMute) {
        muted = shouldMute;
        if (audioPlugin) {
            audioPlugin->SetMuted(muted);
        }
        if (onMuteChanged) onMuteChanged(muted);
    }
    
    bool IsMuted() const { return muted; }
    
    void ToggleMute() {
        SetMuted(!muted);
    }
    
    void SetPlaybackRate(float rate) {
        playbackRate = std::max(0.1f, std::min(rate, 4.0f));
        if (audioPlugin) {
            audioPlugin->SetPlaybackRate(playbackRate);
        }
        if (onPlaybackRateChanged) onPlaybackRateChanged(playbackRate);
    }
    
    float GetPlaybackRate() const { return playbackRate; }
    
    // ===== STATE QUERIES =====
    AudioState GetState() const { return currentState; }
    bool IsLoaded() const { return audioPlugin && audioPlugin->IsLoaded(); }
    bool IsPlaying() const { return currentState == AudioState::Playing; }
    bool IsPaused() const { return currentState == AudioState::Paused; }
    bool IsStopped() const { return currentState == AudioState::Stopped; }
    
    float GetPosition() const { return position; }
    float GetDuration() const { return duration; }
    float GetProgress() const { return (duration > 0.0f) ? (position / duration) : 0.0f; }
    
    const AudioMetadata& GetMetadata() const { return metadata; }
    
    // ===== CONTROLS STYLE =====
    void SetControlsStyle(const AudioControlsStyle& style) {
        controlsStyle = style;
    }
    
    const AudioControlsStyle& GetControlsStyle() const { return controlsStyle; }
    
    void SetShowControls(bool show) { showControls = show; }
    bool GetShowControls() const { return showControls; }
    
    void SetAutoPlay(bool enable) { autoPlay = enable; }
    bool GetAutoPlay() const { return autoPlay; }
    
    void SetLoop(bool enable) { loop = enable; }
    bool GetLoop() const { return loop; }
    
    // ===== VISUALIZATION =====
    void SetVisualizationEnabled(bool enable) {
        visualizationEnabled = enable;
        controlsStyle.showVisualization = enable;
    }
    
    bool IsVisualizationEnabled() const { return visualizationEnabled; }
    
    const std::vector<float>& GetSpectrumData() const { return spectrumData; }
    const std::vector<float>& GetWaveformData() const { return waveformData; }
    float GetPeakLevel() const { return peakLevel; }
    float GetRMSLevel() const { return rmsLevel; }
    
    // ===== PLUGIN MANAGEMENT =====
    void SetAudioPlugin(std::shared_ptr<IAudioPlugin> plugin) {
        audioPlugin = plugin;
    }
    
    std::shared_ptr<IAudioPlugin> GetAudioPlugin() const { return audioPlugin; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Draw background
        SetFillColor(controlsStyle.backgroundColor);
        DrawRect(GetBounds());
        
        if (!showControls) return;
        
        Rect2D contentArea = GetBounds();
        contentArea.x += controlsStyle.controlPadding;
        contentArea.y += controlsStyle.controlPadding;
        contentArea.width -= controlsStyle.controlPadding * 2;
        contentArea.height -= controlsStyle.controlPadding * 2;
        
        // Update audio state
        UpdateAudioState();
        
        // Render components based on style
        float currentY = contentArea.y;
        
        // Title and metadata
        if (controlsStyle.showMetadata && metadata.IsValid()) {
            currentY += RenderMetadata(contentArea, currentY);
        }
        
        // Progress bar
        if (controlsStyle.showProgress) {
            currentY += RenderProgressBar(contentArea, currentY);
        }
        
        // Control buttons and volume
        currentY += RenderControls(contentArea, currentY);
        
        // Visualization
        if (controlsStyle.showVisualization && visualizationEnabled) {
            RenderVisualization(contentArea, currentY);
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(const AudioMetadata&)> onAudioLoaded;
    std::function<void(AudioState)> onPlaybackStateChanged;
    std::function<void(float)> onPositionChanged;
    std::function<void(float)> onVolumeChanged;
    std::function<void(bool)> onMuteChanged;
    std::function<void(float)> onPlaybackRateChanged;
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void()> onPlaybackFinished;
    std::function<void(const std::string&)> onError;

private:

    // ===== PLUGIN MANAGEMENT =====
    std::shared_ptr<IAudioPlugin> FindAudioPlugin(const std::string& filePath) {
        // This would search the audio plugin registry
        // For now, return a default plugin
        return nullptr; // Would be implemented with actual plugin system
    }
    
    std::shared_ptr<IAudioPlugin> FindStreamingAudioPlugin() {
        // Find a plugin that supports streaming
        return nullptr; // Would be implemented with actual plugin system
    }
    
    // ===== AUDIO STATE UPDATE =====
    void UpdateAudioState() {
        if (!audioPlugin) return;
        
        // Update position and state
        AudioState newState = audioPlugin->GetState();
        if (newState != currentState) {
            currentState = newState;
            if (onPlaybackStateChanged) onPlaybackStateChanged(currentState);
            
            if (currentState == AudioState::EndOfMedia) {
                if (loop) {
                    Seek(0.0f);
                    Play();
                } else {
                    if (onPlaybackFinished) onPlaybackFinished();
                }
            }
        }
        
        if (currentState == AudioState::Playing) {
            position = audioPlugin->GetPosition();
            if (onPositionChanged) onPositionChanged(position);
        }
        
        // Update visualization data
        if (visualizationEnabled && audioPlugin->SupportsVisualization()) {
            spectrumData = audioPlugin->GetSpectrumData();
            waveformData = audioPlugin->GetWaveformData();
            peakLevel = audioPlugin->GetPeakLevel();
            rmsLevel = audioPlugin->GetRMSLevel();
        }
    }
    
    // ===== RENDERING HELPERS =====
    float RenderMetadata(const Rect2D& area, float y) {
        SetTextColor(controlsStyle.titleColor);
        SetFont(controlsStyle.fontFamily, controlsStyle.titleFontSize);
        
        std::string title = metadata.GetDisplayTitle();
        DrawText(title, Point2D(area.x, y));
        
        SetTextColor(controlsStyle.textColor);
        SetFont(controlsStyle.fontFamily, controlsStyle.fontSize);
        
        std::string artist = metadata.GetDisplayArtist();
        if (!artist.empty()) {
            DrawText(artist, Point2D(area.x, y + controlsStyle.titleFontSize + 2));
        }
        
        std::string format = metadata.GetFormatString();
        if (!format.empty()) {
            DrawText(format, Point2D(area.x, y + controlsStyle.titleFontSize + controlsStyle.fontSize + 4));
        }
        
        return controlsStyle.titleFontSize + controlsStyle.fontSize + 8;
    }
    
    float RenderProgressBar(const Rect2D& area, float y) {
        float barY = y + 4;
        Rect2D progressBg(area.x, barY, area.width, controlsStyle.progressHeight);
        
        // Background
        SetFillColor(controlsStyle.progressBackgroundColor);
        DrawRect(progressBg);
        
        // Progress
        if (duration > 0.0f) {
            float progressWidth = (position / duration) * area.width;
            Rect2D progressBar(area.x, barY, progressWidth, controlsStyle.progressHeight);
            SetFillColor(controlsStyle.progressBarColor);
            DrawRect(progressBar);
        }
        
        // Time labels
        SetTextColor(controlsStyle.textColor);
        SetFont(controlsStyle.fontFamily, controlsStyle.fontSize);
        
        std::string positionText = FormatTime(position);
        std::string durationText = FormatTime(duration);
        
        DrawText(positionText, Point2D(area.x, barY + controlsStyle.progressHeight + 4));
        
        float durationWidth = GetTextWidth(durationText);
        DrawText(durationText, Point2D(area.x + area.width - durationWidth, barY + controlsStyle.progressHeight + 4));
        
        return controlsStyle.progressHeight + controlsStyle.fontSize + 8;
    }
    
    float RenderControls(const Rect2D& area, float y) {
        float buttonY = y + 4;
        float currentX = area.x;
        
        // Play/Pause button
        Rect2D playButton(currentX, buttonY, controlsStyle.buttonSize, controlsStyle.buttonSize);
        SetFillColor(controlsStyle.controlColor);
        DrawRect(playButton);
        
        // Draw play/pause icon (simplified)
        SetTextColor(controlsStyle.backgroundColor);
        SetFont(controlsStyle.fontFamily, controlsStyle.buttonSize * 0.6f);
        
        std::string icon = (currentState == AudioState::Playing) ? "||" : "▶";
        DrawText(icon, Point2D(currentX + 6, buttonY + 4));
        
        currentX += controlsStyle.buttonSize + 8;
        
        // Stop button
        Rect2D stopButton(currentX, buttonY, controlsStyle.buttonSize, controlsStyle.buttonSize);
        SetFillColor(controlsStyle.controlColor);
        DrawRect(stopButton);
        
        DrawText("■", Point2D(currentX + 6, buttonY + 4));
        currentX += controlsStyle.buttonSize + 16;
        
        // Volume control
        if (controlsStyle.showVolume) {
            SetTextColor(controlsStyle.textColor);
            SetFont(controlsStyle.fontFamily, controlsStyle.fontSize);
            DrawText("Vol:", Point2D(currentX, buttonY + 6));
            currentX += 30;
            
            // Volume slider
            Rect2D volumeBg(currentX, buttonY + 8, controlsStyle.volumeSliderWidth, 8);
            SetFillColor(controlsStyle.progressBackgroundColor);
            DrawRect(volumeBg);
            
            float volumeWidth = volume * controlsStyle.volumeSliderWidth;
            Rect2D volumeBar(currentX, buttonY + 8, volumeWidth, 8);
            SetFillColor(controlsStyle.progressBarColor);
            DrawRect(volumeBar);
        }
        
        return controlsStyle.buttonSize + 8;
    }
    
    void RenderVisualization(const Rect2D& area, float y) {
        if (spectrumData.empty()) return;
        
        float vizHeight = 40.0f;
        float barWidth = area.width / spectrumData.size();
        
        SetFillColor(controlsStyle.progressBarColor);
        
        for (size_t i = 0; i < spectrumData.size(); ++i) {
            float barHeight = spectrumData[i] * vizHeight;
            float barX = area.x + i * barWidth;
            float barY = y + vizHeight - barHeight;
            
            Rect2D bar(barX, barY, barWidth - 1, barHeight);
            DrawRect(bar);
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        Point2D localPos(event.x - GetX(), event.y - GetY());
        
        // Check progress bar click
        if (IsProgressBarClick(localPos)) {
            isDraggingProgress = true;
            UpdateProgressFromMouse(localPos);
        }
        
        // Check volume slider click
        if (IsVolumeSliderClick(localPos)) {
            isDraggingVolume = true;
            UpdateVolumeFromMouse(localPos);
        }
        
        // Check control buttons
        CheckControlButtonClicks(localPos);
        
        lastMousePos = Point2D(event.x, event.y);
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (isDraggingProgress) {
            Point2D localPos(event.x - GetX(), event.y - GetY());
            UpdateProgressFromMouse(localPos);
        }
        
        if (isDraggingVolume) {
            Point2D localPos(event.x - GetX(), event.y - GetY());
            UpdateVolumeFromMouse(localPos);
        }
        
        lastMousePos = Point2D(event.x, event.y);
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDraggingProgress = false;
        isDraggingVolume = false;
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        switch (event.virtualKey) {
            case UCKeys::Space:
                TogglePlayPause();
                break;
            case UCKeys::Left:
                SeekRelative(-5.0f);
                break;
            case UCKeys::Right:
                SeekRelative(5.0f);
                break;
            case 'M':
            case 'm':
                ToggleMute();
                break;
        }
    }
    
    // ===== UTILITY METHODS =====
    std::string FormatTime(float seconds) {
        int minutes = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        return std::to_string(minutes) + ":" + (secs < 10 ? "0" : "") + std::to_string(secs);
    }
    
    bool IsProgressBarClick(const Point2D& localPos) {
        // Simplified hit testing for progress bar
        return localPos.y > 30 && localPos.y < 50;
    }
    
    bool IsVolumeSliderClick(const Point2D& localPos) {
        // Simplified hit testing for volume slider
        return localPos.y > 60 && localPos.y < 80 && localPos.x > 200;
    }
    
    void UpdateProgressFromMouse(const Point2D& localPos) {
        float progress = localPos.x / GetWidth();
        progress = std::max(0.0f, std::min(progress, 1.0f));
        Seek(progress * duration);
    }
    
    void UpdateVolumeFromMouse(const Point2D& localPos) {
        float volumeX = localPos.x - 200; // Adjust based on actual volume slider position
        float newVolume = volumeX / controlsStyle.volumeSliderWidth;
        SetVolume(std::max(0.0f, std::min(newVolume, 1.0f)));
    }
    
    void CheckControlButtonClicks(const Point2D& localPos) {
        // Simplified button hit testing
        if (localPos.y > 60 && localPos.y < 90) {
            if (localPos.x < 30) {
                TogglePlayPause();
            } else if (localPos.x < 60) {
                Stop();
            }
        }
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasAudioElement> CreateAudioElement(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasAudioElement>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasAudioElement> CreateAudioFromFile(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const std::string& filePath) {
    auto element = CreateAudioElement(identifier, id, x, y, w, h);
    element->LoadFromFile(filePath);
    return element;
}

inline std::shared_ptr<UltraCanvasAudioElement> CreateAudioPlayer(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    auto element = CreateAudioElement(identifier, id, x, y, w, h);
    element->SetControlsStyle(AudioControlsStyle::Full());
    return element;
}

// ===== BUILDER PATTERN =====
class AudioElementBuilder {
private:
    std::string identifier = "AudioElement";
    long id = 0;
    long x = 0, y = 0, w = 400, h = 120;
    std::string filePath;
    AudioControlsStyle style = AudioControlsStyle::Default();
    bool autoPlay = false;
    bool loop = false;
    float volume = 1.0f;
    
public:
    AudioElementBuilder& SetIdentifier(const std::string& elementId) { identifier = elementId; return *this; }
    AudioElementBuilder& SetID(long elementId) { id = elementId; return *this; }
    AudioElementBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    AudioElementBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    AudioElementBuilder& SetFilePath(const std::string& path) { filePath = path; return *this; }
    AudioElementBuilder& SetStyle(const AudioControlsStyle& controlsStyle) { style = controlsStyle; return *this; }
    AudioElementBuilder& SetAutoPlay(bool enable) { autoPlay = enable; return *this; }
    AudioElementBuilder& SetLoop(bool enable) { loop = enable; return *this; }
    AudioElementBuilder& SetVolume(float vol) { volume = vol; return *this; }
    
    std::shared_ptr<UltraCanvasAudioElement> Build() {
        auto element = CreateAudioElement(identifier, id, x, y, w, h);
        
        element->SetControlsStyle(style);
        element->SetAutoPlay(autoPlay);
        element->SetLoop(loop);
        element->SetVolume(volume);
        
        if (!filePath.empty()) {
            element->LoadFromFile(filePath);
        }
        
        return element;
    }
};

// ===== AUDIO PLUGIN REGISTRY =====
class UltraCanvasAudioPluginRegistry {
private:
    static std::vector<std::shared_ptr<IAudioPlugin>> plugins;
    
public:
    static void RegisterPlugin(std::shared_ptr<IAudioPlugin> plugin) {
        plugins.push_back(plugin);
    }
    
    static void UnregisterPlugin(std::shared_ptr<IAudioPlugin> plugin) {
        plugins.erase(std::remove(plugins.begin(), plugins.end(), plugin), plugins.end());
    }
    
    static std::shared_ptr<IAudioPlugin> FindPluginForExtension(const std::string& extension) {
        for (auto& plugin : plugins) {
            if (plugin->CanHandle("dummy." + extension)) {
                return plugin;
            }
        }
        return nullptr;
    }
    
    static std::shared_ptr<IAudioPlugin> FindPluginForFile(const std::string& filePath) {
        for (auto& plugin : plugins) {
            if (plugin->CanHandle(filePath)) {
                return plugin;
            }
        }
        return nullptr;
    }
    
    static const std::vector<std::shared_ptr<IAudioPlugin>>& GetPlugins() {
        return plugins;
    }
    
    static void ClearPlugins() {
        plugins.clear();
    }
};

// ===== BASIC AUDIO PLUGIN IMPLEMENTATION =====
class BasicAudioPlugin : public IAudioPlugin {
private:
    std::unordered_set<std::string> supportedExtensions = {
        "wav", "mp3", "flac", "aac", "ogg", "opus", "m4a", "aiff"
    };
    
    AudioMetadata metadata;
    AudioState state = AudioState::Stopped;
    float volume = 1.0f;
    bool muted = false;
    float playbackRate = 1.0f;
    float position = 0.0f;
    bool loaded = false;
    
public:
    std::string GetPluginName() override { return "Basic Audio Plugin"; }
    std::string GetPluginVersion() override { return "1.0.0"; }
    
    std::vector<AudioFormat> GetSupportedFormats() override {
        return {AudioFormat::WAV, AudioFormat::MP3, AudioFormat::FLAC, 
                AudioFormat::AAC, AudioFormat::OGG, AudioFormat::OPUS};
    }
    
    std::vector<std::string> GetSupportedExtensions() override {
        return std::vector<std::string>(supportedExtensions.begin(), supportedExtensions.end());
    }
    
    bool CanHandle(const std::string& filePath) override {
        std::string ext = GetFileExtension(filePath);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return supportedExtensions.count(ext) > 0;
    }
    
    bool CanHandle(AudioFormat format) override {
        auto supportedFormats = GetSupportedFormats();
        return std::find(supportedFormats.begin(), supportedFormats.end(), format) != supportedFormats.end();
    }
    
    bool LoadAudio(const std::string& filePath) override {
        // Simplified loading - would use actual audio library
        metadata.filePath = filePath;
        metadata.fileName = GetFileName(filePath);
        metadata.format = DetectAudioFormat(filePath);
        metadata.duration = 180.0f; // Dummy duration
        loaded = true;
        state = AudioState::Stopped;
        return true;
    }
    
    bool LoadFromStream(const std::string& url) override {
        // Simplified streaming - would use actual streaming library
        return false; // Not implemented in basic plugin
    }
    
    AudioMetadata GetMetadata() override { return metadata; }
    bool IsLoaded() override { return loaded; }
    
    bool Play() override {
        if (!loaded) return false;
        state = AudioState::Playing;
        return true;
    }
    
    bool Pause() override {
        if (state == AudioState::Playing) {
            state = AudioState::Paused;
            return true;
        }
        return false;
    }
    
    bool Stop() override {
        state = AudioState::Stopped;
        position = 0.0f;
        return true;
    }
    
    bool Seek(float newPosition) override {
        if (!loaded) return false;
        position = std::max(0.0f, std::min(newPosition, metadata.duration));
        return true;
    }
    
    float GetPosition() override { return position; }
    float GetDuration() override { return metadata.duration; }
    AudioState GetState() override { return state; }
    
    void SetVolume(float vol) override { volume = vol; }
    float GetVolume() override { return volume; }
    void SetMuted(bool mute) override { muted = mute; }
    bool IsMuted() override { return muted; }
    void SetPlaybackRate(float rate) override { playbackRate = rate; }
    float GetPlaybackRate() override { return playbackRate; }
    
    bool SupportsStreaming() override { return false; }
    bool SupportsGaplessPlayback() override { return false; }
    bool SupportsEqualizer() override { return false; }
    bool SupportsVisualization() override { return false; }
    
    std::vector<float> GetSpectrumData(int bands) override { return std::vector<float>(bands, 0.0f); }
    std::vector<float> GetWaveformData(int samples) override { return std::vector<float>(samples, 0.0f); }
    float GetPeakLevel() override { return 0.0f; }
    float GetRMSLevel() override { return 0.0f; }

private:
    std::string GetFileExtension(const std::string& filePath) {
        size_t pos = filePath.find_last_of('.');
        return (pos != std::string::npos) ? filePath.substr(pos + 1) : "";
    }
    
    std::string GetFileName(const std::string& filePath) {
        size_t pos = filePath.find_last_of("/\\");
        return (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
    }
    
    AudioFormat DetectAudioFormat(const std::string& filePath) {
        std::string ext = GetFileExtension(filePath);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "wav") return AudioFormat::WAV;
        if (ext == "mp3") return AudioFormat::MP3;
        if (ext == "flac") return AudioFormat::FLAC;
        if (ext == "aac") return AudioFormat::AAC;
        if (ext == "ogg") return AudioFormat::OGG;
        if (ext == "opus") return AudioFormat::OPUS;
        if (ext == "m4a") return AudioFormat::M4A;
        if (ext == "aiff") return AudioFormat::AIFF;
        
        return AudioFormat::Unknown;
    }
};

// ===== CONVENIENCE FUNCTIONS =====
inline void RegisterBasicAudioPlugin() {
    UltraCanvasAudioPluginRegistry::RegisterPlugin(std::make_shared<BasicAudioPlugin>());
}

inline void InitializeAudioSystem() {
    RegisterBasicAudioPlugin();
}

inline void ShutdownAudioSystem() {
    UltraCanvasAudioPluginRegistry::ClearPlugins();
}

// ===== STATIC MEMBER DEFINITIONS =====
inline std::vector<std::shared_ptr<IAudioPlugin>> UltraCanvasAudioPluginRegistry::plugins;

} // namespace UltraCanvas