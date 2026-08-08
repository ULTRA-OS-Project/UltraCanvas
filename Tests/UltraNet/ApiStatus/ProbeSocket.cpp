// Tests/UltraNet/ApiStatus/ProbeSocket.cpp
// Probes for UltraNet/UltraNetSocket.h — raw TCP and UDP. Everything here runs
// against loopback peers the probe starts itself, so the whole area is
// verifiable with no network at all.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "Socket";
constexpr int kProbePortStart = 18820;
constexpr int kProbePortEnd   = 18879;

std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::string Text(const std::vector<uint8_t>& b) {
    return std::string(b.begin(), b.end());
}

// A one-shot TCP echo peer: accepts a single connection, echoes until the
// client closes, then goes away. Used to give the TCP probes something real
// to talk to without depending on the HTTP loopback origin.
class EchoPeer {
public:
    bool Start() {
        UltraNetSocketOptions opt;
        opt.reuseAddress = true;
        for (int port = kProbePortStart; port <= kProbePortEnd; ++port) {
            UltraNetHandle h = UltraNet_TcpListen(port, opt);
            if (h != UltraNetInvalidHandle) {
                listener_ = h;
                port_ = port;
                break;
            }
        }
        if (listener_ == UltraNetInvalidHandle) return false;

        thread_ = std::thread([this] {
            UltraNetEndpoint remote;
            UltraNetHandle conn = UltraNet_TcpAccept(listener_, remote);
            if (conn == UltraNetInvalidHandle) return;
            {
                remoteSeen_ = remote;
                accepted_.store(true);
            }
            for (;;) {
                std::vector<uint8_t> in;
                if (!UltraNet_TcpReceive(conn, in, 65536, 5000) || in.empty()) break;
                int sent = 0;
                if (!UltraNet_TcpSend(conn, in, sent)) break;
            }
            UltraNet_SocketClose(conn);
        });
        return true;
    }

    void Stop() {
        if (thread_.joinable()) thread_.join();
        if (listener_ != UltraNetInvalidHandle) {
            UltraNet_SocketClose(listener_);
            listener_ = UltraNetInvalidHandle;
        }
    }

    // Unblocks a peer nobody connected to, so Stop() can join.
    void Abandon() {
        UltraNetSocketOptions opt;
        opt.connectTimeoutMs = 500;
        UltraNetHandle poke = UltraNet_TcpConnect("127.0.0.1", port_, opt);
        if (poke != UltraNetInvalidHandle) UltraNet_SocketClose(poke);
    }

    ~EchoPeer() { Abandon(); Stop(); }

    int  Port() const { return port_; }
    bool Accepted() const { return accepted_.load(); }
    UltraNetEndpoint RemoteSeen() const { return remoteSeen_; }
    UltraNetHandle Listener() const { return listener_; }

private:
    UltraNetHandle    listener_ = UltraNetInvalidHandle;
    int               port_ = 0;
    std::atomic<bool> accepted_{false};
    UltraNetEndpoint  remoteSeen_;
    std::thread       thread_;
};

} // namespace

// ---------------------------------------------------------------------------
// TCP
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_TcpConnect) {
    EchoPeer peer;
    PROBE_EXPECT_MSG(peer.Start(), "could not bind a loopback listener");

    UltraNetSocketOptions opt;
    opt.connectTimeoutMs = 2000;
    opt.noDelay = true;
    const UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", peer.Port(), opt);
    PROBE_EXPECT(client != UltraNetInvalidHandle);
    UltraNet_SocketClose(client);

    // A port nobody listens on must fail rather than hand back a handle.
    const UltraNetHandle refused = UltraNet_TcpConnect("127.0.0.1", 1, opt);
    PROBE_EXPECT(refused == UltraNetInvalidHandle);

    const UltraNetHandle unknownHost =
        UltraNet_TcpConnect("ultranet-probe-nowhere.invalid", 80, opt);
    PROBE_EXPECT(unknownHost == UltraNetInvalidHandle);
    return Working("connected to a loopback listener; refused port and unknown "
                   "host both return the invalid handle");
}

ULTRANET_PROBE(kArea, UltraNet_TcpListen) {
    UltraNetSocketOptions opt;
    opt.reuseAddress = true;

    EchoPeer peer;
    PROBE_EXPECT_MSG(peer.Start(), "could not bind a loopback listener");
    PROBE_EXPECT(peer.Listener() != UltraNetInvalidHandle);

    // A listener is not a stream: send/receive on it must be rejected.
    int sent = 0;
    const UltraNetResult badSend = UltraNet_TcpSend(peer.Listener(), Bytes("x"), sent);
    PROBE_EXPECT(!badSend && badSend.code == UltraNetResultCode::InvalidHandle);

    // The bound port must actually be reachable.
    const UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", peer.Port(), opt);
    PROBE_EXPECT(client != UltraNetInvalidHandle);
    UltraNet_SocketClose(client);
    return Working("bound a listening socket on port " + std::to_string(peer.Port()) +
                   " that accepts connections; send on a listener rejected");
}

ULTRANET_PROBE(kArea, UltraNet_TcpAccept) {
    EchoPeer peer;
    PROBE_EXPECT_MSG(peer.Start(), "could not bind a loopback listener");

    UltraNetSocketOptions opt;
    opt.connectTimeoutMs = 2000;
    const UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", peer.Port(), opt);
    PROBE_EXPECT(client != UltraNetInvalidHandle);

    for (int i = 0; i < 100 && !peer.Accepted(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    PROBE_EXPECT_MSG(peer.Accepted(), "accept never returned a connection");

    const UltraNetEndpoint remote = peer.RemoteSeen();
    UltraNet_SocketClose(client);

    PROBE_EXPECT(remote.address == "127.0.0.1");
    PROBE_EXPECT(remote.port > 0);

    // Accepting on a non-listener must fail.
    UltraNetEndpoint ignored;
    PROBE_EXPECT(UltraNet_TcpAccept(UltraNetInvalidHandle, ignored) ==
                 UltraNetInvalidHandle);
    return Working("accepted a loopback connection and reported the peer as " +
                   remote.ToString());
}

ULTRANET_PROBE(kArea, UltraNet_TcpSend) {
    int sent = 0;
    const UltraNetResult bad = UltraNet_TcpSend(UltraNetInvalidHandle, Bytes("x"), sent);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    EchoPeer peer;
    PROBE_EXPECT_MSG(peer.Start(), "could not bind a loopback listener");
    const UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", peer.Port());
    PROBE_EXPECT(client != UltraNetInvalidHandle);

    const std::string message = "ultranet tcp send probe";
    const UltraNetResult r = UltraNet_TcpSend(client, Bytes(message), sent);
    const bool ok = static_cast<bool>(r) &&
                    sent == static_cast<int>(message.size());

    // An empty payload is a documented no-op success.
    int zero = -1;
    const UltraNetResult emptyResult = UltraNet_TcpSend(client, {}, zero);
    UltraNet_SocketClose(client);

    PROBE_EXPECT_MSG(ok, "sent " + std::to_string(sent) + " of " +
                         std::to_string(message.size()) + " bytes: " + r.message);
    PROBE_EXPECT(static_cast<bool>(emptyResult) && zero == 0);
    return Working("sent " + std::to_string(sent) + " bytes over loopback; "
                   "empty payload is a no-op; invalid handle rejected");
}

ULTRANET_PROBE(kArea, UltraNet_TcpReceive) {
    std::vector<uint8_t> out;
    const UltraNetResult bad = UltraNet_TcpReceive(UltraNetInvalidHandle, out);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    EchoPeer peer;
    PROBE_EXPECT_MSG(peer.Start(), "could not bind a loopback listener");
    const UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", peer.Port());
    PROBE_EXPECT(client != UltraNetInvalidHandle);

    const std::string message = "round trip through the echo peer";
    int sent = 0;
    PROBE_EXPECT(static_cast<bool>(UltraNet_TcpSend(client, Bytes(message), sent)));

    // The echo may arrive in pieces; keep reading until we have it all.
    std::string received;
    for (int i = 0; i < 50 && received.size() < message.size(); ++i) {
        std::vector<uint8_t> chunk;
        if (!UltraNet_TcpReceive(client, chunk, 65536, 1000)) break;
        if (chunk.empty()) break;
        received += Text(chunk);
    }

    // With a short timeout and nothing to read, the call must return rather
    // than block for ever.
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> nothing;
    UltraNet_TcpReceive(client, nothing, 65536, 300);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    UltraNet_SocketClose(client);

    PROBE_EXPECT_MSG(received == message,
                     "echo returned \"" + received + "\"");
    PROBE_EXPECT_MSG(elapsed < 3000,
                     "a 300 ms receive timeout took " + std::to_string(elapsed) + " ms");
    return Working("payload echoed back byte-for-byte; the receive timeout is "
                   "honoured (" + std::to_string(elapsed) + " ms for a 300 ms "
                   "timeout); invalid handle rejected");
}

// ---------------------------------------------------------------------------
// UDP
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_UdpOpen) {
    const UltraNetHandle ephemeral = UltraNet_UdpOpen();
    PROBE_EXPECT(ephemeral != UltraNetInvalidHandle);
    UltraNet_SocketClose(ephemeral);

    UltraNetSocketOptions opt;
    opt.reuseAddress = true;
    const UltraNetHandle bound = UltraNet_UdpOpen(kProbePortStart + 50, opt);
    PROBE_EXPECT(bound != UltraNetInvalidHandle);

    // A UDP socket is not a TCP stream.
    int sent = 0;
    const UltraNetResult wrongKind = UltraNet_TcpSend(bound, Bytes("x"), sent);
    UltraNet_SocketClose(bound);
    PROBE_EXPECT(!wrongKind && wrongKind.code == UltraNetResultCode::InvalidHandle);
    return Working("opened both an ephemeral and an explicitly bound UDP "
                   "socket; kind checking rejects TCP calls on them");
}

ULTRANET_PROBE(kArea, UltraNet_UdpSend) {
    const UltraNetResult bad =
        UltraNet_UdpSend(UltraNetInvalidHandle, "127.0.0.1", 9, Bytes("x"));
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    const int port = kProbePortStart + 51;
    UltraNetSocketOptions opt;
    opt.reuseAddress = true;
    const UltraNetHandle receiver = UltraNet_UdpOpen(port, opt);
    PROBE_EXPECT(receiver != UltraNetInvalidHandle);
    const UltraNetHandle sender = UltraNet_UdpOpen();
    PROBE_EXPECT(sender != UltraNetInvalidHandle);

    const std::string datagram = "ultranet udp datagram";
    const UltraNetResult r = UltraNet_UdpSend(sender, "127.0.0.1", port, Bytes(datagram));

    std::vector<uint8_t> got;
    UltraNetEndpoint from;
    const UltraNetResult recv = UltraNet_UdpReceive(receiver, got, from, 2000);

    const UltraNetResult unknownHost =
        UltraNet_UdpSend(sender, "ultranet-probe-nowhere.invalid", 9, Bytes("x"));

    UltraNet_SocketClose(sender);
    UltraNet_SocketClose(receiver);

    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT_MSG(static_cast<bool>(recv) && Text(got) == datagram,
                     "receiver saw \"" + Text(got) + "\"");
    PROBE_EXPECT(!unknownHost && unknownHost.code == UltraNetResultCode::HostNotFound);
    return Working("datagram delivered over loopback; unresolvable destination "
                   "reports HostNotFound; invalid handle rejected");
}

ULTRANET_PROBE(kArea, UltraNet_UdpReceive) {
    std::vector<uint8_t> out;
    UltraNetEndpoint from;
    const UltraNetResult bad = UltraNet_UdpReceive(UltraNetInvalidHandle, out, from);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    const int port = kProbePortStart + 52;
    UltraNetSocketOptions opt;
    opt.reuseAddress = true;
    const UltraNetHandle receiver = UltraNet_UdpOpen(port, opt);
    PROBE_EXPECT(receiver != UltraNetInvalidHandle);
    const UltraNetHandle sender = UltraNet_UdpOpen();
    PROBE_EXPECT(sender != UltraNetInvalidHandle);

    PROBE_EXPECT(static_cast<bool>(
        UltraNet_UdpSend(sender, "127.0.0.1", port, Bytes("peer-check"))));

    const UltraNetResult r = UltraNet_UdpReceive(receiver, out, from, 2000);
    const bool gotPayload = static_cast<bool>(r) && Text(out) == "peer-check";
    const bool gotPeer = from.address == "127.0.0.1" && from.port > 0;

    // Nothing more is coming: the timeout must fire instead of blocking.
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> nothing;
    UltraNetEndpoint ignored;
    const UltraNetResult timedOut =
        UltraNet_UdpReceive(receiver, nothing, ignored, 300);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    UltraNet_SocketClose(sender);
    UltraNet_SocketClose(receiver);

    PROBE_EXPECT_MSG(gotPayload, "received \"" + Text(out) + "\": " + r.message);
    PROBE_EXPECT_MSG(gotPeer, "sender endpoint reported as " + from.ToString());
    PROBE_EXPECT(!timedOut);
    PROBE_EXPECT_MSG(elapsed < 3000,
                     "a 300 ms receive timeout took " + std::to_string(elapsed) + " ms");
    return Working("payload and sender endpoint (" + from.ToString() +
                   ") both reported; timeout honoured");
}

// ---------------------------------------------------------------------------
// Common
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_SocketClose) {
    const UltraNetResult bad = UltraNet_SocketClose(UltraNetInvalidHandle);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    const UltraNetHandle udp = UltraNet_UdpOpen();
    PROBE_EXPECT(udp != UltraNetInvalidHandle);
    PROBE_EXPECT(static_cast<bool>(UltraNet_SocketClose(udp)));

    // The handle must be retired, not merely closed.
    const UltraNetResult again = UltraNet_SocketClose(udp);
    PROBE_EXPECT(!again && again.code == UltraNetResultCode::InvalidHandle);
    return Working("closes and unregisters the handle; double close and unknown "
                   "handles report InvalidHandle");
}

ULTRANET_PROBE(kArea, UltraNet_SocketSetTimeout) {
    const UltraNetResult bad =
        UltraNet_SocketSetTimeout(UltraNetInvalidHandle, 100, 100);
    PROBE_EXPECT(!bad && bad.code == UltraNetResultCode::InvalidHandle);

    UltraNetSocketOptions opt;
    opt.reuseAddress = true;
    const UltraNetHandle udp = UltraNet_UdpOpen(kProbePortStart + 53, opt);
    PROBE_EXPECT(udp != UltraNetInvalidHandle);

    const UltraNetResult set = UltraNet_SocketSetTimeout(udp, 0, 250);
    PROBE_EXPECT_MSG(static_cast<bool>(set), set.message);

    // timeoutMs = 0 means "use whatever is on the socket", so the 250 ms
    // receive timeout just installed is what must fire.
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> nothing;
    UltraNetEndpoint ignored;
    const UltraNetResult r = UltraNet_UdpReceive(udp, nothing, ignored, 0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    UltraNet_SocketClose(udp);

    PROBE_EXPECT_MSG(!r, "a receive with no data pending reported success");
    PROBE_EXPECT_MSG(elapsed >= 200 && elapsed < 2000,
                     "a 250 ms socket timeout fired after " +
                     std::to_string(elapsed) + " ms");
    return Working("SO_RCVTIMEO applied: a blocking receive returned after " +
                   std::to_string(elapsed) + " ms");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetEndpoint::ToString", EndpointToString) {
    UltraNetEndpoint v4;
    v4.address = "192.0.2.10";
    v4.port = 8080;
    PROBE_EXPECT(v4.ToString() == "192.0.2.10:8080");

    UltraNetEndpoint v6;
    v6.address = "::1";
    v6.port = 443;
    v6.isIpv6 = true;
    PROBE_EXPECT(v6.ToString() == "[::1]:443");
    return Working("IPv4 as host:port, IPv6 bracketed per RFC 3986");
}
