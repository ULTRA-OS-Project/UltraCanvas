// include/NetworkMonitor/NetworkMonitorNames.h
// Domain names for NetworkMonitor. Sockets carry addresses, not names;
// resolving 104.18.x.x back to the site the user visited needs a separate
// source, and the proposal (§2.3) makes that a plug-in point rather than a
// hard-wired collector: several *name sources* may run at once, each hands
// its observations to one *name table*, and every name carries which source
// it came from, so a UI can tell "we saw this query" from "we guessed".
//
// Built in:
//   - the local DNS proxy: listens on 127.0.0.1 (port 53 needs privilege;
//     any other port needs the resolver pointed at it), forwards every
//     query to the upstream resolver unchanged, and reads the answers on
//     the way back. Cross-platform; sees every client that uses the system
//     resolver; blind to a browser resolving over HTTPS on its own.
//   - reverse DNS: a PTR lookup of each peer address, on its own thread,
//     cached. Weak - behind a CDN everything answers "cloudflare" - and
//     labelled as such.
//   - the Windows DNS client's ETW events (Microsoft-Windows-DNS-Client),
//     the one source that knows the asking process; needs an elevated
//     token. Null elsewhere.
//
// NetworkMonitor_ListConnections fills each connection's remoteName from
// the table; a caller that only wants names calls NetworkMonitor_LookupName.
//
// Version: 0.4.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== THE SOURCE CONTRACT =====
// Implemented by every name source, built in or an application's own. A
// source runs on its own thread(s) after Start and reports through the
// callback it was given; the callback is cheap and may be called from any
// thread. Stop joins and returns only when no callback can run again.
class INameSource {
public:
    virtual ~INameSource() = default;

    virtual std::string Name() const = 0;          // "DNS proxy 127.0.0.1:53"
    virtual NameSource  Kind() const = 0;
    // Whether the observations carry the asking process (dnsWithProcess).
    virtual bool ReportsProcess() const { return false; }

    virtual NetworkMonitorResult Start(std::function<void(const DnsObservation&)> onObservation) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    // The last failure after a successful Start (the proxy's socket died,
    // the trace session ended); empty while healthy.
    virtual std::string LastError() const { return std::string(); }

    // Called by the core for every peer address a snapshot could not name,
    // so a source that resolves on demand (reverse DNS) knows what to look
    // up. Most sources ignore it. Must not block.
    virtual void NoteAddress(const std::string& /*address*/) {}
    // Waits until on-demand work is done or the timeout passes; true when
    // idle. Sources with no queue return true at once.
    virtual bool WaitIdle(int /*timeoutMs*/) { return true; }
};

// ===== BUILT-IN SOURCES =====

struct DnsProxyOptions {
    std::string listenAddress = "127.0.0.1";
    uint16_t    listenPort = 53;          // needs privilege below 1024
    // Empty: the system's first configured resolver (/etc/resolv.conf,
    // GetNetworkParams). Must not be the proxy itself.
    std::string upstreamAddress;
    uint16_t    upstreamPort = 53;
    int         upstreamTimeoutMs = 3000;
};

struct ReverseDnsOptions {
    int  negativeCacheSeconds = 600;      // how long "no PTR" is remembered
    int  maxQueueLength = 512;            // addresses waiting; newer ones drop
    bool includePrivateRanges = false;    // 10/8, 172.16/12, 192.168/16, fc00::/7
};

std::unique_ptr<INameSource> NetworkMonitor_CreateDnsProxySource(const DnsProxyOptions& options);
std::unique_ptr<INameSource> NetworkMonitor_CreateReverseDnsSource(const ReverseDnsOptions& options);
// The platform's own resolver events, where it has any: the Windows DNS
// client's ETW provider (with the asking PID; elevated only). Null on
// every other platform - the caller uses the proxy there.
std::unique_ptr<INameSource> NetworkMonitor_CreateSystemDnsSource();

// The upstream resolver the system is configured with, in text; empty when
// none could be read. What DnsProxyOptions::upstreamAddress defaults to.
std::string NetworkMonitor_SystemResolver();

// ===== THE REGISTRY =====
// Sources registered here are started, feed the name table, and are
// stopped and destroyed by NetworkMonitor_StopNameSources or at exit. A
// source that fails to start is not kept; the result says why.
struct NameSourceStatus {
    std::string name;
    NameSource  kind = NameSource::None;
    bool        running = false;
    bool        reportsProcess = false;
    std::string lastError;
    int64_t     observations = 0;   // handed to the table so far
};

NetworkMonitorResult NetworkMonitor_RegisterNameSource(std::unique_ptr<INameSource> source);
void                 NetworkMonitor_ListNameSources(std::vector<NameSourceStatus>& out);
void                 NetworkMonitor_StopNameSources();
// Every registered source with a queue has drained, or the timeout passed.
bool                 NetworkMonitor_WaitForNames(int timeoutMs);

// A listener sees every observation the table receives, from any source,
// on the source's thread - the app's recorder writes them to the store.
using NameListenerId = uint64_t;
NameListenerId NetworkMonitor_AddNameListener(std::function<void(const DnsObservation&)> listener);
void           NetworkMonitor_RemoveNameListener(NameListenerId id);

// ===== THE NAME TABLE =====
// address -> the best name known for it. An observed source beats a weak
// one; among equals the newer wins. Entries live for max(TTL, one hour)
// from their observation, since a connection outlives the DNS answer that
// started it, and the table is capped: past the cap the expired go first,
// then the oldest.
struct NameRecord {
    std::string address;
    std::string name;
    NameSource  source = NameSource::None;
    int64_t     observedAt = 0;   // Unix seconds
    int64_t     expiresAt = 0;
    std::optional<ProcessIdentity> process;   // the asking process, where the source knew it
};

constexpr int64_t kNameMinimumLifetimeSeconds = 3600;
constexpr std::size_t kNameTableCapacity = 65536;

// Feeds the table directly (what sources do through their callback). An
// observation with observedAt == 0 is stamped now; one without addresses
// or without a name is ignored.
void NetworkMonitor_ObserveName(const DnsObservation& observation);
bool NetworkMonitor_LookupName(const std::string& address, NameRecord& out);
// Every live record, newest first.
void NetworkMonitor_ListNames(std::vector<NameRecord>& out);
void NetworkMonitor_ClearNames();

} // namespace UltraCanvas
