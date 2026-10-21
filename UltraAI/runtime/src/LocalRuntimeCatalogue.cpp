// UltraAI/runtime/src/LocalRuntimeCatalogue.cpp
// The gallery index and the model catalogue: JSON in, structs out, and the
// Hugging Face helpers. Formats: Docs/Modules/UltraAI/LocalRuntime.md.
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module

#include "LocalRuntimeInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace UltraAI {
namespace local_detail {

using nlohmann::json;

namespace {

std::string Str(const json& j, const char* key, const std::string& fallback = {}) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

uint64_t U64(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return 0;
    if (it->is_number_unsigned()) return it->get<uint64_t>();
    const double d = it->get<double>();
    return d > 0 ? static_cast<uint64_t>(d) : 0;
}

int32_t I32(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return 0;
    return it->get<int32_t>();
}

bool Bool(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

std::vector<std::string> Strings(const json& j, const char* key) {
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& e : *it) {
        if (e.is_string()) out.push_back(e.get<std::string>());
    }
    return out;
}

bool ParseModel(const json& m, LocalModelInfo& out, std::string& outError) {
    out = LocalModelInfo{};
    out.id = Str(m, "id");
    if (out.id.empty()) { outError = "a model entry has no \"id\""; return false; }
    out.displayName = Str(m, "displayName", out.id);
    out.description = Str(m, "description");
    out.capability  = Str(m, "capability");
    if (out.capability.empty()) {
        outError = "model \"" + out.id + "\" has no \"capability\"";
        return false;
    }
    out.engineId = Str(m, "engine");
    if (out.engineId.empty()) {
        outError = "model \"" + out.id + "\" has no \"engine\"";
        return false;
    }
    out.license = Str(m, "license");
    const std::string source = Str(m, "source", "catalogue");
    out.source = source == "file"        ? LocalModelSource::File
               : source == "huggingface" ? LocalModelSource::HuggingFace
                                         : LocalModelSource::Catalogue;
    auto files = m.find("files");
    if (files != m.end() && files->is_array()) {
        for (const auto& f : *files) {
            LocalModelFile file;
            file.name      = Str(f, "name");
            file.url       = Str(f, "url");
            file.sha256    = Str(f, "sha256");
            file.sizeBytes = U64(f, "size");
            if (file.name.empty()) {
                outError = "model \"" + out.id + "\" has a file without a \"name\"";
                return false;
            }
            std::transform(file.sha256.begin(), file.sha256.end(), file.sha256.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            out.sizeBytes += file.sizeBytes;
            out.files.push_back(std::move(file));
        }
    }
    auto req = m.find("requires");
    if (req != m.end() && req->is_object()) {
        out.memoryBytes    = U64(*req, "memoryBytes");
        out.gpuMemoryBytes = U64(*req, "gpuMemoryBytes");
    }
    auto feat = m.find("features");
    if (feat != m.end() && feat->is_object()) {
        out.contextWindow       = I32(*feat, "contextWindow");
        out.embeddingDimensions = I32(*feat, "embeddingDimensions");
        out.supportsTools       = Bool(*feat, "tools");
        out.supportsVision      = Bool(*feat, "vision");
    }
    out.engineArgs    = Strings(m, "engineArgs");
    out.tags          = Strings(m, "tags");
    out.installedPath = Str(m, "installedPath");
    out.primaryFile   = Str(m, "primaryFile");
    return true;
}

json ModelToJson(const LocalModelInfo& m) {
    json j;
    j["id"]          = m.id;
    j["displayName"] = m.displayName;
    if (!m.description.empty()) j["description"] = m.description;
    j["capability"]  = m.capability;
    j["engine"]      = m.engineId;
    if (!m.license.empty()) j["license"] = m.license;
    j["source"] = m.source == LocalModelSource::File        ? "file"
                : m.source == LocalModelSource::HuggingFace ? "huggingface"
                                                            : "catalogue";
    json files = json::array();
    for (const auto& f : m.files) {
        json fj;
        fj["name"] = f.name;
        if (!f.url.empty()) fj["url"] = f.url;
        if (!f.sha256.empty()) fj["sha256"] = f.sha256;
        if (f.sizeBytes) fj["size"] = f.sizeBytes;
        files.push_back(std::move(fj));
    }
    j["files"] = std::move(files);
    if (m.memoryBytes || m.gpuMemoryBytes) {
        json req;
        if (m.memoryBytes) req["memoryBytes"] = m.memoryBytes;
        if (m.gpuMemoryBytes) req["gpuMemoryBytes"] = m.gpuMemoryBytes;
        j["requires"] = std::move(req);
    }
    json feat;
    if (m.contextWindow) feat["contextWindow"] = m.contextWindow;
    if (m.embeddingDimensions) feat["embeddingDimensions"] = m.embeddingDimensions;
    if (m.supportsTools) feat["tools"] = true;
    if (m.supportsVision) feat["vision"] = true;
    if (!feat.empty()) j["features"] = std::move(feat);
    if (!m.engineArgs.empty()) j["engineArgs"] = m.engineArgs;
    if (!m.tags.empty()) j["tags"] = m.tags;
    if (!m.installedPath.empty()) j["installedPath"] = m.installedPath;
    if (!m.primaryFile.empty()) j["primaryFile"] = m.primaryFile;
    return j;
}

} // namespace

const EngineInfo* Gallery::Find(const std::string& engineId) const {
    for (const auto& e : engines) {
        if (e.id == engineId) return &e;
    }
    return nullptr;
}

const LocalModelInfo* Catalogue::Find(const std::string& modelId) const {
    for (const auto& m : models) {
        if (m.id == modelId) return &m;
    }
    return nullptr;
}

bool ParseGallery(const std::string& text, Gallery& out, std::string& outError) {
    out = Gallery{};
    json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) { outError = "the gallery index is not a JSON object"; return false; }
    out.formatVersion = I32(doc, "formatVersion");
    if (out.formatVersion != 1) {
        outError = "the gallery index has formatVersion " + std::to_string(out.formatVersion) +
                   "; this runtime reads 1";
        return false;
    }
    out.published = Str(doc, "published");
    auto engines = doc.find("engines");
    if (engines == doc.end() || !engines->is_array()) {
        outError = "the gallery index has no \"engines\" list";
        return false;
    }
    for (const auto& e : *engines) {
        EngineInfo engine;
        engine.id = Str(e, "id");
        if (engine.id.empty()) { outError = "an engine entry has no \"id\""; return false; }
        engine.version     = Str(e, "version");
        engine.license     = Str(e, "license");
        engine.executable  = Str(e, "executable", engine.id);
        engine.serves      = Strings(e, "serves");
        engine.defaultArgs = Strings(e, "defaultArgs");
        auto bundles = e.find("bundles");
        if (bundles != e.end() && bundles->is_array()) {
            for (const auto& b : *bundles) {
                EngineBundleInfo bundle;
                bundle.engineId    = engine.id;
                bundle.version     = Str(b, "version", engine.version);
                bundle.platform    = Str(b, "platform");
                bundle.accelerator = Str(b, "accel", "cpu");
                bundle.url         = Str(b, "url");
                bundle.sha256      = Str(b, "sha256");
                bundle.sizeBytes   = U64(b, "size");
                bundle.optional    = Bool(b, "optional");
                bundle.minOs       = Str(b, "minOs");
                if (bundle.platform.empty() || bundle.url.empty()) {
                    outError = "engine \"" + engine.id + "\" has a bundle without platform or url";
                    return false;
                }
                std::transform(bundle.sha256.begin(), bundle.sha256.end(), bundle.sha256.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                auto req = b.find("requires");
                if (req != b.end() && req->is_object()) {
                    for (auto it = req->begin(); it != req->end(); ++it) {
                        if (it.value().is_string()) {
                            bundle.requirements[it.key()] = it.value().get<std::string>();
                        }
                    }
                }
                engine.bundles.push_back(std::move(bundle));
            }
        }
        out.engines.push_back(std::move(engine));
    }
    return true;
}

bool ParseCatalogue(const std::string& text, Catalogue& out, std::string& outError) {
    out = Catalogue{};
    json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) { outError = "the catalogue is not a JSON object"; return false; }
    out.formatVersion = I32(doc, "formatVersion");
    if (out.formatVersion != 1) {
        outError = "the catalogue has formatVersion " + std::to_string(out.formatVersion) +
                   "; this runtime reads 1";
        return false;
    }
    out.published = Str(doc, "published");
    auto models = doc.find("models");
    if (models == doc.end() || !models->is_array()) {
        outError = "the catalogue has no \"models\" list";
        return false;
    }
    for (const auto& m : *models) {
        LocalModelInfo model;
        if (!ParseModel(m, model, outError)) return false;
        out.models.push_back(std::move(model));
    }
    return true;
}

std::string SerializeModels(const std::vector<LocalModelInfo>& models) {
    json doc;
    doc["formatVersion"] = 1;
    json list = json::array();
    for (const auto& m : models) list.push_back(ModelToJson(m));
    doc["models"] = std::move(list);
    return doc.dump(2);
}

bool ParseModels(const std::string& text, std::vector<LocalModelInfo>& out, std::string& outError) {
    out.clear();
    json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) { outError = "not a JSON object"; return false; }
    auto models = doc.find("models");
    if (models == doc.end() || !models->is_array()) { outError = "no \"models\" list"; return false; }
    for (const auto& m : *models) {
        LocalModelInfo model;
        if (!ParseModel(m, model, outError)) return false;
        out.push_back(std::move(model));
    }
    return true;
}

std::string HuggingFaceFileUrl(const std::string& repo, const std::string& revision,
                               const std::string& file) {
    return "https://huggingface.co/" + repo + "/resolve/" +
           (revision.empty() ? std::string("main") : revision) + "/" + file;
}

std::string HuggingFaceTreeUrl(const std::string& repo, const std::string& revision) {
    return "https://huggingface.co/api/models/" + repo + "/tree/" +
           (revision.empty() ? std::string("main") : revision);
}

bool ParseHuggingFaceTree(const std::string& text, const std::string& file,
                          uint64_t& outSize, std::string& outSha256) {
    json doc = json::parse(text, nullptr, false);
    if (!doc.is_array()) return false;
    for (const auto& entry : doc) {
        if (!entry.is_object() || Str(entry, "path") != file) continue;
        outSize = U64(entry, "size");
        outSha256.clear();
        auto lfs = entry.find("lfs");
        if (lfs != entry.end() && lfs->is_object()) {
            outSha256 = Str(*lfs, "oid");
            if (const uint64_t lfsSize = U64(*lfs, "size")) outSize = lfsSize;
        }
        std::transform(outSha256.begin(), outSha256.end(), outSha256.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return true;
    }
    return false;
}

std::string SerializeSettings(const LocalRuntimeSettings& s) {
    json j;
    j["policy"]               = ToString(s.policy);
    j["preferredAccelerator"] = s.preferredAccelerator;
    j["galleryUrl"]           = s.galleryUrl;
    j["idleMinutes"]          = s.idleMinutes;
    j["diskCapBytes"]         = s.diskCapBytes;
    j["startTimeoutSeconds"]  = s.startTimeoutSeconds;
    return j.dump(2);
}

bool ParseSettings(const std::string& text, LocalRuntimeSettings& out) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return false;
    LocalRuntimeSettings s;
    DownloadPolicy policy;
    if (ParseDownloadPolicy(Str(j, "policy"), policy)) s.policy = policy;
    s.preferredAccelerator = Str(j, "preferredAccelerator");
    s.galleryUrl           = Str(j, "galleryUrl");
    if (j.contains("idleMinutes")) s.idleMinutes = I32(j, "idleMinutes");
    if (j.contains("diskCapBytes")) s.diskCapBytes = U64(j, "diskCapBytes");
    if (j.contains("startTimeoutSeconds")) s.startTimeoutSeconds = I32(j, "startTimeoutSeconds");
    out = s;
    return true;
}

} // namespace local_detail

const char* ToString(DownloadPolicy policy) {
    switch (policy) {
        case DownloadPolicy::Never:     return "never";
        case DownloadPolicy::Ask:       return "ask";
        case DownloadPolicy::Automatic: return "automatic";
    }
    return "ask";
}

bool ParseDownloadPolicy(const std::string& text, DownloadPolicy& out) {
    if (text == "never")     { out = DownloadPolicy::Never;     return true; }
    if (text == "ask")       { out = DownloadPolicy::Ask;       return true; }
    if (text == "automatic") { out = DownloadPolicy::Automatic; return true; }
    return false;
}

const char* ToString(ModelFit fit) {
    switch (fit) {
        case ModelFit::Recommended: return "recommended";
        case ModelFit::RunsOnCpu:   return "runs-on-cpu";
        case ModelFit::TooLarge:    return "too-large";
        case ModelFit::Unknown:     return "unknown";
    }
    return "unknown";
}

const char* ToString(InstallProgress::State state) {
    switch (state) {
        case InstallProgress::State::Queued:      return "queued";
        case InstallProgress::State::Downloading: return "downloading";
        case InstallProgress::State::Verifying:   return "verifying";
        case InstallProgress::State::Unpacking:   return "unpacking";
        case InstallProgress::State::Done:        return "done";
        case InstallProgress::State::Failed:      return "failed";
        case InstallProgress::State::Cancelled:   return "cancelled";
    }
    return "queued";
}

} // namespace UltraAI
