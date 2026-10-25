// Apps/UltraDBAdmin/relay/RelayWebSocket.h
// The relay's listener: a WebSocket server (RFC 6455, text messages) on one
// address and path, each connection a session of UltraDbRelayServer, each
// message a request answered on a thread of its own - so a cancel reaches
// the server while the query it stops still runs. GET /health answers "ok"
// for a load balancer. No TLS here: the relay listens on loopback behind a
// TLS proxy (RelayConfig.h), whose X-Forwarded-For names the visitor.
// Version: 0.3.0 - plain HTTP under the relay's path for the program (the sign-in)
// Version: 0.2.0 - the visitor's address from a proxy on this computer
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraDBAdminRelay {

// A plain HTTP request below the relay's path (/relay/signin...), for the
// program to answer.
struct HttpRequest {
    std::string method;
    std::string path;                               // without the query
    std::map<std::string, std::string> query;       // decoded
    std::map<std::string, std::string> headers;     // names in lower case
    std::string peer;                               // ClientAddress
};

struct HttpReply {
    int status = 200;
    std::string reason = "OK";
    std::string contentType = "text/html; charset=utf-8";
    std::map<std::string, std::string> headers;     // Location, Cache-Control...
    std::string body;
};

// A query string's parameters, %-decoded ("a=1&b=x%20y").
std::map<std::string, std::string> ParseQuery(const std::string& query);

struct ListenOptions {
    std::string host = "127.0.0.1";
    int port = 8765;                       // 0: any free port (the tests)
    std::string path = "/relay";
    std::vector<std::string> origins;      // allowed Origin headers; empty: any
    size_t maxMessage = 32 * 1024 * 1024;  // bytes in one request
    // Answers a GET below `path` (path + "/..."); false: not its, 404.
    std::function<bool(const HttpRequest&, HttpReply&)> http;
    int maxClients = 256;
};

// Who a session is, for the audit log: the address the connection came
// from, or - when that is this computer, a proxy in front - the last address
// its X-Forwarded-For names, the one that proxy added. A client cannot put
// itself in the log under another name by sending the header itself: from
// elsewhere it is ignored, and behind a proxy only the proxy's entry counts.
std::string ClientAddress(const std::string& peer, const std::string& forwardedFor);

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
