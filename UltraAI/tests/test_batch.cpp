// UltraAI/tests/test_batch.cpp
// Exercises batches (UltraAIBatch.h): item validation, the one-at-a-time
// fallback (results, counts, concurrency, cancel), CreateTextBatch's
// choice between native and fallback, RunBatch, and the Anthropic and
// OpenAI batch wire formats offline through ScriptedTransport. Also the
// Anthropic adapter sending one turn's tool results in one user message.

#include "UltraAI.h"
#include "UltraAIAnthropicTextLLM.h"
#include "UltraAIOpenAI.h"
#include "UltraAITransport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
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

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

BatchRequestItem Item(const std::string& id, const std::string& text) {
    BatchRequestItem item;
    item.customId = id;
    Message m;
    m.role = Role::User;
    m.text = text;
    item.request.messages.push_back(m);
    return item;
}

// Answers "<text> done" after a pause, so a cancel can land mid-batch.
class SlowLLM : public ITextLLM {
public:
    std::atomic<int> calls{0};
    std::chrono::milliseconds delay{0};
    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities c;
        c.providerId = "slow";
        return c;
    }
    ChatResponse Chat(const ChatRequest& request) override {
        ++calls;
        std::this_thread::sleep_for(delay);
        ChatResponse r;
        const std::string text = request.messages.empty() ? "" : request.messages[0].text;
        if (text == "fail") {
            r.error.code = ErrorCode::ProviderError;
            r.error.message = "scripted failure";
            return r;
        }
        r.text = text + " done";
        r.finishReason = FinishReason::Stop;
        r.usage.outputTokens = 2;
        return r;
    }
    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        return std::async(std::launch::async, [this, request] { return Chat(request); });
    }
    StreamHandle ChatStream(const ChatRequest&, StreamCallback) override { return nullptr; }
    int32_t CountTokens(const std::string&, const std::vector<Message>&) override { return 0; }
};

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers = {{"content-type", "application/json"}};
    r.body = body.dump();
    return r;
}

TransportResponse TextResponse(int status, const std::string& body) {
    TransportResponse r;
    r.statusCode = status;
    r.body = body;
    return r;
}

std::string FindHeader(const TransportRequest& r, const std::string& name) {
    for (const auto& [k, v] : r.headers) if (k == name) return v;
    return {};
}

TextBatchConfig KeyConfig() {
    TextBatchConfig cfg;
    cfg.apiKey = "sk-test-key";
    cfg.defaultModel = "model-1";
    return cfg;
}

// ---------------------------------------------------------------------

void TestValidation() {
    Error e;
    EXPECT_TRUE(!ValidateBatchItems({}, &e));
    EXPECT_TRUE(Contains(e.message, "at least one"));
    EXPECT_TRUE(ValidateBatchItems({Item("a-1", "x"), Item("B_2", "y")}, &e));
    EXPECT_TRUE(!ValidateBatchItems({Item("a", "x"), Item("a", "y")}, &e));
    EXPECT_TRUE(Contains(e.message, "used twice"));
    EXPECT_TRUE(!ValidateBatchItems({Item("has space", "x")}, &e));
    EXPECT_TRUE(!ValidateBatchItems({Item("", "x")}, &e));
    EXPECT_TRUE(!ValidateBatchItems({Item(std::string(65, 'a'), "x")}, &e));
    EXPECT_TRUE(IsFinished(BatchState::Expired));
    EXPECT_TRUE(!IsFinished(BatchState::Finalizing));
}

void TestSequentialBatch() {
    auto llm = std::make_shared<SlowLLM>();
    auto batch = CreateSequentialTextBatch(llm, 2);
    EXPECT_TRUE(!batch->IsNative());
    EXPECT_EQ(batch->ProviderId(), "slow");

    std::vector<BatchRequestItem> items;
    for (int i = 0; i < 7; ++i) items.push_back(Item("q" + std::to_string(i), "item" + std::to_string(i)));
    items.push_back(Item("bad", "fail"));

    int progressCalls = 0;
    BatchRunOptions opts;
    opts.onProgress = [&](const BatchJob& job) {
        ++progressCalls;
        EXPECT_EQ(job.counts.total, 8);
    };
    BatchRunResult run = RunBatch(*batch, items, opts);
    EXPECT_TRUE(run.error.IsOk());
    EXPECT_TRUE(run.job.state == BatchState::Completed);
    EXPECT_EQ(run.job.counts.succeeded, 7);
    EXPECT_EQ(run.job.counts.failed, 1);
    EXPECT_EQ(run.job.counts.processing, 0);
    EXPECT_TRUE(!run.job.createdAt.empty() && !run.job.endedAt.empty());
    EXPECT_TRUE(progressCalls >= 2);
    EXPECT_EQ(run.results.size(), size_t(8));
    EXPECT_EQ(run.results[3].customId, "q3");
    EXPECT_EQ(run.results[3].response.text, "item3 done");
    EXPECT_TRUE(run.results[7].status == BatchItemStatus::Failed);
    EXPECT_TRUE(Contains(run.results[7].response.error.message, "scripted failure"));
    EXPECT_EQ(llm->calls.load(), 8);

    // Unknown ids and unfinished results.
    BatchJob unknown = batch->Get("nope");
    EXPECT_TRUE(!unknown.error.IsOk());
    Error err;
    EXPECT_TRUE(batch->Results("nope", &err).empty());
    EXPECT_TRUE(!err.IsOk());
    BatchJob refused = batch->Submit({});
    EXPECT_TRUE(refused.state == BatchState::Failed && !refused.error.IsOk());
}

void TestSequentialCancel() {
    auto llm = std::make_shared<SlowLLM>();
    llm->delay = std::chrono::milliseconds(60);
    auto batch = CreateSequentialTextBatch(llm, 1);
    std::vector<BatchRequestItem> items;
    for (int i = 0; i < 20; ++i) items.push_back(Item("c" + std::to_string(i), "x"));

    std::atomic<bool> cancel{false};
    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        cancel = true;
    });
    BatchRunOptions opts;
    opts.cancel = &cancel;
    BatchRunResult run = RunBatch(*batch, items, opts);
    stopper.join();
    EXPECT_TRUE(run.error.code == ErrorCode::Cancelled);
    EXPECT_TRUE(run.job.state == BatchState::Cancelled);
    EXPECT_TRUE(run.job.counts.cancelled > 0);
    EXPECT_EQ(run.job.counts.succeeded + run.job.counts.cancelled, 20);
    EXPECT_EQ(run.results.size(), size_t(20));
    EXPECT_TRUE(run.results.back().status == BatchItemStatus::Cancelled);
    EXPECT_TRUE(llm->calls.load() < 20);
}

void TestCreateTextBatchChoosesFallback() {
    TextBatchConfig mock;
    mock.providerId = "mock";
    Error err;
    auto batch = CreateTextBatch(mock, &err);
    EXPECT_TRUE(batch != nullptr);
    EXPECT_TRUE(!batch->IsNative());
    BatchRunResult run = RunBatch(*batch, {Item("one", "hello")});
    EXPECT_TRUE(run.error.IsOk());
    EXPECT_EQ(run.results.size(), size_t(1));
    EXPECT_TRUE(run.results[0].status == BatchItemStatus::Succeeded);

    // OpenAI's adapter pointed at a self-hosted server has no Batch API.
    TextBatchConfig local = KeyConfig();
    local.baseUrl = "http://127.0.0.1:11434";
    Error declined;
    EXPECT_TRUE(CreateOpenAITextBatch(local, &declined) == nullptr);
    EXPECT_TRUE(declined.code == ErrorCode::UnsupportedFormat);

    auto providers = ListTextBatchProviders();
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "anthropic") != providers.end());
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "openai") != providers.end());

    TextBatchConfig bad;
    bad.providerId = "no-such-provider";
    Error badErr;
    EXPECT_TRUE(CreateTextBatch(bad, &badErr) == nullptr);
    EXPECT_TRUE(!badErr.IsOk());
}

json AnthropicBatchObject(const std::string& status, int processing, int succeeded,
                          int errored, int canceled) {
    return json{{"id", "msgbatch_01"}, {"type", "message_batch"},
                {"processing_status", status},
                {"request_counts", {{"processing", processing}, {"succeeded", succeeded},
                                    {"errored", errored}, {"canceled", canceled}, {"expired", 0}}},
                {"created_at", "2026-11-09T10:00:00Z"},
                {"expires_at", "2026-11-10T10:00:00Z"},
                {"ended_at", status == "ended" ? json("2026-11-09T10:05:00Z") : json(nullptr)}};
}

void TestAnthropicBatch() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, AnthropicBatchObject("in_progress", 3, 0, 0, 0)));
    transport->ScriptResponse(JsonResponse(200, AnthropicBatchObject("in_progress", 1, 2, 0, 0)));
    transport->ScriptResponse(JsonResponse(200, AnthropicBatchObject("ended", 0, 1, 1, 1)));
    // Results(): the job again, then the JSONL.
    transport->ScriptResponse(JsonResponse(200, AnthropicBatchObject("ended", 0, 1, 1, 1)));
    std::string jsonl =
        json{{"custom_id", "a"}, {"result", {{"type", "succeeded"}, {"message", {
            {"id", "msg_1"}, {"model", "claude-opus-5"},
            {"content", json::array({json{{"type", "text"}, {"text", "Bonjour"}}})},
            {"stop_reason", "end_turn"},
            {"usage", {{"input_tokens", 5}, {"output_tokens", 1}}}}}}}}.dump() + "\n" +
        json{{"custom_id", "b"}, {"result", {{"type", "errored"}, {"error", {
            {"type", "error"}, {"error", {{"type", "invalid_request_error"},
                                          {"message", "max_tokens too large"}}}}}}}}.dump() + "\n" +
        json{{"custom_id", "c"}, {"result", {{"type", "canceled"}}}}.dump() + "\n";
    transport->ScriptResponse(TextResponse(200, jsonl));

    auto batch = CreateAnthropicTextBatch(KeyConfig(), nullptr, transport);
    EXPECT_TRUE(batch && batch->IsNative());
    EXPECT_EQ(batch->ProviderId(), "anthropic");

    std::vector<BatchRequestItem> items = {Item("a", "Hello"), Item("b", "x"), Item("c", "y")};
    items[1].request.sampling.maxOutputTokens = 999999;
    items[0].request.messages.insert(items[0].request.messages.begin(),
                                     Message{Role::System, "Translate to French.", {}, {}, {}, {}});
    BatchRunOptions opts;
    opts.pollInterval = std::chrono::milliseconds(1);   // clamped to 200 ms
    std::vector<BatchState> seen;
    opts.onProgress = [&](const BatchJob& job) { seen.push_back(job.state); };
    BatchRunResult run = RunBatch(*batch, items, opts);

    EXPECT_TRUE(run.error.IsOk());
    EXPECT_TRUE(run.job.state == BatchState::Cancelled);   // one request was cancelled
    EXPECT_EQ(run.job.id, "msgbatch_01");
    EXPECT_EQ(run.job.counts.total, 3);
    EXPECT_EQ(run.job.endedAt, "2026-11-09T10:05:00Z");
    EXPECT_EQ(seen.size(), size_t(3));
    EXPECT_EQ(run.results.size(), size_t(3));
    EXPECT_TRUE(run.results[0].status == BatchItemStatus::Succeeded);
    EXPECT_EQ(run.results[0].response.text, "Bonjour");
    EXPECT_EQ(run.results[0].response.usage.inputTokens, 5);
    EXPECT_TRUE(run.results[1].status == BatchItemStatus::Failed);
    EXPECT_TRUE(run.results[1].response.error.code == ErrorCode::InvalidRequest);
    EXPECT_EQ(run.results[1].response.error.message, "max_tokens too large");
    EXPECT_TRUE(run.results[2].status == BatchItemStatus::Cancelled);

    const auto& sent = transport->Requests();
    EXPECT_EQ(sent.size(), size_t(5));
    EXPECT_EQ(sent[0].method, "POST");
    EXPECT_EQ(sent[0].url, "https://api.anthropic.com/v1/messages/batches");
    EXPECT_EQ(FindHeader(sent[0], "x-api-key"), "sk-test-key");
    json body = json::parse(sent[0].body);
    EXPECT_EQ(body["requests"].size(), size_t(3));
    EXPECT_EQ(body["requests"][0]["custom_id"], "a");
    EXPECT_EQ(body["requests"][0]["params"]["model"], "model-1");
    EXPECT_EQ(body["requests"][0]["params"]["system"], "Translate to French.");
    EXPECT_EQ(body["requests"][1]["params"]["max_tokens"], 999999);
    EXPECT_EQ(sent[1].method, "GET");
    EXPECT_EQ(sent[1].url, "https://api.anthropic.com/v1/messages/batches/msgbatch_01");
    EXPECT_TRUE(sent[1].body.empty());
    EXPECT_EQ(sent[4].url, "https://api.anthropic.com/v1/messages/batches/msgbatch_01/results");

    // Cancel and an HTTP error.
    auto t2 = std::make_shared<ScriptedTransport>();
    t2->ScriptResponse(JsonResponse(200, AnthropicBatchObject("canceling", 2, 0, 0, 0)));
    t2->ScriptResponse(JsonResponse(404, json{{"type", "error"}, {"error", {
        {"type", "not_found_error"}, {"message", "no such batch"}}}}));
    auto b2 = CreateAnthropicTextBatch(KeyConfig(), nullptr, t2);
    BatchJob cancelled = b2->Cancel("msgbatch_01");
    EXPECT_TRUE(cancelled.state == BatchState::Cancelling);
    EXPECT_EQ(t2->Requests()[0].url, "https://api.anthropic.com/v1/messages/batches/msgbatch_01/cancel");
    BatchJob missing = b2->Get("gone");
    EXPECT_TRUE(!missing.error.IsOk());
    EXPECT_TRUE(Contains(missing.error.message, "no such batch"));
}

json OpenAIBatchObject(const std::string& status, int total, int completed, int failed) {
    json j = {{"id", "batch_abc"}, {"object", "batch"}, {"status", status},
              {"input_file_id", "file-in"},
              {"request_counts", {{"total", total}, {"completed", completed}, {"failed", failed}}},
              {"created_at", 1762682400}, {"expires_at", 1762768800}};
    if (status == "completed") {
        j["completed_at"] = 1762682700;
        j["output_file_id"] = "file-out";
        j["error_file_id"] = "file-err";
    }
    return j;
}

void TestOpenAIBatch() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"id", "file-in"}, {"object", "file"}}));
    transport->ScriptResponse(JsonResponse(200, OpenAIBatchObject("validating", 0, 0, 0)));
    transport->ScriptResponse(JsonResponse(200, OpenAIBatchObject("in_progress", 2, 1, 0)));
    transport->ScriptResponse(JsonResponse(200, OpenAIBatchObject("completed", 2, 1, 1)));
    // Results(): the job, the output file, the error file.
    transport->ScriptResponse(JsonResponse(200, OpenAIBatchObject("completed", 2, 1, 1)));
    transport->ScriptResponse(TextResponse(200, json{{"id", "r1"}, {"custom_id", "one"},
        {"response", {{"status_code", 200}, {"body", {
            {"id", "chatcmpl-1"}, {"model", "gpt-x"},
            {"choices", json::array({json{{"index", 0}, {"finish_reason", "stop"},
                {"message", {{"role", "assistant"}, {"content", "Hallo"}}}}})},
            {"usage", {{"prompt_tokens", 7}, {"completion_tokens", 1}}}}}}},
        {"error", nullptr}}.dump() + "\n"));
    transport->ScriptResponse(TextResponse(200, json{{"id", "r2"}, {"custom_id", "two"},
        {"response", {{"status_code", 400}, {"body", {{"error", {
            {"message", "bad model"}, {"type", "invalid_request_error"}, {"code", "model_not_found"}}}}}}},
        {"error", nullptr}}.dump() + "\n"));

    auto batch = CreateOpenAITextBatch(KeyConfig(), nullptr, transport);
    EXPECT_TRUE(batch && batch->IsNative());
    BatchRunOptions opts;
    opts.pollInterval = std::chrono::milliseconds(1);
    BatchRunResult run = RunBatch(*batch, {Item("one", "Hello"), Item("two", "x")}, opts);

    EXPECT_TRUE(run.error.IsOk());
    EXPECT_TRUE(run.job.state == BatchState::Completed);
    EXPECT_EQ(run.job.createdAt, "2025-11-09T10:00:00Z");
    EXPECT_EQ(run.job.endedAt, "2025-11-09T10:05:00Z");
    EXPECT_EQ(run.results.size(), size_t(2));
    EXPECT_EQ(run.results[0].customId, "one");
    EXPECT_EQ(run.results[0].response.text, "Hallo");
    EXPECT_EQ(run.results[0].response.usage.inputTokens, 7);
    EXPECT_TRUE(run.results[1].status == BatchItemStatus::Failed);
    EXPECT_TRUE(run.results[1].response.error.code == ErrorCode::ModelNotFound);
    EXPECT_EQ(run.results[1].response.error.message, "bad model");

    const auto& sent = transport->Requests();
    EXPECT_EQ(sent.size(), size_t(7));
    EXPECT_EQ(sent[0].url, "https://api.openai.com/v1/files");
    EXPECT_EQ(FindHeader(sent[0], "authorization"), "Bearer sk-test-key");
    EXPECT_TRUE(Contains(FindHeader(sent[0], "content-type"), "multipart/form-data"));
    EXPECT_TRUE(Contains(sent[0].body, "name=\"purpose\""));
    EXPECT_TRUE(Contains(sent[0].body, "\"custom_id\":\"one\""));
    EXPECT_TRUE(Contains(sent[0].body, "\"url\":\"/v1/chat/completions\""));
    EXPECT_TRUE(Contains(sent[0].body, "\"model\":\"model-1\""));
    EXPECT_EQ(sent[1].url, "https://api.openai.com/v1/batches");
    json create = json::parse(sent[1].body);
    EXPECT_EQ(create["input_file_id"], "file-in");
    EXPECT_EQ(create["completion_window"], "24h");
    EXPECT_EQ(sent[5].url, "https://api.openai.com/v1/files/file-out/content");
    EXPECT_EQ(sent[6].url, "https://api.openai.com/v1/files/file-err/content");

    // A batch the provider refused.
    auto t2 = std::make_shared<ScriptedTransport>();
    json failed = OpenAIBatchObject("failed", 0, 0, 0);
    failed["errors"] = {{"data", json::array({json{{"code", "invalid_json_line"},
                                                    {"message", "line 1 is not JSON"}}})}};
    t2->ScriptResponse(JsonResponse(200, failed));
    auto b2 = CreateOpenAITextBatch(KeyConfig(), nullptr, t2);
    BatchJob job = b2->Get("batch_abc");
    EXPECT_TRUE(job.state == BatchState::Failed);
    EXPECT_EQ(job.error.message, "line 1 is not JSON");
    EXPECT_EQ(job.error.providerCode, "invalid_json_line");
}

void TestAnthropicGroupsToolResults() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"id", "msg_02"}, {"model", "claude-opus-5"},
        {"content", json::array({json{{"type", "text"}, {"text", "ok"}}})},
        {"stop_reason", "end_turn"}, {"usage", {{"input_tokens", 1}, {"output_tokens", 1}}}}));
    TextLLMConfig cfg;
    cfg.apiKey = "sk-test-key";
    auto llm = CreateAnthropicTextLLM(cfg, nullptr, transport);

    ChatRequest req;
    Message user{Role::User, "two lookups", {}, {}, {}, {}};
    Message asst;
    asst.role = Role::Assistant;
    asst.toolCalls = {ToolCall{"t1", "a", "{}"}, ToolCall{"t2", "b", "{}"}};
    Message r1; r1.role = Role::Tool; r1.toolCallId = "t1"; r1.text = "A";
    Message r2; r2.role = Role::Tool; r2.toolCallId = "t2"; r2.text = "B";
    Message next{Role::User, "thanks", {}, {}, {}, {}};
    req.messages = {user, asst, r1, r2, next};
    EXPECT_TRUE(llm->Chat(req).error.IsOk());

    json body = json::parse(transport->Requests()[0].body);
    EXPECT_EQ(body["messages"].size(), size_t(4));
    EXPECT_EQ(body["messages"][2]["content"].size(), size_t(2));
    EXPECT_EQ(body["messages"][2]["content"][1]["tool_use_id"], "t2");
    EXPECT_EQ(body["messages"][3]["content"], "thanks");
}

} // namespace

int main() {
    TestValidation();
    TestSequentialBatch();
    TestSequentialCancel();
    TestCreateTextBatchChoosesFallback();
    TestAnthropicBatch();
    TestOpenAIBatch();
    TestAnthropicGroupsToolResults();
    std::cout << "ultraai batch tests passed" << std::endl;
    return 0;
}
