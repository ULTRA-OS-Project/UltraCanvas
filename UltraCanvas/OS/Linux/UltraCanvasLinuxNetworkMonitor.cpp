// OS/Linux/UltraCanvasLinuxNetworkMonitor.cpp
// Linux backend for NetworkMonitor. The socket table comes from netlink
// sock_diag (inet_diag) where the kernel allows it - one round trip per
// table, and for TCP the per-socket tcp_info with its byte counters - and
// from /proc/net/{tcp,tcp6,udp,udp6} otherwise (a seccomp profile that
// blocks netlink, a kernel without udp_diag). Either way, process
// attribution is the walk of every /proc/<pid>/fd/ for links of the form
// "socket:[<inode>]" - the join `ss -p` performs. Netlink reports the inode
// and UID but never the PID, so the walk stays. No daemon, no helper, no
// extra library.
//
// What it cannot see, it says: a process owned by another user has an
// unreadable fd directory unless the monitor runs as root, and every such
// process is counted and reported through the capabilities' notes rather
// than silently left out of the table.
//
// Version: 0.2.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorAddress.h"
#include "NetworkMonitor/NetworkMonitorBackend.h"
#include "NetworkMonitor/NetworkMonitorProcfs.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>

namespace UltraCanvas {
namespace {

// ===== PROCFS PRIMITIVES =====

bool ReadWholeFile(const std::string& path, std::string& out, int& errnoOut) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { errnoOut = errno; return false; }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    errnoOut = 0;
    return true;
}

std::string ReadLink(const std::string& path) {
    char buffer[4096];
    const ssize_t length = ::readlink(path.c_str(), buffer, sizeof buffer - 1);
    if (length < 0) return std::string();
    return std::string(buffer, static_cast<std::size_t>(length));
}

std::string ReadFirstLine(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line)) return std::string();
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

bool IsAllDigits(const char* text) {
    if (!*text) return false;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
    }
    return true;
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

// ===== NETLINK sock_diag =====

struct DiagRequest {
    nlmsghdr header;
    inet_diag_req_v2 request;
};

// Dumps one (family, protocol) table through NETLINK_SOCK_DIAG. Returns
// false, with errno in `error`, when the kernel refuses - the caller then
// reads the procfs file instead. `sawByteCounters` reports whether at least
// one TCP socket carried a tcp_info long enough to hold the byte counters
// (kernel 4.1+), which is what "byte counters available" means.
bool DumpTable(int family, int protocol, NetworkTransport transport,
               NetworkAddressFamily addressFamily,
               std::vector<NetworkConnection>& out, int& error, bool& sawByteCounters) {
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (fd < 0) { error = errno; return false; }

    DiagRequest message{};
    message.header.nlmsg_len = sizeof message;
    message.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    message.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    message.header.nlmsg_seq = 1;
    message.request.sdiag_family = static_cast<__u8>(family);
    message.request.sdiag_protocol = static_cast<__u8>(protocol);
    message.request.idiag_states = ~0u;
    if (protocol == IPPROTO_TCP) message.request.idiag_ext = 1u << (INET_DIAG_INFO - 1);

    if (::send(fd, &message, sizeof message, 0) < 0) {
        error = errno;
        ::close(fd);
        return false;
    }

    std::vector<unsigned char> buffer(64 * 1024);
    const std::size_t firstNew = out.size();
    bool done = false;
    while (!done) {
        const ssize_t received = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            error = errno;
            ::close(fd);
            out.resize(firstNew);
            return false;
        }
        if (received == 0) break;

        auto* header = reinterpret_cast<nlmsghdr*>(buffer.data());
        int remaining = static_cast<int>(received);
        for (; NLMSG_OK(header, remaining); header = NLMSG_NEXT(header, remaining)) {
            if (header->nlmsg_type == NLMSG_DONE) { done = true; break; }
            if (header->nlmsg_type == NLMSG_ERROR) {
                const auto* failure = reinterpret_cast<const nlmsgerr*>(NLMSG_DATA(header));
                error = failure ? -failure->error : EIO;
                ::close(fd);
                out.resize(firstNew);
                return false;
            }
            if (header->nlmsg_type != SOCK_DIAG_BY_FAMILY) continue;
            const auto* diag = reinterpret_cast<const inet_diag_msg*>(NLMSG_DATA(header));

            NetworkConnection connection;
            connection.transport = transport;
            connection.family = addressFamily;
            const auto* source = reinterpret_cast<const unsigned char*>(diag->id.idiag_src);
            const auto* destination = reinterpret_cast<const unsigned char*>(diag->id.idiag_dst);
            if (addressFamily == NetworkAddressFamily::IPv4) {
                connection.localAddress = NetworkMonitorAddress::FormatIPv4(source);
                connection.remoteAddress = NetworkMonitorAddress::FormatIPv4(destination);
            } else {
                connection.localAddress = NetworkMonitorAddress::FormatIPv6(source);
                connection.remoteAddress = NetworkMonitorAddress::FormatIPv6(destination);
            }
            connection.localPort = ntohs(diag->id.idiag_sport);
            connection.remotePort = ntohs(diag->id.idiag_dport);
            connection.state = NetworkMonitorProcfs::StateFromCode(diag->idiag_state, transport);
            connection.ownerUid = diag->idiag_uid;
            connection.socketInode = diag->idiag_inode;

            // Attributes follow the fixed part. INET_DIAG_INFO is a tcp_info,
            // possibly shorter than ours on an older kernel: copy what came.
            const auto* attribute = reinterpret_cast<const rtattr*>(
                reinterpret_cast<const unsigned char*>(diag) + NLMSG_ALIGN(sizeof *diag));
            int attributeLength = static_cast<int>(header->nlmsg_len) -
                                  static_cast<int>(NLMSG_LENGTH(sizeof *diag));
            for (; RTA_OK(attribute, attributeLength); attribute = RTA_NEXT(attribute, attributeLength)) {
                if (attribute->rta_type != INET_DIAG_INFO) continue;
                const std::size_t needed = offsetof(tcp_info, tcpi_bytes_received) + sizeof(__u64);
                if (RTA_PAYLOAD(attribute) < needed) break;
                tcp_info info{};
                std::memcpy(&info, RTA_DATA(attribute),
                            RTA_PAYLOAD(attribute) < sizeof info ? RTA_PAYLOAD(attribute) : sizeof info);
                // tcpi_bytes_acked is what the peer has acknowledged, i.e.
                // what was sent and arrived; it counts the SYN as one byte.
                connection.bytesSent = info.tcpi_bytes_acked;
                connection.bytesReceived = info.tcpi_bytes_received;
                sawByteCounters = true;
                break;
            }
            out.push_back(std::move(connection));
        }
    }
    ::close(fd);
    error = 0;
    return true;
}

// ===== THE BACKEND =====

class LinuxNetworkMonitorBackend : public INetworkMonitorBackend {
public:
    NetworkMonitorCapabilities Capabilities() const override {
        NetworkMonitorCapabilities caps;
        caps.backendName = lastUsedNetlink_ ? "procfs+sock_diag" : "procfs";
        caps.socketTable = ::access("/proc/net/tcp", R_OK) == 0 ||
                           ::access("/proc/net/tcp6", R_OK) == 0;
        caps.processAttribution = caps.socketTable && ::access("/proc/self/fd", R_OK) == 0;
        caps.allUsers = ::geteuid() == 0;
        caps.perConnectionBytes = lastSawByteCounters_;
        if (!caps.socketTable) {
            caps.notes.push_back("/proc/net is not readable here; no socket table.");
        } else if (!caps.allUsers) {
            caps.notes.push_back("Not running as root: only this user's processes can be "
                                 "attributed. Sockets of other users' processes are listed "
                                 "without a process.");
            if (lastUnreadableProcesses_ > 0) {
                caps.notes.push_back(std::to_string(lastUnreadableProcesses_) +
                                     " processes could not be inspected in the last snapshot.");
            }
        }
        if (lastSawByteCounters_) {
            caps.notes.push_back("TCP byte counters from netlink sock_diag (tcp_info); UDP "
                                 "sockets carry none.");
        } else if (!lastNetlinkError_.empty()) {
            caps.notes.push_back("netlink sock_diag unavailable (" + lastNetlinkError_ +
                                 "); reading /proc/net without byte counters.");
        } else {
            caps.notes.push_back("Byte counters appear after the first snapshot when the "
                                 "kernel allows netlink sock_diag.");
        }
        caps.notes.push_back("Connection events are not collected by this backend (Phase 3).");
        return caps;
    }

    NetworkMonitorResult Snapshot(std::vector<NetworkConnection>& out,
                                  bool resolveProcesses) override {
        out.clear();
        struct Table {
            const char* path; int family; int protocol;
            NetworkTransport transport; NetworkAddressFamily addressFamily;
        };
        static const Table kTables[] = {
            { "/proc/net/tcp",  AF_INET,  IPPROTO_TCP, NetworkTransport::Tcp, NetworkAddressFamily::IPv4 },
            { "/proc/net/tcp6", AF_INET6, IPPROTO_TCP, NetworkTransport::Tcp, NetworkAddressFamily::IPv6 },
            { "/proc/net/udp",  AF_INET,  IPPROTO_UDP, NetworkTransport::Udp, NetworkAddressFamily::IPv4 },
            { "/proc/net/udp6", AF_INET6, IPPROTO_UDP, NetworkTransport::Udp, NetworkAddressFamily::IPv6 },
        };

        int readable = 0;
        int lastError = 0;
        bool usedNetlink = false;
        bool sawByteCounters = false;
        std::string netlinkError;
        for (const Table& table : kTables) {
            int error = 0;
            if (DumpTable(table.family, table.protocol, table.transport, table.addressFamily,
                          out, error, sawByteCounters)) {
                ++readable;
                usedNetlink = true;
                continue;
            }
            // ENOENT / EOPNOTSUPP: no diag module for this protocol; EPERM /
            // EACCES: a sandbox forbids netlink. Either way the file still works.
            if (netlinkError.empty() && table.protocol == IPPROTO_TCP) {
                netlinkError = std::strerror(error);
            }
            std::string text;
            // A kernel without IPv6 has no tcp6/udp6; that is not a failure.
            if (!ReadWholeFile(table.path, text, error)) { lastError = error; continue; }
            ++readable;
            NetworkMonitorProcfs::ParseTable(text, table.transport, table.addressFamily, out);
        }
        lastUsedNetlink_ = usedNetlink;
        lastSawByteCounters_ = sawByteCounters;
        lastNetlinkError_ = netlinkError;
        if (readable == 0) {
            const bool denied = lastError == EACCES || lastError == EPERM;
            return NetworkMonitorResult::Error(
                denied ? NetworkMonitorResultCode::PermissionDenied
                       : NetworkMonitorResultCode::IoError,
                std::string("Could not read the socket table: ") + std::strerror(lastError));
        }

        if (resolveProcesses) AttributeProcesses(out);
        for (auto& connection : out) {
            if (connection.process && connection.ownerUid) {
                connection.process->userName = UserNameForUid(*connection.ownerUid, userNames_);
            }
        }
        return NetworkMonitorResult::Ok();
    }

private:
    // Walks /proc/<pid>/fd for every process, builds inode -> pid, and fills
    // `process` on each connection whose inode was found. Identities are
    // cached by PID across snapshots and evicted when the PID is gone, so the
    // steady-state cost is the readlink per descriptor, not the exe/comm reads.
    void AttributeProcesses(std::vector<NetworkConnection>& connections) {
        std::unordered_map<uint64_t, uint32_t> inodeToPid;
        std::unordered_map<uint32_t, ProcessIdentity> seen;
        int unreadable = 0;

        DIR* proc = ::opendir("/proc");
        if (!proc) return;
        while (struct dirent* entry = ::readdir(proc)) {
            if (!IsAllDigits(entry->d_name)) continue;
            const uint32_t pid = static_cast<uint32_t>(std::strtoul(entry->d_name, nullptr, 10));
            const std::string fdPath = "/proc/" + std::string(entry->d_name) + "/fd";
            DIR* fds = ::opendir(fdPath.c_str());
            if (!fds) {
                if (errno == EACCES || errno == EPERM) ++unreadable;
                continue;
            }
            bool ownsSockets = false;
            while (struct dirent* fd = ::readdir(fds)) {
                if (fd->d_name[0] == '.') continue;
                const std::string target = ReadLink(fdPath + "/" + fd->d_name);
                // "socket:[12345]"
                if (target.size() < 9 || target.compare(0, 8, "socket:[") != 0) continue;
                const uint64_t inode = std::strtoull(target.c_str() + 8, nullptr, 10);
                if (inode == 0) continue;
                inodeToPid.emplace(inode, pid);
                ownsSockets = true;
            }
            ::closedir(fds);
            if (ownsSockets) seen.emplace(pid, IdentityFor(pid));
        }
        ::closedir(proc);
        lastUnreadableProcesses_ = unreadable;

        // The cache is exactly the set of socket-owning processes this walk saw.
        identities_.swap(seen);

        for (auto& connection : connections) {
            if (connection.socketInode == 0) continue;
            auto found = inodeToPid.find(connection.socketInode);
            if (found == inodeToPid.end()) continue;
            auto identity = identities_.find(found->second);
            if (identity != identities_.end()) connection.process = identity->second;
        }
    }

    ProcessIdentity IdentityFor(uint32_t pid) {
        auto cached = identities_.find(pid);
        if (cached != identities_.end()) return cached->second;

        ProcessIdentity identity;
        identity.pid = pid;
        const std::string base = "/proc/" + std::to_string(pid);
        identity.executablePath = ReadLink(base + "/exe");
        // " (deleted)" marks a binary replaced since it started; keep the
        // path, it is still the right name.
        identity.displayName = ReadFirstLine(base + "/comm");
        if (identity.displayName.empty() && !identity.executablePath.empty()) {
            const std::size_t slash = identity.executablePath.rfind('/');
            identity.displayName = slash == std::string::npos
                ? identity.executablePath : identity.executablePath.substr(slash + 1);
        }
        if (identity.displayName.empty()) identity.displayName = "pid " + std::to_string(pid);
        return identity;
    }

    std::unordered_map<uint32_t, ProcessIdentity> identities_;
    std::unordered_map<uint32_t, std::string> userNames_;
    int lastUnreadableProcesses_ = 0;
    bool lastUsedNetlink_ = false;
    bool lastSawByteCounters_ = false;
    std::string lastNetlinkError_;
};

} // namespace

std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend() {
    return std::make_unique<LinuxNetworkMonitorBackend>();
}

} // namespace UltraCanvas
