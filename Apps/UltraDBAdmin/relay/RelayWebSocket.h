// Apps/UltraDBAdmin/relay/RelayWebSocket.h
// The relay's listener: a WebSocket server (RFC 6455, text messages) on one
// address and path, each connection a session of UltraDbRelayServer, each
// message a request answered on a thread of its own - so a cancel reaches
// the server while the query it stops still runs. GET /health answers "ok"
// for a load balancer. No TLS here: the relay listens on loopback behind a
// TLS proxy (RelayConfig.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraDBAdminRelay {

struct ListenOptions {
    std::string host = "127.0.0.1";
    int port = 8765;                       // 0: any free port (the tests)
    std::string path = "/relay";
    std::vector<std::string> origins;      // allowed Origin headers; empty: any
    size_t maxMessage = 32 * 1024 * 1024;  // bytes in one request
    int maxClients = 256;
};

class RelayListener {
public:
    RelayListener(UltraDbRelayServer& server, ListenOptions options);
    ~RelayListener();

    // Binds and listens; false with `error`.
    bool Start(std::string& error);
    // The port bound (the one asked for, or the one given for 0).
    int Port() const { return port_; }
    // Accepts until Stop(); call on a thread of its own or as the main loop.
    void Run();
    void Stop();

private:
    void Serve(int socket, std::string peer);

    UltraDbRelayServer& server_;
    ListenOptions options_;
    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> stopping_{ false };
    std::atomic<int> clients_{ 0 };
    std::mutex socketsMutex_;
    std::set<int> sockets_;               // the clients', shut on Stop
};

// The handshake's Sec-WebSocket-Accept for a Sec-WebSocket-Key (SHA-1 and
// base64, as RFC 6455 4.2.2 says) - exposed for the tests.
std::string WebSocketAccept(const std::string& key);

} // namespace UltraDBAdminRelay
