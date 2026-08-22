// UltraAI/adapters/_shared/src/Cassette.cpp
// Cassette (recorded transport traffic) load/save. Format, version 1:
// {
//   "ultraai_cassette": 1,
//   "exchanges": [
//     {"type":"response","status":200,
//      "headers":[["content-type","application/json"]],"body":"..."},
//     {"type":"error","code":"NetworkError","message":"..."},
//     {"type":"sse","status":200,
//      "events":[{"event":"message_start","data":"...","id":"","retryMs":0}],
//      "error":{"code":"None","message":""}}
//   ]
// }
// Error codes are stored by name so cassettes stay readable and stable if
// the ErrorCode enum is ever reordered.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAICassette.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <utility>

namespace UltraAI {

namespace {

using nlohmann::json;

constexpr int kCassetteVersion = 1;

struct CodeName { ErrorCode code; const char* name; };
constexpr CodeName kCodeNames[] = {
    {ErrorCode::None,                 "None"},
    {ErrorCode::InvalidRequest,       "InvalidRequest"},
    {ErrorCode::AuthenticationFailed, "AuthenticationFailed"},
    {ErrorCode::RateLimited,          "RateLimited"},
    {ErrorCode::QuotaExceeded,        "QuotaExceeded"},
    {ErrorCode::ModelNotFound,        "ModelNotFound"},
    {ErrorCode::ContextLengthExceeded,"ContextLengthExceeded"},
    {ErrorCode::ContentFiltered,      "ContentFiltered"},
    {ErrorCode::UnsupportedFormat,    "UnsupportedFormat"},
    {ErrorCode::NetworkError,         "NetworkError"},
    {ErrorCode::Timeout,              "Timeout"},
    {ErrorCode::ProviderError,        "ProviderError"},
    {ErrorCode::Cancelled,            "Cancelled"},
    {ErrorCode::Unknown,              "Unknown"},
};

std::string ErrorCodeName(ErrorCode code) {
    for (const auto& entry : kCodeNames) {
        if (entry.code == code) return entry.name;
    }
    return "Unknown";
}

ErrorCode ErrorCodeFromName(const std::string& name) {
    for (const auto& entry : kCodeNames) {
        if (name == entry.name) return entry.code;
    }
    return ErrorCode::Unknown;
}

json ErrorToJson(const Error& e) {
    json j;
    j["code"] = ErrorCodeName(e.code);
    if (!e.message.empty())      j["message"] = e.message;
    if (!e.providerCode.empty()) j["providerCode"] = e.providerCode;
    return j;
}

Error ErrorFromJson(const json& j) {
    Error e;
    if (!j.is_object()) return e;
    e.code         = ErrorCodeFromName(j.value("code", "Unknown"));
    e.message      = j.value("message", "");
    e.providerCode = j.value("providerCode", "");
    return e;
}

bool Fail(std::string* out, const std::string& message) {
    if (out) *out = message;
    return false;
}

} // namespace

// ---------------------------------------------------------------------
// LoadCassette
// ---------------------------------------------------------------------

bool LoadCassette(const std::string& path, ScriptedTransport& transport,
                  std::string* outErrorMessage) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Fail(outErrorMessage, "cannot open cassette: " + path);

    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return Fail(outErrorMessage, "cassette is not valid JSON: " + path);
    }
    if (j.value("ultraai_cassette", 0) != kCassetteVersion) {
        return Fail(outErrorMessage,
                    "not a version-" + std::to_string(kCassetteVersion) +
                    " UltraAI cassette: " + path);
    }
    if (!j.contains("exchanges") || !j["exchanges"].is_array()) {
        return Fail(outErrorMessage, "cassette has no exchanges array: " + path);
    }

    for (const auto& ex : j["exchanges"]) {
        const std::string type = ex.value("type", "");
        if (type == "response") {
            TransportResponse resp;
            resp.statusCode = ex.value("status", 200);
            resp.body       = ex.value("body", "");
            if (ex.contains("headers") && ex["headers"].is_array()) {
                for (const auto& h : ex["headers"]) {
                    if (h.is_array() && h.size() == 2 &&
                        h[0].is_string() && h[1].is_string()) {
                        resp.headers.push_back({h[0], h[1]});
                    }
                }
            }
            transport.ScriptResponse(std::move(resp));
        } else if (type == "error") {
            transport.ScriptError(ErrorFromJson(ex));
        } else if (type == "sse") {
            std::vector<TransportSseEvent> events;
            if (ex.contains("events") && ex["events"].is_array()) {
                for (const auto& evj : ex["events"]) {
                    TransportSseEvent ev;
                    ev.event   = evj.value("event", "");
                    ev.data    = evj.value("data", "");
                    ev.id      = evj.value("id", "");
                    ev.retryMs = evj.value("retryMs", 0);
                    events.push_back(std::move(ev));
                }
            }
            Error finalError;
            if (ex.contains("error")) finalError = ErrorFromJson(ex["error"]);
            transport.ScriptSse(std::move(events), std::move(finalError),
                                ex.value("status", 200));
        } else {
            return Fail(outErrorMessage,
                        "cassette exchange has unknown type '" + type +
                        "': " + path);
        }
    }
    return true;
}

// ---------------------------------------------------------------------
// RecordingTransport
// ---------------------------------------------------------------------

RecordingTransport::RecordingTransport(std::shared_ptr<ITransport> inner)
    : inner_(std::move(inner)) {}

TransportResponse RecordingTransport::Request(const TransportRequest& request,
                                              Error* outError) {
    Error localError;
    TransportResponse resp = inner_->Request(request, &localError);

    Recorded rec;
    if (!localError.IsOk()) {
        rec.isError = true;
        rec.error   = localError;
    } else {
        rec.response = resp;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        exchanges_.push_back(std::move(rec));
    }
    if (outError) *outError = localError;
    return resp;
}

CancelFn RecordingTransport::SseStream(const TransportRequest& request,
                                       SseEventCallback onEvent,
                                       SseCompleteCallback onComplete) {
    auto events = std::make_shared<std::vector<TransportSseEvent>>();
    return inner_->SseStream(
        request,
        [events, onEvent](const TransportSseEvent& ev) {
            events->push_back(ev);
            onEvent(ev);
        },
        [this, events, onComplete](const Error& error, int statusCode) {
            Recorded rec;
            rec.isSse         = true;
            rec.events        = *events;
            rec.error         = error;
            rec.sseStatusCode = statusCode;
            {
                std::lock_guard<std::mutex> lock(mu_);
                exchanges_.push_back(std::move(rec));
            }
            onComplete(error, statusCode);
        });
}

bool RecordingTransport::Save(const std::string& path,
                              std::string* outErrorMessage) const {
    json exchanges = json::array();
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& rec : exchanges_) {
            json ex;
            if (rec.isSse) {
                ex["type"]   = "sse";
                ex["status"] = rec.sseStatusCode;
                json events = json::array();
                for (const auto& ev : rec.events) {
                    json evj;
                    evj["data"] = ev.data;
                    if (!ev.event.empty()) evj["event"] = ev.event;
                    if (!ev.id.empty())    evj["id"] = ev.id;
                    if (ev.retryMs != 0)   evj["retryMs"] = ev.retryMs;
                    events.push_back(std::move(evj));
                }
                ex["events"] = std::move(events);
                if (!rec.error.IsOk()) ex["error"] = ErrorToJson(rec.error);
            } else if (rec.isError) {
                ex["type"]  = "error";
                json e = ErrorToJson(rec.error);
                ex.update(e);
            } else {
                ex["type"]   = "response";
                ex["status"] = rec.response.statusCode;
                ex["body"]   = rec.response.body;
                json headers = json::array();
                for (const auto& kv : rec.response.headers) {
                    headers.push_back(json::array({kv.first, kv.second}));
                }
                ex["headers"] = std::move(headers);
            }
            exchanges.push_back(std::move(ex));
        }
    }
    json j;
    j["ultraai_cassette"] = kCassetteVersion;
    j["exchanges"]        = std::move(exchanges);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Fail(outErrorMessage, "cannot write cassette: " + path);
    out << j.dump(2) << '\n';
    if (!out.good()) {
        return Fail(outErrorMessage, "write failed for cassette: " + path);
    }
    return true;
}

} // namespace UltraAI
