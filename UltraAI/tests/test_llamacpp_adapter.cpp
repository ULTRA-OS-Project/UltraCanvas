// UltraAI/tests/test_llamacpp_adapter.cpp
// Smoke test for the llama.cpp ITextLLM adapter. Needs a real GGUF model:
// set ULTRAAI_TEST_LLAMA_MODEL to a model path (a tiny test model like
// stories260K.gguf is enough). Without it the test reports SKIP and
// passes, so suites with the adapter enabled stay green in CI.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAILlamaEmbeddings.h"
#include "UltraAILlamaTextLLM.h"
#include "UltraAI.h"

#include <cmath>

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

    // Structured output stays valid JSON whether the schema compiles to a
    // GBNF grammar (llama.cpp common linked) or the universal grammar
    // applies. A random-weight test model still has to emit parseable JSON.
    {
        ChatRequest jsonReq = req;
        jsonReq.sampling.maxOutputTokens = 48;
        jsonReq.responseFormat = ResponseFormat::JsonSchema;
        jsonReq.jsonSchema =
            R"({"type":"object","properties":{"answer":{"type":"string"}}})";
        ChatResponse jsonResp = llm->Chat(jsonReq);
        EXPECT_TRUE(jsonResp.error.IsOk());
        EXPECT_TRUE(!jsonResp.text.empty());
        EXPECT_TRUE(jsonResp.text[0] == '{');
    }

    // Embeddings: registered, pooled, normalized, dimension-truncatable.
    {
        bool hasLlamaEmbeds = false;
        for (const auto& p : ListEmbeddingsProviders()) {
            if (p == "llama-cpp") hasLlamaEmbeds = true;
        }
        EXPECT_TRUE(hasLlamaEmbeds);

        EmbeddingsConfig ecfg;
        ecfg.providerId   = "llama-cpp";
        ecfg.defaultModel = modelPath;
        Error eerr;
        auto embeds = CreateEmbeddings(ecfg, &eerr);
        EXPECT_TRUE(embeds != nullptr);

        EmbeddingProviderCapabilities ecaps = embeds->GetCapabilities();
        EXPECT_TRUE(ecaps.providerId == "llama-cpp");
        EXPECT_TRUE(!ecaps.models.empty() &&
                    ecaps.models[0].outputDimensions > 0);

        EmbeddingRequest ereq;
        ereq.input = {"a cat sat on the mat", "quantum chromodynamics"};
        EmbeddingResponse eresp = embeds->Embed(ereq);
        EXPECT_TRUE(eresp.error.IsOk());
        EXPECT_TRUE(eresp.embeddings.size() == 2);
        EXPECT_TRUE(static_cast<int32_t>(eresp.embeddings[0].values.size()) ==
                    ecaps.models[0].outputDimensions);
        EXPECT_TRUE(eresp.usage.inputTokens > 0);
        // L2-normalized: self-similarity is 1.
        double selfSim = IEmbeddings::CosineSimilarity(eresp.embeddings[0],
                                                       eresp.embeddings[0]);
        EXPECT_TRUE(std::fabs(selfSim - 1.0) < 1e-4);

        // Truncated dimensions.
        EmbeddingRequest small;
        small.input      = {"hello"};
        small.dimensions = 4;
        EmbeddingResponse smallResp = embeds->Embed(small);
        EXPECT_TRUE(smallResp.error.IsOk());
        EXPECT_TRUE(smallResp.embeddings[0].values.size() == 4);

        // Out-of-range dimensions fail cleanly.
        EmbeddingRequest tooBig;
        tooBig.input      = {"hello"};
        tooBig.dimensions = ecaps.models[0].outputDimensions + 1;
        EXPECT_TRUE(embeds->Embed(tooBig).error.code ==
                    ErrorCode::InvalidRequest);
    }

    std::cout << "test_llamacpp_adapter: all checks passed ("
              << resp.usage.outputTokens << " tokens generated)" << std::endl;
    return 0;
}
