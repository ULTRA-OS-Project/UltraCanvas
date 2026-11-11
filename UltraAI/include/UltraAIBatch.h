// UltraAI/include/UltraAIBatch.h
// Batches: many chat requests sent as one job, answered later, cheaper.
//
// Anthropic (Message Batches) and OpenAI (Batch API) take a list of chat
// requests, work through them within 24 hours (usually much sooner) and
// charge about half of what the same requests cost one at a time. That
// suits work nobody waits on: translating a catalogue, summarising an
// archive, classifying a mailbox. ITextBatch is that job, written once
// for both, and for every other text-LLM provider too: where a provider
// has no batch endpoint (local, qwen, llama-cpp, mock), the same interface
// runs the requests one after another on a thread of its own, so an
// application never has to care which kind it got (IsNative() says).
//
//   TextBatchConfig cfg; cfg.providerId = "anthropic";
//   auto batch = CreateTextBatch(cfg);
//   std::vector<BatchRequestItem> items = ...;   // a custom id + a ChatRequest each
//   BatchRunResult run = RunBatch(*batch, items);   // submit, poll, collect
//   for (const auto& r : run.results) Use(r.customId, r.response.text);
//
// Or in steps, for a job that outlives the program (a batch id can be
// stored and asked about again tomorrow, for the native providers):
//   BatchJob job = batch->Submit(items);
//   ... later: job = batch->Get(job.id);  if (IsFinished(job.state)) batch->Results(job.id);
//
// Version: 0.1.0 - first version
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAITextLLM.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraAI {

// One request of a batch. customId identifies its answer (results can come
// back in any order); it must be unique within the batch, and the
// providers accept 1-64 characters of letters, digits, '-' and '_'.
struct BatchRequestItem {
    std::string customId;
    ChatRequest request;
};

enum class BatchState {
    Validating,   // the provider is checking the requests (OpenAI)
    InProgress,   // requests are being answered
    Finalizing,   // answering is done, results are being written (OpenAI)
    Cancelling,   // Cancel() was asked for; requests already running finish
    Completed,    // finished: results are ready (some items may have failed)
    Cancelled,    // finished after a cancel: results hold what was answered
    Expired,      // the provider's window ran out: results hold what was answered
    Failed        // the batch as a whole was refused (BatchJob::error says why)
};

// Completed, Cancelled, Expired and Failed: nothing will change any more.
inline bool IsFinished(BatchState state) {
    return state == BatchState::Completed || state == BatchState::Cancelled ||
           state == BatchState::Expired || state == BatchState::Failed;
}

struct BatchCounts {
    int total = 0;
    int processing = 0;   // not answered yet
    int succeeded = 0;
    int failed = 0;
    int cancelled = 0;
    int expired = 0;
};

struct BatchJob {
    std::string id;              // the provider's batch id
    BatchState state = BatchState::Failed;
    BatchCounts counts;
    std::string createdAt;       // RFC 3339, UTC, when the provider says
    std::string endedAt;
    std::string expiresAt;
    Error error;                 // the call failed, or state == Failed
};

enum class BatchItemStatus {
    Succeeded,
    Failed,      // response.error says why
    Cancelled,   // the batch was cancelled before this item ran
    Expired      // the batch expired before this item ran
};

struct BatchItemResult {
    std::string customId;
    BatchItemStatus status = BatchItemStatus::Failed;
    ChatResponse response;       // text, tool calls, usage; error when it failed
};

// A batch job at one provider.
class ITextBatch {
public:
    virtual ~ITextBatch() = default;

    virtual std::string ProviderId() const = 0;

    // True when the provider runs the batch itself (anthropic, openai):
    // cheaper, answered within 24 hours, and the id stays valid after
    // this object is gone. False for the fallback that sends the requests
    // one at a time: no discount, and its jobs live only in this object.
    virtual bool IsNative() const = 0;

    // Send the requests. The returned job carries the id to ask about
    // later; its error is set (and state Failed) when the provider refused
    // the batch or the items are unusable (none, duplicate or invalid ids).
    virtual BatchJob Submit(const std::vector<BatchRequestItem>& items) = 0;

    // The job as it stands now.
    virtual BatchJob Get(const std::string& batchId) = 0;

    // Ask the provider to stop. Requests already running still finish;
    // Get() reports Cancelling, then Cancelled.
    virtual BatchJob Cancel(const std::string& batchId) = 0;

    // The answers of a finished job, one per item (in the provider's order;
    // match them by customId). Fails (empty, *outError set) while the job
    // is not finished.
    virtual std::vector<BatchItemResult> Results(const std::string& batchId,
                                                 Error* outError = nullptr) = 0;
};

using TextBatchConfig = ProviderConfig;

// The provider's own batch when it has one, otherwise the one-at-a-time
// fallback over CreateTextLLM(config) - so this fails only where
// CreateTextLLM fails. An empty providerId follows the text-LLM route.
std::unique_ptr<ITextBatch> CreateTextBatch(const TextBatchConfig& config,
                                            Error* outError = nullptr);

// The one-at-a-time fallback over an LLM the caller already holds.
// `concurrency` requests run at once (at least 1).
std::unique_ptr<ITextBatch> CreateSequentialTextBatch(std::shared_ptr<ITextLLM> llm,
                                                      int concurrency = 1);

// Providers with a batch endpoint of their own, and how adapters add one.
std::vector<std::string> ListTextBatchProviders();
bool RegisterTextBatchProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<ITextBatch>(const TextBatchConfig&, Error*)> factory);

// ---------------------------------------------------------------------
// Submit, wait, collect
// ---------------------------------------------------------------------

struct BatchRunOptions {
    // How often to ask. Native batches take minutes to hours, so the
    // interval starts at `pollInterval` and grows by `backoff` up to
    // `maxPollInterval`. The one-at-a-time fallback is asked every 200 ms
    // whatever these say.
    std::chrono::milliseconds pollInterval{std::chrono::seconds(10)};
    std::chrono::milliseconds maxPollInterval{std::chrono::minutes(2)};
    double backoff = 1.5;

    // Give up waiting after this long (0 = until the provider finishes;
    // its own window is 24 hours). The batch keeps running at the
    // provider; RunBatch returns it unfinished with ErrorCode::Timeout.
    std::chrono::milliseconds timeout{0};

    // Seen after each poll.
    std::function<void(const BatchJob& job)> onProgress;

    // When it turns true, RunBatch cancels the batch at the provider and
    // returns what was answered (ErrorCode::Cancelled).
    const std::atomic<bool>* cancel = nullptr;
};

struct BatchRunResult {
    BatchJob job;                          // the job as it ended
    std::vector<BatchItemResult> results;  // empty when it never finished
    Error error;                           // submit refused, timeout, cancel, results unreadable
};

// Submit `items`, poll until the job is finished, and fetch the results.
// Blocks; run it off the UI thread.
BatchRunResult RunBatch(ITextBatch& batch,
                        const std::vector<BatchRequestItem>& items,
                        const BatchRunOptions& options = {});

// The checks Submit runs on the items before anything is sent: at least
// one, every customId 1-64 characters of [A-Za-z0-9_-], none twice.
// Returns false and says which item is wrong. (Inline: the adapters use it
// and UltraAI_Core links them, not the other way round.)
inline bool ValidateBatchItems(const std::vector<BatchRequestItem>& items, Error* outError) {
    auto fail = [outError](const std::string& message) {
        if (outError) {
            outError->code = ErrorCode::InvalidRequest;
            outError->message = message;
        }
        return false;
    };
    if (items.empty()) return fail("a batch needs at least one request");
    std::set<std::string> seen;
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string& id = items[i].customId;
        bool valid = !id.empty() && id.size() <= 64;
        for (const char c : id) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_')) {
                valid = false;
            }
        }
        if (!valid) {
            return fail("request " + std::to_string(i + 1) + ": the custom id '" + id +
                        "' must be 1-64 characters of letters, digits, '-' and '_'");
        }
        if (!seen.insert(id).second) {
            return fail("request " + std::to_string(i + 1) + ": the custom id '" + id +
                        "' is used twice");
        }
    }
    return true;
}

} // namespace UltraAI
