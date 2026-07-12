// UltraAI/adapters/mock/src/MockCodeAssist.cpp
// Version: 0.1.0

#include "UltraAIMockCodeAssist.h"

#include <atomic>
#include <deque>
#include <thread>

namespace UltraAI {

namespace {

class StreamHandleImpl : public IStreamHandle {
public:
    void Cancel() override { cancelled.store(true); }
    bool IsDone() const override { return done.load(); }
    void MarkDone() { done.store(true); }
    bool IsCancelled() const { return cancelled.load(); }
    void AttachThread(std::thread t) {
        std::lock_guard<std::mutex> lock(mu);
        worker = std::move(t);
    }
    ~StreamHandleImpl() override {
        std::lock_guard<std::mutex> lock(mu);
        if (worker.joinable()) worker.join();
    }
private:
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::mutex mu;
    std::thread worker;
};

CodeAssistResponse SynthesizeResponse(const CodeAssistRequest& req) {
    CodeAssistResponse r;
    r.model = req.model.empty() ? "mock-code-1" : req.model;

    switch (req.task) {
        case CodeTask::Generate:
            r.code = "// generated " +
                     (req.language.empty() ? std::string("code") : req.language) +
                     " for: " + req.instruction + "\n";
            break;
        case CodeTask::Explain:
            r.explanation = "Mock explanation: " + req.codeSnippet;
            break;
        case CodeTask::Refactor:
            r.code = "/* refactored */\n" + req.codeSnippet;
            break;
        case CodeTask::TranslateLanguage:
            r.code = "// ported from " + req.language + " to " +
                     req.targetLanguage + "\n" + req.codeSnippet;
            break;
        case CodeTask::DetectBugs: {
            CodeDiagnostic d;
            d.severity = CodeDiagnostic::Severity::Warning;
            d.message  = "Mock diagnostic";
            d.line = 1; d.column = 1;
            d.endLine = 1; d.endColumn = 1;
            d.suggestedFix = "// fix";
            r.diagnostics.push_back(std::move(d));
            break;
        }
        case CodeTask::GenerateTests:
            r.code = "// tests for:\n" + req.codeSnippet + "\n// TEST OK\n";
            break;
        case CodeTask::Document:
            r.code = "/** mock doc */\n" + req.codeSnippet;
            break;
        case CodeTask::Complete:
            r.code = req.prefix + "/*FIM*/" + req.suffix;
            break;
    }

    if (req.requestExplanation && r.explanation.empty()) {
        r.explanation = "Mock explanation for task";
    }
    int suggestions = req.numSuggestions > 1 ? req.numSuggestions - 1 : 0;
    for (int i = 0; i < suggestions; ++i) {
        r.alternativeSuggestions.push_back(r.code + "\n// alt " + std::to_string(i));
    }

    int chars = static_cast<int>(r.code.size() + r.explanation.size());
    r.usage.inputTokens  = chars / 4;
    r.usage.outputTokens = chars / 4;
    return r;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        cur.push_back(c);
        if (c == ' ' || c == '\n') { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace

struct MockCodeAssist::Impl {
    std::mutex mu;
    std::deque<CodeAssistResponse> scripted;
    std::deque<Error> errors;
    std::chrono::milliseconds streamDelay{0};
    int callCount = 0;
};

MockCodeAssist::MockCodeAssist() : impl_(std::make_unique<Impl>()) {}
MockCodeAssist::~MockCodeAssist() = default;

void MockCodeAssist::EnqueueResponse(CodeAssistResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockCodeAssist::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
void MockCodeAssist::SetStreamDelay(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->streamDelay = delay;
}
int MockCodeAssist::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

CodeAssistProviderCapabilities MockCodeAssist::GetCapabilities() const {
    CodeAssistProviderCapabilities caps;
    caps.providerId = "mock";
    CodeAssistModelInfo m;
    m.id = "mock-code-1";
    m.displayName = "UltraAI Mock Code";
    m.contextWindow = 65536;
    m.maxOutputTokens = 4096;
    m.supportedTasks = {
        CodeTask::Generate, CodeTask::Explain, CodeTask::Refactor,
        CodeTask::TranslateLanguage, CodeTask::DetectBugs,
        CodeTask::GenerateTests, CodeTask::Document, CodeTask::Complete
    };
    m.supportedLanguages = { "cpp", "c", "python", "javascript", "typescript",
                             "rust", "go", "java", "csharp" };
    m.supportsFim = true;
    m.supportsStreaming = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

CodeAssistResponse MockCodeAssist::Run(const CodeAssistRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    if (!impl_->errors.empty()) {
        CodeAssistResponse r;
        r.error = impl_->errors.front();
        impl_->errors.pop_front();
        return r;
    }
    if (!impl_->scripted.empty()) {
        CodeAssistResponse out = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        if (out.model.empty()) {
            out.model = request.model.empty() ? "mock-code-1" : request.model;
        }
        return out;
    }
    return SynthesizeResponse(request);
}

std::future<CodeAssistResponse> MockCodeAssist::RunAsync(
    const CodeAssistRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Run(request); });
}

StreamHandle MockCodeAssist::RunStream(const CodeAssistRequest& request,
                                       CodeStreamCallback onEvent) {
    auto handle = std::make_shared<StreamHandleImpl>();
    auto response = Run(request);
    auto delay = impl_->streamDelay;

    std::thread worker([handle, response, onEvent, delay]() {
        if (response.error.code != ErrorCode::None) {
            CodeStreamEvent ev;
            ev.kind = CodeStreamEventKind::Error;
            ev.error = response.error;
            if (onEvent) onEvent(ev);
            handle->MarkDone();
            return;
        }
        for (const auto& chunk : SplitWords(response.code)) {
            if (handle->IsCancelled()) break;
            CodeStreamEvent ev;
            ev.kind = CodeStreamEventKind::CodeDelta;
            ev.textDelta = chunk;
            if (onEvent) onEvent(ev);
            std::this_thread::sleep_for(delay);
        }
        for (const auto& chunk : SplitWords(response.explanation)) {
            if (handle->IsCancelled()) break;
            CodeStreamEvent ev;
            ev.kind = CodeStreamEventKind::ExplanationDelta;
            ev.textDelta = chunk;
            if (onEvent) onEvent(ev);
            std::this_thread::sleep_for(delay);
        }
        for (const auto& d : response.diagnostics) {
            if (handle->IsCancelled()) break;
            CodeStreamEvent ev;
            ev.kind = CodeStreamEventKind::Diagnostic;
            ev.diagnostic = d;
            if (onEvent) onEvent(ev);
        }
        if (onEvent && !handle->IsCancelled()) {
            CodeStreamEvent done;
            done.kind = CodeStreamEventKind::Done;
            done.usage = response.usage;
            onEvent(done);
        }
        handle->MarkDone();
    });
    handle->AttachThread(std::move(worker));
    return handle;
}

std::unique_ptr<ICodeAssist> CreateMockCodeAssist() {
    return std::make_unique<MockCodeAssist>();
}

} // namespace UltraAI
