// UltraCanvas/Plugins/UltraNet/grpc/GrpcPlugin.cpp
// gRPC plug-in — libcurl-native, NOT grpc-c++. libcurl already speaks
// HTTP/2 (the gRPC transport); we add the 5-byte gRPC length-prefix
// framing on the wire and pull the gRPC-Status trailer out of the
// response. This avoids dragging in grpc-c++ / protobuf / abseil /
// re2 as build dependencies; apps bring their own protobuf-serialized
// payloads.
//
// Wire format (HTTP/2 body for gRPC, RFC-ish via grpc.io spec):
//   - Compression flag (1 byte): 0 = uncompressed, 1 = compressed
//   - Length (4 bytes, big-endian, unsigned)
//   - Message bytes (length)
//   - ...repeats for streaming, but UnaryCall is one-in / one-out
//
// Response trailer:
//   grpc-status: 0   (or non-zero on RPC error)
//   grpc-message: <human-readable error string>
//
// Plug-in does NOT compress payloads in v0.1; flag is always 0.
// Schemes registered: grpc / grpcs.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// =====================================================================
// Per-session state — store URL + auth + tls prefs. Each UnaryCall
// makes a fresh libcurl easy handle; libcurl reuses HTTP/2 connections
// per-handle via its share/pool internally so this stays cheap.
// =====================================================================
struct Session {
    UltraNetHandle handle  = UltraNetInvalidHandle;
    std::string    baseUrl;        // https://host[:port]
    UltraNetRpcOptions opts;
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

// =====================================================================
// libcurl glue
// =====================================================================
std::size_t WriteBytes(char* p, std::size_t s, std::size_t n, void* ud) {
    auto* v = static_cast<std::vector<uint8_t>*>(ud);
    const auto bytes = s * n;
    v->insert(v->end(),
              reinterpret_cast<uint8_t*>(p),
              reinterpret_cast<uint8_t*>(p) + bytes);
    return bytes;
}

struct HeaderCollector {
    int grpcStatus = -1;
    std::string grpcMessage;
};

std::size_t HeaderCallback(char* data, std::size_t s, std::size_t n, void* ud) {
    auto* h = static_cast<HeaderCollector*>(ud);
    const std::size_t total = s * n;
    std::string line(data, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    auto lowerStarts = [&](const char* prefix) {
        const std::size_t plen = std::strlen(prefix);
        if (line.size() < plen) return false;
        for (std::size_t i = 0; i < plen; ++i) {
            if (std::tolower(static_cast<unsigned char>(line[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i]))) {
                return false;
            }
        }
        return true;
    };
    if (lowerStarts("grpc-status:")) {
        h->grpcStatus = std::atoi(line.c_str() + 12);
    } else if (lowerStarts("grpc-message:")) {
        std::size_t i = 13;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        h->grpcMessage = line.substr(i);
    }
    return total;
}

bool LibcurlHasHttp2() {
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    return v && (v->features & CURL_VERSION_HTTP2) != 0;
}

// =====================================================================
// Frame helpers — gRPC wraps each message in a 5-byte length-prefix.
// =====================================================================
std::vector<uint8_t> WrapFrame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.reserve(payload.size() + 5);
    out.push_back(0);                                          // no compression
    const uint32_t n = static_cast<uint32_t>(payload.size());
    out.push_back(static_cast<uint8_t>((n >> 24) & 0xff));     // big-endian length
    out.push_back(static_cast<uint8_t>((n >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((n >>  8) & 0xff));
    out.push_back(static_cast<uint8_t>((n      ) & 0xff));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool UnwrapFrame(const std::vector<uint8_t>& wire,
                 std::vector<uint8_t>& outPayload) {
    if (wire.size() < 5) return false;
    const uint32_t n = (uint32_t(wire[1]) << 24) | (uint32_t(wire[2]) << 16) |
                       (uint32_t(wire[3]) <<  8) |  uint32_t(wire[4]);
    if (wire.size() < 5u + n) return false;
    outPayload.assign(wire.begin() + 5, wire.begin() + 5 + n);
    return true;
}

// =====================================================================
// GrpcPlugin
// =====================================================================
class GrpcPlugin : public IRpcProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-gRPC"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"grpc", "grpcs"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle Connect(const std::string& url,
                           const UltraNetRpcOptions& options) override {
        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c)) return UltraNetInvalidHandle;
        if (c.scheme != "grpc" && c.scheme != "grpcs") return UltraNetInvalidHandle;
        if (!LibcurlHasHttp2()) return UltraNetInvalidHandle;

        const bool tls = (c.scheme == "grpcs" || options.useTls);
        const int  port = c.port > 0 ? c.port : (tls ? 443 : 50051);

        std::ostringstream base;
        base << (tls ? "https://" : "http://") << c.host << ':' << port;

        auto s = std::make_shared<Session>();
        s->opts    = options;
        s->baseUrl = base.str();

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions[h] = s;
        }
        return h;
    }

    UltraNetResult UnaryCall(UltraNetHandle handle,
                             const std::string& service,
                             const std::string& method,
                             const std::vector<uint8_t>& request,
                             std::vector<uint8_t>& outResponse,
                             const UltraNetRpcOptions& options) override {
        outResponse.clear();
        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                             "no such gRPC session");

        const std::string url = s->baseUrl + "/" + service + "/" + method;
        const std::vector<uint8_t> wire = WrapFrame(request);

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");

        curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(h.get(), CURLOPT_HTTP_VERSION,
                         (long)CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
        curl_easy_setopt(h.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(h.get(), CURLOPT_POSTFIELDS, wire.data());
        curl_easy_setopt(h.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(wire.size()));

        // Build headers. content-type + te:trailers are mandatory for
        // gRPC; we also pass any custom metadata from options.headers.
        curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "content-type: application/grpc+proto");
        hdrs = curl_slist_append(hdrs, "te: trailers");
        hdrs = curl_slist_append(hdrs,
            ("user-agent: " + std::string("ultranet-grpc/0.1")).c_str());
        if (!options.credentials.token.empty()) {
            const std::string auth = "authorization: Bearer " +
                                     options.credentials.token;
            hdrs = curl_slist_append(hdrs, auth.c_str());
        }
        for (const auto& [name, value] : options.headers.Entries()) {
            const std::string line = name + ": " + value;
            hdrs = curl_slist_append(hdrs, line.c_str());
        }
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
            hdrsGuard(hdrs, curl_slist_free_all);
        curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, hdrs);

        std::vector<uint8_t> body;
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteBytes);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &body);

        HeaderCollector hc;
        curl_easy_setopt(h.get(), CURLOPT_HEADERFUNCTION, &HeaderCallback);
        curl_easy_setopt(h.get(), CURLOPT_HEADERDATA, &hc);

        curl_easy_setopt(h.get(), CURLOPT_SSL_VERIFYPEER,
                         options.verifyTls ? 1L : 0L);
        curl_easy_setopt(h.get(), CURLOPT_SSL_VERIFYHOST,
                         options.verifyTls ? 2L : 0L);
        curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(options.connectTimeoutMs));
        curl_easy_setopt(h.get(), CURLOPT_TIMEOUT_MS,
                         static_cast<long>(options.callTimeoutMs));
        curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);

        const CURLcode rc = curl_easy_perform(h.get());
        if (rc != CURLE_OK) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         curl_easy_strerror(rc));
        }

        // Unwrap the 5-byte gRPC frame. If grpc-status came back non-zero
        // surface that even when the response body is well-formed.
        std::vector<uint8_t> payload;
        if (!body.empty() && !UnwrapFrame(body, payload)) {
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         "malformed gRPC response frame");
        }
        outResponse = std::move(payload);

        if (hc.grpcStatus > 0) {
            UltraNetResult r;
            r.success    = false;
            r.code       = UltraNetResultCode::HttpError;
            r.httpStatus = hc.grpcStatus;
            r.message    = "gRPC status " + std::to_string(hc.grpcStatus) +
                          (hc.grpcMessage.empty() ? "" : ": " + hc.grpcMessage);
            return r;
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<GrpcPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<GrpcPlugin>());
}
