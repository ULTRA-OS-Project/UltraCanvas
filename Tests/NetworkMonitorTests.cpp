// Tests/NetworkMonitorTests.cpp
// Unit tests for the NetworkMonitor module (include/NetworkMonitor/): the
// shared address formatter and the /proc/net table parser against fixture
// text, the per-process roll-up, the endpoint formatting, and - where this
// platform has a backend - a live check that sockets this test opens appear
// in the snapshot attributed to this test's own PID, with byte counters
// where the backend collects them. Where there is no backend, the check is
// that the module says so rather than returning an empty table.
//
// Self-contained: no test framework, no UI stack, links only NetworkMonitor.
//
// Version: 0.3.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorAddress.h"
#include "NetworkMonitor/NetworkMonitorProcfs.h"
#include "NetworkMonitor/NetworkMonitorStore.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    using SocketHandle = SOCKET;
    static const SocketHandle kNoSocket = INVALID_SOCKET;
    static void CloseSocket(SocketHandle s) { ::closesocket(s); }
    static uint32_t OwnPid() { return static_cast<uint32_t>(::GetCurrentProcessId()); }
    static bool SocketsUp() { WSADATA data; return ::WSAStartup(MAKEWORD(2, 2), &data) == 0; }
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using SocketHandle = int;
    static const SocketHandle kNoSocket = -1;
    static void CloseSocket(SocketHandle s) { ::close(s); }
    static uint32_t OwnPid() { return static_cast<uint32_t>(::getpid()); }
    static bool SocketsUp() { return true; }
#endif

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) { std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }                 \
    } while (0)

// =============================================================================

static void TestAddressFormatting() {
    std::printf("Address formatting (shared by every backend)\n");
    const unsigned char loopback4[4] = {127, 0, 0, 1};
    CHECK(NetworkMonitorAddress::FormatIPv4(loopback4) == "127.0.0.1", "IPv4 bytes in network order");
    const unsigned char loopback6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(NetworkMonitorAddress::FormatIPv6(loopback6) == "::1", "IPv6 loopback collapses to ::1");
    const unsigned char mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 10, 0, 0, 7};
    CHECK(NetworkMonitorAddress::FormatIPv6(mapped) == "::ffff:10.0.0.7", "IPv4-mapped in mixed notation");
    const unsigned char documentation[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(NetworkMonitorAddress::FormatIPv6(documentation) == "2001:db8::1", "the longest zero run collapses");
    const unsigned char twoRuns[16] = {0, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 3};
    CHECK(NetworkMonitorAddress::FormatIPv6(twoRuns) == "1:0:0:2::3", "of two equal runs the leftmost... no: the longer one wins");
}

static void TestDecodeAddress() {
    std::printf("procfs address decoding\n");
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

// ===== LIVE =====

// A loopback listener on an ephemeral port. kNoSocket when it cannot be made.
static SocketHandle OpenListener(uint16_t& port) {
    const SocketHandle fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kNoSocket) return kNoSocket;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 ||
        ::listen(fd, 1) != 0) {
        CloseSocket(fd);
        return kNoSocket;
    }
    socklen_t length = sizeof address;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        CloseSocket(fd);
        return kNoSocket;
    }
    port = ntohs(address.sin_port);
    return fd;
}

static const NetworkConnection* FindTcp(const std::vector<NetworkConnection>& connections,
                                        uint16_t localPort, uint16_t remotePort,
                                        NetworkConnectionState state) {
    for (const auto& c : connections) {
        if (c.transport == NetworkTransport::Tcp && c.localPort == localPort &&
            c.remotePort == remotePort && c.state == state &&
            (c.localAddress == "127.0.0.1" || c.localAddress == "::ffff:127.0.0.1")) {
            return &c;
        }
    }
    return nullptr;
}

static bool HasTcpOnPort(const std::vector<NetworkConnection>& connections, uint16_t port) {
    for (const auto& c : connections) {
        if (c.transport == NetworkTransport::Tcp && c.localPort == port) return true;
    }
    return false;
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
    if (!SocketsUp()) { CHECK(false, "the socket layer starts"); return; }

    // A listener, then a connection to it with data across it. That
    // exercises the whole join on every backend - table read, attribution,
    // identity - and, where the backend collects them, the byte counters.
    uint16_t listenerPort = 0;
    const SocketHandle listener = OpenListener(listenerPort);
    CHECK(listener != kNoSocket, "a loopback listener can be opened");
    if (listener == kNoSocket) return;

    const SocketHandle client = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    target.sin_port = htons(listenerPort);
    const bool connected = client != kNoSocket &&
        ::connect(client, reinterpret_cast<sockaddr*>(&target), sizeof target) == 0;
    CHECK(connected, "a client connects to it");
    const SocketHandle server = connected ? ::accept(listener, nullptr, nullptr) : kNoSocket;
    CHECK(server != kNoSocket, "and is accepted");

    uint16_t clientPort = 0;
    if (connected) {
        sockaddr_in local{};
        socklen_t length = sizeof local;
        if (::getsockname(client, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
            clientPort = ntohs(local.sin_port);
        }
    }

    // 64 KiB across the loop, fully drained, so the counters have settled.
    const std::size_t kPayload = 64 * 1024;
    std::size_t moved = 0;
    if (server != kNoSocket) {
        std::vector<char> chunk(4096, 'x');
        std::size_t sent = 0;
        while (sent < kPayload) {
            const auto n = ::send(client, chunk.data(), static_cast<int>(chunk.size()), 0);
            if (n <= 0) break;
            sent += static_cast<std::size_t>(n);
            std::size_t got = 0;
            while (got < static_cast<std::size_t>(n)) {
                const auto r = ::recv(server, chunk.data(), static_cast<int>(chunk.size()), 0);
                if (r <= 0) break;
                got += static_cast<std::size_t>(r);
            }
            moved += got;
        }
    }
    CHECK(moved == kPayload, "the payload crosses the connection");

    const NetworkMonitorResult again = NetworkMonitor_ListConnections(connections);
    CHECK(again, "a second snapshot succeeds");
    const NetworkMonitorCapabilities after = NetworkMonitor_GetCapabilities();

    const NetworkConnection* listening = FindTcp(connections, listenerPort, 0,
                                                 NetworkConnectionState::Listening);
    CHECK(listening != nullptr, "the listener this test opened is in the snapshot");
    if (listening) {
        CHECK(listening->process && listening->process->pid == OwnPid(),
              "attributed to this test's own PID");
        if (listening->process) {
            CHECK(!listening->process->displayName.empty(), "with a display name");
            std::printf("  seen as: %s (pid %u, user '%s') %s\n",
                        listening->process->displayName.c_str(), listening->process->pid,
                        listening->process->userName.c_str(), listening->LocalEndpoint().c_str());
        }
        CHECK(listening->IsLoopback(), "and it is loopback");
    }

    const NetworkConnection* flow = FindTcp(connections, clientPort, listenerPort,
                                            NetworkConnectionState::Established);
    CHECK(flow != nullptr, "the established client side is in the snapshot");
    if (flow) {
        CHECK(flow->process && flow->process->pid == OwnPid(), "also attributed to this PID");
        if (after.perConnectionBytes) {
            CHECK(flow->bytesSent && *flow->bytesSent >= kPayload,
                  "the client side reports at least the payload as sent");
            CHECK(flow->bytesReceived.has_value(), "and a received count");
            if (flow->bytesSent) {
                std::printf("  counters: sent %llu, received %llu\n",
                            static_cast<unsigned long long>(*flow->bytesSent),
                            static_cast<unsigned long long>(flow->bytesReceived.value_or(0)));
            }
            const NetworkConnection* peer = FindTcp(connections, listenerPort, clientPort,
                                                    NetworkConnectionState::Established);
            CHECK(peer && peer->bytesReceived && *peer->bytesReceived >= kPayload,
                  "the server side reports at least the payload as received");
        } else {
            std::printf("  skipped: this backend collects no byte counters\n");
            CHECK(!flow->bytesSent && !flow->bytesReceived,
                  "and leaves the counters unset rather than reporting zero");
        }
    }

    // The loopback filter must drop them; the listener filter must drop the listener.
    NetworkMonitorOptions noLoopback;
    noLoopback.includeLoopback = false;
    std::vector<NetworkConnection> filtered;
    NetworkMonitor_ListConnections(filtered, noLoopback);
    CHECK(!HasTcpOnPort(filtered, listenerPort) && !HasTcpOnPort(filtered, clientPort),
          "includeLoopback = false removes both");

    NetworkMonitorOptions noListeners;
    noListeners.includeListening = false;
    NetworkMonitor_ListConnections(filtered, noListeners);
    CHECK(FindTcp(filtered, listenerPort, 0, NetworkConnectionState::Listening) == nullptr,
          "includeListening = false removes the listener");
    CHECK(FindTcp(filtered, clientPort, listenerPort, NetworkConnectionState::Established) != nullptr,
          "but keeps the established flow");

    if (server != kNoSocket) CloseSocket(server);
    if (client != kNoSocket) CloseSocket(client);
    CloseSocket(listener);

    const auto groups = NetworkMonitor_SummarizeByProcess(connections);
    bool selfGroup = false;
    for (const auto& g : groups) if (g.attributed && g.process.pid == OwnPid()) selfGroup = true;
    CHECK(selfGroup, "the roll-up has a group for this process");
}

// ===== STORE =====

static void TestStore() {
    std::printf("Activity store\n");
    NetworkMonitorStoreHandle store = NetworkMonitorInvalidStore;
    NetworkMonitorStoreOptions options;
    options.path = ":memory:";
    options.retentionDays = 7;
    const NetworkMonitorResult opened = NetworkMonitor_OpenStore(options, store);

    if (!NetworkMonitor_StoreAvailable()) {
        CHECK(!opened && opened.code == NetworkMonitorResultCode::NotSupported,
              "without UltraDatabase, opening a store reports NotSupported");
        std::printf("  skipped: no UltraDatabase in this build\n");
        return;
    }
    CHECK(opened && store != NetworkMonitorInvalidStore, "an in-memory store opens");
    if (!opened) return;

    NetworkMonitorStoreOptions bad;
    NetworkMonitorStoreHandle none = NetworkMonitorInvalidStore;
    CHECK(!NetworkMonitor_OpenStore(bad, none) && none == NetworkMonitorInvalidStore,
          "a store without a path is refused");
    std::vector<RecordedFlow> flows;
    CHECK(NetworkMonitor_QueryFlows(12345, ActivityQuery(), flows).code ==
              NetworkMonitorResultCode::InvalidArgument,
          "a bad handle is refused");

    const int64_t t0 = 1'800'000'000;   // some Tuesday
    std::vector<NetworkConnection> snapshot;
    snapshot.push_back(MakeConnection(100, "firefox", "1.1.1.1", NetworkConnectionState::Established));
    snapshot[0].bytesSent = 1000; snapshot[0].bytesReceived = 5000; snapshot[0].socketInode = 77;
    snapshot.push_back(MakeConnection(200, "sshd", "0.0.0.0", NetworkConnectionState::Listening));
    snapshot[1].localPort = 22; snapshot[1].remotePort = 0;
    snapshot.push_back(MakeConnection(0, "", "9.9.9.9", NetworkConnectionState::Established));

    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0), "a snapshot records");
    snapshot[0].bytesSent = 2000; snapshot[0].bytesReceived = 9000;
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0 + 1), "a second snapshot a second later records");

    CHECK(NetworkMonitor_QueryFlows(store, ActivityQuery(), flows), "the flows read back");
    CHECK(flows.size() == 3, "three connections seen twice are three flows, not six");
    const RecordedFlow* firefox = nullptr;
    const RecordedFlow* orphan = nullptr;
    for (const auto& f : flows) {
        if (f.process && f.process->displayName == "firefox") firefox = &f;
        if (!f.process) orphan = &f;
    }
    CHECK(firefox != nullptr, "the browser's flow is there");
    if (firefox) {
        CHECK(firefox->snapshots == 2 && firefox->firstSeen == t0 && firefox->lastSeen == t0 + 1,
              "seen in both snapshots, with first and last sighting");
        CHECK(firefox->bytesSent && *firefox->bytesSent == 2000 &&
              firefox->bytesReceived && *firefox->bytesReceived == 9000,
              "and the latest counters");
        CHECK(firefox->process->pid == 100 && firefox->RemoteEndpoint() == "1.1.1.1:443",
              "with its process and peer");
    }
    CHECK(orphan != nullptr && orphan->snapshots == 2 && !orphan->bytesSent,
          "an unattributed socket is a flow too, without counters");

    // The same 5-tuple long after the last sighting is a new conversation.
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0 + 1 + kFlowContinuationSeconds + 1),
          "a snapshot past the continuation window records");
    CHECK(NetworkMonitor_QueryFlows(store, ActivityQuery(), flows) && flows.size() == 6,
          "and starts new flows rather than extending the old ones");
    CHECK(flows.front().lastSeen > flows.back().lastSeen, "newest first");

    ActivityQuery byName;
    byName.processName = "sshd";
    CHECK(NetworkMonitor_QueryFlows(store, byName, flows) && flows.size() == 2 &&
          flows[0].lastState == NetworkConnectionState::Listening,
          "filtering by process name");
    ActivityQuery noListeners;
    noListeners.includeListening = false;
    CHECK(NetworkMonitor_QueryFlows(store, noListeners, flows) && flows.size() == 4,
          "leaving listeners out");
    ActivityQuery byText;
    byText.text = "9.9.9";
    CHECK(NetworkMonitor_QueryFlows(store, byText, flows) && flows.size() == 2 &&
          flows[0].remoteAddress == "9.9.9.9", "a substring over the addresses");
    ActivityQuery byPid;
    byPid.pid = 100;
    CHECK(NetworkMonitor_QueryFlows(store, byPid, flows) && flows.size() == 2, "filtering by PID");
    ActivityQuery recent;
    recent.since = t0 + 100;
    CHECK(NetworkMonitor_QueryFlows(store, recent, flows) && flows.size() == 3,
          "a since-time keeps only the later sightings");
    ActivityQuery limited;
    limited.limit = 2;
    CHECK(NetworkMonitor_QueryFlows(store, limited, flows) && flows.size() == 2, "the limit holds");

    NetworkMonitorStoreStats stats;
    CHECK(NetworkMonitor_StoreStats(store, stats) && stats.flows == 6 && stats.snapshots == 3 &&
          stats.dailyTotals == 0 && stats.oldestFlow == t0,
          "the stats count flows and snapshots");

    // CSV export: a header and one line per flow, RFC 4180 quoting.
    const std::filesystem::path csv = std::filesystem::temp_directory_path() / "networkmonitor-test.csv";
    int64_t written = 0;
    CHECK(NetworkMonitor_ExportFlowsCsv(store, ActivityQuery(), csv.string(), &written) && written == 6,
          "six flows export to CSV");
    {
        std::ifstream file(csv);
        std::string line;
        int lines = 0;
        bool header = false;
        bool isoTime = false;
        while (std::getline(file, line)) {
            if (lines == 0) header = line.rfind("first_seen,last_seen,", 0) == 0;
            if (line.find("T") != std::string::npos && line.find("Z,") != std::string::npos) isoTime = true;
            ++lines;
        }
        CHECK(lines == 7 && header, "with a header and a line per flow");
        CHECK(isoTime, "and UTC ISO-8601 times");
        std::filesystem::remove(csv);
    }

    // Roll-up: the first two sightings' flows (last seen t0+1) are older
    // than t0+60; the third snapshot's are not.
    int64_t rolled = 0;
    CHECK(NetworkMonitor_RollUp(store, t0 + 60, &rolled) && rolled == 3,
          "rolling up drops exactly the old flows");
    CHECK(NetworkMonitor_QueryFlows(store, ActivityQuery(), flows) && flows.size() == 3,
          "leaving the recent ones");
    std::vector<DailyProcessTotal> totals;
    CHECK(NetworkMonitor_QueryDailyTotals(store, ActivityQuery(), totals) && totals.size() == 3,
          "into one daily total per process and peer");
    const DailyProcessTotal* browserDay = nullptr;
    for (const auto& d : totals) if (d.processName == "firefox") browserDay = &d;
    CHECK(browserDay && browserDay->flows == 1 && browserDay->countedFlows == 1 &&
          browserDay->bytesSent == 2000 && browserDay->bytesReceived == 9000 &&
          browserDay->day == (t0 / 86400) * 86400 && browserDay->remoteAddress == "1.1.1.1",
          "the browser's day carries its flow and its counters");
    bool orphanDay = false;
    for (const auto& d : totals) if (d.processName == "(unattributed)" && d.countedFlows == 0) orphanDay = true;
    CHECK(orphanDay, "the unattributed flow is a day total with no counted bytes");

    // A second roll-up onto the same day accumulates.
    CHECK(NetworkMonitor_RollUp(store, t0 + 1000, &rolled) && rolled == 3,
          "the remaining flows roll up too");
    CHECK(NetworkMonitor_QueryDailyTotals(store, ActivityQuery(), totals) && totals.size() == 3,
          "onto the same three day rows");
    for (const auto& d : totals) if (d.processName == "firefox") browserDay = &d;
    CHECK(browserDay && browserDay->flows == 2 && browserDay->bytesSent == 4000,
          "which now count both flows and both byte totals");

    // Retention: with a 7-day window and 'now' far in the future, everything goes.
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0 + 5000), "one more snapshot records");
    CHECK(NetworkMonitor_ApplyRetention(store, t0 + 400LL * 86400), "retention applies");
    CHECK(NetworkMonitor_StoreStats(store, stats) && stats.flows == 0 && stats.dailyTotals == 0 &&
          stats.snapshots == 0, "and a store older than twelve windows is empty");

    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0), "recording works again");
    CHECK(NetworkMonitor_Purge(store), "purging");
    CHECK(NetworkMonitor_StoreStats(store, stats) && stats.flows == 0, "leaves nothing");
    CHECK(NetworkMonitor_CloseStore(store), "the store closes");
    CHECK(NetworkMonitor_CloseStore(store).code == NetworkMonitorResultCode::InvalidArgument,
          "and closing it twice is refused");
}

int main() {
    std::printf("=== NetworkMonitor tests ===\n");
    TestAddressFormatting();
    TestDecodeAddress();
    TestStates();
    TestParseTable();
    TestSummarize();
    TestFormatting();
    TestLiveSnapshot();
    TestStore();
    std::printf("=== %s ===\n", g_failures == 0 ? "all tests passed" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
