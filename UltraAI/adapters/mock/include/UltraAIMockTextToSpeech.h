// UltraAI/adapters/mock/include/UltraAIMockTextToSpeech.h
// Version: 0.1.0
#pragma once

#include "UltraAITextToSpeech.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace UltraAI {

class MockTextToSpeech : public ITextToSpeech {
public:
    MockTextToSpeech();
    ~MockTextToSpeech() override;

    // Test programming
    void EnqueueAudio(std::vector<uint8_t> bytes, std::string mimeType = "audio/mpeg");
    void EnqueueError(Error error);
    void SetStreamChunkSize(size_t bytesPerChunk);   // default 16
    void SetStreamChunkInterval(std::chrono::milliseconds interval); // default 1ms
    int CallCount() const;

    // ITextToSpeech
    TTSProviderCapabilities GetCapabilities() const override;
    std::vector<VoiceInfo> ListVoices(const std::string& language = "") override;
    SpeakResponse Speak(const SpeakRequest& request) override;
    std::future<SpeakResponse> SpeakAsync(const SpeakRequest& request) override;
    StreamHandle SpeakStream(const SpeakRequest& request,
                             TtsStreamCallback onEvent) override;
    CloneVoiceResponse CloneVoice(const CloneVoiceRequest& request) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ITextToSpeech> CreateMockTextToSpeech();

} // namespace UltraAI
