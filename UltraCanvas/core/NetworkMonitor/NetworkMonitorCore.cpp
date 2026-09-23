// core/NetworkMonitor/NetworkMonitorCore.cpp
// The platform-independent half of NetworkMonitor: the public functions,
// the option filters (applied here so every backend filters identically),
// the names a snapshot's peers are known by, the per-process roll-up, the
// display names, and the null backend for platforms that have none yet.
//
// Version: 0.8.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorBackend.h"
#include "NetworkMonitor/NetworkMonitorCsv.h"
#include "NetworkMonitor/NetworkMonitorNames.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace UltraCanvas {

#ifndef ULTRACANVAS_NETWORKMONITOR_NATIVE
// No backend on this platform. Capabilities read as all-false and every
// snapshot reports NotSupported, so a caller can say so instead of showing
// an empty table that looks like a quiet machine.
std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend() {
    return nullptr;
}
#endif

namespace {

// One backend per process, created on first use. Backends cache the process
// identities they have resolved, which is why there is one rather than one
// per call.
INetworkMonitorBackend* Backend() {
    static std::unique_ptr<INetworkMonitorBackend> backend;
    static std::once_flag once;
    std::call_once(once, [] { backend = CreateNativeNetworkMonitorBackend(); });
    return backend.get();
}

bool PassesFilters(const NetworkConnection& connection, const NetworkMonitorOptions& options) {
    if (!options.includeListening &&
        (connection.state == NetworkConnectionState::Listening ||
         connection.state == NetworkConnectionState::Unconnected)) {
        return false;
    }
    if (!options.includeLoopback && connection.IsLoopback()) return false;
    return true;
}

bool IsWildcard(const std::string& address) {
    return address.empty() || address == "0.0.0.0" || address == "::";
}

} // namespace

// ===== NetworkConnection =====

bool NetworkConnection::IsLoopback() const {
    // 127.0.0.0/8, ::1, and the IPv4-mapped form a dual-stack socket reports.
    auto loopback = [](const std::string& address) {
        return address.rfind("127.", 0) == 0 || address == "::1" ||
               address.rfind("::ffff:127.", 0) == 0;
    };
    return loopback(localAddress) || loopback(remoteAddress);
}

std::string NetworkConnection::LocalEndpoint() const {
    return NetworkMonitor_FormatEndpoint(localAddress, localPort);
}

std::string NetworkConnection::RemoteEndpoint() const {
    return NetworkMonitor_FormatEndpoint(remoteAddress, remotePort);
}

// ===== PUBLIC FUNCTIONS =====

NetworkMonitorCapabilities NetworkMonitor_GetCapabilities() {
    NetworkMonitorCapabilities caps;
    if (INetworkMonitorBackend* backend = Backend()) {
        caps = backend->Capabilities();
    } else {
        caps.backendName = "none";
        caps.notes.push_back("No NetworkMonitor backend for this platform in this build.");
    }
    // Names and events come from the registered sources, not the backend.
    caps.dnsWithProcess = NetworkMonitor_AnySourceReportsProcess();
    caps.connectionEvents = NetworkMonitor_AnyEventSourceRunning();
    return caps;
}

bool NetworkMonitor_IsAvailable() {
    return Backend() != nullptr;
}

NetworkMonitorResult NetworkMonitor_ListConnections(std::vector<NetworkConnection>& out,
                                                    const NetworkMonitorOptions& options) {
    out.clear();
    INetworkMonitorBackend* backend = Backend();
    if (!backend) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::NotSupported,
                                           "No NetworkMonitor backend for this platform.");
    }
    std::vector<NetworkConnection> all;
    const NetworkMonitorResult result = backend->Snapshot(all, options.resolveProcesses);
    if (!result) return result;
    // On the whole table, before the filters: a connection kept without
    // its loopback mirror still carries what the mirror told.
    NetworkMonitor_DecodeLoopback(all);

    out.reserve(all.size());
    for (auto& connection : all) {
        if (PassesFilters(connection, options)) out.push_back(std::move(connection));
    }
    if (options.resolveNames) {
        // One table lookup per distinct peer; the sources hear about the
        // peers nobody has named yet, so reverse DNS can look them up.
        std::map<std::string, NameRecord> named;
        std::set<std::string> unnamed;
        for (auto& connection : out) {
            if (IsWildcard(connection.remoteAddress) || connection.IsListening() ||
                connection.state == NetworkConnectionState::Unconnected) {
                continue;
            }
            auto found = named.find(connection.remoteAddress);
            if (found == named.end()) {
                NameRecord record;
                if (!NetworkMonitor_LookupName(connection.remoteAddress, record)) {
                    unnamed.insert(connection.remoteAddress);
                }
                found = named.emplace(connection.remoteAddress, std::move(record)).first;
            }
            connection.remoteName = found->second.name;
            connection.nameSource = found->second.source;
        }
        for (const auto& address : unnamed) NetworkMonitor_NoteUnnamedAddress(address);
    }
    return NetworkMonitorResult::Ok();
}

std::vector<ProcessTrafficSummary> NetworkMonitor_SummarizeByProcess(
    const std::vector<NetworkConnection>& connections) {
    struct Group {
        ProcessTrafficSummary summary;
        std::set<std::string> remotes;
        std::set<std::string> names;
        std::set<std::string> via;
        std::set<std::string> serves;
        bool allSentKnown = true;
        bool allReceivedKnown = true;
        uint64_t sent = 0;
        uint64_t received = 0;
    };
    // Keyed by PID; 0 collects everything the backend could not attribute.
    std::map<uint32_t, Group> groups;

    for (const auto& connection : connections) {
        const uint32_t pid = connection.process ? connection.process->pid : 0;
        Group& group = groups[pid];
        if (group.summary.connectionCount == 0) {
            if (connection.process) {
                group.summary.process = *connection.process;
                group.summary.attributed = true;
            } else {
                group.summary.process.displayName = "(unattributed)";
                group.summary.attributed = false;
            }
        }
        ++group.summary.connectionCount;
        if (connection.state == NetworkConnectionState::Established) ++group.summary.establishedCount;
        if (connection.state == NetworkConnectionState::Listening ||
            connection.state == NetworkConnectionState::Unconnected) {
            ++group.summary.listeningCount;
        }
        if (!IsWildcard(connection.remoteAddress)) group.remotes.insert(connection.remoteAddress);
        if (!connection.remoteName.empty()) group.names.insert(connection.remoteName);
        if (connection.localPeer) {
            const std::string peer = connection.localPeer->Label();
            if (connection.loopbackRole == LoopbackRole::Client) group.via.insert(peer);
            else if (connection.loopbackRole == LoopbackRole::Server) group.serves.insert(peer);
        }
        if (connection.bytesSent) group.sent += *connection.bytesSent; else group.allSentKnown = false;
        if (connection.bytesReceived) group.received += *connection.bytesReceived; else group.allReceivedKnown = false;
    }

    std::vector<ProcessTrafficSummary> result;
    result.reserve(groups.size());
    for (auto& [pid, group] : groups) {
        group.summary.remoteAddresses.assign(group.remotes.begin(), group.remotes.end());
        group.summary.remoteNames.assign(group.names.begin(), group.names.end());
        group.summary.viaProcesses.assign(group.via.begin(), group.via.end());
        group.summary.servesProcesses.assign(group.serves.begin(), group.serves.end());
        if (group.allSentKnown) group.summary.bytesSent = group.sent;
        if (group.allReceivedKnown) group.summary.bytesReceived = group.received;
        result.push_back(std::move(group.summary));
    }
    std::sort(result.begin(), result.end(),
              [](const ProcessTrafficSummary& a, const ProcessTrafficSummary& b) {
                  if (a.connectionCount != b.connectionCount) return a.connectionCount > b.connectionCount;
                  return a.process.displayName < b.process.displayName;
              });
    return result;
}

const char* NetworkMonitor_TransportName(NetworkTransport transport) {
    switch (transport) {
        case NetworkTransport::Tcp: return "TCP";
        case NetworkTransport::Udp: return "UDP";
        default:                    return "other";
    }
}

const char* NetworkMonitor_StateName(NetworkConnectionState state) {
    switch (state) {
        case NetworkConnectionState::Listening:   return "LISTEN";
        case NetworkConnectionState::SynSent:     return "SYN_SENT";
        case NetworkConnectionState::SynReceived: return "SYN_RECV";
        case NetworkConnectionState::Established: return "ESTABLISHED";
        case NetworkConnectionState::FinWait1:    return "FIN_WAIT1";
        case NetworkConnectionState::FinWait2:    return "FIN_WAIT2";
        case NetworkConnectionState::CloseWait:   return "CLOSE_WAIT";
        case NetworkConnectionState::Closing:     return "CLOSING";
        case NetworkConnectionState::LastAck:     return "LAST_ACK";
        case NetworkConnectionState::TimeWait:    return "TIME_WAIT";
        case NetworkConnectionState::Closed:      return "CLOSED";
        case NetworkConnectionState::Unconnected: return "UNCONN";
        default:                                  return "UNKNOWN";
    }
}

namespace {

bool LoopbackAddress(const std::string& address) {
    return address.rfind("127.", 0) == 0 || address == "::1" || address.rfind("::ffff:127.", 0) == 0;
}

std::string EndpointKey(NetworkTransport transport, const std::string& address, uint16_t port) {
    // A dual-stack socket reports its IPv4 peer as ::ffff:a.b.c.d; the
    // peer reports itself as a.b.c.d. One key for both spellings.
    std::string plain = address;
    if (plain.rfind("::ffff:", 0) == 0 && plain.find('.') != std::string::npos) plain.erase(0, 7);
    return std::to_string(static_cast<int>(transport)) + '|' + plain + '|' + std::to_string(port);
}

} // namespace

void NetworkMonitor_DecodeLoopback(std::vector<NetworkConnection>& connections) {
    // The sockets by their local endpoint, and the ports listened on.
    std::map<std::string, std::vector<std::size_t>> byLocal;
    std::set<std::string> listening;
    for (std::size_t i = 0; i < connections.size(); ++i) {
        NetworkConnection& c = connections[i];
        c.loopbackRole = LoopbackRole::None;
        c.localPeer.reset();
        c.forProcesses.clear();
        if (c.IsListening()) {
            listening.insert(std::to_string(static_cast<int>(c.transport)) + '|' + std::to_string(c.localPort));
            continue;
        }
        if (c.state == NetworkConnectionState::Unconnected) continue;
        byLocal[EndpointKey(c.transport, c.localAddress, c.localPort)].push_back(i);
    }

    // Pair each loopback connection with its mirror: the socket whose local
    // endpoint is this one's remote and whose remote is this one's local.
    for (std::size_t i = 0; i < connections.size(); ++i) {
        NetworkConnection& c = connections[i];
        if (c.IsListening() || c.state == NetworkConnectionState::Unconnected) continue;
        if (!LoopbackAddress(c.remoteAddress) && !LoopbackAddress(c.localAddress)) continue;
        auto candidates = byLocal.find(EndpointKey(c.transport, c.remoteAddress, c.remotePort));
        if (candidates == byLocal.end()) continue;
        for (std::size_t j : candidates->second) {
            if (j == i) continue;
            const NetworkConnection& m = connections[j];
            if (EndpointKey(m.transport, m.remoteAddress, m.remotePort) !=
                EndpointKey(c.transport, c.localAddress, c.localPort)) continue;
            c.localPeer = m.process;
            // The server is the side a listener holds; failing that, the
            // side whose port is the lower (ephemeral ports are high).
            const bool serverHere = listening.count(std::to_string(static_cast<int>(c.transport)) + '|' +
                                                    std::to_string(c.localPort)) > 0;
            const bool serverThere = listening.count(std::to_string(static_cast<int>(m.transport)) + '|' +
                                                     std::to_string(m.localPort)) > 0;
            if (serverHere && !serverThere) c.loopbackRole = LoopbackRole::Server;
            else if (serverThere && !serverHere) c.loopbackRole = LoopbackRole::Client;
            else c.loopbackRole = c.localPort < m.localPort ? LoopbackRole::Server : LoopbackRole::Client;
            break;
        }
    }

    // The inference: a process that serves loopback clients works for
    // them, so its outbound connections are marked with who they are for.
    std::map<uint32_t, std::set<std::string>> clientsOf;
    for (const auto& c : connections) {
        if (c.loopbackRole == LoopbackRole::Server && c.process && c.localPeer &&
            c.localPeer->pid != c.process->pid) {
            clientsOf[c.process->pid].insert(c.localPeer->Label());
        }
    }
    if (clientsOf.empty()) return;
    for (auto& c : connections) {
        if (!c.process || c.loopbackRole != LoopbackRole::None || c.IsListening() ||
            c.state == NetworkConnectionState::Unconnected || IsWildcard(c.remoteAddress) ||
            LoopbackAddress(c.remoteAddress)) {
            continue;
        }
        auto clients = clientsOf.find(c.process->pid);
        if (clients != clientsOf.end()) c.forProcesses.assign(clients->second.begin(), clients->second.end());
    }
}

const char* NetworkMonitor_LoopbackRoleName(LoopbackRole role) {
    switch (role) {
        case LoopbackRole::Client: return "client";
        case LoopbackRole::Server: return "server";
        default:                   return "";
    }
}

const char* NetworkMonitor_NameSourceName(NameSource source) {
    switch (source) {
        case NameSource::DnsProxy:      return "DNS proxy";
        case NameSource::EtwDnsClient:  return "DNS client events";
        case NameSource::PacketCapture: return "packet capture";
        case NameSource::Sni:           return "TLS SNI";
        case NameSource::ReverseDns:    return "reverse DNS";
        case NameSource::Inferred:      return "inferred";
        default:                        return "none";
    }
}

bool NetworkMonitor_NameIsObserved(NameSource source) {
    switch (source) {
        case NameSource::DnsProxy:
        case NameSource::EtwDnsClient:
        case NameSource::PacketCapture:
        case NameSource::Sni:
            return true;
        default:
            return false;
    }
}

namespace {

std::string Joined(const std::vector<std::string>& items) {
    std::string text;
    for (const auto& item : items) {
        if (!text.empty()) text += ';';
        text += item;
    }
    return text;
}

std::string Bytes(const std::optional<uint64_t>& bytes) {
    return bytes ? std::to_string(*bytes) : std::string();
}

NetworkMonitorResult FinishCsv(std::ofstream& file, const std::string& path, std::size_t rows, int64_t* rowsWritten) {
    if (!file) {
        return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError, "Writing " + path + " failed part-way.");
    }
    if (rowsWritten) *rowsWritten = static_cast<int64_t>(rows);
    return NetworkMonitorResult::Ok();
}

} // namespace

NetworkMonitorResult NetworkMonitor_ExportSummaryCsv(const std::vector<ProcessTrafficSummary>& summaries,
                                                     const std::string& path, int64_t* rowsWritten) {
    if (rowsWritten) *rowsWritten = 0;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError, "Could not write " + path);
    using NetworkMonitorCsv::Field;
    file << "application,pid,executable,user,attributed,connections,established,listening,"
            "peers,peer_addresses,hosts,via,serves,bytes_sent,bytes_received\r\n";
    for (const auto& s : summaries) {
        file << Field(s.process.displayName) << ','
             << (s.attributed ? std::to_string(s.process.pid) : std::string()) << ','
             << Field(s.process.executablePath) << ',' << Field(s.process.userName) << ','
             << (s.attributed ? "yes" : "no") << ','
             << s.connectionCount << ',' << s.establishedCount << ',' << s.listeningCount << ','
             << s.remoteAddresses.size() << ','
             << Field(Joined(s.remoteAddresses)) << ',' << Field(Joined(s.remoteNames)) << ','
             << Field(Joined(s.viaProcesses)) << ',' << Field(Joined(s.servesProcesses)) << ','
             << Bytes(s.bytesSent) << ',' << Bytes(s.bytesReceived) << "\r\n";
    }
    return FinishCsv(file, path, summaries.size(), rowsWritten);
}

NetworkMonitorResult NetworkMonitor_ExportConnectionsCsv(const std::vector<NetworkConnection>& connections,
                                                         const std::string& path, int64_t* rowsWritten) {
    if (rowsWritten) *rowsWritten = 0;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError, "Could not write " + path);
    using NetworkMonitorCsv::Field;
    file << "application,pid,executable,user,transport,family,local,remote,remote_name,name_source,"
            "state,loopback_role,local_peer,for,bytes_sent,bytes_received\r\n";
    for (const auto& c : connections) {
        const bool unbound = c.IsListening() || c.state == NetworkConnectionState::Unconnected;
        file << Field(c.process ? c.process->displayName : std::string("(unattributed)")) << ','
             << (c.process ? std::to_string(c.process->pid) : std::string()) << ','
             << Field(c.process ? c.process->executablePath : std::string()) << ','
             << Field(c.process ? c.process->userName
                                : (c.ownerUid ? "uid " + std::to_string(*c.ownerUid) : std::string())) << ','
             << NetworkMonitor_TransportName(c.transport) << ','
             << (c.family == NetworkAddressFamily::IPv6 ? "IPv6" : "IPv4") << ','
             << Field(c.LocalEndpoint()) << ',' << Field(unbound ? std::string("*") : c.RemoteEndpoint()) << ','
             << Field(c.remoteName) << ','
             << (c.remoteName.empty() ? "" : NetworkMonitor_NameSourceName(c.nameSource)) << ','
             << NetworkMonitor_StateName(c.state) << ','
             << NetworkMonitor_LoopbackRoleName(c.loopbackRole) << ','
             << Field(c.localPeer ? c.localPeer->Label() : std::string()) << ','
             << Field(Joined(c.forProcesses)) << ','
             << Bytes(c.bytesSent) << ',' << Bytes(c.bytesReceived) << "\r\n";
    }
    return FinishCsv(file, path, connections.size(), rowsWritten);
}

std::string NetworkMonitor_FormatEndpoint(const std::string& address, uint16_t port) {
    std::string text;
    if (address.empty()) {
        text = "*";
    } else if (address.find(':') != std::string::npos) {
        text = "[" + address + "]";
    } else {
        text = address;
    }
    return text + ":" + std::to_string(port);
}

} // namespace UltraCanvas
