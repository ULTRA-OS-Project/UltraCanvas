// core/UltraNet/UltraNetSocket.cpp
// Raw TCP/UDP sockets: POSIX on Linux/macOS, Winsock on Windows. Tracks each
// open file descriptor in a process-wide registry keyed by UltraNetHandle so
// callers operate on the handle rather than the OS fd.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetSocket.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using SocketFd = SOCKET;
  static constexpr SocketFd kInvalidFd = INVALID_SOCKET;
  static int LastSocketError() { return WSAGetLastError(); }
  static void CloseFd(SocketFd fd) { closesocket(fd); }
  static const char* SocketErrorString(int) {
      static thread_local char buf[256];
      DWORD len = FormatMessageA(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr, WSAGetLastError(),
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          buf, sizeof buf, nullptr);
      if (len == 0) std::snprintf(buf, sizeof buf, "winsock error %d", WSAGetLastError());
      return buf;
  }
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  using SocketFd = int;
  static constexpr SocketFd kInvalidFd = -1;
  static int LastSocketError() { return errno; }
  static void CloseFd(SocketFd fd) { ::close(fd); }
  static const char* SocketErrorString(int err) { return std::strerror(err); }
#endif

namespace {

#if defined(_WIN32) || defined(_WIN64)
struct WsaInit {
    WsaInit()  { WSADATA d{}; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { WSACleanup(); }
};
WsaInit g_wsaInit;
#endif

enum class Kind { TcpStream, TcpListener, Udp };

struct Entry {
    UltraNetHandle handle = UltraNetInvalidHandle;
    SocketFd       fd     = kInvalidFd;
    Kind           kind   = Kind::TcpStream;
    bool           isIpv6 = false;
};

std::mutex g_regMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Entry>> g_sockets;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Entry> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_regMutex);
    auto it = g_sockets.find(h);
    return it == g_sockets.end() ? nullptr : it->second;
}

UltraNetHandle Register(std::shared_ptr<Entry> e) {
    UltraNetHandle h = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    e->handle = h;
    std::lock_guard<std::mutex> lk(g_regMutex);
    g_sockets[h] = std::move(e);
    return h;
}

void Unregister(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_regMutex);
    g_sockets.erase(h);
}

void ApplyOptions(SocketFd fd, const UltraNetSocketOptions& opt) {
    int on = 1;
    if (opt.reuseAddress) {
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&on), sizeof on);
    }
    if (opt.keepAlive) {
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                     reinterpret_cast<const char*>(&on), sizeof on);
    }
    if (opt.noDelay) {
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&on), sizeof on);
    }
    if (opt.sendBufferSize > 0) {
        ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                     reinterpret_cast<const char*>(&opt.sendBufferSize),
                     sizeof opt.sendBufferSize);
    }
    if (opt.recvBufferSize > 0) {
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&opt.recvBufferSize),
                     sizeof opt.recvBufferSize);
    }
}

void ApplyTimeout(SocketFd fd, int sendMs, int recvMs) {
#if defined(_WIN32) || defined(_WIN64)
    if (sendMs > 0) {
        DWORD t = static_cast<DWORD>(sendMs);
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char*>(&t), sizeof t);
    }
    if (recvMs > 0) {
        DWORD t = static_cast<DWORD>(recvMs);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&t), sizeof t);
    }
#else
    auto toTimeval = [](int ms) {
        timeval tv{};
        tv.tv_sec  = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return tv;
    };
    if (sendMs > 0) {
        timeval tv = toTimeval(sendMs);
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    if (recvMs > 0) {
        timeval tv = toTimeval(recvMs);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
#endif
}

bool EndpointFromSockaddr(const sockaddr* sa, socklen_t /*sal*/,
                          UltraNetEndpoint& out) {
    char host[INET6_ADDRSTRLEN]{};
    if (sa->sa_family == AF_INET) {
        const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(sa);
        inet_ntop(AF_INET, &sin->sin_addr, host, sizeof host);
        out.address = host;
        out.port    = ntohs(sin->sin_port);
        out.isIpv6  = false;
        return true;
    }
    if (sa->sa_family == AF_INET6) {
        const sockaddr_in6* sin6 = reinterpret_cast<const sockaddr_in6*>(sa);
        inet_ntop(AF_INET6, &sin6->sin6_addr, host, sizeof host);
        out.address = host;
        out.port    = ntohs(sin6->sin6_port);
        out.isIpv6  = true;
        return true;
    }
    return false;
}

// Resolves host:port to a single sockaddr suitable for connect/sendto.
UltraNetResult Resolve(const std::string& host, int port, bool preferIpv6,
                       sockaddr_storage& outStorage, socklen_t& outLen,
                       bool& outIsIpv6) {
    addrinfo hints{};
    hints.ai_family   = preferIpv6 ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        if (res) ::freeaddrinfo(res);
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                     gai_strerror(rc));
    }
    std::memcpy(&outStorage, res->ai_addr, res->ai_addrlen);
    outLen    = static_cast<socklen_t>(res->ai_addrlen);
    outIsIpv6 = (res->ai_family == AF_INET6);
    ::freeaddrinfo(res);
    return UltraNetResult::Ok();
}

} // namespace

std::string UltraNetEndpoint::ToString() const {
    std::string out;
    if (isIpv6) { out += '['; out += address; out += ']'; }
    else        { out += address; }
    out += ':';
    out += std::to_string(port);
    return out;
}

// ============================================================================
// TCP
// ============================================================================
UltraNetHandle UltraNet_TcpConnect(const std::string& host, int port,
                                   const UltraNetSocketOptions& opt) {
    sockaddr_storage ss{};
    socklen_t        sl = 0;
    bool             v6 = false;
    auto r = Resolve(host, port, opt.useIpv6, ss, sl, v6);
    if (!r) return UltraNetInvalidHandle;

    SocketFd fd = ::socket(ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidFd) return UltraNetInvalidHandle;
    ApplyOptions(fd, opt);
    ApplyTimeout(fd, opt.sendTimeoutMs, opt.recvTimeoutMs);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&ss), sl) != 0) {
        CloseFd(fd);
        return UltraNetInvalidHandle;
    }
    auto e = std::make_shared<Entry>();
    e->fd     = fd;
    e->kind   = Kind::TcpStream;
    e->isIpv6 = v6;
    return Register(std::move(e));
}

UltraNetHandle UltraNet_TcpListen(int port,
                                  const UltraNetSocketOptions& opt) {
    const int family = opt.useIpv6 ? AF_INET6 : AF_INET;
    SocketFd fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidFd) return UltraNetInvalidHandle;
    ApplyOptions(fd, opt);

    sockaddr_storage ss{};
    socklen_t sl = 0;
    if (opt.useIpv6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        a->sin6_family = AF_INET6;
        a->sin6_addr   = in6addr_any;
        a->sin6_port   = htons(static_cast<uint16_t>(port));
        sl = sizeof(sockaddr_in6);
    } else {
        auto* a = reinterpret_cast<sockaddr_in*>(&ss);
        a->sin_family      = AF_INET;
        a->sin_addr.s_addr = INADDR_ANY;
        a->sin_port        = htons(static_cast<uint16_t>(port));
        sl = sizeof(sockaddr_in);
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&ss), sl) != 0 ||
        ::listen(fd, 64) != 0) {
        CloseFd(fd);
        return UltraNetInvalidHandle;
    }
    auto e = std::make_shared<Entry>();
    e->fd     = fd;
    e->kind   = Kind::TcpListener;
    e->isIpv6 = opt.useIpv6;
    return Register(std::move(e));
}

UltraNetHandle UltraNet_TcpAccept(UltraNetHandle listener,
                                  UltraNetEndpoint& outRemote) {
    auto e = Find(listener);
    if (!e || e->kind != Kind::TcpListener) return UltraNetInvalidHandle;

    sockaddr_storage ss{};
    socklen_t sl = sizeof ss;
    SocketFd fd = ::accept(e->fd, reinterpret_cast<sockaddr*>(&ss), &sl);
    if (fd == kInvalidFd) return UltraNetInvalidHandle;

    EndpointFromSockaddr(reinterpret_cast<sockaddr*>(&ss), sl, outRemote);
    auto child = std::make_shared<Entry>();
    child->fd     = fd;
    child->kind   = Kind::TcpStream;
    child->isIpv6 = (ss.ss_family == AF_INET6);
    return Register(std::move(child));
}

UltraNetResult UltraNet_TcpSend(UltraNetHandle handle,
                                const std::vector<uint8_t>& data,
                                int& outBytesSent) {
    outBytesSent = 0;
    auto e = Find(handle);
    if (!e || e->kind != Kind::TcpStream) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "not a TCP stream");
    }
    if (data.empty()) return UltraNetResult::Ok();
#if defined(_WIN32) || defined(_WIN64)
    int n = ::send(e->fd, reinterpret_cast<const char*>(data.data()),
                   static_cast<int>(data.size()), 0);
#else
    ssize_t n = ::send(e->fd, data.data(), data.size(), 0);
#endif
    if (n < 0) {
        return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                     SocketErrorString(LastSocketError()));
    }
    outBytesSent = static_cast<int>(n);
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_TcpReceive(UltraNetHandle handle,
                                   std::vector<uint8_t>& outData,
                                   int maxBytes,
                                   int timeoutMs) {
    outData.clear();
    auto e = Find(handle);
    if (!e || e->kind != Kind::TcpStream) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "not a TCP stream");
    }
    if (timeoutMs > 0) ApplyTimeout(e->fd, 0, timeoutMs);

    outData.resize(static_cast<std::size_t>(maxBytes > 0 ? maxBytes : 65536));
#if defined(_WIN32) || defined(_WIN64)
    int n = ::recv(e->fd, reinterpret_cast<char*>(outData.data()),
                   static_cast<int>(outData.size()), 0);
#else
    ssize_t n = ::recv(e->fd, outData.data(), outData.size(), 0);
#endif
    if (n < 0) {
        outData.clear();
        return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                     SocketErrorString(LastSocketError()));
    }
    outData.resize(static_cast<std::size_t>(n));
    return UltraNetResult::Ok();
}

// ============================================================================
// UDP
// ============================================================================
UltraNetHandle UltraNet_UdpOpen(int localPort,
                                const UltraNetSocketOptions& opt) {
    const int family = opt.useIpv6 ? AF_INET6 : AF_INET;
    SocketFd fd = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == kInvalidFd) return UltraNetInvalidHandle;
    ApplyOptions(fd, opt);
    ApplyTimeout(fd, opt.sendTimeoutMs, opt.recvTimeoutMs);

    if (localPort > 0) {
        sockaddr_storage ss{};
        socklen_t sl = 0;
        if (opt.useIpv6) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
            a->sin6_family = AF_INET6;
            a->sin6_addr   = in6addr_any;
            a->sin6_port   = htons(static_cast<uint16_t>(localPort));
            sl = sizeof(sockaddr_in6);
        } else {
            auto* a = reinterpret_cast<sockaddr_in*>(&ss);
            a->sin_family      = AF_INET;
            a->sin_addr.s_addr = INADDR_ANY;
            a->sin_port        = htons(static_cast<uint16_t>(localPort));
            sl = sizeof(sockaddr_in);
        }
        if (::bind(fd, reinterpret_cast<sockaddr*>(&ss), sl) != 0) {
            CloseFd(fd);
            return UltraNetInvalidHandle;
        }
    }
    auto e = std::make_shared<Entry>();
    e->fd     = fd;
    e->kind   = Kind::Udp;
    e->isIpv6 = opt.useIpv6;
    return Register(std::move(e));
}

UltraNetResult UltraNet_UdpSend(UltraNetHandle handle,
                                const std::string& host, int port,
                                const std::vector<uint8_t>& data) {
    auto e = Find(handle);
    if (!e || e->kind != Kind::Udp) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "not a UDP socket");
    }
    sockaddr_storage ss{};
    socklen_t sl = 0;
    bool v6 = false;
    auto r = Resolve(host, port, e->isIpv6, ss, sl, v6);
    if (!r) return r;

#if defined(_WIN32) || defined(_WIN64)
    int n = ::sendto(e->fd, reinterpret_cast<const char*>(data.data()),
                     static_cast<int>(data.size()), 0,
                     reinterpret_cast<sockaddr*>(&ss), sl);
#else
    ssize_t n = ::sendto(e->fd, data.data(), data.size(), 0,
                         reinterpret_cast<sockaddr*>(&ss), sl);
#endif
    if (n < 0) {
        return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                     SocketErrorString(LastSocketError()));
    }
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_UdpReceive(UltraNetHandle handle,
                                   std::vector<uint8_t>& outData,
                                   UltraNetEndpoint& outRemote,
                                   int timeoutMs) {
    outData.clear();
    auto e = Find(handle);
    if (!e || e->kind != Kind::Udp) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "not a UDP socket");
    }
    if (timeoutMs > 0) ApplyTimeout(e->fd, 0, timeoutMs);

    outData.resize(65536);
    sockaddr_storage ss{};
    socklen_t sl = sizeof ss;
#if defined(_WIN32) || defined(_WIN64)
    int n = ::recvfrom(e->fd, reinterpret_cast<char*>(outData.data()),
                       static_cast<int>(outData.size()), 0,
                       reinterpret_cast<sockaddr*>(&ss), &sl);
#else
    ssize_t n = ::recvfrom(e->fd, outData.data(), outData.size(), 0,
                           reinterpret_cast<sockaddr*>(&ss), &sl);
#endif
    if (n < 0) {
        outData.clear();
        return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                     SocketErrorString(LastSocketError()));
    }
    outData.resize(static_cast<std::size_t>(n));
    EndpointFromSockaddr(reinterpret_cast<sockaddr*>(&ss), sl, outRemote);
    return UltraNetResult::Ok();
}

// ============================================================================
// Common
// ============================================================================
UltraNetResult UltraNet_SocketClose(UltraNetHandle handle) {
    auto e = Find(handle);
    if (!e) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such socket");
    }
    CloseFd(e->fd);
    Unregister(handle);
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_SocketSetTimeout(UltraNetHandle handle,
                                         int sendTimeoutMs,
                                         int recvTimeoutMs) {
    auto e = Find(handle);
    if (!e) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such socket");
    }
    ApplyTimeout(e->fd, sendTimeoutMs, recvTimeoutMs);
    return UltraNetResult::Ok();
}
