// Plugins/UltraIM/direct/DirectLinks.h
// ChatCube Direct - one WebRTC data channel per contact, set up through
// signalling the provider carries (relay WebSocket or local network)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraRTC/UltraRTCTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace UltraIMDirect {

class LinkManager {
public:
    // Signalling out: the provider signs `data` and delivers it to `to`.
    std::function<void(const std::string& to, const std::string& kind,
                       const std::string& session, const std::string& data)> sendSignal;
    std::function<void(const std::string& peerId)> onOpen;
    std::function<void(const std::string& peerId, const std::string& text)> onMessage;
    // The link closed or could not be built; queued messages go another way.
    std::function<void(const std::string& peerId)> onClosed;

    void SetLocalUserId(const std::string& userId);
    void SetIceServers(const std::vector<UltraRtcIceServer>& servers);

    // Starts a link to the peer unless one exists or is being built.
    void Ensure(const std::string& peerId);
    // A signalling message from `from` (already verified by the provider).
    void HandleSignal(const std::string& from, const std::string& kind,
                      const std::string& session, const std::string& data);
    bool IsOpen(const std::string& peerId);
    bool Send(const std::string& peerId, const std::string& text);
    void Close(const std::string& peerId);
    void CloseAll();
    // Gives up on links that did not open within timeoutMs (call periodically).
    void ExpireStale(int64_t nowMs, int64_t timeoutMs);

private:
    struct Link {
        std::string    peerId;
        std::string    session;
        UltraRtcHandle peer = UltraRtcInvalidHandle;
        UltraRtcHandle channel = UltraRtcInvalidHandle;
        bool           open = false;
        int64_t        startedMs = 0;
    };

    UltraRtcPeerEventCallback MakeHandler(const std::string& peerId, const std::string& session);
    void OnPeerEvent(const std::string& peerId, const std::string& session, const UltraRtcPeerEvent& event);
    // Creates the engine peer for a link (lock not held).
    bool StartPeer(const std::string& peerId, const std::string& session, UltraRtcHandle& outPeer);
    void Drop(const std::string& peerId, bool notify);

    std::mutex mutex_;
    std::string localUserId_;
    std::vector<UltraRtcIceServer> iceServers_;
    std::map<std::string, Link> links_;   // peer id -> link
};

} // namespace UltraIMDirect
