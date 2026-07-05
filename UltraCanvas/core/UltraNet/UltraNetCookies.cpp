// core/UltraNet/UltraNetCookies.cpp
// Sessions = CURLSH (connection / cookie sharing) + libcurl cookie engine
// (CURLOPT_COOKIEFILE / CURLOPT_COOKIEJAR) + default headers / proxy.
// Each Session_HttpGet/Post call builds a fresh easy handle that attaches
// the share; the share guarantees connections and the cookie store are
// shared across all requests issued through the same session handle.
// Version: 0.2.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCookies.h"
#include "UltraNetHttpEasy.h"

#include <curl/curl.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    CURLSH*        share  = nullptr;
    UltraNetSessionOptions options;
    std::string    cookieFile;       // CURLOPT_COOKIEFILE — read at start
    std::string    cookieJar;        // CURLOPT_COOKIEJAR  — written at end
    std::array<std::mutex, CURL_LOCK_DATA_LAST> locks;
};

void ShareLock(CURL*, curl_lock_data data, curl_lock_access, void* userp) {
    auto* s = static_cast<Session*>(userp);
    if (data < CURL_LOCK_DATA_LAST) s->locks[data].lock();
}
void ShareUnlock(CURL*, curl_lock_data data, void* userp) {
    auto* s = static_cast<Session*>(userp);
    if (data < CURL_LOCK_DATA_LAST) s->locks[data].unlock();
}

// Registry. Sessions are reference-counted by the registry (one strong owner).
std::mutex g_registryMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextSession{1};

std::shared_ptr<Session> FindSession(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

UltraNetResult PerformSessionRequest(Session& s,
                                     const UltraNetHttpRequest& request,
                                     UltraNetResponse& outResponse,
                                     const std::vector<uint8_t>* readBody) {
    if (request.url.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "URL is empty");
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(
        curl_easy_init(), curl_easy_cleanup);
    if (!easy) {
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_easy_init() failed");
    }

    // Apply session default headers first, then per-call options override.
    UltraNetHttpRequest merged = request;
    UltraNetHttpHeaders effective = s.options.defaultHeaders;
    for (const auto& kv : request.headers.Entries())         effective.Set(kv.first, kv.second);
    for (const auto& kv : request.options.headers.Entries()) effective.Set(kv.first, kv.second);
    merged.headers = effective;
    merged.options.headers = UltraNetHttpHeaders{};   // avoid double-apply
    if (!merged.options.proxy.IsEnabled() && s.options.proxy.IsEnabled()) {
        merged.options.proxy = s.options.proxy;
    }

    const UltraNetConfig cfg = UltraNet_GetConfig();
    ultranet_internal::WriteSink sink;
    sink.body = &outResponse.body;

    curl_slist* slist = ultranet_internal::ConfigureEasyHandle(
        easy.get(), merged, cfg, &sink, &outResponse);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        slistGuard(slist, curl_slist_free_all);

    if (readBody && !readBody->empty()) {
        curl_easy_setopt(easy.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(readBody->size()));
        curl_easy_setopt(easy.get(), CURLOPT_POSTFIELDS, readBody->data());
    }

    // Hook up the share (cookies + connection pool).
    curl_easy_setopt(easy.get(), CURLOPT_SHARE, s.share);

    // Always engage the cookie engine. An empty COOKIEFILE turns it on with
    // no preload, which is what we want for in-memory cookies between calls.
    curl_easy_setopt(easy.get(), CURLOPT_COOKIEFILE,
                     s.cookieFile.empty() ? "" : s.cookieFile.c_str());
    if (!s.cookieJar.empty()) {
        curl_easy_setopt(easy.get(), CURLOPT_COOKIEJAR, s.cookieJar.c_str());
    }
    if (s.options.maxConnectionsPerHost > 0) {
        curl_easy_setopt(easy.get(), CURLOPT_MAXCONNECTS,
                         static_cast<long>(s.options.maxConnectionsPerHost));
    }

    CURLcode rc = curl_easy_perform(easy.get());
    return ultranet_internal::FinalizeFromEasy(
        easy.get(), rc, request.url, outResponse, sink.exceededLimit);
}

} // namespace

UltraNetHandle UltraNet_CreateSession(const UltraNetSessionOptions& options) {
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    auto s = std::make_shared<Session>();
    s->options = options;
    s->cookieFile = options.persistCookies ? options.cookieJarPath : "";
    s->cookieJar  = options.persistCookies ? options.cookieJarPath : "";
    s->share = curl_share_init();
    if (!s->share) return UltraNetInvalidHandle;

    curl_share_setopt(s->share, CURLSHOPT_LOCKFUNC,   ShareLock);
    curl_share_setopt(s->share, CURLSHOPT_UNLOCKFUNC, ShareUnlock);
    curl_share_setopt(s->share, CURLSHOPT_USERDATA,   s.get());
    curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    if (options.reuseConnections) {
        curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    }
    curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

    s->handle = g_nextSession.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_registryMutex);
        g_sessions[s->handle] = s;
    }
    return s->handle;
}

void UltraNet_DestroySession(UltraNetHandle session) {
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lk(g_registryMutex);
        auto it = g_sessions.find(session);
        if (it == g_sessions.end()) return;
        s = it->second;
        g_sessions.erase(it);
    }
    if (s && s->share) {
        curl_share_cleanup(s->share);
        s->share = nullptr;
    }
}

UltraNetResult UltraNet_SessionHttpGet(UltraNetHandle session,
                                       const std::string& url,
                                       UltraNetResponse& outResponse,
                                       const UltraNetHttpOptions& options) {
    auto s = FindSession(session);
    if (!s) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such session");
    }
    outResponse = {};
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Get; req.options = options;
    return PerformSessionRequest(*s, req, outResponse, nullptr);
}

UltraNetResult UltraNet_SessionHttpPost(UltraNetHandle session,
                                        const std::string& url,
                                        const std::vector<uint8_t>& body,
                                        UltraNetResponse& outResponse,
                                        const UltraNetHttpOptions& options) {
    auto s = FindSession(session);
    if (!s) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such session");
    }
    outResponse = {};
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Post;
    req.body = body; req.options = options;
    return PerformSessionRequest(*s, req, outResponse, &body);
}

UltraNetResult UltraNet_SessionLoadCookies(UltraNetHandle session,
                                           const std::string& filePath) {
    auto s = FindSession(session);
    if (!s) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such session");
    }
    s->cookieFile = filePath;
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_SessionSaveCookies(UltraNetHandle session,
                                           const std::string& filePath) {
    auto s = FindSession(session);
    if (!s) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such session");
    }
    s->cookieJar = filePath;
    // libcurl flushes the jar when the easy handle is destroyed. Trigger a
    // no-op request to force a flush of the current cookie state.
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> e(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (e) {
        curl_easy_setopt(e.get(), CURLOPT_SHARE,     s->share);
        curl_easy_setopt(e.get(), CURLOPT_COOKIEJAR, filePath.c_str());
        curl_easy_setopt(e.get(), CURLOPT_COOKIELIST, "FLUSH");
    }
    return UltraNetResult::Ok();
}
