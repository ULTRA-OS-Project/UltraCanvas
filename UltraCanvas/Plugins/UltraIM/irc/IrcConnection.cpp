// Plugins/UltraIM/irc/IrcConnection.cpp
// A TCP (and TLS) connection to an IRC server, read and written by lines.
// Version: 0.1.1 - goes by UltraNet's answer (Timeout: quiet, an error: gone); no
//                  SIGPIPE handling of its own (UltraNet raises none)
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "IrcConnection.h"

#include "UltraNet/UltraNetSocket.h"


namespace UltraIMIrc {

namespace {

// A line longer than this (tags 8191 + the message 512, and some room) is
// not IRC: the connection is given up.
constexpr size_t kMaxLine = 16384;

bool IsIpLiteral(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    for (char c : host) {
        if (!(c == '.' || (c >= '0' && c <= '9'))) return false;
    }
    return !host.empty();
}


} // namespace

bool IrcConnection::Open(const std::string& host, int port, bool tls, const UltraNetTlsOptions& tlsOptions,
                         int timeoutMs, std::string& error, bool& certificateRefused) {
    Close();
    certificateRefused = false;
    UltraNetSocketOptions options;
    options.connectTimeoutMs = timeoutMs;
    options.noDelay = true;
    options.keepAlive = true;
    // The system's SOCKS proxy where one is set (never for this machine).
    options.useProxySetting = true;
    UltraNetHandle handle = UltraNetInvalidHandle;
    const UltraNetResult opened = UltraNet_TcpOpen(host, port, options, handle);
    if (!opened || handle == UltraNetInvalidHandle) {
        error = opened.message.empty() ? "could not connect" : opened.message;
        return false;
    }
    // Bounds the handshake, and every write from now on.
    UltraNet_SocketSetTimeout(handle, timeoutMs, timeoutMs);
    if (tls) {
        UltraNetTlsOptions settings = tlsOptions;
        // Never without verification: the server is who it says it is.
        settings.verifyPeer = true;
        settings.verifyHostname = true;
        if (settings.sniHostname.empty() && !IsIpLiteral(host)) settings.sniHostname = host;
        if (UltraNet_TlsWrap(handle, host, settings) != handle) {
            UltraNet_SocketClose(handle);
            error = "could not start TLS";
            return false;
        }
        const UltraNetResult shaken = UltraNet_TlsHandshake(handle);
        if (!shaken) {
            UltraNet_SocketClose(handle);
            certificateRefused = shaken.code == UltraNetResultCode::TlsCertificateInvalid;
            error = certificateRefused ? "the server's certificate is not trusted (" + shaken.message + ")"
                                       : "the TLS handshake failed (" + shaken.message + ")";
            return false;
        }
    }
    handle_ = handle;
    tls_ = tls;
    buffer_.clear();
    return true;
}

IrcConnection::ReadStatus IrcConnection::Read(int timeoutMs, std::vector<std::string>& lines) {
    if (handle_ == UltraNetInvalidHandle) return ReadStatus::Closed;
    if (timeoutMs < 10) timeoutMs = 10;
    std::vector<uint8_t> chunk;
    const UltraNetResult r = UltraNet_TcpReceive(handle_, chunk, 16384, timeoutMs);
    if (!r) return r.code == UltraNetResultCode::Timeout ? ReadStatus::Idle : ReadStatus::Closed;
    if (chunk.empty()) return ReadStatus::Closed;       // the server closed it
    buffer_.append(chunk.begin(), chunk.end());
    size_t start = 0;
    for (;;) {
        const size_t nl = buffer_.find('\n', start);
        if (nl == std::string::npos) break;
        size_t end = nl;
        if (end > start && buffer_[end - 1] == '\r') --end;
        if (end > start) lines.push_back(buffer_.substr(start, end - start));
        start = nl + 1;
    }
    buffer_.erase(0, start);
    if (buffer_.size() > kMaxLine) return ReadStatus::Closed;
    return ReadStatus::Lines;
}

bool IrcConnection::Send(const std::string& line) {
    if (handle_ == UltraNetInvalidHandle) return false;
    std::string data = line;
    data += "\r\n";
    size_t at = 0;
    while (at < data.size()) {
        const std::vector<uint8_t> part(data.begin() + static_cast<std::ptrdiff_t>(at), data.end());
        int sent = 0;
        const UltraNetResult r = UltraNet_TcpSend(handle_, part, sent);
        if (!r || sent <= 0) {
            return false;
        }
        at += static_cast<size_t>(sent);
    }
    return true;
}

void IrcConnection::Close() {
    if (handle_ == UltraNetInvalidHandle) return;
    UltraNet_SocketClose(handle_);
    handle_ = UltraNetInvalidHandle;
    tls_ = false;
    buffer_.clear();
}

} // namespace UltraIMIrc
