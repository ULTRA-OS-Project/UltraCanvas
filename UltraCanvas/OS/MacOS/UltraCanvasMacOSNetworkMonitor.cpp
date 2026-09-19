// OS/MacOS/UltraCanvasMacOSNetworkMonitor.cpp
// macOS backend for NetworkMonitor, built on libproc - a C API, so this file
// is plain C++ rather than Objective-C++. There is no system-wide socket
// table to read on macOS: the sockets are enumerated per process, from each
// process's descriptor list (proc_pidinfo PROC_PIDLISTFDS) and the socket
// info behind each descriptor (proc_pidfdinfo PROC_PIDFDSOCKETINFO), which
// is how lsof -i and nettop do it. Attribution is therefore free - every
// socket is found through its owner - and the flip side is that a process
// this monitor may not inspect contributes no sockets at all, not even
// unattributed ones, unless the monitor runs as root. That is counted and
// reported through the capabilities' notes.
//
// Per-process byte counters are not read: the public socket info carries
// none worth trusting, and nettop's numbers come from a private framework.
//
// Version: 0.2.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorAddress.h"
#include "NetworkMonitor/NetworkMonitorBackend.h"

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <libproc.h>
#include <netinet/in.h>
#include <pwd.h>
#include <sys/param.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <unistd.h>

namespace UltraCanvas {
namespace {

// TSI_S_* numbering (sys/proc_info.h) - neither the kernel's nor Windows'.
NetworkConnectionState StateFromTsi(int state) {
    switch (state) {
        case TSI_S_CLOSED:       return NetworkConnectionState::Closed;
        case TSI_S_LISTEN:       return NetworkConnectionState::Listening;
        case TSI_S_SYN_SENT:     return NetworkConnectionState::SynSent;
        case TSI_S_SYN_RECEIVED: return NetworkConnectionState::SynReceived;
        case TSI_S_ESTABLISHED:  return NetworkConnectionState::Established;
        case TSI_S__CLOSE_WAIT:  return NetworkConnectionState::CloseWait;
        case TSI_S_FIN_WAIT_1:   return NetworkConnectionState::FinWait1;
        case TSI_S_CLOSING:      return NetworkConnectionState::Closing;
        case TSI_S_LAST_ACK:     return NetworkConnectionState::LastAck;
        case TSI_S_FIN_WAIT_2:   return NetworkConnectionState::FinWait2;
        case TSI_S_TIME_WAIT:    return NetworkConnectionState::TimeWait;
        default:                 return NetworkConnectionState::Unknown;
    }
}

// Fills family and both endpoints from an in_sockinfo. The port fields are
// ints holding the port in network byte order.
void EndpointsFrom(const in_sockinfo& info, NetworkConnection& c) {
    if (info.insi_vflag & INI_IPV6) {
        c.family = NetworkAddressFamily::IPv6;
        c.localAddress = NetworkMonitorAddress::FormatIPv6(
            reinterpret_cast<const unsigned char*>(&info.insi_laddr.ina_6));
        c.remoteAddress = NetworkMonitorAddress::FormatIPv6(
            reinterpret_cast<const unsigned char*>(&info.insi_faddr.ina_6));
    } else {
        c.family = NetworkAddressFamily::IPv4;
        c.localAddress = NetworkMonitorAddress::FormatIPv4(
            reinterpret_cast<const unsigned char*>(&info.insi_laddr.ina_46.i46a_addr4));
        c.remoteAddress = NetworkMonitorAddress::FormatIPv4(
            reinterpret_cast<const unsigned char*>(&info.insi_faddr.ina_46.i46a_addr4));
    }
    c.localPort = ntohs(static_cast<uint16_t>(info.insi_lport));
    c.remotePort = ntohs(static_cast<uint16_t>(info.insi_fport));
}

std::string UserNameForUid(uint32_t uid, std::unordered_map<uint32_t, std::string>& cache) {
    auto found = cache.find(uid);
    if (found != cache.end()) return found->second;
    std::string name;
    struct passwd entry;
    struct passwd* result = nullptr;
    char buffer[4096];
    if (::getpwuid_r(uid, &entry, buffer, sizeof buffer, &result) == 0 && result) {
        name = result->pw_name;
    } else {
        name = std::to_string(uid);
    }
    cache.emplace(uid, name);
    return name;
}

class MacOSNetworkMonitorBackend : public INetworkMonitorBackend {
public:
    NetworkMonitorCapabilities Capabilities() const override {
        NetworkMonitorCapabilities caps;
        caps.backendName = "libproc";
        caps.socketTable = true;
        caps.processAttribution = true;
        caps.allUsers = ::geteuid() == 0;
        if (!caps.allUsers) {
            caps.notes.push_back("Not running as root: macOS enumerates sockets per process, "
                                 "so other users' processes contribute no sockets at all - "
                                 "not even unattributed ones.");
            if (lastUnreadableProcesses_ > 0) {
                caps.notes.push_back(std::to_string(lastUnreadableProcesses_) +
                                     " processes could not be inspected in the last snapshot.");
            }
        }
        caps.notes.push_back("Byte counters and connection events are not collected by the "
                             "libproc backend.");
        return caps;
    }

    NetworkMonitorResult Snapshot(std::vector<NetworkConnection>& out,
                                  bool resolveProcesses) override {
        out.clear();
        int bytes = ::proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (bytes <= 0) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                                               std::string("proc_listpids: ") + std::strerror(errno));
        }
        // Processes appear between the two calls; leave room for them.
        std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 64);
        bytes = ::proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                static_cast<int>(pids.size() * sizeof(pid_t)));
        if (bytes <= 0) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                                               std::string("proc_listpids: ") + std::strerror(errno));
        }
        pids.resize(static_cast<std::size_t>(bytes) / sizeof(pid_t));

        std::unordered_map<uint32_t, ProcessIdentity> seen;
        int unreadable = 0;
        std::vector<proc_fdinfo> fds;
        for (const pid_t pid : pids) {
            if (pid <= 0) continue;
            int size = ::proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
            if (size <= 0) {
                if (errno == EPERM || errno == EACCES) ++unreadable;
                continue;
            }
            fds.resize(static_cast<std::size_t>(size) / PROC_PIDLISTFD_SIZE + 16);
            size = ::proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(),
                                  static_cast<int>(fds.size() * PROC_PIDLISTFD_SIZE));
            if (size <= 0) continue;
            const std::size_t count = static_cast<std::size_t>(size) / PROC_PIDLISTFD_SIZE;

            for (std::size_t i = 0; i < count; ++i) {
                if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;
                socket_fdinfo info;
                const int got = ::proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO,
                                                 &info, PROC_PIDFDSOCKETINFO_SIZE);
                if (got != PROC_PIDFDSOCKETINFO_SIZE) continue;

                NetworkConnection c;
                if (info.psi.soi_kind == SOCKINFO_TCP) {
                    c.transport = NetworkTransport::Tcp;
                    EndpointsFrom(info.psi.soi_proto.pri_tcp.tcpsi_ini, c);
                    c.state = StateFromTsi(info.psi.soi_proto.pri_tcp.tcpsi_state);
                } else if (info.psi.soi_kind == SOCKINFO_IN &&
                           info.psi.soi_protocol == IPPROTO_UDP) {
                    c.transport = NetworkTransport::Udp;
                    EndpointsFrom(info.psi.soi_proto.pri_in, c);
                    c.state = c.remotePort != 0 ? NetworkConnectionState::Established
                                                : NetworkConnectionState::Unconnected;
                } else {
                    continue;   // Unix-domain, raw, kernel-event sockets
                }
                if (resolveProcesses) {
                    auto identity = seen.find(static_cast<uint32_t>(pid));
                    if (identity == seen.end()) {
                        identity = seen.emplace(static_cast<uint32_t>(pid), IdentityFor(pid)).first;
                    }
                    c.process = identity->second;
                    auto uid = uidByPid_.find(static_cast<uint32_t>(pid));
                    if (uid != uidByPid_.end()) c.ownerUid = uid->second;
                }
                out.push_back(std::move(c));
            }
        }
        lastUnreadableProcesses_ = unreadable;
        identities_.swap(seen);
        return NetworkMonitorResult::Ok();
    }

private:
    ProcessIdentity IdentityFor(pid_t pid) {
        auto cached = identities_.find(static_cast<uint32_t>(pid));
        if (cached != identities_.end()) return cached->second;

        ProcessIdentity identity;
        identity.pid = static_cast<uint32_t>(pid);
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (::proc_pidpath(pid, path, sizeof path) > 0) identity.executablePath = path;
        char name[2 * MAXCOMLEN + 1];
        if (::proc_name(pid, name, sizeof name) > 0) identity.displayName = name;
        if (identity.displayName.empty() && !identity.executablePath.empty()) {
            const std::size_t slash = identity.executablePath.rfind('/');
            identity.displayName = slash == std::string::npos
                ? identity.executablePath : identity.executablePath.substr(slash + 1);
        }
        if (identity.displayName.empty()) identity.displayName = "pid " + std::to_string(pid);

        proc_bsdinfo bsd{};
        if (::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, PROC_PIDTBSDINFO_SIZE) == PROC_PIDTBSDINFO_SIZE) {
            uidByPid_[identity.pid] = bsd.pbi_uid;
            identity.userName = UserNameForUid(bsd.pbi_uid, userNames_);
        }
        return identity;
    }

    std::unordered_map<uint32_t, ProcessIdentity> identities_;
    std::unordered_map<uint32_t, uint32_t> uidByPid_;
    std::unordered_map<uint32_t, std::string> userNames_;
    int lastUnreadableProcesses_ = 0;
};

} // namespace

std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend() {
    return std::make_unique<MacOSNetworkMonitorBackend>();
}

} // namespace UltraCanvas
