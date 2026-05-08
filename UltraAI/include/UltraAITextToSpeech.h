// UltraAI/include/UltraAITextToSpeech.h
// Provider-agnostic text-to-speech (synthesis) interface.
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
// Audio output description
// =====================================================================

enum class TtsAudioFormat {
    Mp3,
    Wav,
    Flac,
    Opus,
    Ogg,
    PcmS16Le,
    PcmF32Le
};

// =====================================================================
// Voices
// =====================================================================

enum class VoiceGender {
    Unspecified,
    Female,
    Male,
    Neutral
};

struct VoiceInfo {
    std::string id;
    std::string displayName;
    std::string language;       // BCP-47
    VoiceGender gender = VoiceGender::Unspecified;
    std::vector<std::string> styles;   // e.g. "calm", "narrative", "newscaster"
    bool isCloned = false;
    bool runsLocally = false;
};

// =====================================================================
// Request / Response
// =====================================================================

struct SpeakRequest {
    std::string model;           // empty -> adapter default
    std::string text;            // plain text or SSML if `ssml` is true
    bool ssml = false;

    std::string voiceId;         // empty -> adapter default
    std::string language;        // optional override
    std::string style;           // optional, model-dependent

    TtsAudioFormat format = TtsAudioFormat::Mp3;
    int32_t sampleRateHz = 0;    // 0 -> format default
    double speed  = 1.0;         // 1.0 = normal
    double pitch  = 0.0;         // semitones (provider may ignore)
    double volume = 1.0;         // 0.0 - 1.0 linear gain

    OptionsMap options;
};

struct SpeakResponse {
    MediaBlob audio;             // full synthesized audio
    double durationSec = 0.0;
    TokenUsage usage;            // characters in usage.units when applicable
    Error error;
};

// =====================================================================
// Streaming synthesis (low-latency playback)
// =====================================================================

enum class TtsStreamEventKind {
    AudioChunk,
    UsageUpdate,
    Done,
    Error
};

struct TtsStreamEvent {
    TtsStreamEventKind kind = TtsStreamEventKind::AudioChunk;
    std::vector<uint8_t> audioChunk;   // encoded per request.format
    double timestampSec = 0.0;
    Error error;
};

using TtsStreamCallback = std::function<void(const TtsStreamEvent&)>;

// =====================================================================
// Voice cloning (optional capability)
// =====================================================================

struct CloneVoiceRequest {
    std::string displayName;
    std::vector<MediaBlob> samples;   // reference recordings
    std::string consentText;          // some providers require this
    OptionsMap options;
};

struct CloneVoiceResponse {
    VoiceInfo voice;
    Error error;
};

// =====================================================================
// Capability discovery
// =====================================================================

struct TTSModelInfo {
    std::string id;
    std::string displayName;
    std::vector<std::string> supportedLanguages;
    std::vector<TtsAudioFormat> supportedFormats;
    bool supportsSsml = false;
    bool supportsStreaming = false;
    bool supportsVoiceCloning = false;
    bool runsLocally = false;
};

struct TTSProviderCapabilities {
    std::string providerId;
    std::vector<TTSModelInfo> models;
};

// =====================================================================
// ITextToSpeech — the capability interface
// =====================================================================

class ITextToSpeech {
public:
    virtual ~ITextToSpeech() = default;

    virtual TTSProviderCapabilities GetCapabilities() const = 0;

    // List available voices (filtered by language if non-empty).
    virtual std::vector<VoiceInfo> ListVoices(
        const std::string& language = "") = 0;

    // One-shot synthesis returning the full audio.
    virtual SpeakResponse Speak(const SpeakRequest& request) = 0;

    virtual std::future<SpeakResponse> SpeakAsync(
        const SpeakRequest& request) = 0;

    // Streaming synthesis: chunks delivered as soon as they are produced.
    // Final event is always Done or Error.
    virtual StreamHandle SpeakStream(const SpeakRequest& request,
                                     TtsStreamCallback onEvent) = 0;

    // Optional: voice cloning. Returns Error::UnsupportedFormat (or similar)
    // when the provider does not support cloning.
    virtual CloneVoiceResponse CloneVoice(const CloneVoiceRequest& request) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using TextToSpeechConfig = ProviderConfig;

std::unique_ptr<ITextToSpeech> CreateTextToSpeech(
    const TextToSpeechConfig& config,
    Error* outError = nullptr);

std::vector<std::string> ListTextToSpeechProviders();

} // namespace UltraAI
