// UltraAI/adapters/comfyui/src/ComfyUIInternal.h
// Shared pieces of the ComfyUI adapter: the run context, URL building,
// option reading, and ComfyUI's node_errors validation format.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAIHttpError.h"
#include "UltraAITransport.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace UltraAI {
namespace comfyui_detail {

using nlohmann::json;

constexpr const char* kDefaultBaseUrl = "http://127.0.0.1:8188";

constexpr const char* kOptWorkflow       = "workflow";
constexpr const char* kOptNodeOverrides  = "node_overrides";
constexpr const char* kOptSamplerName    = "sampler_name";
constexpr const char* kOptJobTimeoutMs   = "job_timeout_ms";
constexpr const char* kOptPollIntervalMs = "poll_interval_ms";
constexpr const char* kOptUseWebSocket   = "use_websocket";

// One produced file, as ComfyUI names it in history / executed events.
struct OutputRef {
    std::string filename;
    std::string subfolder;
    std::string type;      // "output", "temp", ...
};

// Everything a generation needs, by value: a job's worker thread must not
// reach back into the adapter, which the caller may destroy first.
struct RunContext {
    std::shared_ptr<ITransport> transport;
    std::string baseUrl;
    std::string apiKey;      // empty for a local server
    std::string clientId;
    int  timeoutMs      = 60000;
    int  jobTimeoutMs   = 600000;
    int  pollIntervalMs = 1000;
    bool useWebSocket   = true;
    bool returnAsUrl    = false;
};

inline json ParseJsonLenient(const std::string& text) {
    return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

inline std::string NormalizeBaseUrl(const std::string& configured) {
    std::string base = configured.empty() ? kDefaultBaseUrl : configured;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

// Percent-encode a query-string value. Output filenames routinely contain
// spaces and brackets, and the subfolder comes back from the server, so
// nothing is concatenated into a URL raw.
std::string UrlEncode(const std::string& value);

// GET /view URL for one produced file.
std::string ViewUrl(const RunContext& ctx, const OutputRef& ref);

// Headers for a JSON request (or a bare GET when `json` is false),
// including the optional bearer credential.
std::vector<std::pair<std::string, std::string>> Headers(
    const RunContext& ctx, bool json);

// Read a control option, preferring the per-request map over the provider
// defaults.
std::string StringOption(const OptionsMap& providerOptions,
                         const OptionsMap& requestOptions,
                         const char* key, const std::string& fallback);
int64_t IntOption(const OptionsMap& providerOptions,
                  const OptionsMap& requestOptions,
                  const char* key, int64_t fallback);
bool BoolOption(const OptionsMap& providerOptions,
                const OptionsMap& requestOptions,
                const char* key, bool fallback);

// Map a failed /prompt exchange. ComfyUI answers 400 with
// {"error":{"type":...,"message":...}, "node_errors":{"<id>":{...}}}; a
// checkpoint or sampler value the server does not have shows up there as
// value_not_in_list, which is ModelNotFound rather than a generic bad
// request — the difference between "install this model" and "fix your code".
Error MapPromptError(int statusCode, const std::string& body);

// Collect the images an execution produced from a ComfyUI outputs object
// ({"<node id>": {"images": [{filename, subfolder, type}, ...]}}).
void CollectOutputs(const json& outputs, std::vector<OutputRef>& out);

} // namespace comfyui_detail
} // namespace UltraAI
