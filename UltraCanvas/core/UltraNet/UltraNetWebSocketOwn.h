// core/UltraNet/UltraNetWebSocketOwn.h
// A WebSocket connection (RFC 6455) made by UltraNet itself - on its own TCP
// sockets (UltraNet_TcpOpen, through the proxy setting), its own TLS
// (UltraNet_TlsWrap: OpenSSL, Schannel, SecureTransport) and nothing of
// libcurl's. UltraNet_WebSocket* is libcurl's curl_ws_* where libcurl has it;
// this is what it runs on where libcurl has not: Apple's own libcurl through
// macOS 15 lists neither ws nor wss, and several distributions' builds leave
// it out. Internal - the public surface is UltraNetWebSocket.h, the same on
// both.
//
// One object is one connection, already upgraded. It keeps no thread and no
// lock: its owner (UltraNetWebSocket.cpp) calls Send and Receive under one
// mutex, since a TLS connection is read and written by one thread at a time.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetWebSocket.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ultranet_internal {

// The parts of a ws:// or wss:// address the handshake needs. Pure.
struct WebSocketAddress {
    bool tls = false;
    std::string host;           // without the brackets of an IPv6 literal
    int port = 0;               // the scheme's (80, 443) when the address names none
    std::string hostHeader;     // as the Host header carries it
    std::string target;         // the path and the query: what GET asks for
    std::string userInfo;       // "user:password" when the address has one (decoded as it stands)
};
// False with a sentence in `error` for anything that is not a ws:// or wss://
// address with a host, or that holds a space or a control character (a CR or
// LF would end the request line).
bool ParseWebSocketAddress(const std::string& url, WebSocketAddress& out, std::string& error);

// base64(SHA-1(key + the protocol's GUID)): what the server must answer in
// Sec-WebSocket-Accept to the Sec-WebSocket-Key it was sent. Pure.
std::string WebSocketAcceptFor(const std::string& key);

class OwnWebSocket {
public:
    enum Opcode : uint8_t {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA,
    };

    struct Frame {
        uint8_t opcode = 0;
        bool fin = true;
        std::vector<uint8_t> payload;
    };

    enum class Read {
        Frame,      // `out` holds one
        Partial,    // bytes came, the frame is not whole yet: call again
        Idle,       // nothing came within the time
        Closed,     // the other side closed the connection (no close frame)
        Broken,     // the connection failed; `error` says how
        Malformed,  // not the protocol; `error` says what
        TooBig,     // a data frame announcing more than `allowance`
    };

    // Connects (through the proxy the setting names for the address), makes
    // the TLS of wss://, and upgrades. nullptr, with why in `error`, when any
    // of it fails - the time for all of it is options.connectTimeoutMs.
    static std::unique_ptr<OwnWebSocket> Open(const std::string& url, const UltraNetWebSocketOptions& options,
                                              std::string& error);
    ~OwnWebSocket();
    OwnWebSocket(const OwnWebSocket&) = delete;
    OwnWebSocket& operator=(const OwnWebSocket&) = delete;

    // One whole frame, masked as a client's must be (5.3). False when the
    // connection did not take it.
    bool Send(uint8_t opcode, const void* data, std::size_t size);

    // The next frame. Reads from the connection at most once, waiting up to
    // timeoutMs, so a caller holding a lock for it holds it no longer than
    // that. `allowance`: the most a data frame may announce; a larger one is
    // TooBig before any of it is kept.
    Read Receive(int timeoutMs, std::size_t allowance, Frame& out, std::string& error);

    // The subprotocol the server chose ("" for none).
    const std::string& Subprotocol() const { return subprotocol_; }

private:
    OwnWebSocket() = default;
    bool SendAll(const std::vector<uint8_t>& bytes);
    void NextMask(uint8_t mask[4]);
    // A frame from what is already here: Frame, Partial (not all of it yet),
    // Malformed or TooBig.
    Read Parse(std::size_t allowance, Frame& out, std::string& error);

    UltraNetHandle socket_ = UltraNetInvalidHandle;
    std::string subprotocol_;
    std::vector<uint8_t> in_;           // received and not yet a frame
    std::size_t at_ = 0;                // where the next frame starts in it
    std::vector<uint8_t> random_;       // the operating system's, handed out four bytes a frame
    std::size_t randomAt_ = 0;
};

} // namespace ultranet_internal
