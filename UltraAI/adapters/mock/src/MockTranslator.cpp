// UltraAI/adapters/mock/src/MockTranslator.cpp
// Version: 0.1.0

#include "UltraAIMockTranslator.h"

#include <deque>

namespace UltraAI {

namespace {

std::string GuessLanguage(const std::string& text) {
    // Toy language detector based on a few high-frequency words.
    auto contains = [&](const char* needle) {
        return text.find(needle) != std::string::npos;
    };
    if (contains(" der ") || contains(" und ") || contains(" ist ")) return "de";
    if (contains(" le ") || contains(" et ")  || contains(" est ")) return "fr";
    if (contains(" el ") || contains(" y ")   || contains(" es "))  return "es";
    return "en";
}

} // namespace

struct MockTranslator::Impl {
    std::mutex mu;
    std::deque<TranslateResponse> scripted;
    std::deque<Error> errors;
    int callCount = 0;
};

MockTranslator::MockTranslator() : impl_(std::make_unique<Impl>()) {}
MockTranslator::~MockTranslator() = default;

void MockTranslator::EnqueueResponse(TranslateResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockTranslator::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
int MockTranslator::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

TranslatorProviderCapabilities MockTranslator::GetCapabilities() const {
    TranslatorProviderCapabilities caps;
    caps.providerId = "mock";
    caps.maxBatchSize = 64;
    TranslatorModelInfo m;
    m.id = "mock-translator-1";
    m.displayName = "UltraAI Mock Translator";
    m.supportedLanguages = { "en", "de", "fr", "es", "ja", "zh" };
    m.supportsAutoDetect = true;
    m.supportsBatching = true;
    m.supportsFormality = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

TranslateResponse MockTranslator::Translate(const TranslateRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    TranslateResponse resp;
    resp.model = request.model.empty() ? "mock-translator-1" : request.model;

    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }
    if (!impl_->scripted.empty()) {
        TranslateResponse out = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        if (out.model.empty()) out.model = resp.model;
        return out;
    }

    int32_t totalChars = 0;
    resp.results.reserve(request.texts.size());
    for (const auto& t : request.texts) {
        TranslateResult r;
        r.detectedSourceLanguage = request.sourceLanguage.empty()
            ? GuessLanguage(t)
            : request.sourceLanguage;
        r.text = "[" + request.targetLanguage + "] " + t;
        r.confidence = 0.95;
        resp.results.push_back(std::move(r));
        totalChars += static_cast<int32_t>(t.size());
    }
    resp.usage.units = totalChars;
    return resp;
}

std::future<TranslateResponse> MockTranslator::TranslateAsync(
    const TranslateRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Translate(request); });
}

DetectLanguageResponse MockTranslator::DetectLanguage(
    const DetectLanguageRequest& request) {
    DetectLanguageResponse resp;
    resp.guesses.reserve(request.texts.size());
    for (const auto& t : request.texts) {
        std::vector<LanguageGuess> g;
        LanguageGuess top;
        top.language = GuessLanguage(t);
        top.confidence = 0.9;
        g.push_back(top);
        if (request.topN > 1) {
            LanguageGuess alt;
            alt.language = top.language == "en" ? "de" : "en";
            alt.confidence = 0.05;
            g.push_back(alt);
        }
        resp.guesses.push_back(std::move(g));
    }
    return resp;
}

std::unique_ptr<ITranslator> CreateMockTranslator() {
    return std::make_unique<MockTranslator>();
}

} // namespace UltraAI
