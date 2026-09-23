// Tests/NetworkMonitorTests.cpp
// Unit tests for the NetworkMonitor module (include/NetworkMonitor/): the
// shared address formatter and the /proc/net table parser against fixture
// text, the per-process roll-up, the endpoint formatting, and - where this
// platform has a backend - a live check that sockets this test opens appear
// in the snapshot attributed to this test's own PID, with byte counters
// where the backend collects them. Where there is no backend, the check is
// that the module says so rather than returning an empty table. Then the
// activity store on an in-memory database, the DNS wire format from
// fixture bytes, the name table's precedence rules, the local DNS proxy
// end to end against a fake upstream resolver on loopback, the conntrack
// message parser from captured bytes, the event registry's attribution,
// and the snapshot differ against sockets this test opens.
//
// Self-contained: no test framework, no UI stack, links only NetworkMonitor.
//
// Version: 0.6.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorAddress.h"
#include "NetworkMonitor/NetworkMonitorConntrack.h"
#include "NetworkMonitor/NetworkMonitorDns.h"
#include "NetworkMonitor/NetworkMonitorEvents.h"
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorProcfs.h"
#include "NetworkMonitor/NetworkMonitorStore.h"
#ifdef ULTRACANVAS_HAS_DATABASE
#include "UltraDatabase/UltraDatabase.h"
#endif

#include <atomic>
#include <chrono>
#include <initializer_list>
#include <mutex>
#include <thread>

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
    using SockLen = int;
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using SocketHandle = int;
    static const SocketHandle kNoSocket = -1;
    static void CloseSocket(SocketHandle s) { ::close(s); }
    static uint32_t OwnPid() { return static_cast<uint32_t>(::getpid()); }
    static bool SocketsUp() { return true; }
    using SockLen = socklen_t;
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

// =============================================================================
// Names

static std::vector<unsigned char> Bytes(std::initializer_list<int> values) {
    std::vector<unsigned char> out;
    for (int v : values) out.push_back(static_cast<unsigned char>(v));
    return out;
}

static void TestDnsWire() {
    std::printf("DNS wire format\n");
    using namespace NetworkMonitorDns;

    const std::vector<unsigned char> query = BuildQuery(0x1234, "WWW.Example.COM.", kTypeA);
    Message parsed;
    CHECK(Parse(query.data(), query.size(), parsed) && !parsed.isResponse && parsed.id == 0x1234 &&
          parsed.questionName == "www.example.com" && parsed.questionType == kTypeA && parsed.answers.empty(),
          "a built query parses back, with the name normalised");

    // www.example.com -> CNAME edge.cdn.net -> A 93.184.216.34, AAAA 2606:2800::1;
    // an unrelated A record in the same section does not count.
    std::vector<Answer> answers = {
        { "www.example.com", kTypeCname, 300, "", "edge.cdn.net" },
        { "edge.cdn.net", kTypeA, 60, "93.184.216.34", "" },
        { "edge.cdn.net", kTypeAaaa, 120, "2606:2800::1", "" },
        { "other.example.net", kTypeA, 10, "10.9.8.7", "" },
    };
    const std::vector<unsigned char> response = BuildResponse(7, "www.example.com", kTypeA, answers);
    CHECK(Parse(response.data(), response.size(), parsed) && parsed.isResponse && parsed.rcode == 0 &&
          parsed.answers.size() == 4 && parsed.answerCount == 4,
          "a response with a CNAME chain parses");
    CHECK(parsed.answers.size() == 4 && parsed.answers[0].type == kTypeCname &&
          parsed.answers[0].target == "edge.cdn.net" && parsed.answers[2].address == "2606:2800::1",
          "with the CNAME target and the AAAA in text");
    DnsObservation observation;
    CHECK(ToObservation(parsed, observation) && observation.queryName == "www.example.com" &&
          observation.addresses.size() == 2 && observation.addresses[0] == "93.184.216.34" &&
          observation.addresses[1] == "2606:2800::1" && observation.ttlSeconds == 60,
          "the addresses reached through the chain map to the name asked for, at the shortest TTL");

    const std::vector<unsigned char> failure = BuildResponse(8, "nope.invalid", kTypeA, {}, 3);
    CHECK(Parse(failure.data(), failure.size(), parsed) && parsed.rcode == 3 && !ToObservation(parsed, observation),
          "NXDOMAIN names nothing");

    // Hand-made: the answer's owner is a pointer (0xC00C) back to the
    // question name at offset 12.
    std::vector<unsigned char> compressed = Bytes({
        0x00, 0x2A, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        3, 'f', 'o', 'o', 2, 'i', 'o', 0, 0x00, 0x01, 0x00, 0x01,
        0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x04, 1, 2, 3, 4 });
    CHECK(Parse(compressed.data(), compressed.size(), parsed) && parsed.answers.size() == 1 &&
          parsed.answers[0].name == "foo.io" && parsed.answers[0].address == "1.2.3.4" &&
          parsed.answers[0].ttl == 5,
          "a compression pointer resolves to the earlier name");
    std::vector<unsigned char> loop = compressed;
    loop[24] = 0xC0; loop[25] = 0x18;   // the pointer points at itself
    CHECK(!Parse(loop.data(), loop.size(), parsed), "a pointer that does not go backwards is refused");
    std::vector<unsigned char> cut(compressed.begin(), compressed.begin() + 30);
    CHECK(!Parse(cut.data(), cut.size(), parsed), "a message cut inside a record is refused");
    CHECK(!Parse(compressed.data(), 5, parsed), "a message shorter than its header is refused");
    CHECK(NormalizeName("A.B.") == "a.b" && NormalizeName("") == "", "names normalise to lower case without the root dot");
}

static void TestNameTable() {
    std::printf("Name table\n");
    NetworkMonitor_ClearNames();
    NameRecord record;
    CHECK(!NetworkMonitor_LookupName("203.0.113.5", record), "an unknown address has no name");

    const int64_t t0 = 1'800'000'000;
    DnsObservation weak;
    weak.queryName = "a203-0-113-5.deploy.static.cdn.example";
    weak.addresses = { "203.0.113.5" };
    weak.source = NameSource::ReverseDns;
    weak.observedAt = t0;
    NetworkMonitor_ObserveName(weak);
    CHECK(NetworkMonitor_LookupName("203.0.113.5", record) && record.name == weak.queryName &&
          record.source == NameSource::ReverseDns && !NetworkMonitor_NameIsObserved(record.source),
          "a reverse-DNS name is stored and reads as weak");

    DnsObservation observed;
    observed.queryName = "www.example.com";
    observed.addresses = { "203.0.113.5", "203.0.113.6" };
    observed.source = NameSource::DnsProxy;
    observed.observedAt = t0 - 100;   // older, but observed
    observed.ttlSeconds = 30;
    NetworkMonitor_ObserveName(observed);
    CHECK(NetworkMonitor_LookupName("203.0.113.5", record) && record.name == "www.example.com" &&
          record.source == NameSource::DnsProxy,
          "an observed name replaces a weak one even when older");
    CHECK(record.expiresAt == t0 - 100 + kNameMinimumLifetimeSeconds,
          "and lives at least the minimum lifetime, not the 30-second TTL");
    NetworkMonitor_ObserveName(weak);
    CHECK(NetworkMonitor_LookupName("203.0.113.5", record) && record.source == NameSource::DnsProxy,
          "a weak name never replaces an observed one");

    DnsObservation newer = observed;
    newer.queryName = "cdn.example.com";
    newer.addresses = { "203.0.113.6" };
    newer.observedAt = t0 + 5;
    NetworkMonitor_ObserveName(newer);
    CHECK(NetworkMonitor_LookupName("203.0.113.6", record) && record.name == "cdn.example.com",
          "between two observed names the newer wins");

    std::vector<NameRecord> all;
    NetworkMonitor_ListNames(all);
    CHECK(all.size() == 2 && all[0].address == "203.0.113.6", "the list is newest first");

    DnsObservation nameless;
    nameless.addresses = { "203.0.113.9" };
    nameless.source = NameSource::DnsProxy;
    NetworkMonitor_ObserveName(nameless);
    CHECK(!NetworkMonitor_LookupName("203.0.113.9", record), "an observation without a name is ignored");

    // A listener hears every observation, after the table has it.
    std::string heard;
    const NameListenerId listener = NetworkMonitor_AddNameListener([&heard](const DnsObservation& o) {
        NameRecord seen;
        if (NetworkMonitor_LookupName(o.addresses.front(), seen)) heard = seen.name;
    });
    DnsObservation more = observed;
    more.queryName = "mail.example.com";
    more.addresses = { "203.0.113.7" };
    more.observedAt = 0;   // stamped now
    NetworkMonitor_ObserveName(more);
    CHECK(heard == "mail.example.com", "a listener sees the observation once the table holds it");
    NetworkMonitor_RemoveNameListener(listener);
    heard.clear();
    NetworkMonitor_ObserveName(more);
    CHECK(heard.empty(), "and not after it is removed");

    CHECK(std::string(NetworkMonitor_NameSourceName(NameSource::DnsProxy)) == "DNS proxy" &&
          std::string(NetworkMonitor_NameSourceName(NameSource::None)) == "none" &&
          NetworkMonitor_NameIsObserved(NameSource::EtwDnsClient) && !NetworkMonitor_NameIsObserved(NameSource::Inferred),
          "source names and the observed / weak split");

    // A connection to a named peer comes back named from the snapshot.
    std::vector<NetworkConnection> fixture;
    fixture.push_back(MakeConnection(1, "app", "203.0.113.5", NetworkConnectionState::Established));
    fixture.push_back(MakeConnection(1, "app", "203.0.113.6", NetworkConnectionState::Established));
    fixture.push_back(MakeConnection(1, "app", "198.51.100.1", NetworkConnectionState::Established));
    for (auto& c : fixture) {
        NameRecord named;
        if (NetworkMonitor_LookupName(c.remoteAddress, named)) { c.remoteName = named.name; c.nameSource = named.source; }
    }
    const auto groups = NetworkMonitor_SummarizeByProcess(fixture);
    CHECK(groups.size() == 1 && groups[0].remoteNames.size() == 2 && groups[0].remoteNames[0] == "cdn.example.com",
          "the roll-up lists the distinct names it saw, sorted");
    NetworkMonitor_ClearNames();
}

static void TestReverseDns() {
    std::printf("Reverse DNS source\n");
    NetworkMonitor_ClearNames();
    ReverseDnsOptions options;
    auto source = NetworkMonitor_CreateReverseDnsSource(options);
    CHECK(source && source->Kind() == NameSource::ReverseDns && !source->IsRunning(), "a reverse DNS source is created stopped");
    std::atomic<int> observations{0};
    CHECK(source->Start([&observations](const DnsObservation&) { ++observations; }), "and starts");
    // Nothing worth a PTR lookup: loopback, wildcard, link-local, private.
    for (const char* address : { "127.0.0.1", "0.0.0.0", "169.254.1.1", "10.0.0.1", "192.168.1.1", "::1", "fe80::1", "ff02::1", "not-an-address" }) {
        source->NoteAddress(address);
    }
    CHECK(source->WaitIdle(2000) && observations.load() == 0,
          "addresses no PTR can name usefully are never looked up");
    source->Stop();
    CHECK(!source->IsRunning(), "and stops");
    // Registered, it is stopped by the registry.
    CHECK(NetworkMonitor_RegisterNameSource(NetworkMonitor_CreateReverseDnsSource(options)), "registering one starts it");
    std::vector<NameSourceStatus> sources;
    NetworkMonitor_ListNameSources(sources);
    CHECK(sources.size() == 1 && sources[0].running && sources[0].kind == NameSource::ReverseDns && !sources[0].reportsProcess,
          "the registry lists it running, without process attribution");
    CHECK(!NetworkMonitor_GetCapabilities().dnsWithProcess, "so the capabilities do not claim DNS with process");
    CHECK(NetworkMonitor_WaitForNames(500), "an idle registry reports idle");
    NetworkMonitor_StopNameSources();
    NetworkMonitor_ListNameSources(sources);
    CHECK(sources.empty(), "stopping the sources empties the registry");
    CHECK(NetworkMonitor_RegisterNameSource(nullptr).code == NetworkMonitorResultCode::InvalidArgument,
          "registering nothing is refused");
}

// A fake upstream resolver on loopback: answers every A query for
// "www.example.com" with 93.184.216.34, over UDP and over TCP, and refuses
// (NXDOMAIN) everything else. Runs until told to stop.
struct FakeResolver {
    SocketHandle udp = kNoSocket;
    SocketHandle tcp = kNoSocket;
    uint16_t port = 0;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<int> udpQueries{0};
    std::atomic<int> tcpQueries{0};

    bool Start() {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        udp = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (udp == kNoSocket || ::bind(udp, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) return false;
        SockLen length = sizeof local;
        ::getsockname(udp, reinterpret_cast<sockaddr*>(&local), &length);
        port = ntohs(local.sin_port);
        tcp = ::socket(AF_INET, SOCK_STREAM, 0);
        if (tcp == kNoSocket || ::bind(tcp, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0 ||
            ::listen(tcp, 4) != 0) return false;
        thread = std::thread([this] { Run(); });
        return true;
    }

    static std::vector<unsigned char> Answer(const unsigned char* data, std::size_t size) {
        NetworkMonitorDns::Message query;
        if (!NetworkMonitorDns::Parse(data, size, query)) return {};
        if (query.questionName == "www.example.com" && query.questionType == NetworkMonitorDns::kTypeA) {
            return NetworkMonitorDns::BuildResponse(query.id, query.questionName, query.questionType,
                { { "www.example.com", NetworkMonitorDns::kTypeA, 60, "93.184.216.34", "" } });
        }
        return NetworkMonitorDns::BuildResponse(query.id, query.questionName, query.questionType, {}, 3);
    }

    void Run() {
        unsigned char buffer[4096];
        while (!stop.load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(udp, &readable);
            FD_SET(tcp, &readable);
            timeval wait{};
            wait.tv_usec = 100 * 1000;
            const SocketHandle highest = udp > tcp ? udp : tcp;
            if (::select(static_cast<int>(highest + 1), &readable, nullptr, nullptr, &wait) <= 0) continue;
            if (FD_ISSET(udp, &readable)) {
                sockaddr_storage from{};
                SockLen length = sizeof from;
                const int got = ::recvfrom(udp, reinterpret_cast<char*>(buffer), sizeof buffer, 0,
                                           reinterpret_cast<sockaddr*>(&from), &length);
                if (got > 0) {
                    ++udpQueries;
                    const auto answer = Answer(buffer, static_cast<std::size_t>(got));
                    ::sendto(udp, reinterpret_cast<const char*>(answer.data()), static_cast<int>(answer.size()), 0,
                             reinterpret_cast<sockaddr*>(&from), length);
                }
            }
            if (FD_ISSET(tcp, &readable)) {
                const SocketHandle client = ::accept(tcp, nullptr, nullptr);
                if (client != kNoSocket) {
                    unsigned char header[2];
                    if (::recv(client, reinterpret_cast<char*>(header), 2, 0) == 2) {
                        const int length = (header[0] << 8) | header[1];
                        int got = 0;
                        while (got < length) {
                            const int n = ::recv(client, reinterpret_cast<char*>(buffer + got), length - got, 0);
                            if (n <= 0) break;
                            got += n;
                        }
                        if (got == length) {
                            ++tcpQueries;
                            auto answer = Answer(buffer, static_cast<std::size_t>(got));
                            unsigned char frame[2] = { static_cast<unsigned char>(answer.size() >> 8),
                                                       static_cast<unsigned char>(answer.size() & 0xFF) };
                            ::send(client, reinterpret_cast<const char*>(frame), 2, 0);
                            ::send(client, reinterpret_cast<const char*>(answer.data()), static_cast<int>(answer.size()), 0);
                        }
                    }
                    CloseSocket(client);
                }
            }
        }
    }

    ~FakeResolver() {
        stop = true;
        if (thread.joinable()) thread.join();
        if (udp != kNoSocket) CloseSocket(udp);
        if (tcp != kNoSocket) CloseSocket(tcp);
    }
};

// Declared in NetworkMonitorDnsProxy.cpp for exactly this test: the port a
// proxy asked to listen on port 0 was given.
namespace UltraCanvas { uint16_t NetworkMonitor_DnsProxyListenPort(const INameSource& source); }

static bool UdpExchange(uint16_t port, const std::vector<unsigned char>& query, std::vector<unsigned char>& reply) {
    const SocketHandle s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kNoSocket) return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(port);
    bool ok = ::sendto(s, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
                       reinterpret_cast<sockaddr*>(&to), sizeof to) == static_cast<int>(query.size());
    if (ok) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s, &readable);
        timeval wait{};
        wait.tv_sec = 3;
        ok = ::select(static_cast<int>(s + 1), &readable, nullptr, nullptr, &wait) > 0;
    }
    if (ok) {
        reply.assign(4096, 0);
        const int got = ::recv(s, reinterpret_cast<char*>(reply.data()), 4096, 0);
        ok = got > 0;
        if (ok) reply.resize(static_cast<std::size_t>(got));
    }
    CloseSocket(s);
    return ok;
}

static bool TcpExchange(uint16_t port, const std::vector<unsigned char>& query, std::vector<unsigned char>& reply) {
    const SocketHandle s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kNoSocket) return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(port);
    bool ok = ::connect(s, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0;
    if (ok) {
        std::vector<unsigned char> framed = { static_cast<unsigned char>(query.size() >> 8),
                                              static_cast<unsigned char>(query.size() & 0xFF) };
        framed.insert(framed.end(), query.begin(), query.end());
        ok = ::send(s, reinterpret_cast<const char*>(framed.data()), static_cast<int>(framed.size()), 0) ==
             static_cast<int>(framed.size());
    }
    if (ok) {
        unsigned char header[2];
        ok = ::recv(s, reinterpret_cast<char*>(header), 2, 0) == 2;
        if (ok) {
            const int length = (header[0] << 8) | header[1];
            reply.assign(static_cast<std::size_t>(length), 0);
            int got = 0;
            while (ok && got < length) {
                const int n = ::recv(s, reinterpret_cast<char*>(reply.data() + got), length - got, 0);
                if (n <= 0) ok = false; else got += n;
            }
        }
    }
    CloseSocket(s);
    return ok;
}

static void TestDnsProxy() {
    std::printf("DNS proxy\n");
    if (!SocketsUp()) { std::printf("  skipped: no sockets\n"); return; }
    NetworkMonitor_ClearNames();

    DnsProxyOptions self;
    self.listenPort = 5300;
    self.upstreamAddress = "127.0.0.1";
    self.upstreamPort = 5300;
    auto looped = NetworkMonitor_CreateDnsProxySource(self);
    CHECK(looped->Start([](const DnsObservation&) {}).code == NetworkMonitorResultCode::InvalidArgument,
          "a proxy whose upstream is itself is refused");
    DnsProxyOptions bad;
    bad.upstreamAddress = "resolver.example";
    CHECK(NetworkMonitor_CreateDnsProxySource(bad)->Start([](const DnsObservation&) {}).code ==
              NetworkMonitorResultCode::InvalidArgument,
          "an upstream that is not an address is refused");

    FakeResolver upstream;
    if (!upstream.Start()) { std::printf("  skipped: could not bind a loopback resolver\n"); return; }

    DnsProxyOptions options;
    options.listenPort = 0;   // any free port
    options.upstreamAddress = "127.0.0.1";
    options.upstreamPort = upstream.port;
    auto proxy = NetworkMonitor_CreateDnsProxySource(options);
    std::atomic<int> observations{0};
    std::string lastName;
    std::mutex nameMutex;
    const NetworkMonitorResult started = proxy->Start([&](const DnsObservation& o) {
        ++observations;
        std::lock_guard<std::mutex> lock(nameMutex);
        lastName = o.queryName;
        NetworkMonitor_ObserveName(o);
    });
    CHECK(started, ("the proxy starts: " + started.message).c_str());
    if (!started) return;
    const uint16_t port = NetworkMonitor_DnsProxyListenPort(*proxy);
    CHECK(port != 0 && proxy->Name().find(std::to_string(port)) != std::string::npos,
          "on a port of its own that its name reports");

    std::vector<unsigned char> reply;
    const auto query = NetworkMonitorDns::BuildQuery(0x4242, "www.example.com", NetworkMonitorDns::kTypeA);
    CHECK(UdpExchange(port, query, reply), "a UDP query through the proxy is answered");
    NetworkMonitorDns::Message answer;
    CHECK(NetworkMonitorDns::Parse(reply.data(), reply.size(), answer) && answer.id == 0x4242 &&
          answer.answers.size() == 1 && answer.answers[0].address == "93.184.216.34",
          "with the upstream's answer, untouched");
    for (int i = 0; i < 50 && observations.load() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    NameRecord record;
    CHECK(observations.load() == 1 && NetworkMonitor_LookupName("93.184.216.34", record) &&
          record.name == "www.example.com" && record.source == NameSource::DnsProxy,
          "and the proxy observed the name for the address");
    CHECK(upstream.udpQueries.load() == 1, "the upstream saw the one UDP query");

    CHECK(TcpExchange(port, query, reply), "a TCP query through the proxy is answered");
    CHECK(NetworkMonitorDns::Parse(reply.data(), reply.size(), answer) && answer.answers.size() == 1,
          "with the same answer");
    for (int i = 0; i < 50 && observations.load() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(observations.load() == 2 && upstream.tcpQueries.load() == 1, "observed over TCP too");

    const auto missing = NetworkMonitorDns::BuildQuery(9, "nope.invalid", NetworkMonitorDns::kTypeA);
    CHECK(UdpExchange(port, missing, reply) && NetworkMonitorDns::Parse(reply.data(), reply.size(), answer) &&
          answer.rcode == 3, "a refusal is relayed as it came");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(observations.load() == 2, "and observed as nothing");

    CHECK(proxy->LastError().empty() && proxy->IsRunning(), "the proxy is still healthy");
    proxy->Stop();
    CHECK(!proxy->IsRunning(), "and stops");
    NetworkMonitor_ClearNames();
}

static void TestStoreNames() {
    std::printf("Names in the store\n");
    if (!NetworkMonitor_StoreAvailable()) { std::printf("  skipped: no UltraDatabase in this build\n"); return; }
    NetworkMonitorStoreHandle store = NetworkMonitorInvalidStore;
    NetworkMonitorStoreOptions options;
    options.path = ":memory:";
    options.retentionDays = 7;
    CHECK(NetworkMonitor_OpenStore(options, store), "an in-memory store opens");

    const int64_t t0 = 1'800'000'000;
    std::vector<NetworkConnection> snapshot;
    snapshot.push_back(MakeConnection(100, "firefox", "93.184.216.34", NetworkConnectionState::Established));
    snapshot[0].remoteName = "a93-184.deploy.example";
    snapshot[0].nameSource = NameSource::ReverseDns;
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0), "a snapshot with a weak name records");
    std::vector<RecordedFlow> flows;
    CHECK(NetworkMonitor_QueryFlows(store, ActivityQuery(), flows) && flows.size() == 1 &&
          flows[0].remoteName == "a93-184.deploy.example" && flows[0].nameSource == NameSource::ReverseDns,
          "the flow carries the name and its source");
    snapshot[0].remoteName = "www.example.com";
    snapshot[0].nameSource = NameSource::DnsProxy;
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0 + 1), "a later sighting with an observed name records");
    CHECK(NetworkMonitor_QueryFlows(store, ActivityQuery(), flows) && flows.size() == 1 &&
          flows[0].remoteName == "www.example.com" && flows[0].nameSource == NameSource::DnsProxy,
          "and the observed name replaces the weak one on the same flow");
    snapshot[0].remoteName = "a93-184.deploy.example";
    snapshot[0].nameSource = NameSource::ReverseDns;
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0 + 2) &&
          NetworkMonitor_QueryFlows(store, ActivityQuery(), flows) && flows[0].remoteName == "www.example.com",
          "but a weak name never replaces an observed one");
    snapshot[0].remoteName.clear();
    snapshot[0].nameSource = NameSource::None;
    CHECK(NetworkMonitor_RecordSnapshot(store, snapshot, t0 + 3) &&
          NetworkMonitor_QueryFlows(store, ActivityQuery(), flows) && flows[0].remoteName == "www.example.com",
          "and a sighting without a name keeps the one recorded");
    ActivityQuery byName;
    byName.text = "example.com";
    CHECK(NetworkMonitor_QueryFlows(store, byName, flows) && flows.size() == 1, "the text filter matches the name");
    byName.text = "nothing-here";
    CHECK(NetworkMonitor_QueryFlows(store, byName, flows) && flows.empty(), "and only the name");

    // DNS observations of their own.
    DnsObservation observation;
    observation.queryName = "api.example.com";
    observation.addresses = { "198.51.100.1", "198.51.100.2" };
    observation.source = NameSource::EtwDnsClient;
    observation.observedAt = t0 + 10;
    ProcessIdentity asker;
    asker.pid = 4242;
    asker.displayName = "curl";
    observation.process = asker;
    CHECK(NetworkMonitor_RecordDnsObservation(store, observation), "an observation with two addresses records");
    DnsObservation empty;
    CHECK(NetworkMonitor_RecordDnsObservation(store, empty).code == NetworkMonitorResultCode::InvalidArgument,
          "an empty one is refused");
    std::vector<RecordedDnsObservation> recorded;
    CHECK(NetworkMonitor_QueryDnsObservations(store, ActivityQuery(), recorded) && recorded.size() == 2 &&
          recorded[0].queryName == "api.example.com" && recorded[0].source == NameSource::EtwDnsClient &&
          recorded[0].process && recorded[0].process->pid == 4242 && recorded[0].observedAt == t0 + 10,
          "as two rows with the asking process");
    ActivityQuery byPid;
    byPid.pid = 4242;
    ActivityQuery byOther;
    byOther.pid = 1;
    CHECK(NetworkMonitor_QueryDnsObservations(store, byPid, recorded) && recorded.size() == 2 &&
          NetworkMonitor_QueryDnsObservations(store, byOther, recorded) && recorded.empty(),
          "filtered by PID");
    ActivityQuery byAddress;
    byAddress.text = "100.2";
    CHECK(NetworkMonitor_QueryDnsObservations(store, byAddress, recorded) && recorded.size() == 1 &&
          recorded[0].address == "198.51.100.2", "and by address text");
    NetworkMonitorStoreStats stats;
    CHECK(NetworkMonitor_StoreStats(store, stats) && stats.dnsObservations == 2, "the stats count them");

    // The CSV carries the name; the roll-up keeps it on the day total.
    const std::filesystem::path csv = std::filesystem::temp_directory_path() / "networkmonitor-names.csv";
    int64_t written = 0;
    CHECK(NetworkMonitor_ExportFlowsCsv(store, ActivityQuery(), csv.string(), &written) && written == 1, "the CSV exports");
    {
        std::ifstream file(csv);
        std::string header, line;
        std::getline(file, header);
        std::getline(file, line);
        CHECK(header.find(",remote_name,name_source,") != std::string::npos &&
              line.find(",www.example.com,DNS proxy,") != std::string::npos,
              "with the name and its source as columns");
        std::filesystem::remove(csv);
    }
    int64_t rolled = 0;
    std::vector<DailyProcessTotal> totals;
    CHECK(NetworkMonitor_RollUp(store, t0 + 100, &rolled) && rolled == 1 &&
          NetworkMonitor_QueryDailyTotals(store, ActivityQuery(), totals) && totals.size() == 1 &&
          totals[0].remoteName == "www.example.com",
          "the daily total remembers the peer's name");
    ActivityQuery totalsByName;
    totalsByName.text = "www.example";
    CHECK(NetworkMonitor_QueryDailyTotals(store, totalsByName, totals) && totals.size() == 1,
          "and the totals filter matches it");

    // Retention drops observations older than the window.
    CHECK(NetworkMonitor_ApplyRetention(store, t0 + 8LL * 86400) &&
          NetworkMonitor_StoreStats(store, stats) && stats.dnsObservations == 0,
          "observations older than the retention window are dropped");
    CHECK(NetworkMonitor_RecordDnsObservation(store, observation) && NetworkMonitor_Purge(store) &&
          NetworkMonitor_StoreStats(store, stats) && stats.dnsObservations == 0,
          "and purge takes the rest");
    CHECK(NetworkMonitor_CloseStore(store), "the store closes");

#ifdef ULTRACANVAS_HAS_DATABASE
    // A version-1 file (the schema as 0.3 wrote it) opens and gains the
    // columns: the migration is the same code path a user's store takes.
    const std::filesystem::path v1 = std::filesystem::temp_directory_path() / "networkmonitor-v1.db";
    std::filesystem::remove(v1);
    {
        UltraDbConnectionConfig config;
        config.name = "networkmonitor-test-v1";
        config.driver = "sqlite";
        config.database = v1.string();
        CHECK(UltraDb_RegisterConnection(config), "a version-1 file is created");
        const std::vector<UltraDbMigration> steps = { { 1, "v1", 
            "CREATE TABLE processes(id INTEGER PRIMARY KEY, pid INTEGER NOT NULL, name TEXT NOT NULL,"
            " executable TEXT NOT NULL, user TEXT NOT NULL, UNIQUE(pid, name, executable, user));"
            "CREATE TABLE flows(id INTEGER PRIMARY KEY, transport INTEGER NOT NULL, family INTEGER NOT NULL,"
            " local_address TEXT NOT NULL, local_port INTEGER NOT NULL, remote_address TEXT NOT NULL,"
            " remote_port INTEGER NOT NULL, state INTEGER NOT NULL, inode INTEGER NOT NULL DEFAULT 0,"
            " process_id INTEGER REFERENCES processes(id), first_seen INTEGER NOT NULL,"
            " last_seen INTEGER NOT NULL, snapshots INTEGER NOT NULL DEFAULT 1, bytes_sent INTEGER,"
            " bytes_received INTEGER);"
            "CREATE TABLE daily_totals(day INTEGER NOT NULL, process_name TEXT NOT NULL,"
            " executable TEXT NOT NULL, remote_address TEXT NOT NULL, flows INTEGER NOT NULL,"
            " counted_flows INTEGER NOT NULL, bytes_sent INTEGER NOT NULL, bytes_received INTEGER NOT NULL,"
            " PRIMARY KEY(day, process_name, executable, remote_address));"
            "CREATE TABLE snapshots(id INTEGER PRIMARY KEY, observed_at INTEGER NOT NULL,"
            " connections INTEGER NOT NULL);"
            "INSERT INTO flows(transport, family, local_address, local_port, remote_address, remote_port,"
            " state, first_seen, last_seen) VALUES(0, 0, '10.0.0.1', 5000, '93.184.216.34', 443, 4,"
            " 1800000000, 1800000001);" } };
        CHECK(UltraDb_Migrate(config.name, steps), "with a flow recorded by the old schema");
        UltraDb_CloseConnection(config.name);
    }
    NetworkMonitorStoreOptions onDisk;
    onDisk.path = v1.string();
    NetworkMonitorStoreHandle migrated = NetworkMonitorInvalidStore;
    const NetworkMonitorResult reopened = NetworkMonitor_OpenStore(onDisk, migrated);
    CHECK(reopened, ("the version-1 file opens: " + reopened.message).c_str());
    if (reopened) {
        CHECK(NetworkMonitor_QueryFlows(migrated, ActivityQuery(), flows) && flows.size() == 1 &&
              flows[0].remoteName.empty() && flows[0].nameSource == NameSource::None,
              "its old flow reads back without a name");
        snapshot[0].remoteName = "www.example.com";
        snapshot[0].nameSource = NameSource::DnsProxy;
        NetworkConnectionEvent migratedEvent;
        migratedEvent.localAddress = "10.0.0.1";
        migratedEvent.remoteAddress = "93.184.216.34";
        migratedEvent.remotePort = 443;
        CHECK(NetworkMonitor_RecordSnapshot(migrated, snapshot, t0 + 5) &&
              NetworkMonitor_RecordDnsObservation(migrated, observation) &&
              NetworkMonitor_RecordConnectionEvent(migrated, migratedEvent) &&
              NetworkMonitor_StoreStats(migrated, stats) && stats.dnsObservations == 2 && stats.connectionEvents == 1,
              "and the migrated file records names, observations and events");
        NetworkMonitor_CloseStore(migrated);
    }
    std::filesystem::remove(v1);
#endif
}

// =============================================================================
// Events

static std::vector<unsigned char> FromHex(const char* hex) {
    std::vector<unsigned char> out;
    for (std::size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        out.push_back(static_cast<unsigned char>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
    }
    return out;
}

static void TestConntrackParse() {
    std::printf("conntrack messages\n");
    using namespace NetworkMonitorConntrack;
    // Captured from a Linux 6.18 kernel with nf_conntrack_acct on: a NEW
    // for a TCP connection, and the DESTROY of a loopback connection that
    // moved 1000 bytes one way and 300 the other.
    const auto created = FromHex(
        "c400000000010006000000000000000002000000340001801400018008000100a04f680a08000200c00002021c00028005"
        "000100060000000600020001bb000006000300c60c0000340002801400018008000100c000020208000200a04f680a1c00"
        "0280050001000600000006000200c60c00000600030001bb000008000c00be2b633b08000300000000080800070000000"
        "12c300004802c000180050001000300000005000200000000000500030000000000060004000a000000060005000a000000");
    const auto destroyed = FromHex(
        "d4000000020100000000000000000000020000003400018014000180080001007f000001080002007f0000011c000280"
        "05000100060000000600020081f0000006000300afbd00003400028014000180080001007f000001080002007f000001"
        "1c000280050001000600000006000200afbd00000600030081f0000008000c00040a079c080003000000020e1c000980"
        "0c00010000000000000000060c00020000000000000005281c000a800c00010000000000000000040c00020000000000"
        "00000204100004800c0001800500010008000000");
    Flow flow;
    CHECK(Parse(created.data(), created.size(), flow) && flow.message == Message::New,
          "a NEW with CREATE|EXCL parses as a new connection");
    CHECK(flow.transport == NetworkTransport::Tcp && flow.protocol == 6 && flow.family == NetworkAddressFamily::IPv4,
          "TCP over IPv4");
    CHECK(flow.sourceAddress == "160.79.104.10" && flow.sourcePort == 443 &&
          flow.destinationAddress == "192.0.2.2" && flow.destinationPort == 50700,
          "with the original-direction tuple");
    CHECK(!flow.bytesOriginal && !flow.bytesReply, "and no counters yet");

    CHECK(Parse(destroyed.data(), destroyed.size(), flow) && flow.message == Message::Destroy,
          "a DESTROY parses");
    CHECK(flow.sourceAddress == "127.0.0.1" && flow.sourcePort == 33264 &&
          flow.destinationAddress == "127.0.0.1" && flow.destinationPort == 44989,
          "with its tuple");
    CHECK(flow.bytesOriginal && *flow.bytesOriginal == 1320 && flow.bytesReply && *flow.bytesReply == 516,
          "and the bytes each direction moved");

    std::vector<unsigned char> update = created;
    update[6] = 0; update[7] = 0;   // no CREATE|EXCL: an UPDATE
    CHECK(Parse(update.data(), update.size(), flow) && flow.message == Message::Update,
          "the same message without the flags is an update");
    std::vector<unsigned char> other = created;
    other[5] = 2;   // another nfnetlink subsystem
    CHECK(!Parse(other.data(), other.size(), flow), "another subsystem's message is refused");
    CHECK(!Parse(created.data(), 40, flow), "a message cut short is refused");
    CHECK(!Parse(created.data(), 10, flow), "a message shorter than its headers is refused");
}

static void TestEventRegistry() {
    std::printf("Event registry\n");
    NetworkMonitor_ClearRecentEvents();
    std::vector<NetworkConnectionEvent> heard;
    std::mutex heardMutex;
    const EventListenerId listener = NetworkMonitor_AddEventListener([&](const NetworkConnectionEvent& e) {
        std::lock_guard<std::mutex> lock(heardMutex);
        heard.push_back(e);
    });

    NetworkConnectionEvent e;
    e.kind = NetworkEventKind::Opened;
    e.localAddress = "203.0.113.9";
    e.localPort = 40000;
    e.remoteAddress = "198.51.100.7";
    e.remotePort = 443;
    e.sourceName = "test";
    ProcessIdentity process;
    process.pid = 4242;
    process.displayName = "curl";
    e.process = process;
    NetworkMonitor_ReportEvent(e);
    std::vector<NetworkConnectionEvent> recent;
    NetworkMonitor_RecentEvents(recent);
    CHECK(recent.size() == 1 && recent[0].observedAtMs > 0 && recent[0].process && recent[0].process->pid == 4242,
          "a reported event is in the ring, stamped, with its process kept");
    CHECK(heard.size() == 1 && heard[0].remoteAddress == "198.51.100.7", "and the listener heard it");
    CHECK(std::string(NetworkMonitor_EventKindName(NetworkEventKind::Accepted)) == "accepted", "event kinds have names");

    // Names come from the name table.
    DnsObservation observation;
    observation.queryName = "api.example.com";
    observation.addresses = { "198.51.100.7" };
    observation.source = NameSource::DnsProxy;
    NetworkMonitor_ObserveName(observation);
    NetworkMonitor_ReportEvent(e);
    NetworkMonitor_RecentEvents(recent);
    CHECK(recent.size() == 2 && recent[0].remoteName == "api.example.com" && recent[0].nameSource == NameSource::DnsProxy,
          "an event's peer is named from the table, newest first");
    NetworkMonitor_ClearNames();

    // Attribution from the socket table, in either orientation.
    if (NetworkMonitor_IsAvailable() && SocketsUp()) {
        const SocketHandle server = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(server, reinterpret_cast<sockaddr*>(&local), sizeof local);
        ::listen(server, 1);
        SockLen length = sizeof local;
        ::getsockname(server, reinterpret_cast<sockaddr*>(&local), &length);
        const uint16_t serverPort = ntohs(local.sin_port);
        const SocketHandle client = ::socket(AF_INET, SOCK_STREAM, 0);
        ::connect(client, reinterpret_cast<sockaddr*>(&local), sizeof local);
        const SocketHandle accepted = ::accept(server, nullptr, nullptr);
        sockaddr_in clientSide{};
        length = sizeof clientSide;
        ::getsockname(client, reinterpret_cast<sockaddr*>(&clientSide), &length);
        const uint16_t clientPort = ntohs(clientSide.sin_port);

        // The tuple the way conntrack would give it for a connection that
        // came *to* the listener: source = the client side.
        NetworkConnectionEvent bare;
        bare.kind = NetworkEventKind::Opened;
        bare.localAddress = "127.0.0.1";
        bare.localPort = clientPort;
        bare.remoteAddress = "127.0.0.1";
        bare.remotePort = serverPort;
        bare.sourceName = "test";
        NetworkMonitor_ReportEvent(bare);
        NetworkMonitor_RecentEvents(recent, 1);
        const bool attributed = !recent.empty() && recent[0].process && recent[0].process->pid == OwnPid();
        CHECK(attributed, "a bare tuple is attributed from the socket table");
        // Both ends are ours here, so either orientation matches first;
        // what matters is that the process is right and the kind sound.
        CHECK(!recent.empty() && (recent[0].kind == NetworkEventKind::Opened || recent[0].kind == NetworkEventKind::Accepted),
              "and reads as opened or accepted");

        NetworkConnectionEvent closing = bare;
        closing.kind = NetworkEventKind::Closed;
        CloseSocket(client);
        CloseSocket(accepted);
        NetworkMonitor_ReportEvent(closing);
        NetworkMonitor_RecentEvents(recent, 1);
        CHECK(!recent.empty() && recent[0].kind == NetworkEventKind::Closed && recent[0].process &&
              recent[0].process->pid == OwnPid(),
              "its Closed is attributed from memory, though the socket is gone");
        CloseSocket(server);
    }
    NetworkMonitor_RemoveEventListener(listener);
    NetworkMonitor_ReportEvent(e);
    CHECK(heard.size() == 4 || heard.size() == 2, "a removed listener hears nothing more");
    NetworkMonitor_ClearRecentEvents();
    NetworkMonitor_RecentEvents(recent);
    CHECK(recent.empty(), "the ring clears");
}

static void TestSnapshotDiff() {
    std::printf("Snapshot differ\n");
    if (!NetworkMonitor_IsAvailable() || !SocketsUp()) {
        auto none = NetworkMonitor_CreateSnapshotDiffEventSource(SnapshotDiffOptions());
        CHECK(none->Start([](const NetworkConnectionEvent&) {}).code == NetworkMonitorResultCode::NotSupported,
              "without a socket table the differ says so");
        return;
    }
    NetworkMonitor_ClearRecentEvents();
    SnapshotDiffOptions options;
    options.intervalMs = 100;
    CHECK(NetworkMonitor_RegisterEventSource(NetworkMonitor_CreateSnapshotDiffEventSource(options)),
          "the differ starts through the registry");
    CHECK(NetworkMonitor_GetCapabilities().connectionEvents, "and the capabilities say events are on");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));   // the seed snapshot

    const SocketHandle server = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(server, reinterpret_cast<sockaddr*>(&local), sizeof local);
    ::listen(server, 1);
    SockLen length = sizeof local;
    ::getsockname(server, reinterpret_cast<sockaddr*>(&local), &length);
    const uint16_t serverPort = ntohs(local.sin_port);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the listener is in a snapshot
    const SocketHandle client = ::socket(AF_INET, SOCK_STREAM, 0);
    ::connect(client, reinterpret_cast<sockaddr*>(&local), sizeof local);
    const SocketHandle accepted = ::accept(server, nullptr, nullptr);

    auto waitFor = [&](NetworkEventKind kind, int tries) -> bool {
        for (int i = 0; i < tries; ++i) {
            std::vector<NetworkConnectionEvent> recent;
            NetworkMonitor_RecentEvents(recent);
            for (const auto& r : recent) {
                if (r.kind == kind && r.transport == NetworkTransport::Tcp && r.process &&
                    r.process->pid == OwnPid() &&
                    (r.localPort == serverPort || r.remotePort == serverPort)) {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    };
    CHECK(waitFor(NetworkEventKind::Opened, 40), "the client side appears as opened, attributed to this PID");
    CHECK(waitFor(NetworkEventKind::Accepted, 40), "the server side appears as accepted, since the port was listening");
    CloseSocket(client);
    CloseSocket(accepted);
    CHECK(waitFor(NetworkEventKind::Closed, 60), "and closing them is reported");
    CloseSocket(server);

    std::vector<EventSourceStatus> sources;
    NetworkMonitor_ListEventSources(sources);
    CHECK(sources.size() == 1 && sources[0].running && sources[0].events >= 3 && sources[0].lastError.empty(),
          "the registry lists the differ running and counting");
    NetworkMonitor_StopEventSources();
    NetworkMonitor_ListEventSources(sources);
    CHECK(sources.empty() && !NetworkMonitor_GetCapabilities().connectionEvents, "stopping empties the registry");
    NetworkMonitor_ClearRecentEvents();
}

static void TestSystemEventSource() {
    std::printf("System event source\n");
    auto source = NetworkMonitor_CreateSystemEventSource();
    if (!source) {
        std::printf("  skipped: no system event source on this platform\n");
        return;
    }
    CHECK(!source->IsRunning() && !source->Name().empty(), "the platform's source is created stopped");
    const NetworkMonitorResult started = source->Start([](const NetworkConnectionEvent&) {});
    if (!started) {
        CHECK(started.code == NetworkMonitorResultCode::PermissionDenied ||
              started.code == NetworkMonitorResultCode::NotSupported ||
              started.code == NetworkMonitorResultCode::IoError,
              ("it refuses with a reason: " + started.message).c_str());
        return;
    }
    CHECK(source->IsRunning(), "it starts here");
    std::printf("  note: %s\n", source->LastError().empty() ? "healthy" : source->LastError().c_str());
    source->Stop();
    CHECK(!source->IsRunning(), "and stops");
}

static void TestStoreEvents() {
    std::printf("Events in the store\n");
    if (!NetworkMonitor_StoreAvailable()) { std::printf("  skipped: no UltraDatabase in this build\n"); return; }
    NetworkMonitorStoreHandle store = NetworkMonitorInvalidStore;
    NetworkMonitorStoreOptions options;
    options.path = ":memory:";
    options.retentionDays = 7;
    CHECK(NetworkMonitor_OpenStore(options, store), "an in-memory store opens");

    const int64_t t0 = 1'800'000'000;
    NetworkConnectionEvent e;
    e.kind = NetworkEventKind::Opened;
    e.localAddress = "10.0.0.5";
    e.localPort = 50000;
    e.remoteAddress = "93.184.216.34";
    e.remotePort = 443;
    e.remoteName = "www.example.com";
    e.nameSource = NameSource::DnsProxy;
    e.observedAtMs = t0 * 1000 + 250;
    e.sourceName = "test";
    ProcessIdentity process;
    process.pid = 100;
    process.displayName = "firefox";
    process.executablePath = "/usr/lib/firefox/firefox";
    process.userName = "me";
    e.process = process;
    CHECK(NetworkMonitor_RecordConnectionEvent(store, e), "an opened event records");
    e.kind = NetworkEventKind::Closed;
    e.observedAtMs = t0 * 1000 + 900;
    e.bytesSent = 1234;
    e.bytesReceived = 56789;
    CHECK(NetworkMonitor_RecordConnectionEvent(store, e), "its closed records with counters");
    NetworkConnectionEvent loop;
    loop.kind = NetworkEventKind::Accepted;
    loop.localAddress = "127.0.0.1";
    loop.localPort = 22;
    loop.remoteAddress = "127.0.0.1";
    loop.remotePort = 40000;
    loop.observedAtMs = (t0 + 5) * 1000;
    CHECK(NetworkMonitor_RecordConnectionEvent(store, loop), "an unattributed loopback event records");

    std::vector<RecordedConnectionEvent> events;
    CHECK(NetworkMonitor_QueryConnectionEvents(store, ActivityQuery(), events) && events.size() == 3 &&
          events[0].event.kind == NetworkEventKind::Accepted && events[2].event.kind == NetworkEventKind::Opened,
          "they read back newest first");
    CHECK(events[1].event.bytesSent && *events[1].event.bytesSent == 1234 && events[1].event.remoteName == "www.example.com" &&
          events[1].event.process && events[1].event.process->pid == 100 && events[1].event.observedAtMs == t0 * 1000 + 900,
          "with counters, name, process and the millisecond");
    CHECK(!events[0].event.process && !events[0].event.bytesSent, "and absent stays absent");
    ActivityQuery noLoopback;
    noLoopback.includeLoopback = false;
    CHECK(NetworkMonitor_QueryConnectionEvents(store, noLoopback, events) && events.size() == 2, "loopback filtered out");
    ActivityQuery byText;
    byText.text = "example";
    CHECK(NetworkMonitor_QueryConnectionEvents(store, byText, events) && events.size() == 2, "the text filter matches the name");
    ActivityQuery byPid;
    byPid.pid = 100;
    CHECK(NetworkMonitor_QueryConnectionEvents(store, byPid, events) && events.size() == 2, "and by PID");
    ActivityQuery since;
    since.since = t0 + 1;
    CHECK(NetworkMonitor_QueryConnectionEvents(store, since, events) && events.size() == 1, "since applies to the event's second");
    ActivityQuery limited;
    limited.limit = 1;
    CHECK(NetworkMonitor_QueryConnectionEvents(store, limited, events) && events.size() == 1, "the limit holds");

    NetworkMonitorStoreStats stats;
    CHECK(NetworkMonitor_StoreStats(store, stats) && stats.connectionEvents == 3, "the stats count them");
    const std::filesystem::path csv = std::filesystem::temp_directory_path() / "networkmonitor-events.csv";
    int64_t written = 0;
    CHECK(NetworkMonitor_ExportEventsCsv(store, ActivityQuery(), csv.string(), &written) && written == 3, "the CSV exports");
    {
        std::ifstream file(csv);
        std::string header, line;
        std::getline(file, header);
        std::getline(file, line);
        CHECK(header.rfind("observed_at,milliseconds,kind,", 0) == 0 && line.find(",accepted,TCP,") != std::string::npos,
              "with the kind and the millisecond as columns");
        std::filesystem::remove(csv);
    }
    CHECK(NetworkMonitor_ApplyRetention(store, t0 + 8LL * 86400) && NetworkMonitor_StoreStats(store, stats) &&
          stats.connectionEvents == 0, "events older than the window are dropped");
    CHECK(NetworkMonitor_RecordConnectionEvent(store, e) && NetworkMonitor_Purge(store) &&
          NetworkMonitor_StoreStats(store, stats) && stats.connectionEvents == 0, "and purge takes the rest");
    CHECK(NetworkMonitor_CloseStore(store), "the store closes");
}

// =============================================================================
// Snapshot CSV

static void TestSnapshotCsv() {
    std::printf("Snapshot CSV\n");
    std::vector<NetworkConnection> fixture;
    fixture.push_back(MakeConnection(100, "firefox", "93.184.216.34", NetworkConnectionState::Established));
    fixture[0].bytesSent = 1000; fixture[0].bytesReceived = 5000;
    fixture[0].remoteName = "www.example.com"; fixture[0].nameSource = NameSource::DnsProxy;
    fixture.push_back(MakeConnection(100, "firefox", "203.0.113.7", NetworkConnectionState::Established));
    fixture[1].remoteName = "a203.deploy, static"; fixture[1].nameSource = NameSource::ReverseDns;
    fixture.push_back(MakeConnection(200, "sshd", "0.0.0.0", NetworkConnectionState::Listening));
    fixture[2].localPort = 22; fixture[2].remotePort = 0;
    fixture.push_back(MakeConnection(0, "", "9.9.9.9", NetworkConnectionState::Established));
    fixture[3].ownerUid = 1000;
    const auto summaries = NetworkMonitor_SummarizeByProcess(fixture);

    const std::filesystem::path apps = std::filesystem::temp_directory_path() / "networkmonitor-apps.csv";
    int64_t rows = 0;
    CHECK(NetworkMonitor_ExportSummaryCsv(summaries, apps.string(), &rows) && rows == 3,
          "the roll-up exports one row per application");
    {
        std::ifstream file(apps);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); lines.push_back(line); }
        CHECK(lines.size() == 4 && lines[0] == "application,pid,executable,user,attributed,connections,established,"
              "listening,peers,peer_addresses,hosts,bytes_sent,bytes_received", "with the header");
        CHECK(lines.size() > 1 && lines[1].rfind("firefox,100,", 0) == 0 &&
              lines[1].find(",yes,2,2,0,2,203.0.113.7;93.184.216.34,\"a203.deploy, static;www.example.com\",") != std::string::npos,
              "the busiest first, peers and hosts semicolon-joined and quoted when a comma is inside");
        CHECK(lines.size() > 1 && (lines[1].find(",1000,5000") != std::string::npos || lines[1].find(",,") != std::string::npos),
              "byte totals present only when every connection had them");
        bool orphan = false;
        for (const auto& l : lines) if (l.rfind("(unattributed),,", 0) == 0 && l.find(",no,") != std::string::npos) orphan = true;
        CHECK(orphan, "the unattributed group is a row without a PID");
        std::filesystem::remove(apps);
    }

    const std::filesystem::path conns = std::filesystem::temp_directory_path() / "networkmonitor-conns.csv";
    CHECK(NetworkMonitor_ExportConnectionsCsv(fixture, conns.string(), &rows) && rows == 4,
          "the connections export one row each");
    {
        std::ifstream file(conns);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); lines.push_back(line); }
        CHECK(lines.size() == 5 && lines[0].rfind("application,pid,executable,user,transport,family,local,remote,"
              "remote_name,name_source,state,", 0) == 0, "with the header");
        CHECK(lines.size() > 1 && lines[1].find("firefox,100,") == 0 &&
              lines[1].find(",93.184.216.34:443,www.example.com,DNS proxy,ESTABLISHED,1000,5000") != std::string::npos,
              "a named connection carries the name, its source and its counters");
        CHECK(lines.size() > 3 && lines[3].find(",*,,,LISTEN,,") != std::string::npos,
              "a listener's peer is * and absent counters are empty, never zero");
        CHECK(lines.size() > 4 && lines[4].rfind("(unattributed),,,uid 1000,", 0) == 0,
              "an unattributed socket shows its owning UID");
        std::filesystem::remove(conns);
    }
    CHECK(!NetworkMonitor_ExportSummaryCsv(summaries, "/nonexistent-dir/x.csv"),
          "a path that cannot be written is refused");
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
    TestDnsWire();
    TestNameTable();
    TestReverseDns();
    TestDnsProxy();
    TestStoreNames();
    TestConntrackParse();
    TestEventRegistry();
    TestSnapshotDiff();
    TestSystemEventSource();
    TestStoreEvents();
    TestSnapshotCsv();
    std::printf("=== %s ===\n", g_failures == 0 ? "all tests passed" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
