// Apps/UltraDBAdmin/relay/RelayWebSocket.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "RelayWebSocket.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <map>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace UltraDBAdminRelay {
namespace {

// ===== SHA-1 (FIPS 180-4) and base64: the handshake's =====

uint32_t Rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::array<uint8_t, 20> Sha1(const std::string& input) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    std::string m = input;
    const uint64_t bits = static_cast<uint64_t>(input.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
    for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4 + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4 + 3]));
        for (int i = 16; i < 80; ++i) w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t t = Rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = Rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - j * 8));
    return out;
}

std::string Base64(const uint8_t* data, size_t size) {
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (i + 1 < size ? data[i + 1] << 8 : 0) |
                           (i + 2 < size ? data[i + 2] : 0);
        out += a[(n >> 18) & 63];
        out += a[(n >> 12) & 63];
        out += i + 1 < size ? a[(n >> 6) & 63] : '=';
        out += i + 2 < size ? a[n & 63] : '=';
    }
    return out;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool SendAll(int socket, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t n = ::send(socket, p, size, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool ReadAll(int socket, void* data, size_t size) {
    char* p = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t n = ::recv(socket, p, size, 0);
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

// One client: its socket, written by many request threads.
struct Client {
    int socket = -1;
    std::mutex write;
    std::atomic<bool> open{ true };
    std::atomic<int> inFlight{ 0 };

    bool Frame(uint8_t opcode, const std::string& payload) {
        std::lock_guard<std::mutex> lock(write);
        if (!open) return false;
        std::string header;
        header += static_cast<char>(0x80 | opcode);   // FIN, unmasked: a server's frames are
        if (payload.size() < 126) {
            header += static_cast<char>(payload.size());
        } else if (payload.size() <= 0xFFFF) {
            header += static_cast<char>(126);
            header += static_cast<char>((payload.size() >> 8) & 0xFF);
            header += static_cast<char>(payload.size() & 0xFF);
        } else {
            header += static_cast<char>(127);
            for (int i = 7; i >= 0; --i) header += static_cast<char>((static_cast<uint64_t>(payload.size()) >> (i * 8)) & 0xFF);
        }
        return SendAll(socket, header.data(), header.size()) && SendAll(socket, payload.data(), payload.size());
    }
};

std::string HttpAnswer(int code, const char* status, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\nContent-Type: text/plain\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

} // namespace

std::string WebSocketAccept(const std::string& key) {
    const auto digest = Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return Base64(digest.data(), digest.size());
}

RelayListener::RelayListener(UltraDbRelayServer& server, ListenOptions options)
    : server_(server), options_(std::move(options)) {}

RelayListener::~RelayListener() {
    Stop();
    // The clients' threads end once their sockets are shut; wait for them.
    for (int i = 0; i < 500 && clients_ > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

bool RelayListener::Start(std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* found = nullptr;
    const std::string port = std::to_string(options_.port);
    if (getaddrinfo(options_.host.c_str(), port.c_str(), &hints, &found) != 0 || !found) {
        error = "cannot resolve " + options_.host;
        return false;
    }
    for (addrinfo* a = found; a && listener_ < 0; a = a->ai_next) {
        const int s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        const int yes = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (::bind(s, a->ai_addr, a->ai_addrlen) == 0 && ::listen(s, 64) == 0) {
            listener_ = s;
            sockaddr_storage bound{};
            socklen_t length = sizeof bound;
            getsockname(s, reinterpret_cast<sockaddr*>(&bound), &length);
            port_ = ntohs(bound.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port
                                                      : reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
        } else {
            ::close(s);
        }
    }
    freeaddrinfo(found);
    if (listener_ < 0) {
        error = "cannot listen on " + options_.host + ":" + port + ": " + std::strerror(errno);
        return false;
    }
    return true;
}

void RelayListener::Stop() {
    stopping_ = true;
}

void RelayListener::Run() {
    while (!stopping_) {
        pollfd p{ listener_, POLLIN, 0 };
        if (::poll(&p, 1, 200) <= 0) continue;
        sockaddr_storage from{};
        socklen_t length = sizeof from;
        const int s = ::accept(listener_, reinterpret_cast<sockaddr*>(&from), &length);
        if (s < 0) continue;
        if (clients_ >= options_.maxClients) {
            const std::string busy = HttpAnswer(503, "Service Unavailable", "too many clients\n");
            SendAll(s, busy.data(), busy.size());
            ::close(s);
            continue;
        }
        char host[NI_MAXHOST] = "";
        getnameinfo(reinterpret_cast<sockaddr*>(&from), length, host, sizeof host, nullptr, 0, NI_NUMERICHOST);
        ++clients_;
        {
            std::lock_guard<std::mutex> lock(socketsMutex_);
            sockets_.insert(s);
        }
        std::thread([this, s, peer = std::string(host)]() {
            Serve(s, peer);
            {
                std::lock_guard<std::mutex> lock(socketsMutex_);
                sockets_.erase(s);
            }
            ::close(s);
            --clients_;
        }).detach();
    }
    // Stopping: every client's socket shut, so its thread ends.
    {
        std::lock_guard<std::mutex> lock(socketsMutex_);
        for (int s : sockets_) ::shutdown(s, SHUT_RDWR);
    }
    for (int i = 0; i < 500 && clients_ > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ::close(listener_);
    listener_ = -1;
}

void RelayListener::Serve(int socket, std::string peer) {
    // ----- the HTTP request -----
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        if (request.size() > 16384) return;
        pollfd p{ socket, POLLIN, 0 };
        if (::poll(&p, 1, 10000) <= 0) return;     // 10 s to say what it wants
        const ssize_t n = ::recv(socket, buffer, sizeof buffer, 0);
        if (n <= 0) return;
        request.append(buffer, static_cast<size_t>(n));
    }
    const size_t lineEnd = request.find("\r\n");
    const std::string line = request.substr(0, lineEnd);
    std::map<std::string, std::string> headers;
    for (size_t at = lineEnd + 2; at < request.size();) {
        const size_t end = request.find("\r\n", at);
        if (end == std::string::npos || end == at) break;
        const std::string header = request.substr(at, end - at);
        const size_t colon = header.find(':');
        if (colon != std::string::npos) {
            size_t v = colon + 1;
            while (v < header.size() && header[v] == ' ') ++v;
            headers[Lower(header.substr(0, colon))] = header.substr(v);
        }
        at = end + 2;
    }
    const size_t space = line.find(' ');
    const size_t space2 = line.find(' ', space + 1);
    const std::string method = line.substr(0, space);
    const std::string path = space == std::string::npos ? std::string() : line.substr(space + 1, space2 - space - 1);
    auto answer = [socket](const std::string& text) { SendAll(socket, text.data(), text.size()); };
    if (method == "GET" && path == "/health") return answer(HttpAnswer(200, "OK", "ok\n"));
    if (method != "GET" || path != options_.path) return answer(HttpAnswer(404, "Not Found", "not here\n"));
    if (Lower(headers["upgrade"]) != "websocket" || headers["sec-websocket-key"].empty() ||
        headers["sec-websocket-version"] != "13")
        return answer(HttpAnswer(426, "Upgrade Required", "a WebSocket (version 13) is expected here\n"));
    // A page from elsewhere does not speak to the relay with its visitor's cookies.
    if (!options_.origins.empty()) {
        bool allowed = false;
        for (const std::string& origin : options_.origins) allowed = allowed || headers["origin"] == origin;
        if (!allowed) return answer(HttpAnswer(403, "Forbidden", "this page's origin is not allowed\n"));
    }
    answer("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
           WebSocketAccept(headers["sec-websocket-key"]) + "\r\n\r\n");

    // ----- messages -----
    auto client = std::make_shared<Client>();
    client->socket = socket;
    const uint64_t session = server_.OpenSession(peer);
    std::string message;
    bool messageOpen = false;
    while (!stopping_) {
        uint8_t head[2];
        if (!ReadAll(socket, head, 2)) break;
        const uint8_t opcode = head[0] & 0x0F;
        const bool fin = (head[0] & 0x80) != 0;
        const bool masked = (head[1] & 0x80) != 0;
        uint64_t size = head[1] & 0x7F;
        if (size == 126) {
            uint8_t ext[2];
            if (!ReadAll(socket, ext, 2)) break;
            size = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        } else if (size == 127) {
            uint8_t ext[8];
            if (!ReadAll(socket, ext, 8)) break;
            size = 0;
            for (uint8_t b : ext) size = (size << 8) | b;
        }
        if (!masked || size > options_.maxMessage || message.size() + size > options_.maxMessage) break;
        uint8_t mask[4];
        if (!ReadAll(socket, mask, 4)) break;
        std::string payload(static_cast<size_t>(size), '\0');
        if (size > 0 && !ReadAll(socket, payload.data(), payload.size())) break;
        for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
        if (opcode == 0x8) {                       // close: answered, and done
            client->Frame(0x8, payload.substr(0, 2));
            break;
        }
        if (opcode == 0x9) {                       // ping
            client->Frame(0xA, payload);
            continue;
        }
        if (opcode == 0xA) continue;               // pong
        if (opcode == 0x2) break;                  // binary: not this protocol
        if (opcode == 0x1) {
            message = std::move(payload);
            messageOpen = !fin;
        } else if (opcode == 0x0 && messageOpen) {
            message += payload;
            messageOpen = !fin;
        } else {
            break;
        }
        if (messageOpen) continue;
        // A request on a thread of its own: a cancel must not wait for the
        // query it stops.
        ++client->inFlight;
        std::thread([this, client, session, request = std::move(message)]() {
            const std::string response = server_.Handle(session, request);
            client->Frame(0x1, response);
            --client->inFlight;
        }).detach();
        message.clear();
    }
    // Gone: its session ended (its queries stopped), then its requests let finish.
    server_.CloseSession(session);
    client->open = false;
    for (int i = 0; i < 3000 && client->inFlight > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

} // namespace UltraDBAdminRelay
