// UltraCanvas/Plugins/UltraNet/mdns/MdnsPlugin.cpp
// mDNS / DNS-SD service discovery (Bonjour / Zeroconf). One source file,
// three backends — same pattern as our TLS wrap split:
//   Linux   - Avahi (libavahi-client)
//   macOS   - Bonjour (<dns_sd.h>, system-provided)
//   Windows - Win32 DNS-SD API (DnsServiceBrowse from dnsapi.dll, Win10+)
//
// Implements IDirectoryProtocolPlugin::Search where the query parameters
// map to DNS-SD browse:
//   baseDn = service type to browse, e.g. "_http._tcp" or
//            "_ipp._tcp.local"
//   filter = (ignored — DNS-SD has no LDAP-style filter)
//   sizeLimit = max services to return before stopping the browse
//   timeLimitSeconds = how long to wait for responses (default 2s)
//
// Each returned UltraNetDirectoryEntry has:
//   dn                   = "<instance>.<type>.<domain>"
//   attributes["host"]   = resolved hostname
//   attributes["port"]   = TCP/UDP port
//   attributes["ip"]     = first-seen IPv4 (when resolution succeeds)
//   attributes["txt"]    = TXT record key=value entries (one per push_back)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <chrono>
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
// Win32 DNS-SD backend (Windows 10+) — DnsServiceBrowse + DnsServiceResolve
// ============================================================================
bool RunWindowsBrowse(const std::string& serviceType, BrowseState& state,
                      int timeoutMs) {
    // DnsServiceBrowse is callback-based and event-loop-driven; for v0.1
    // we go through the lower-level DnsQuery_W for PTR records on
    // "_<service>._tcp.local" and stop there (no resolution).
    std::wstring wsvc(serviceType.begin(), serviceType.end());
    wsvc += L".local";

    PDNS_RECORD recs = nullptr;
    const DNS_STATUS status = DnsQuery_W(wsvc.c_str(), DNS_TYPE_PTR,
                                         DNS_QUERY_MULTICAST_ONLY,
                                         nullptr, &recs, nullptr);
    (void)timeoutMs;
    if (status != 0 || !recs) return false;
    for (PDNS_RECORD r = recs; r; r = r->pNext) {
        if (r->wType != DNS_TYPE_PTR || !r->Data.PTR.pNameHost) continue;
        UltraNetDirectoryEntry e;
        // pNameHost is a wide string; convert.
        const wchar_t* w = r->Data.PTR.pNameHost;
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1,
                                      nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len,
                                         nullptr, nullptr);
        e.dn = std::move(s);
        std::lock_guard<std::mutex> lk(state.mu);
        state.entries.push_back(std::move(e));
        if (state.sizeLimit > 0 &&
            static_cast<int>(state.entries.size()) >= state.sizeLimit) break;
    }
    DnsRecordListFree(recs, DnsFreeRecordList);
    return true;
}

#endif

class MdnsPlugin : public IDirectoryProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-mDNS"; }
    std::string GetVersion() const override { return "0.1.0"; }
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
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<MdnsPlugin>());
}
