// UltraAI/src/Batch.cpp
// Batches - see UltraAIBatch.h: the provider registry, the one-at-a-time
// fallback for providers without a batch endpoint, and RunBatch.
//
// RunBatch has a poll loop of its own rather than RunJobPoll
// (UltraAIJobPoll.h): that helper lives in the adapters' shared library,
// which UltraAI_Core does not depend on (the core builds with no adapter
// at all), and a batch needs per-poll progress and a grow-to-minutes
// interval it does not have.
//
// Version: 0.1.0 - first version
// Last Modified: 2026-11-09
// Author: UltraAI Module

#include "UltraAIBatch.h"

#ifdef ULTRAAI_HAS_ANTHROPIC_ADAPTER
#include "UltraAIAnthropicTextLLM.h"
#endif
#ifdef ULTRAAI_HAS_OPENAI_ADAPTER
#include "UltraAIOpenAI.h"
#endif

#include <algorithm>
#include <ctime>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace UltraAI {

namespace {

using Clock = std::chrono::steady_clock;

std::string NowRfc3339() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

// ---------------------------------------------------------------------
// The registry of native batch providers
// ---------------------------------------------------------------------

using TextBatchFactoryFn =
    std::function<std::unique_ptr<ITextBatch>(const TextBatchConfig&, Error*)>;

struct Registry {
    std::mutex mu;
    std::map<std::string, TextBatchFactoryFn> providers;

    void EnsureBuiltins() {
#ifdef ULTRAAI_HAS_ANTHROPIC_ADAPTER
        if (providers.find("anthropic") == providers.end()) {
            providers["anthropic"] = [](const TextBatchConfig& cfg, Error* err) {
                return CreateAnthropicTextBatch(cfg, err);
            };
        }
#endif
#ifdef ULTRAAI_HAS_OPENAI_ADAPTER
        if (providers.find("openai") == providers.end()) {
            providers["openai"] = [](const TextBatchConfig& cfg, Error* err) {
                return CreateOpenAITextBatch(cfg, err);
            };
        }
#endif
    }

    TextBatchFactoryFn Find(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu);
        EnsureBuiltins();
        auto it = providers.find(id);
        return it == providers.end() ? TextBatchFactoryFn{} : it->second;
    }
};

Registry& GetRegistry() {
    static Registry r;
    return r;
}

// ---------------------------------------------------------------------
// The one-at-a-time fallback
// ---------------------------------------------------------------------

class SequentialTextBatch : public ITextBatch {
public:
    SequentialTextBatch(std::shared_ptr<ITextLLM> llm, int concurrency)
        : llm_(std::move(llm)), concurrency_(std::max(1, concurrency)) {}

    ~SequentialTextBatch() override {
        std::vector<std::shared_ptr<Job>> jobs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, job] : jobs_) jobs.push_back(job);
        }
        for (auto& job : jobs) {
            job->cancel.store(true);
            for (auto& t : job->workers) if (t.joinable()) t.join();
        }
    }

    std::string ProviderId() const override {
        return llm_ ? llm_->GetCapabilities().providerId : std::string{};
    }

    bool IsNative() const override { return false; }

    BatchJob Submit(const std::vector<BatchRequestItem>& items) override {
        BatchJob refused;
        if (!llm_) {
            refused.error.code = ErrorCode::InvalidRequest;
            refused.error.message = "the batch has no text LLM";
            return refused;
        }
        if (!ValidateBatchItems(items, &refused.error)) return refused;

        auto job = std::make_shared<Job>();
        job->items = items;
        job->results.resize(items.size());
        job->done.assign(items.size(), false);
        job->counts.total = static_cast<int>(items.size());
        job->counts.processing = job->counts.total;
        job->createdAt = NowRfc3339();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job->id = "local-batch-" + std::to_string(++lastId_);
            jobs_[job->id] = job;
        }
        const int workers = std::min<int>(concurrency_, static_cast<int>(items.size()));
        job->running = workers;
        for (int i = 0; i < workers; ++i) {
            job->workers.emplace_back([this, job]() { Work(*job); });
        }
        return Snapshot(*job);
    }

    BatchJob Get(const std::string& batchId) override {
        auto job = Find(batchId);
        if (!job) return Unknown(batchId);
        return Snapshot(*job);
    }

    BatchJob Cancel(const std::string& batchId) override {
        auto job = Find(batchId);
        if (!job) return Unknown(batchId);
        job->cancel.store(true);
        return Snapshot(*job);
    }

    std::vector<BatchItemResult> Results(const std::string& batchId, Error* outError) override {
        auto job = Find(batchId);
        if (!job) {
            if (outError) *outError = Unknown(batchId).error;
            return {};
        }
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->running > 0) {
            if (outError) {
                outError->code = ErrorCode::InvalidRequest;
                outError->message = "the batch has not finished";
            }
            return {};
        }
        return job->results;
    }

private:
    struct Job {
        std::string id;
        std::vector<BatchRequestItem> items;
        std::vector<BatchItemResult> results;
        std::vector<bool> done;
        BatchCounts counts;
        std::string createdAt;
        std::string endedAt;
        size_t next = 0;
        int running = 0;
        std::atomic<bool> cancel{false};
        std::mutex mutex;
        std::vector<std::thread> workers;
    };

    void Work(Job& job) {
        for (;;) {
            size_t index;
            {
                std::lock_guard<std::mutex> lock(job.mutex);
                if (job.cancel.load() || job.next >= job.items.size()) break;
                index = job.next++;
            }
            ChatResponse response = llm_->Chat(job.items[index].request);
            std::lock_guard<std::mutex> lock(job.mutex);
            BatchItemResult& r = job.results[index];
            r.customId = job.items[index].customId;
            r.status = response.error.IsOk() ? BatchItemStatus::Succeeded : BatchItemStatus::Failed;
            r.response = std::move(response);
            job.done[index] = true;
            --job.counts.processing;
            if (r.status == BatchItemStatus::Succeeded) ++job.counts.succeeded;
            else ++job.counts.failed;
        }
        std::lock_guard<std::mutex> lock(job.mutex);
        if (--job.running == 0) {
            // The last worker out marks what never ran.
            for (size_t i = 0; i < job.items.size(); ++i) {
                if (job.done[i]) continue;
                BatchItemResult& r = job.results[i];
                r.customId = job.items[i].customId;
                r.status = BatchItemStatus::Cancelled;
                r.response.error.code = ErrorCode::Cancelled;
                r.response.error.message = "the batch was cancelled before this request ran";
                --job.counts.processing;
                ++job.counts.cancelled;
            }
            job.endedAt = NowRfc3339();
        }
    }

    BatchJob Snapshot(Job& job) {
        std::lock_guard<std::mutex> lock(job.mutex);
        BatchJob out;
        out.id = job.id;
        out.counts = job.counts;
        out.createdAt = job.createdAt;
        out.endedAt = job.endedAt;
        if (job.running > 0) {
            out.state = job.cancel.load() ? BatchState::Cancelling : BatchState::InProgress;
        } else {
            out.state = job.counts.cancelled > 0 ? BatchState::Cancelled : BatchState::Completed;
        }
        return out;
    }

    std::shared_ptr<Job> Find(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        return it == jobs_.end() ? nullptr : it->second;
    }

    static BatchJob Unknown(const std::string& id) {
        BatchJob out;
        out.id = id;
        out.error.code = ErrorCode::InvalidRequest;
        out.error.message = "no batch '" + id + "' in this object (a one-at-a-time batch "
                            "lives only as long as the object that ran it)";
        return out;
    }

    std::shared_ptr<ITextLLM> llm_;
    int concurrency_;
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    int lastId_ = 0;
};

} // namespace

bool RegisterTextBatchProvider(const std::string& providerId, TextBatchFactoryFn factory) {
    auto& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mu);
    r.providers[providerId] = std::move(factory);
    return true;
}

std::vector<std::string> ListTextBatchProviders() {
    auto& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mu);
    r.EnsureBuiltins();
    std::vector<std::string> ids;
    for (const auto& kv : r.providers) ids.push_back(kv.first);
    return ids;
}

std::unique_ptr<ITextBatch> CreateSequentialTextBatch(std::shared_ptr<ITextLLM> llm,
                                                      int concurrency) {
    return std::make_unique<SequentialTextBatch>(std::move(llm), concurrency);
}

std::unique_ptr<ITextBatch> CreateTextBatch(const TextBatchConfig& config, Error* outError) {
    if (!config.providerId.empty()) {
        if (auto factory = GetRegistry().Find(config.providerId)) {
            // A provider whose batch endpoint is not where this config
            // points (OpenAI's adapter at a self-hosted server) declines,
            // and the requests go one at a time instead.
            Error declined;
            if (auto batch = factory(config, &declined)) return batch;
            if (declined.code != ErrorCode::UnsupportedFormat) {
                if (outError) *outError = declined;
                return nullptr;
            }
        }
    }
    // No batch endpoint of its own (or the default route): the text LLM,
    // one request at a time - unless the route chose a provider that has one.
    std::unique_ptr<ITextLLM> llm = CreateTextLLM(config, outError);
    if (!llm) return nullptr;
    if (config.providerId.empty()) {
        const std::string routed = llm->GetCapabilities().providerId;
        if (auto factory = GetRegistry().Find(routed)) {
            TextBatchConfig native = config;
            native.providerId = routed;
            Error declined;
            if (auto batch = factory(native, &declined)) return batch;
        }
    }
    return CreateSequentialTextBatch(std::shared_ptr<ITextLLM>(std::move(llm)));
}

BatchRunResult RunBatch(ITextBatch& batch, const std::vector<BatchRequestItem>& items,
                        const BatchRunOptions& options) {
    BatchRunResult out;
    out.job = batch.Submit(items);
    if (!out.job.error.IsOk() || out.job.id.empty()) {
        out.error = out.job.error;
        if (out.error.IsOk()) {
            out.error.code = ErrorCode::ProviderError;
            out.error.message = "the provider returned no batch id";
        }
        return out;
    }
    if (options.onProgress) options.onProgress(out.job);

    const bool native = batch.IsNative();
    const auto start = Clock::now();
    auto interval = native ? std::max(options.pollInterval, std::chrono::milliseconds(200))
                           : std::chrono::milliseconds(200);
    const auto maxInterval = native ? std::max(options.maxPollInterval, interval) : interval;
    // After a cancel the provider still has to wind the batch down; wait for
    // that this long, then return the job as it stands.
    const auto cancelWait = native ? std::chrono::milliseconds(std::chrono::minutes(1))
                                   : std::chrono::milliseconds(std::chrono::seconds(30));
    Clock::time_point cancelledAt{};
    bool cancelSent = false;
    int failedPolls = 0;

    while (!IsFinished(out.job.state)) {
        // Sleep in slices so a cancel is noticed quickly.
        const auto wakeAt = Clock::now() + (cancelSent ? std::min(interval, std::chrono::milliseconds(2000))
                                                       : interval);
        while (Clock::now() < wakeAt) {
            if (!cancelSent && options.cancel && options.cancel->load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!cancelSent && options.cancel && options.cancel->load()) {
            cancelSent = true;
            cancelledAt = Clock::now();
            BatchJob c = batch.Cancel(out.job.id);
            if (c.error.IsOk()) out.job = c;
        }
        if (cancelSent && Clock::now() - cancelledAt > cancelWait) break;
        if (!cancelSent && options.timeout.count() > 0 && Clock::now() - start > options.timeout) {
            out.error.code = ErrorCode::Timeout;
            out.error.message = "the batch did not finish in time; it is still running at the provider";
            return out;
        }

        BatchJob polled = batch.Get(out.job.id);
        if (!polled.error.IsOk() && !IsFinished(polled.state)) {
            // A poll that failed (network, rate limit) is tried again; five
            // in a row end the wait.
            if (++failedPolls >= 5) {
                out.error = polled.error;
                return out;
            }
            continue;
        }
        failedPolls = 0;
        out.job = polled;
        if (options.onProgress) options.onProgress(out.job);
        if (native) {
            interval = std::min(maxInterval, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 interval * options.backoff));
        }
    }

    if (out.job.state == BatchState::Failed) {
        out.error = out.job.error;
        if (out.error.IsOk()) {
            out.error.code = ErrorCode::ProviderError;
            out.error.message = "the provider refused the batch";
        }
        return out;
    }
    if (IsFinished(out.job.state)) {
        Error resultsError;
        out.results = batch.Results(out.job.id, &resultsError);
        if (!resultsError.IsOk()) {
            out.error = resultsError;
            return out;
        }
    }
    if (cancelSent) {
        out.error.code = ErrorCode::Cancelled;
        out.error.message = "cancelled";
    }
    return out;
}

} // namespace UltraAI
