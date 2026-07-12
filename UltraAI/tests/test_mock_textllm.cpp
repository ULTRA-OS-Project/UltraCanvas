// UltraAI/tests/test_mock_textllm.cpp
// Exercises the UltraAI ITextLLM contract through the in-process mock
// adapter. Validates: factory registration, blocking Chat, ChatAsync,
// ChatStream (including cancellation), tool-call routing, error
// surfacing, and the RawProvider escape hatch.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAI.h"
#include "UltraAIMockTextLLM.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace UltraAI;

namespace {

ChatRequest BuildSimpleRequest(const std::string& userText) {
    ChatRequest req;
    req.model = "mock-1";
    Message m;
    m.role = Role::User;
    m.text = userText;
    req.messages.push_back(std::move(m));
    return req;
}

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

void TestProviderRegistration() {
    auto providers = ListTextLLMProviders();
    bool hasMock = false;
    for (const auto& p : providers) if (p == "mock") { hasMock = true; break; }
    EXPECT_TRUE(hasMock);
}

void TestDefaultEcho() {
    TextLLMConfig cfg; cfg.providerId = "mock";
    auto llm = CreateTextLLM(cfg);
    EXPECT_TRUE(llm != nullptr);

    auto resp = llm->Chat(BuildSimpleRequest("hello world"));
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.text, std::string("Mock reply: hello world"));
    EXPECT_EQ(resp.finishReason, FinishReason::Stop);
}

void TestScriptedResponse() {
    TextLLMConfig cfg; cfg.providerId = "mock";
    auto llm = CreateTextLLM(cfg);
    auto* mock = static_cast<MockTextLLM*>(llm->RawProvider());
    EXPECT_TRUE(mock != nullptr);

    mock->EnqueueResponse("scripted answer");
    auto resp = llm->Chat(BuildSimpleRequest("ignored"));
    EXPECT_EQ(resp.text, std::string("scripted answer"));
}

void TestChatAsync() {
    auto llm = CreateMockTextLLM();
    auto* mock = static_cast<MockTextLLM*>(llm->RawProvider());
    mock->EnqueueResponse("async ok");

    auto fut = llm->ChatAsync(BuildSimpleRequest("anything"));
    auto resp = fut.get();
    EXPECT_EQ(resp.text, std::string("async ok"));
}

void TestStreaming() {
    auto llm = CreateMockTextLLM();
    auto* mock = static_cast<MockTextLLM*>(llm->RawProvider());
    mock->SetStreamDelay(std::chrono::milliseconds(0));
    mock->EnqueueResponse("one two three four");

    std::string assembled;
    std::atomic<int> deltas{0};
    std::atomic<bool> done{false};

    auto handle = llm->ChatStream(
        BuildSimpleRequest("stream me"),
        [&](const StreamEvent& ev) {
            switch (ev.kind) {
                case StreamEventKind::TextDelta:
                    assembled += ev.textDelta;
                    ++deltas;
                    break;
                case StreamEventKind::Done:
                    done = true;
                    break;
                case StreamEventKind::Error:
                    std::cerr << "stream error: " << ev.error.message << "\n";
                    std::abort();
                default: break;
            }
        });

    while (!handle->IsDone()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(deltas.load(), 4);
    EXPECT_EQ(assembled, std::string("one two three four"));
}

void TestStreamingCancellation() {
    auto llm = CreateMockTextLLM();
    auto* mock = static_cast<MockTextLLM*>(llm->RawProvider());
    mock->SetStreamDelay(std::chrono::milliseconds(20));
    mock->EnqueueResponse("a b c d e f g h i j k l m n o p");

    std::atomic<int> deltas{0};
    auto handle = llm->ChatStream(
        BuildSimpleRequest("long"),
        [&](const StreamEvent& ev) {
            if (ev.kind == StreamEventKind::TextDelta) ++deltas;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    handle->Cancel();
    while (!handle->IsDone()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(deltas.load() < 16);
}

void TestToolCallRouting() {
    auto llm = CreateMockTextLLM();
    auto* mock = static_cast<MockTextLLM*>(llm->RawProvider());

    ToolCall tc;
    tc.id = "call_1";
    tc.name = "get_weather";
    tc.argumentsJson = "{\"city\":\"Berlin\"}";
    mock->EnqueueToolCall(tc);

    auto resp = llm->Chat(BuildSimpleRequest("what's the weather?"));
    EXPECT_EQ(resp.finishReason, FinishReason::ToolCalls);
    EXPECT_EQ(resp.toolCalls.size(), size_t{1});
    EXPECT_EQ(resp.toolCalls[0].name, std::string("get_weather"));
}

void TestErrorPath() {
    auto llm = CreateMockTextLLM();
    auto* mock = static_cast<MockTextLLM*>(llm->RawProvider());

    Error e;
    e.code = ErrorCode::RateLimited;
    e.message = "slow down";
    mock->EnqueueError(e);

    auto resp = llm->Chat(BuildSimpleRequest("anything"));
    EXPECT_EQ(resp.error.code, ErrorCode::RateLimited);
    EXPECT_EQ(resp.finishReason, FinishReason::Error);
}

void TestUnknownProvider() {
    TextLLMConfig cfg; cfg.providerId = "definitely-not-installed";
    Error err;
    auto llm = CreateTextLLM(cfg, &err);
    EXPECT_TRUE(llm == nullptr);
    EXPECT_TRUE(err.code != ErrorCode::None);
}

void TestCountTokens() {
    auto llm = CreateMockTextLLM();
    Message m; m.role = Role::User; m.text = "12345678";   // 8 chars
    auto count = llm->CountTokens("mock-1", { m });
    EXPECT_EQ(count, 2);                                    // 8 / 4
}

void TestCapabilities() {
    auto llm = CreateMockTextLLM();
    auto caps = llm->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("mock"));
    EXPECT_TRUE(caps.supportsStreaming);
    EXPECT_TRUE(caps.supportsTools);
    EXPECT_TRUE(!caps.models.empty());
}

} // namespace

int main() {
    TestProviderRegistration();
    TestDefaultEcho();
    TestScriptedResponse();
    TestChatAsync();
    TestStreaming();
    TestStreamingCancellation();
    TestToolCallRouting();
    TestErrorPath();
    TestUnknownProvider();
    TestCountTokens();
    TestCapabilities();
    std::cout << "OK: ultraai_test_mock_textllm" << std::endl;
    return 0;
}
