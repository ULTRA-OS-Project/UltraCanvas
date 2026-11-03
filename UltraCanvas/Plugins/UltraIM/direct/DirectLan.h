// Plugins/UltraIM/direct/DirectLan.h
// ChatCube Direct - the local network without a relay: ChatCubes announce
// themselves over mDNS (_chatcube._tcp, TXT id= and name=), find each
// other, and talk over a TCP connection both ends authenticate with their
// identity keys. It carries what the relay's socket carries - signed
// signalling - so chat links and calls are set up the same way.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "DirectCrypto.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraIMDirect {

// A ChatCube seen on the local network (from its mDNS announcement, not yet
// proven: only a connection's handshake proves who is at the other end).
struct LanPeer {
    std::string userId;
    std::string displayName;
    std::string host;
    int         port = 0;
};

// Connection protocol, one JSON object per frame, each frame a 4-byte
// big-endian length and the bytes:
//   both:  {"t":"hello","v":1,"id":..,"pk":..,"name":..,"nonce":..}
//   both:  {"t":"auth","sig": Sign(LanAuthMessage(their nonce, me, them))}
//   then:  {"t":"ping"} / {"t":"pong"}, and the caller's frames ("signal")
// A hello whose id is not the hash of its key, or an auth that does not
// verify, closes the connection.
class LanTransport {
public:
    LanTransport();
    ~LanTransport();
    LanTransport(const LanTransport&) = delete;
    LanTransport& operator=(const LanTransport&) = delete;

    // Run on the transport's threads, never after Stop() returns. A
    // handler may call Send / IsConnected, not Start or Stop.
    std::function<void(const std::string& peerId, const std::vector<uint8_t>& publicKey,
                       const std::string& displayName)> onPeerUp;
    std::function<void(const std::string& peerId)> onPeerDown;
    std::function<void(const std::string& peerId, const UltraCanvas::JSONValue& frame)> onFrame;
    std::function<void()> onNearbyChanged;

    // Listens on a free port, announces us over mDNS when `advertise`, and
    // browses for others every `discoveryIntervalMs` (0: no browsing).
    // `identity` must outlive Stop().
    bool Start(const Identity* identity, const std::string& displayName, bool advertise,
               int discoveryIntervalMs, std::string& error);
    void Stop();
    bool IsRunning() const;
    int  GetPort() const;
    // Whether mDNS is there at all (the plug-in and the system's responder).
    bool HasDiscovery() const;

    bool IsConnected(const std::string& peerId) const;
    bool Send(const std::string& peerId, const UltraCanvas::JSONValue& frame);

    // Users to stay connected to whenever they are seen nearby (contacts).
    void SetWanted(const std::set<std::string>& userIds);
    void AddWanted(const std::string& userId);
    // An address to connect to without discovery (typed in by a person, or a
    // test), tried until a connection there is up.
    void AddAddress(const std::string& host, int port);

    std::vector<LanPeer> Nearby();

    struct Core;        // shared with the transport's threads (defined in the .cpp)

private:
    std::shared_ptr<Core> core_;
};

} // namespace UltraIMDirect
