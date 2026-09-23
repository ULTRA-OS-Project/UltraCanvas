// include/NetworkMonitor/NetworkMonitorEvents.h
// Connection events for NetworkMonitor: a connection opened, accepted or
// closed, reported when it happens rather than found in the next
// snapshot. A polled table misses every connection shorter than its
// interval - a DNS lookup, a tracking beacon, an API call can open,
// transfer and close between two reads - and the proposal (§2.1, §5.2)
// asked for event-rate collection to close that gap.
//
// Same shape as the name sources (NetworkMonitorNames.h): an *event
// source* implements one interface, the registry runs it, every event goes
// to the listeners and into a bounded ring the UI reads. Built in:
//   - the snapshot differ: reads the socket table at a short interval and
//     reports what appeared and what went. Portable; attribution and
//     counters come with the table; blind to anything shorter than the
//     interval, and says so.
//   - the platform's own events where it has any: on Linux the kernel's
//     connection tracker (nf_conntrack over netlink: root, and a firewall
//     rule must have the tracker active); on Windows the kernel network
//     ETW provider (elevated), with the PID and the bytes moved. macOS has
//     none without a Network Extension; the differ covers it.
// The registry names each event's peer from the name table and, for a
// source that reports no process, attributes it from the socket table on a
// best-effort basis.
//
// Version: 0.5.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class NetworkEventKind {
    Opened,     // this machine initiated the connection (or the source cannot tell)
    Accepted,   // a peer connected to a listener here
    Closed
};

struct NetworkConnectionEvent {
    NetworkEventKind       kind = NetworkEventKind::Opened;
    NetworkTransport       transport = NetworkTransport::Tcp;
    NetworkAddressFamily   family = NetworkAddressFamily::IPv4;
    std::string            localAddress;
    uint16_t               localPort = 0;
    std::string            remoteAddress;
    uint16_t               remotePort = 0;
    // The owning process, where the source knows it or the registry could
    // find the socket in the table at the time; empty otherwise.
    std::optional<ProcessIdentity> process;
    // For Closed: the bytes the connection moved, where the source counts
    // them. Absent is "not reported", never zero.
    std::optional<uint64_t> bytesSent;
    std::optional<uint64_t> bytesReceived;
    int64_t                observedAtMs = 0;    // Unix milliseconds; 0 = now
    std::string            sourceName;          // which source reported it
    // Filled by the registry from the name table (NetworkMonitorNames.h).
    std::string            remoteName;
    NameSource             nameSource = NameSource::None;

    bool IsLoopback() const;
    std::string LocalEndpoint() const;
    std::string RemoteEndpoint() const;
};

// ===== THE SOURCE CONTRACT =====
class IConnectionEventSource {
public:
    virtual ~IConnectionEventSource() = default;

    virtual std::string Name() const = 0;               // "snapshot differ (250 ms)"
    virtual bool ReportsProcess() const { return false; }
    virtual bool ReportsBytes() const { return false; }  // Closed events carry counters

    virtual NetworkMonitorResult Start(std::function<void(const NetworkConnectionEvent&)> onEvent) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    // The last failure after a successful Start, or a standing limit the
    // caller should show ("conntrack is idle: no firewall rule registers
    // it"); empty while healthy.
    virtual std::string LastError() const { return std::string(); }
};

// ===== BUILT-IN SOURCES =====

struct SnapshotDiffOptions {
    int  intervalMs = 250;            // connections shorter than this are missed
    bool includeListening = false;    // report listeners appearing and going
    bool includeLoopback = true;
};

std::unique_ptr<IConnectionEventSource> NetworkMonitor_CreateSnapshotDiffEventSource(const SnapshotDiffOptions& options);
// The platform's own connection events: nf_conntrack on Linux, the kernel
// network ETW provider on Windows, null on every other platform.
std::unique_ptr<IConnectionEventSource> NetworkMonitor_CreateSystemEventSource();

// ===== THE REGISTRY =====
struct EventSourceStatus {
    std::string name;
    bool        running = false;
    bool        reportsProcess = false;
    bool        reportsBytes = false;
    std::string lastError;
    int64_t     events = 0;   // reported so far
};

NetworkMonitorResult NetworkMonitor_RegisterEventSource(std::unique_ptr<IConnectionEventSource> source);
void                 NetworkMonitor_ListEventSources(std::vector<EventSourceStatus>& out);
void                 NetworkMonitor_StopEventSources();

// A listener sees every event from every source, on the source's thread,
// after the registry has named and attributed it.
using EventListenerId = uint64_t;
EventListenerId NetworkMonitor_AddEventListener(std::function<void(const NetworkConnectionEvent&)> listener);
void            NetworkMonitor_RemoveEventListener(EventListenerId id);

// The most recent events, newest first, from a ring the registry keeps so
// a UI can show them without a listener of its own.
constexpr std::size_t kRecentEventsCapacity = 2000;
void NetworkMonitor_RecentEvents(std::vector<NetworkConnectionEvent>& out, std::size_t max = kRecentEventsCapacity);
void NetworkMonitor_ClearRecentEvents();
// Feeds the registry directly - what sources do through their callback.
void NetworkMonitor_ReportEvent(const NetworkConnectionEvent& event);

// "opened", "accepted", "closed". Never null.
const char* NetworkMonitor_EventKindName(NetworkEventKind kind);
// Unix milliseconds now.
int64_t NetworkMonitor_NowMs();

} // namespace UltraCanvas
