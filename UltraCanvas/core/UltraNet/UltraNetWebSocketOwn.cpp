// core/UltraNet/UltraNetWebSocketOwn.cpp
// UltraNet's own WebSocket connection: the upgrade (RFC 6455 section 4) and
// the frames (section 5) on UltraNet_Tcp* and UltraNet_Tls*. See
// UltraNetWebSocketOwn.h for where it is used and why.
//
// What it does not do: extensions (none is offered, so a frame with a
// reserved bit is not the protocol), fragmenting what it sends (one frame a
// message), and following a redirect in the upgrade's answer (anything but
// 101 is the failure it says it is).
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNetWebSocketOwn.h"

#include "UltraCanvasTextUtils.h"
#include "UltraNet/UltraNetSocket.h"
#include "UltraNet/UltraNetTls.h"
#include "UltraNet/UltraNetUrl.h"
#include "UltraNetRandom.h"
#include "UltraNetSha1.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <limits>

namespace ultranet_internal {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// An address, not a name: nothing to put in the TLS hello's server name.
bool IsIpLiteral(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    return !host.empty() && host.find_first_not_of("0123456789.") == std::string::npos;
}

bool HasLineBreak(const std::string& s) {
    return s.find_first_of("\r\n") != std::string::npos;
}

// Whether a comma-separated header value has `token`, whatever its case.
bool HasToken(const std::string& value, const std::string& token) {
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(',', start);
        if (end == std::string::npos) end = value.size();
        if (Lower(Trim(value.substr(start, end - start))) == token) return true;
        start = end + 1;
    }
    return false;
}

std::string Base64(const std::string& bytes) {
    return UltraCanvas::Base64Encode(std::vector<uint8_t>(bytes.begin(), bytes.end()), false);
}

} // namespace

// ============================================================================
// THE ADDRESS, THE ACCEPT KEY
// ============================================================================

bool ParseWebSocketAddress(const std::string& url, WebSocketAddress& out, std::string& error) {
    out = WebSocketAddress();
    for (unsigned char ch : url) {
        if (ch <= 0x20 || ch == 0x7F) {
            error = "a WebSocket address holds no space and no control character";
            return false;
        }
    }
    const size_t schemeEnd = url.find("://");
    const std::string scheme = schemeEnd == std::string::npos ? std::string() : Lower(url.substr(0, schemeEnd));
    if (scheme != "ws" && scheme != "wss") {
        error = "not a ws:// or wss:// address";
        return false;
    }
    out.tls = scheme == "wss";
    const size_t authorityStart = schemeEnd + 3;
    size_t authorityEnd = url.find_first_of("/?#", authorityStart);
    if (authorityEnd == std::string::npos) authorityEnd = url.size();
    std::string authority = url.substr(authorityStart, authorityEnd - authorityStart);
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        out.userInfo = UltraNet_UrlDecode(authority.substr(0, at));
        authority.erase(0, at + 1);
    }
    std::string portText;
    bool bracketed = false;
    if (!authority.empty() && authority[0] == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos) {
            error = "an IPv6 address without its closing bracket";
            return false;
        }
        bracketed = true;
        out.host = authority.substr(1, close - 1);
        const std::string rest = authority.substr(close + 1);
        if (!rest.empty()) {
            if (rest[0] != ':') {
                error = "something other than a port after the address";
                return false;
            }
            portText = rest.substr(1);
        }
    } else {
        const size_t colon = authority.rfind(':');
        out.host = authority.substr(0, colon);
        if (colon != std::string::npos) portText = authority.substr(colon + 1);
    }
    if (out.host.empty()) {
        error = "the address names no host";
        return false;
    }
    const int schemePort = out.tls ? 443 : 80;
    out.port = schemePort;
    if (!portText.empty()) {
        if (portText.size() > 5 || portText.find_first_not_of("0123456789") != std::string::npos) {
            error = "the port is not a number";
            return false;
        }
        out.port = std::atoi(portText.c_str());
        if (out.port < 1 || out.port > 65535) {
            error = "the port is not between 1 and 65535";
            return false;
        }
    }
    out.hostHeader = bracketed ? "[" + out.host + "]" : out.host;
    if (out.port != schemePort) out.hostHeader += ":" + std::to_string(out.port);
    // The path and the query; the fragment is the client's alone.
    size_t fragment = url.find('#', authorityEnd);
    if (fragment == std::string::npos) fragment = url.size();
    out.target = url.substr(authorityEnd, fragment - authorityEnd);
    if (out.target.empty() || out.target[0] == '?') out.target.insert(out.target.begin(), '/');
    return true;
}

std::string WebSocketAcceptFor(const std::string& key) {
    const std::string joined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::array<uint8_t, 20> digest = Sha1(joined.data(), joined.size());
    return UltraCanvas::Base64Encode(std::vector<uint8_t>(digest.begin(), digest.end()), false);
}

// ============================================================================
// CONNECTING: TCP, TLS, THE UPGRADE
// ============================================================================

std::unique_ptr<OwnWebSocket> OwnWebSocket::Open(const std::string& url, const UltraNetWebSocketOptions& options,
                                                 std::string& error) {
    WebSocketAddress address;
    if (!ParseWebSocketAddress(url, address, error)) return nullptr;

    using Clock = std::chrono::steady_clock;
    const int timeoutMs = options.connectTimeoutMs > 0 ? options.connectTimeoutMs : 10000;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    const auto left = [&]() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return static_cast<int>(std::max<long long>(1, ms));
    };

    UltraNetSocketOptions socketOptions;
    socketOptions.connectTimeoutMs = timeoutMs;
    socketOptions.noDelay = true;
    socketOptions.keepAlive = true;
    // The proxy the setting names for this address - what libcurl is given
    // for it (UltraNetCurlProxy.h) - and through an HTTP proxy as a CONNECT
    // tunnel, which is how the socket layer goes through one.
    socketOptions.proxy = UltraNet_ResolveProxy(url);
    UltraNetHandle socket = UltraNetInvalidHandle;
    const UltraNetResult opened = UltraNet_TcpOpen(address.host, address.port, socketOptions, socket);
    if (!opened || socket == UltraNetInvalidHandle) {
        error = opened.message.empty() ? "could not connect to " + address.hostHeader : opened.message;
        return nullptr;
    }
    std::unique_ptr<OwnWebSocket> ws(new OwnWebSocket());
    ws->socket_ = socket;       // the destructor's to close from here on

    // The handshakes are bounded by what is left of the time.
    UltraNet_SocketSetTimeout(socket, left(), left());
    if (address.tls) {
        UltraNetTlsOptions tls;
        tls.verifyPeer = options.verifyTls;
        tls.verifyHostname = options.verifyTls;
        if (!IsIpLiteral(address.host)) tls.sniHostname = address.host;
        if (UltraNet_TlsWrap(socket, address.host, tls) != socket) {
            error = "could not start TLS with " + address.hostHeader;
            return nullptr;
        }
        const UltraNetResult shaken = UltraNet_TlsHandshake(socket);
        if (!shaken) {
            error = shaken.code == UltraNetResultCode::TlsCertificateInvalid
                        ? "the server's certificate is not trusted for " + address.host + " (" + shaken.message + ")"
                        : "the TLS handshake with " + address.hostHeader + " failed (" + shaken.message + ")";
            return nullptr;
        }
    }

    // ---- the request -----------------------------------------------------------------
    uint8_t keyBytes[16];
    FillRandom(keyBytes, sizeof keyBytes);
    const std::string key = UltraCanvas::Base64Encode(std::vector<uint8_t>(keyBytes, keyBytes + sizeof keyBytes), false);
    std::string protocols;
    for (const std::string& p : options.subprotocols) {
        if (HasLineBreak(p)) {
            error = "a subprotocol's name holds a line break";
            return nullptr;
        }
        protocols += (protocols.empty() ? "" : ", ") + p;
    }
    std::string request = "GET " + address.target + " HTTP/1.1\r\n";
    if (!options.headers.Has("Host")) request += "Host: " + address.hostHeader + "\r\n";
    request += "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
               "\r\nSec-WebSocket-Version: 13\r\n";
    if (!protocols.empty()) request += "Sec-WebSocket-Protocol: " + protocols + "\r\n";
    if (!address.userInfo.empty() && !options.headers.Has("Authorization"))
        request += "Authorization: Basic " + Base64(address.userInfo) + "\r\n";
    for (const auto& [name, value] : options.headers.Entries()) {
        // The upgrade's own headers are not the caller's to give twice.
        const std::string lower = Lower(name);
        if (lower == "upgrade" || lower == "connection" || lower == "sec-websocket-key" ||
            lower == "sec-websocket-version" || (lower == "sec-websocket-protocol" && !protocols.empty()))
            continue;
        if (name.empty() || HasLineBreak(name) || HasLineBreak(value) || name.find(':') != std::string::npos) {
            error = "a header for the upgrade is not one line, or has no name";
            return nullptr;
        }
        request += name + ": " + value + "\r\n";
    }
    request += "\r\n";
    if (!ws->SendAll(std::vector<uint8_t>(request.begin(), request.end()))) {
        error = "the connection to " + address.hostHeader + " broke before the upgrade was asked for";
        return nullptr;
    }

    // ---- the answer ------------------------------------------------------------------
    std::string head;
    size_t headEnd = std::string::npos;
    while ((headEnd = head.find("\r\n\r\n")) == std::string::npos) {
        if (head.size() > 16 * 1024) {
            error = "the server's answer to the upgrade is larger than 16 KB";
            return nullptr;
        }
        if (Clock::now() >= deadline) {
            error = "the server did not answer the upgrade in time";
            return nullptr;
        }
        std::vector<uint8_t> chunk;
        const UltraNetResult r = UltraNet_TcpReceive(socket, chunk, 4096, left());
        if (!r) {
            error = r.code == UltraNetResultCode::Timeout
                        ? "the server did not answer the upgrade in time"
                        : "the connection broke during the upgrade (" + r.message + ")";
            return nullptr;
        }
        if (chunk.empty()) {
            error = "the server closed the connection instead of answering the upgrade";
            return nullptr;
        }
        head.append(chunk.begin(), chunk.end());
    }
    // What came with the answer already belongs to the frames.
    ws->in_.assign(head.begin() + static_cast<std::ptrdiff_t>(headEnd + 4), head.end());
    head.resize(headEnd);

    const size_t lineEnd = head.find("\r\n");
    const std::string status = head.substr(0, lineEnd);
    // "HTTP/1.1 101 Switching Protocols"
    const size_t space = status.find(' ');
    const bool http = status.rfind("HTTP/1.", 0) == 0 && space != std::string::npos;
    const int code = http ? std::atoi(status.c_str() + space + 1) : 0;
    if (code != 101) {
        error = http ? "the server answered \"" + status + "\" instead of switching to WebSocket"
                     : "the server's answer is not HTTP";
        return nullptr;
    }
    std::string upgrade, connection, accept, extensions, chosen;
    size_t at = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
    while (at < head.size()) {
        size_t end = head.find("\r\n", at);
        if (end == std::string::npos) end = head.size();
        const std::string line = head.substr(at, end - at);
        at = end + 2;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = Lower(Trim(line.substr(0, colon)));
        const std::string value = Trim(line.substr(colon + 1));
        if (name == "upgrade") upgrade = value;
        else if (name == "connection") connection += (connection.empty() ? "" : ", ") + value;
        else if (name == "sec-websocket-accept") accept = value;
        else if (name == "sec-websocket-extensions") extensions = value;
        else if (name == "sec-websocket-protocol") chosen = value;
    }
    if (Lower(upgrade) != "websocket" || !HasToken(connection, "upgrade")) {
        error = "the server answered 101 without upgrading to WebSocket";
        return nullptr;
    }
    if (accept != WebSocketAcceptFor(key)) {
        error = "the server's Sec-WebSocket-Accept is not the answer to this handshake";
        return nullptr;
    }
    if (!extensions.empty()) {
        error = "the server turned on an extension (" + extensions + ") that was not offered";
        return nullptr;
    }
    if (!chosen.empty()) {
        if (std::find(options.subprotocols.begin(), options.subprotocols.end(), chosen) == options.subprotocols.end()) {
            error = "the server chose a subprotocol (" + chosen + ") that was not offered";
            return nullptr;
        }
        ws->subprotocol_ = chosen;
    }
    // From here a read waits as long as its caller says. A frame that cannot
    // be written in half a minute is a connection that has stopped taking any.
    UltraNet_SocketSetTimeout(socket, 30000, 0);
    return ws;
}

OwnWebSocket::~OwnWebSocket() {
    if (socket_ != UltraNetInvalidHandle) UltraNet_SocketClose(socket_);
}

// ============================================================================
// FRAMES
// ============================================================================

bool OwnWebSocket::SendAll(const std::vector<uint8_t>& bytes) {
    int sent = 0;
    UltraNetResult r = UltraNet_TcpSend(socket_, bytes, sent);
    if (!r || sent <= 0) return false;
    size_t at = static_cast<size_t>(sent);
    while (at < bytes.size()) {
        // (The socket took a part: the rest, as a buffer of its own.)
        const std::vector<uint8_t> rest(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
        sent = 0;
        r = UltraNet_TcpSend(socket_, rest, sent);
        if (!r || sent <= 0) return false;
        at += static_cast<size_t>(sent);
    }
    return true;
}

// Four bytes nobody can foresee, a frame (5.3: from a strong source of
// entropy) - the operating system's, taken a few frames' worth at a time.
void OwnWebSocket::NextMask(uint8_t mask[4]) {
    if (randomAt_ + 4 > random_.size()) {
        random_.resize(256);
        FillRandom(random_.data(), random_.size());
        randomAt_ = 0;
    }
    for (int i = 0; i < 4; ++i) mask[i] = random_[randomAt_ + static_cast<size_t>(i)];
    randomAt_ += 4;
}

bool OwnWebSocket::Send(uint8_t opcode, const void* data, std::size_t size) {
    if (socket_ == UltraNetInvalidHandle) return false;
    std::vector<uint8_t> frame;
    frame.reserve(size + 14);
    frame.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0F)));     // FIN: one frame a message
    if (size < 126) {
        frame.push_back(static_cast<uint8_t>(0x80 | size));
    } else if (size <= 0xFFFF) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>(size >> 8));
        frame.push_back(static_cast<uint8_t>(size));
    } else {
        frame.push_back(0x80 | 127);
        const uint64_t length = size;
        for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(static_cast<uint8_t>(length >> shift));
    }
    uint8_t mask[4];
    NextMask(mask);
    frame.insert(frame.end(), mask, mask + 4);
    const size_t start = frame.size();
    frame.resize(start + size);
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) frame[start + i] = static_cast<uint8_t>(bytes[i] ^ mask[i & 3]);
    return SendAll(frame);
}

OwnWebSocket::Read OwnWebSocket::Parse(std::size_t allowance, Frame& out, std::string& error) {
    const size_t have = in_.size() - at_;
    if (have < 2) return Read::Partial;
    const uint8_t* p = in_.data() + at_;
    const uint8_t first = p[0], second = p[1];
    if (first & 0x70) {
        error = "a frame with a reserved bit set - an extension that was not agreed";
        return Read::Malformed;
    }
    if (second & 0x80) {
        error = "a masked frame from the server";
        return Read::Malformed;
    }
    const uint8_t opcode = first & 0x0F;
    const bool fin = (first & 0x80) != 0;
    uint64_t length = second & 0x7F;
    size_t header = 2;
    if (length == 126) {
        if (have < 4) return Read::Partial;
        length = (static_cast<uint64_t>(p[2]) << 8) | p[3];
        header = 4;
    } else if (length == 127) {
        if (have < 10) return Read::Partial;
        length = 0;
        for (int i = 0; i < 8; ++i) length = (length << 8) | p[2 + i];
        header = 10;
        if (length >> 63) {
            error = "a frame length with its top bit set";
            return Read::Malformed;
        }
    }
    if (opcode & 0x08) {
        if (opcode != Close && opcode != Ping && opcode != Pong) {
            error = "an unknown control frame (opcode " + std::to_string(opcode) + ")";
            return Read::Malformed;
        }
        if (!fin || length > 125) {
            error = "a control frame in fragments, or longer than 125 bytes";
            return Read::Malformed;
        }
    } else {
        if (opcode > Binary) {
            error = "an unknown frame (opcode " + std::to_string(opcode) + ")";
            return Read::Malformed;
        }
        // Before any of it is kept: the frame says how long it is.
        if (length > allowance) return Read::TooBig;
    }
    if (length > std::numeric_limits<size_t>::max() - header) return Read::TooBig;
    if (have - header < length) return Read::Partial;
    const size_t size = static_cast<size_t>(length);
    out.opcode = opcode;
    out.fin = fin;
    out.payload.assign(p + header, p + header + size);
    at_ += header + size;
    if (at_ == in_.size()) {
        in_.clear();
        at_ = 0;
    } else if (at_ >= 64 * 1024) {
        in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(at_));
        at_ = 0;
    }
    return Read::Frame;
}

OwnWebSocket::Read OwnWebSocket::Receive(int timeoutMs, std::size_t allowance, Frame& out, std::string& error) {
    if (socket_ == UltraNetInvalidHandle) {
        error = "the connection is gone";
        return Read::Broken;
    }
    const Read already = Parse(allowance, out, error);
    if (already != Read::Partial) return already;
    std::vector<uint8_t> chunk;
    const UltraNetResult r = UltraNet_TcpReceive(socket_, chunk, 65536, timeoutMs < 1 ? 1 : timeoutMs);
    if (!r) {
        if (r.code == UltraNetResultCode::Timeout) return Read::Idle;
        error = r.message.empty() ? "the connection broke" : r.message;
        return Read::Broken;
    }
    if (chunk.empty()) return Read::Closed;
    in_.insert(in_.end(), chunk.begin(), chunk.end());
    return Parse(allowance, out, error);
}

} // namespace ultranet_internal
