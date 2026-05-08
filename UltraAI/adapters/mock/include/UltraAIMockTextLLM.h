// UltraAI/adapters/mock/include/UltraAIMockTextLLM.h
// In-process mock implementation of ITextLLM for testing and examples.
// No network, no external dependencies — useful as a baseline that proves
// the abstraction is implementable, and as a stand-in in unit tests.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module
#pragma once

#include "UltraAITextLLM.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraAI {

// =====================================================================
// Mock-specific configuration
//
// Tests configure responses via the concrete class accessed through
// ITextLLM::RawProvider(), which returns a MockTextLLM* — exercising the
// same escape-hatch pattern real adapters expose.
// =====================================================================

class MockTextLLM : public ITextLLM {
public:
    MockTextLLM();
    ~MockTextLLM() override;

    // ===== Test programming API =====

    // Queue scripted responses. Each Chat / ChatAsync / ChatStream call
    // pops the next response. When the queue is empty, the mock falls back
    // to echoing the last user message ("Mock reply: <text>").
    void EnqueueResponse(std::string text);
    void EnqueueResponse(ChatResponse response);

    // Enqueue an assistant tool-call response (used to verify tool-routing).
    void EnqueueToolCall(ToolCall call);

    // Override token counting (default: ~chars/4).
    void SetTokenCounter(std::function<int32_t(const std::vector<Message>&)> fn);

    // Streaming: how long to wait between word deltas (default 5 ms).
    void SetStreamDelay(std::chrono::milliseconds delay);

    // Force errors for the next N calls to test error paths.
    void EnqueueError(Error error);

    // Counters for assertions.
    int CallCount() const;
    void ResetCounters();

    // ===== ITextLLM =====

    ProviderCapabilities GetCapabilities() const override;

    ChatResponse Chat(const ChatRequest& request) override;

    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override;

    StreamHandle ChatStream(const ChatRequest& request,
                            StreamCallback onEvent) override;

    int32_t CountTokens(const std::string& model,
                        const std::vector<Message>& messages) override;

    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Direct factory (does not depend on the registry).
std::unique_ptr<ITextLLM> CreateMockTextLLM();

} // namespace UltraAI
