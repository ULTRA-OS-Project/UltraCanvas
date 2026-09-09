// UltraAI/tests/test_qwen_adapter.cpp
// Exercises the Qwen local adapter offline through ScriptedTransport:
// endpoint discovery across the candidate ports, model selection from
// GET /v1/models, delegation to the OpenAI-compatible wire format,
// capability reporting with runsLocally, the no-server error, an explicit
// baseUrl that does not list models, embeddings model preference, and
// factory registration.
//
// Uses plain asserts so the test suite has no third-party dependency
// beyond the repo-vendored nlohmann/json (used to inspect recorded
// request bodies).

#include "UltraAIQwen.h"
#include "UltraAI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraAI;
using nlohmann::json;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

// A transport-level failure, as ScriptedTransport::ScriptError takes it.
Error NetworkError(const std::string& message) {
    Error error;
    error.code    = ErrorCode::NetworkError;
    error.message = message;
    return error;
}

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers    = {{"content-type", "application/json"}};
    r.body       = body.dump();
    return r;
}

json ModelList(const std::vector<std::string>& ids) {
    json data = json::array();
    for (const std::string& id : ids) {
        data.push_back(json{{"id", id}, {"object", "model"}});
    }
    return json{{"object", "list"}, {"data", std::move(data)}};
}

json ChatCompletion(const std::string& text) {
    return json{{"id", "chat-1"},
                {"model", "qwen3:8b"},
                {"choices", json::array({json{
                    {"index", 0},
                    {"message", {{"role", "assistant"}, {"content", text}}},
                    {"finish_reason", "stop"}}})}};
}

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first == name) return kv.second;
    }
    return {};
}

void TestDiscoveryAndChat() {
    auto transport = std::make_shared<ScriptedTransport>();
    // The first candidate (Ollama) answers, so nothing else is probed.
    transport->ScriptResponse(JsonResponse(200, ModelList({
        "llama3.2:3b", "qwen3:8b", "qwen3-embedding:0.6b"})));
    transport->ScriptResponse(JsonResponse(200, ChatCompletion("hello")));

    TextLLMConfig cfg;
    cfg.providerId = "qwen";
    auto llm = CreateQwenTextLLM(cfg, nullptr, transport);
    EXPECT_TRUE(llm != nullptr);

    ChatRequest req;
    Message message;
    message.role = Role::User;
    message.text = "hi";
    req.messages.push_back(message);

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("hello"));

    const auto& requests = transport->Requests();
    EXPECT_EQ(requests.size(), size_t(2));
    EXPECT_EQ(requests[0].url, std::string("http://localhost:11434/v1/models"));
    EXPECT_EQ(requests[1].url,
              std::string("http://localhost:11434/v1/chat/completions"));
    // The Qwen chat model is chosen over the llama one, and over the Qwen
    // embedding model.
    EXPECT_EQ(json::parse(requests[1].body)["model"], "qwen3:8b");
    // A local server needs no credential, so none is sent.
    EXPECT_EQ(FindHeader(requests[1], "authorization"), std::string());
}

void TestCapabilitiesAndSecondCandidate() {
    auto transport = std::make_shared<ScriptedTransport>();
    // Ollama is not running; vLLM on 8000 answers.
    transport->ScriptError(NetworkError("connection refused"));
    transport->ScriptResponse(JsonResponse(200, ModelList({
        "Qwen/Qwen3-8B", "Qwen/Qwen3-Embedding-0.6B"})));

    TextLLMConfig cfg;
    auto llm = CreateQwenTextLLM(cfg, nullptr, transport);
    ProviderCapabilities caps = llm->GetCapabilities();

    EXPECT_EQ(caps.providerId, std::string("qwen"));
    // Embedding models are not chat models.
    EXPECT_EQ(caps.models.size(), size_t(1));
    EXPECT_EQ(caps.models[0].id, std::string("Qwen/Qwen3-8B"));
    EXPECT_TRUE(caps.models[0].runsLocally);
    EXPECT_EQ(transport->Requests().size(), size_t(2));
    EXPECT_EQ(transport->Requests()[1].url,
              std::string("http://localhost:8000/v1/models"));

    // Discovery is cached: a second call probes nothing.
    llm->GetCapabilities();
    EXPECT_EQ(transport->Requests().size(), size_t(2));
}

void TestNoServerRunning() {
    auto transport = std::make_shared<ScriptedTransport>();
    for (size_t i = 0; i < QwenDefaultEndpoints().size(); ++i) {
        transport->ScriptError(NetworkError("refused"));
    }

    TextLLMConfig cfg;
    auto llm = CreateQwenTextLLM(cfg, nullptr, transport);

    ChatRequest req;
    Message message;
    message.role = Role::User;
    message.text = "hi";
    req.messages.push_back(message);

    ChatResponse resp = llm->Chat(req);
    EXPECT_EQ(resp.error.code, ErrorCode::NetworkError);
    // The message has to name what was tried, or the user cannot act on it.
    EXPECT_TRUE(resp.error.message.find("localhost:11434") != std::string::npos);
    EXPECT_TRUE(resp.error.message.find("baseUrl") != std::string::npos);

    // Streaming fails the same way, through a terminal Error event rather
    // than a callback that never fires.
    bool sawError = false;
    StreamHandle handle = llm->ChatStream(req, [&](const StreamEvent& event) {
        if (event.kind == StreamEventKind::Error) sawError = true;
    });
    EXPECT_TRUE(sawError);
    EXPECT_TRUE(handle->IsDone());
}

void TestExplicitBaseUrlWithoutModelListing() {
    auto transport = std::make_shared<ScriptedTransport>();
    // A locked-down server that refuses /v1/models is still usable when the
    // caller names the model.
    transport->ScriptResponse(JsonResponse(403, json{{"error", "forbidden"}}));
    transport->ScriptResponse(JsonResponse(200, ChatCompletion("ok")));

    TextLLMConfig cfg;
    cfg.baseUrl = "http://gpu-box.lan:8000/";   // trailing slash on purpose
    cfg.apiKey  = "local-token";
    auto llm = CreateQwenTextLLM(cfg, nullptr, transport);

    ChatRequest req;
    req.model = "Qwen/Qwen3-32B";
    Message message;
    message.role = Role::User;
    message.text = "hi";
    req.messages.push_back(message);

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(transport->Requests()[1].url,
              std::string("http://gpu-box.lan:8000/v1/chat/completions"));
    EXPECT_EQ(FindHeader(transport->Requests()[1], "authorization"),
              std::string("Bearer local-token"));

    // With no model anywhere, the failure names the missing piece instead
    // of going to the server.
    auto bare = std::make_shared<ScriptedTransport>();
    bare->ScriptResponse(JsonResponse(200, ModelList({})));
    TextLLMConfig empty;
    empty.baseUrl = "http://gpu-box.lan:8000";
    auto llm2 = CreateQwenTextLLM(empty, nullptr, bare);
    ChatRequest bareReq;
    bareReq.messages.push_back(message);
    EXPECT_EQ(llm2->Chat(bareReq).error.code, ErrorCode::ModelNotFound);
    EXPECT_EQ(bare->Requests().size(), size_t(1));
}

void TestEmbeddings() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, ModelList({
        "qwen3:8b", "qwen3-embedding:0.6b"})));
    transport->ScriptResponse(JsonResponse(200, json{
        {"model", "qwen3-embedding:0.6b"},
        {"data", json::array({json{{"index", 0},
                                   {"embedding", json::array({0.5, 0.25})}}})}}));

    EmbeddingsConfig cfg;
    auto embeds = CreateQwenEmbeddings(cfg, nullptr, transport);
    EXPECT_TRUE(embeds != nullptr);

    EmbeddingRequest req;
    req.input = {"hello"};
    EmbeddingResponse resp = embeds->Embed(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.embeddings.size(), size_t(1));
    EXPECT_EQ(resp.embeddings[0].values.size(), size_t(2));

    // The embedding model is preferred over the chat model.
    EXPECT_EQ(json::parse(transport->Requests()[1].body)["model"],
              "qwen3-embedding:0.6b");
    EXPECT_EQ(transport->Requests()[1].url,
              std::string("http://localhost:11434/v1/embeddings"));
}

void TestFactoryRegistration() {
    const std::vector<std::string> textProviders = ListTextLLMProviders();
    EXPECT_TRUE(std::find(textProviders.begin(), textProviders.end(), "qwen") !=
                textProviders.end());

    const std::vector<std::string> embedProviders = ListEmbeddingsProviders();
    EXPECT_TRUE(std::find(embedProviders.begin(), embedProviders.end(), "qwen") !=
                embedProviders.end());

    EXPECT_TRUE(!QwenDefaultEndpoints().empty());
}

} // namespace

int main() {
    TestDiscoveryAndChat();
    TestCapabilitiesAndSecondCandidate();
    TestNoServerRunning();
    TestExplicitBaseUrlWithoutModelListing();
    TestEmbeddings();
    TestFactoryRegistration();
    std::cout << "test_qwen_adapter: all checks passed" << std::endl;
    return 0;
}
