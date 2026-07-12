// UltraAI/adapters/mock/include/UltraAIMockSpeechToText.h
// Version: 0.1.0
#pragma once

#include "UltraAISpeechToText.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace UltraAI {

class MockSpeechToText : public ISpeechToText {
public:
    MockSpeechToText();
    ~MockSpeechToText() override;

    // Test programming
    void EnqueueTranscript(std::string text);
    void EnqueueResponse(TranscribeResponse response);
    void EnqueueError(Error error);
    void SetLiveChunkInterval(std::chrono::milliseconds interval); // default 5ms
    int CallCount() const;

    // ISpeechToText
    STTProviderCapabilities GetCapabilities() const override;
    TranscribeResponse Transcribe(const TranscribeRequest& request) override;
    std::future<TranscribeResponse> TranscribeAsync(const TranscribeRequest& request) override;
    LiveTranscriber StartLiveTranscribe(const TranscribeRequest& request,
                                        TranscriptCallback onEvent) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ISpeechToText> CreateMockSpeechToText();

} // namespace UltraAI
