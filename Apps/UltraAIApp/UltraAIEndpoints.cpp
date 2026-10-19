// Apps/UltraAIApp/UltraAIEndpoints.cpp
// Version: 0.1.1 - the config folder comes from the framework's settings root
//                  (UltraCanvasSettingsFolderRoot), every variable read as UTF-8
// Version: 0.1.0

#include "UltraAIEndpoints.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"   // UltraCanvasSettingsFolderRoot

#include <UltraNet/UltraNetCore.h>       // UltraNetResult, UltraNet init
#include <UltraNet/UltraNetHttp.h>       // UltraNet_HttpRequest, request/response

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

// UltraAI under the framework's settings root (UltraCanvasSettingsFolderRoot:
// %APPDATA%, Application Support, $XDG_CONFIG_HOME or ~/.config), every
// variable read as UTF-8 since the JSON file helpers open the file with
// OpenFileUtf8 and the narrow getenv answers in the ANSI code page, which
// cannot spell a profile folder named in Thai under code page 1252. "UltraAI"
// in the working directory when the environment names no home.
std::string EndpointStore::ConfigDir() {
    const std::filesystem::path root = UltraCanvas::UltraCanvasSettingsFolderRoot();
    if (root.empty()) return "UltraAI";
    return UltraCanvas::PathToUtf8(root / "UltraAI");
}

std::string EndpointStore::ConfigPath() {
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(ConfigDir()) / "endpoints.json");
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
    if (!std::filesystem::exists(UltraCanvas::PathFromUtf8(ConfigPath()), ec)) {
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

        // Models cached from a previous /v1/model/info fetch (optional).
        const JSONValue& models = obj.Get("models");
        for (size_t mi = 0; mi < models.GetSize(); ++mi) {
            const JSONValue& mo = models.At(mi);
            if (!mo.IsObject()) continue;
            Endpoint::DiscoveredModel m;
            m.name = mo.Get("name").GetString();
            m.task = mo.Get("task").GetString();
            m.mode = mo.Get("mode").GetString();
            const JSONValue& langs = mo.Get("supported_languages");
            for (const auto& member : langs.GetMembers()) {
                m.supportedLanguages.emplace_back(member.first,
                                                  member.second.GetString());
            }
            if (!m.name.empty()) e.models.push_back(std::move(m));
        }

        if (e.id.empty()) e.id = MakeId(e.name.empty() ? e.providerId : e.name);
        endpoints_.push_back(std::move(e));
    }
    return true;
}

bool EndpointStore::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(ConfigDir()), ec);

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

        // Cache discovered models (and their translation languages) only when
        // present, so endpoints that were never fetched stay compact.
        if (!e.models.empty()) {
            JSONValue models = JSONValue::MakeArray();
            for (const auto& m : e.models) {
                JSONValue mo = JSONValue::MakeObject();
                mo.Set("name", m.name);
                if (!m.task.empty()) mo.Set("task", m.task);
                if (!m.mode.empty()) mo.Set("mode", m.mode);
                if (!m.supportedLanguages.empty()) {
                    JSONValue langs = JSONValue::MakeObject();
                    for (const auto& [code, lname] : m.supportedLanguages) {
                        langs.Set(code, lname);
                    }
                    mo.Set("supported_languages", std::move(langs));
                }
                models.Append(std::move(mo));
            }
            obj.Set("models", std::move(models));
        }
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

ModelInfoFetch FetchModelInfo(const std::string& baseUrl,
                              const std::string& apiKey, int timeoutMs) {
    ModelInfoFetch out;

    std::string base = baseUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) {
        out.error = "Set a Base URL first (e.g. https://ai.ultraos.eu).";
        return out;
    }

    // Same wire shape the adapters use: a GET with the key as a Bearer token.
    UltraNetHttpRequest req;
    req.url    = base + "/v1/model/info";
    req.method = UltraNetHttpMethod::Get;
    if (!apiKey.empty()) req.headers.Add("Authorization", "Bearer " + apiKey);
    req.options.timeoutMs = timeoutMs;

    UltraNetResponse resp;
    UltraNetResult result = UltraNet_HttpRequest(req, resp);

    // A completed HTTP exchange (even a 4xx/5xx) returns true or HttpError; a
    // real transport failure (DNS, TLS, timeout) is anything else.
    if (!result && result.code != UltraNetResultCode::HttpError) {
        out.error = result.message.empty()
            ? "Could not reach " + req.url : result.message;
        return out;
    }
    if (resp.statusCode >= 400) {
        const bool auth = resp.statusCode == 401 || resp.statusCode == 403;
        out.error = "Server returned HTTP " + std::to_string(resp.statusCode) +
                    (auth ? " — check the API key." : ".");
        return out;
    }

    UltraCanvas::JSONParseResult pr;
    JSONValue root = JSON::Parse(resp.GetBodyAsString(), &pr);
    if (!pr.success) {
        out.error = "Could not parse the server response.";
        return out;
    }
    const JSONValue& data = root.Get("data");
    if (!data.IsArray()) {
        out.error = "Unexpected response (no \"data\" array).";
        return out;
    }

    for (size_t i = 0; i < data.GetSize(); ++i) {
        const JSONValue& obj = data.At(i);
        if (!obj.IsObject()) continue;
        Endpoint::DiscoveredModel m;
        m.name = obj.Get("model_name").GetString();
        const JSONValue& mi = obj.Get("model_info");
        m.task = mi.Get("task").GetString();
        m.mode = mi.Get("mode").GetString();
        const JSONValue& langs = mi.Get("supported_languages");
        for (const auto& member : langs.GetMembers()) {
            m.supportedLanguages.emplace_back(member.first,
                                              member.second.GetString());
        }
        if (!m.name.empty()) out.models.push_back(std::move(m));
    }
    out.ok = true;
    return out;
}

} // namespace UltraAIApp
