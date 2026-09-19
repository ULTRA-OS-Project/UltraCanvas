// OS/MSWindows/UltraCanvasWindowsNetworkMonitor.cpp
// Windows backend for NetworkMonitor, built on documented Win32 only: the
// socket tables from IP Helper (GetExtendedTcpTable / GetExtendedUdpTable
// with the *_OWNER_PID classes, which hand back the owning PID directly -
// no second lookup), the executable from QueryFullProcessImageNameW, and the
// owning user from the process token. No WMI, no ETW yet (that is the
// event-rate collector of a later phase), no elevation required for the
// tables themselves.
//
// What it cannot see, it says: a process this monitor may not open (another
// user's, when not elevated; a protected process) keeps its PID but gets no
// executable path, and every such process is counted and reported through
// the capabilities' notes.
//
// Version: 0.2.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS

// QueryFullProcessImageNameW and the token elevation query are Vista+; the
// SDK derives NTDDI_VERSION from _WIN32_WINNT and errors on a mismatch, so
// both are set together, the way UltraCanvasWindowsHardwareInfo.cpp does it.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "NetworkMonitor/NetworkMonitorAddress.h"
#include "NetworkMonitor/NetworkMonitorBackend.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
// winsock2.h must precede windows.h so the ancient winsock.h is never pulled
// in; iphlpapi.h builds on the v2 declarations.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

namespace UltraCanvas {
namespace {

std::string Utf8FromWide(const wchar_t* text, int length) {
    if (!text || length <= 0) return std::string();
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), needed, nullptr, nullptr);
    return out;
}

// The port fields of the MIB rows hold the port in network byte order in
// their low 16 bits.
uint16_t PortFromRow(DWORD field) {
    return ntohs(static_cast<u_short>(field & 0xFFFFu));
}

std::string IPv4FromRow(DWORD field) {
    unsigned char bytes[4];
    std::memcpy(bytes, &field, sizeof bytes);   // in_addr: already network order
    return NetworkMonitorAddress::FormatIPv4(bytes);
}

// MIB_TCP_STATE_* numbering (tcpmib.h) - not the kernel's.
NetworkConnectionState StateFromMib(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED:     return NetworkConnectionState::Closed;
        case MIB_TCP_STATE_LISTEN:     return NetworkConnectionState::Listening;
        case MIB_TCP_STATE_SYN_SENT:   return NetworkConnectionState::SynSent;
        case MIB_TCP_STATE_SYN_RCVD:   return NetworkConnectionState::SynReceived;
        case MIB_TCP_STATE_ESTAB:      return NetworkConnectionState::Established;
        case MIB_TCP_STATE_FIN_WAIT1:  return NetworkConnectionState::FinWait1;
        case MIB_TCP_STATE_FIN_WAIT2:  return NetworkConnectionState::FinWait2;
        case MIB_TCP_STATE_CLOSE_WAIT: return NetworkConnectionState::CloseWait;
        case MIB_TCP_STATE_CLOSING:    return NetworkConnectionState::Closing;
        case MIB_TCP_STATE_LAST_ACK:   return NetworkConnectionState::LastAck;
        case MIB_TCP_STATE_TIME_WAIT:  return NetworkConnectionState::TimeWait;
        case MIB_TCP_STATE_DELETE_TCB: return NetworkConnectionState::Closed;
        default:                       return NetworkConnectionState::Unknown;
    }
}

// Fetches one table into `buffer`, growing it as IP Helper asks. False when
// the call fails for a reason other than the buffer size.
bool FetchTable(std::vector<unsigned char>& buffer, bool tcp, ULONG family, DWORD& error) {
    ULONG size = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD result = tcp
            ? ::GetExtendedTcpTable(size ? buffer.data() : nullptr, &size, FALSE, family,
                                    TCP_TABLE_OWNER_PID_ALL, 0)
            : ::GetExtendedUdpTable(size ? buffer.data() : nullptr, &size, FALSE, family,
                                    UDP_TABLE_OWNER_PID, 0);
        if (result == NO_ERROR && size != 0) return true;
        if (result != ERROR_INSUFFICIENT_BUFFER && result != NO_ERROR) { error = result; return false; }
        buffer.resize(size ? size : 1);
    }
    error = ERROR_INSUFFICIENT_BUFFER;
    return false;
}

class WindowsNetworkMonitorBackend : public INetworkMonitorBackend {
public:
    NetworkMonitorCapabilities Capabilities() const override {
        NetworkMonitorCapabilities caps;
        caps.backendName = "iphlpapi";
        caps.socketTable = true;
        caps.processAttribution = true;
        caps.allUsers = IsElevated();
        if (!caps.allUsers) {
            caps.notes.push_back("Not elevated: every socket carries its PID, but the "
                                 "executable and user of another user's process cannot be "
                                 "read. Run as administrator for the full picture.");
        }
        if (lastUnreadableProcesses_ > 0) {
            caps.notes.push_back(std::to_string(lastUnreadableProcesses_) +
                                 " processes could not be opened in the last snapshot.");
        }
        caps.notes.push_back("Byte counters and connection events are not collected by the "
                             "IP Helper backend (ETW, later phase).");
        return caps;
    }

    NetworkMonitorResult Snapshot(std::vector<NetworkConnection>& out,
                                  bool resolveProcesses) override {
        out.clear();
        std::vector<unsigned char> buffer;
        DWORD error = 0;
        int readable = 0;

        if (FetchTable(buffer, true, AF_INET, error)) {
            ++readable;
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_TCPROW_OWNER_PID& row = table->table[i];
                NetworkConnection c;
                c.transport = NetworkTransport::Tcp;
                c.family = NetworkAddressFamily::IPv4;
                c.localAddress = IPv4FromRow(row.dwLocalAddr);
                c.localPort = PortFromRow(row.dwLocalPort);
                c.remoteAddress = IPv4FromRow(row.dwRemoteAddr);
                c.remotePort = PortFromRow(row.dwRemotePort);
                c.state = StateFromMib(row.dwState);
                c.process = Identity(row.dwOwningPid, resolveProcesses);
                out.push_back(std::move(c));
            }
        }
        if (FetchTable(buffer, true, AF_INET6, error)) {
            ++readable;
            const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_TCP6ROW_OWNER_PID& row = table->table[i];
                NetworkConnection c;
                c.transport = NetworkTransport::Tcp;
                c.family = NetworkAddressFamily::IPv6;
                c.localAddress = NetworkMonitorAddress::FormatIPv6(row.ucLocalAddr);
                c.localPort = PortFromRow(row.dwLocalPort);
                c.remoteAddress = NetworkMonitorAddress::FormatIPv6(row.ucRemoteAddr);
                c.remotePort = PortFromRow(row.dwRemotePort);
                c.state = StateFromMib(row.dwState);
                c.process = Identity(row.dwOwningPid, resolveProcesses);
                out.push_back(std::move(c));
            }
        }
        if (FetchTable(buffer, false, AF_INET, error)) {
            ++readable;
            const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_UDPROW_OWNER_PID& row = table->table[i];
                NetworkConnection c;
                c.transport = NetworkTransport::Udp;
                c.family = NetworkAddressFamily::IPv4;
                c.localAddress = IPv4FromRow(row.dwLocalAddr);
                c.localPort = PortFromRow(row.dwLocalPort);
                c.remoteAddress = "0.0.0.0";
                c.state = NetworkConnectionState::Unconnected;
                c.process = Identity(row.dwOwningPid, resolveProcesses);
                out.push_back(std::move(c));
            }
        }
        if (FetchTable(buffer, false, AF_INET6, error)) {
            ++readable;
            const auto* table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_UDP6ROW_OWNER_PID& row = table->table[i];
                NetworkConnection c;
                c.transport = NetworkTransport::Udp;
                c.family = NetworkAddressFamily::IPv6;
                c.localAddress = NetworkMonitorAddress::FormatIPv6(row.ucLocalAddr);
                c.localPort = PortFromRow(row.dwLocalPort);
                c.remoteAddress = "::";
                c.state = NetworkConnectionState::Unconnected;
                c.process = Identity(row.dwOwningPid, resolveProcesses);
                out.push_back(std::move(c));
            }
        }

        // Identities are re-read per snapshot only for PIDs not seen before;
        // drop the ones that vanished so a reused PID is looked up afresh.
        std::unordered_map<uint32_t, ProcessIdentity> kept;
        int unreadable = 0;
        for (const auto& c : out) {
            if (!c.process) continue;
            auto found = identities_.find(c.process->pid);
            if (found != identities_.end()) kept.emplace(*found);
        }
        if (resolveProcesses) {
            for (const auto& [pid, identity] : kept) {
                if (identity.executablePath.empty() && pid > 4) ++unreadable;
            }
        }
        identities_.swap(kept);
        lastUnreadableProcesses_ = unreadable;

        if (readable == 0) {
            return NetworkMonitorResult::Error(
                error == ERROR_ACCESS_DENIED ? NetworkMonitorResultCode::PermissionDenied
                                             : NetworkMonitorResultCode::IoError,
                "GetExtendedTcpTable / GetExtendedUdpTable failed with error " +
                    std::to_string(error));
        }
        return NetworkMonitorResult::Ok();
    }

private:
    static bool IsElevated() {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof elevation;
        const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation, size, &size) != 0;
        ::CloseHandle(token);
        return ok && elevation.TokenIsElevated != 0;
    }

    // The PID is always known on Windows, so `process` is always set; the
    // path and user are best effort. `resolve` false keeps only the PID.
    std::optional<ProcessIdentity> Identity(DWORD pid, bool resolve) {
        auto cached = identities_.find(static_cast<uint32_t>(pid));
        if (cached != identities_.end()) return cached->second;

        ProcessIdentity identity;
        identity.pid = static_cast<uint32_t>(pid);
        if (pid == 0) {
            identity.displayName = "System Idle Process";
        } else if (pid == 4) {
            identity.displayName = "System";
        } else if (resolve) {
            HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                wchar_t path[MAX_PATH * 2];
                DWORD length = static_cast<DWORD>(sizeof path / sizeof path[0]);
                if (::QueryFullProcessImageNameW(process, 0, path, &length)) {
                    identity.executablePath = Utf8FromWide(path, static_cast<int>(length));
                }
                identity.userName = UserOf(process);
                ::CloseHandle(process);
            }
        }
        if (identity.displayName.empty()) {
            if (!identity.executablePath.empty()) {
                const std::size_t slash = identity.executablePath.find_last_of("\\/");
                identity.displayName = slash == std::string::npos
                    ? identity.executablePath : identity.executablePath.substr(slash + 1);
                // "firefox.exe" reads as "firefox", the way comm does on Linux.
                if (identity.displayName.size() > 4) {
                    std::string tail = identity.displayName.substr(identity.displayName.size() - 4);
                    for (auto& ch : tail) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
                    if (tail == ".exe") identity.displayName.resize(identity.displayName.size() - 4);
                }
            } else {
                identity.displayName = "pid " + std::to_string(pid);
            }
        }
        identities_.emplace(identity.pid, identity);
        return identity;
    }

    static std::string UserOf(HANDLE process) {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(process, TOKEN_QUERY, &token)) return std::string();
        DWORD size = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::string name;
        if (size > 0) {
            std::vector<unsigned char> buffer(size);
            if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
                const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
                wchar_t account[256];
                wchar_t domain[256];
                DWORD accountLength = 256;
                DWORD domainLength = 256;
                SID_NAME_USE use;
                if (::LookupAccountSidW(nullptr, user->User.Sid, account, &accountLength,
                                        domain, &domainLength, &use)) {
                    name = Utf8FromWide(account, static_cast<int>(accountLength));
                }
            }
        }
        ::CloseHandle(token);
        return name;
    }

    std::unordered_map<uint32_t, ProcessIdentity> identities_;
    int lastUnreadableProcesses_ = 0;
};

} // namespace

std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend() {
    return std::make_unique<WindowsNetworkMonitorBackend>();
}

} // namespace UltraCanvas
