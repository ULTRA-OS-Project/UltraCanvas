// UltraCanvas/Plugins/UltraNet/rtp/RtpPlugin.cpp
// RTP receiver plug-in (RFC 3550). No external library — RTP is a fixed
// 12-byte header on top of UDP; ~60 lines of parsing + BSD sockets is
// all we need. Same call as gRPC / SIP: skip the framework, do the
// protocol directly.
//
// v0.1 scope: RTP RECEIVER.
//   OpenStream(rtp://[iface][:port])  → binds UDP socket on `port`
//                                        (defaults 5004; iface defaults
//                                        to 0.0.0.0 = INADDR_ANY).
//   ReadFrame(handle, outFrame)       → recvfrom one UDP datagram, parse
//                                        the RTP header, return payload
//                                        bytes only.
//
// Sending RTP (as a source generating packets) is a future extension.
// RTCP (control) is out of scope for v0.1 — apps that need it get their
// own UDP socket on the paired odd port and do the RTCP framing.
//
// The RTP header per RFC 3550 § 5.1:
//   V=2 P X CC | M PT | sequence number (16)     4 bytes
//   timestamp (32)                                4 bytes
//   SSRC (32)                                     4 bytes
//   CSRC list (0..15 × 4 bytes)                   CC × 4
//   optional extension                            variable
//   payload                                       rest
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
// NB: NOT using UltraNet_ParseUrl here — libcurl's URL parser rejects
// non-standard schemes by default (rtp:// / sip:// / coap:// etc. all
// return CURLUE_UNSUPPORTED_SCHEME). rtp:// URLs are trivial enough
// that a small string-based parser below covers the shape we need.

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using SocketFd = SOCKET;
  static constexpr SocketFd kInvalidFd = INVALID_SOCKET;
  static void CloseFd(SocketFd fd) { closesocket(fd); }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  using SocketFd = int;
  static constexpr SocketFd kInvalidFd = -1;
  static void CloseFd(SocketFd fd) { ::close(fd); }
#endif

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

#if defined(_WIN32) || defined(_WIN64)
struct WsaInit {
    WsaInit()  { WSADATA d{}; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { WSACleanup(); }
};
WsaInit g_wsaInit;
#endif

// =====================================================================
// Session — one bound UDP socket per stream.
// =====================================================================
struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    SocketFd       fd     = kInvalidFd;
    int            port   = 0;

    // Last-seen RTP header fields — apps can read via GetTransferStats
    // in a future extension; for now they're just diagnostic bookkeeping.
    std::atomic<uint16_t> lastSequence{0};
    std::atomic<uint32_t> lastTimestamp{0};
    std::atomic<uint32_t> lastSsrc{0};
    std::atomic<uint8_t>  lastPayloadType{0};

    ~Session() {
        if (fd != kInvalidFd) CloseFd(fd);
    }
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

void ApplyRecvTimeout(SocketFd fd, int ms) {
    if (ms <= 0) return;
#if defined(_WIN32) || defined(_WIN64)
    DWORD t = static_cast<DWORD>(ms);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&t), sizeof t);
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
}

// =====================================================================
// RTP header parse (RFC 3550 § 5.1). Returns payload start offset within
// the datagram, or -1 if the packet isn't valid RTP v2.
// =====================================================================
struct RtpHeader {
    uint8_t  version = 0;
    bool     padding = false;
    bool     extension = false;
    uint8_t  csrcCount = 0;
    bool     marker = false;
    uint8_t  payloadType = 0;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
};

int ParseRtpHeader(const uint8_t* buf, std::size_t len, RtpHeader& out) {
    if (len < 12) return -1;
    out.version     = (buf[0] >> 6) & 0x03;
    out.padding     = (buf[0] & 0x20) != 0;
    out.extension   = (buf[0] & 0x10) != 0;
    out.csrcCount   = buf[0] & 0x0f;
    out.marker      = (buf[1] & 0x80) != 0;
    out.payloadType = buf[1] & 0x7f;
    out.sequence    = static_cast<uint16_t>(buf[2]) << 8 | buf[3];
    out.timestamp   = static_cast<uint32_t>(buf[4]) << 24 |
                      static_cast<uint32_t>(buf[5]) << 16 |
                      static_cast<uint32_t>(buf[6]) << 8  |
                      static_cast<uint32_t>(buf[7]);
    out.ssrc        = static_cast<uint32_t>(buf[8])  << 24 |
                      static_cast<uint32_t>(buf[9])  << 16 |
                      static_cast<uint32_t>(buf[10]) << 8  |
                      static_cast<uint32_t>(buf[11]);
    if (out.version != 2) return -1;

    std::size_t offset = 12 + std::size_t{out.csrcCount} * 4;
    if (offset > len) return -1;

    // Skip the optional extension header (RFC 3550 § 5.3.1): 2 bytes of
    // profile-specific data + 2 bytes of extension length in 32-bit
    // words + the extension data itself.
    if (out.extension) {
        if (offset + 4 > len) return -1;
        const uint16_t extWords = static_cast<uint16_t>(buf[offset + 2]) << 8 |
                                   buf[offset + 3];
        offset += 4 + static_cast<std::size_t>(extWords) * 4;
        if (offset > len) return -1;
    }

    // Strip trailing padding when the P flag is set. RFC 3550 § 5.1: the
    // last byte of the payload contains the padding count.
    std::size_t payloadEnd = len;
    if (out.padding && len > offset) {
        const uint8_t padCount = buf[len - 1];
        if (padCount > 0 && padCount <= len - offset) {
            payloadEnd = len - padCount;
        }
    }
    return static_cast<int>(payloadEnd) - static_cast<int>(offset) >= 0
        ? static_cast<int>(offset)
        : -1;
}

// =====================================================================
// Plug-in
// =====================================================================
class RtpPlugin : public IStreamingProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-RTP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"rtp"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle OpenStream(const std::string& url,
                              const UltraNetStreamOptions& options) override {
        // Minimal rtp:// URL parse — rtp://[host][:port] with no path/query.
        constexpr const char* kPrefix = "rtp://";
        constexpr std::size_t kPrefixLen = 6;
        if (url.rfind(kPrefix, 0) != 0) return UltraNetInvalidHandle;

        std::string authority = url.substr(kPrefixLen);
        // Strip any accidental trailing path — we don't use it.
        const std::size_t slash = authority.find('/');
        if (slash != std::string::npos) authority.resize(slash);

        std::string host;
        int port = 5004;   // default: VoIP audio convention
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            host = authority.substr(0, colon);
            const std::string portStr = authority.substr(colon + 1);
            if (!portStr.empty()) {
                port = std::atoi(portStr.c_str());
                if (port <= 0 || port > 65535) return UltraNetInvalidHandle;
            }
        } else {
            host = authority;
        }

        auto s = std::make_shared<Session>();
        s->port = port;
        s->fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s->fd == kInvalidFd) return UltraNetInvalidHandle;

        // Allow port reuse — multiple readers on the same RTP port is a
        // legitimate use case (e.g. multicast receivers).
        int one = 1;
        ::setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof one);

        sockaddr_in bind4{};
        bind4.sin_family = AF_INET;
        bind4.sin_port   = htons(static_cast<uint16_t>(port));
        // Bind interface: URL host if it's an IPv4 address literal, else
        // INADDR_ANY. Hostname resolution isn't useful here — receivers
        // bind, they don't connect.
        if (host.empty() ||
            ::inet_pton(AF_INET, host.c_str(), &bind4.sin_addr) != 1) {
            bind4.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        if (::bind(s->fd, reinterpret_cast<sockaddr*>(&bind4), sizeof bind4) != 0) {
            return UltraNetInvalidHandle;
        }
        if (options.connectTimeoutMs > 0) {
            ApplyRecvTimeout(s->fd, options.connectTimeoutMs);
        }

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions[h] = s;
        }
        return h;
    }

    UltraNetResult ReadFrame(UltraNetHandle handle,
                             std::vector<uint8_t>& outFrame) override {
        outFrame.clear();
        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(
            UltraNetResultCode::InvalidHandle, "no such RTP session");

        // Datagram cap of 64 KB is the theoretical UDP max; typical RTP
        // payloads are 200-1500 bytes so this is generous.
        std::vector<uint8_t> buf(65536);
        sockaddr_storage src{};
        socklen_t srcLen = sizeof src;
#if defined(_WIN32) || defined(_WIN64)
        int n = ::recvfrom(s->fd, reinterpret_cast<char*>(buf.data()),
                           static_cast<int>(buf.size()), 0,
                           reinterpret_cast<sockaddr*>(&src), &srcLen);
#else
        ssize_t n = ::recvfrom(s->fd, buf.data(), buf.size(), 0,
                               reinterpret_cast<sockaddr*>(&src), &srcLen);
#endif
        if (n < 0) {
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         "recvfrom failed (timeout?)");
        }
        if (n < 12) {
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         "packet too small to be RTP");
        }

        RtpHeader hdr;
        const int payloadOffset = ParseRtpHeader(buf.data(),
                                                  static_cast<std::size_t>(n),
                                                  hdr);
        if (payloadOffset < 0) {
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         "malformed RTP packet");
        }
        s->lastSequence.store(hdr.sequence,     std::memory_order_relaxed);
        s->lastTimestamp.store(hdr.timestamp,   std::memory_order_relaxed);
        s->lastSsrc.store(hdr.ssrc,             std::memory_order_relaxed);
        s->lastPayloadType.store(hdr.payloadType, std::memory_order_relaxed);

        outFrame.assign(buf.begin() + payloadOffset,
                        buf.begin() + static_cast<std::ptrdiff_t>(n));
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<RtpPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<RtpPlugin>());
}
#endif
