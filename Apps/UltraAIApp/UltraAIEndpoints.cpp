// Apps/UltraAIApp/UltraAIEndpoints.cpp
// Version: 0.1.0

#include "UltraAIEndpoints.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace UltraAIApp {

using UltraCanvas::JSONValue;
namespace JSON = UltraCanvas::JSON;

const std::vector<CapabilityInfo>& AllCapabilities() {
    static const std::vector<CapabilityInfo> kAll = {
        {AICapability::Chat,         "chat",        "Chat (LLM)"},
        {AICapability::Embeddings,   "embeddings",  "Embeddings"},
        {AICapability::SpeechToText, "stt",         "Speech → Text"},
        {AICapability::TextToSpeech, "tts",         "Text → Speech"},
        {AICapability::Translation,  "translation", "Translation"},
        {AICapability::ImageGen,     "imagegen",    "Image Generation"},
        {AICapability::Vision,       "vision",      "Vision Analysis"},
        {AICapability::VideoGen,     "videogen",    "Video Generation"},
        {AICapability::MusicGen,     "musicgen",    "Music Generation"},
        {AICapability::CodeAssist,   "codeassist",  "Code Assist"},
    };
    return kAll;
}

const char* CapabilityId(AICapability cap) {
    for (const auto& info : AllCapabilities()) {
        if (info.cap == cap) return info.id;
    }
    return "";
}

bool ParseCapability(const std::string& id, AICapability& out) {
    for (const auto& info : AllCapabilities()) {
        if (id == info.id) { out = info.cap; return true; }
    }
    return false;
}

// ===== EndpointStore =====

EndpointStore& EndpointStore::Instance() {
    static EndpointStore instance;
    return instance;
}

std::string EndpointStore::ConfigDir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::string(appdata) + "\\UltraAI";
    return "UltraAI";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/Library/Application Support/UltraAI";
    return "UltraAI";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string(xdg) + "/UltraAI";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.config/UltraAI";
    return "UltraAI";
#endif
}

std::string EndpointStore::ConfigPath() {
#if defined(_WIN32)
    return ConfigDir() + "\\endpoints.json";
#else
    return ConfigDir() + "/endpoints.json";
#endif
}

std::vector<Endpoint> EndpointStore::ForCapability(AICapability c) const {
    std::vector<Endpoint> out;
    for (const auto& e : endpoints_) {
        if (e.Supports(c)) out.push_back(e);
    }
    return out;
}

const Endpoint* EndpointStore::FindById(const std::string& id) const {
    for (const auto& e : endpoints_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void EndpointStore::Upsert(const Endpoint& e) {
    for (auto& existing : endpoints_) {
        if (existing.id == e.id) { existing = e; return; }
    }
    endpoints_.push_back(e);
}

void EndpointStore::Remove(const std::string& id) {
    endpoints_.erase(
        std::remove_if(endpoints_.begin(), endpoints_.end(),
                       [&](const Endpoint& e) { return e.id == id; }),
        endpoints_.end());
}

std::string EndpointStore::MakeId(const std::string& name) const {
    std::string slug;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!slug.empty() && slug.back() != '-') {
            slug.push_back('-');
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "endpoint";

    // Ensure uniqueness against existing ids.
    std::string candidate = slug;
    int n = 2;
    while (FindById(candidate) != nullptr) {
        candidate = slug + "-" + std::to_string(n++);
    }
    return candidate;
}

bool EndpointStore::Load() {
    endpoints_.clear();

    std::error_code ec;
    if (!std::filesystem::exists(ConfigPath(), ec)) {
        return true;  // no file yet — an empty store is valid
    }

    UltraCanvas::JSONParseResult res;
    JSONValue root = JSON::ParseFile(ConfigPath(), &res);
    if (!res.success) return false;

    const JSONValue& arr = root.IsArray() ? root : root.Get("endpoints");
    if (!arr.IsArray()) return true;

    for (size_t i = 0; i < arr.GetSize(); ++i) {
        const JSONValue& obj = arr.At(i);
        if (!obj.IsObject()) continue;

        Endpoint e;
        e.id           = obj.Get("id").GetString();
        e.name         = obj.Get("name").GetString();
        e.providerId   = obj.Get("provider").GetString();
        e.baseUrl      = obj.Get("baseUrl").GetString();
        e.defaultModel = obj.Get("model").GetString();

        const JSONValue& modes = obj.Get("modes");
        for (size_t m = 0; m < modes.GetSize(); ++m) {
            AICapability cap;
            if (ParseCapability(modes.At(m).GetString(), cap)) e.modes.insert(cap);
        }

        if (e.id.empty()) e.id = MakeId(e.name.empty() ? e.providerId : e.name);
        endpoints_.push_back(std::move(e));
    }
    return true;
}

bool EndpointStore::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(ConfigDir(), ec);

    JSONValue arr = JSONValue::MakeArray();
    for (const auto& e : endpoints_) {
        JSONValue obj = JSONValue::MakeObject();
        obj.Set("id", e.id);
        obj.Set("name", e.name);
        obj.Set("provider", e.providerId);
        obj.Set("baseUrl", e.baseUrl);
        obj.Set("model", e.defaultModel);

        JSONValue modes = JSONValue::MakeArray();
        // Emit in canonical display order for stable, diff-friendly files.
        for (const auto& info : AllCapabilities()) {
            if (e.modes.count(info.cap)) modes.Append(std::string(info.id));
        }
        obj.Set("modes", std::move(modes));
        arr.Append(std::move(obj));
    }

    UltraCanvas::JSONSerializeOptions opts;
    opts.pretty = true;
    return JSON::SerializeToFile(ConfigPath(), arr, opts);
}

// ===== conversion =====

UltraAI::TextLLMConfig ToTextLLMConfig(const Endpoint& e) {
    UltraAI::TextLLMConfig cfg;
    cfg.providerId     = e.providerId;
    cfg.baseUrl        = e.baseUrl;
    cfg.defaultModel   = e.defaultModel;
    cfg.apiKeyVaultRef = e.VaultRef();
    return cfg;
}

} // namespace UltraAIApp
