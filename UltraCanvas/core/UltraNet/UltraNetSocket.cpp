// core/UltraNet/UltraNetSocket.cpp
// Raw TCP/UDP sockets: POSIX on Linux/macOS, Winsock on Windows. Tracks each
// open file descriptor in a process-wide registry keyed by UltraNetHandle so
// callers operate on the handle rather than the OS fd.
// Version: 0.4.0 - a TCP connection through a proxy: SOCKS 5, SOCKS 4 / 4a and an HTTP
//                  CONNECT tunnel (UltraNet_TcpOpen); UltraNet_SocketDetach
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetSocket.h"
#include "UltraNetSocketInternal.h"
#include "UltraNet/UltraNetMime.h"   // UltraNet_Base64Encode: an HTTP proxy's Basic sign-in

#include <atomic>
#include <cctype>
#include <cstdint>
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

    // TLS layer (populated by UltraNet_TlsWrap via AttachTlsToSocket).
    // If tlsCtx is non-null, send/recv/close are routed through these
    // function pointers instead of touching the raw socket directly.
    void*                       tlsCtx   = nullptr;
    ultranet_internal::TlsReadFn  tlsRead  = nullptr;
    ultranet_internal::TlsWriteFn tlsWrite = nullptr;
    ultranet_internal::TlsCloseFn tlsClose = nullptr;
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

// Fills a sockaddr for bind(): opt.bindAddress as a literal IPv4/IPv6
// address, or the any-address when empty. Returns false on a literal that
// doesn't parse in the socket's address family.
bool BuildBindAddress(const UltraNetSocketOptions& opt, int port,
                      sockaddr_storage& ss, socklen_t& sl) {
    if (opt.useIpv6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        a->sin6_family = AF_INET6;
        a->sin6_addr   = in6addr_any;
        a->sin6_port   = htons(static_cast<uint16_t>(port));
        sl = sizeof(sockaddr_in6);
        if (!opt.bindAddress.empty() &&
            inet_pton(AF_INET6, opt.bindAddress.c_str(), &a->sin6_addr) != 1) {
            return false;
        }
    } else {
        auto* a = reinterpret_cast<sockaddr_in*>(&ss);
        a->sin_family      = AF_INET;
        a->sin_addr.s_addr = INADDR_ANY;
        a->sin_port        = htons(static_cast<uint16_t>(port));
        sl = sizeof(sockaddr_in);
        if (!opt.bindAddress.empty() &&
            inet_pton(AF_INET, opt.bindAddress.c_str(), &a->sin_addr) != 1) {
            return false;
        }
    }
    return true;
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

// ---- Through a proxy ---------------------------------------------------------

// The send and receive timeouts as given, 0 included (no limit) -
// ApplyTimeout leaves a 0 alone, which would keep a handshake's limit on the
// connection after it.
void SetTimeouts(SocketFd fd, int sendMs, int recvMs) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD s = static_cast<DWORD>(sendMs > 0 ? sendMs : 0);
    DWORD r = static_cast<DWORD>(recvMs > 0 ? recvMs : 0);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&s), sizeof s);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&r), sizeof r);
#else
    auto toTimeval = [](int ms) {
        timeval tv{};
        if (ms > 0) {
            tv.tv_sec  = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
        }
        return tv;
    };
    timeval s = toTimeval(sendMs), r = toTimeval(recvMs);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &s, sizeof s);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &r, sizeof r);
#endif
}

bool SendAll(SocketFd fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32) || defined(_WIN64)
        const int n = ::send(fd, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool ReceiveExactly(SocketFd fd, unsigned char* buf, std::size_t len) {
    std::size_t got = 0;
    while (got < len) {
#if defined(_WIN32) || defined(_WIN64)
        const int n = ::recv(fd, reinterpret_cast<char*>(buf) + got, static_cast<int>(len - got), 0);
#else
        const ssize_t n = ::recv(fd, buf + got, len - got, 0);
#endif
        if (n <= 0) return false;
        got += static_cast<std::size_t>(n);
    }
    return true;
}

UltraNetResult ProxyError(UltraNetResultCode code, const UltraNetProxyConfig& proxy, const std::string& what) {
    return UltraNetResult::Error(code, "proxy " + UltraNet_DescribeProxy(proxy) + ": " + what);
}

UltraNetResult NoAnswer(const UltraNetProxyConfig& proxy) {
    return ProxyError(UltraNetResultCode::Timeout, proxy, "no answer to the handshake");
}

// The literal address bytes of `host`, when it is an IPv4 or IPv6 literal.
bool LiteralAddress(const std::string& host, std::string& outBytes, bool& outIpv6) {
    in_addr v4{};
    if (::inet_pton(AF_INET, host.c_str(), &v4) == 1) {
        outBytes.assign(reinterpret_cast<const char*>(&v4), 4);
        outIpv6 = false;
        return true;
    }
    in6_addr v6{};
    if (::inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
        outBytes.assign(reinterpret_cast<const char*>(&v6), 16);
        outIpv6 = true;
        return true;
    }
    return false;
}

// `host` looked up here, as address bytes (IPv4 only when `ipv4Only`).
bool LookUp(const std::string& host, bool ipv4Only, std::string& outBytes, bool& outIpv6) {
    addrinfo hints{};
    hints.ai_family = ipv4Only ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        if (res) ::freeaddrinfo(res);
        return false;
    }
    if (res->ai_family == AF_INET) {
        const auto* sa = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
        outBytes.assign(reinterpret_cast<const char*>(&sa->sin_addr), 4);
        outIpv6 = false;
    } else {
        const auto* sa = reinterpret_cast<const sockaddr_in6*>(res->ai_addr);
        outBytes.assign(reinterpret_cast<const char*>(&sa->sin6_addr), 16);
        outIpv6 = true;
    }
    ::freeaddrinfo(res);
    return true;
}

std::string PortBytes(int port) {
    std::string out;
    out += static_cast<char>((port >> 8) & 0xFF);
    out += static_cast<char>(port & 0xFF);
    return out;
}

// RFC 1928 (and RFC 1929 for the user name and password).
UltraNetResult Socks5Handshake(SocketFd fd, const UltraNetProxyConfig& proxy,
                               const std::string& host, int port) {
    const std::string& user = proxy.credentials.username;
    const std::string& pass = proxy.credentials.password;
    if (user.size() > 255 || pass.size() > 255)
        return ProxyError(UltraNetResultCode::InvalidState, proxy, "the user name or password is longer than SOCKS 5 allows");
    std::string greeting;
    greeting += '\x05';
    if (user.empty()) { greeting += '\x01'; greeting += '\x00'; }
    else { greeting += '\x02'; greeting += '\x00'; greeting += '\x02'; }
    if (!SendAll(fd, greeting)) return ProxyError(UltraNetResultCode::SendFailed, proxy, "the greeting could not be sent");
    unsigned char choice[2];
    if (!ReceiveExactly(fd, choice, 2)) return NoAnswer(proxy);
    if (choice[0] != 0x05) return ProxyError(UltraNetResultCode::ConnectionRefused, proxy, "not a SOCKS 5 proxy");
    if (choice[1] == 0xFF)
        return ProxyError(UltraNetResultCode::AuthenticationRequired, proxy,
                          user.empty() ? "it asks for a user name and password" : "it accepts no way of signing in offered");
    if (choice[1] == 0x02) {
        if (user.empty())
            return ProxyError(UltraNetResultCode::AuthenticationRequired, proxy, "it asks for a user name and password");
        std::string signIn;
        signIn += '\x01';
        signIn += static_cast<char>(user.size());
        signIn += user;
        signIn += static_cast<char>(pass.size());
        signIn += pass;
        if (!SendAll(fd, signIn)) return ProxyError(UltraNetResultCode::SendFailed, proxy, "the sign-in could not be sent");
        unsigned char status[2];
        if (!ReceiveExactly(fd, status, 2)) return NoAnswer(proxy);
        if (status[1] != 0x00)
            return ProxyError(UltraNetResultCode::AuthenticationFailed, proxy, "the user name or password was refused");
    } else if (choice[1] != 0x00) {
        return ProxyError(UltraNetResultCode::ConnectionRefused, proxy, "it chose a way of signing in that was not offered");
    }

    std::string request = std::string("\x05\x01\x00", 3);
    std::string bytes;
    bool ipv6 = false;
    if (LiteralAddress(host, bytes, ipv6)) {
        request += ipv6 ? '\x04' : '\x01';
        request += bytes;
    } else if (proxy.remoteDns) {
        if (host.size() > 255) return ProxyError(UltraNetResultCode::InvalidUrl, proxy, "the host name is too long");
        request += '\x03';
        request += static_cast<char>(host.size());
        request += host;
    } else if (LookUp(host, false, bytes, ipv6)) {
        request += ipv6 ? '\x04' : '\x01';
        request += bytes;
    } else {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound, host + " could not be looked up");
    }
    request += PortBytes(port);
    if (!SendAll(fd, request)) return ProxyError(UltraNetResultCode::SendFailed, proxy, "the request could not be sent");

    unsigned char reply[4];
    if (!ReceiveExactly(fd, reply, 4)) return NoAnswer(proxy);
    if (reply[0] != 0x05) return ProxyError(UltraNetResultCode::ConnectionRefused, proxy, "not a SOCKS 5 answer");
    if (reply[1] != 0x00) {
        static const char* const kReasons[] = {
            "succeeded", "general failure", "not allowed by its rules", "the network is unreachable",
            "the host is unreachable", "the connection was refused", "the time to live expired",
            "the command is not supported", "the address type is not supported"};
        const std::string why = reply[1] < 9 ? kReasons[reply[1]] : "error " + std::to_string(reply[1]);
        const UltraNetResultCode code = reply[1] == 4 ? UltraNetResultCode::HostNotFound
                                      : reply[1] == 2 ? UltraNetResultCode::AccessDenied
                                                      : UltraNetResultCode::ConnectionRefused;
        return ProxyError(code, proxy, host + ":" + std::to_string(port) + " - " + why);
    }
    // The address the proxy connected from, which nothing here needs.
    std::size_t rest = 0;
    if (reply[3] == 0x01) rest = 4 + 2;
    else if (reply[3] == 0x04) rest = 16 + 2;
    else if (reply[3] == 0x03) {
        unsigned char len = 0;
        if (!ReceiveExactly(fd, &len, 1)) return NoAnswer(proxy);
        rest = static_cast<std::size_t>(len) + 2;
    } else {
        return ProxyError(UltraNetResultCode::ConnectionRefused, proxy, "an answer it cannot read");
    }
    unsigned char skip[256 + 2];
    if (!ReceiveExactly(fd, skip, rest)) return NoAnswer(proxy);
    return UltraNetResult::Ok();
}

// SOCKS 4, and 4a when the proxy looks the name up.
UltraNetResult Socks4Handshake(SocketFd fd, const UltraNetProxyConfig& proxy,
                               const std::string& host, int port) {
    std::string bytes;
    bool ipv6 = false;
    bool byName = false;
    if (LiteralAddress(host, bytes, ipv6)) {
        if (ipv6) return ProxyError(UltraNetResultCode::Unsupported, proxy, "SOCKS 4 cannot reach an IPv6 address");
    } else if (proxy.remoteDns) {
        byName = true;
        bytes = std::string("\x00\x00\x00\x01", 4);   // 4a: "the name follows"
    } else if (!LookUp(host, true, bytes, ipv6)) {
        return UltraNetResult::Error(UltraNetResultCode::HostNotFound, host + " could not be looked up");
    }
    std::string request = std::string("\x04\x01", 2) + PortBytes(port) + bytes;
    request += proxy.credentials.username;
    request += '\0';
    if (byName) {
        request += host;
        request += '\0';
    }
    if (!SendAll(fd, request)) return ProxyError(UltraNetResultCode::SendFailed, proxy, "the request could not be sent");
    unsigned char reply[8];
    if (!ReceiveExactly(fd, reply, 8)) return NoAnswer(proxy);
    if (reply[1] != 0x5A)
        return ProxyError(reply[1] == 0x5C || reply[1] == 0x5D ? UltraNetResultCode::AuthenticationFailed
                                                               : UltraNetResultCode::ConnectionRefused,
                          proxy, host + ":" + std::to_string(port) + " was refused");
    return UltraNetResult::Ok();
}

// CONNECT host:port through an HTTP proxy. The answer is read a byte at a
// time up to its blank line, so nothing the server sends after it is lost.
UltraNetResult HttpConnectHandshake(SocketFd fd, const UltraNetProxyConfig& proxy,
                                    const std::string& host, int port) {
    const std::string target = (host.find(':') != std::string::npos ? "[" + host + "]" : host) +
                               ":" + std::to_string(port);
    std::string request = "CONNECT " + target + " HTTP/1.1\r\nHost: " + target + "\r\n";
    if (!proxy.credentials.username.empty()) {
        const std::string pair = proxy.credentials.username + ":" + proxy.credentials.password;
        request += "Proxy-Authorization: Basic " +
                   UltraNet_Base64Encode(std::vector<uint8_t>(pair.begin(), pair.end()), false) + "\r\n";
    }
    request += "\r\n";
    if (!SendAll(fd, request)) return ProxyError(UltraNetResultCode::SendFailed, proxy, "the request could not be sent");
    std::string answer;
    while (answer.size() < 16384 &&
           (answer.size() < 4 || answer.compare(answer.size() - 4, 4, "\r\n\r\n") != 0)) {
        unsigned char c = 0;
        if (!ReceiveExactly(fd, &c, 1)) return NoAnswer(proxy);
        answer += static_cast<char>(c);
    }
    const std::string statusLine = answer.substr(0, answer.find("\r\n"));
    int status = 0;
    const std::size_t space = statusLine.find(' ');
    if (statusLine.rfind("HTTP/", 0) == 0 && space != std::string::npos) {
        for (std::size_t i = space + 1; i < statusLine.size() && std::isdigit(static_cast<unsigned char>(statusLine[i])); ++i)
            status = status * 10 + (statusLine[i] - '0');
    }
    if (status >= 200 && status < 300) return UltraNetResult::Ok();
    if (status == 407)
        return ProxyError(proxy.credentials.username.empty() ? UltraNetResultCode::AuthenticationRequired
                                                             : UltraNetResultCode::AuthenticationFailed,
                          proxy, "it asks for a user name and password (407)");
    return ProxyError(UltraNetResultCode::ConnectionRefused, proxy,
                      statusLine.empty() ? std::string("not an HTTP proxy") : "it answered \"" + statusLine + "\"");
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
UltraNetResult UltraNet_TcpOpen(const std::string& host, int port,
                                const UltraNetSocketOptions& opt, UltraNetHandle& outHandle) {
    outHandle = UltraNetInvalidHandle;
    // Through a proxy only when asked: a raw socket is mostly a device or a
    // peer on the local network.
    UltraNetProxyConfig proxy;
    if (opt.proxy.IsEnabled() || opt.useProxySetting) {
        const std::string authority = host.find(':') != std::string::npos ? "[" + host + "]" : host;
        proxy = UltraNet_ResolveProxy("tcp://" + authority + ":" + std::to_string(port), opt.proxy);
    }
    const bool viaProxy = proxy.IsEnabled();
    if (viaProxy && proxy.type == UltraNetProxyType::Https)
        return ProxyError(UltraNetResultCode::Unsupported, proxy, "a proxy reached over TLS cannot carry a raw connection");

    const std::string connectHost = viaProxy ? proxy.host : host;
    const int connectPort = viaProxy ? proxy.port : port;
    sockaddr_storage ss{};
    socklen_t        sl = 0;
    bool             v6 = false;
    auto r = Resolve(connectHost, connectPort, opt.useIpv6 && !viaProxy, ss, sl, v6);
    if (!r) return viaProxy ? ProxyError(UltraNetResultCode::HostNotFound, proxy, r.message) : r;

    SocketFd fd = ::socket(ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidFd)
        return UltraNetResult::Error(UltraNetResultCode::Unknown, SocketErrorString(LastSocketError()));
    ApplyOptions(fd, opt);
    if (viaProxy) SetTimeouts(fd, opt.connectTimeoutMs, opt.connectTimeoutMs);
    else ApplyTimeout(fd, opt.sendTimeoutMs, opt.recvTimeoutMs);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&ss), sl) != 0) {
        const std::string why = SocketErrorString(LastSocketError());
        CloseFd(fd);
        return viaProxy ? ProxyError(UltraNetResultCode::ConnectionRefused, proxy, why)
                        : UltraNetResult::Error(UltraNetResultCode::ConnectionRefused,
                                                host + ":" + std::to_string(port) + ": " + why);
    }
    if (viaProxy) {
        UltraNetResult shaken =
            proxy.type == UltraNetProxyType::Socks5 ? Socks5Handshake(fd, proxy, host, port)
          : proxy.type == UltraNetProxyType::Socks4 ? Socks4Handshake(fd, proxy, host, port)
                                                    : HttpConnectHandshake(fd, proxy, host, port);
        if (!shaken) {
            CloseFd(fd);
            return shaken;
        }
        // The connection's own limits from here on.
        SetTimeouts(fd, opt.sendTimeoutMs, opt.recvTimeoutMs);
        // The address family is the target's as far as anyone above knows.
        std::string bytes;
        bool literalV6 = false;
        v6 = LiteralAddress(host, bytes, literalV6) && literalV6;
    }
    auto e = std::make_shared<Entry>();
    e->fd     = fd;
    e->kind   = Kind::TcpStream;
    e->isIpv6 = v6;
    outHandle = Register(std::move(e));
    return UltraNetResult::Ok();
}

UltraNetHandle UltraNet_TcpConnect(const std::string& host, int port,
                                   const UltraNetSocketOptions& opt) {
    UltraNetHandle handle = UltraNetInvalidHandle;
    UltraNet_TcpOpen(host, port, opt, handle);
    return handle;
}

UltraNetHandle UltraNet_TcpListen(int port,
                                  const UltraNetSocketOptions& opt) {
    const int family = opt.useIpv6 ? AF_INET6 : AF_INET;
    SocketFd fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidFd) return UltraNetInvalidHandle;
    ApplyOptions(fd, opt);

    sockaddr_storage ss{};
    socklen_t sl = 0;
    if (!BuildBindAddress(opt, port, ss, sl)) {
        CloseFd(fd);
        return UltraNetInvalidHandle;
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
                                  UltraNetEndpoint& outRemote,
                                  int timeoutMs) {
    auto e = Find(listener);
    if (!e || e->kind != Kind::TcpListener) return UltraNetInvalidHandle;

    if (timeoutMs > 0) {
        // select() on the listener fd: SO_RCVTIMEO does not apply to
        // accept() on Winsock, so this is the one portable way to bound it.
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(e->fd, &rd);
        timeval tv{};
        tv.tv_sec  = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int ready = ::select(static_cast<int>(e->fd) + 1, &rd, nullptr,
                             nullptr, &tv);
        if (ready <= 0) return UltraNetInvalidHandle;
    }

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

    if (e->tlsCtx && e->tlsWrite) {
        int n = e->tlsWrite(e->tlsCtx,
                            reinterpret_cast<const char*>(data.data()),
                            data.size());
        if (n < 0) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         "TLS write failed");
        }
        outBytesSent = n;
        return UltraNetResult::Ok();
    }

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

    if (e->tlsCtx && e->tlsRead) {
        int n = e->tlsRead(e->tlsCtx,
                           reinterpret_cast<char*>(outData.data()),
                           outData.size(), timeoutMs);
        if (n < 0) {
            outData.clear();
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         "TLS read failed");
        }
        outData.resize(static_cast<std::size_t>(n));
        return UltraNetResult::Ok();
    }

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

    if (localPort > 0 || !opt.bindAddress.empty()) {
        sockaddr_storage ss{};
        socklen_t sl = 0;
        if (!BuildBindAddress(opt, localPort, ss, sl)) {
            CloseFd(fd);
            return UltraNetInvalidHandle;
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
    // Tear TLS down first so the close-notify alert is written before the
    // raw fd disappears.
    if (e->tlsCtx && e->tlsClose) {
        e->tlsClose(e->tlsCtx);
        e->tlsCtx   = nullptr;
        e->tlsRead  = nullptr;
        e->tlsWrite = nullptr;
        e->tlsClose = nullptr;
    }
    CloseFd(e->fd);
    Unregister(handle);
    return UltraNetResult::Ok();
}

std::intptr_t UltraNet_SocketDetach(UltraNetHandle handle) {
    auto e = Find(handle);
    if (!e || e->kind != Kind::TcpStream || e->tlsCtx) return -1;
    Unregister(handle);
    return static_cast<std::intptr_t>(e->fd);
}

UltraNetResult UltraNet_SocketLocalEndpoint(UltraNetHandle handle,
                                            UltraNetEndpoint& outLocal) {
    auto e = Find(handle);
    if (!e) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such socket");
    }
    sockaddr_storage ss{};
    socklen_t sl = sizeof ss;
    if (::getsockname(e->fd, reinterpret_cast<sockaddr*>(&ss), &sl) != 0 ||
        !EndpointFromSockaddr(reinterpret_cast<sockaddr*>(&ss), sl, outLocal)) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                     SocketErrorString(LastSocketError()));
    }
    return UltraNetResult::Ok();
}

// ============================================================================
// Internal hooks for the TLS implementation. Lives in this TU because it
// needs access to the private g_sockets registry and Entry.
// ============================================================================
namespace ultranet_internal {

std::intptr_t GetTcpFd(UltraNetHandle handle) {
    auto e = Find(handle);
    if (!e || e->kind != Kind::TcpStream) return -1;
    return static_cast<std::intptr_t>(e->fd);
}

bool AttachTlsToSocket(UltraNetHandle handle,
                       void* tlsCtx,
                       TlsReadFn  readFn,
                       TlsWriteFn writeFn,
                       TlsCloseFn closeFn) {
    auto e = Find(handle);
    if (!e || e->kind != Kind::TcpStream) return false;
    e->tlsCtx   = tlsCtx;
    e->tlsRead  = readFn;
    e->tlsWrite = writeFn;
    e->tlsClose = closeFn;
    return true;
}

bool IsTlsAttached(UltraNetHandle handle) {
    auto e = Find(handle);
    return e && e->tlsCtx != nullptr;
}

void* GetTlsCtx(UltraNetHandle handle) {
    auto e = Find(handle);
    return e ? e->tlsCtx : nullptr;
}

} // namespace ultranet_internal

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
