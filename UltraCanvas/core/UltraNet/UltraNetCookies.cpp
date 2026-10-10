// core/UltraNet/UltraNetCookies.cpp
// Sessions = CURLSH (cookie / DNS / TLS-session sharing) + libcurl cookie
// engine (CURLOPT_COOKIEFILE / CURLOPT_COOKIEJAR) + default headers / proxy
// + a pool of easy handles that carries the open connections.
//
// The share holds what libcurl supports sharing between threads that run
// transfers at the same time: the cookie store, the DNS cache and the TLS
// session cache. It used to hold the connection pool too
// (CURL_LOCK_DATA_CONNECT), which libcurl documents as NOT supported between
// concurrent threads - and a session is meant to be used from several. The
// connections now live in the session's easy handles instead: a request
// takes an idle handle for itself, and gives it back with the connections it
// opened still open, so the next request takes them up. No two threads ever
// use one handle - or one connection pool - at once.
// Version: 0.3.0 - connection pool per easy handle, not shared across threads
// Last Modified: 2026-10-09
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
#include <vector>

namespace {

// How many idle easy handles - and the connections they hold - a session
// keeps between requests: one per thread that used it at the same time, up
// to this many; a handle given back beyond it is closed.
constexpr std::size_t kMaxIdleHandles = 8;

struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    CURLSH*        share  = nullptr;
    UltraNetSessionOptions options;
    std::string    cookieFile;       // CURLOPT_COOKIEFILE — read at start
    std::string    cookieJar;        // CURLOPT_COOKIEJAR  — flushed per request
    std::array<std::mutex, CURL_LOCK_DATA_LAST> locks;

    // Easy handles between requests, each with the connections its last
    // request left open (libcurl keeps a handle's connections across
    // curl_easy_reset).
    std::mutex         poolMutex;
    std::vector<CURL*> idle;

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    // Runs when the last request still holding the session has finished,
    // however long after UltraNet_DestroySession that is. The handles go
    // before the share they are attached to.
    ~Session() {
        for (CURL* h : idle) curl_easy_cleanup(h);
        idle.clear();
        if (share) curl_share_cleanup(share);
    }

    // A handle for one request, used by the calling thread alone until
    // Give: an idle one with its connections, reset to defaults, or a new
    // one. Null only when libcurl cannot make one.
    CURL* Take() {
        CURL* h = nullptr;
        {
            std::lock_guard<std::mutex> lk(poolMutex);
            if (!idle.empty()) {
                h = idle.back();
                idle.pop_back();
            }
        }
        if (h) {
            curl_easy_reset(h);
            return h;
        }
        return curl_easy_init();
    }

    // Back into the pool with its connections open - or closed, for a
    // session that does not reuse connections, or a pool already full.
    void Give(CURL* h) {
        if (!h) return;
        if (options.reuseConnections) {
            std::lock_guard<std::mutex> lk(poolMutex);
            if (idle.size() < kMaxIdleHandles) {
                idle.push_back(h);
                return;
            }
        }
        curl_easy_cleanup(h);
    }
};

// Gives a request's handle back to its session however the request ends.
class TakenHandle {
public:
    explicit TakenHandle(Session& s) : session_(s), handle_(s.Take()) {}
    ~TakenHandle() { session_.Give(handle_); }
    TakenHandle(const TakenHandle&) = delete;
    TakenHandle& operator=(const TakenHandle&) = delete;
    CURL* get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    Session& session_;
    CURL*    handle_;
};

void ShareLock(CURL*, curl_lock_data data, curl_lock_access, void* userp) {
    auto* s = static_cast<Session*>(userp);
    if (data < CURL_LOCK_DATA_LAST) s->locks[data].lock();
}
void ShareUnlock(CURL*, curl_lock_data data, void* userp) {
    auto* s = static_cast<Session*>(userp);
    if (data < CURL_LOCK_DATA_LAST) s->locks[data].unlock();
}

// Registry. Sessions are reference-counted by the registry (one strong owner)
// and by each request in flight on them. Never destroyed: a session still
// open at exit must not call into libcurl from a static destructor, after
// the TLS library may already have cleaned up - UltraNet_Shutdown closes
// them in time (CloseAllSessions).
std::mutex& RegistryMutex() {
    static std::mutex* m = new std::mutex;
    return *m;
}
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>>& Sessions() {
    static auto* sessions = new std::unordered_map<UltraNetHandle, std::shared_ptr<Session>>;
    return *sessions;
}
std::atomic<UltraNetHandle> g_nextSession{1};

std::shared_ptr<Session> FindSession(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(RegistryMutex());
    auto it = Sessions().find(h);
    return it == Sessions().end() ? nullptr : it->second;
}

UltraNetResult PerformSessionRequest(Session& s,
                                     const UltraNetHttpRequest& request,
                                     UltraNetResponse& outResponse,
                                     const std::vector<uint8_t>* readBody) {
    if (request.url.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "URL is empty");
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    // Declared first, so it is given back last: after the header list and
    // the body it points at are gone, which an idle handle never reads -
    // the next Take resets it before anything else.
    TakenHandle easy(s);
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

    // Hook up the share (cookies, DNS, TLS sessions); the connections are
    // the handle's own.
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
    // The jar used to be written when the request's handle was destroyed;
    // a handle that goes back to the pool is not, so it is written now.
    if (!s.cookieJar.empty())
        curl_easy_setopt(easy.get(), CURLOPT_COOKIELIST, "FLUSH");
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
    // Not CURL_LOCK_DATA_CONNECT: libcurl does not support one connection
    // pool used by concurrent threads. Connections are reused through the
    // session's handles instead (Session::Take / Give), when
    // options.reuseConnections asks for it.
    curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

    s->handle = g_nextSession.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(RegistryMutex());
        Sessions()[s->handle] = s;
    }
    return s->handle;
}

void UltraNet_DestroySession(UltraNetHandle session) {
    // The handles, their connections and the share are closed by ~Session,
    // once no request is using them - now, unless one is in flight on
    // another thread, which then closes them when it returns rather than
    // finding them freed under it.
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lk(RegistryMutex());
        auto it = Sessions().find(session);
        if (it == Sessions().end()) return;
        s = std::move(it->second);
        Sessions().erase(it);
    }
}

namespace ultranet_internal {

// Called by UltraNet_Shutdown before curl_global_cleanup: every session still
// open is closed, since libcurl may not be called once it is gone.
void CloseAllSessions() {
    std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> closing;
    {
        std::lock_guard<std::mutex> lk(RegistryMutex());
        closing.swap(Sessions());
    }
}

} // namespace ultranet_internal

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
    // Attaching the share binds this handle to the session's cookie db, so
    // COOKIELIST=FLUSH writes the shared cookies (probe-verified). Caveat on
    // libcurl >= ~8.10: FLUSH is gated on the cookie db's `running` flag,
    // which is only set once a transfer has run — saving from a session that
    // never issued a request writes nothing (there is nothing to save).
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> e(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (e) {
        curl_easy_setopt(e.get(), CURLOPT_SHARE,     s->share);
        curl_easy_setopt(e.get(), CURLOPT_COOKIEJAR, filePath.c_str());
        curl_easy_setopt(e.get(), CURLOPT_COOKIELIST, "FLUSH");
    }
    return UltraNetResult::Ok();
}
