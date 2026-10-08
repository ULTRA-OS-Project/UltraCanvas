// UltraAI/src/TextLLMTranslator.cpp
// ITranslator on top of any ITextLLM — see UltraAITextLLMTranslator.h.
//
// The exchange with the model is JSON both ways: the request carries the
// segments under numbered ids, and the reply is asked for as a JSON object
// with the same ids, so a batch comes back in order whatever the model
// does with it. The reply is read by a small JSON reader of this file's
// own: UltraAI_Core builds without the vendored nlohmann/json (the module
// is standalone), and the shape to read is three keys deep.
//
// Version: 0.1.0
// Last Modified: 2026-10-07
// Author: UltraAI Module

#include "UltraAITextLLMTranslator.h"

#include <algorithm>
#include <cstdint>
#include <future>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace UltraAI {

namespace {

// =====================================================================
// A JSON value and a reader for the model's reply
// =====================================================================

// An object is kept as two parallel vectors rather than a vector of pairs:
// std::vector may hold the incomplete JsonValue, but a std::pair of it
// cannot be instantiated inside the class (libstdc++ 12 with clang 19).
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::string> objectKeys;
    std::vector<JsonValue> objectValues;

    const JsonValue* Get(const std::string& key) const;
};

const JsonValue* JsonValue::Get(const std::string& key) const {
    if (kind != Kind::Object) return nullptr;
    for (size_t i = 0; i < objectKeys.size(); ++i) {
        if (objectKeys[i] == key) return &objectValues[i];
    }
    return nullptr;
}

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& out) {
        SkipSpace();
        if (!ParseValue(out)) return false;
        SkipSpace();
        return pos_ == text_.size();
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }

    void SkipSpace() {
        while (!AtEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool Consume(const char* literal) {
        size_t n = 0;
        while (literal[n]) ++n;
        if (text_.compare(pos_, n, literal) != 0) return false;
        pos_ += n;
        return true;
    }

    bool ParseValue(JsonValue& out) {
        switch (Peek()) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': out.kind = JsonValue::Kind::String;
                      return ParseString(out.string);
            case 't': out.kind = JsonValue::Kind::Bool; out.boolean = true;
                      return Consume("true");
            case 'f': out.kind = JsonValue::Kind::Bool; out.boolean = false;
                      return Consume("false");
            case 'n': out.kind = JsonValue::Kind::Null;
                      return Consume("null");
            default:  return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue& out) {
        out.kind = JsonValue::Kind::Object;
        ++pos_;                                   // '{'
        SkipSpace();
        if (Peek() == '}') { ++pos_; return true; }
        for (;;) {
            SkipSpace();
            if (Peek() != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            SkipSpace();
            if (Peek() != ':') return false;
            ++pos_;
            SkipSpace();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.objectKeys.push_back(std::move(key));
            out.objectValues.push_back(std::move(value));
            SkipSpace();
            if (Peek() == ',') { ++pos_; continue; }
            if (Peek() == '}') { ++pos_; return true; }
            return false;
        }
    }

    bool ParseArray(JsonValue& out) {
        out.kind = JsonValue::Kind::Array;
        ++pos_;                                   // '['
        SkipSpace();
        if (Peek() == ']') { ++pos_; return true; }
        for (;;) {
            SkipSpace();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.array.push_back(std::move(value));
            SkipSpace();
            if (Peek() == ',') { ++pos_; continue; }
            if (Peek() == ']') { ++pos_; return true; }
            return false;
        }
    }

    static int HexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    }

    bool ParseHex4(uint32_t& code) {
        if (pos_ + 4 > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const int d = HexDigit(text_[pos_ + static_cast<size_t>(i)]);
            if (d < 0) return false;
            code = (code << 4) | static_cast<uint32_t>(d);
        }
        pos_ += 4;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool ParseString(std::string& out) {
        ++pos_;                                   // opening quote
        for (;;) {
            if (AtEnd()) return false;
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (AtEnd()) return false;
            const char e = text_[pos_++];
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    uint32_t code = 0;
                    if (!ParseHex4(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        // High surrogate: a low one must follow.
                        uint32_t low = 0;
                        if (Consume("\\u") && ParseHex4(low) &&
                            low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) +
                                   (low - 0xDC00);
                        } else {
                            code = 0xFFFD;
                        }
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        code = 0xFFFD;
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default: return false;
            }
        }
    }

    // JSON numbers are dot-decimal by definition; read them digit by digit
    // so the result never depends on the process locale.
    bool ParseNumber(JsonValue& out) {
        out.kind = JsonValue::Kind::Number;
        const size_t start = pos_;
        bool negative = false;
        if (Peek() == '-') { negative = true; ++pos_; }
        double value = 0.0;
        bool digits = false;
        while (Peek() >= '0' && Peek() <= '9') {
            value = value * 10.0 + (text_[pos_] - '0');
            ++pos_;
            digits = true;
        }
        if (Peek() == '.') {
            ++pos_;
            double scale = 0.1;
            while (Peek() >= '0' && Peek() <= '9') {
                value += (text_[pos_] - '0') * scale;
                scale *= 0.1;
                ++pos_;
                digits = true;
            }
        }
        if (!digits) { pos_ = start; return false; }
        if (Peek() == 'e' || Peek() == 'E') {
            ++pos_;
            bool expNegative = false;
            if (Peek() == '+') ++pos_;
            else if (Peek() == '-') { expNegative = true; ++pos_; }
            int exponent = 0;
            bool expDigits = false;
            while (Peek() >= '0' && Peek() <= '9') {
                if (exponent < 1000) exponent = exponent * 10 + (text_[pos_] - '0');
                ++pos_;
                expDigits = true;
            }
            if (!expDigits) { pos_ = start; return false; }
            for (int i = 0; i < exponent; ++i) {
                value = expNegative ? value / 10.0 : value * 10.0;
            }
        }
        out.number = negative ? -value : value;
        return true;
    }
};

// The model is asked for JSON only, but a chat model may still wrap it in
// a code fence or a sentence. Take the outermost object.
std::string ExtractJsonObject(const std::string& reply) {
    const size_t open = reply.find('{');
    const size_t close = reply.rfind('}');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return {};
    }
    return reply.substr(open, close - open + 1);
}

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\n' || s[b] == '\r' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

// A reply that is a bare translation in a code fence, or plain text.
std::string StripCodeFence(const std::string& reply) {
    std::string s = Trim(reply);
    if (s.rfind("```", 0) != 0) return s;
    const size_t firstLineEnd = s.find('\n');
    if (firstLineEnd == std::string::npos) return s;
    s = s.substr(firstLineEnd + 1);
    const size_t fence = s.rfind("```");
    if (fence != std::string::npos) s = s.substr(0, fence);
    return Trim(s);
}

// =====================================================================
// JSON writing for the prompt
// =====================================================================

void AppendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void AppendSegments(std::string& out, const std::vector<std::string>& texts,
                    size_t first, size_t count) {
    out += "\"segments\":[";
    for (size_t i = 0; i < count; ++i) {
        if (i) out += ',';
        out += "{\"id\":" + std::to_string(i + 1) + ",\"text\":";
        AppendJsonString(out, texts[first + i]);
        out += '}';
    }
    out += ']';
}

// =====================================================================
// Reading the model's reply
// =====================================================================

// The id a reply item carries, as 1-based position; 0 when it has none.
size_t ItemId(const JsonValue& item) {
    const JsonValue* id = item.Get("id");
    if (!id) return 0;
    if (id->kind == JsonValue::Kind::Number && id->number >= 1.0) {
        return static_cast<size_t>(id->number);
    }
    if (id->kind == JsonValue::Kind::String) {
        size_t n = 0;
        for (const char c : id->string) {
            if (c < '0' || c > '9') return 0;
            n = n * 10 + static_cast<size_t>(c - '0');
        }
        return n;
    }
    return 0;
}

const JsonValue* FindSegments(const JsonValue& root) {
    for (const char* key : {"segments", "translations", "results", "items"}) {
        const JsonValue* v = root.Get(key);
        if (v && v->kind == JsonValue::Kind::Array) return v;
    }
    return nullptr;
}

std::string StringField(const JsonValue& item,
                        std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const JsonValue* v = item.Get(key);
        if (v && v->kind == JsonValue::Kind::String) return v->string;
    }
    return {};
}

double NumberField(const JsonValue& item, const char* key) {
    const JsonValue* v = item.Get(key);
    if (v && v->kind == JsonValue::Kind::Number) return v->number;
    return 0.0;
}

// Place the reply's items into `count` slots by id, or by position when
// the model dropped the ids. False when a slot stays empty.
bool PlaceItems(const JsonValue& segments, size_t count,
                std::vector<const JsonValue*>& slots, Error& error) {
    slots.assign(count, nullptr);
    bool anyId = false;
    for (const JsonValue& item : segments.array) {
        if (ItemId(item) != 0) { anyId = true; break; }
    }
    if (anyId) {
        for (const JsonValue& item : segments.array) {
            const size_t id = ItemId(item);
            if (id >= 1 && id <= count && !slots[id - 1]) slots[id - 1] = &item;
        }
    } else if (segments.array.size() == count) {
        for (size_t i = 0; i < count; ++i) slots[i] = &segments.array[i];
    }
    const size_t filled = static_cast<size_t>(
        std::count_if(slots.begin(), slots.end(),
                      [](const JsonValue* p) { return p != nullptr; }));
    if (filled != count) {
        error.code = ErrorCode::ProviderError;
        error.message = "The model returned " + std::to_string(filled) +
                        " of " + std::to_string(count) + " segments";
        return false;
    }
    return true;
}

bool ParseReplyRoot(const std::string& reply, JsonValue& root,
                    const JsonValue*& segments, Error& error) {
    const std::string body = ExtractJsonObject(reply);
    if (body.empty() || !JsonReader(body).Parse(root)) {
        error.code = ErrorCode::ProviderError;
        error.message = "The model did not reply with JSON";
        return false;
    }
    segments = FindSegments(root);
    if (!segments) {
        error.code = ErrorCode::ProviderError;
        error.message = "The model's JSON has no \"segments\" array";
        return false;
    }
    return true;
}

// =====================================================================
// Options
// =====================================================================

std::optional<std::string> OptionString(const OptionsMap& options,
                                        const std::string& key) {
    auto it = options.find(key);
    if (it == options.end()) return std::nullopt;
    if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    return std::nullopt;
}

std::optional<double> OptionNumber(const OptionsMap& options,
                                   const std::string& key) {
    auto it = options.find(key);
    if (it == options.end()) return std::nullopt;
    if (const auto* d = std::get_if<double>(&it->second)) return *d;
    if (const auto* i = std::get_if<int64_t>(&it->second)) {
        return static_cast<double>(*i);
    }
    return std::nullopt;
}

size_t CountCodePoints(const std::string& s) {
    size_t n = 0;
    for (const unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

void AddUsage(TokenUsage& into, const TokenUsage& from) {
    into.inputTokens       += from.inputTokens;
    into.outputTokens      += from.outputTokens;
    into.cachedInputTokens += from.cachedInputTokens;
    into.reasoningTokens   += from.reasoningTokens;
}

// =====================================================================
// The translator
// =====================================================================

class TextLLMTranslator : public ITranslator {
public:
    TextLLMTranslator(std::unique_ptr<ITextLLM> llm, TranslatorConfig config)
        : llm_(std::move(llm)), config_(std::move(config)) {
        if (const auto n = OptionNumber(config_.providerOptions, "textllm.batchSize");
            n && *n >= 1.0) {
            batchSize_ = static_cast<size_t>(*n);
        }
        if (const auto t = OptionNumber(config_.providerOptions, "textllm.temperature")) {
            temperature_ = *t;
        }
        if (const auto m = OptionNumber(config_.providerOptions, "textllm.maxOutputTokens");
            m && *m >= 1.0) {
            maxOutputTokens_ = static_cast<int32_t>(*m);
        }
    }

    TranslatorProviderCapabilities GetCapabilities() const override {
        const ProviderCapabilities llmCaps = llm_->GetCapabilities();
        TranslatorProviderCapabilities caps;
        caps.providerId = config_.providerId.empty() ? llmCaps.providerId
                                                     : config_.providerId;
        caps.maxBatchSize = 0;                    // batches are split internally
        for (const ModelInfo& m : llmCaps.models) {
            TranslatorModelInfo info;
            info.id = m.id;
            info.displayName = m.displayName;
            info.supportsAutoDetect = true;
            info.supportsBatching = true;
            info.supportsFormality = true;
            info.runsLocally = m.runsLocally;
            caps.models.push_back(std::move(info));
        }
        return caps;
    }

    TranslateResponse Translate(const TranslateRequest& request) override {
        TranslateResponse resp;
        resp.model = request.model.empty() ? config_.defaultModel : request.model;
        if (request.targetLanguage.empty()) {
            resp.error.code = ErrorCode::InvalidRequest;
            resp.error.message = "TranslateRequest::targetLanguage is required";
            return resp;
        }
        resp.results.reserve(request.texts.size());
        for (size_t first = 0; first < request.texts.size(); first += batchSize_) {
            const size_t count = std::min(batchSize_, request.texts.size() - first);
            ChatRequest chat = MakeChat(request.model, request.options);
            chat.messages[0].text = BuildTranslationPrompt(request, first, count);
            chat.messages[1].text = SegmentsJson(request.texts, first, count,
                                                 "\"target\":" + Quoted(request.targetLanguage));
            ChatResponse reply = llm_->Chat(chat);
            AddUsage(resp.usage, reply.usage);
            if (!reply.model.empty()) resp.model = reply.model;
            if (!reply.error.IsOk()) { resp.error = reply.error; return resp; }
            if (!CheckFinish(reply, resp.error)) return resp;

            std::vector<TranslateResult> results;
            if (!ParseTranslationReply(reply.text, count, results, resp.error)) {
                return resp;
            }
            for (TranslateResult& r : results) {
                if (r.detectedSourceLanguage.empty()) {
                    r.detectedSourceLanguage = request.sourceLanguage;
                }
                resp.results.push_back(std::move(r));
            }
        }
        size_t characters = 0;
        for (const std::string& t : request.texts) characters += CountCodePoints(t);
        resp.usage.units = static_cast<int32_t>(characters);
        return resp;
    }

    std::future<TranslateResponse> TranslateAsync(
        const TranslateRequest& request) override {
        return std::async(std::launch::async,
                          [this, request]() { return Translate(request); });
    }

    DetectLanguageResponse DetectLanguage(
        const DetectLanguageRequest& request) override {
        DetectLanguageResponse resp;
        const int32_t topN = std::max<int32_t>(1, request.topN);
        resp.guesses.reserve(request.texts.size());
        for (size_t first = 0; first < request.texts.size(); first += batchSize_) {
            const size_t count = std::min(batchSize_, request.texts.size() - first);
            ChatRequest chat = MakeChat("", request.options);
            chat.messages[0].text = BuildDetectionPrompt(request, first, count);
            chat.messages[1].text = SegmentsJson(request.texts, first, count, "");
            ChatResponse reply = llm_->Chat(chat);
            if (!reply.error.IsOk()) { resp.error = reply.error; return resp; }
            if (!CheckFinish(reply, resp.error)) return resp;
            std::vector<std::vector<LanguageGuess>> guesses;
            if (!ParseDetectionReply(reply.text, count, topN, guesses, resp.error)) {
                return resp;
            }
            for (auto& g : guesses) resp.guesses.push_back(std::move(g));
        }
        return resp;
    }

    void* RawProvider() override { return llm_.get(); }

private:
    std::unique_ptr<ITextLLM> llm_;
    TranslatorConfig config_;
    size_t batchSize_ = 20;
    double temperature_ = 0.0;
    std::optional<int32_t> maxOutputTokens_;

    static std::string Quoted(const std::string& s) {
        std::string out;
        AppendJsonString(out, s);
        return out;
    }

    static std::string SegmentsJson(const std::vector<std::string>& texts,
                                    size_t first, size_t count,
                                    const std::string& leadingFields) {
        std::string out = "{";
        if (!leadingFields.empty()) out += leadingFields + ",";
        AppendSegments(out, texts, first, count);
        out += '}';
        return out;
    }

    ChatRequest MakeChat(const std::string& model, const OptionsMap& options) const {
        ChatRequest chat;
        chat.model = model;
        chat.responseFormat = ResponseFormat::JsonObject;
        chat.sampling.temperature = temperature_;
        if (maxOutputTokens_) chat.sampling.maxOutputTokens = *maxOutputTokens_;
        // The caller's per-request options reach the LLM adapter, minus the
        // translator's own keys.
        for (const auto& kv : options) {
            if (kv.first.rfind("textllm.", 0) == 0) continue;
            chat.options[kv.first] = kv.second;
        }
        Message system; system.role = Role::System;
        Message user;   user.role = Role::User;
        chat.messages.push_back(std::move(system));
        chat.messages.push_back(std::move(user));
        return chat;
    }

    static bool CheckFinish(const ChatResponse& reply, Error& error) {
        if (reply.finishReason == FinishReason::Length) {
            error.code = ErrorCode::ContextLengthExceeded;
            error.message = "The model's reply was cut off; translate fewer "
                            "texts per batch (textllm.batchSize) or raise "
                            "textllm.maxOutputTokens";
            return false;
        }
        if (reply.finishReason == FinishReason::ContentFiltered) {
            error.code = ErrorCode::ContentFiltered;
            error.message = "The model declined to translate these texts";
            return false;
        }
        return true;
    }
};

} // namespace

// =====================================================================
// Prompts and reply parsers (public, see the header)
// =====================================================================

std::string BuildTranslationPrompt(const TranslateRequest& request,
                                   size_t /*first*/, size_t count) {
    std::ostringstream p;
    p << "You are a professional translator. The user message is a JSON "
         "object with a \"target\" language (BCP-47) and " << count
      << " \"segments\", each with an \"id\" and a \"text\". Translate the "
         "text of every segment into the target language.\n"
         "Reply with one JSON object and nothing else, in this shape:\n"
         "{\"segments\":[{\"id\":1,\"text\":\"<translation>\",\"source\":\"<BCP-47 "
         "of the segment's source language>\"}]}\n"
         "Rules:\n"
         "- One entry per input segment, with the same id; keep every "
         "segment, including empty ones.\n"
         "- Translate faithfully and naturally; add no commentary, notes "
         "or quotation marks of your own.\n"
         "- Leave code, URLs, e-mail addresses, numbers, product names and "
         "placeholders such as {0}, %s or {{name}} exactly as they are.\n";
    if (request.preserveFormatting) {
        p << "- Keep the markup of each segment (HTML tags and attributes, "
             "Markdown, line breaks, leading and trailing whitespace); "
             "translate only the human-readable text.\n";
    }
    if (!request.sourceLanguage.empty()) {
        p << "- The source language is \"" << request.sourceLanguage
          << "\"; report it as \"source\".\n";
    } else {
        p << "- Detect each segment's source language and report it as "
             "\"source\".\n";
    }
    switch (request.formality) {
        case TranslationFormality::Formal:
            p << "- Use a formal register (polite forms of address).\n";
            break;
        case TranslationFormality::Informal:
            p << "- Use an informal register (familiar forms of address).\n";
            break;
        case TranslationFormality::Default:
            break;
    }
    if (!request.domain.empty() && request.domain != "general") {
        p << "- The texts belong to the \"" << request.domain
          << "\" domain; use its established terminology.\n";
    }
    if (const auto glossary = OptionString(request.options, "textllm.glossary");
        glossary && !glossary->empty()) {
        p << "- Use these glossary entries (source = translation), one per "
             "line:\n" << *glossary << "\n";
    }
    return p.str();
}

bool ParseTranslationReply(const std::string& reply, size_t count,
                           std::vector<TranslateResult>& results,
                           Error& error) {
    results.clear();
    JsonValue root;
    const JsonValue* segments = nullptr;
    Error parseError;
    if (!ParseReplyRoot(reply, root, segments, parseError)) {
        // A model that answered a single segment with the bare translation
        // still answered it.
        if (count == 1 && reply.find('{') == std::string::npos) {
            TranslateResult r;
            r.text = StripCodeFence(reply);
            results.push_back(std::move(r));
            return true;
        }
        error = parseError;
        return false;
    }
    std::vector<const JsonValue*> slots;
    if (!PlaceItems(*segments, count, slots, error)) return false;
    results.reserve(count);
    for (const JsonValue* item : slots) {
        TranslateResult r;
        r.text = StringField(*item, {"text", "translation", "translated"});
        r.detectedSourceLanguage =
            StringField(*item, {"source", "sourceLanguage", "detectedSourceLanguage",
                                "language"});
        r.confidence = std::clamp(NumberField(*item, "confidence"), 0.0, 1.0);
        results.push_back(std::move(r));
    }
    return true;
}

std::string BuildDetectionPrompt(const DetectLanguageRequest& request,
                                 size_t /*first*/, size_t count) {
    const int32_t topN = std::max<int32_t>(1, request.topN);
    std::ostringstream p;
    p << "Identify the language of each of the " << count
      << " \"segments\" in the user's JSON object (each has an \"id\" and a "
         "\"text\").\n"
         "Reply with one JSON object and nothing else, in this shape:\n"
         "{\"segments\":[{\"id\":1,\"guesses\":[{\"language\":\"<BCP-47>\","
         "\"confidence\":<0 to 1>}]}]}\n"
         "Rules:\n"
         "- One entry per input segment, with the same id.\n"
         "- Give up to " << topN << " guess" << (topN == 1 ? "" : "es")
      << " per segment, most likely first, with language codes such as "
         "\"en\", \"de\", \"pt-BR\" or \"zh-Hant\".\n"
         "- A segment with no recognisable language gets the code \"und\".\n";
    return p.str();
}

bool ParseDetectionReply(const std::string& reply, size_t count, int32_t topN,
                         std::vector<std::vector<LanguageGuess>>& guesses,
                         Error& error) {
    guesses.clear();
    JsonValue root;
    const JsonValue* segments = nullptr;
    if (!ParseReplyRoot(reply, root, segments, error)) return false;
    std::vector<const JsonValue*> slots;
    if (!PlaceItems(*segments, count, slots, error)) return false;
    guesses.reserve(count);
    const size_t keep = static_cast<size_t>(std::max<int32_t>(1, topN));
    for (const JsonValue* item : slots) {
        std::vector<LanguageGuess> list;
        const JsonValue* array = item->Get("guesses");
        if (!array) array = item->Get("languages");
        if (array && array->kind == JsonValue::Kind::Array) {
            for (const JsonValue& g : array->array) {
                if (list.size() >= keep) break;
                LanguageGuess guess;
                if (g.kind == JsonValue::Kind::String) {
                    guess.language = g.string;
                } else {
                    guess.language = StringField(g, {"language", "code", "lang"});
                    guess.confidence = std::clamp(NumberField(g, "confidence"), 0.0, 1.0);
                }
                if (!guess.language.empty()) list.push_back(std::move(guess));
            }
        }
        if (list.empty()) {
            // The model answered with a single language on the segment.
            LanguageGuess guess;
            guess.language = StringField(*item, {"language", "code", "lang", "source"});
            guess.confidence = std::clamp(NumberField(*item, "confidence"), 0.0, 1.0);
            if (guess.language.empty()) {
                error.code = ErrorCode::ProviderError;
                error.message = "The model named no language for segment " +
                                std::to_string(guesses.size() + 1);
                return false;
            }
            list.push_back(std::move(guess));
        }
        guesses.push_back(std::move(list));
    }
    return true;
}

// =====================================================================
// Factories
// =====================================================================

std::unique_ptr<ITranslator> CreateTextLLMTranslator(
    std::unique_ptr<ITextLLM> llm, const TranslatorConfig& config,
    Error* outError) {
    if (!llm) {
        if (outError) {
            outError->code = ErrorCode::InvalidRequest;
            outError->message = "CreateTextLLMTranslator needs a text LLM";
        }
        return nullptr;
    }
    return std::make_unique<TextLLMTranslator>(std::move(llm), config);
}

std::unique_ptr<ITranslator> CreateTextLLMTranslator(
    const TranslatorConfig& config, Error* outError) {
    TextLLMConfig llmConfig = config;
    if (const auto provider = OptionString(config.providerOptions, "textllm.provider")) {
        llmConfig.providerId = *provider;
    }
    std::unique_ptr<ITextLLM> llm = CreateTextLLM(llmConfig, outError);
    if (!llm) return nullptr;
    return std::make_unique<TextLLMTranslator>(std::move(llm), config);
}

} // namespace UltraAI
