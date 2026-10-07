// UltraAI/tests/test_textllm_translator.cpp
// Exercises the text-LLM-backed translator (UltraAITextLLMTranslator.h):
// what it sends to the chat model, how it reads the reply back — in order,
// out of order, fenced, bare — how batches are split, how LLM errors and a
// cut-off reply surface, language detection, and its registration as a
// translator provider for every text-LLM provider.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAI.h"
#include "UltraAIMockTextLLM.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraAI;

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

// A text LLM that records every request and answers from a script.
class RecordingLLM : public ITextLLM {
public:
    std::vector<ChatRequest> requests;
    std::deque<ChatResponse> replies;

    void Reply(const std::string& text,
               FinishReason finish = FinishReason::Stop) {
        ChatResponse r;
        r.text = text;
        r.finishReason = finish;
        r.usage.inputTokens = 10;
        r.usage.outputTokens = 5;
        replies.push_back(std::move(r));
    }
    void Fail(ErrorCode code, const std::string& message) {
        ChatResponse r;
        r.error.code = code;
        r.error.message = message;
        r.finishReason = FinishReason::Error;
        replies.push_back(std::move(r));
    }

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId = "recorder";
        ModelInfo m;
        m.id = "recorder-1";
        m.displayName = "Recorder";
        m.runsLocally = true;
        caps.models.push_back(m);
        return caps;
    }
    ChatResponse Chat(const ChatRequest& request) override {
        requests.push_back(request);
        ChatResponse r;
        if (replies.empty()) {
            r.error.code = ErrorCode::ProviderError;
            r.error.message = "no scripted reply";
            return r;
        }
        r = std::move(replies.front());
        replies.pop_front();
        r.model = request.model.empty() ? "recorder-1" : request.model;
        return r;
    }
    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        std::promise<ChatResponse> p;
        p.set_value(Chat(request));
        return p.get_future();
    }
    StreamHandle ChatStream(const ChatRequest&, StreamCallback) override {
        return nullptr;
    }
    int32_t CountTokens(const std::string&,
                        const std::vector<Message>& messages) override {
        int32_t n = 0;
        for (const auto& m : messages) n += static_cast<int32_t>(m.text.size() / 4);
        return n;
    }
    void* RawProvider() override { return this; }
};

struct Fixture {
    RecordingLLM* llm = nullptr;
    std::unique_ptr<ITranslator> translator;

    explicit Fixture(TranslatorConfig cfg = {}) {
        auto owned = std::make_unique<RecordingLLM>();
        llm = owned.get();
        Error err;
        translator = CreateTextLLMTranslator(std::move(owned), cfg, &err);
        EXPECT_TRUE(translator != nullptr);
        EXPECT_EQ(err.code, ErrorCode::None);
    }
};

// ============================================================
// The request the model sees
// ============================================================

void TestPromptAndRequestShape() {
    Fixture f;
    f.llm->Reply(R"({"segments":[{"id":1,"text":"Bonjour le monde","source":"en"},)"
                 R"({"id":2,"text":"Au revoir","source":"en"}]})");

    TranslateRequest req;
    req.model = "my-model";
    req.texts = { "Hello \"world\"\nsecond line", "Goodbye" };
    req.targetLanguage = "fr";
    req.formality = TranslationFormality::Formal;
    req.domain = "technical";
    req.options["textllm.glossary"] = std::string("world = monde");
    req.options["vendor.knob"] = int64_t{7};

    auto resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.results.size(), size_t{2});
    EXPECT_EQ(resp.results[0].text, std::string("Bonjour le monde"));
    EXPECT_EQ(resp.results[0].detectedSourceLanguage, std::string("en"));
    EXPECT_EQ(resp.results[1].text, std::string("Au revoir"));
    EXPECT_EQ(resp.model, std::string("my-model"));
    // Characters of the input, tokens of the exchange.
    EXPECT_EQ(resp.usage.units, int32_t{32});
    EXPECT_EQ(resp.usage.inputTokens, int32_t{10});
    EXPECT_EQ(resp.usage.outputTokens, int32_t{5});

    EXPECT_EQ(f.llm->requests.size(), size_t{1});
    const ChatRequest& chat = f.llm->requests[0];
    EXPECT_EQ(chat.model, std::string("my-model"));
    EXPECT_TRUE(chat.responseFormat == ResponseFormat::JsonObject);
    EXPECT_TRUE(chat.sampling.temperature.has_value());
    EXPECT_EQ(*chat.sampling.temperature, 0.0);
    EXPECT_EQ(chat.messages.size(), size_t{2});
    EXPECT_TRUE(chat.messages[0].role == Role::System);
    EXPECT_TRUE(chat.messages[1].role == Role::User);

    const std::string& system = chat.messages[0].text;
    EXPECT_TRUE(Contains(system, "formal register"));
    EXPECT_TRUE(Contains(system, "\"technical\" domain"));
    EXPECT_TRUE(Contains(system, "world = monde"));
    EXPECT_TRUE(Contains(system, "Keep the markup"));
    EXPECT_TRUE(Contains(system, "Detect each segment's source language"));

    // The user message is JSON with the texts escaped and numbered.
    const std::string& user = chat.messages[1].text;
    EXPECT_TRUE(Contains(user, "\"target\":\"fr\""));
    EXPECT_TRUE(Contains(user, R"({"id":1,"text":"Hello \"world\"\nsecond line"})"));
    EXPECT_TRUE(Contains(user, R"({"id":2,"text":"Goodbye"})"));

    // Vendor options pass through; the translator's own do not.
    EXPECT_TRUE(chat.options.count("vendor.knob") == 1);
    EXPECT_TRUE(chat.options.count("textllm.glossary") == 0);
}

void TestPromptVariants() {
    TranslateRequest req;
    req.texts = { "x" };
    req.targetLanguage = "de";
    req.sourceLanguage = "en";
    req.preserveFormatting = false;
    req.formality = TranslationFormality::Informal;
    const std::string p = BuildTranslationPrompt(req, 0, 1);
    EXPECT_TRUE(Contains(p, "The source language is \"en\""));
    EXPECT_TRUE(!Contains(p, "Detect each segment"));
    EXPECT_TRUE(!Contains(p, "Keep the markup"));
    EXPECT_TRUE(Contains(p, "informal register"));
    EXPECT_TRUE(!Contains(p, "domain"));
}

// ============================================================
// Reading the reply
// ============================================================

void TestReplyOutOfOrderAndFenced() {
    Fixture f;
    f.llm->Reply("Here you go:\n```json\n"
                 R"({"segments":[{"id":"2","text":"zwei","source":"en","confidence":0.8},)"
                 R"({"id":1,"text":"eins","source":"en"}]})"
                 "\n```");
    TranslateRequest req;
    req.texts = { "one", "two" };
    req.targetLanguage = "de";
    auto resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.results[0].text, std::string("eins"));
    EXPECT_EQ(resp.results[1].text, std::string("zwei"));
    EXPECT_EQ(resp.results[1].confidence, 0.8);
}

void TestReplyWithoutIdsAndUnicodeEscapes() {
    std::vector<TranslateResult> results;
    Error err;
    EXPECT_TRUE(ParseTranslationReply(
        R"({"translations":[{"text":"café 😀"},{"translation":"b"}]})",
        2, results, err));
    EXPECT_EQ(results.size(), size_t{2});
    EXPECT_EQ(results[0].text, std::string("caf\xC3\xA9 \xF0\x9F\x98\x80"));
    EXPECT_EQ(results[1].text, std::string("b"));
}

void TestBareReplyForOneText() {
    std::vector<TranslateResult> results;
    Error err;
    EXPECT_TRUE(ParseTranslationReply("```\nBonjour\n```\n", 1, results, err));
    EXPECT_EQ(results.size(), size_t{1});
    EXPECT_EQ(results[0].text, std::string("Bonjour"));
    EXPECT_TRUE(results[0].detectedSourceLanguage.empty());

    // Two texts need JSON.
    EXPECT_TRUE(!ParseTranslationReply("Bonjour\nAu revoir", 2, results, err));
    EXPECT_EQ(err.code, ErrorCode::ProviderError);
}

void TestReplyMissingSegments() {
    std::vector<TranslateResult> results;
    Error err;
    EXPECT_TRUE(!ParseTranslationReply(R"({"segments":[{"id":1,"text":"a"}]})",
                                       2, results, err));
    EXPECT_EQ(err.code, ErrorCode::ProviderError);
    EXPECT_TRUE(Contains(err.message, "1 of 2"));

    EXPECT_TRUE(!ParseTranslationReply(R"({"foo":1})", 1, results, err));
    EXPECT_TRUE(!ParseTranslationReply(R"({"segments":[{"id":1,"text":"a"})",
                                       1, results, err));   // truncated
}

// ============================================================
// Batching, errors, source-language fill-in
// ============================================================

void TestBatchesAndSourceFillIn() {
    TranslatorConfig cfg;
    cfg.providerOptions["textllm.batchSize"] = int64_t{2};
    cfg.providerOptions["textllm.temperature"] = 0.3;
    cfg.providerOptions["textllm.maxOutputTokens"] = int64_t{512};
    Fixture f(cfg);
    f.llm->Reply(R"({"segments":[{"id":1,"text":"a'"},{"id":2,"text":"b'"}]})");
    f.llm->Reply(R"({"segments":[{"id":1,"text":"c'"}]})");

    TranslateRequest req;
    req.texts = { "a", "b", "c" };
    req.targetLanguage = "es";
    req.sourceLanguage = "en";
    auto resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.results.size(), size_t{3});
    EXPECT_EQ(resp.results[2].text, std::string("c'"));
    // The model reported no source; the request's fills in.
    EXPECT_EQ(resp.results[2].detectedSourceLanguage, std::string("en"));
    EXPECT_EQ(resp.usage.inputTokens, int32_t{20});

    EXPECT_EQ(f.llm->requests.size(), size_t{2});
    EXPECT_TRUE(Contains(f.llm->requests[0].messages[1].text, R"({"id":2,"text":"b"})"));
    // The second batch numbers its segments from 1 again.
    EXPECT_TRUE(Contains(f.llm->requests[1].messages[1].text, R"({"id":1,"text":"c"})"));
    EXPECT_EQ(*f.llm->requests[1].sampling.temperature, 0.3);
    EXPECT_EQ(*f.llm->requests[1].sampling.maxOutputTokens, int32_t{512});
}

void TestErrors() {
    Fixture f;
    TranslateRequest req;
    req.texts = { "a" };

    // No target language: refused before any call.
    auto resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::InvalidRequest);
    EXPECT_EQ(f.llm->requests.size(), size_t{0});

    // No texts: nothing to send.
    req.targetLanguage = "fr";
    req.texts.clear();
    resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_TRUE(resp.results.empty());
    EXPECT_EQ(f.llm->requests.size(), size_t{0});

    // The LLM's error is the translator's error.
    req.texts = { "a" };
    f.llm->Fail(ErrorCode::AuthenticationFailed, "no key");
    resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(resp.error.message, std::string("no key"));

    // A cut-off reply says what to change.
    f.llm->Reply(R"({"segments":[{"id":1,"text":"a)", FinishReason::Length);
    resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::ContextLengthExceeded);
    EXPECT_TRUE(Contains(resp.error.message, "textllm.batchSize"));

    // A refusal.
    f.llm->Reply("", FinishReason::ContentFiltered);
    resp = f.translator->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::ContentFiltered);

    // Async goes through the same path.
    f.llm->Reply(R"({"segments":[{"id":1,"text":"A"}]})");
    auto fut = f.translator->TranslateAsync(req);
    EXPECT_EQ(fut.get().results[0].text, std::string("A"));
}

// ============================================================
// Language detection
// ============================================================

void TestDetectLanguage() {
    Fixture f;
    f.llm->Reply(R"({"segments":[)"
                 R"({"id":2,"guesses":[{"language":"es","confidence":0.9},)"
                 R"({"language":"pt","confidence":0.1},{"language":"it","confidence":0.0}]},)"
                 R"({"id":1,"language":"de"}]})");
    DetectLanguageRequest req;
    req.texts = { "der schnelle Fuchs", "el coche rojo" };
    req.topN = 2;
    auto resp = f.translator->DetectLanguage(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.guesses.size(), size_t{2});
    EXPECT_EQ(resp.guesses[0].size(), size_t{1});
    EXPECT_EQ(resp.guesses[0][0].language, std::string("de"));
    EXPECT_EQ(resp.guesses[1].size(), size_t{2});          // topN caps it
    EXPECT_EQ(resp.guesses[1][0].language, std::string("es"));
    EXPECT_EQ(resp.guesses[1][0].confidence, 0.9);
    EXPECT_EQ(resp.guesses[1][1].language, std::string("pt"));

    const ChatRequest& chat = f.llm->requests[0];
    EXPECT_TRUE(chat.responseFormat == ResponseFormat::JsonObject);
    EXPECT_TRUE(Contains(chat.messages[0].text, "up to 2 guesses"));
    EXPECT_TRUE(Contains(chat.messages[1].text, R"({"id":1,"text":"der schnelle Fuchs"})"));

    f.llm->Fail(ErrorCode::NetworkError, "down");
    resp = f.translator->DetectLanguage(req);
    EXPECT_EQ(resp.error.code, ErrorCode::NetworkError);
}

// ============================================================
// Capabilities and the escape hatch
// ============================================================

void TestCapabilities() {
    TranslatorConfig cfg;
    cfg.providerId = "my-translator";
    Fixture f(cfg);
    auto caps = f.translator->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("my-translator"));
    EXPECT_EQ(caps.models.size(), size_t{1});
    EXPECT_EQ(caps.models[0].id, std::string("recorder-1"));
    EXPECT_TRUE(caps.models[0].runsLocally);
    EXPECT_TRUE(caps.models[0].supportsAutoDetect);
    EXPECT_TRUE(caps.models[0].supportsFormality);
    // Without a providerId the LLM's id is reported.
    Fixture g;
    EXPECT_EQ(g.translator->GetCapabilities().providerId, std::string("recorder"));
    // RawProvider is the wrapped text LLM.
    EXPECT_TRUE(g.translator->RawProvider() == g.llm);

    Error err;
    EXPECT_TRUE(CreateTextLLMTranslator(nullptr, {}, &err) == nullptr);
    EXPECT_EQ(err.code, ErrorCode::InvalidRequest);
}

// ============================================================
// Registration: a translator for every text-LLM provider
// ============================================================

void TestRegistry() {
    // A text-LLM provider registered at runtime becomes a translator
    // provider of the same id, and CreateTranslator goes through it.
    RegisterTextLLMProvider("recorder",
        [](const TextLLMConfig&, Error*) -> std::unique_ptr<ITextLLM> {
            auto llm = std::make_unique<RecordingLLM>();
            llm->Reply(R"({"segments":[{"id":1,"text":"ciao","source":"en"}]})");
            return llm;
        });
    const std::vector<std::string> translators = ListTranslatorProviders();
    EXPECT_TRUE(std::find(translators.begin(), translators.end(), "recorder")
                != translators.end());
    // Every compiled-in text-LLM provider but the mock is one too.
    for (const std::string& id : ListTextLLMProviders()) {
        if (id == "mock") continue;
        EXPECT_TRUE(std::find(translators.begin(), translators.end(), id)
                    != translators.end());
    }

    TranslatorConfig cfg;
    cfg.providerId = "recorder";
    Error err;
    auto tr = CreateTranslator(cfg, &err);
    EXPECT_TRUE(tr != nullptr);
    EXPECT_EQ(tr->GetCapabilities().providerId, std::string("recorder"));
    TranslateRequest req;
    req.texts = { "hello" };
    req.targetLanguage = "it";
    auto resp = tr->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.results[0].text, std::string("ciao"));

    // "textllm.provider" picks the LLM when the translator id differs.
    TranslatorConfig viaOption;
    viaOption.providerOptions["textllm.provider"] = std::string("recorder");
    auto tr2 = CreateTextLLMTranslator(viaOption, &err);
    EXPECT_TRUE(tr2 != nullptr);
    EXPECT_EQ(tr2->GetCapabilities().providerId, std::string("recorder"));

    // The mock keeps its own translator, not the LLM-backed one.
    TranslatorConfig mockCfg;
    mockCfg.providerId = "mock";
    auto mock = CreateTranslator(mockCfg, &err);
    EXPECT_TRUE(mock != nullptr);
    resp = mock->Translate(req);
    EXPECT_EQ(resp.results[0].text, std::string("[it] hello"));

    // An unknown LLM provider fails the way CreateTextLLM does.
    TranslatorConfig unknown;
    unknown.providerOptions["textllm.provider"] = std::string("no-such-llm");
    err = {};
    EXPECT_TRUE(CreateTextLLMTranslator(unknown, &err) == nullptr);
    EXPECT_EQ(err.code, ErrorCode::ModelNotFound);
}

} // namespace

int main() {
    TestPromptAndRequestShape();
    TestPromptVariants();
    TestReplyOutOfOrderAndFenced();
    TestReplyWithoutIdsAndUnicodeEscapes();
    TestBareReplyForOneText();
    TestReplyMissingSegments();
    TestBatchesAndSourceFillIn();
    TestErrors();
    TestDetectLanguage();
    TestCapabilities();
    TestRegistry();
    std::cout << "test_textllm_translator: all checks passed" << std::endl;
    return 0;
}
