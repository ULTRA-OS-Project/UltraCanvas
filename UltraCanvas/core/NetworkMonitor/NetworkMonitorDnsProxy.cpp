// core/NetworkMonitor/NetworkMonitorDnsProxy.cpp
// The local DNS proxy name source. One thread, one select() loop: UDP
// queries are forwarded to the upstream resolver on a socket of their own
// and the answer relayed back to the client and read on the way; TCP
// clients (a truncated answer makes a resolver retry over TCP) are relayed
// the same way through a small per-connection state machine. Nothing is
// rewritten, cached or filtered - the proxy is a window, not a resolver -
// and a query that gets no answer within the timeout is simply dropped,
// which is what the client expects from a lossy UDP resolver anyway.
//
// BSD sockets with the Winsock spellings behind three shims, the way the
// test suite and UltraNet already do it; no platform code beyond that.
//
// Version: 0.4.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorDns.h"
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorStore.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    using SocketHandle = SOCKET;
    static const SocketHandle kNoSocket = INVALID_SOCKET;
    static void CloseSocket(SocketHandle s) { ::closesocket(s); }
    static int LastSocketError() { return ::WSAGetLastError(); }
    static bool WouldBlock(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
    static void SetNonBlocking(SocketHandle s) { u_long on = 1; ::ioctlsocket(s, FIONBIO, &on); }
    static void NoSigPipe(SocketHandle) {}
    static constexpr int kSendFlags = 0;
    using SockLen = int;
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using SocketHandle = int;
    static const SocketHandle kNoSocket = -1;
    static void CloseSocket(SocketHandle s) { ::close(s); }
    static int LastSocketError() { return errno; }
    static bool WouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS; }
    static void SetNonBlocking(SocketHandle s) { ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
    // A TCP peer gone before its answer is written must not raise SIGPIPE
    // in the whole process: MSG_NOSIGNAL per send where it exists (Linux),
    // SO_NOSIGPIPE on the socket where it does not (macOS, the BSDs).
    #ifdef SO_NOSIGPIPE
    static void NoSigPipe(SocketHandle s) { int on = 1; ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on); }
    #else
    static void NoSigPipe(SocketHandle) {}
    #endif
    #ifdef MSG_NOSIGNAL
    static constexpr int kSendFlags = MSG_NOSIGNAL;
    #else
    static constexpr int kSendFlags = 0;
    #endif
    using SockLen = socklen_t;
#endif

namespace UltraCanvas {
namespace {

using Clock = std::chrono::steady_clock;

std::string SocketErrorText(int error) {
#ifdef _WIN32
    return "Winsock error " + std::to_string(error);
#else
    return std::string(std::strerror(error));
#endif
}

struct Endpoint {
    sockaddr_storage storage{};
    SockLen length = 0;
    int family() const { return storage.ss_family; }
    const sockaddr* addr() const { return reinterpret_cast<const sockaddr*>(&storage); }
    sockaddr* addr() { return reinterpret_cast<sockaddr*>(&storage); }
};

bool MakeEndpoint(const std::string& address, uint16_t port, Endpoint& out) {
    out = Endpoint();
    auto* in4 = reinterpret_cast<sockaddr_in*>(&out.storage);
    auto* in6 = reinterpret_cast<sockaddr_in6*>(&out.storage);
    if (::inet_pton(AF_INET, address.c_str(), &in4->sin_addr) == 1) {
        in4->sin_family = AF_INET;
        in4->sin_port = htons(port);
        out.length = sizeof(sockaddr_in);
        return true;
    }
    if (::inet_pton(AF_INET6, address.c_str(), &in6->sin6_addr) == 1) {
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(port);
        out.length = sizeof(sockaddr_in6);
        return true;
    }
    return false;
}

bool SameEndpoint(const Endpoint& a, const Endpoint& b) {
    if (a.family() != b.family()) return false;
    if (a.family() == AF_INET) {
        const auto* x = reinterpret_cast<const sockaddr_in*>(&a.storage);
        const auto* y = reinterpret_cast<const sockaddr_in*>(&b.storage);
        return x->sin_port == y->sin_port && std::memcmp(&x->sin_addr, &y->sin_addr, 4) == 0;
    }
    const auto* x = reinterpret_cast<const sockaddr_in6*>(&a.storage);
    const auto* y = reinterpret_cast<const sockaddr_in6*>(&b.storage);
    return x->sin6_port == y->sin6_port && std::memcmp(&x->sin6_addr, &y->sin6_addr, 16) == 0;
}

// A UDP query in flight: its own socket to the upstream, and the client to
// answer when the response arrives.
struct PendingUdp {
    SocketHandle upstream = kNoSocket;
    Endpoint client;
    Clock::time_point deadline;
};

// A TCP client being relayed. Length-prefixed messages both ways.
struct TcpRelay {
    enum class Stage { ReadingClient, ConnectingUpstream, SendingUpstream, ReadingUpstream, WritingClient };
    Stage stage = Stage::ReadingClient;
    SocketHandle client = kNoSocket;
    SocketHandle upstream = kNoSocket;
    std::vector<unsigned char> buffer;   // the message being read or written, with its 2-byte length
    std::size_t done = 0;                // bytes read / written so far
    Clock::time_point deadline;
};

constexpr std::size_t kMaxPendingUdp = 256;
constexpr std::size_t kMaxTcpRelays = 64;

class DnsProxySource : public INameSource {
public:
    explicit DnsProxySource(const DnsProxyOptions& options) : options_(options) {}
    ~DnsProxySource() override { Stop(); }

    std::string Name() const override {
        return "DNS proxy " + NetworkMonitor_FormatEndpoint(options_.listenAddress, options_.listenPort) +
               " -> " + NetworkMonitor_FormatEndpoint(upstreamText_, options_.upstreamPort);
    }
    NameSource Kind() const override { return NameSource::DnsProxy; }
    bool IsRunning() const override { return running_.load(); }
    std::string LastError() const override {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastError_;
    }

    NetworkMonitorResult Start(std::function<void(const DnsObservation&)> onObservation) override {
        if (running_.load()) return NetworkMonitorResult::Ok();
#ifdef _WIN32
        WSADATA data;
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError, "WSAStartup failed.");
        }
#endif
        upstreamText_ = options_.upstreamAddress.empty() ? NetworkMonitor_SystemResolver()
                                                         : options_.upstreamAddress;
        if (upstreamText_.empty()) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                "No upstream resolver: none configured on this system, so give one.");
        }
        if (!MakeEndpoint(upstreamText_, options_.upstreamPort, upstream_)) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                "Not an IP address: " + upstreamText_);
        }
        Endpoint listen;
        if (!MakeEndpoint(options_.listenAddress, options_.listenPort, listen)) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                "Not an IP address: " + options_.listenAddress);
        }
        if (SameEndpoint(listen, upstream_)) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::InvalidArgument,
                "The upstream resolver is the proxy itself (" + upstreamText_ + ":" +
                std::to_string(options_.upstreamPort) + "); it would forward to itself forever.");
        }

        // Both sockets on one port. With port 0 the UDP socket is given one
        // and the TCP socket must take the same, which another process may
        // grab in between - so a few tries.
        int error = 0;
        for (int attempt = 0; attempt < 8; ++attempt) {
            error = 0;
            udp_ = ::socket(listen.family(), SOCK_DGRAM, 0);
            tcp_ = ::socket(listen.family(), SOCK_STREAM, 0);
            if (udp_ == kNoSocket || tcp_ == kNoSocket) {
                error = LastSocketError();
                CloseAll();
                return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                                                   "Creating the listening sockets: " + SocketErrorText(error));
            }
            int one = 1;
            ::setsockopt(tcp_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
            if (::bind(udp_, listen.addr(), listen.length) != 0) { error = LastSocketError(); break; }
            Endpoint bound;
            bound.length = sizeof bound.storage;
            if (::getsockname(udp_, bound.addr(), &bound.length) != 0) { error = LastSocketError(); break; }
            const uint16_t port = ntohs(bound.family() == AF_INET
                ? reinterpret_cast<sockaddr_in*>(&bound.storage)->sin_port
                : reinterpret_cast<sockaddr_in6*>(&bound.storage)->sin6_port);
            if (::bind(tcp_, bound.addr(), bound.length) == 0 && ::listen(tcp_, 16) == 0) {
                options_.listenPort = port;
                break;
            }
            error = LastSocketError();
            if (options_.listenPort != 0) break;    // a fixed port: no point retrying
            CloseAll();                             // port 0: try another
        }
        if (error != 0) {
            CloseAll();
            const std::string where = NetworkMonitor_FormatEndpoint(options_.listenAddress, options_.listenPort);
#ifdef _WIN32
            const bool denied = error == WSAEACCES;
#else
            const bool denied = error == EACCES || error == EPERM;
#endif
            return NetworkMonitorResult::Error(
                denied ? NetworkMonitorResultCode::PermissionDenied : NetworkMonitorResultCode::IoError,
                "Binding " + where + ": " + SocketErrorText(error) +
                (denied ? " (ports below 1024 need privilege; choose another port or elevate)" : ""));
        }
        SetNonBlocking(udp_);
        SetNonBlocking(tcp_);
        onObservation_ = std::move(onObservation);
        stop_ = false;
        running_ = true;
        worker_ = std::thread([this] { Run(); });
        return NetworkMonitorResult::Ok();
    }

    void Stop() override {
        if (!running_.load()) return;
        stop_ = true;
        if (worker_.joinable()) worker_.join();
        CloseAll();
        running_ = false;
    }

    // The port the proxy listens on - what a caller that asked for port 0
    // needs to know. Not part of INameSource.
    uint16_t ListenPort() const { return options_.listenPort; }

private:
    void CloseAll() {
        if (udp_ != kNoSocket) { CloseSocket(udp_); udp_ = kNoSocket; }
        if (tcp_ != kNoSocket) { CloseSocket(tcp_); tcp_ = kNoSocket; }
        for (auto& pending : pendingUdp_) CloseSocket(pending.upstream);
        pendingUdp_.clear();
        for (auto& relay : relays_) {
            if (relay.client != kNoSocket) CloseSocket(relay.client);
            if (relay.upstream != kNoSocket) CloseSocket(relay.upstream);
        }
        relays_.clear();
    }

    void Fail(const std::string& text) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = text;
    }

    void Observe(const unsigned char* data, std::size_t size) {
        NetworkMonitorDns::Message message;
        DnsObservation observation;
        if (!NetworkMonitorDns::Parse(data, size, message) ||
            !NetworkMonitorDns::ToObservation(message, observation)) {
            return;
        }
        observation.source = NameSource::DnsProxy;
        observation.observedAt = NetworkMonitor_Now();
        if (onObservation_) onObservation_(observation);
    }

    void Run() {
        std::vector<unsigned char> buffer(NetworkMonitorDns::kMaxMessageBytes + 2);
        while (!stop_.load()) {
            fd_set readable, writable;
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            SocketHandle highest = udp_;
            auto watch = [&](SocketHandle s, fd_set& set) {
                FD_SET(s, &set);
                if (s > highest) highest = s;
            };
            watch(udp_, readable);
            watch(tcp_, readable);
            for (const auto& pending : pendingUdp_) watch(pending.upstream, readable);
            for (const auto& relay : relays_) {
                switch (relay.stage) {
                    case TcpRelay::Stage::ReadingClient:      watch(relay.client, readable); break;
                    case TcpRelay::Stage::ConnectingUpstream:
                    case TcpRelay::Stage::SendingUpstream:    watch(relay.upstream, writable); break;
                    case TcpRelay::Stage::ReadingUpstream:    watch(relay.upstream, readable); break;
                    case TcpRelay::Stage::WritingClient:      watch(relay.client, writable); break;
                }
            }
            timeval wait{};
            wait.tv_usec = 200 * 1000;
            const int ready = ::select(static_cast<int>(highest + 1), &readable, &writable, nullptr, &wait);
            if (ready < 0) {
                const int error = LastSocketError();
#ifndef _WIN32
                if (error == EINTR) continue;
#endif
                Fail("select failed: " + SocketErrorText(error));
                return;
            }
            const auto now = Clock::now();
            if (FD_ISSET(udp_, &readable)) ServeUdp(buffer);
            if (FD_ISSET(tcp_, &readable)) AcceptTcp(now);
            for (auto it = pendingUdp_.begin(); it != pendingUdp_.end();) {
                bool finished = false;
                if (FD_ISSET(it->upstream, &readable)) { RelayUdpAnswer(*it, buffer); finished = true; }
                else if (now >= it->deadline) finished = true;
                if (finished) { CloseSocket(it->upstream); it = pendingUdp_.erase(it); } else ++it;
            }
            for (auto it = relays_.begin(); it != relays_.end();) {
                const bool alive = now < it->deadline && StepRelay(*it, readable, writable, buffer);
                if (!alive) {
                    if (it->client != kNoSocket) CloseSocket(it->client);
                    if (it->upstream != kNoSocket) CloseSocket(it->upstream);
                    it = relays_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // ----- UDP -----

    void ServeUdp(std::vector<unsigned char>& buffer) {
        for (int burst = 0; burst < 64; ++burst) {
            Endpoint client;
            client.length = sizeof client.storage;
            const int got = ::recvfrom(udp_, reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(NetworkMonitorDns::kMaxMessageBytes), 0,
                                       client.addr(), &client.length);
            if (got < 0) return;                       // would block, or a stray ICMP error
            if (got < 12 || pendingUdp_.size() >= kMaxPendingUdp) continue;
            const SocketHandle upstream = ::socket(upstream_.family(), SOCK_DGRAM, 0);
            if (upstream == kNoSocket) continue;
            SetNonBlocking(upstream);
            if (::sendto(upstream, reinterpret_cast<const char*>(buffer.data()), got, 0,
                         upstream_.addr(), upstream_.length) != got) {
                CloseSocket(upstream);
                continue;
            }
            PendingUdp pending;
            pending.upstream = upstream;
            pending.client = client;
            pending.deadline = Clock::now() + std::chrono::milliseconds(options_.upstreamTimeoutMs);
            pendingUdp_.push_back(pending);
        }
    }

    void RelayUdpAnswer(PendingUdp& pending, std::vector<unsigned char>& buffer) {
        Endpoint from;
        from.length = sizeof from.storage;
        const int got = ::recvfrom(pending.upstream, reinterpret_cast<char*>(buffer.data()),
                                   static_cast<int>(NetworkMonitorDns::kMaxMessageBytes), 0,
                                   from.addr(), &from.length);
        if (got < 12) return;
        // Only the resolver asked may answer; anything else on this
        // ephemeral port is noise.
        if (!SameEndpoint(from, upstream_)) return;
        ::sendto(udp_, reinterpret_cast<const char*>(buffer.data()), got, 0,
                 pending.client.addr(), pending.client.length);
        Observe(buffer.data(), static_cast<std::size_t>(got));
    }

    // ----- TCP -----

    void AcceptTcp(Clock::time_point now) {
        Endpoint client;
        client.length = sizeof client.storage;
        const SocketHandle accepted = ::accept(tcp_, client.addr(), &client.length);
        if (accepted == kNoSocket) return;
        if (relays_.size() >= kMaxTcpRelays) { CloseSocket(accepted); return; }
        SetNonBlocking(accepted);
        NoSigPipe(accepted);
        TcpRelay relay;
        relay.client = accepted;
        relay.deadline = now + std::chrono::milliseconds(options_.upstreamTimeoutMs * 2);
        relays_.push_back(relay);
    }

    // Reads a length-prefixed message from `s` into relay.buffer; true when
    // complete, false while more is expected; `dead` when the peer is gone.
    static bool ReadFramed(SocketHandle s, TcpRelay& relay, bool& dead) {
        dead = false;
        for (;;) {
            // The length is known once two bytes are in; until then, want them.
            std::size_t want = 2;
            if (relay.done >= 2) {
                want = 2 + ((static_cast<std::size_t>(relay.buffer[0]) << 8) | relay.buffer[1]);
            }
            if (want > NetworkMonitorDns::kMaxMessageBytes + 2) { dead = true; return false; }
            if (relay.done >= want) return true;
            if (relay.buffer.size() < want) relay.buffer.resize(want);
            const int got = ::recv(s, reinterpret_cast<char*>(relay.buffer.data() + relay.done),
                                   static_cast<int>(want - relay.done), 0);
            if (got == 0) { dead = true; return false; }
            if (got < 0) {
                if (WouldBlock(LastSocketError())) return false;
                dead = true;
                return false;
            }
            relay.done += static_cast<std::size_t>(got);
        }
    }

    static bool WriteFramed(SocketHandle s, TcpRelay& relay, bool& dead) {
        dead = false;
        while (relay.done < relay.buffer.size()) {
            const int sent = ::send(s, reinterpret_cast<const char*>(relay.buffer.data() + relay.done),
                                    static_cast<int>(relay.buffer.size() - relay.done), kSendFlags);
            if (sent < 0) {
                if (WouldBlock(LastSocketError())) return false;
                dead = true;
                return false;
            }
            relay.done += static_cast<std::size_t>(sent);
        }
        return true;
    }

    bool StepRelay(TcpRelay& relay, fd_set& readable, fd_set& writable, std::vector<unsigned char>&) {
        bool dead = false;
        switch (relay.stage) {
            case TcpRelay::Stage::ReadingClient:
                if (!FD_ISSET(relay.client, &readable)) return true;
                if (!ReadFramed(relay.client, relay, dead)) return !dead;
                relay.upstream = ::socket(upstream_.family(), SOCK_STREAM, 0);
                if (relay.upstream == kNoSocket) return false;
                SetNonBlocking(relay.upstream);
                NoSigPipe(relay.upstream);
                if (::connect(relay.upstream, upstream_.addr(), upstream_.length) != 0 &&
                    !WouldBlock(LastSocketError())) {
                    return false;
                }
                relay.stage = TcpRelay::Stage::ConnectingUpstream;
                relay.done = 0;
                return true;
            case TcpRelay::Stage::ConnectingUpstream:
                if (!FD_ISSET(relay.upstream, &writable)) return true;
                {
                    int error = 0;
                    SockLen length = sizeof error;
                    ::getsockopt(relay.upstream, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
                    if (error != 0) return false;
                }
                relay.stage = TcpRelay::Stage::SendingUpstream;
                [[fallthrough]];
            case TcpRelay::Stage::SendingUpstream:
                if (!WriteFramed(relay.upstream, relay, dead)) return !dead;
                relay.stage = TcpRelay::Stage::ReadingUpstream;
                relay.buffer.clear();
                relay.done = 0;
                return true;
            case TcpRelay::Stage::ReadingUpstream:
                if (!FD_ISSET(relay.upstream, &readable)) return true;
                if (!ReadFramed(relay.upstream, relay, dead)) return !dead;
                Observe(relay.buffer.data() + 2, relay.buffer.size() - 2);
                relay.stage = TcpRelay::Stage::WritingClient;
                relay.done = 0;
                [[fallthrough]];
            case TcpRelay::Stage::WritingClient:
                if (!WriteFramed(relay.client, relay, dead)) return !dead;
                // One query per connection is what resolvers do; a client
                // that pipelines gets its next answer on a new connection.
                return false;
        }
        return false;
    }

    DnsProxyOptions options_;
    std::string upstreamText_;
    Endpoint upstream_;
    SocketHandle udp_ = kNoSocket;
    SocketHandle tcp_ = kNoSocket;
    std::list<PendingUdp> pendingUdp_;
    std::list<TcpRelay> relays_;
    std::function<void(const DnsObservation&)> onObservation_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace

std::unique_ptr<INameSource> NetworkMonitor_CreateDnsProxySource(const DnsProxyOptions& options) {
    return std::make_unique<DnsProxySource>(options);
}

// The bound port of a proxy created with listenPort 0, for tests. Internal.
uint16_t NetworkMonitor_DnsProxyListenPort(const INameSource& source);
uint16_t NetworkMonitor_DnsProxyListenPort(const INameSource& source) {
    if (const auto* proxy = dynamic_cast<const DnsProxySource*>(&source)) return proxy->ListenPort();
    return 0;
}

} // namespace UltraCanvas
