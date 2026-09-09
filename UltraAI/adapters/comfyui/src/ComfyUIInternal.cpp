// UltraAI/adapters/comfyui/src/ComfyUIInternal.cpp
// Implementation of the ComfyUI adapter's shared pieces.
// Version: 0.2.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "ComfyUIInternal.h"

#include "ComfyUIWorkflows.h"

#include <cctype>
#include <sstream>

namespace UltraAI {
namespace comfyui_detail {

std::string UrlEncode(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool unreserved = std::isalnum(c) || c == '-' || c == '_' ||
                                c == '.' || c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

std::string ViewUrl(const RunContext& ctx, const OutputRef& ref) {
    std::string url = ctx.baseUrl + "/view?filename=" + UrlEncode(ref.filename);
    if (!ref.subfolder.empty()) {
        url += "&subfolder=" + UrlEncode(ref.subfolder);
    }
    url += "&type=" + UrlEncode(ref.type.empty() ? "output" : ref.type);
    return url;
}

std::vector<std::pair<std::string, std::string>> Headers(const RunContext& ctx,
                                                         bool json) {
    std::vector<std::pair<std::string, std::string>> headers;
    if (json) headers.push_back({"content-type", "application/json"});
    if (!ctx.apiKey.empty()) {
        headers.push_back({"authorization", "Bearer " + ctx.apiKey});
    }
    return headers;
}

std::string StringOption(const OptionsMap& providerOptions,
                         const OptionsMap& requestOptions,
                         const char* key, const std::string& fallback) {
    for (const OptionsMap* map : {&requestOptions, &providerOptions}) {
        auto it = map->find(key);
        if (it == map->end()) continue;
        if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    }
    return fallback;
}

int64_t IntOption(const OptionsMap& providerOptions,
                  const OptionsMap& requestOptions,
                  const char* key, int64_t fallback) {
    for (const OptionsMap* map : {&requestOptions, &providerOptions}) {
        auto it = map->find(key);
        if (it == map->end()) continue;
        if (const auto* i = std::get_if<int64_t>(&it->second)) return *i;
        if (const auto* d = std::get_if<double>(&it->second)) {
            return static_cast<int64_t>(*d);
        }
    }
    return fallback;
}

bool BoolOption(const OptionsMap& providerOptions,
                const OptionsMap& requestOptions,
                const char* key, bool fallback) {
    for (const OptionsMap* map : {&requestOptions, &providerOptions}) {
        auto it = map->find(key);
        if (it == map->end()) continue;
        if (const auto* b = std::get_if<bool>(&it->second)) return *b;
    }
    return fallback;
}

Error MapPromptError(int statusCode, const std::string& body) {
    json parsed = ParseJsonLenient(body);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return MapHttpStatus(statusCode, body.substr(0, 200));
    }

    std::string message;
    std::string providerCode;
    if (parsed.contains("error") && parsed["error"].is_object()) {
        const json& error = parsed["error"];
        message      = error.value("message", "");
        providerCode = error.value("type", "");
        const std::string details = error.value("details", "");
        if (!details.empty()) message += " (" + details + ")";
    }

    // node_errors names the node that rejected the graph, which is the part
    // a user can act on: a missing checkpoint, an unknown sampler, a custom
    // node the install does not have.
    bool missingValue = false;
    if (parsed.contains("node_errors") && parsed["node_errors"].is_object()) {
        for (const auto& [nodeId, nodeError] : parsed["node_errors"].items()) {
            if (!nodeError.is_object()) continue;
            const std::string classType = nodeError.value("class_type", "");
            if (!nodeError.contains("errors") || !nodeError["errors"].is_array()) {
                continue;
            }
            for (const auto& entry : nodeError["errors"]) {
                if (!entry.is_object()) continue;
                const std::string type = entry.value("type", "");
                if (type == "value_not_in_list") missingValue = true;
                std::string line = "node " + nodeId;
                if (!classType.empty()) line += " (" + classType + ")";
                line += ": " + entry.value("message", type);
                const std::string details = entry.value("details", "");
                if (!details.empty()) line += " — " + details;
                message += message.empty() ? line : "; " + line;
            }
        }
    }

    Error error = MapHttpStatus(statusCode,
                                message.empty() ? body.substr(0, 200) : message);
    if (missingValue) error.code = ErrorCode::ModelNotFound;
    if (!providerCode.empty()) error.providerCode = providerCode;
    return error;
}

void CollectOutputs(const json& outputs, std::vector<OutputRef>& out) {
    if (!outputs.is_object()) return;
    for (const auto& [nodeId, node] : outputs.items()) {
        (void)nodeId;
        if (!node.is_object()) continue;
        for (const char* key : {"images", "gifs", "videos"}) {
            if (!node.contains(key) || !node[key].is_array()) continue;
            for (const auto& file : node[key]) {
                if (!file.is_object()) continue;
                OutputRef ref;
                ref.filename  = file.value("filename", "");
                ref.subfolder = file.value("subfolder", "");
                ref.type      = file.value("type", "output");
                if (ref.filename.empty()) continue;
                // Previews are written as temp files alongside the real
                // output; only finished files belong in the response.
                if (ref.type == "temp") continue;
                out.push_back(std::move(ref));
            }
        }
    }
}

bool ApplyNodeOverrides(json& graph, const OptionsMap& providerOptions,
                        const OptionsMap& requestOptions, Error* outError) {
    const std::string overrides =
        StringOption(providerOptions, requestOptions, kOptNodeOverrides, "");
    if (overrides.empty()) return true;

    json parsed = ParseJsonLenient(overrides);
    if (!parsed.is_object()) {
        if (outError) {
            outError->code    = ErrorCode::InvalidRequest;
            outError->message = "the \"node_overrides\" option is not a JSON "
                                "object of {node id: {input: value}}";
        }
        return false;
    }
    for (const auto& [nodeId, inputs] : parsed.items()) {
        if (!inputs.is_object()) continue;
        for (const auto& [field, value] : inputs.items()) {
            SetNodeInput(graph, nodeId, field, value);
        }
    }
    return true;
}

} // namespace comfyui_detail
} // namespace UltraAI
