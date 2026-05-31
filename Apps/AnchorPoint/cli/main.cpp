// Apps/AnchorPoint/cli/main.cpp
// AnchorPoint CLI - headless send/receive for testing the M1 transfer core.
// Version: 0.1.0
// Author: AnchorPoint
//
// Usage:
//   anchorpoint send <file> --to <host>[:port]      connect to a listener, push
//   anchorpoint send <file> --listen [--port N]     wait for a peer, then push
//   anchorpoint recv [--out DIR] --listen [--port N] wait for a sender
//   anchorpoint recv [--out DIR] --from <host>[:port] connect to a listening sender
//
// The connection role (listen/connect) is independent of the data role
// (send/recv), mirroring how NAT-traversed sessions will work later.
#include "../net/Protocol.h"
#include "../net/Transport.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace AnchorPoint;

namespace {

constexpr uint16_t kDefaultPort = 5040;

struct Args {
    std::string role;        // "send" | "recv"
    std::string file;        // for send
    std::string outDir = ".";
    std::string host;        // for connect modes
    uint16_t port = kDefaultPort;
    bool listen = false;
    bool connect = false;
};

void SplitHostPort(const std::string& s, std::string& host, uint16_t& port) {
    size_t colon = s.find_last_of(':');
    if (colon != std::string::npos && s.find(':') == colon) { // single colon → host:port
        host = s.substr(0, colon);
        port = static_cast<uint16_t>(std::stoi(s.substr(colon + 1)));
    } else {
        host = s;
    }
}

void PrintProgress(uint64_t done, uint64_t total) {
    if (total == 0) return;
    int pct = static_cast<int>((done * 100) / total);
    std::fprintf(stderr, "\r  %3d%%  (%llu / %llu bytes)", pct,
                 static_cast<unsigned long long>(done),
                 static_cast<unsigned long long>(total));
    std::fflush(stderr);
}

int Usage() {
    std::cerr <<
        "AnchorPoint CLI (M1)\n"
        "  anchorpoint send <file> --to <host>[:port]\n"
        "  anchorpoint send <file> --listen [--port N]\n"
        "  anchorpoint recv [--out DIR] --listen [--port N]\n"
        "  anchorpoint recv [--out DIR] --from <host>[:port]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();

    Args a;
    a.role = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (arg == "--to")        { a.connect = true; SplitHostPort(next(), a.host, a.port); }
        else if (arg == "--from") { a.connect = true; SplitHostPort(next(), a.host, a.port); }
        else if (arg == "--listen") a.listen = true;
        else if (arg == "--port")   a.port = static_cast<uint16_t>(std::stoi(next()));
        else if (arg == "--out")    a.outDir = next();
        else if (arg[0] != '-' && a.role == "send" && a.file.empty()) a.file = arg;
        else { std::cerr << "unknown arg: " << arg << "\n"; return Usage(); }
    }

    if (a.role != "send" && a.role != "recv") return Usage();
    if (a.role == "send" && a.file.empty()) { std::cerr << "send: missing <file>\n"; return Usage(); }
    if (a.listen == a.connect) { std::cerr << "specify exactly one of --listen / --to / --from\n"; return Usage(); }

    // Establish the connection.
    std::string err;
    std::unique_ptr<IConnection> conn;
    if (a.listen) {
        auto listener = Listen(a.port, &err);
        if (!listener) { std::cerr << "listen failed: " << err << "\n"; return 1; }
        std::cerr << "Listening on port " << listener->Port() << " ... (waiting for peer)\n";
        conn = listener->Accept();
        if (!conn) { std::cerr << "accept failed\n"; return 1; }
        std::cerr << "Peer connected: " << conn->PeerAddress() << "\n";
    } else {
        std::cerr << "Connecting to " << a.host << ":" << a.port << " ...\n";
        conn = Connect(a.host, a.port, &err);
        if (!conn) { std::cerr << "connect failed: " << err << "\n"; return 1; }
        std::cerr << "Connected to " << conn->PeerAddress() << "\n";
    }

    if (a.role == "send") {
        auto res = SendFile(*conn, a.file, "anchorpoint-cli", PrintProgress);
        std::fprintf(stderr, "\n");
        if (!res.ok) { std::cerr << "Send failed: " << res.error << "\n"; return 1; }
        std::cerr << "Sent " << res.bytes << " bytes, verified OK.\n";
        return 0;
    } else {
        auto res = ReceiveFile(*conn,
            [&](const OfferInfo& offer, const std::string& peer) -> std::string {
                std::cerr << "Incoming: \"" << offer.fileName << "\" ("
                          << offer.fileSize << " bytes) from " << peer << "\n";
                std::string out = a.outDir;
                if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
                out += offer.fileName;
                return out;
            },
            PrintProgress);
        std::fprintf(stderr, "\n");
        if (!res.ok) { std::cerr << "Receive failed: " << res.error << "\n"; return 1; }
        std::cerr << "Received " << res.bytes << " bytes, integrity verified.\n";
        return 0;
    }
}
