// UltraCanvas/Plugins/UltraNet/sip/SipPlugin.cpp
// SIP plug-in — UDP-native (RFC 3261), NOT pjsip/sofia-sip. Same
// approach as our gRPC plug-in: skip the heavyweight framework, do the
// minimal protocol work directly. Buys us cross-platform simplicity at
// the cost of feature depth — this is not a full softphone.
//
// v0.1 scope: OPTIONS probing + MESSAGE send. Enough to answer "is
// there a SIP endpoint reachable at this address?" and to shove a
// small text message through the SIP MESSAGE method (RFC 3428). A
// production softphone (INVITE, dialog state, SDP, RTP, digest auth
// with WWW-Authenticate challenge/response, TLS transport,
// registrar-tracked bindings) needs pjsip or sofia and would be its
// own workstream.
//
// Interface: IRemoteAccessPlugin — the closest fit in the existing
// spec-defined interface set:
//   OpenShell(sip://server[:port])   → sends OPTIONS; returns handle if the
//                                       server answered with a 2xx status.
//   ExecuteCommand(handle, message)  → sends a SIP MESSAGE with the given
//                                       text body; the response status line
//                                       and reason land in outStdOut.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

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
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  using SocketFd = int;
  static constexpr SocketFd kInvalidFd = -1;
  static void CloseFd(SocketFd fd) { ::close(fd); }
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
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
// Session — one UDP socket per session, remembers server + our Call-ID.
// Each ExecuteCommand call gets a fresh CSeq.
// =====================================================================
struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    SocketFd fd = kInvalidFd;
    std::string serverHost;
    int serverPort = 5060;
    sockaddr_storage serverAddr{};
    socklen_t        serverAddrLen = 0;
    std::string localAddr;
    int localPort = 0;
    std::string callId;
    std::string fromTag;
    std::atomic<int> cseq{1};
    int timeoutMs = 3000;

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

// =====================================================================
// Random-ish tokens per RFC 3261 for uniqueness within a UA.
// =====================================================================
std::string RandomHex(std::size_t nibbles) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::string s;
    s.reserve(nibbles);
    static const char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < nibbles; ++i) {
        s.push_back(kHex[rng() & 0xf]);
    }
    return s;
}

// =====================================================================
// Socket + address helpers
// =====================================================================
bool Resolve(const std::string& host, int port,
             sockaddr_storage& out, socklen_t& outLen) {
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    const std::string ps = std::to_string(port);
    if (::getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) {
        if (res) ::freeaddrinfo(res);
        return false;
    }
    std::memcpy(&out, res->ai_addr, res->ai_addrlen);
    outLen = static_cast<socklen_t>(res->ai_addrlen);
    ::freeaddrinfo(res);
    return true;
}

bool BindEphemeral(SocketFd fd, std::string& outAddr, int& outPort) {
    sockaddr_in bind4{};
    bind4.sin_family      = AF_INET;
    bind4.sin_addr.s_addr = htonl(INADDR_ANY);
    bind4.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&bind4), sizeof bind4) != 0) {
        return false;
    }
    sockaddr_in name{};
    socklen_t nameLen = sizeof name;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&name), &nameLen) != 0) {
        return false;
    }
    outPort = ntohs(name.sin_port);
    // Best-effort local IP — for the SIP Via header. Use gethostname → getaddrinfo
    // so the value is routable; on failure fall back to 127.0.0.1.
    char host[256]{};
    if (::gethostname(host, sizeof host - 1) == 0) {
        addrinfo hints{}; hints.ai_family = AF_INET;
        addrinfo* res = nullptr;
        if (::getaddrinfo(host, nullptr, &hints, &res) == 0 && res) {
            char buf[INET_ADDRSTRLEN]{};
            auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
            ::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf);
            outAddr = buf;
            ::freeaddrinfo(res);
            return true;
        }
    }
    outAddr = "127.0.0.1";
    return true;
}

void ApplyRecvTimeout(SocketFd fd, int ms) {
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
// SIP request builder + response status-line parser.
// =====================================================================
std::string BuildRequest(const Session& s,
                         const std::string& method,
                         const std::string& targetUri,
                         const std::string& body,
                         int cseq) {
    std::ostringstream os;
    const std::string branch  = "z9hG4bK-" + RandomHex(20);
    const std::string userTag = "ultranet";
    os << method << ' ' << targetUri << " SIP/2.0\r\n"
       << "Via: SIP/2.0/UDP " << s.localAddr << ':' << s.localPort
       << ";branch=" << branch << ";rport\r\n"
       << "Max-Forwards: 70\r\n"
       << "From: \"UltraNet\" <sip:" << userTag << '@' << s.localAddr << ">;tag="
       << s.fromTag << "\r\n"
       << "To: <" << targetUri << ">\r\n"
       << "Call-ID: " << s.callId << "\r\n"
       << "CSeq: " << cseq << ' ' << method << "\r\n"
       << "Contact: <sip:" << userTag << '@' << s.localAddr << ':'
       << s.localPort << ">\r\n"
       << "User-Agent: UltraNet-SIP/0.1\r\n";
    if (!body.empty()) {
        os << "Content-Type: text/plain\r\n";
    }
    os << "Content-Length: " << body.size() << "\r\n\r\n";
    if (!body.empty()) os << body;
    return os.str();
}

struct SipResponse {
    int status = 0;
    std::string reason;
    std::string body;
};

bool ParseResponse(const std::string& wire, SipResponse& r) {
    if (wire.rfind("SIP/2.0 ", 0) != 0) return false;
    std::size_t sp1 = wire.find(' ', 8);
    if (sp1 == std::string::npos) return false;
    r.status = std::atoi(wire.c_str() + 8);
    std::size_t eol = wire.find("\r\n", sp1);
    if (eol == std::string::npos) return false;
    r.reason = wire.substr(sp1 + 1, eol - sp1 - 1);
    // Body starts after \r\n\r\n if any.
    const std::size_t bodyStart = wire.find("\r\n\r\n");
    if (bodyStart != std::string::npos && bodyStart + 4 < wire.size()) {
        r.body = wire.substr(bodyStart + 4);
    }
    return true;
}

// Send a request, wait for one response, return response on success.
bool Roundtrip(Session& s, const std::string& method,
               const std::string& targetUri,
               const std::string& body,
               SipResponse& resp) {
    const int cseq = s.cseq.fetch_add(1, std::memory_order_relaxed);
    const std::string req = BuildRequest(s, method, targetUri, body, cseq);
    ApplyRecvTimeout(s.fd, s.timeoutMs);
#if defined(_WIN32) || defined(_WIN64)
    int sent = ::sendto(s.fd, req.data(), static_cast<int>(req.size()), 0,
                        reinterpret_cast<sockaddr*>(&s.serverAddr),
                        s.serverAddrLen);
#else
    ssize_t sent = ::sendto(s.fd, req.data(), req.size(), 0,
                            reinterpret_cast<sockaddr*>(&s.serverAddr),
                            s.serverAddrLen);
#endif
    if (sent < 0) return false;

    char buf[4096];
#if defined(_WIN32) || defined(_WIN64)
    int n = ::recv(s.fd, buf, sizeof buf, 0);
#else
    ssize_t n = ::recv(s.fd, buf, sizeof buf, 0);
#endif
    if (n <= 0) return false;
    std::string wire(buf, static_cast<std::size_t>(n));
    return ParseResponse(wire, resp);
}

// =====================================================================
// Plug-in
// =====================================================================
class SipPlugin : public IRemoteAccessPlugin {
public:
    std::string GetName() const override { return "UltraNet-SIP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"sip"};   // "sips" (SIP over TLS) is a future extension
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle OpenShell(const std::string& url,
                             const UltraNetRemoteOptions& options) override {
        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c) || c.scheme != "sip") {
            return UltraNetInvalidHandle;
        }
        if (c.host.empty()) return UltraNetInvalidHandle;
        const int port = c.port > 0 ? c.port : 5060;

        auto s = std::make_shared<Session>();
        s->serverHost = c.host;
        s->serverPort = port;
        s->callId  = RandomHex(24) + "@ultranet.local";
        s->fromTag = RandomHex(8);
        if (options.connectTimeoutMs > 0) s->timeoutMs = options.connectTimeoutMs;

        if (!Resolve(c.host, port, s->serverAddr, s->serverAddrLen)) {
            return UltraNetInvalidHandle;
        }
        s->fd = ::socket(s->serverAddr.ss_family, SOCK_DGRAM, IPPROTO_UDP);
        if (s->fd == kInvalidFd) return UltraNetInvalidHandle;
        if (!BindEphemeral(s->fd, s->localAddr, s->localPort)) {
            return UltraNetInvalidHandle;
        }

        // Probe with OPTIONS. Any 2xx (200 OK) status = success.
        SipResponse resp;
        const std::string targetUri = "sip:" + c.host + ':' + std::to_string(port);
        if (!Roundtrip(*s, "OPTIONS", targetUri, "", resp)) {
            return UltraNetInvalidHandle;
        }
        if (resp.status < 200 || resp.status >= 300) {
            return UltraNetInvalidHandle;
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

    UltraNetResult ExecuteCommand(UltraNetHandle handle,
                                  const std::string& command,
                                  std::string& outStdOut,
                                  std::string& outStdErr,
                                  int& outExitCode) override {
        outStdOut.clear();
        outStdErr.clear();
        outExitCode = -1;

        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(
            UltraNetResultCode::InvalidHandle, "no such SIP session");

        SipResponse resp;
        const std::string targetUri = "sip:" + s->serverHost + ':' +
                                      std::to_string(s->serverPort);
        if (!Roundtrip(*s, "MESSAGE", targetUri, command, resp)) {
            return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                         "SIP MESSAGE roundtrip failed");
        }
        std::ostringstream os;
        os << "SIP/2.0 " << resp.status << ' ' << resp.reason;
        if (!resp.body.empty()) os << "\r\n\r\n" << resp.body;
        outStdOut = os.str();
        outExitCode = (resp.status >= 200 && resp.status < 300) ? 0 : resp.status;
        return outExitCode == 0
            ? UltraNetResult::Ok()
            : UltraNetResult::Error(UltraNetResultCode::HttpError, resp.reason);
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<SipPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<SipPlugin>());
}
