// UltraAI/include/UltraAIMusicGen.h
// Provider-agnostic music / song generation interface.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraAI {

// =====================================================================
// Output format / generation mode
// =====================================================================

enum class MusicAudioFormat {
    Mp3,
    Wav,
    Flac,
    Ogg,
    Opus,
    Midi
};

enum class MusicGenMode {
    Instrumental,        // no vocals
    Song,                // with vocals (requires lyrics or auto-generated)
    StyleTransfer,       // restyle reference audio
    Continuation,        // continue from referenceAudio
    SoundEffect          // short SFX clip
};

// =====================================================================
// Request / Response
// =====================================================================

struct MusicGenRequest {
    std::string model;                    // empty -> adapter default
    MusicGenMode mode = MusicGenMode::Instrumental;

    std::string prompt;                   // free-form description
    std::vector<std::string> genres;
    std::vector<std::string> instruments;
    std::vector<std::string> moods;

    int32_t bpm = 0;                      // 0 -> let model decide
    std::string key;                      // "C major", "A minor", ...
    double durationSec = 0.0;             // 0 -> model default
    int32_t count = 1;

    // Song-specific
    std::string lyrics;                   // empty + Song mode -> auto-generated
    std::string vocalStyle;               // "soulful", "rap", ...
    std::string language;                 // BCP-47 for vocals
    bool autoGenerateLyrics = false;

    // Inputs
    std::optional<MediaBlob> referenceAudio;   // style / continuation seed

    // Sampling
    std::optional<uint64_t> seed;
    std::optional<double>   guidanceScale;

    // Output
    MusicAudioFormat outputFormat = MusicAudioFormat::Mp3;
    int32_t sampleRateHz = 0;             // 0 -> format default
    bool returnStems = false;             // separate vocals/drums/bass/other

    OptionsMap options;
};

struct GeneratedTrack {
    MediaBlob audio;
    double durationSec = 0.0;
    int32_t bpm = 0;
    std::string key;
    std::string lyrics;                   // populated when generated
    std::vector<MediaBlob> stems;         // when returnStems=true
    uint64_t seed = 0;
    bool flaggedUnsafe = false;
};

struct MusicGenResponse {
    std::string model;
    std::vector<GeneratedTrack> tracks;
    TokenUsage usage;                     // tracks counted in usage.units
    Error error;
};

// =====================================================================
// Job-style progress (music gen can take a while)
// =====================================================================

enum class MusicJobEventKind {
    Queued,
    InProgress,
    PreviewSnippet,
    Completed,
    Error
};

struct MusicJobEvent {
    MusicJobEventKind kind = MusicJobEventKind::InProgress;
    double progress = 0.0;
    std::optional<MediaBlob> previewSnippet;
    std::optional<MusicGenResponse> result;
    Error error;
};

using MusicJobCallback = std::function<void(const MusicJobEvent&)>;

// =====================================================================
// Capability discovery
// =====================================================================

struct MusicGenModelInfo {
    std::string id;
    std::string displayName;
    std::vector<MusicGenMode> supportedModes;
    std::vector<std::string> supportedGenres;
    std::vector<std::string> supportedLanguages;     // for vocals
    std::vector<MusicAudioFormat> supportedFormats;
    double maxDurationSec = 0.0;
    bool supportsLyrics = false;
    bool supportsVocals = false;
    bool supportsStems = false;
    bool runsLocally = false;
};

struct MusicGenProviderCapabilities {
    std::string providerId;
    std::vector<MusicGenModelInfo> models;
};

// =====================================================================
// IMusicGen
// =====================================================================

class IMusicGen {
public:
    virtual ~IMusicGen() = default;

    virtual MusicGenProviderCapabilities GetCapabilities() const = 0;

    virtual MusicGenResponse Generate(const MusicGenRequest& request) = 0;

    virtual std::future<MusicGenResponse> GenerateAsync(
        const MusicGenRequest& request) = 0;

    virtual StreamHandle GenerateJob(const MusicGenRequest& request,
                                     MusicJobCallback onEvent) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using MusicGenConfig = ProviderConfig;

std::unique_ptr<IMusicGen> CreateMusicGen(const MusicGenConfig& config,
                                          Error* outError = nullptr);

std::vector<std::string> ListMusicGenProviders();

bool RegisterMusicGenProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<IMusicGen>(const MusicGenConfig&, Error*)> factory);

} // namespace UltraAI
