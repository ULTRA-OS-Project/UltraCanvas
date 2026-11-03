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
//   attributes["ip"]     = every address the service answered from, IPv4
//                          first (empty when resolution found none). A
//                          service answered on several interfaces or over
//                          both IP versions is one entry with all of them.
//   attributes["txt"]    = TXT record key=value entries (one per push_back)
//
// And IServiceDiscoveryPlugin::Advertise announces a service of our own
// until StopAdvertising: Avahi's entry group on a threaded poll, Bonjour's
// DNSServiceRegister serviced on a thread, Windows' DnsServiceRegister
// (1703+, bound at run time like the browse calls). A name another host
// already uses is renamed by the backend.
//
// Browsing alone is not discovery. It returns names; a caller needs a host
// and a port, which is a second query on every platform. The Windows branch
// used to stop after the first one and hand back names nothing could connect
// to, so eSCL scanners were discoverable everywhere except there.
// Version: 0.4.0 - IServiceDiscoveryPlugin::Advertise / StopAdvertising (Avahi
//                  entry group, Bonjour DNSServiceRegister, DnsServiceRegister)
// Version: 0.3.2 - every entry point is guarded (UltraNetPluginGuard.h): an
//                  exception becomes a PluginError result or an invalid
//                  handle, never ends the process
// Version: 0.3.1
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include "UltraNetPluginGuard.h"
#include "UltraNetPluginHostShim.h"

#include "MdnsNames.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
  #include <avahi-client/client.h>
  #include <avahi-client/lookup.h>
  #include <avahi-client/publish.h>
  #include <avahi-common/alternative.h>
  #include <avahi-common/simple-watch.h>
  #include <avahi-common/thread-watch.h>
  #include <avahi-common/malloc.h>
  #include <avahi-common/error.h>
#elif defined(__APPLE__)
  #include <dns_sd.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
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

// Adds one answer. A service answered before - on another interface, or over
// the other IP version - is not a second service: its addresses join the
// first answer's (Mdns::MergeAnswer), so a caller sees every address one
// service has. The caller holds state.mu.
void AddAnswer(BrowseState& state, UltraNetDirectoryEntry&& entry) {
    if (!entry.dn.empty()) {
        auto same = std::find_if(state.entries.begin(), state.entries.end(),
                                 [&](const UltraNetDirectoryEntry& e) { return e.dn == entry.dn; });
        if (same != state.entries.end()) {
            UltraCanvas::Mdns::MergeAnswer(same->attributes, entry.attributes);
            return;
        }
    }
    auto ip = entry.attributes.find("ip");
    if (ip != entry.attributes.end()) {
        std::vector<std::string> ordered;
        for (const std::string& address : ip->second) {
            UltraCanvas::Mdns::AddAddress(ordered, address);
        }
        ip->second = std::move(ordered);
    }
    state.entries.push_back(std::move(entry));
    if (state.sizeLimit > 0 && static_cast<int>(state.entries.size()) >= state.sizeLimit) {
        state.stop = true;
    }
}

// One announced service, whatever the backend. Stop() withdraws it and
// waits for the backend's thread; the destructor stops it too.
class Advertiser {
public:
    virtual ~Advertiser() = default;
    virtual void Stop() = 0;
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
        AddAnswer(*st, std::move(e));
    }
    avahi_service_resolver_free(r);
}

void OnBrowse(AvahiServiceBrowser*, AvahiIfIndex ifx, AvahiProtocol proto,
              AvahiBrowserEvent event, const char* name, const char* type,
              const char* domain, AvahiLookupResultFlags, void* userdata) {
    auto* ctx = static_cast<AvahiCtx*>(userdata);
    if (event == AVAHI_BROWSER_NEW) {
        // The address asked for is the one of the protocol the service was
        // seen over: Avahi reports a service once per interface and IP
        // version, so a printer on both gets an A and an AAAA lookup and
        // AddAnswer folds the two into one entry. Asking for either family
        // (AVAHI_PROTO_UNSPEC) would give the same address twice.
        avahi_service_resolver_new(ctx->client, ifx, proto,
                                   name, type, domain,
                                   proto, static_cast<AvahiLookupFlags>(0),
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

// ---- advertising ------------------------------------------------------------
// A threaded poll per advertisement: the client reconnects to the daemon by
// itself (AVAHI_CLIENT_NO_FAIL), and every time it is running the entry group
// is (re)built; a name clash takes Avahi's alternative name ("Alice #2").

class AvahiAdvertiser final : public Advertiser {
public:
    explicit AvahiAdvertiser(const UltraNetServiceAdvertisement& ad) : ad_(ad), name_(ad.instanceName) {}
    ~AvahiAdvertiser() override { Stop(); }

    bool Start(std::string& error) {
        poll_ = avahi_threaded_poll_new();
        if (!poll_) {
            error = "Avahi: no poll";
            return false;
        }
        int err = 0;
        // The callback can run inside avahi_client_new, before it returns:
        // it records the client itself.
        client_ = avahi_client_new(avahi_threaded_poll_get(poll_), AVAHI_CLIENT_NO_FAIL, &OnClient, this, &err);
        if (!client_) {
            error = std::string("Avahi: ") + avahi_strerror(err);
            avahi_threaded_poll_free(poll_);
            poll_ = nullptr;
            return false;
        }
        if (avahi_threaded_poll_start(poll_) < 0) {
            error = "Avahi: the poll thread did not start";
            avahi_client_free(client_);
            avahi_threaded_poll_free(poll_);
            client_ = nullptr;
            poll_ = nullptr;
            return false;
        }
        return true;
    }

    void Stop() override {
        if (!poll_) return;
        avahi_threaded_poll_stop(poll_);
        if (group_) avahi_entry_group_free(group_);
        if (client_) avahi_client_free(client_);
        avahi_threaded_poll_free(poll_);
        group_ = nullptr;
        client_ = nullptr;
        poll_ = nullptr;
    }

private:
    // On the poll thread (or inside avahi_client_new).
    static void OnClient(AvahiClient* client, AvahiClientState state, void* userdata) {
        auto* self = static_cast<AvahiAdvertiser*>(userdata);
        self->client_ = client;
        switch (state) {
            case AVAHI_CLIENT_S_RUNNING:
                self->Publish();
                break;
            case AVAHI_CLIENT_S_COLLISION:
            case AVAHI_CLIENT_S_REGISTERING:
                // The host name changes: withdraw, and publish again once running.
                if (self->group_) avahi_entry_group_reset(self->group_);
                break;
            case AVAHI_CLIENT_FAILURE:
                // The daemon went away. The group dies with the connection;
                // NO_FAIL brings the client back and S_RUNNING republishes.
                if (self->group_) {
                    avahi_entry_group_free(self->group_);
                    self->group_ = nullptr;
                }
                break;
            default:
                break;
        }
    }

    static void OnGroup(AvahiEntryGroup* group, AvahiEntryGroupState state, void* userdata) {
        auto* self = static_cast<AvahiAdvertiser*>(userdata);
        self->group_ = group;
        if (state == AVAHI_ENTRY_GROUP_COLLISION) {
            self->Rename();
            avahi_entry_group_reset(group);
            self->Publish();
        }
    }

    void Rename() {
        char* next = avahi_alternative_service_name(name_.c_str());
        if (next) {
            name_ = next;
            avahi_free(next);
        }
    }

    void Publish() {
        if (!client_) return;
        if (!group_) group_ = avahi_entry_group_new(client_, &OnGroup, this);
        if (!group_ || !avahi_entry_group_is_empty(group_)) return;
        for (int attempt = 0; attempt < 10; ++attempt) {
            AvahiStringList* txt = nullptr;
            for (const std::string& record : ad_.txt) txt = avahi_string_list_add(txt, record.c_str());
            const int r = avahi_entry_group_add_service_strlst(
                group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, static_cast<AvahiPublishFlags>(0), name_.c_str(),
                ad_.serviceType.c_str(), nullptr, nullptr, static_cast<uint16_t>(ad_.port), txt);
            avahi_string_list_free(txt);
            if (r == AVAHI_ERR_COLLISION) {
                Rename();
                avahi_entry_group_reset(group_);
                continue;
            }
            if (r >= 0) avahi_entry_group_commit(group_);
            return;
        }
    }

    UltraNetServiceAdvertisement ad_;
    std::string name_;
    AvahiThreadedPoll* poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;
};

std::unique_ptr<Advertiser> StartAdvertiser(const UltraNetServiceAdvertisement& ad, std::string& error) {
    auto advertiser = std::make_unique<AvahiAdvertiser>(ad);
    if (!advertiser->Start(error)) return nullptr;
    return advertiser;
}

#elif defined(__APPLE__)
// ============================================================================
// Bonjour backend (macOS)
// ============================================================================
struct BonjourPending {
    BrowseState* state;
    std::string  type;
};

// The addresses behind a resolved host. A resolve gives a host name and no
// address, so this asks for both families with one DNSServiceGetAddrInfo.
struct AddressLookup {
    std::vector<std::string> addresses;
    bool moreComing = true;
};

void DNSSD_API OnAddressReply(DNSServiceRef, DNSServiceFlags flags, uint32_t,
                              DNSServiceErrorType err, const char*,
                              const struct sockaddr* address, uint32_t, void* ctx) {
    auto* lookup = static_cast<AddressLookup*>(ctx);
    lookup->moreComing = (flags & kDNSServiceFlagsMoreComing) != 0;
    if (err != kDNSServiceErr_NoError || !address || !(flags & kDNSServiceFlagsAdd)) return;
    char text[INET6_ADDRSTRLEN] = {};
    if (address->sa_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(address)->sin_addr,
                  text, sizeof text);
    } else if (address->sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr,
                  text, sizeof text);
    }
    if (text[0]) UltraCanvas::Mdns::AddAddress(lookup->addresses, text);
}

std::vector<std::string> LookUpAddresses(uint32_t interfaceIndex, const char* host) {
    if (!host || !*host) return {};
    AddressLookup lookup;
    DNSServiceRef ref = nullptr;
    if (DNSServiceGetAddrInfo(&ref, 0, interfaceIndex,
                              kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, host,
                              &OnAddressReply, &lookup) != kDNSServiceErr_NoError) {
        return {};
    }
    // At most a second for the first address, then a short grace for the
    // other family: a responder answers A and AAAA from the same cache, and a
    // host with no IPv6 never sends the second, so waiting it out in full
    // would cost every IPv4-only printer the whole second.
    const int fd = DNSServiceRefSockFD(ref);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    bool graceSet = false;
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        timeval tv{0, 50000};
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
            if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError) break;
        }
        if (!lookup.addresses.empty() && !graceSet) {
            deadline = std::min(deadline, std::chrono::steady_clock::now() +
                                              std::chrono::milliseconds(150));
            graceSet = true;
        }
    }
    DNSServiceRefDeallocate(ref);
    return lookup.addresses;
}

void DNSSD_API OnResolveReply(DNSServiceRef r, DNSServiceFlags,
                              uint32_t interfaceIndex, DNSServiceErrorType err,
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
        // This backend used to report no address at all, so a caller could
        // compare nothing but the host name.
        for (const std::string& address : LookUpAddresses(interfaceIndex, hosttarget)) {
            e.attributes["ip"].push_back(address);
        }
        std::lock_guard<std::mutex> lk(p->state->mu);
        AddAnswer(*p->state, std::move(e));
    }
    DNSServiceRefDeallocate(r);
}

void DNSSD_API OnBrowseReply(DNSServiceRef, DNSServiceFlags flags,
                             uint32_t interfaceIndex, DNSServiceErrorType err,
                             const char* serviceName, const char* regtype,
                             const char* replyDomain, void* ctx) {
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

// ---- advertising ------------------------------------------------------------
// DNSServiceRegister renames on a clash by itself (no NoAutoRename flag);
// its socket is serviced on a thread until Stop.

class BonjourAdvertiser final : public Advertiser {
public:
    ~BonjourAdvertiser() override { Stop(); }

    bool Start(const UltraNetServiceAdvertisement& ad, std::string& error) {
        // TXT: each record one length byte and its bytes.
        std::vector<unsigned char> txt;
        for (const std::string& record : ad.txt) {
            const size_t n = std::min<size_t>(record.size(), 255);
            txt.push_back(static_cast<unsigned char>(n));
            txt.insert(txt.end(), record.begin(), record.begin() + static_cast<std::ptrdiff_t>(n));
        }
        if (txt.empty()) txt.push_back(0);      // an empty TXT record is one zero byte
        const DNSServiceErrorType err = DNSServiceRegister(
            &ref_, 0, 0, ad.instanceName.c_str(), ad.serviceType.c_str(), nullptr, nullptr,
            htons(static_cast<uint16_t>(ad.port)), static_cast<uint16_t>(txt.size()), txt.data(), nullptr, nullptr);
        if (err != kDNSServiceErr_NoError) {
            error = "Bonjour: DNSServiceRegister failed (" + std::to_string(err) + ")";
            ref_ = nullptr;
            return false;
        }
        thread_ = std::thread([this]() {
            const int fd = DNSServiceRefSockFD(ref_);
            while (!stop_) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(fd, &fds);
                timeval tv{0, 200000};
                if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
                    if (DNSServiceProcessResult(ref_) != kDNSServiceErr_NoError) break;
                }
            }
        });
        return true;
    }

    void Stop() override {
        if (!ref_) return;
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        DNSServiceRefDeallocate(ref_);
        ref_ = nullptr;
    }

private:
    DNSServiceRef ref_ = nullptr;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

std::unique_ptr<Advertiser> StartAdvertiser(const UltraNetServiceAdvertisement& ad, std::string& error) {
    auto advertiser = std::make_unique<BonjourAdvertiser>();
    if (!advertiser->Start(ad, error)) return nullptr;
    return advertiser;
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
    using ConstructFn     = PDNS_SERVICE_INSTANCE (WINAPI*)(PCWSTR, PCWSTR, PIP4_ADDRESS, PIP6_ADDRESS, WORD,
                                                            WORD, WORD, DWORD, PCWSTR*, PCWSTR*);
    using RegisterFn      = DWORD (WINAPI*)(PDNS_SERVICE_REGISTER_REQUEST, PDNS_SERVICE_CANCEL);
    using DeRegisterFn    = DWORD (WINAPI*)(PDNS_SERVICE_REGISTER_REQUEST, PDNS_SERVICE_CANCEL);

    BrowseFn        Browse        = nullptr;
    BrowseCancelFn  BrowseCancel  = nullptr;
    ResolveFn       Resolve       = nullptr;
    ResolveCancelFn ResolveCancel = nullptr;
    FreeInstanceFn  FreeInstance  = nullptr;
    ConstructFn     Construct     = nullptr;
    RegisterFn      Register      = nullptr;
    DeRegisterFn    DeRegister    = nullptr;

    bool Ready() const {
        return Browse && BrowseCancel && Resolve && ResolveCancel && FreeInstance;
    }
    bool CanRegister() const { return Construct && Register && DeRegister && FreeInstance; }
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
        bound.Construct = reinterpret_cast<DnsSdApi::ConstructFn>(get("DnsServiceConstructInstance"));
        bound.Register = reinterpret_cast<DnsSdApi::RegisterFn>(get("DnsServiceRegister"));
        bound.DeRegister = reinterpret_cast<DnsSdApi::DeRegisterFn>(get("DnsServiceDeRegister"));
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
    std::vector<std::string> ips;
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
        // Both, when the device answered on both: a caller matching a
        // printer against an address it knows by one family must not miss it
        // because the other was reported. AddAddress puts IPv4 first.
        if (instance->ip4Address) {
            Mdns::AddAddress(ctx->ips, Mdns::IPv4ToString(
                static_cast<uint32_t>(*instance->ip4Address)));
        }
        if (instance->ip6Address) {
            Mdns::AddAddress(ctx->ips, Mdns::IPv6ToString(
                reinterpret_cast<const uint8_t*>(instance->ip6Address)));
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
        for (const std::string& address : resolved.ips) e.attributes["ip"].push_back(address);
        for (std::string& record : resolved.txt) {
            e.attributes["txt"].push_back(std::move(record));
        }

        std::lock_guard<std::mutex> lk(state.mu);
        AddAnswer(state, std::move(e));
        if (state.stop) break;
    }
    return true;
}

// ---- advertising ------------------------------------------------------------
// DnsServiceRegister announces an instance named "<name>.<type>.local" on
// this computer's "<computer>.local" host; DnsServiceDeRegister withdraws it.
// Both complete asynchronously; the registration is kept until Stop.

// The register and the deregister both complete here (on a DNS thread),
// with a copy of the instance that is ours to free; `context` is the
// advertiser's completion event.
void WINAPI OnWindowsRegistered(DWORD, PVOID context, PDNS_SERVICE_INSTANCE instance) {
    if (instance) {
        const DnsSdApi& api = GetDnsSdApi();
        if (api.FreeInstance) api.FreeInstance(instance);
    }
    if (context) SetEvent(static_cast<HANDLE>(context));
}

class WindowsAdvertiser final : public Advertiser {
public:
    ~WindowsAdvertiser() override { Stop(); }

    bool Start(const UltraNetServiceAdvertisement& ad, std::string& error) {
        const DnsSdApi& api = GetDnsSdApi();
        if (!api.CanRegister()) {
            error = "DNS-SD registration needs Windows 10 1703 or later";
            return false;
        }
        wchar_t computer[256] = {0};
        DWORD size = 256;
        if (!GetComputerNameExW(ComputerNameDnsHostname, computer, &size) || size == 0) {
            error = "No computer name";
            return false;
        }
        const std::string type = Mdns::ServiceTypeOnly(ad.serviceType);
        instanceName_ = WideFromUtf8(ad.instanceName + "." + type + ".local");
        hostName_ = std::wstring(computer) + L".local";
        std::vector<std::wstring> keys, values;
        for (const std::string& record : ad.txt) {
            const size_t eq = record.find('=');
            keys.push_back(WideFromUtf8(record.substr(0, eq)));
            values.push_back(eq == std::string::npos ? std::wstring() : WideFromUtf8(record.substr(eq + 1)));
        }
        std::vector<PCWSTR> keyPtrs, valuePtrs;
        for (size_t i = 0; i < keys.size(); ++i) {
            keyPtrs.push_back(keys[i].c_str());
            valuePtrs.push_back(values[i].c_str());
        }
        instance_ = api.Construct(instanceName_.c_str(), hostName_.c_str(), nullptr, nullptr,
                                  static_cast<WORD>(ad.port), 0, 0, static_cast<DWORD>(keyPtrs.size()),
                                  keyPtrs.empty() ? nullptr : keyPtrs.data(),
                                  valuePtrs.empty() ? nullptr : valuePtrs.data());
        if (!instance_) {
            error = "DnsServiceConstructInstance failed";
            return false;
        }
        completed_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        request_ = DNS_SERVICE_REGISTER_REQUEST{};
        request_.Version = DNS_QUERY_REQUEST_VERSION1;
        request_.InterfaceIndex = 0;
        request_.pServiceInstance = instance_;
        request_.pRegisterCompletionCallback = &OnWindowsRegistered;
        request_.pQueryContext = completed_;
        request_.unicastEnabled = FALSE;
        if (api.Register(&request_, nullptr) != DNS_REQUEST_PENDING) {
            api.FreeInstance(instance_);
            instance_ = nullptr;
            error = "DnsServiceRegister failed";
            return false;
        }
        registered_ = true;
        return true;
    }

    void Stop() override {
        const DnsSdApi& api = GetDnsSdApi();
        if (registered_) {
            // The withdrawal finishes on a DNS thread that still reads the
            // instance: wait for it (briefly) before freeing.
            if (completed_) ResetEvent(completed_);
            if (api.DeRegister(&request_, nullptr) == DNS_REQUEST_PENDING && completed_)
                WaitForSingleObject(completed_, 2000);
            registered_ = false;
        }
        if (instance_) {
            api.FreeInstance(instance_);
            instance_ = nullptr;
        }
        if (completed_) {
            CloseHandle(completed_);
            completed_ = nullptr;
        }
    }

private:
    std::wstring instanceName_;
    std::wstring hostName_;
    PDNS_SERVICE_INSTANCE instance_ = nullptr;
    DNS_SERVICE_REGISTER_REQUEST request_{};
    HANDLE completed_ = nullptr;
    bool registered_ = false;
};

std::unique_ptr<Advertiser> StartAdvertiser(const UltraNetServiceAdvertisement& ad, std::string& error) {
    auto advertiser = std::make_unique<WindowsAdvertiser>();
    if (!advertiser->Start(ad, error)) return nullptr;
    return advertiser;
}

#endif


class MdnsPlugin : public IServiceDiscoveryPlugin {
public:
    std::string GetName() const override { return "UltraNet-mDNS"; }
    std::string GetVersion() const override { return "0.4.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"mdns", "dns-sd"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return ultranet_plugin::Guarded("Initialize", [&]() -> UltraNetResult {
            return UltraNetResult::Ok();
        });
    }
    void Shutdown() override {
        ultranet_plugin::GuardedVoid([&]() {
            std::map<UltraNetHandle, std::unique_ptr<Advertiser>> ads;
            {
                std::lock_guard<std::mutex> lock(adsMutex_);
                ads.swap(ads_);
            }
            for (auto& entry : ads) entry.second->Stop();
        });
    }

    UltraNetResult Advertise(const UltraNetServiceAdvertisement& ad, UltraNetHandle& outHandle) override {
        outHandle = UltraNetInvalidHandle;
        return ultranet_plugin::Guarded("Advertise", [&]() -> UltraNetResult {
            if (ad.serviceType.empty() || ad.instanceName.empty() || ad.port <= 0 || ad.port > 65535)
                return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                             "An advertisement needs a service type, a name and a port");
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32) || defined(_WIN64)
            std::string error;
            std::unique_ptr<Advertiser> advertiser = StartAdvertiser(ad, error);
            if (!advertiser) return UltraNetResult::Error(UltraNetResultCode::Unknown, error);
            std::lock_guard<std::mutex> lock(adsMutex_);
            outHandle = nextHandle_++;
            ads_[outHandle] = std::move(advertiser);
            return UltraNetResult::Ok();
#else
            return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
                                         "mDNS plug-in: no backend for this platform");
#endif
        });
    }

    UltraNetResult StopAdvertising(UltraNetHandle handle) override {
        return ultranet_plugin::Guarded("StopAdvertising", [&]() -> UltraNetResult {
            std::unique_ptr<Advertiser> advertiser;
            {
                std::lock_guard<std::mutex> lock(adsMutex_);
                auto it = ads_.find(handle);
                if (it == ads_.end())
                    return UltraNetResult::Error(UltraNetResultCode::InvalidHandle, "No such advertisement");
                advertiser = std::move(it->second);
                ads_.erase(it);
            }
            advertiser->Stop();
            return UltraNetResult::Ok();
        });
    }

    UltraNetResult Search(const std::string& /*url*/,
                          const UltraNetDirectoryQuery& q,
                          std::vector<UltraNetDirectoryEntry>& out) override {
        return ultranet_plugin::Guarded("Search", [&]() -> UltraNetResult {
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
        });
    }

private:
    std::mutex adsMutex_;
    std::map<UltraNetHandle, std::unique_ptr<Advertiser>> ads_;
    UltraNetHandle nextHandle_ = 1;
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    // ABI 2: the core functions this plug-in calls come through `host`
    // (UltraNetPluginHostShim); an older host cannot serve them.
    if (!UltraNetPlugin_AttachHost(host)) return;
    host->RegisterPlugin(std::make_shared<MdnsPlugin>());
}
