// Apps/AnchorPoint/net/RawSocketTransport.cpp
// AnchorPoint - raw POSIX / Winsock implementation of the Transport interface.
// Version: 0.1.0
// Author: AnchorPoint
//
// Cross-platform (Linux + Windows). This is the "today" backend; it will be
// superseded by an UltraNet-backed implementation that exposes the same
// AnchorPoint::IConnection / IListener interface.
#include "Transport.h"

#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socklen_t = int;
  using ssize_t = long long;
  #define AP_CLOSESOCK closesocket
  #define AP_BAD_SOCKET INVALID_SOCKET
  using ap_socket_t = SOCKET;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #define AP_CLOSESOCK ::close
  #define AP_BAD_SOCKET (-1)
  using ap_socket_t = int;
#endif

namespace AnchorPoint {

namespace {

// Process-wide Winsock init (no-op elsewhere).
struct NetInit {
    NetInit() {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    }
    ~NetInit() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};
void EnsureNetInit() { static NetInit init; (void)init; }

std::string LastError() {
#ifdef _WIN32
    return "winsock error " + std::to_string(WSAGetLastError());
#else
    return std::string(std::strerror(errno));
#endif
}

void SetNoDelay(ap_socket_t s) {
    int one = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
}

std::string FormatPeer(const sockaddr_storage& ss) {
    char host[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    if (ss.ss_family == AF_INET) {
        auto* a = reinterpret_cast<const sockaddr_in*>(&ss);
        ::inet_ntop(AF_INET, &a->sin_addr, host, sizeof(host));
        port = ntohs(a->sin_port);
    } else if (ss.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<const sockaddr_in6*>(&ss);
        ::inet_ntop(AF_INET6, &a->sin6_addr, host, sizeof(host));
        port = ntohs(a->sin6_port);
    }
    return std::string(host) + ":" + std::to_string(port);
}

class SocketConnection : public IConnection {
public:
    SocketConnection(ap_socket_t fd, std::string peer)
        : fd_(fd), peer_(std::move(peer)) {}
    ~SocketConnection() override { Close(); }

    bool SendAll(const void* data, size_t len) override {
        const char* p = static_cast<const char*>(data);
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::send(fd_, p + sent, static_cast<int>(len - sent), 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    bool RecvAll(void* data, size_t len) override {
        char* p = static_cast<char*>(data);
        size_t got = 0;
        while (got < len) {
            ssize_t n = ::recv(fd_, p + got, static_cast<int>(len - got), 0);
            if (n <= 0) return false; // 0 = peer closed, <0 = error
            got += static_cast<size_t>(n);
        }
        return true;
    }

    void Close() override {
        if (fd_ != AP_BAD_SOCKET) {
            AP_CLOSESOCK(fd_);
            fd_ = AP_BAD_SOCKET;
        }
    }

    std::string PeerAddress() const override { return peer_; }

private:
    ap_socket_t fd_;
    std::string peer_;
};

class SocketListener : public IListener {
public:
    SocketListener(ap_socket_t fd, uint16_t port) : fd_(fd), port_(port) {}
    ~SocketListener() override { Close(); }

    std::unique_ptr<IConnection> Accept() override {
        sockaddr_storage ss{};
        socklen_t slen = sizeof(ss);
        ap_socket_t c = ::accept(fd_, reinterpret_cast<sockaddr*>(&ss), &slen);
        if (c == AP_BAD_SOCKET) return nullptr;
        SetNoDelay(c);
        return std::make_unique<SocketConnection>(c, FormatPeer(ss));
    }

    uint16_t Port() const override { return port_; }

    void Close() override {
        if (fd_ != AP_BAD_SOCKET) {
            AP_CLOSESOCK(fd_);
            fd_ = AP_BAD_SOCKET;
        }
    }

private:
    ap_socket_t fd_;
    uint16_t port_;
};

} // namespace

std::unique_ptr<IConnection> Connect(const std::string& host, uint16_t port,
                                     std::string* err) {
    EnsureNetInit();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "cannot resolve " + host;
        return nullptr;
    }

    ap_socket_t fd = AP_BAD_SOCKET;
    std::string peer;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == AP_BAD_SOCKET) continue;
        if (::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
            sockaddr_storage ss{};
            std::memcpy(&ss, ai->ai_addr, ai->ai_addrlen);
            peer = FormatPeer(ss);
            break;
        }
        AP_CLOSESOCK(fd);
        fd = AP_BAD_SOCKET;
    }
    ::freeaddrinfo(res);

    if (fd == AP_BAD_SOCKET) {
        if (err) *err = "connect to " + host + ":" + portStr + " failed: " + LastError();
        return nullptr;
    }
    SetNoDelay(fd);
    return std::make_unique<SocketConnection>(fd, peer);
}

std::unique_ptr<IListener> Listen(uint16_t port, std::string* err) {
    EnsureNetInit();

    ap_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == AP_BAD_SOCKET) {
        if (err) *err = "socket() failed: " + LastError();
        return nullptr;
    }

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (err) *err = "bind(:" + std::to_string(port) + ") failed: " + LastError();
        AP_CLOSESOCK(fd);
        return nullptr;
    }

    if (::listen(fd, 4) != 0) {
        if (err) *err = "listen() failed: " + LastError();
        AP_CLOSESOCK(fd);
        return nullptr;
    }

    // Resolve the actual bound port (in case caller passed 0).
    sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    uint16_t actualPort = port;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &blen) == 0) {
        actualPort = ntohs(bound.sin_port);
    }

    return std::make_unique<SocketListener>(fd, actualPort);
}

} // namespace AnchorPoint
