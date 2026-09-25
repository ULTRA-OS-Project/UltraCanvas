// UltraCanvas/Plugins/UltraNet/mdns/MdnsPlugin.cpp
// mDNS / DNS-SD service discovery (Bonjour / Zeroconf). One source file,
// three backends - same pattern as our TLS wrap split:
//   Linux   - Avahi (libavahi-client)
//   macOS   - Bonjour (<dns_sd.h>, system-provided)
//   Windows - Win32 DNS-SD (DnsServiceBrowse + DnsServiceResolve, Win10 1703+),
//             with a PTR-only fallback on anything older
//
// Implements IDirectoryProtocolPlugin::Search where the query parameters
// map to DNS-SD browse:
//   baseDn = service type to browse, e.g. "_http._tcp" or
//            "_ipp._tcp.local"
//   filter = (ignored - DNS-SD has no LDAP-style filter)
//   sizeLimit = max services to return before stopping the browse
//   timeLimitSeconds = how long to wait for responses (default 2s)
//
// Each returned UltraNetDirectoryEntry has:
//   dn                   = "<instance>.<type>.<domain>", instance unescaped
//   attributes["host"]   = resolved hostname
//   attributes["port"]   = TCP/UDP port
//   attributes["ip"]     = first-seen address (when resolution succeeds)
//   attributes["txt"]    = TXT record key=value entries (one per push_back)
//
// Browsing alone is not discovery. It returns names; a caller needs a host
// and a port, which is a second query on every platform. The Windows branch
// used to stop after the first one and hand back names nothing could connect
// to, so eSCL scanners were discoverable everywhere except there.
// Version: 0.2.0
// Last Modified: 2026-09-20
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include "MdnsNames.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
  #include <avahi-client/client.h>
  #include <avahi-client/lookup.h>
  #include <avahi-common/simple-watch.h>
  #include <avahi-common/malloc.h>
  #include <avahi-common/error.h>
#elif defined(__APPLE__)
  #include <dns_sd.h>
  #include <arpa/inet.h>
  #include <sys/select.h>
#elif defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <windns.h>
  #pragma comment(lib, "dnsapi.lib")
#endif

namespace {

// Browse session state — shared across the platform branches so the
// callback-style Avahi / Bonjour code can pile entries into one place.
struct BrowseState {
    std::mutex mu;
    std::vector<UltraNetDirectoryEntry> entries;
    int  sizeLimit = 0;
    bool stop = false;
};

#if defined(__linux__)
// ============================================================================
// Avahi backend (Linux)
// ============================================================================
struct AvahiCtx {
    AvahiSimplePoll* poll  = nullptr;
    AvahiClient*     client = nullptr;
    BrowseState*     state  = nullptr;
};

void OnResolved(AvahiServiceResolver* r, AvahiIfIndex, AvahiProtocol,
                AvahiResolverEvent event, const char* name,
                const char* type, const char* domain, const char* host_name,
                const AvahiAddress* address, uint16_t port,
                AvahiStringList* txt, AvahiLookupResultFlags, void* userdata) {
    auto* st = static_cast<BrowseState*>(userdata);
    if (event == AVAHI_RESOLVER_FOUND) {
        UltraNetDirectoryEntry e;
        e.dn = std::string(name) + "." + type + "." + domain;
        if (host_name) e.attributes["host"].push_back(host_name);
        e.attributes["port"].push_back(std::to_string(port));
        if (address) {
            char buf[AVAHI_ADDRESS_STR_MAX];
            avahi_address_snprint(buf, sizeof buf, address);
            e.attributes["ip"].push_back(buf);
        }
        for (auto* p = txt; p; p = avahi_string_list_get_next(p)) {
            char* key = nullptr; char* val = nullptr; size_t vlen = 0;
            if (avahi_string_list_get_pair(p, &key, &val, &vlen) == 0) {
                std::string entry(key ? key : "");
                if (val) { entry += '='; entry.append(val, vlen); }
                e.attributes["txt"].push_back(std::move(entry));
                avahi_free(key); avahi_free(val);
            }
        }
        std::lock_guard<std::mutex> lk(st->mu);
        st->entries.push_back(std::move(e));
        if (st->sizeLimit > 0 &&
            static_cast<int>(st->entries.size()) >= st->sizeLimit) {
            st->stop = true;
        }
    }
    avahi_service_resolver_free(r);
}

void OnBrowse(AvahiServiceBrowser*, AvahiIfIndex ifx, AvahiProtocol proto,
              AvahiBrowserEvent event, const char* name, const char* type,
              const char* domain, AvahiLookupResultFlags, void* userdata) {
    auto* ctx = static_cast<AvahiCtx*>(userdata);
    if (event == AVAHI_BROWSER_NEW) {
        avahi_service_resolver_new(ctx->client, ifx, proto,
                                   name, type, domain,
                                   AVAHI_PROTO_UNSPEC, static_cast<AvahiLookupFlags>(0),
                                   &OnResolved, ctx->state);
    }
}

bool RunAvahiBrowse(const std::string& serviceType, BrowseState& state,
                    int timeoutMs) {
    AvahiCtx ctx{};
    ctx.state = &state;
    ctx.poll  = avahi_simple_poll_new();
    if (!ctx.poll) return false;
    int err = 0;
    ctx.client = avahi_client_new(avahi_simple_poll_get(ctx.poll),
                                  AVAHI_CLIENT_NO_FAIL, nullptr, nullptr, &err);
    if (!ctx.client) {
        avahi_simple_poll_free(ctx.poll);
        return false;
    }
    auto* browser = avahi_service_browser_new(
        ctx.client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
        serviceType.c_str(), nullptr, static_cast<AvahiLookupFlags>(0),
        &OnBrowse, &ctx);
    if (!browser) {
        avahi_client_free(ctx.client);
        avahi_simple_poll_free(ctx.poll);
        return false;
    }
    // Drive the poll until timeout (or state.stop).
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (avahi_simple_poll_iterate(ctx.poll, 50) != 0) break;
        std::lock_guard<std::mutex> lk(state.mu);
        if (state.stop) break;
    }
    avahi_service_browser_free(browser);
    avahi_client_free(ctx.client);
    avahi_simple_poll_free(ctx.poll);
    return true;
}

#elif defined(__APPLE__)
// ============================================================================
// Bonjour backend (macOS)
// ============================================================================
struct BonjourPending {
    BrowseState* state;
    std::string  type;
};

void DNSSD_API OnResolveReply(DNSServiceRef r, DNSServiceFlags,
                              uint32_t, DNSServiceErrorType err,
                              const char* fullname, const char* hosttarget,
                              uint16_t portNet, uint16_t txtLen,
                              const unsigned char* txt, void* ctx) {
    auto* p = static_cast<BonjourPending*>(ctx);
    if (err == kDNSServiceErr_NoError) {
        UltraNetDirectoryEntry e;
        e.dn = fullname ? fullname : "";
        if (hosttarget) e.attributes["host"].push_back(hosttarget);
        e.attributes["port"].push_back(std::to_string(ntohs(portNet)));
        // Walk TXT entries — Bonjour delivers them packed.
        for (uint16_t i = 0; i < txtLen; ) {
            const uint8_t segLen = txt[i++];
            if (i + segLen > txtLen) break;
            e.attributes["txt"].emplace_back(
                reinterpret_cast<const char*>(txt + i), segLen);
            i += segLen;
        }
        std::lock_guard<std::mutex> lk(p->state->mu);
        p->state->entries.push_back(std::move(e));
        if (p->state->sizeLimit > 0 &&
            static_cast<int>(p->state->entries.size()) >= p->state->sizeLimit) {
            p->state->stop = true;
        }
    }
    DNSServiceRefDeallocate(r);
}

void DNSSD_API OnBrowseReply(DNSServiceRef, DNSServiceFlags flags,
                             uint32_t interfaceIndex, DNSServiceErrorType err,
                             const char* serviceName, const char* regtype,
                             const char* replyDomain, void* ctx) {
    auto* p = static_cast<BonjourPending*>(ctx);
    if (err != kDNSServiceErr_NoError) return;
    if (!(flags & kDNSServiceFlagsAdd)) return;

    DNSServiceRef r = nullptr;
    if (DNSServiceResolve(&r, 0, interfaceIndex, serviceName, regtype,
                          replyDomain, &OnResolveReply, ctx)
        == kDNSServiceErr_NoError) {
        // Process the single resolution synchronously: select on the fd.
        const int fd = DNSServiceRefSockFD(r);
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        timeval tv{1, 0};
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
            DNSServiceProcessResult(r);
        } else {
            DNSServiceRefDeallocate(r);
        }
    }
}

bool RunBonjourBrowse(const std::string& serviceType, BrowseState& state,
                      int timeoutMs) {
    BonjourPending pending{&state, serviceType};
    DNSServiceRef browser = nullptr;
    if (DNSServiceBrowse(&browser, 0, 0,
                         serviceType.c_str(), nullptr,
                         &OnBrowseReply, &pending) != kDNSServiceErr_NoError) {
        return false;
    }
    const int fd = DNSServiceRefSockFD(browser);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        timeval tv{0, 50000};   // 50 ms
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
            DNSServiceProcessResult(browser);
        }
        std::lock_guard<std::mutex> lk(state.mu);
        if (state.stop) break;
    }
    DNSServiceRefDeallocate(browser);
    return true;
}

#elif defined(_WIN32) || defined(_WIN64)
// ============================================================================
// Win32 DNS-SD backend (Windows 10 1703+) - DnsServiceBrowse + DnsServiceResolve
// ============================================================================
// Discovery is two asynchronous calls, each answering on a DNS worker thread:
// the browse returns PTR records naming the instances, and a resolve turns one
// of those names into a host, a port and its TXT keys. Both are needed. A name
// with no address behind it is not something a caller can connect to, and
// handing one back - which is all this backend used to do - looks like
// discovery working while nothing can actually be reached.
//
// The four entry points are bound at run time rather than imported. They
// arrived in Windows 10 1703; importing them would stop this DLL loading at
// all on anything older, and taking the plug-in down is a worse answer than
// the PTR-only fallback further down, which still works there.

// The name arithmetic lives next door, free of windows.h so a test can
// drive it. See MdnsNames.h.
namespace Mdns = UltraCanvas::Mdns;

std::string Utf8FromWide(const wchar_t* text) {
    if (!text || !*text) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring WideFromUtf8(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), len);
    return out;
}

struct DnsSdApi {
    using BrowseFn        = DNS_STATUS (WINAPI*)(PDNS_SERVICE_BROWSE_REQUEST,
                                                 PDNS_SERVICE_CANCEL);
    using BrowseCancelFn  = DNS_STATUS (WINAPI*)(PDNS_SERVICE_CANCEL);
    using ResolveFn       = DNS_STATUS (WINAPI*)(PDNS_SERVICE_RESOLVE_REQUEST,
                                                 PDNS_SERVICE_CANCEL);
    using ResolveCancelFn = DNS_STATUS (WINAPI*)(PDNS_SERVICE_CANCEL);
    using FreeInstanceFn  = VOID (WINAPI*)(PDNS_SERVICE_INSTANCE);

    BrowseFn        Browse        = nullptr;
    BrowseCancelFn  BrowseCancel  = nullptr;
    ResolveFn       Resolve       = nullptr;
    ResolveCancelFn ResolveCancel = nullptr;
    FreeInstanceFn  FreeInstance  = nullptr;

    bool Ready() const {
        return Browse && BrowseCancel && Resolve && ResolveCancel && FreeInstance;
    }
};

const DnsSdApi& GetDnsSdApi() {
    // Resolved once. The module handle is deliberately not released: dnsapi is
    // loaded into every process that touches DNS anyway, and dropping it while
    // a cancelled callback might still be unwinding would be the one way this
    // could crash.
    static const DnsSdApi api = [] {
        DnsSdApi bound;
        HMODULE dll = LoadLibraryW(L"dnsapi.dll");
        if (!dll) return bound;
        auto get = [dll](const char* name) {
            return reinterpret_cast<void*>(GetProcAddress(dll, name));
        };
        bound.Browse = reinterpret_cast<DnsSdApi::BrowseFn>(get("DnsServiceBrowse"));
        bound.BrowseCancel =
            reinterpret_cast<DnsSdApi::BrowseCancelFn>(get("DnsServiceBrowseCancel"));
        bound.Resolve = reinterpret_cast<DnsSdApi::ResolveFn>(get("DnsServiceResolve"));
        bound.ResolveCancel =
            reinterpret_cast<DnsSdApi::ResolveCancelFn>(get("DnsServiceResolveCancel"));
        bound.FreeInstance =
            reinterpret_cast<DnsSdApi::FreeInstanceFn>(get("DnsServiceFreeInstance"));
        return bound;
    }();
    return api;
}

// ---- browse ----------------------------------------------------------------

struct WinBrowseCtx {
    std::mutex mu;
    std::vector<std::string> names;   // escaped wire names, as they arrived
    HANDLE   done = nullptr;
    int      sizeLimit = 0;
};

void WINAPI OnWindowsBrowse(DWORD /*status*/, PVOID context, PDNS_RECORD records) {
    auto* ctx = static_cast<WinBrowseCtx*>(context);
    if (!ctx) return;
    bool enough = false;
    if (records) {
        std::lock_guard<std::mutex> lk(ctx->mu);
        for (PDNS_RECORD r = records; r; r = r->pNext) {
            // These came from the wide API, so every record is a DNS_RECORDW
            // even in a build without UNICODE, where PDNS_RECORD would
            // otherwise mean the ANSI variant with char* fields.
            const DNS_RECORDW* rw = reinterpret_cast<const DNS_RECORDW*>(r);
            if (rw->wType != DNS_TYPE_PTR || !rw->Data.PTR.pNameHost) continue;
            std::string name = Utf8FromWide(rw->Data.PTR.pNameHost);
            if (name.empty()) continue;
            // A responder may answer more than once for the same service.
            if (std::find(ctx->names.begin(), ctx->names.end(), name) !=
                ctx->names.end()) {
                continue;
            }
            ctx->names.push_back(std::move(name));
        }
        enough = ctx->sizeLimit > 0 &&
                 static_cast<int>(ctx->names.size()) >= ctx->sizeLimit;
        DnsRecordListFree(records, DnsFreeRecordList);
    }
    // Only wake the caller early once there is nothing more worth waiting for.
    // A browse has no natural end - instances keep arriving for as long as it
    // runs - so otherwise the caller's timeout is what stops it.
    if (enough && ctx->done) SetEvent(ctx->done);
}

// ---- resolve ---------------------------------------------------------------

struct WinResolveCtx {
    std::mutex mu;
    bool        resolved = false;
    std::string host;
    uint16_t    port = 0;
    std::vector<std::string> txt;
    std::string ip;
    HANDLE      done = nullptr;
};

void WINAPI OnWindowsResolve(DWORD status, PVOID context,
                             PDNS_SERVICE_INSTANCE instance) {
    auto* ctx = static_cast<WinResolveCtx*>(context);
    if (ctx && status == ERROR_SUCCESS && instance) {
        std::lock_guard<std::mutex> lk(ctx->mu);
        ctx->host = Mdns::TrimTrailingDot(Utf8FromWide(instance->pszHostName));
        ctx->port = instance->wPort;
        for (DWORD i = 0; i < instance->dwPropertyCount; ++i) {
            if (!instance->keys || !instance->keys[i]) continue;
            const std::string key = Utf8FromWide(instance->keys[i]);
            if (key.empty()) continue;
            if (instance->values && instance->values[i]) {
                // An empty value is a real value, so the wide string is
                // converted and passed even when it comes out empty: that is
                // "key=", which is not the same record as a bare "key".
                const std::string value = Utf8FromWide(instance->values[i]);
                ctx->txt.push_back(Mdns::TxtPair(key, value.c_str()));
            } else {
                ctx->txt.push_back(Mdns::TxtPair(key, nullptr));
            }
        }
        // IPv4 first, to match what the other two backends report when a
        // device answers on both.
        if (instance->ip4Address) {
            ctx->ip = Mdns::IPv4ToString(
                static_cast<uint32_t>(*instance->ip4Address));
        } else if (instance->ip6Address) {
            ctx->ip = Mdns::IPv6ToString(
                reinterpret_cast<const uint8_t*>(instance->ip6Address));
        }
        ctx->resolved = true;
    }
    if (instance) {
        const DnsSdApi& api = GetDnsSdApi();
        if (api.FreeInstance) api.FreeInstance(instance);
    }
    if (ctx && ctx->done) SetEvent(ctx->done);
}

// One instance, turned into a host and a port. Returns false when the resolve
// did not answer in time, which leaves the instance out rather than reporting
// a service with nowhere to connect.
bool ResolveOneInstance(const DnsSdApi& api, const std::string& fullName,
                        int timeoutMs, WinResolveCtx& out) {
    // The escaped name, exactly as the browse gave it. Unescaping first would
    // ask about a different name - see Mdns::ResolveNameFor.
    std::wstring query = WideFromUtf8(Mdns::ResolveNameFor(fullName));
    if (query.empty()) return false;

    out.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!out.done) return false;

    DNS_SERVICE_RESOLVE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = query.data();      // the API wants it mutable
    request.pResolveCompletionCallback = &OnWindowsResolve;
    request.pQueryContext = &out;

    DNS_SERVICE_CANCEL cancel{};
    const DNS_STATUS status = api.Resolve(&request, &cancel);
    if (status != DNS_REQUEST_PENDING) {
        CloseHandle(out.done);
        out.done = nullptr;
        return false;
    }
    const DWORD waited = WaitForSingleObject(out.done,
                                             static_cast<DWORD>(timeoutMs));
    if (waited != WAIT_OBJECT_0) {
        // Cancel is the synchronisation point: after it returns the callback
        // will not run again, so the stack context below is safe to drop.
        api.ResolveCancel(&cancel);
    }
    CloseHandle(out.done);
    out.done = nullptr;

    std::lock_guard<std::mutex> lk(out.mu);
    return out.resolved;
}

// ---- the pre-1703 fallback -------------------------------------------------

// One multicast PTR query, which is all the older DNS API offers. It names the
// services but cannot say where they are, so the entries carry no host or
// port and a caller that needs one will skip them. Kept because naming them is
// still better than reporting none, and because it is what runs on Windows 8.
bool RunWindowsPtrOnlyBrowse(const std::string& serviceType, BrowseState& state) {
    const std::wstring query = WideFromUtf8(Mdns::BrowseQueryName(serviceType));
    if (query.empty()) return false;

    PDNS_RECORD records = nullptr;
    const DNS_STATUS status = DnsQuery_W(query.c_str(), DNS_TYPE_PTR,
                                         DNS_QUERY_MULTICAST_ONLY,
                                         nullptr, &records, nullptr);
    if (status != 0 || !records) return false;
    for (PDNS_RECORD r = records; r; r = r->pNext) {
        const DNS_RECORDW* rw = reinterpret_cast<const DNS_RECORDW*>(r);
        if (rw->wType != DNS_TYPE_PTR || !rw->Data.PTR.pNameHost) continue;
        const std::string fullName = Utf8FromWide(rw->Data.PTR.pNameHost);
        if (fullName.empty()) continue;

        UltraNetDirectoryEntry e;
        std::string instance, domain;
        if (Mdns::SplitInstanceName(fullName, serviceType, instance, domain)) {
            e.dn = instance + "." + Mdns::ServiceTypeOnly(serviceType);
            if (!domain.empty()) e.dn += "." + domain;
        } else {
            e.dn = fullName;
        }
        std::lock_guard<std::mutex> lk(state.mu);
        state.entries.push_back(std::move(e));
        if (state.sizeLimit > 0 &&
            static_cast<int>(state.entries.size()) >= state.sizeLimit) break;
    }
    DnsRecordListFree(records, DnsFreeRecordList);
    return true;
}

// ---- browse then resolve ---------------------------------------------------

bool RunWindowsBrowse(const std::string& serviceType, BrowseState& state,
                      int timeoutMs) {
    const DnsSdApi& api = GetDnsSdApi();
    if (!api.Ready()) return RunWindowsPtrOnlyBrowse(serviceType, state);

    const std::wstring query = WideFromUtf8(Mdns::BrowseQueryName(serviceType));
    if (query.empty()) return false;

    WinBrowseCtx browse;
    browse.sizeLimit = state.sizeLimit;
    browse.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!browse.done) return false;

    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;             // every interface
    request.QueryName = query.c_str();
    request.pBrowseCallback = &OnWindowsBrowse;
    request.pQueryContext = &browse;

    DNS_SERVICE_CANCEL cancel{};
    if (api.Browse(&request, &cancel) != DNS_REQUEST_PENDING) {
        CloseHandle(browse.done);
        // The browse never started, so fall back rather than report nothing.
        return RunWindowsPtrOnlyBrowse(serviceType, state);
    }

    // Two thirds of the budget listening for instances, a third to resolve
    // them. Resolving is the cheaper half - the responder that just answered
    // the browse answers the resolve out of the same cache - so this leaves
    // the listening as long as it can be.
    const int browseMs = (timeoutMs * 2) / 3 > 0 ? (timeoutMs * 2) / 3 : 1;
    WaitForSingleObject(browse.done, static_cast<DWORD>(browseMs));
    api.BrowseCancel(&cancel);
    CloseHandle(browse.done);
    browse.done = nullptr;

    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lk(browse.mu);
        names = browse.names;
    }
    if (names.empty()) return true;   // nothing on the network is not a failure

    const int resolveBudget = timeoutMs - browseMs;
    const int perResolveMs =
        std::max(500, resolveBudget / static_cast<int>(names.size()));

    for (const std::string& fullName : names) {
        WinResolveCtx resolved;
        if (!ResolveOneInstance(api, fullName, perResolveMs, resolved)) continue;
        if (resolved.host.empty()) continue;

        UltraNetDirectoryEntry e;
        std::string instance, domain;
        if (Mdns::SplitInstanceName(fullName, serviceType, instance, domain)) {
            e.dn = instance + "." + Mdns::ServiceTypeOnly(serviceType);
            if (!domain.empty()) e.dn += "." + domain;
        } else {
            e.dn = fullName;
        }
        e.attributes["host"].push_back(resolved.host);
        e.attributes["port"].push_back(std::to_string(resolved.port));
        if (!resolved.ip.empty()) e.attributes["ip"].push_back(resolved.ip);
        for (std::string& record : resolved.txt) {
            e.attributes["txt"].push_back(std::move(record));
        }

        std::lock_guard<std::mutex> lk(state.mu);
        state.entries.push_back(std::move(e));
        if (state.sizeLimit > 0 &&
            static_cast<int>(state.entries.size()) >= state.sizeLimit) break;
    }
    return true;
}

#endif


class MdnsPlugin : public IDirectoryProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-mDNS"; }
    std::string GetVersion() const override { return "0.2.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"mdns", "dns-sd"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult Search(const std::string& /*url*/,
                          const UltraNetDirectoryQuery& q,
                          std::vector<UltraNetDirectoryEntry>& out) override {
        out.clear();
        if (q.baseDn.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                "baseDn (DNS-SD service type, e.g. \"_http._tcp\") is required");
        }
        const int timeoutMs = q.timeLimitSeconds > 0
                                  ? q.timeLimitSeconds * 1000 : 2000;
        BrowseState state{};
        state.sizeLimit = q.sizeLimit;

        bool ok = false;
#if defined(__linux__)
        ok = RunAvahiBrowse(q.baseDn, state, timeoutMs);
#elif defined(__APPLE__)
        ok = RunBonjourBrowse(q.baseDn, state, timeoutMs);
#elif defined(_WIN32) || defined(_WIN64)
        ok = RunWindowsBrowse(q.baseDn, state, timeoutMs);
#else
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "mDNS plug-in: no backend for this platform");
#endif
        if (!ok && state.entries.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                         "DNS-SD browse failed to start");
        }
        out = std::move(state.entries);
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<MdnsPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<MdnsPlugin>());
}
#endif
