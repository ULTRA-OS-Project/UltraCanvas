// UltraCanvas/Plugins/UltraNet/telnet/TelnetPlugin.cpp
// Telnet plug-in. Implements IRemoteAccessPlugin on libcurl's native
// telnet:// support. Telnet is a stream-of-bytes protocol; libcurl
// exposes it as "send the request body to the server, collect the
// response until the connection closes (or the server is silent)".
// This plug-in models that as one-shot "ExecuteCommand" — interactive
// shell sessions are a future extension.
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <curl/curl.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::size_t WriteString(char* p, std::size_t s, std::size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}

UltraNetResultCode MapCurlError(CURLcode rc) {
    switch (rc) {
        case CURLE_OK:                      return UltraNetResultCode::Success;
        case CURLE_URL_MALFORMAT:           return UltraNetResultCode::InvalidUrl;
        case CURLE_COULDNT_RESOLVE_HOST:    return UltraNetResultCode::HostNotFound;
        case CURLE_COULDNT_CONNECT:         return UltraNetResultCode::ConnectionRefused;
        case CURLE_OPERATION_TIMEDOUT:      return UltraNetResultCode::Timeout;
        case CURLE_RECV_ERROR:              return UltraNetResultCode::ReceiveFailed;
        case CURLE_SEND_ERROR:              return UltraNetResultCode::SendFailed;
        default:                            return UltraNetResultCode::Unknown;
    }
}

// Cursor used by libcurl's READFUNCTION to feed the command body.
struct UploadCursor {
    const char* data;
    std::size_t remaining;
};
std::size_t ReadCallback(char* buf, std::size_t size, std::size_t nmemb,
                         void* userp) {
    auto* u = static_cast<UploadCursor*>(userp);
    const std::size_t want = size * nmemb;
    const std::size_t n = want < u->remaining ? want : u->remaining;
    if (n == 0) return 0;
    std::memcpy(buf, u->data, n);
    u->data      += n;
    u->remaining -= n;
    return n;
}

// "Open shell" is a logical concept here — we just remember the URL +
// options so ExecuteCommand can issue per-command libcurl calls against
// the same server. No persistent socket between calls.
struct Session {
    std::string url;
    UltraNetRemoteOptions opts;
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

class TelnetPlugin : public IRemoteAccessPlugin {
public:
    std::string GetName() const override { return "UltraNet-Telnet"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"telnet"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle OpenShell(const std::string& url,
                             const UltraNetRemoteOptions& options) override {
        if (url.empty() || url.rfind("telnet://", 0) != 0) {
            return UltraNetInvalidHandle;
        }
        auto s = std::make_shared<Session>();
        s->url  = url;
        s->opts = options;
        const UltraNetHandle h = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions[h] = std::move(s);
        return h;
    }

    UltraNetResult ExecuteCommand(UltraNetHandle handle,
                                  const std::string& command,
                                  std::string& outStdOut,
                                  std::string& outStdErr,
                                  int& outExitCode) override {
        outStdOut.clear();
        outStdErr.clear();
        outExitCode = -1;

        std::shared_ptr<Session> s;
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            auto it = g_sessions.find(handle);
            if (it == g_sessions.end()) {
                return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                             "no such telnet session");
            }
            s = it->second;
        }

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");

        // Telnet command body: the command plus CRLF so the remote shell
        // executes it. libcurl streams this as the upload while
        // simultaneously collecting whatever the server writes.
        std::string body = command;
        if (body.empty() || body.back() != '\n') body += "\r\n";
        UploadCursor cursor{body.data(), body.size()};

        curl_easy_setopt(h.get(), CURLOPT_URL,           s->url.c_str());
        curl_easy_setopt(h.get(), CURLOPT_UPLOAD,        1L);
        curl_easy_setopt(h.get(), CURLOPT_READFUNCTION,  &ReadCallback);
        curl_easy_setopt(h.get(), CURLOPT_READDATA,      &cursor);
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteString);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA,     &outStdOut);
        curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL,      1L);
        curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(s->opts.connectTimeoutMs));
        // Telnet servers usually keep the connection open after a command;
        // libcurl reads until either the server closes or the timeout fires.
        // Use the idle timeout as the overall ceiling.
        const long overall = s->opts.idleTimeoutMs > 0
                                 ? static_cast<long>(s->opts.idleTimeoutMs)
                                 : 10000;
        curl_easy_setopt(h.get(), CURLOPT_TIMEOUT_MS, overall);
        if (!s->opts.credentials.username.empty()) {
            curl_easy_setopt(h.get(), CURLOPT_USERNAME,
                             s->opts.credentials.username.c_str());
            curl_easy_setopt(h.get(), CURLOPT_PASSWORD,
                             s->opts.credentials.password.c_str());
        }

        CURLcode rc = curl_easy_perform(h.get());
        // Telnet has no real exit code; treat any successful completion
        // as 0 and any error as -1.
        outExitCode = (rc == CURLE_OK || rc == CURLE_OPERATION_TIMEDOUT) ? 0 : -1;
        if (rc != CURLE_OK && rc != CURLE_OPERATION_TIMEDOUT) {
            return UltraNetResult::Error(MapCurlError(rc),
                                         curl_easy_strerror(rc));
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<TelnetPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<TelnetPlugin>());
}
#endif
