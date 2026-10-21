// include/UltraRTC/UltraRTCTypes.h
// UltraRTC - types of the real-time communication API (WebRTC peers, data channels)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Every object with a lifetime (peer, data channel) is an opaque handle.
using UltraRtcHandle = uint64_t;
constexpr UltraRtcHandle UltraRtcInvalidHandle = 0;

// ============================================================================
// RESULTS
// ============================================================================

enum class UltraRtcResultCode {
    Success,
    NotInitialized,
    InvalidHandle,
    InvalidArgument,
    InvalidState,
    InvalidSdp,
    IceFailed,
    DtlsFailed,
    Unsupported,        // No engine in this build (OpenSSL was not found)
    Unknown
};

struct UltraRtcResult {
    UltraRtcResultCode code = UltraRtcResultCode::Unknown;
    bool               success = false;
    std::string        message;

    operator bool() const { return success; }

    static UltraRtcResult Ok() { return {UltraRtcResultCode::Success, true, std::string()}; }
    static UltraRtcResult Error(UltraRtcResultCode code, const std::string& message) {
        return {code, false, message};
    }
};

// ============================================================================
// CONFIGURATION
// ============================================================================

struct UltraRtcIceServer {
    // "stun:stun.example.org:3478", "turn:turn.example.org:3478?transport=udp",
    // "turns:turn.example.org:5349"
    std::string url;
    std::string username;       // TURN only
    std::string credential;     // TURN only
};

enum class UltraRtcIceTransportPolicy {
    All,                        // host, server-reflexive and relayed candidates
    Relay                       // relayed (TURN) only: hides the local addresses
};

struct UltraRtcPeerConfig {
    std::vector<UltraRtcIceServer> iceServers;
    UltraRtcIceTransportPolicy iceTransportPolicy = UltraRtcIceTransportPolicy::All;
    // true: adding a data channel produces an offer, a remote offer produces
    // an answer, each as a LocalDescription event, with no call needed.
    // false: call UltraRtc_SetLocalDescription yourself.
    bool        autoNegotiation = true;
    bool        enableIceTcp = false;
    uint16_t    portRangeBegin = 0;     // 0 = any port
    uint16_t    portRangeEnd = 0;
    std::string bindAddress;            // "" = every interface
};

enum class UltraRtcSdpType {
    Unspecified,                // UltraRtc_SetLocalDescription: whichever the state needs
    Offer,
    Answer,
    Pranswer,
    Rollback
};

struct UltraRtcDataChannelInit {
    bool        ordered = true;
    int         maxRetransmits = -1;        // -1 = reliable
    int         maxPacketLifeTimeMs = -1;   // -1 = reliable
    std::string protocol;
    int         negotiatedId = -1;          // -1 = announced in-band
};

// ============================================================================
// STATES (W3C spellings)
// ============================================================================

enum class UltraRtcConnectionState { New, Connecting, Connected, Disconnected, Failed, Closed };
enum class UltraRtcIceGatheringState { New, InProgress, Complete };
enum class UltraRtcSignalingState { Stable, HaveLocalOffer, HaveRemoteOffer, HaveLocalPranswer, HaveRemotePranswer };
enum class UltraRtcDataChannelState { Connecting, Open, Closing, Closed };

// ============================================================================
// EVENTS
// ============================================================================

enum class UltraRtcPeerEventType {
    LocalDescription,       // sdpType, sdp: send to the other side
    IceCandidate,           // candidate, sdpMid: send to the other side
    IceGatheringStateChange,// gatheringState
    SignalingStateChange,   // signalingState
    ConnectionStateChange,  // connectionState
    DataChannel,            // channel, channelLabel: the other side opened a channel
    DataChannelOpen,        // channel
    DataChannelClose,       // channel
    DataChannelMessage,     // channel, isBinary, text or binary
    DataChannelError        // channel, error
};

struct UltraRtcPeerEvent {
    UltraRtcPeerEventType     type = UltraRtcPeerEventType::LocalDescription;
    UltraRtcHandle            peer = UltraRtcInvalidHandle;
    UltraRtcHandle            channel = UltraRtcInvalidHandle;
    UltraRtcSdpType           sdpType = UltraRtcSdpType::Unspecified;
    std::string               sdp;
    std::string               candidate;
    std::string               sdpMid;
    UltraRtcConnectionState   connectionState = UltraRtcConnectionState::New;
    UltraRtcIceGatheringState gatheringState = UltraRtcIceGatheringState::New;
    UltraRtcSignalingState    signalingState = UltraRtcSignalingState::Stable;
    std::string               channelLabel;
    bool                      isBinary = false;
    std::string               text;
    std::vector<uint8_t>      binary;
    std::string               error;
};

struct UltraRtcCapabilities {
    bool        available = false;      // An engine is compiled in
    std::string engine;                 // "libdatachannel 0.24.6"
    bool        dataChannels = false;
    bool        audio = false;
    bool        video = false;
};

using UltraRtcPeerEventCallback = std::function<void(const UltraRtcPeerEvent& event)>;
using UltraRtcDispatcher        = std::function<void(std::function<void()> task)>;
