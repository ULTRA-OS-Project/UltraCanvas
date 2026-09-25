// UltraAI/adapters/elevenlabs/src/ElevenLabsInternal.h
// Helpers for the ElevenLabs adapters: base-URL normalization, the two
// authentication schemes (ElevenLabs' xi-api-key header, or a bearer token
// for a relay), option handling with reserved control keys, and mapping of
// ElevenLabs' {"detail": ...} error bodies.
// Version: 0.1.0
// Last Modified: 2026-09-24
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAICredentials.h"
#include "UltraAIHttpError.h"
#include "UltraAITransport.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {
namespace elevenlabs_detail {

using nlohmann::json;

constexpr const char* kDefaultBaseUrl      = "https://api.elevenlabs.io";
constexpr const char* kDefaultSpeechModel  = "eleven_multilingual_v2";

// Control keys the adapter consumes; never sent as request-body fields.
constexpr std::initializer_list<const char*> kReservedKeys = {
    "auth_scheme", "default_voice_id", "output_format", "enable_logging",
    "stability", "similarity_boost", "style", "use_speaker_boost"};

inline json ParseJsonLenient(const std::string& text) {
    return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

inline std::string NormalizeBaseUrl(const std::string& configured) {
    std::string base = configured.empty() ? kDefaultBaseUrl : configured;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

// Percent-encode one URL path segment (voice ids are alphanumeric today,
// but a caller-supplied id must not be able to change the path).
inline std::string EncodePathSegment(const std::string& segment) {
    std::string out;
    for (unsigned char c : segment) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

inline const OptionValue* FindOption(const OptionsMap& providerOptions,
                                     const OptionsMap& requestOptions,
                                     const char* key) {
    for (const OptionsMap* map : {&requestOptions, &providerOptions}) {
        auto it = map->find(key);
        if (it != map->end()) return &it->second;
    }
    return nullptr;
}

inline std::string StringOption(const OptionsMap& providerOptions,
                                const OptionsMap& requestOptions,
                                const char* key) {
    const OptionValue* v = FindOption(providerOptions, requestOptions, key);
    if (v) {
        if (const auto* s = std::get_if<std::string>(v)) return *s;
    }
    return {};
}

// Apply an OptionsMap as top-level body fields, skipping reserved keys.
// String values that parse as JSON objects/arrays are embedded
// structurally (pronunciation_dictionary_locators, labels, ...).
inline void ApplyOptions(json& body, const OptionsMap& options) {
    for (const auto& [key, value] : options) {
        bool skip = false;
        for (const char* name : kReservedKeys) {
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

// ElevenLabs' "detail.status" strings, refined over the HTTP mapping.
inline ErrorCode MapDetailStatus(const std::string& status, ErrorCode fallback) {
    if (status == "quota_exceeded" || status == "payment_required" ||
        status == "insufficient_credits") {
        return ErrorCode::QuotaExceeded;
    }
    if (status == "invalid_api_key" || status == "missing_permissions" ||
        status == "needs_authorization" || status == "unauthorized") {
        return ErrorCode::AuthenticationFailed;
    }
    if (status == "too_many_concurrent_requests" || status == "system_busy" ||
        status == "rate_limit_exceeded") {
        return ErrorCode::RateLimited;
    }
    if (status == "voice_not_found" || status == "invalid_voice_id") {
        return ErrorCode::InvalidRequest;
    }
    if (status == "model_not_found" || status == "invalid_model_id") {
        return ErrorCode::ModelNotFound;
    }
    return fallback;
}

// Map a parsed {"detail": ...} object. `detail` is an object with status
// and message, a plain string, or (422 validation errors) an array of
// {"loc", "msg"} entries. Returns false when `body` carries no detail.
inline bool ErrorFromDetail(const json& body, int httpStatus, Error* outError) {
    if (!body.is_object() || !body.contains("detail")) return false;
    const json& detail = body["detail"];

    std::string status;
    std::string message;
    if (detail.is_object()) {
        status  = detail.value("status", "");
        message = detail.value("message", "");
    } else if (detail.is_string()) {
        message = detail.get<std::string>();
    } else if (detail.is_array() && !detail.empty() && detail[0].is_object()) {
        message = detail[0].value("msg", "");
    }
    if (message.empty()) message = "ElevenLabs request failed";

    Error base = httpStatus >= 400 ? MapHttpStatus(httpStatus)
                                   : Error{ErrorCode::ProviderError, {}, {}};
    if (outError) {
        outError->code    = MapDetailStatus(status, base.code);
        outError->message = message;
        outError->providerCode =
            !status.empty() ? status : std::to_string(httpStatus);
    }
    return true;
}

// HTTP-level failure, refined with the detail object when the body has one.
inline Error MapElevenLabsHttpError(int statusCode, const std::string& body) {
    Error error;
    if (ErrorFromDetail(ParseJsonLenient(body), statusCode, &error)) {
        return error;
    }
    return MapHttpStatus(statusCode, body.substr(0, 200));
}

// Characters, not bytes: ElevenLabs bills per character and a UTF-8 byte
// count would overstate every non-ASCII script.
inline int32_t CountUtf8Characters(const std::string& text) {
    int32_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;   // not a continuation byte
    }
    return count;
}

// Authentication header for the configured scheme; empty name on an
// unknown scheme.
inline bool AuthHeader(const ProviderConfig& config, const std::string& key,
                       std::pair<std::string, std::string>& outHeader,
                       Error* outError) {
    std::string scheme = StringOption(config.providerOptions, {}, "auth_scheme");
    if (scheme.empty() || scheme == "xi-api-key") {
        outHeader = {"xi-api-key", key};
        return true;
    }
    if (scheme == "bearer") {
        outHeader = {"authorization", "Bearer " + key};
        return true;
    }
    if (outError) {
        outError->code    = ErrorCode::InvalidRequest;
        outError->message = "unknown auth_scheme '" + scheme +
                            "'; use \"xi-api-key\" or \"bearer\"";
    }
    return false;
}

} // namespace elevenlabs_detail
} // namespace UltraAI
