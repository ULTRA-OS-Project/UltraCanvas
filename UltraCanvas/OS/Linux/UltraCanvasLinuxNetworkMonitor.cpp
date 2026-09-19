// OS/Linux/UltraCanvasLinuxNetworkMonitor.cpp
// Linux backend for NetworkMonitor: the socket table from
// /proc/net/{tcp,tcp6,udp,udp6}, and process attribution by walking every
// /proc/<pid>/fd/ for links of the form "socket:[<inode>]" - the join that
// `ss -p` and `netstat -p` perform. No daemon, no helper, no extra library,
// so it works on a minimal system and inside a container.
//
// What it cannot see, it says: a process owned by another user has an
// unreadable fd directory unless the monitor runs as root, and every such
// process is counted and reported through the capabilities' notes rather
// than silently left out of the table.
//
// Version: 0.1.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorBackend.h"
#include "NetworkMonitor/NetworkMonitorProcfs.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

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

// ===== THE BACKEND =====

class LinuxNetworkMonitorBackend : public INetworkMonitorBackend {
public:
    NetworkMonitorCapabilities Capabilities() const override {
        NetworkMonitorCapabilities caps;
        caps.backendName = "procfs";
        caps.socketTable = ::access("/proc/net/tcp", R_OK) == 0 ||
                           ::access("/proc/net/tcp6", R_OK) == 0;
        caps.processAttribution = caps.socketTable && ::access("/proc/self/fd", R_OK) == 0;
        caps.allUsers = ::geteuid() == 0;
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
        caps.notes.push_back("Byte counters and connection events are not collected "
                             "by the procfs backend (Phase 2).");
        return caps;
    }

    NetworkMonitorResult Snapshot(std::vector<NetworkConnection>& out,
                                  bool resolveProcesses) override {
        out.clear();
        struct Table { const char* path; NetworkTransport transport; NetworkAddressFamily family; };
        static const Table kTables[] = {
            { "/proc/net/tcp",  NetworkTransport::Tcp, NetworkAddressFamily::IPv4 },
            { "/proc/net/tcp6", NetworkTransport::Tcp, NetworkAddressFamily::IPv6 },
            { "/proc/net/udp",  NetworkTransport::Udp, NetworkAddressFamily::IPv4 },
            { "/proc/net/udp6", NetworkTransport::Udp, NetworkAddressFamily::IPv6 },
        };

        int readable = 0;
        int lastError = 0;
        for (const Table& table : kTables) {
            std::string text;
            int err = 0;
            // A kernel without IPv6 has no tcp6/udp6; that is not a failure.
            if (!ReadWholeFile(table.path, text, err)) { lastError = err; continue; }
            ++readable;
            NetworkMonitorProcfs::ParseTable(text, table.transport, table.family, out);
        }
        if (readable == 0) {
            const bool denied = lastError == EACCES || lastError == EPERM;
            return NetworkMonitorResult::Error(
                denied ? NetworkMonitorResultCode::PermissionDenied
                       : NetworkMonitorResultCode::IoError,
                std::string("Could not read /proc/net: ") + std::strerror(lastError));
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
};

} // namespace

std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend() {
    return std::make_unique<LinuxNetworkMonitorBackend>();
}

} // namespace UltraCanvas
