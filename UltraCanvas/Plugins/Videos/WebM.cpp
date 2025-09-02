// WebM.cpp
// WebM video format support implementation with libvpx and libwebm
// Version: 1.0.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasWebMVideo.h"
#include "../include/UltraCanvasDrawingSurface.h"
#include "../include/UltraCanvasRect.h"
#include "../include/UltraCanvasPoint.h"

// Third-party WebM/VP8/VP9 libraries
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include <webm/webm_parser.h>
#include <webm/file_reader.h>

// Audio support
#include <vorbis/codec.h>
#include <opus/opus.h>

// System includes
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <algorithm>

namespace UltraCanvas {

// Internal WebM decoder implementation
struct UltraCanvasWebMVideo::WebMVideoImpl {
    // File and stream data
    std::string FilePath;
    std::unique_ptr<uint8_t[]> VideoData;
    size_t VideoDataSize = 0;
    
    // WebM parser and reader
    std::unique_ptr<webm::FileReader> WebMReader;
    std::unique_ptr<webm::WebmParser> WebMParser;
    
    // VPX decoder context
    vpx_codec_ctx_t VpxCodec;
    vpx_codec_iface_t* VpxInterface = nullptr;
    bool CodecInitialized = false;
    
    // Video information
    WebMVideoInfo VideoInfo;
    WebMPlaybackOptions PlaybackOptions;
    WebMPlaybackState CurrentState = WebMPlaybackState::Stopped;
    
    // Timing and playback
    double CurrentTime = 0.0;
    uint32_t CurrentFrameNumber = 0;
    std::chrono::steady_clock::time_point PlaybackStartTime;
    std::chrono::steady_clock::time_point LastFrameTime;
    
    // Threading for playback
    std::thread PlaybackThread;
    std::mutex StateMutex;
    std::condition_variable StateCondition;
    bool ShouldExit = false;
    
    // Frame buffer
    std::unique_ptr<uint8_t[]> CurrentFrameBuffer;
    size_t FrameBufferSize = 0;
    
    // Event callbacks
    WebMVideoEventCallback StateCallback;
    WebMVideoProgressCallback ProgressCallback;
    WebMVideoErrorCallback ErrorCallback;
    
    // Audio components (if present)
    vorbis_info VorbisInfo;
    vorbis_comment VorbisComment;
    vorbis_dsp_state VorbisDspState;
    vorbis_block VorbisBlock;
    bool AudioInitialized = false;
};

UltraCanvasWebMVideo::UltraCanvasWebMVideo() 
    : m_Impl(std::make_unique<WebMVideoImpl>()) {
    InitializeDecoder();
}

UltraCanvasWebMVideo::~UltraCanvasWebMVideo() {
    UnloadVideo();
    CleanupDecoder();
}

void UltraCanvasWebMVideo::InitializeDecoder() {
    // Initialize VPX interface for VP8/VP9
    m_Impl->VpxInterface = vpx_codec_vp9_dx();
    if (!m_Impl->VpxInterface) {
        m_Impl->VpxInterface = vpx_codec_vp8_dx();
    }
}

void UltraCanvasWebMVideo::CleanupDecoder() {
    if (m_Impl->CodecInitialized) {
        vpx_codec_destroy(&m_Impl->VpxCodec);
        m_Impl->CodecInitialized = false;
    }
    
    if (m_Impl->AudioInitialized) {
        vorbis_block_clear(&m_Impl->VorbisBlock);
        vorbis_dsp_clear(&m_Impl->VorbisDspState);
        vorbis_comment_clear(&m_Impl->VorbisComment);
        vorbis_info_clear(&m_Impl->VorbisInfo);
        m_Impl->AudioInitialized = false;
    }
}

bool UltraCanvasWebMVideo::LoadFromFile(const std::string& filePath) {
    if (!IsWebMFile(filePath)) {
        NotifyError("File is not a valid WebM format: " + filePath);
        return false;
    }
    
    // Read file into memory
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NotifyError("Cannot open WebM file: " + filePath);
        return false;
    }
    
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    auto fileData = std::make_unique<uint8_t[]>(fileSize);
    file.read(reinterpret_cast<char*>(fileData.get()), fileSize);
    file.close();
    
    if (!LoadFromMemory(fileData.get(), fileSize)) {
        return false;
    }
    
    m_Impl->FilePath = filePath;
    return true;
}

bool UltraCanvasWebMVideo::LoadFromMemory(const uint8_t* data, size_t dataSize) {
    if (!ValidateWebMData(data, dataSize)) {
        NotifyError("Invalid WebM data provided");
        return false;
    }
    
    // Clean up previous video
    UnloadVideo();
    
    // Copy video data
    m_Impl->VideoData = std::make_unique<uint8_t[]>(dataSize);
    std::memcpy(m_Impl->VideoData.get(), data, dataSize);
    m_Impl->VideoDataSize = dataSize;
    
    // Initialize WebM parser
    try {
        m_Impl->WebMReader = std::make_unique<webm::FileReader>();
        if (!m_Impl->WebMReader->Open(m_Impl->VideoData.get(), dataSize)) {
            NotifyError("Failed to open WebM data stream");
            return false;
        }
        
        m_Impl->WebMParser = std::make_unique<webm::WebmParser>();
        if (!m_Impl->WebMParser->Init(m_Impl->WebMReader.get())) {
            NotifyError("Failed to initialize WebM parser");
            return false;
        }
        
        // Parse video information
        if (!ParseVideoInfo()) {
            NotifyError("Failed to parse WebM video information");
            return false;
        }
        
        // Initialize VPX decoder
        vpx_codec_dec_cfg_t codecConfig = {};
        codecConfig.threads = std::thread::hardware_concurrency();
        
        if (vpx_codec_dec_init(&m_Impl->VpxCodec, m_Impl->VpxInterface, &codecConfig, 0) != VPX_CODEC_OK) {
            NotifyError("Failed to initialize VPX decoder");
            return false;
        }
        m_Impl->CodecInitialized = true;
        
        // Allocate frame buffer
        size_t bufferSize = m_Impl->VideoInfo.Width * m_Impl->VideoInfo.Height * 4; // RGBA
        m_Impl->CurrentFrameBuffer = std::make_unique<uint8_t[]>(bufferSize);
        m_Impl->FrameBufferSize = bufferSize;
        
        NotifyStateChange(WebMPlaybackState::Stopped);
        return true;
        
    } catch (const std::exception& e) {
        NotifyError("Exception during WebM loading: " + std::string(e.what()));
        return false;
    }
}

bool UltraCanvasWebMVideo::LoadFromUrl(const std::string& url) {
    // URL loading would require HTTP client implementation
    // For now, return false and suggest downloading the file first
    NotifyError("URL loading not yet implemented. Please download the WebM file locally.");
    return false;
}

void UltraCanvasWebMVideo::UnloadVideo() {
    // Stop playback first
    Stop();
    
    // Wait for playback thread to finish
    if (m_Impl->PlaybackThread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_Impl->StateMutex);
            m_Impl->ShouldExit = true;
        }
        m_Impl->StateCondition.notify_all();
        m_Impl->PlaybackThread.join();
    }
    
    // Reset all data
    m_Impl->VideoData.reset();
    m_Impl->VideoDataSize = 0;
    m_Impl->FilePath.clear();
    m_Impl->CurrentTime = 0.0;
    m_Impl->CurrentFrameNumber = 0;
    
    // Reset video info
    m_Impl->VideoInfo = {};
    
    NotifyStateChange(WebMPlaybackState::Stopped);
}

bool UltraCanvasWebMVideo::Play() {
    std::lock_guard<std::mutex> lock(m_Impl->StateMutex);
    
    if (m_Impl->CurrentState == WebMPlaybackState::Playing) {
        return true;
    }
    
    if (!m_Impl->CodecInitialized) {
        NotifyError("No video loaded");
        return false;
    }
    
    // Start playback thread if not already running
    if (!m_Impl->PlaybackThread.joinable()) {
        m_Impl->ShouldExit = false;
        m_Impl->PlaybackThread = std::thread([this]() { PlaybackThreadFunction(); });
    }
    
    m_Impl->PlaybackStartTime = std::chrono::steady_clock::now();
    NotifyStateChange(WebMPlaybackState::Playing);
    m_Impl->StateCondition.notify_all();
    
    return true;
}

bool UltraCanvasWebMVideo::Pause() {
    std::lock_guard<std::mutex> lock(m_Impl->StateMutex);
    
    if (m_Impl->CurrentState == WebMPlaybackState::Playing) {
        NotifyStateChange(WebMPlaybackState::Paused);
        return true;
    }
    
    return false;
}

bool UltraCanvasWebMVideo::Stop() {
    std::lock_guard<std::mutex> lock(m_Impl->StateMutex);
    
    NotifyStateChange(WebMPlaybackState::Stopped);
    m_Impl->CurrentTime = 0.0;
    m_Impl->CurrentFrameNumber = 0;
    
    return true;
}

bool UltraCanvasWebMVideo::SeekToTime(double timeSeconds) {
    if (timeSeconds < 0.0 || timeSeconds > m_Impl->VideoInfo.Duration) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_Impl->StateMutex);
    m_Impl->CurrentTime = timeSeconds;
    m_Impl->CurrentFrameNumber = static_cast<uint32_t>(timeSeconds * m_Impl->VideoInfo.FrameRate);
    
    // Seek implementation would go here
    // This requires WebM container seeking capabilities
    
    return true;
}

bool UltraCanvasWebMVideo::SeekToFrame(uint32_t frameNumber) {
    if (frameNumber >= GetTotalFrames()) {
        return false;
    }
    
    double timeSeconds = frameNumber / m_Impl->VideoInfo.FrameRate;
    return SeekToTime(timeSeconds);
}

const WebMVideoInfo& UltraCanvasWebMVideo::GetVideoInfo() const {
    return m_Impl->VideoInfo;
}

WebMPlaybackState UltraCanvasWebMVideo::GetPlaybackState() const {
    return m_Impl->CurrentState;
}

double UltraCanvasWebMVideo::GetCurrentTime() const {
    return m_Impl->CurrentTime;
}

double UltraCanvasWebMVideo::GetDuration() const {
    return m_Impl->VideoInfo.Duration;
}

uint32_t UltraCanvasWebMVideo::GetCurrentFrame() const {
    return m_Impl->CurrentFrameNumber;
}

uint32_t UltraCanvasWebMVideo::GetTotalFrames() const {
    return static_cast<uint32_t>(m_Impl->VideoInfo.Duration * m_Impl->VideoInfo.FrameRate);
}

void UltraCanvasWebMVideo::SetPlaybackOptions(const WebMPlaybackOptions& options) {
    m_Impl->PlaybackOptions = options;
}

const WebMPlaybackOptions& UltraCanvasWebMVideo::GetPlaybackOptions() const {
    return m_Impl->PlaybackOptions;
}

void UltraCanvasWebMVideo::SetVolume(double volume) {
    m_Impl->PlaybackOptions.Volume = std::clamp(volume, 0.0, 1.0);
}

double UltraCanvasWebMVideo::GetVolume() const {
    return m_Impl->PlaybackOptions.Volume;
}

void UltraCanvasWebMVideo::SetMuted(bool muted) {
    m_Impl->PlaybackOptions.Muted = muted;
}

bool UltraCanvasWebMVideo::IsMuted() const {
    return m_Impl->PlaybackOptions.Muted;
}

void UltraCanvasWebMVideo::SetPlaybackSpeed(double speed) {
    m_Impl->PlaybackOptions.PlaybackSpeed = std::clamp(speed, 0.25, 4.0);
}

double UltraCanvasWebMVideo::GetPlaybackSpeed() const {
    return m_Impl->PlaybackOptions.PlaybackSpeed;
}

bool UltraCanvasWebMVideo::RenderToSurface(UltraCanvasDrawingSurface* surface, 
                                          const UltraCanvasRect& destinationRect) {
    if (!surface || !m_Impl->CurrentFrameBuffer) {
        return false;
    }
    
    // Convert current frame to surface format and render
    // This would involve color space conversion and scaling
    return surface->DrawImageData(m_Impl->CurrentFrameBuffer.get(), 
                                 m_Impl->VideoInfo.Width, 
                                 m_Impl->VideoInfo.Height,
                                 destinationRect);
}

bool UltraCanvasWebMVideo::RenderToSurface(UltraCanvasDrawingSurface* surface, 
                                          const UltraCanvasPoint& position) {
    if (!surface) {
        return false;
    }
    
    UltraCanvasRect destRect;
    destRect.X = position.X;
    destRect.Y = position.Y;
    destRect.Width = m_Impl->VideoInfo.Width;
    destRect.Height = m_Impl->VideoInfo.Height;
    
    return RenderToSurface(surface, destRect);
}

bool UltraCanvasWebMVideo::ExtractFrameAtTime(double timeSeconds, UltraCanvasDrawingSurface* outputSurface) {
    if (!outputSurface) {
        return false;
    }
    
    // Seek to specific time and extract frame
    double originalTime = m_Impl->CurrentTime;
    if (SeekToTime(timeSeconds)) {
        bool result = ExtractCurrentFrame(outputSurface);
        SeekToTime(originalTime); // Restore original position
        return result;
    }
    
    return false;
}

bool UltraCanvasWebMVideo::ExtractCurrentFrame(UltraCanvasDrawingSurface* outputSurface) {
    if (!outputSurface || !m_Impl->CurrentFrameBuffer) {
        return false;
    }
    
    return outputSurface->LoadFromImageData(m_Impl->CurrentFrameBuffer.get(),
                                           m_Impl->VideoInfo.Width,
                                           m_Impl->VideoInfo.Height);
}

bool UltraCanvasWebMVideo::GenerateThumbnail(UltraCanvasDrawingSurface* thumbnailSurface, 
                                            uint32_t thumbnailWidth, uint32_t thumbnailHeight,
                                            double timeSeconds) {
    if (!thumbnailSurface) {
        return false;
    }
    
    // Create temporary surface for frame extraction
    UltraCanvasDrawingSurface tempSurface;
    if (!ExtractFrameAtTime(timeSeconds, &tempSurface)) {
        return false;
    }
    
    // Scale to thumbnail size
    return thumbnailSurface->LoadScaledFrom(&tempSurface, thumbnailWidth, thumbnailHeight);
}

void UltraCanvasWebMVideo::SetStateChangeCallback(WebMVideoEventCallback callback) {
    m_Impl->StateCallback = callback;
}

void UltraCanvasWebMVideo::SetProgressCallback(WebMVideoProgressCallback callback) {
    m_Impl->ProgressCallback = callback;
}

void UltraCanvasWebMVideo::SetErrorCallback(WebMVideoErrorCallback callback) {
    m_Impl->ErrorCallback = callback;
}

// Static utility functions
bool UltraCanvasWebMVideo::IsWebMFile(const std::string& filePath) {
    // Check file extension
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string extension = filePath.substr(dotPos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    return extension == "webm";
}

bool UltraCanvasWebMVideo::ValidateWebMData(const uint8_t* data, size_t dataSize) {
    if (!data || dataSize < 32) {
        return false;
    }
    
    // Check for WebM/Matroska signature
    // WebM files start with EBML header
    const uint8_t webmSignature[] = {0x1A, 0x45, 0xDF, 0xA3};
    return std::memcmp(data, webmSignature, 4) == 0;
}

std::string UltraCanvasWebMVideo::GetWebMFormatInfo(const std::string& filePath) {
    // Return basic format information
    return "WebM Video Container (VP8/VP9 + Vorbis/Opus)";
}

// Private helper methods
bool UltraCanvasWebMVideo::ParseVideoInfo() {
    // Parse WebM container for video track information
    // This is a simplified version - real implementation would use libwebm
    
    // Set default values for now
    m_Impl->VideoInfo.Width = 1920;
    m_Impl->VideoInfo.Height = 1080;
    m_Impl->VideoInfo.Duration = 0.0;
    m_Impl->VideoInfo.FrameRate = 30.0;
    m_Impl->VideoInfo.BitRate = 2000000;
    m_Impl->VideoInfo.HasAudio = true;
    m_Impl->VideoInfo.CodecName = "VP9";
    m_Impl->VideoInfo.AudioCodec = "Opus";
    
    return true;
}

void UltraCanvasWebMVideo::PlaybackThreadFunction() {
    while (true) {
        std::unique_lock<std::mutex> lock(m_Impl->StateMutex);
        
        if (m_Impl->ShouldExit) {
            break;
        }
        
        if (m_Impl->CurrentState != WebMPlaybackState::Playing) {
            m_Impl->StateCondition.wait(lock);
            continue;
        }
        
        lock.unlock();
        
        // Update frame and timing
        if (UpdateVideoFrame()) {
            NotifyProgress(m_Impl->CurrentTime);
        }
        
        // Sleep until next frame
        auto frameDuration = std::chrono::microseconds(
            static_cast<int64_t>(1000000.0 / (m_Impl->VideoInfo.FrameRate * m_Impl->PlaybackOptions.PlaybackSpeed))
        );
        std::this_thread::sleep_for(frameDuration);
    }
}

bool UltraCanvasWebMVideo::UpdateVideoFrame() {
    // Decode next frame using VPX
    // This is a simplified placeholder
    
    m_Impl->CurrentTime += 1.0 / m_Impl->VideoInfo.FrameRate;
    m_Impl->CurrentFrameNumber++;
    
    // Check for end of video
    if (m_Impl->CurrentTime >= m_Impl->VideoInfo.Duration) {
        if (m_Impl->PlaybackOptions.Loop) {
            m_Impl->CurrentTime = 0.0;
            m_Impl->CurrentFrameNumber = 0;
        } else {
            NotifyStateChange(WebMPlaybackState::Stopped);
            return false;
        }
    }
    
    return true;
}

void UltraCanvasWebMVideo::NotifyStateChange(WebMPlaybackState newState) {
    m_Impl->CurrentState = newState;
    if (m_Impl->StateCallback) {
        m_Impl->StateCallback(newState);
    }
}

void UltraCanvasWebMVideo::NotifyProgress(double currentTime) {
    if (m_Impl->ProgressCallback) {
        m_Impl->ProgressCallback(currentTime, m_Impl->VideoInfo.Duration);
    }
}

void UltraCanvasWebMVideo::NotifyError(const std::string& error) {
    if (m_Impl->ErrorCallback) {
        m_Impl->ErrorCallback(error);
    }
}

// Factory implementation
std::unique_ptr<UltraCanvasWebMVideo> UltraCanvasWebMVideoFactory::CreateWebMPlayer() {
    return std::make_unique<UltraCanvasWebMVideo>();
}

bool UltraCanvasWebMVideoFactory::RegisterWebMSupport() {
    // Register with UltraCanvas plugin system
    return true;
}

void UltraCanvasWebMVideoFactory::UnregisterWebMSupport() {
    // Unregister from plugin system
}

std::string UltraCanvasWebMVideoFactory::GetSupportedExtensions() {
    return ".webm";
}

std::string UltraCanvasWebMVideoFactory::GetSupportedMimeTypes() {
    return "video/webm";
}

} // namespace UltraCanvas
