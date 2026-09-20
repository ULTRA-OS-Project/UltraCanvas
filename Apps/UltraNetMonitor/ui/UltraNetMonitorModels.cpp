// Apps/UltraNetMonitor/ui/UltraNetMonitorModels.cpp
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorModels.h"

#include "UltraCanvasHardwareInfo.h"

#include <ctime>
#include <optional>
#include <string>

using namespace UltraCanvas;

namespace UltraNetMonitor {
namespace {

// A counter the backend did not report is a dash, never a zero - the two
// are different facts. SortRole answers nothing for it, so it sorts last.
std::string ByteText(const std::optional<uint64_t>& bytes) {
    return bytes ? UltraCanvasHardwareInfo::FormatBytes(*bytes) : std::string("\u2014");
}

ListDataValue ByteSortKey(const std::optional<uint64_t>& bytes) {
    if (!bytes) return {};
    return static_cast<float>(*bytes);
}

// Local time, to the second, for the history columns.
std::string LocalTime(int64_t seconds) {
    if (seconds <= 0) return "\u2014";
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

std::string ProcessName(const std::optional<ProcessIdentity>& process) {
    return process ? process->displayName : std::string("(unattributed)");
}

// The tooltip on a process cell: the executable, when it was readable.
std::string ProcessTooltip(const std::optional<ProcessIdentity>& process) {
    if (!process) {
        return "Not attributable: the socket belongs to a process this monitor "
               "may not inspect (run as root to see every process).";
    }
    return process->executablePath.empty() ? std::string() : process->executablePath;
}

} // namespace

// ===== CONNECTIONS =====

int ConnectionListModel::GetRowCount() const { return static_cast<int>(rows_.size()); }
int ConnectionListModel::GetColumnCount() const { return ColumnCount; }

ListDataValue ConnectionListModel::GetData(const ListIndex& index, ListDataRole role) const {
    const NetworkConnection* c = At(index.row);
    if (!c) return {};
    switch (role) {
        case ListDataRole::DisplayRole:
            switch (index.column) {
                case Application: return ProcessName(c->process);
                case Pid:         return c->process ? std::to_string(c->process->pid) : std::string("-");
                case Protocol:    return std::string(NetworkMonitor_TransportName(c->transport)) +
                                         (c->family == NetworkAddressFamily::IPv6 ? "6" : "");
                case Local:       return c->LocalEndpoint();
                case Remote:      return c->IsListening() || c->state == NetworkConnectionState::Unconnected
                                         ? std::string("*") : c->RemoteEndpoint();
                case State:       return std::string(NetworkMonitor_StateName(c->state));
                case Sent:        return ByteText(c->bytesSent);
                case Received:    return ByteText(c->bytesReceived);
                case User:        return c->process ? c->process->userName
                                         : (c->ownerUid ? std::string("uid ") + std::to_string(*c->ownerUid)
                                                        : std::string());
                default:          return {};
            }
        case ListDataRole::SortRole:
            // Numbers sort as numbers; the rest falls back to the text.
            if (index.column == Pid) return static_cast<int>(c->process ? c->process->pid : 0);
            if (index.column == Sent) return ByteSortKey(c->bytesSent);
            if (index.column == Received) return ByteSortKey(c->bytesReceived);
            return {};
        case ListDataRole::ToolTipRole:
            if (index.column == Application || index.column == Pid) return ProcessTooltip(c->process);
            if (index.column == Remote && !c->remoteAddress.empty()) return c->RemoteEndpoint();
            return {};
        default:
            return {};
    }
}

ListColumnDef ConnectionListModel::GetColumnDef(int column) const {
    switch (column) {
        case Application: return ListColumnDef("Application", 150, TextAlignment::Left,
                                               "The process that owns the socket");
        case Pid:         return ListColumnDef("PID", 64, TextAlignment::Right);
        case Protocol:    return ListColumnDef("Proto", 58);
        case Local:       return ListColumnDef("Local", 170, TextAlignment::Left,
                                               "This machine's side of the connection");
        case Remote:      return ListColumnDef("Remote", 200, TextAlignment::Left,
                                               "The other side; * for a listener");
        case State:       return ListColumnDef("State", 110);
        case Sent:        return ListColumnDef("Sent", 80, TextAlignment::Right,
                                               "Bytes the peer acknowledged; a dash where the backend has no counter");
        case Received:    return ListColumnDef("Received", 80, TextAlignment::Right,
                                               "Bytes received; a dash where the backend has no counter");
        case User:        return ListColumnDef("User", 90);
        default:          return ListColumnDef("", 80);
    }
}

void ConnectionListModel::Replace(std::vector<NetworkConnection> rows) {
    rows_ = std::move(rows);
    NotifyDataChanged();
}

const NetworkConnection* ConnectionListModel::At(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return nullptr;
    return &rows_[static_cast<std::size_t>(row)];
}

// ===== PROCESSES =====

int ProcessListModel::GetRowCount() const { return static_cast<int>(rows_.size()); }
int ProcessListModel::GetColumnCount() const { return ColumnCount; }

ListDataValue ProcessListModel::GetData(const ListIndex& index, ListDataRole role) const {
    const ProcessTrafficSummary* p = At(index.row);
    if (!p) return {};
    switch (role) {
        case ListDataRole::DisplayRole:
            switch (index.column) {
                case Application: return p->process.displayName;
                case Pid:         return p->attributed ? std::to_string(p->process.pid) : std::string("-");
                case Connections: return std::to_string(p->connectionCount);
                case Established: return std::to_string(p->establishedCount);
                case Listening:   return std::to_string(p->listeningCount);
                case Remotes:     return std::to_string(p->remoteAddresses.size());
                case Sent:        return ByteText(p->bytesSent);
                case Received:    return ByteText(p->bytesReceived);
                default:          return {};
            }
        case ListDataRole::SortRole:
            switch (index.column) {
                case Pid:         return static_cast<int>(p->attributed ? p->process.pid : 0);
                case Connections: return p->connectionCount;
                case Established: return p->establishedCount;
                case Listening:   return p->listeningCount;
                case Remotes:     return static_cast<int>(p->remoteAddresses.size());
                case Sent:        return ByteSortKey(p->bytesSent);
                case Received:    return ByteSortKey(p->bytesReceived);
                default:          return {};
            }
        case ListDataRole::ToolTipRole:
            if (index.column == Remotes && !p->remoteAddresses.empty()) {
                // The first few peers, so a hover answers "talking to whom?".
                std::string text;
                std::size_t shown = 0;
                for (const auto& address : p->remoteAddresses) {
                    if (shown++ == 8) { text += "\n…"; break; }
                    if (!text.empty()) text += "\n";
                    text += address;
                }
                return text;
            }
            if (index.column == Application || index.column == Pid) {
                return p->attributed ? p->process.executablePath
                                     : ProcessTooltip(std::nullopt);
            }
            return {};
        default:
            return {};
    }
}

ListColumnDef ProcessListModel::GetColumnDef(int column) const {
    switch (column) {
        case Application: return ListColumnDef("Application", 160);
        case Pid:         return ListColumnDef("PID", 64, TextAlignment::Right);
        case Connections: return ListColumnDef("Conns", 58, TextAlignment::Right,
                                               "All sockets the process holds");
        case Established: return ListColumnDef("Estab", 58, TextAlignment::Right,
                                               "Established connections");
        case Listening:   return ListColumnDef("Listen", 58, TextAlignment::Right,
                                               "Listening and unconnected sockets");
        case Remotes:     return ListColumnDef("Peers", 58, TextAlignment::Right,
                                               "Distinct remote addresses");
        case Sent:        return ListColumnDef("Sent", 80, TextAlignment::Right,
                                               "Total over the process's TCP connections; a dash when any is uncounted");
        case Received:    return ListColumnDef("Received", 80, TextAlignment::Right,
                                               "Total over the process's TCP connections; a dash when any is uncounted");
        default:          return ListColumnDef("", 60);
    }
}

void ProcessListModel::Replace(std::vector<ProcessTrafficSummary> rows) {
    rows_ = std::move(rows);
    NotifyDataChanged();
}

const ProcessTrafficSummary* ProcessListModel::At(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return nullptr;
    return &rows_[static_cast<std::size_t>(row)];
}

// ===== RECORDED FLOWS =====

int FlowListModel::GetRowCount() const { return static_cast<int>(rows_.size()); }
int FlowListModel::GetColumnCount() const { return ColumnCount; }

ListDataValue FlowListModel::GetData(const ListIndex& index, ListDataRole role) const {
    const RecordedFlow* f = At(index.row);
    if (!f) return {};
    const bool unbound = f->lastState == NetworkConnectionState::Listening ||
                         f->lastState == NetworkConnectionState::Unconnected;
    switch (role) {
        case ListDataRole::DisplayRole:
            switch (index.column) {
                case Application: return ProcessName(f->process);
                case Pid:         return f->process ? std::to_string(f->process->pid) : std::string("-");
                case Protocol:    return std::string(NetworkMonitor_TransportName(f->transport)) +
                                         (f->family == NetworkAddressFamily::IPv6 ? "6" : "");
                case Local:       return f->LocalEndpoint();
                case Remote:      return unbound ? std::string("*") : f->RemoteEndpoint();
                case State:       return std::string(NetworkMonitor_StateName(f->lastState));
                case FirstSeen:   return LocalTime(f->firstSeen);
                case LastSeen:    return LocalTime(f->lastSeen);
                case Seen:        return std::to_string(f->snapshots);
                case Sent:        return ByteText(f->bytesSent);
                case Received:    return ByteText(f->bytesReceived);
                default:          return {};
            }
        case ListDataRole::SortRole:
            switch (index.column) {
                case Pid:       return static_cast<int>(f->process ? f->process->pid : 0);
                case FirstSeen: return static_cast<float>(f->firstSeen);
                case LastSeen:  return static_cast<float>(f->lastSeen);
                case Seen:      return f->snapshots;
                case Sent:      return ByteSortKey(f->bytesSent);
                case Received:  return ByteSortKey(f->bytesReceived);
                default:        return {};
            }
        case ListDataRole::ToolTipRole:
            if (index.column == Application || index.column == Pid) return ProcessTooltip(f->process);
            return {};
        default:
            return {};
    }
}

ListColumnDef FlowListModel::GetColumnDef(int column) const {
    switch (column) {
        case Application: return ListColumnDef("Application", 140);
        case Pid:         return ListColumnDef("PID", 60, TextAlignment::Right);
        case Protocol:    return ListColumnDef("Proto", 56);
        case Local:       return ListColumnDef("Local", 150);
        case Remote:      return ListColumnDef("Remote", 180);
        case State:       return ListColumnDef("Last state", 100);
        case FirstSeen:   return ListColumnDef("First seen", 140);
        case LastSeen:    return ListColumnDef("Last seen", 140);
        case Seen:        return ListColumnDef("Seen", 50, TextAlignment::Right,
                                               "How many snapshots saw this flow");
        case Sent:        return ListColumnDef("Sent", 80, TextAlignment::Right);
        case Received:    return ListColumnDef("Received", 80, TextAlignment::Right);
        default:          return ListColumnDef("", 60);
    }
}

void FlowListModel::Replace(std::vector<RecordedFlow> rows) {
    rows_ = std::move(rows);
    NotifyDataChanged();
}

const RecordedFlow* FlowListModel::At(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return nullptr;
    return &rows_[static_cast<std::size_t>(row)];
}

} // namespace UltraNetMonitor
