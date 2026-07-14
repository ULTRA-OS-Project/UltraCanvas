// Apps/AnchorPoint/net/Transport.h
// AnchorPoint - peer-to-peer connection abstraction
// Version: 0.1.0
// Author: AnchorPoint
//
// This interface deliberately mirrors the shape of the forthcoming UltraNet
// socket API (UltraNet/UltraNetSocket.h). Today it is backed by raw POSIX /
// Winsock sockets (RawSocketTransport.cpp); once UltraNet ships its TCP/UDP
// core, an UltraNetTransport backend can implement the same interface and the
// rest of AnchorPoint (Protocol, GUI, CLI) keeps working unchanged.
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace AnchorPoint {

// A reliable, ordered, bidirectional byte stream (TCP today).
class IConnection {
public:
    virtual ~IConnection() = default;

    // Sends exactly `len` bytes. Returns false on any error / disconnect.
    virtual bool SendAll(const void* data, size_t len) = 0;

    // Reads exactly `len` bytes into `data`. Returns false on EOF or error.
    virtual bool RecvAll(void* data, size_t len) = 0;

    virtual void Close() = 0;

    // Human-readable "ip:port" of the remote peer (best effort).
    virtual std::string PeerAddress() const = 0;
};

// Accepts inbound connections on a bound port.
class IListener {
public:
    virtual ~IListener() = default;

    // Blocks until a peer connects. Returns nullptr on error / Close().
    virtual std::unique_ptr<IConnection> Accept() = 0;

    virtual uint16_t Port() const = 0;
    virtual void Close() = 0;
};

// ---- Factory (raw-socket backend today; UltraNet backend later) ----------

// Connects to host:port. On failure returns nullptr and, if `err` is non-null,
// fills it with a description.
std::unique_ptr<IConnection> Connect(const std::string& host, uint16_t port,
                                     std::string* err = nullptr);

// Binds and listens on `port` (0 = let the OS choose; query via Port()).
std::unique_ptr<IListener> Listen(uint16_t port, std::string* err = nullptr);

} // namespace AnchorPoint
