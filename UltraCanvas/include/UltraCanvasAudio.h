// include/UltraCanvasAudio.h
// Base interface for cross-platform audio resources (PCM buffers, format metadata)
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASAUDIO_H
#define ULTRACANVASAUDIO_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== AUDIO FORMAT =====
enum class AudioFormat {
    Autodetect,
    WAV,
    MP3,
    OGG,        // Vorbis
    FLAC,
    AAC,
    Opus,
    PCM,        // Raw PCM (no container)
    Unknown
};

// ===== AUDIO SAMPLE TYPE =====
enum class AudioSampleType {
    PCM_S16,    // 16-bit signed integer (most common)
    PCM_S24,    // 24-bit signed integer
    PCM_S32,    // 32-bit signed integer
    PCM_F32     // 32-bit float (-1.0..1.0)
};

// ===== AUDIO BUFFER DESCRIPTION =====
struct AudioBufferInfo {
    int sampleRate = 44100;
    int channels = 2;
    AudioSampleType sampleType = AudioSampleType::PCM_S16;
    size_t frameCount = 0;     // Frames = samples per channel
    double durationSeconds = 0.0;

    size_t BytesPerSample() const;
    size_t BytesPerFrame() const { return BytesPerSample() * channels; }
    size_t TotalBytes() const { return BytesPerFrame() * frameCount; }
};

// ===== UCAUDIO RESOURCE =====
// Holds a decoded PCM buffer plus its metadata. Parallels UCImage.
class UCAudio {
public:
    UCAudio() = default;
    ~UCAudio() = default;

    // Loading
    static std::shared_ptr<UCAudio> LoadFromFile(const std::string& filePath,
                                                 AudioFormat hint = AudioFormat::Autodetect);
    static std::shared_ptr<UCAudio> LoadFromMemory(const uint8_t* data, size_t size,
                                                   AudioFormat hint = AudioFormat::Autodetect);
    static std::shared_ptr<UCAudio> FromRawPCM(const void* samples, size_t bytes,
                                               const AudioBufferInfo& info);

    // Saving (WAV always available; encoded formats require encoder plugin)
    bool SaveToFile(const std::string& filePath, AudioFormat format = AudioFormat::WAV) const;

    // Accessors
    bool IsValid() const { return valid && !data.empty(); }
    const AudioBufferInfo& GetInfo() const { return info; }
    double GetDuration() const { return info.durationSeconds; }
    int GetSampleRate() const { return info.sampleRate; }
    int GetChannels() const { return info.channels; }
    AudioFormat GetSourceFormat() const { return sourceFormat; }
    const std::string& GetSourcePath() const { return sourcePath; }

    // Raw PCM access (read-only)
    const uint8_t* GetData() const { return data.data(); }
    size_t GetDataSize() const { return data.size(); }

    // Allow recorders to populate the buffer in-place
    std::vector<uint8_t>& MutableData() { return data; }
    void SetInfo(const AudioBufferInfo& i) { info = i; valid = true; }
    void SetSourceFormat(AudioFormat f) { sourceFormat = f; }
    void SetSourcePath(const std::string& p) { sourcePath = p; }

private:
    AudioBufferInfo info;
    AudioFormat sourceFormat = AudioFormat::Unknown;
    std::string sourcePath;
    std::vector<uint8_t> data;
    bool valid = false;
};

} // namespace UltraCanvas

#endif // ULTRACANVASAUDIO_H
