// UltraAI/tests/test_openai_adapter.cpp
// Exercises the OpenAI ITextLLM + IEmbeddings adapters offline through
// ScriptedTransport: Chat Completions serialization (messages, tool calls,
// tool results, sampling, structured output, Bearer auth), response parsing
// (text, tool calls, usage, finish reasons), provider error mapping, SSE
// chunk assembly ([DONE] sentinel, split tool-call arguments, usage chunk),
// the keyless-when-self-hosted rule, and the embeddings endpoint.
//
// Uses plain asserts so the test suite has no third-party dependency
// beyond the repo-vendored nlohmann/json (used to inspect recorded
// request bodies).

#include "UltraAIOpenAI.h"
#include "UltraAI.h"

#include <nlohmann/json.hpp>

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

TextLLMConfig TestConfig() {
    TextLLMConfig cfg;
    cfg.providerId   = "openai";
    cfg.apiKey       = "sk-test-key";
    cfg.defaultModel = "gpt-5-mini";
    return cfg;
}

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first == name) return kv.second;
    }
    return {};
}

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers    = {{"content-type", "application/json"}};
    r.body       = body.dump();
    return r;
}

json ChatCompletion(const std::string& id, const json& message,
                    const std::string& finishReason,
                    const json& usage = json()) {
    json j = {{"id", id}, {"model", "gpt-5-mini"},
              {"choices", json::array({json{
                  {"index", 0},
                  {"message", message},
                  {"finish_reason", finishReason}}})}};
    if (usage.is_object()) j["usage"] = usage;
    return j;
}

void TestRequestSerialization() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, ChatCompletion(
        "chatcmpl-1", json{{"role", "assistant"}, {"content", "hi"}}, "stop",
        json{{"prompt_tokens", 10}, {"completion_tokens", 2}})));

    auto llm = CreateOpenAITextLLM(TestConfig(), nullptr, transport);
    EXPECT_TRUE(llm != nullptr);

    ChatRequest req;
    req.model = "gpt-5";
    Message sys;  sys.role = Role::System;    sys.text = "Be terse.";
    Message user; user.role = Role::User;     user.text = "hello";
    Message asst; asst.role = Role::Assistant;
    ToolCall priorCall;
    priorCall.id = "call_1"; priorCall.name = "get_weather";
    priorCall.argumentsJson = R"({"location":"Paris"})";
    asst.toolCalls.push_back(priorCall);
    Message toolMsg; toolMsg.role = Role::Tool;
    toolMsg.toolCallId = "call_1"; toolMsg.text = "22C and sunny";
    req.messages = {sys, user, asst, toolMsg};

    ToolDefinition tool;
    tool.name = "get_weather";
    tool.description = "Get the weather";
    tool.parametersJsonSchema =
        R"({"type":"object","properties":{"location":{"type":"string"}},"required":["location"]})";
    req.tools.push_back(tool);
    req.toolChoice = ToolChoice::Auto;

    req.sampling.maxOutputTokens  = 512;
    req.sampling.temperature      = 0.5;
    req.sampling.seed             = 42;
    req.sampling.presencePenalty  = 0.1;
    req.sampling.frequencyPenalty = 0.2;
    req.sampling.stopSequences    = {"END"};

    req.responseFormat = ResponseFormat::JsonSchema;
    req.jsonSchema = R"({"type":"object","properties":{"answer":{"type":"string"}}})";

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("hi"));
    EXPECT_EQ(resp.finishReason, FinishReason::Stop);
    EXPECT_EQ(resp.usage.inputTokens, 10);
    EXPECT_EQ(resp.id, std::string("chatcmpl-1"));

    EXPECT_EQ(transport->Requests().size(), static_cast<size_t>(1));
    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.method, std::string("POST"));
    EXPECT_EQ(sent.url,
              std::string("https://api.openai.com/v1/chat/completions"));
    EXPECT_EQ(FindHeader(sent, "authorization"),
              std::string("Bearer sk-test-key"));

    json body = json::parse(sent.body);
    EXPECT_EQ(body["model"], std::string("gpt-5"));
    EXPECT_EQ(body["max_completion_tokens"], 512);
    EXPECT_EQ(body["temperature"], 0.5);
    EXPECT_EQ(body["seed"], 42);
    EXPECT_EQ(body["presence_penalty"], 0.1);
    EXPECT_EQ(body["frequency_penalty"], 0.2);
    EXPECT_EQ(body["stop"][0], std::string("END"));

    // Unlike Anthropic there is no system lift: all four messages ride in
    // the messages array with their native roles.
    EXPECT_EQ(body["messages"].size(), static_cast<size_t>(4));
    EXPECT_EQ(body["messages"][0]["role"], std::string("system"));
    EXPECT_EQ(body["messages"][0]["content"], std::string("Be terse."));
    EXPECT_EQ(body["messages"][1]["role"], std::string("user"));
    EXPECT_EQ(body["messages"][1]["content"], std::string("hello"));
    EXPECT_EQ(body["messages"][2]["role"], std::string("assistant"));
    EXPECT_EQ(body["messages"][2]["tool_calls"][0]["id"], std::string("call_1"));
    EXPECT_EQ(body["messages"][2]["tool_calls"][0]["function"]["name"],
              std::string("get_weather"));
    EXPECT_TRUE(!body["messages"][2].contains("content"));
    EXPECT_EQ(body["messages"][3]["role"], std::string("tool"));
    EXPECT_EQ(body["messages"][3]["tool_call_id"], std::string("call_1"));
    EXPECT_EQ(body["messages"][3]["content"], std::string("22C and sunny"));

    EXPECT_EQ(body["tools"][0]["type"], std::string("function"));
    EXPECT_EQ(body["tools"][0]["function"]["name"], std::string("get_weather"));
    EXPECT_TRUE(body["tools"][0]["function"]["parameters"].is_object());
    EXPECT_TRUE(!body.contains("tool_choice"));   // Auto is the API default

    EXPECT_EQ(body["response_format"]["type"], std::string("json_schema"));
    EXPECT_TRUE(body["response_format"]["json_schema"]["schema"].is_object());
    EXPECT_TRUE(!body.contains("stream"));
}

void TestToolCallResponse() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, ChatCompletion(
        "chatcmpl-2",
        json{{"role", "assistant"}, {"content", nullptr},
             {"tool_calls", json::array({json{
                 {"id", "call_9"}, {"type", "function"},
                 {"function", {{"name", "get_weather"},
                               {"arguments", R"({"location":"Berlin"})"}}}}})}},
        "tool_calls",
        json{{"prompt_tokens", 30}, {"completion_tokens", 15},
             {"prompt_tokens_details", {{"cached_tokens", 12}}}})));

    auto llm = CreateOpenAITextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "weather in berlin?";
    req.messages.push_back(m);

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.finishReason, FinishReason::ToolCalls);
    EXPECT_EQ(resp.toolCalls.size(), static_cast<size_t>(1));
    EXPECT_EQ(resp.toolCalls[0].id, std::string("call_9"));
    EXPECT_EQ(resp.toolCalls[0].name, std::string("get_weather"));
    json args = json::parse(resp.toolCalls[0].argumentsJson);
    EXPECT_EQ(args["location"], std::string("Berlin"));
    EXPECT_EQ(resp.usage.cachedInputTokens, 12);

    // Default model applies when the request leaves it empty.
    json body = json::parse(transport->Requests()[0].body);
    EXPECT_EQ(body["model"], std::string("gpt-5-mini"));
}

void TestProviderErrorMapping() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(429, json{
        {"error", {{"type", "rate_limit_error"},
                   {"message", "Too many requests"}}}}));
    transport->ScriptResponse(JsonResponse(401, json{
        {"error", {{"type", "invalid_request_error"},
                   {"code", "invalid_api_key"},
                   {"message", "Incorrect API key provided"}}}}));
    transport->ScriptResponse(JsonResponse(429, json{
        {"error", {{"type", "insufficient_quota"},
                   {"code", "insufficient_quota"},
                   {"message", "You exceeded your current quota"}}}}));

    auto llm = CreateOpenAITextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);

    ChatResponse r1 = llm->Chat(req);
    EXPECT_EQ(r1.error.code, ErrorCode::RateLimited);
    EXPECT_TRUE(r1.error.message.find("Too many requests") != std::string::npos);

    ChatResponse r2 = llm->Chat(req);
    EXPECT_EQ(r2.error.code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(r2.error.providerCode, std::string("invalid_api_key"));

    ChatResponse r3 = llm->Chat(req);
    EXPECT_EQ(r3.error.code, ErrorCode::QuotaExceeded);
}

void TestMissingModel() {
    auto transport = std::make_shared<ScriptedTransport>();
    TextLLMConfig cfg;
    cfg.apiKey = "sk-test-key";              // no defaultModel
    auto llm = CreateOpenAITextLLM(cfg, nullptr, transport);

    ChatRequest req;                          // no model either
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    EXPECT_EQ(resp.error.code, ErrorCode::InvalidRequest);
    EXPECT_TRUE(transport->Requests().empty());
}

void TestStreaming() {
    auto transport = std::make_shared<ScriptedTransport>();

    auto chunk = [](const json& data) {
        TransportSseEvent ev;
        ev.data = data.dump();
        return ev;
    };
    auto deltaChunk = [&chunk](const json& delta) {
        return chunk(json{{"choices", json::array({json{
            {"index", 0}, {"delta", delta}}})}});
    };
    TransportSseEvent doneSentinel;
    doneSentinel.data = "[DONE]";

    std::vector<TransportSseEvent> script = {
        deltaChunk(json{{"role", "assistant"}, {"content", ""}}),
        deltaChunk(json{{"content", "Hel"}}),
        deltaChunk(json{{"content", "lo"}}),
        deltaChunk(json{{"tool_calls", json::array({json{
            {"index", 0}, {"id", "call_s1"},
            {"function", {{"name", "lookup"}, {"arguments", ""}}}}})}}),
        deltaChunk(json{{"tool_calls", json::array({json{
            {"index", 0},
            {"function", {{"arguments", "{\"q\":"}}}}})}}),
        deltaChunk(json{{"tool_calls", json::array({json{
            {"index", 0},
            {"function", {{"arguments", "\"cats\"}"}}}}})}}),
        chunk(json{{"choices", json::array({json{
            {"index", 0}, {"delta", json::object()},
            {"finish_reason", "tool_calls"}}})}}),
        // include_usage: final chunk with empty choices carries usage.
        chunk(json{{"choices", json::array()},
                   {"usage", {{"prompt_tokens", 25},
                              {"completion_tokens", 40}}}}),
        doneSentinel,
    };
    transport->ScriptSse(script);

    auto llm = CreateOpenAITextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "stream please";
    req.messages.push_back(m);

    std::string text;
    ToolCall finishedCall;
    bool sawUsage = false, sawDone = false;
    FinishReason finish = FinishReason::Unknown;
    TokenUsage finalUsage;

    StreamHandle handle = llm->ChatStream(req, [&](const StreamEvent& ev) {
        switch (ev.kind) {
            case StreamEventKind::TextDelta:     text += ev.textDelta; break;
            case StreamEventKind::ToolCallDelta: break;
            case StreamEventKind::ToolCallEnd:   finishedCall = ev.toolCallDelta; break;
            case StreamEventKind::UsageUpdate:   sawUsage = true; break;
            case StreamEventKind::Done:
                sawDone = true; finish = ev.finishReason; finalUsage = ev.usage;
                break;
            case StreamEventKind::Error:
                std::cerr << "unexpected stream error: " << ev.error.message
                          << std::endl;
                std::abort();
        }
    });

    EXPECT_TRUE(handle != nullptr);
    EXPECT_TRUE(handle->IsDone());          // ScriptedTransport runs inline
    EXPECT_EQ(text, std::string("Hello"));
    EXPECT_EQ(finishedCall.id, std::string("call_s1"));
    EXPECT_EQ(finishedCall.name, std::string("lookup"));
    json args = json::parse(finishedCall.argumentsJson);
    EXPECT_EQ(args["q"], std::string("cats"));
    EXPECT_TRUE(sawUsage);
    EXPECT_TRUE(sawDone);
    EXPECT_EQ(finish, FinishReason::ToolCalls);
    EXPECT_EQ(finalUsage.inputTokens, 25);
    EXPECT_EQ(finalUsage.outputTokens, 40);

    // The streamed request carries stream=true and asks for the usage chunk.
    json body = json::parse(transport->Requests()[0].body);
    EXPECT_TRUE(body["stream"].get<bool>());
    EXPECT_TRUE(body["stream_options"]["include_usage"].get<bool>());
}

void TestStreamTerminalError() {
    auto transport = std::make_shared<ScriptedTransport>();
    Error rate;
    rate.code = ErrorCode::RateLimited;
    transport->ScriptSse({}, rate, 429);

    auto llm = CreateOpenAITextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);

    bool sawError = false;
    llm->ChatStream(req, [&](const StreamEvent& ev) {
        if (ev.kind == StreamEventKind::Error) {
            sawError = true;
            EXPECT_EQ(ev.error.code, ErrorCode::RateLimited);
        }
    });
    EXPECT_TRUE(sawError);
}

void TestMissingCredentials() {
    // Against the hosted API a key is mandatory...
    auto transport = std::make_shared<ScriptedTransport>();
    TextLLMConfig cfg;                       // no apiKey, no vault ref
    cfg.defaultModel = "gpt-5-mini";
    auto llm = CreateOpenAITextLLM(cfg, nullptr, transport);
    EXPECT_TRUE(llm != nullptr);

    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    EXPECT_EQ(resp.error.code, ErrorCode::AuthenticationFailed);
    EXPECT_TRUE(transport->Requests().empty());   // never reached the wire
}

void TestKeylessSelfHosted() {
    // ...but a custom baseUrl (Ollama / vLLM / llama.cpp server) runs
    // keyless: the request goes out with no Authorization header.
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, ChatCompletion(
        "chatcmpl-3", json{{"role", "assistant"}, {"content", "local hi"}},
        "stop")));

    TextLLMConfig cfg;
    cfg.baseUrl      = "http://localhost:11434/";
    cfg.defaultModel = "llama3.2";
    auto llm = CreateOpenAITextLLM(cfg, nullptr, transport);

    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("local hi"));

    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.url,
              std::string("http://localhost:11434/v1/chat/completions"));
    EXPECT_EQ(FindHeader(sent, "authorization"), std::string(""));
}

void TestFactoryRegistration() {
    bool textHasOpenAI = false;
    for (const auto& p : ListTextLLMProviders()) {
        if (p == "openai") textHasOpenAI = true;
    }
    EXPECT_TRUE(textHasOpenAI);

    bool embedsHasOpenAI = false;
    for (const auto& p : ListEmbeddingsProviders()) {
        if (p == "openai") embedsHasOpenAI = true;
    }
    EXPECT_TRUE(embedsHasOpenAI);
}

// ---------------------------------------------------------------------
// Embeddings
// ---------------------------------------------------------------------

EmbeddingsConfig EmbedConfig() {
    EmbeddingsConfig cfg;
    cfg.providerId   = "openai";
    cfg.apiKey       = "sk-test-key";
    cfg.defaultModel = "text-embedding-3-small";
    return cfg;
}

void TestEmbeddings() {
    auto transport = std::make_shared<ScriptedTransport>();
    // Entries deliberately out of order: placement must follow "index".
    transport->ScriptResponse(JsonResponse(200, json{
        {"object", "list"},
        {"model", "text-embedding-3-small"},
        {"data", json::array({
            json{{"object", "embedding"}, {"index", 1},
                 {"embedding", json::array({0.0, 1.0})}},
            json{{"object", "embedding"}, {"index", 0},
                 {"embedding", json::array({1.0, 0.0})}}})},
        {"usage", {{"prompt_tokens", 8}, {"total_tokens", 8}}}}));

    auto embeds = CreateOpenAIEmbeddings(EmbedConfig(), nullptr, transport);
    EXPECT_TRUE(embeds != nullptr);

    EmbeddingRequest req;
    req.input      = {"first", "second"};
    req.dimensions = 2;
    EmbeddingResponse resp = embeds->Embed(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.embeddings.size(), static_cast<size_t>(2));
    EXPECT_EQ(resp.embeddings[0].values[0], 1.0f);
    EXPECT_EQ(resp.embeddings[1].values[1], 1.0f);
    EXPECT_EQ(resp.usage.inputTokens, 8);
    EXPECT_TRUE(IEmbeddings::CosineSimilarity(resp.embeddings[0],
                                              resp.embeddings[1]) < 0.001);

    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.url, std::string("https://api.openai.com/v1/embeddings"));
    EXPECT_EQ(FindHeader(sent, "authorization"),
              std::string("Bearer sk-test-key"));
    json body = json::parse(sent.body);
    EXPECT_EQ(body["model"], std::string("text-embedding-3-small"));
    EXPECT_EQ(body["input"].size(), static_cast<size_t>(2));
    EXPECT_EQ(body["input"][1], std::string("second"));
    EXPECT_EQ(body["dimensions"], 2);
    EXPECT_EQ(body["encoding_format"], std::string("float"));
}

void TestEmbeddingsDefaultModel() {
    // Hosted API + no model anywhere -> text-embedding-3-small.
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"model", "text-embedding-3-small"},
        {"data", json::array({json{{"index", 0},
            {"embedding", json::array({1.0})}}})}}));
    EmbeddingsConfig hosted;
    hosted.apiKey = "sk-test-key";           // no defaultModel
    auto embeds = CreateOpenAIEmbeddings(hosted, nullptr, transport);
    EmbeddingRequest req;
    req.input = {"text"};
    EXPECT_TRUE(embeds->Embed(req).error.IsOk());
    json body = json::parse(transport->Requests()[0].body);
    EXPECT_EQ(body["model"], std::string("text-embedding-3-small"));

    // A self-hosted server has no safe fallback: model stays required.
    auto transport2 = std::make_shared<ScriptedTransport>();
    EmbeddingsConfig selfHosted;
    selfHosted.baseUrl = "http://localhost:11434";
    auto local = CreateOpenAIEmbeddings(selfHosted, nullptr, transport2);
    EXPECT_EQ(local->Embed(req).error.code, ErrorCode::InvalidRequest);
    EXPECT_TRUE(transport2->Requests().empty());
}

void TestEmbeddingsErrors() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(401, json{
        {"error", {{"type", "invalid_request_error"},
                   {"code", "invalid_api_key"},
                   {"message", "Incorrect API key provided"}}}}));

    auto embeds = CreateOpenAIEmbeddings(EmbedConfig(), nullptr, transport);

    EmbeddingRequest empty;                    // no input
    EmbeddingResponse r1 = embeds->Embed(empty);
    EXPECT_EQ(r1.error.code, ErrorCode::InvalidRequest);
    EXPECT_TRUE(transport->Requests().empty());

    EmbeddingRequest req;
    req.input = {"text"};
    EmbeddingResponse r2 = embeds->Embed(req);
    EXPECT_EQ(r2.error.code, ErrorCode::AuthenticationFailed);
}

} // namespace

int main() {
    TestRequestSerialization();
    TestToolCallResponse();
    TestProviderErrorMapping();
    TestMissingModel();
    TestStreaming();
    TestStreamTerminalError();
    TestMissingCredentials();
    TestKeylessSelfHosted();
    TestFactoryRegistration();
    TestEmbeddings();
    TestEmbeddingsDefaultModel();
    TestEmbeddingsErrors();
    std::cout << "test_openai_adapter: all checks passed" << std::endl;
    return 0;
}
