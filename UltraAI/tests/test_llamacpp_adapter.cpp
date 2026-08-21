// UltraAI/tests/test_llamacpp_adapter.cpp
// Smoke test for the llama.cpp ITextLLM adapter. Needs a real GGUF model:
// set ULTRAAI_TEST_LLAMA_MODEL to a model path (a tiny test model like
// stories260K.gguf is enough). Without it the test reports SKIP and
// passes, so suites with the adapter enabled stay green in CI.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAILlamaTextLLM.h"
#include "UltraAI.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace UltraAI;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

} // namespace

int main() {
    // Provider is registered whether or not a model is available.
    bool hasLlama = false;
    for (const auto& p : ListTextLLMProviders()) {
        if (p == "llama-cpp") hasLlama = true;
    }
    EXPECT_TRUE(hasLlama);

    // A missing model path fails cleanly.
    {
        TextLLMConfig bad;
        bad.providerId = "llama-cpp";
        Error err;
        auto llm = CreateLlamaTextLLM(bad, &err);
        EXPECT_TRUE(llm == nullptr);
        EXPECT_TRUE(err.code == ErrorCode::InvalidRequest);
    }

    const char* modelPath = std::getenv("ULTRAAI_TEST_LLAMA_MODEL");
    if (!modelPath || !*modelPath) {
        std::cout << "test_llamacpp_adapter: SKIP inference checks "
                     "(ULTRAAI_TEST_LLAMA_MODEL not set)" << std::endl;
        return 0;
    }

    TextLLMConfig cfg;
    cfg.providerId   = "llama-cpp";
    cfg.defaultModel = modelPath;
    Error err;
    auto llm = CreateTextLLM(cfg, &err);
    EXPECT_TRUE(llm != nullptr);

    ProviderCapabilities caps = llm->GetCapabilities();
    EXPECT_TRUE(caps.providerId == "llama-cpp");
    EXPECT_TRUE(!caps.models.empty() && caps.models[0].runsLocally);

    ChatRequest req;
    Message m; m.role = Role::User; m.text = "Once upon a time";
    req.messages.push_back(m);
    req.sampling.maxOutputTokens = 32;
    req.sampling.temperature     = 0.0;    // greedy: deterministic

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_TRUE(!resp.text.empty());
    EXPECT_TRUE(resp.usage.inputTokens > 0);
    EXPECT_TRUE(resp.usage.outputTokens > 0);
    EXPECT_TRUE(resp.finishReason == FinishReason::Stop ||
                resp.finishReason == FinishReason::Length);

    // Streaming produces the same greedy text, delta by delta.
    std::string streamed;
    bool done = false;
    auto handle = llm->ChatStream(req, [&](const StreamEvent& ev) {
        if (ev.kind == StreamEventKind::TextDelta) streamed += ev.textDelta;
        if (ev.kind == StreamEventKind::Done)      done = true;
        if (ev.kind == StreamEventKind::Error) {
            std::cerr << "stream error: " << ev.error.message << std::endl;
            std::abort();
        }
    });
    while (!handle->IsDone()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done);
    EXPECT_TRUE(streamed == resp.text);

    // Exact token counting counts the templated prompt.
    EXPECT_TRUE(llm->CountTokens("", req.messages) > 0);

    std::cout << "test_llamacpp_adapter: all checks passed ("
              << resp.usage.outputTokens << " tokens generated)" << std::endl;
    return 0;
}
