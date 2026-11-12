// Apps/UltraWeb/host/UltraWebSocket.h
// An app's WebSockets (uc_ws_* in UltraWeb/guest/ultraweb.h): the request
// the bridge hands to the network, the rules a browser applies to it - ws:
// and wss: only, no ws: from an https app, the app's origin in an Origin
// header for the server to judge, no cookies - and the network service that
// opens them through UltraNet.
//
// The rules are kept apart from the network, as the fetch rules are, so the
// bridge's tests can run against a fake service with no network at all.
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraWeb {

struct SocketRequest {
    std::string url;                       // absolute, ws: or wss:
    std::string origin;                    // the app's origin, or "null" when it has none
    std::vector<std::string> protocols;    // subprotocols asked for, in order
};

// What happens to a socket, on the UI thread, and never after Close.
struct SocketEvents {
    std::function<void()> onOpen;
    std::function<void(std::vector<uint8_t> data, bool binary)> onMessage;
    // The socket closed, either side having asked: the code and reason the
    // other side gave (1005 when it gave none).
    std::function<void(int code, std::string reason)> onClose;
    // It could not connect, or the connection was lost; `tooLarge` when the
    // other side sent a message over SocketRules::kMaxMessageBytes.
    std::function<void(std::string error, bool tooLarge)> onError;
};

class SocketConnection {
public:
    virtual ~SocketConnection() = default;
    // False when it is not open, or the message could not go.
    virtual bool Send(const std::vector<uint8_t>& data, bool binary) = 0;
    // Closes it (if it is open or still connecting); no event follows.
    virtual void Close(int code, const std::string& reason) = 0;
};

// Opens a socket. Null when it cannot even start (the service then reports
// nothing); otherwise the events above follow.
using SocketService =
    std::function<std::shared_ptr<SocketConnection>(const SocketRequest& request, SocketEvents events)>;

namespace SocketRules {

constexpr size_t kMaxMessageBytes = 1u << 20;     // either way
constexpr size_t kMaxQueuedBytes = 4u << 20;      // received and not yet read, per socket
constexpr size_t kMaxOpen = 8;                    // per app
constexpr size_t kMaxUrlBytes = 8192;
constexpr size_t kMaxProtocolsBytes = 1024;
constexpr int kConnectTimeoutMs = 10000;
constexpr int kPingIntervalMs = 30000;

// Builds the request an app asked for, or refuses it before anything is
// sent. `appAddress` is the address the app was loaded from; a relative
// `reference` is resolved against it, and http: / https: become ws: / wss:.
// `protocols` is a comma-separated list. Returns UC_OK, or a UC_ERR_* with
// `reason` for the app's console.
int32_t Prepare(const std::string& appAddress, const std::string& reference, const std::string& protocols,
                SocketRequest& out, std::string& reason);

} // namespace SocketRules

// The real service: UltraNet's WebSockets, TLS verified, connecting on a
// thread of their own (a handshake blocks), events posted to the UI thread
// through `post` (PostToUIThread). UltraNet's WebSocket callbacks are one set
// for the process: UltraWeb's router takes the sockets it opened and hands
// every other socket's events to whatever set was there before.
SocketService MakeNetworkSockets(std::function<void(std::function<void()>)> post);

} // namespace UltraWeb
