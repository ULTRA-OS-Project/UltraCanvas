// Tests/UltraNet/ApiStatus/ProbeServer.cpp
// Implementation of the in-process HTTP/1.1 + WebSocket origin used by the
// UltraNet API status probes. See ProbeServer.h for the route table.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ProbeServer.h"

#include <UltraNet/UltraNetMime.h>     // UltraNet_Base64Encode (WS accept key)
#include <UltraNet/UltraNetSocket.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#endif

namespace ultranet_apistatus {

namespace {

constexpr int kPortRangeStart = 18700;
constexpr int kPortRangeEnd   = 18799;
constexpr int kReadTimeoutMs  = 5000;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// ---------------------------------------------------------------------------
// SHA-1 (RFC 3174). Needed only for the WebSocket Sec-WebSocket-Accept value.
// Vendoring 60 lines here keeps the status tool free of an OpenSSL dependency
// it would otherwise need purely for the handshake.
// ---------------------------------------------------------------------------
std::array<uint8_t, 20> Sha1(const std::string& input) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    std::string msg = input;
    const uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bitLength >> (i * 8)) & 0xff));
    }

    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(msg.data() + chunk + i * 4);
            w[i] = (static_cast<uint32_t>(p[0]) << 24) |
                   (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8)  |
                    static_cast<uint32_t>(p[3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                      k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);    k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                      k = 0xCA62C1D6u; }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xff);
        out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xff);
        out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8)  & 0xff);
        out[i * 4 + 3] = static_cast<uint8_t>( h[i]        & 0xff);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Buffered reader/writer over an UltraNet TCP handle. UltraNet_TcpReceive
// hands back whatever one recv() produced and UltraNet_TcpSend performs one
// send(), so both need framing/looping on top.
// ---------------------------------------------------------------------------
class Conn {
public:
    explicit Conn(UltraNetHandle h) : handle_(h) {}

    bool Eof() const { return eof_; }

    bool Fill(int timeoutMs) {
        if (eof_) return false;
        std::vector<uint8_t> chunk;
        UltraNetResult r = UltraNet_TcpReceive(handle_, chunk, 65536, timeoutMs);
        if (!r) { eof_ = true; return false; }
        if (chunk.empty()) { eof_ = true; return false; }   // orderly shutdown
        buffer_.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
        return true;
    }

    // Reads up to and including `delim`; `out` excludes the delimiter.
    bool ReadUntil(const std::string& delim, std::string& out, int timeoutMs) {
        for (;;) {
            const auto pos = buffer_.find(delim);
            if (pos != std::string::npos) {
                out = buffer_.substr(0, pos);
                buffer_.erase(0, pos + delim.size());
                return true;
            }
            if (!Fill(timeoutMs)) return false;
        }
    }

    bool ReadExact(std::size_t n, std::string& out, int timeoutMs) {
        while (buffer_.size() < n) {
            if (!Fill(timeoutMs)) return false;
        }
        out = buffer_.substr(0, n);
        buffer_.erase(0, n);
        return true;
    }

    bool WriteAll(const std::string& data) {
        std::size_t offset = 0;
        while (offset < data.size()) {
            std::vector<uint8_t> chunk(data.begin() + static_cast<long>(offset),
                                       data.end());
            int sent = 0;
            UltraNetResult r = UltraNet_TcpSend(handle_, chunk, sent);
            if (!r || sent <= 0) return false;
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    bool WriteAll(const std::vector<uint8_t>& data) {
        return WriteAll(std::string(data.begin(), data.end()));
    }

private:
    UltraNetHandle handle_;
    std::string    buffer_;
    bool           eof_ = false;
};

struct Request {
    std::string method;
    std::string target;
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;  // names lowercased
    std::string body;

    std::string Header(const std::string& name) const {
        const std::string key = ToLower(name);
        for (const auto& h : headers) {
            if (h.first == key) return h.second;
        }
        return {};
    }
    bool HasHeader(const std::string& name) const {
        const std::string key = ToLower(name);
        for (const auto& h : headers) {
            if (h.first == key) return true;
        }
        return false;
    }
};

std::string StatusReason(int code) {
    switch (code) {
        case 100: return "Continue";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 418: return "I'm a teapot";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Status";
    }
}

std::string BuildResponse(int code,
                          const std::string& contentType,
                          const std::string& body,
                          const std::vector<std::string>& extraHeaders,
                          bool keepAlive,
                          bool includeBody) {
    std::string out = "HTTP/1.1 " + std::to_string(code) + ' ' +
                      StatusReason(code) + "\r\n";
    if (!contentType.empty()) out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    for (const std::string& h : extraHeaders) out += h + "\r\n";
    out += std::string("Connection: ") + (keepAlive ? "keep-alive" : "close") + "\r\n";
    out += "\r\n";
    // HEAD responses advertise the length but carry no body (RFC 9110 9.3.2).
    if (includeBody) out += body;
    return out;
}

// Reads a chunked request body (libcurl uses it whenever the payload size
// isn't known up front).
bool ReadChunkedBody(Conn& conn, std::string& out) {
    for (;;) {
        std::string sizeLine;
        if (!conn.ReadUntil("\r\n", sizeLine, kReadTimeoutMs)) return false;
        const std::size_t semi = sizeLine.find(';');
        if (semi != std::string::npos) sizeLine.erase(semi);
        std::size_t size = 0;
        try {
            size = static_cast<std::size_t>(std::stoul(Trim(sizeLine), nullptr, 16));
        } catch (...) {
            return false;
        }
        if (size == 0) {
            std::string trailer;
            // Trailing CRLF (and any trailer section) up to the blank line.
            conn.ReadUntil("\r\n", trailer, kReadTimeoutMs);
            return true;
        }
        std::string chunk;
        if (!conn.ReadExact(size, chunk, kReadTimeoutMs)) return false;
        out += chunk;
        std::string crlf;
        if (!conn.ReadExact(2, crlf, kReadTimeoutMs)) return false;
    }
}

// ---------------------------------------------------------------------------
// WebSocket framing
// ---------------------------------------------------------------------------
constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-5AB0DC85B11F";

std::string WebSocketAccept(const std::string& key) {
    const std::array<uint8_t, 20> digest = Sha1(key + kWsGuid);
    return UltraNet_Base64Encode(std::vector<uint8_t>(digest.begin(), digest.end()),
                                 /*wrap76Cols=*/false);
}

std::string BuildWsFrame(uint8_t opcode, const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));   // FIN + opcode
    const std::size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 0xffff) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xff));
        frame.push_back(static_cast<char>(len & 0xff));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((static_cast<uint64_t>(len) >> (i * 8)) & 0xff));
        }
    }
    frame += payload;   // server -> client frames are never masked
    return frame;
}

struct WsFrame {
    uint8_t     opcode = 0;
    bool        fin = true;
    std::string payload;
};

bool ReadWsFrame(Conn& conn, WsFrame& out, int timeoutMs) {
    std::string head;
    if (!conn.ReadExact(2, head, timeoutMs)) return false;
    const uint8_t b0 = static_cast<uint8_t>(head[0]);
    const uint8_t b1 = static_cast<uint8_t>(head[1]);
    out.fin    = (b0 & 0x80) != 0;
    out.opcode = static_cast<uint8_t>(b0 & 0x0f);
    const bool masked = (b1 & 0x80) != 0;

    uint64_t len = b1 & 0x7f;
    if (len == 126) {
        std::string ext;
        if (!conn.ReadExact(2, ext, timeoutMs)) return false;
        len = (static_cast<uint64_t>(static_cast<uint8_t>(ext[0])) << 8) |
               static_cast<uint8_t>(ext[1]);
    } else if (len == 127) {
        std::string ext;
        if (!conn.ReadExact(8, ext, timeoutMs)) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) {
            len = (len << 8) | static_cast<uint8_t>(ext[static_cast<std::size_t>(i)]);
        }
    }
    if (len > (16u << 20)) return false;   // sanity cap for a test origin

    std::string mask;
    if (masked && !conn.ReadExact(4, mask, timeoutMs)) return false;
    if (!conn.ReadExact(static_cast<std::size_t>(len), out.payload, timeoutMs)) return false;
    if (masked) {
        for (std::size_t i = 0; i < out.payload.size(); ++i) {
            out.payload[i] = static_cast<char>(out.payload[i] ^ mask[i % 4]);
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

LoopbackServer& LoopbackServer::Instance() {
    static LoopbackServer s;
    return s;
}

LoopbackServer& Loopback() {
    LoopbackServer& s = LoopbackServer::Instance();
    s.Start();
    return s;
}

LoopbackServer::~LoopbackServer() { Stop(); }

std::string LoopbackServer::Url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
}

std::string LoopbackServer::WsUrl(const std::string& path) const {
    return "ws://127.0.0.1:" + std::to_string(port_) + path;
}

bool LoopbackServer::Start() {
    if (running_.load(std::memory_order_acquire)) return true;
    if (!error_.empty()) return false;   // already failed once; don't retry per probe

#if !defined(_WIN32)
    // Writing to a socket the client has already closed (which every
    // cancellation probe does) would otherwise raise SIGPIPE and kill the tool.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    UltraNetSocketOptions opt;
    opt.reuseAddress = true;
    // Loopback only. Without an explicit bindAddress the listener would bind
    // the any-address, which makes the tool look like it opens a port to the
    // network — Windows pops a firewall prompt and behaviour-based AV
    // heuristics (e.g. AVG/Avast IDP.Generic) flag exactly that pattern.
    opt.bindAddress = "127.0.0.1";
    for (int port = kPortRangeStart; port <= kPortRangeEnd; ++port) {
        UltraNetHandle h = UltraNet_TcpListen(port, opt);
        if (h != UltraNetInvalidHandle) {
            listener_ = h;
            port_ = port;
            break;
        }
    }
    if (listener_ == UltraNetInvalidHandle) {
        error_ = "UltraNet_TcpListen failed on every port in " +
                 std::to_string(kPortRangeStart) + ".." +
                 std::to_string(kPortRangeEnd);
        return false;
    }

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    acceptThread_ = std::thread([this] { AcceptLoop(); });
    return true;
}

void LoopbackServer::Stop() {
    if (!running_.load(std::memory_order_acquire)) return;
    stop_.store(true, std::memory_order_release);

    // UltraNet_TcpAccept blocks with no timeout and no portable way to
    // interrupt it, so unblock it with a throwaway connection. The accept
    // loop sees stop_ and exits without servicing it.
    UltraNetSocketOptions opt;
    opt.connectTimeoutMs = 500;
    UltraNetHandle poke = UltraNet_TcpConnect("127.0.0.1", port_, opt);
    if (poke != UltraNetInvalidHandle) UltraNet_SocketClose(poke);

    if (acceptThread_.joinable()) acceptThread_.join();

    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lk(workersMutex_);
        workers.swap(workers_);
    }
    for (std::thread& t : workers) {
        if (t.joinable()) t.join();
    }

    if (listener_ != UltraNetInvalidHandle) {
        UltraNet_SocketClose(listener_);
        listener_ = UltraNetInvalidHandle;
    }
    running_.store(false, std::memory_order_release);
}

void LoopbackServer::AcceptLoop() {
    while (!stop_.load(std::memory_order_acquire)) {
        UltraNetEndpoint remote;
        UltraNetHandle conn = UltraNet_TcpAccept(listener_, remote);
        if (conn == UltraNetInvalidHandle) {
            if (stop_.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (stop_.load(std::memory_order_acquire)) {
            UltraNet_SocketClose(conn);
            break;
        }
        // Workers are joined in Stop(). A status run makes tens of requests,
        // so letting the vector grow for the life of the process is fine and
        // avoids the usual "is it safe to join a running thread" dance.
        std::lock_guard<std::mutex> lk(workersMutex_);
        workers_.emplace_back([this, conn] { HandleConnection(conn); });
    }
}

void LoopbackServer::HandleConnection(UltraNetHandle conn) {
    Conn c(conn);

    for (;;) {
        if (stop_.load(std::memory_order_acquire)) break;

        std::string head;
        if (!c.ReadUntil("\r\n\r\n", head, kReadTimeoutMs)) break;

        Request req;
        {
            std::size_t lineStart = 0;
            bool first = true;
            while (lineStart <= head.size()) {
                std::size_t lineEnd = head.find("\r\n", lineStart);
                if (lineEnd == std::string::npos) lineEnd = head.size();
                const std::string line = head.substr(lineStart, lineEnd - lineStart);
                lineStart = lineEnd + 2;
                if (line.empty()) { if (lineEnd >= head.size()) break; continue; }
                if (first) {
                    first = false;
                    std::size_t s1 = line.find(' ');
                    std::size_t s2 = (s1 == std::string::npos)
                                     ? std::string::npos : line.find(' ', s1 + 1);
                    if (s1 == std::string::npos || s2 == std::string::npos) break;
                    req.method  = line.substr(0, s1);
                    req.target  = line.substr(s1 + 1, s2 - s1 - 1);
                    req.version = line.substr(s2 + 1);
                } else {
                    const std::size_t colon = line.find(':');
                    if (colon == std::string::npos) continue;
                    req.headers.emplace_back(ToLower(Trim(line.substr(0, colon))),
                                             Trim(line.substr(colon + 1)));
                }
                if (lineEnd >= head.size()) break;
            }
        }
        if (req.method.empty()) break;

        // WebSocket upgrade takes over the connection entirely.
        if (ToLower(req.Header("upgrade")) == "websocket") {
            const std::string key = req.Header("sec-websocket-key");
            if (key.empty()) {
                c.WriteAll(BuildResponse(400, "text/plain", "missing key", {}, false, true));
                break;
            }
            std::string handshake =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + WebSocketAccept(key) + "\r\n";
            const std::string proto = req.Header("sec-websocket-protocol");
            if (!proto.empty()) {
                // Echo the first offered subprotocol so negotiation is observable.
                const std::size_t comma = proto.find(',');
                handshake += "Sec-WebSocket-Protocol: " +
                             Trim(proto.substr(0, comma)) + "\r\n";
            }
            handshake += "\r\n";
            if (!c.WriteAll(handshake)) break;
            requestCount_.fetch_add(1, std::memory_order_release);

            for (;;) {
                if (stop_.load(std::memory_order_acquire)) break;
                WsFrame f;
                if (!ReadWsFrame(c, f, 30000)) break;
                if (f.opcode == 0x8) {                       // close
                    c.WriteAll(BuildWsFrame(0x8, f.payload));
                    break;
                }
                if (f.opcode == 0x9) {                       // ping -> pong
                    if (!c.WriteAll(BuildWsFrame(0xA, f.payload))) break;
                    continue;
                }
                if (f.opcode == 0xA) continue;               // pong: ignore
                if (f.opcode == 0x1 || f.opcode == 0x2) {    // echo text/binary
                    if (!c.WriteAll(BuildWsFrame(f.opcode, f.payload))) break;
                }
            }
            break;
        }

        // ---- request body ----------------------------------------------
        if (ToLower(req.Header("expect")) == "100-continue") {
            if (!c.WriteAll("HTTP/1.1 100 Continue\r\n\r\n")) break;
        }
        if (ToLower(req.Header("transfer-encoding")).find("chunked") != std::string::npos) {
            if (!ReadChunkedBody(c, req.body)) break;
        } else if (req.HasHeader("content-length")) {
            std::size_t len = 0;
            try {
                len = static_cast<std::size_t>(std::stoul(req.Header("content-length")));
            } catch (...) {
                break;
            }
            if (len > 0 && !c.ReadExact(len, req.body, kReadTimeoutMs)) break;
        }

        requestCount_.fetch_add(1, std::memory_order_release);

        const bool isHead    = (req.method == "HEAD");
        const bool wantClose = ToLower(req.Header("connection")) == "close" ||
                               req.version != "HTTP/1.1";
        bool keepAlive = !wantClose;

        // Strip the query string for routing; probes only key off the path.
        std::string path = req.target;
        const std::size_t qmark = path.find('?');
        if (qmark != std::string::npos) path.erase(qmark);

        std::string response;

        if (path == "/hello") {
            response = BuildResponse(200, "text/plain; charset=utf-8",
                                     "hello from ultranet loopback\n",
                                     {}, keepAlive, !isHead);
        } else if (path.rfind("/bytes/", 0) == 0) {
            std::size_t n = 0;
            try { n = static_cast<std::size_t>(std::stoul(path.substr(7))); }
            catch (...) { n = 0; }
            n = std::min<std::size_t>(n, 8u << 20);
            std::string payload(n, '\0');
            for (std::size_t i = 0; i < n; ++i) {
                payload[i] = static_cast<char>(i & 0xff);
            }
            response = BuildResponse(200, "application/octet-stream", payload,
                                     {}, keepAlive, !isHead);
        } else if (path == "/echo") {
            response = BuildResponse(
                200, "application/octet-stream", req.body,
                {"x-echo-method: " + req.method,
                 "x-echo-probe: " + req.Header("x-probe"),
                 "x-echo-content-type: " + req.Header("content-type")},
                keepAlive, !isHead);
        } else if (path == "/redirect") {
            response = BuildResponse(302, "text/plain", "", {"Location: /hello"},
                                     keepAlive, !isHead);
        } else if (path.rfind("/status/", 0) == 0) {
            int code = 500;
            try { code = std::stoi(path.substr(8)); } catch (...) {}
            response = BuildResponse(code, "text/plain", "", {}, keepAlive, !isHead);
        } else if (path == "/setcookie") {
            response = BuildResponse(200, "text/plain", "cookie set\n",
                                     {"Set-Cookie: ultranet_probe=chocolate; Path=/"},
                                     keepAlive, !isHead);
        } else if (path == "/cookie") {
            const std::string cookie = req.Header("cookie");
            response = BuildResponse(200, "text/plain",
                                     "cookie=" + (cookie.empty() ? "none" : cookie),
                                     {}, keepAlive, !isHead);
        } else if (path == "/auth") {
            const std::string auth = req.Header("authorization");
            if (auth.empty()) {
                response = BuildResponse(401, "text/plain", "",
                                         {"WWW-Authenticate: Basic realm=\"ultranet\""},
                                         keepAlive, !isHead);
            } else {
                response = BuildResponse(200, "text/plain", "auth=" + auth,
                                         {}, keepAlive, !isHead);
            }
        } else if (path == "/sse") {
            // Streamed, so no Content-Length: the stream ends with the socket.
            keepAlive = false;
            std::string stream =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n"
                "\r\n"
                ": this is a comment and must be ignored\n\n"
                "id: 1\nevent: greeting\ndata: hello\n\n"
                "data: line one\ndata: line two\n\n"
                "retry: 2500\nid: 3\ndata: bye\n\n";
            c.WriteAll(stream);
            break;
        } else if (path == "/slow") {
            // Headers first, then a slow dribble. Cancellation probes kill the
            // request part-way through; the write fails and we drop the
            // connection.
            keepAlive = false;
            if (!c.WriteAll("HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/octet-stream\r\n"
                            "Content-Length: 1000000\r\n"
                            "Connection: close\r\n\r\n")) {
                break;
            }
            const std::string block(256, 'x');
            for (int i = 0; i < 200 && !stop_.load(std::memory_order_acquire); ++i) {
                if (!c.WriteAll(block)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            break;
        } else {
            response = BuildResponse(404, "text/plain", "not found\n",
                                     {}, keepAlive, !isHead);
        }

        if (!c.WriteAll(response)) break;
        if (!keepAlive) break;
    }

    UltraNet_SocketClose(conn);
}

} // namespace ultranet_apistatus
