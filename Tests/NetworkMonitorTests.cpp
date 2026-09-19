// Tests/NetworkMonitorTests.cpp
// Unit tests for the NetworkMonitor module (include/NetworkMonitor/): the
// /proc/net table parser against fixture text, the per-process roll-up, the
// endpoint formatting, and - where this platform has a backend - a live check
// that a socket this test opens appears in the snapshot attributed to this
// test's own PID. Where there is no backend, the check is that the module
// says so rather than returning an empty table.
//
// Self-contained: no test framework, no UI stack, links only NetworkMonitor.
//
// Version: 0.1.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorProcfs.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) { std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }                 \
    } while (0)

// =============================================================================

static void TestDecodeAddress() {
    std::printf("Address decoding\n");
    std::string address;
    uint16_t port = 0;

    CHECK(NetworkMonitorProcfs::DecodeAddress("0100007F:0050", NetworkAddressFamily::IPv4, address, port)
          && address == "127.0.0.1" && port == 80,
          "IPv4 words are host byte order: 0100007F is 127.0.0.1");
    CHECK(NetworkMonitorProcfs::DecodeAddress("00000000:0000", NetworkAddressFamily::IPv4, address, port)
          && address == "0.0.0.0" && port == 0, "the IPv4 wildcard");
    CHECK(NetworkMonitorProcfs::DecodeAddress("0A00A8C0:01BB", NetworkAddressFamily::IPv4, address, port)
          && address == "192.168.0.10" && port == 443, "a LAN address with port 443");

    CHECK(NetworkMonitorProcfs::DecodeAddress("00000000000000000000000001000000:0016",
                                              NetworkAddressFamily::IPv6, address, port)
          && address == "::1" && port == 22, "IPv6 loopback collapses to ::1");
    CHECK(NetworkMonitorProcfs::DecodeAddress("00000000000000000000000000000000:0000",
                                              NetworkAddressFamily::IPv6, address, port)
          && address == "::", "the IPv6 wildcard is ::");
    CHECK(NetworkMonitorProcfs::DecodeAddress("B80D0120000000000000000001000000:0050",
                                              NetworkAddressFamily::IPv6, address, port)
          && address == "2001:db8::1" && port == 80, "2001:db8::1 - the longest zero run collapses");
    CHECK(NetworkMonitorProcfs::DecodeAddress("0000000000000000FFFF00000100007F:01BB",
                                              NetworkAddressFamily::IPv6, address, port)
          && address == "::ffff:127.0.0.1", "an IPv4-mapped peer prints in mixed notation");
    CHECK(NetworkMonitorProcfs::DecodeAddress("00000100000000000000000000000000:0000",
                                              NetworkAddressFamily::IPv6, address, port)
          && address == "1::", "a leading group with a trailing zero run");

    CHECK(!NetworkMonitorProcfs::DecodeAddress("0100007F", NetworkAddressFamily::IPv4, address, port),
          "a field without a port is refused");
    CHECK(!NetworkMonitorProcfs::DecodeAddress("ZZZZZZZZ:0050", NetworkAddressFamily::IPv4, address, port),
          "non-hex digits are refused");
    CHECK(!NetworkMonitorProcfs::DecodeAddress("0100007F:0050", NetworkAddressFamily::IPv6, address, port),
          "an IPv4 field is not an IPv6 field");
    CHECK(!NetworkMonitorProcfs::DecodeAddress("", NetworkAddressFamily::IPv4, address, port),
          "an empty field is refused");
}

static void TestStates() {
    std::printf("State codes\n");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x0A, NetworkTransport::Tcp) == NetworkConnectionState::Listening,
          "0A is LISTEN");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x01, NetworkTransport::Tcp) == NetworkConnectionState::Established,
          "01 is ESTABLISHED");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x06, NetworkTransport::Tcp) == NetworkConnectionState::TimeWait,
          "06 is TIME_WAIT");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x0C, NetworkTransport::Tcp) == NetworkConnectionState::SynReceived,
          "0C (NEW_SYN_RECV) reads as SYN_RECV");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x07, NetworkTransport::Udp) == NetworkConnectionState::Unconnected,
          "a UDP socket in TCP_CLOSE is unconnected");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x01, NetworkTransport::Udp) == NetworkConnectionState::Established,
          "a connect()ed UDP socket is established");
    CHECK(NetworkMonitorProcfs::StateFromCode(0x99, NetworkTransport::Tcp) == NetworkConnectionState::Unknown,
          "an unknown code stays unknown rather than guessing");
    CHECK(std::string(NetworkMonitor_StateName(NetworkConnectionState::CloseWait)) == "CLOSE_WAIT",
          "state names use the kernel's spelling");
}

static void TestParseTable() {
    std::printf("Table parsing\n");
    const std::string fixture =
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
        "   0: 0100007F:0277 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 18342 1 0000000000000000 100 0 0 10 0\n"
        "   1: 0A00A8C0:A1B2 5DB8D822:01BB 01 00000000:00000000 02:000A1F3C 00000000  1000        0 27615 1 0000000000000000 22 4 30 10 -1\n"
        "this line is not a row at all\n"
        "   2: GARBAGE:0000 00000000:0000 01 00000000:00000000 00:00000000 00000000     0        0 1 1\n";

    std::vector<NetworkConnection> rows;
    const std::size_t appended = NetworkMonitorProcfs::ParseTable(
        fixture, NetworkTransport::Tcp, NetworkAddressFamily::IPv4, rows);
    CHECK(appended == 2 && rows.size() == 2, "two well-formed rows parse; the header and the garbage do not");
    if (rows.size() == 2) {
        CHECK(rows[0].localAddress == "127.0.0.1" && rows[0].localPort == 631 &&
              rows[0].state == NetworkConnectionState::Listening,
              "row 0 is a listener on 127.0.0.1:631");
        CHECK(rows[0].ownerUid && *rows[0].ownerUid == 0 && rows[0].socketInode == 18342,
              "row 0 carries uid 0 and inode 18342");
        CHECK(rows[1].remoteAddress == "34.216.184.93" && rows[1].remotePort == 443 &&
              rows[1].state == NetworkConnectionState::Established,
              "row 1 is established to 34.216.184.93:443");
        CHECK(rows[1].ownerUid && *rows[1].ownerUid == 1000 && rows[1].socketInode == 27615,
              "row 1 carries uid 1000 and inode 27615");
        CHECK(!rows[1].process && !rows[1].bytesSent && !rows[1].bytesReceived,
              "the parser leaves process and byte counters unset");
        CHECK(rows[0].transport == NetworkTransport::Tcp && rows[0].family == NetworkAddressFamily::IPv4,
              "rows carry the table's transport and family");
    }

    std::vector<NetworkConnection> udp;
    NetworkMonitorProcfs::ParseTable(
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
        "  27: 00000000:0044 00000000:0000 07 00000000:00000000 00:00000000 00000000     0        0 12 2 0000000000000000 0\n",
        NetworkTransport::Udp, NetworkAddressFamily::IPv4, udp);
    CHECK(udp.size() == 1 && udp[0].state == NetworkConnectionState::Unconnected &&
          udp[0].localPort == 68, "a UDP table row: unconnected on port 68");
}

static NetworkConnection MakeConnection(uint32_t pid, const char* name, const char* remote,
                                        NetworkConnectionState state) {
    NetworkConnection c;
    c.localAddress = "10.0.0.5";
    c.localPort = 40000;
    c.remoteAddress = remote;
    c.remotePort = 443;
    c.state = state;
    if (pid != 0) {
        ProcessIdentity p;
        p.pid = pid;
        p.displayName = name;
        c.process = p;
    }
    return c;
}

static void TestSummarize() {
    std::printf("Per-process roll-up\n");
    std::vector<NetworkConnection> rows;
    rows.push_back(MakeConnection(100, "firefox", "1.1.1.1", NetworkConnectionState::Established));
    rows.push_back(MakeConnection(100, "firefox", "0.0.0.0", NetworkConnectionState::Listening));
    rows.push_back(MakeConnection(100, "firefox", "1.1.1.1", NetworkConnectionState::Established));
    rows.push_back(MakeConnection(200, "sshd", "8.8.8.8", NetworkConnectionState::Established));
    rows.push_back(MakeConnection(0, "", "9.9.9.9", NetworkConnectionState::Established));

    const auto groups = NetworkMonitor_SummarizeByProcess(rows);
    CHECK(groups.size() == 3, "three groups: two processes and the unattributed rest");
    if (groups.size() == 3) {
        CHECK(groups[0].process.displayName == "firefox" && groups[0].connectionCount == 3,
              "the busiest process sorts first");
        CHECK(groups[0].establishedCount == 2 && groups[0].listeningCount == 1,
              "established and listening are counted separately");
        CHECK(groups[0].remoteAddresses.size() == 1 && groups[0].remoteAddresses[0] == "1.1.1.1",
              "remote addresses are distinct and exclude the listener's wildcard");
        CHECK(!groups[0].bytesSent && !groups[0].bytesReceived,
              "no byte totals when no connection reported them");
        CHECK(groups[1].process.displayName == "(unattributed)" && !groups[1].attributed &&
              groups[1].process.pid == 0,
              "unattributed sockets form their own group, before sshd by name");
        CHECK(groups[2].process.displayName == "sshd" && groups[2].attributed,
              "an attributed single-connection process");
    }

    // Byte totals appear only when every connection in the group has them.
    std::vector<NetworkConnection> counted;
    counted.push_back(MakeConnection(300, "curl", "1.1.1.1", NetworkConnectionState::Established));
    counted.push_back(MakeConnection(300, "curl", "1.1.1.1", NetworkConnectionState::Established));
    counted[0].bytesSent = 10; counted[0].bytesReceived = 100;
    counted[1].bytesSent = 5;  counted[1].bytesReceived = 50;
    auto summed = NetworkMonitor_SummarizeByProcess(counted);
    CHECK(summed.size() == 1 && summed[0].bytesSent && *summed[0].bytesSent == 15 &&
          summed[0].bytesReceived && *summed[0].bytesReceived == 150,
          "byte totals sum when every connection reports them");
    counted[1].bytesReceived.reset();
    summed = NetworkMonitor_SummarizeByProcess(counted);
    CHECK(summed[0].bytesSent && *summed[0].bytesSent == 15 && !summed[0].bytesReceived,
          "one unknown counter makes that total unknown, not smaller");

    CHECK(NetworkMonitor_SummarizeByProcess({}).empty(), "an empty snapshot rolls up to nothing");
}

static void TestFormatting() {
    std::printf("Endpoint formatting\n");
    CHECK(NetworkMonitor_FormatEndpoint("1.2.3.4", 443) == "1.2.3.4:443", "IPv4 host:port");
    CHECK(NetworkMonitor_FormatEndpoint("::1", 22) == "[::1]:22", "IPv6 is bracketed");
    CHECK(NetworkMonitor_FormatEndpoint("", 0) == "*:0", "an empty address is a star");
    CHECK(std::string(NetworkMonitor_TransportName(NetworkTransport::Udp)) == "UDP", "transport names");

    NetworkConnection c;
    c.localAddress = "127.0.0.1"; c.remoteAddress = "127.0.0.1";
    CHECK(c.IsLoopback(), "127.0.0.1 is loopback");
    c.localAddress = "::1"; c.remoteAddress = "::";
    CHECK(c.IsLoopback(), "::1 is loopback");
    c.localAddress = "::"; c.remoteAddress = "::ffff:127.0.0.1";
    CHECK(c.IsLoopback(), "the IPv4-mapped loopback is loopback");
    c.localAddress = "10.0.0.5"; c.remoteAddress = "1.1.1.1";
    CHECK(!c.IsLoopback(), "a LAN-to-internet flow is not");
    CHECK(c.RemoteEndpoint() == "1.1.1.1:0", "RemoteEndpoint() formats the remote side");
}

static void TestLiveSnapshot() {
    std::printf("Live snapshot\n");
    const NetworkMonitorCapabilities caps = NetworkMonitor_GetCapabilities();
    std::printf("  backend: %s\n", caps.backendName.c_str());
    for (const auto& note : caps.notes) std::printf("  note: %s\n", note.c_str());

    std::vector<NetworkConnection> connections;
    const NetworkMonitorResult result = NetworkMonitor_ListConnections(connections);

    if (!NetworkMonitor_IsAvailable()) {
        CHECK(!result && result.code == NetworkMonitorResultCode::NotSupported,
              "without a backend, a snapshot reports NotSupported");
        CHECK(caps.backendName == "none" && !caps.socketTable,
              "and the capabilities say so");
        return;
    }
    if (!caps.socketTable) {
        std::printf("  skipped: this backend cannot read the socket table here\n");
        CHECK(!result, "a backend without a socket table fails the snapshot rather than faking one");
        return;
    }
    CHECK(result, "a snapshot succeeds where the socket table is readable");

#ifndef _WIN32
    // Open a listener on an ephemeral loopback port, then find it in the
    // table attributed to this process. That exercises the whole join: the
    // table read, the inode, the /proc walk and the identity resolution.
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0, "a loopback listener can be opened");
    if (fd < 0) return;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bool bound = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 &&
                 ::listen(fd, 1) == 0;
    socklen_t length = sizeof address;
    bound = bound && ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0;
    CHECK(bound, "the listener is bound to an ephemeral port");
    const uint16_t port = ntohs(address.sin_port);

    const NetworkMonitorResult again = NetworkMonitor_ListConnections(connections);
    CHECK(again, "a second snapshot succeeds");
    const NetworkConnection* mine = nullptr;
    for (const auto& c : connections) {
        if (c.transport == NetworkTransport::Tcp && c.localPort == port &&
            c.state == NetworkConnectionState::Listening && c.localAddress == "127.0.0.1") {
            mine = &c;
            break;
        }
    }
    CHECK(mine != nullptr, "the listener this test opened is in the snapshot");
    if (mine) {
        CHECK(mine->socketInode != 0, "with its socket inode");
        CHECK(mine->process && mine->process->pid == static_cast<uint32_t>(::getpid()),
              "attributed to this test's own PID");
        if (mine->process) {
            CHECK(!mine->process->displayName.empty(), "with a display name");
            CHECK(!mine->process->userName.empty(), "and the owning user's name");
            std::printf("  seen as: %s (pid %u, user %s) %s\n",
                        mine->process->displayName.c_str(), mine->process->pid,
                        mine->process->userName.c_str(), mine->LocalEndpoint().c_str());
        }
        CHECK(mine->IsLoopback(), "and it is loopback");
    }

    // The loopback filter must drop it; the listener filter must drop it.
    NetworkMonitorOptions noLoopback;
    noLoopback.includeLoopback = false;
    std::vector<NetworkConnection> filtered;
    NetworkMonitor_ListConnections(filtered, noLoopback);
    bool stillThere = false;
    for (const auto& c : filtered) if (c.localPort == port && c.transport == NetworkTransport::Tcp) stillThere = true;
    CHECK(!stillThere, "includeLoopback = false removes it");

    NetworkMonitorOptions noListeners;
    noListeners.includeListening = false;
    NetworkMonitor_ListConnections(filtered, noListeners);
    stillThere = false;
    for (const auto& c : filtered) if (c.localPort == port && c.transport == NetworkTransport::Tcp) stillThere = true;
    CHECK(!stillThere, "includeListening = false removes it");

    ::close(fd);

    const auto groups = NetworkMonitor_SummarizeByProcess(connections);
    bool selfGroup = false;
    for (const auto& g : groups) if (g.attributed && g.process.pid == static_cast<uint32_t>(::getpid())) selfGroup = true;
    CHECK(selfGroup, "the roll-up has a group for this process");
#endif
}

int main() {
    std::printf("=== NetworkMonitor tests ===\n");
    TestDecodeAddress();
    TestStates();
    TestParseTable();
    TestSummarize();
    TestFormatting();
    TestLiveSnapshot();
    std::printf("=== %s ===\n", g_failures == 0 ? "all tests passed" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
