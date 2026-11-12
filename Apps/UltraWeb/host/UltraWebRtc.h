// Apps/UltraWeb/host/UltraWebRtc.h
// An app's peer connections and data channels (uc_rtc_* in
// UltraWeb/guest/ultraweb.h): the peer the bridge talks to, the rules for
// what an app may ask - its configuration read from JSON, which ICE servers
// it may name, TURN-only for an app from the network - and the service that
// runs them on UltraRTC.
//
// The bridge sees only this interface, so the browser runtime (Emscripten,
// no UltraRTC) builds it, and the bridge's tests can hand in a fake peer.
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

struct RtcIceServer {
    std::string url;          // stun:, turn: or turns:
    std::string username;
    std::string credential;
};

struct RtcConfig {
    std::vector<RtcIceServer> iceServers;
    // Relayed candidates only: the other side never learns this machine's
    // local addresses (an app from the network).
    bool relayOnly = false;
};

// What happens on a peer, in the ABI's terms (kind: UC_RTC_*). `channel` is
// the service's own id for a data channel, 0 when the event has none.
struct RtcEvent {
    uint32_t kind = 0;
    uint64_t channel = 0;
    uint32_t number = 0;
    std::string text;
};

class RtcPeer {
public:
    virtual ~RtcPeer() = default;
    // Each answers UC_OK or a UC_ERR_*.
    virtual int32_t SetRemote(uint32_t type, const std::string& sdp) = 0;   // UC_RTC_OFFER / ANSWER
    virtual int32_t AddCandidate(const std::string& candidate, const std::string& mid) = 0;
    virtual int32_t OpenChannel(const std::string& label, bool ordered, int32_t maxRetransmits,
                                uint64_t& outChannel) = 0;
    virtual int32_t Send(uint64_t channel, const std::vector<uint8_t>& data, bool binary) = 0;
    virtual int32_t CloseChannel(uint64_t channel) = 0;
    // Closes the peer and its channels; no event follows.
    virtual void Close() = 0;
};

// Opens a peer. `onEvent` runs on the UI thread, never after Close. Null
// when the peer could not be made (`error` says why).
using RtcService = std::function<std::shared_ptr<RtcPeer>(const RtcConfig& config,
                                                          std::function<void(RtcEvent)> onEvent,
                                                          std::string& error)>;

namespace RtcRules {

constexpr size_t kMaxPeers = 4;                   // per app
constexpr size_t kMaxChannels = 16;               // per peer
constexpr size_t kMaxMessageBytes = 256u << 10;
constexpr size_t kMaxQueuedEvents = 1000;         // per peer, waiting for uc_rtc_poll
constexpr size_t kMaxConfigBytes = 16384;
constexpr size_t kMaxIceServers = 8;
constexpr size_t kMaxLabelBytes = 256;
constexpr size_t kMaxSdpBytes = 64u << 10;
constexpr size_t kMaxCandidateBytes = 1024;

// Reads an app's configuration - {"iceServers": [{"urls": ..., "username":
// ..., "credential": ...}]}, "" for none - and decides relayOnly from the
// address the app came from. UC_OK, or UC_ERR_STATE with `reason`.
int32_t ParseConfig(const std::string& json, const std::string& appAddress, RtcConfig& out, std::string& reason);

} // namespace RtcRules

// The real service: UltraRTC, its events posted to the UI thread through
// `post` (PostToUIThread). Empty when this build has no WebRTC engine.
RtcService MakeEngineRtc(std::function<void(std::function<void()>)> post);

} // namespace UltraWeb
