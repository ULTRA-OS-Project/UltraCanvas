// UltraAI/include/UltraAISpeechToText.h
// Provider-agnostic speech-to-text (transcription) interface.
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
// Audio input description
// =====================================================================

enum class AudioEncoding {
    Auto,           // detect from container / mimeType
    PcmS16Le,       // raw 16-bit signed PCM, little-endian
    PcmF32Le,       // raw 32-bit float PCM
    Mp3,
    Wav,
    Flac,
    Opus,
    Ogg,
    M4a,
    Webm
};

struct AudioFormat {
    AudioEncoding encoding = AudioEncoding::Auto;
    int32_t sampleRateHz   = 0;     // 0 -> derive from container
    int32_t channels       = 0;     // 0 -> derive from container
};

// =====================================================================
// Request / Response
// =====================================================================

struct TranscribeRequest {
    std::string model;              // empty -> adapter default
    MediaBlob audio;                // file-like input
    AudioFormat format;

    std::string language;           // BCP-47, empty -> auto-detect
    bool translateToEnglish = false;
    std::string prompt;             // hint / vocabulary bias
    bool wordTimestamps = false;
    bool segmentTimestamps = true;
    bool diarize = false;           // speaker labels per segment
    int32_t maxSpeakers = 0;        // 0 -> auto

    OptionsMap options;
};

struct WordTiming {
    std::string word;
    double startSec = 0.0;
    double endSec   = 0.0;
    double confidence = 0.0;
};

struct TranscriptSegment {
    std::string text;
    double startSec = 0.0;
    double endSec   = 0.0;
    std::string speakerId;          // empty if diarization disabled
    std::vector<WordTiming> words;  // empty unless wordTimestamps requested
};

struct TranscribeResponse {
    std::string text;                       // full concatenated transcript
    std::string detectedLanguage;
    double durationSec = 0.0;
    std::vector<TranscriptSegment> segments;
    TokenUsage usage;
    Error error;
};

// =====================================================================
// Live / streaming transcription
// =====================================================================

enum class StreamingTranscriptKind {
    PartialText,        // interim hypothesis
    FinalText,          // segment finalised
    UsageUpdate,
    Done,
    Error
};

struct StreamingTranscriptEvent {
    StreamingTranscriptKind kind = StreamingTranscriptKind::PartialText;
    TranscriptSegment segment;
    Error error;
};

using TranscriptCallback = std::function<void(const StreamingTranscriptEvent&)>;

// Producer side of a live session. Caller pushes audio chunks; the
// implementation delivers events through the callback registered at start.
class ILiveTranscriber : public IStreamHandle {
public:
    virtual ~ILiveTranscriber() = default;

    // Push raw audio matching the format declared in StartLiveTranscribe.
    virtual void PushAudio(const uint8_t* data, size_t bytes) = 0;

    // Signal end-of-stream so any trailing partial is finalised.
    virtual void Finish() = 0;
};
using LiveTranscriber = std::shared_ptr<ILiveTranscriber>;

// =====================================================================
// Capability discovery
// =====================================================================

struct STTModelInfo {
    std::string id;
    std::string displayName;
    std::vector<std::string> supportedLanguages;  // BCP-47
    bool supportsTranslation = false;
    bool supportsDiarization = false;
    bool supportsWordTimestamps = false;
    bool supportsStreaming = false;
    bool runsLocally = false;
};

struct STTProviderCapabilities {
    std::string providerId;
    std::vector<STTModelInfo> models;
    std::vector<AudioEncoding> supportedEncodings;
    int32_t maxFileSizeBytes = 0;     // 0 -> unspecified
    double  maxDurationSec   = 0.0;   // 0 -> unspecified
};

// =====================================================================
// ISpeechToText — the capability interface
// =====================================================================

class ISpeechToText {
public:
    virtual ~ISpeechToText() = default;

    virtual STTProviderCapabilities GetCapabilities() const = 0;

    // File / blob transcription.
    virtual TranscribeResponse Transcribe(const TranscribeRequest& request) = 0;

    virtual std::future<TranscribeResponse> TranscribeAsync(
        const TranscribeRequest& request) = 0;

    // Live mic / stream transcription. The request's `audio` field is
    // ignored; instead, audio is fed via the returned LiveTranscriber.
    virtual LiveTranscriber StartLiveTranscribe(
        const TranscribeRequest& request,
        TranscriptCallback onEvent) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using SpeechToTextConfig = ProviderConfig;

std::unique_ptr<ISpeechToText> CreateSpeechToText(
    const SpeechToTextConfig& config,
    Error* outError = nullptr);

std::vector<std::string> ListSpeechToTextProviders();

bool RegisterSpeechToTextProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<ISpeechToText>(const SpeechToTextConfig&, Error*)> factory);

} // namespace UltraAI
