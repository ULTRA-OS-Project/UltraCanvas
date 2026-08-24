// UltraAI/adapters/minimax/src/MiniMaxInternal.h
// Helpers shared by the MiniMax video and image adapters: base-URL
// normalization, credential resolution, option pass-through with reserved
// control keys, and the two-layer error mapping MiniMax needs — HTTP status
// plus the base_resp envelope it returns inside otherwise-200 responses.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAIBase64.h"
#include "UltraAICommon.h"
#include "UltraAICredentials.h"
#include "UltraAIHttpError.h"
#include "UltraAITransport.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace UltraAI {
namespace minimax_detail {

using nlohmann::json;

constexpr const char* kDefaultBaseUrl   = "https://api.minimax.io";
constexpr const char* kDefaultVideoModel = "MiniMax-Hailuo-02";
constexpr const char* kDefaultImageModel = "image-01";

inline json ParseJsonLenient(const std::string& text) {
    return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

inline std::string NormalizeBaseUrl(const std::string& configured) {
    std::string base = configured.empty() ? kDefaultBaseUrl : configured;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

// Apply an OptionsMap as top-level request fields, skipping the adapter's
// own control keys so they never reach the provider. String values that
// parse as JSON objects/arrays are embedded structurally.
inline void ApplyOptions(json& body, const OptionsMap& options,
                         std::initializer_list<const char*> reserved) {
    for (const auto& [key, value] : options) {
        bool skip = false;
        for (const char* name : reserved) {
            if (key == name) { skip = true; break; }
        }
        if (skip) continue;

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

// Read a control option, preferring the per-request map over the provider
// defaults. Missing or wrongly-typed entries leave `fallback` in place.
inline int64_t IntOption(const OptionsMap& providerOptions,
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

inline bool BoolOption(const OptionsMap& providerOptions,
                       const OptionsMap& requestOptions,
                       const char* key, bool fallback) {
    for (const OptionsMap* map : {&requestOptions, &providerOptions}) {
        auto it = map->find(key);
        if (it == map->end()) continue;
        if (const auto* b = std::get_if<bool>(&it->second)) return *b;
    }
    return fallback;
}

// MiniMax reports application-level failures in base_resp with HTTP 200,
// so every response body has to be checked even on success. Codes per the
// published error-code table; anything unlisted becomes ProviderError with
// the numeric code in Error::providerCode.
inline ErrorCode MapBaseRespCode(int64_t status) {
    switch (status) {
        case 0:    return ErrorCode::None;
        case 1000: return ErrorCode::ProviderError;        // unknown error
        case 1001: return ErrorCode::Timeout;
        case 1002: return ErrorCode::RateLimited;
        case 1004: return ErrorCode::AuthenticationFailed;
        case 1008: return ErrorCode::QuotaExceeded;        // insufficient balance
        case 1013: return ErrorCode::ProviderError;        // internal service error
        case 1026: return ErrorCode::ContentFiltered;      // input flagged
        case 1027: return ErrorCode::ContentFiltered;      // output flagged
        case 2013: return ErrorCode::InvalidRequest;
        case 2049: return ErrorCode::AuthenticationFailed; // invalid api key
        default:   return ErrorCode::ProviderError;
    }
}

// Check the base_resp envelope of a parsed body. Returns true when the call
// succeeded; otherwise fills *outError.
inline bool CheckBaseResp(const json& body, Error* outError) {
    if (!body.is_object() || !body.contains("base_resp") ||
        !body["base_resp"].is_object()) {
        return true;   // no envelope -> nothing to reject
    }
    const json& resp = body["base_resp"];
    const int64_t status = resp.value("status_code", static_cast<int64_t>(0));
    if (status == 0) return true;

    if (outError) {
        outError->code    = MapBaseRespCode(status);
        outError->message = resp.value("status_msg", "MiniMax request failed");
        outError->providerCode = std::to_string(status);
    }
    return false;
}

// HTTP-level failure, refined with the base_resp envelope when present.
inline Error MapMiniMaxHttpError(int statusCode, const std::string& body) {
    json parsed = ParseJsonLenient(body);
    Error envelopeError;
    if (!parsed.is_discarded() && !CheckBaseResp(parsed, &envelopeError)) {
        return envelopeError;
    }
    return MapHttpStatus(statusCode, body.substr(0, 200));
}

// MiniMax accepts a public URL or a data URL for image inputs.
inline std::string MediaReference(const MediaBlob& blob) {
    if (!blob.bytes.empty()) {
        return Base64DataUrl(
            blob.mimeType.empty() ? "image/png" : blob.mimeType, blob.bytes);
    }
    return blob.url;
}

// Bearer credential, required: MiniMax has no keyless mode.
inline bool ResolveKey(const ProviderConfig& config, std::string& outKey,
                       Error* outError) {
    Error localError;
    outKey = ResolveApiKey(config, &localError);
    if (outKey.empty()) {
        if (outError) *outError = localError;
        return false;
    }
    return true;
}

} // namespace minimax_detail
} // namespace UltraAI
