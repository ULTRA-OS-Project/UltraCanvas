// UltraAI/adapters/openai/src/OpenAIInternal.h
// Helpers shared by the OpenAI text and embeddings adapters: base-URL
// normalization, the keyless-when-self-hosted credential rule, request
// option pass-through, and error-body refinement.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAICredentials.h"
#include "UltraAIHttpError.h"
#include "UltraAITransport.h"

#include <nlohmann/json.hpp>

#include <string>

namespace UltraAI {
namespace openai_detail {

using nlohmann::json;

constexpr const char* kDefaultBaseUrl = "https://api.openai.com";

inline json ParseJsonLenient(const std::string& text) {
    return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

inline std::string NormalizeBaseUrl(const std::string& configured) {
    std::string base = configured.empty() ? kDefaultBaseUrl : configured;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

// The hosted OpenAI API always needs a key; a self-hosted OpenAI-compatible
// server (Ollama, vLLM, llama.cpp server) usually runs without one, so a
// custom baseUrl makes the credential optional. Returns false only when a
// key is required and unresolvable (*outError filled by ResolveApiKey).
inline bool ResolveOptionalKey(const ProviderConfig& config,
                               const std::string& baseUrl,
                               std::string& outKey, Error* outError) {
    const bool required = (baseUrl == kDefaultBaseUrl);
    Error localError;
    outKey = ResolveApiKey(config, &localError);
    if (outKey.empty() && required) {
        if (outError) *outError = localError;
        return false;
    }
    return true;
}

// Apply an OptionsMap as top-level request fields. String values that parse
// as JSON objects/arrays are embedded structurally (escape hatch for nested
// config like "reasoning_effort" wrappers or "stream_options").
inline void ApplyOptions(json& body, const OptionsMap& options) {
    for (const auto& [key, value] : options) {
        if (const auto* b = std::get_if<bool>(&value))          body[key] = *b;
        else if (const auto* i = std::get_if<int64_t>(&value))  body[key] = *i;
        else if (const auto* d = std::get_if<double>(&value))   body[key] = *d;
        else if (const auto* s = std::get_if<std::string>(&value)) {
            json parsed = ParseJsonLenient(*s);
            if (!parsed.is_discarded() && (parsed.is_object() || parsed.is_array())) {
                body[key] = std::move(parsed);
            } else {
                body[key] = *s;
            }
        }
    }
}

// usage: {"prompt_tokens":N,"completion_tokens":N,
//         "prompt_tokens_details":{"cached_tokens":N},
//         "completion_tokens_details":{"reasoning_tokens":N}}
inline void ParseUsage(const json& u, TokenUsage& out) {
    if (!u.is_object()) return;
    out.inputTokens  = u.value("prompt_tokens", out.inputTokens);
    out.outputTokens = u.value("completion_tokens", out.outputTokens);
    if (u.contains("prompt_tokens_details") &&
        u["prompt_tokens_details"].is_object()) {
        out.cachedInputTokens = u["prompt_tokens_details"]
            .value("cached_tokens", out.cachedInputTokens);
    }
    if (u.contains("completion_tokens_details") &&
        u["completion_tokens_details"].is_object()) {
        out.reasoningTokens = u["completion_tokens_details"]
            .value("reasoning_tokens", out.reasoningTokens);
    }
}

// Refine a generic HTTP error with the OpenAI error body:
// {"error":{"message":"...","type":"invalid_request_error","code":"..."}}
inline Error MapOpenAIError(int statusCode, const std::string& body) {
    json j = ParseJsonLenient(body);
    std::string type, code, message;
    if (j.is_object() && j.contains("error") && j["error"].is_object()) {
        const json& e = j["error"];
        type    = e.value("type", "");
        message = e.value("message", "");
        if (e.contains("code") && e["code"].is_string()) code = e["code"];
    }
    Error err = MapHttpStatus(statusCode, message.empty() ? body.substr(0, 200)
                                                          : message);
    if (type == "invalid_request_error")       err.code = ErrorCode::InvalidRequest;
    else if (type == "authentication_error")   err.code = ErrorCode::AuthenticationFailed;
    else if (type == "insufficient_quota")     err.code = ErrorCode::QuotaExceeded;
    else if (type == "rate_limit_error")       err.code = ErrorCode::RateLimited;
    if (code == "model_not_found")             err.code = ErrorCode::ModelNotFound;
    else if (code == "invalid_api_key")        err.code = ErrorCode::AuthenticationFailed;
    else if (code == "insufficient_quota")     err.code = ErrorCode::QuotaExceeded;
    else if (code == "context_length_exceeded")
        err.code = ErrorCode::ContextLengthExceeded;
    if (!code.empty())      err.providerCode = code;
    else if (!type.empty()) err.providerCode = type;
    return err;
}

} // namespace openai_detail
} // namespace UltraAI
