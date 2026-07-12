// UltraAI/adapters/mock/src/MockTextLLM.cpp
// Mock ITextLLM implementation — see UltraAIMockTextLLM.h.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module

#include "UltraAIMockTextLLM.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>

namespace UltraAI {

namespace {

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        current.push_back(c);
        if (c == ' ') {
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string LastUserText(const std::vector<Message>& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == Role::User) {
            if (!it->text.empty()) return it->text;
            for (const auto& part : it->parts) {
                if (auto* tp = std::get_if<TextPart>(&part)) return tp->text;
            }
        }
    }
    return "";
}

class MockStreamHandle : public IStreamHandle {
public:
    void Cancel() override {
        cancelled.store(true);
    }
    bool IsDone() const override {
        return done.load();
    }
    void MarkDone() { done.store(true); }
    bool IsCancelled() const { return cancelled.load(); }

    void AttachThread(std::thread t) {
        std::lock_guard<std::mutex> lock(threadMutex);
        worker = std::move(t);
    }
    ~MockStreamHandle() override {
        std::lock_guard<std::mutex> lock(threadMutex);
        if (worker.joinable()) worker.join();
    }

private:
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::mutex threadMutex;
    std::thread worker;
};

} // namespace

// =====================================================================
// Impl
// =====================================================================

struct MockTextLLM::Impl {
    std::mutex mu;
    std::deque<ChatResponse> scriptedResponses;
    std::deque<ToolCall> scriptedToolCalls;
    std::deque<Error> scriptedErrors;
    std::function<int32_t(const std::vector<Message>&)> tokenCounter;
    std::chrono::milliseconds streamDelay{5};
    int callCount = 0;

    ChatResponse NextResponse(const ChatRequest& request) {
        std::lock_guard<std::mutex> lock(mu);
        ++callCount;

        ChatResponse resp;
        resp.id = "mock-" + std::to_string(callCount);
        resp.model = request.model.empty() ? "mock-1" : request.model;

        if (!scriptedErrors.empty()) {
            resp.error = scriptedErrors.front();
            scriptedErrors.pop_front();
            resp.finishReason = FinishReason::Error;
            return resp;
        }

        if (!scriptedToolCalls.empty()) {
            resp.toolCalls.push_back(std::move(scriptedToolCalls.front()));
            scriptedToolCalls.pop_front();
            resp.finishReason = FinishReason::ToolCalls;
            return resp;
        }

        if (!scriptedResponses.empty()) {
            ChatResponse scripted = std::move(scriptedResponses.front());
            scriptedResponses.pop_front();
            if (scripted.id.empty())    scripted.id    = resp.id;
            if (scripted.model.empty()) scripted.model = resp.model;
            if (scripted.finishReason == FinishReason::Unknown)
                scripted.finishReason = FinishReason::Stop;
            return scripted;
        }

        resp.text = "Mock reply: " + LastUserText(request.messages);
        resp.finishReason = FinishReason::Stop;
        resp.usage.inputTokens  = static_cast<int32_t>(resp.text.size() / 4);
        resp.usage.outputTokens = static_cast<int32_t>(resp.text.size() / 4);
        return resp;
    }
};

// =====================================================================
// MockTextLLM
// =====================================================================

MockTextLLM::MockTextLLM() : impl_(std::make_unique<Impl>()) {}
MockTextLLM::~MockTextLLM() = default;

void MockTextLLM::EnqueueResponse(std::string text) {
    ChatResponse resp;
    resp.text = std::move(text);
    resp.finishReason = FinishReason::Stop;
    EnqueueResponse(std::move(resp));
}

void MockTextLLM::EnqueueResponse(ChatResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scriptedResponses.push_back(std::move(response));
}

void MockTextLLM::EnqueueToolCall(ToolCall call) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scriptedToolCalls.push_back(std::move(call));
}

void MockTextLLM::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scriptedErrors.push_back(std::move(error));
}

void MockTextLLM::SetTokenCounter(
    std::function<int32_t(const std::vector<Message>&)> fn) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->tokenCounter = std::move(fn);
}

void MockTextLLM::SetStreamDelay(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->streamDelay = delay;
}

int MockTextLLM::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

void MockTextLLM::ResetCounters() {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->callCount = 0;
}

ProviderCapabilities MockTextLLM::GetCapabilities() const {
    ProviderCapabilities caps;
    caps.providerId = "mock";
    caps.supportsStreaming = true;
    caps.supportsTools = true;
    caps.supportsVision = false;
    caps.supportsEmbeddings = false;

    ModelInfo m;
    m.id = "mock-1";
    m.displayName = "UltraAI Mock Model";
    m.contextWindow = 128000;
    m.maxOutputTokens = 4096;
    m.supportsTools = true;
    m.supportsStreaming = true;
    m.supportsJsonSchema = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

ChatResponse MockTextLLM::Chat(const ChatRequest& request) {
    return impl_->NextResponse(request);
}

std::future<ChatResponse> MockTextLLM::ChatAsync(const ChatRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Chat(request); });
}

StreamHandle MockTextLLM::ChatStream(const ChatRequest& request,
                                     StreamCallback onEvent) {
    auto handle = std::make_shared<MockStreamHandle>();
    auto response = impl_->NextResponse(request);
    auto delay = impl_->streamDelay;

    std::thread worker([handle, response, onEvent, delay]() {
        if (response.error.code != ErrorCode::None) {
            StreamEvent ev;
            ev.kind  = StreamEventKind::Error;
            ev.error = response.error;
            ev.finishReason = FinishReason::Error;
            if (onEvent && !handle->IsCancelled()) onEvent(ev);
            handle->MarkDone();
            return;
        }

        for (const auto& tc : response.toolCalls) {
            if (handle->IsCancelled()) break;
            StreamEvent ev;
            ev.kind = StreamEventKind::ToolCallEnd;
            ev.toolCallDelta = tc;
            if (onEvent) onEvent(ev);
            std::this_thread::sleep_for(delay);
        }

        for (const auto& word : SplitWords(response.text)) {
            if (handle->IsCancelled()) break;
            StreamEvent ev;
            ev.kind = StreamEventKind::TextDelta;
            ev.textDelta = word;
            if (onEvent) onEvent(ev);
            std::this_thread::sleep_for(delay);
        }

        StreamEvent done;
        done.kind = StreamEventKind::Done;
        done.usage = response.usage;
        done.finishReason = handle->IsCancelled()
                                ? FinishReason::Error
                                : response.finishReason;
        if (onEvent) onEvent(done);
        handle->MarkDone();
    });
    handle->AttachThread(std::move(worker));
    return handle;
}

int32_t MockTextLLM::CountTokens(const std::string& /*model*/,
                                 const std::vector<Message>& messages) {
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (impl_->tokenCounter) return impl_->tokenCounter(messages);
    }
    size_t chars = 0;
    for (const auto& m : messages) {
        chars += m.text.size();
        for (const auto& part : m.parts) {
            if (auto* tp = std::get_if<TextPart>(&part)) chars += tp->text.size();
        }
    }
    return static_cast<int32_t>(chars / 4);
}

// =====================================================================
// Direct factory
// =====================================================================

std::unique_ptr<ITextLLM> CreateMockTextLLM() {
    return std::make_unique<MockTextLLM>();
}

} // namespace UltraAI
