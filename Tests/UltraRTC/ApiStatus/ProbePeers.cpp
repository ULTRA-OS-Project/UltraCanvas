// Tests/UltraRTC/ApiStatus/ProbePeers.cpp
// UltraRtcApiStatus: peers - creating and closing them, the offer and the
// answer, ICE candidates, and the three state machines.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <atomic>
#include <memory>
#include <thread>

using namespace ultrartc_apistatus;

namespace {

// A peer of its own for one probe: counts its events, remembers the type of
// the last local description, and is closed when the probe returns.
struct LonePeer {
    struct Seen {
        std::atomic<int> events{0};
        std::atomic<int> descriptions{0};
        std::atomic<int> lastSdpType{-1};
    };
    std::shared_ptr<Seen> seen = std::make_shared<Seen>();
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcResult created;

    explicit LonePeer(bool autoNegotiation = true) {
        UltraRtcPeerConfig config;
        config.autoNegotiation = autoNegotiation;
        created = UltraRtc_CreatePeer(config, Callback(seen), peer);
    }
    ~LonePeer() {
        if (peer != UltraRtcInvalidHandle) UltraRtc_ClosePeer(peer);
    }
    static UltraRtcPeerEventCallback Callback(std::shared_ptr<Seen> into) {
        return [into](const UltraRtcPeerEvent& e) {
            ++into->events;
            if (e.type == UltraRtcPeerEventType::LocalDescription) {
                into->lastSdpType = static_cast<int>(e.sdpType);
                ++into->descriptions;
            }
        };
    }
};

const char* Name(UltraRtcConnectionState s) {
    switch (s) {
        case UltraRtcConnectionState::New:          return "New";
        case UltraRtcConnectionState::Connecting:   return "Connecting";
        case UltraRtcConnectionState::Connected:    return "Connected";
        case UltraRtcConnectionState::Disconnected: return "Disconnected";
        case UltraRtcConnectionState::Failed:       return "Failed";
        case UltraRtcConnectionState::Closed:       return "Closed";
    }
    return "?";
}

} // namespace

ULTRARTC_PROBE("Peers", UltraRtc_CreatePeer) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(pair.a.peer != UltraRtcInvalidHandle && pair.b.peer != UltraRtcInvalidHandle);
    PROBE_EXPECT(pair.a.peer != pair.b.peer);

    UltraRtcPeerConfig bad;
    bad.iceServers.push_back({"turn:", "", ""});
    UltraRtcHandle refused = 7;
    const UltraRtcResult r = UltraRtc_CreatePeer(bad, nullptr, refused);
    if (r) UltraRtc_ClosePeer(refused);
    return Working(std::string("two peers of this process created and connected over host candidates") +
                   (r ? "" : "; a malformed ICE server URL is refused"));
}

ULTRARTC_PROBE("Peers", UltraRtc_ClosePeer) {
    REQUIRE_ENGINE();
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcResult created = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, peer);
    PROBE_EXPECT_MSG(created, created.message);
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreateDataChannel(peer, "closing", UltraRtcDataChannelInit(), channel));

    PROBE_EXPECT(UltraRtc_ClosePeer(peer));
    PROBE_EXPECT_MSG(UltraRtc_ClosePeer(peer).code == UltraRtcResultCode::InvalidHandle,
                     "closing a closed peer did not answer InvalidHandle");
    PROBE_EXPECT_MSG(UltraRtc_DataChannelSendText(channel, "late").code == UltraRtcResultCode::InvalidHandle,
                     "the peer's data channel outlived it");
    PROBE_EXPECT(UltraRtc_GetConnectionState(peer) == UltraRtcConnectionState::Closed);
    return Working("the peer and its data channel are gone; a second close answers InvalidHandle");
}

ULTRARTC_PROBE("Peers", UltraRtc_SetPeerEventCallback) {
    REQUIRE_ENGINE();
    LonePeer lone;
    PROBE_EXPECT_MSG(lone.created, lone.created.message);
    auto replacement = std::make_shared<LonePeer::Seen>();
    PROBE_EXPECT(UltraRtc_SetPeerEventCallback(lone.peer, LonePeer::Callback(replacement)));
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreateDataChannel(lone.peer, "callback", UltraRtcDataChannelInit(), channel));
    PROBE_EXPECT_MSG(WaitUntil([&]() { return replacement->events.load() > 0; }),
                     "the replacement callback received no event within 5 s");
    PROBE_EXPECT_MSG(lone.seen->events.load() == 0, "the replaced callback was still called");
    PROBE_EXPECT(UltraRtc_SetPeerEventCallback(UltraRtcInvalidHandle, nullptr).code ==
                 UltraRtcResultCode::InvalidHandle);
    return Working("the replacement receives the peer's events, the callback it replaced none");
}

ULTRARTC_PROBE("Peers", UltraRtc_SetLocalDescription) {
    REQUIRE_ENGINE();
    LonePeer lone(false);       // autoNegotiation off: nothing happens by itself
    PROBE_EXPECT_MSG(lone.created, lone.created.message);
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreateDataChannel(lone.peer, "manual", UltraRtcDataChannelInit(), channel));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    PROBE_EXPECT_MSG(lone.seen->descriptions.load() == 0,
                     "an offer was produced although autoNegotiation is off");

    UltraRtcResult r = UltraRtc_SetLocalDescription(lone.peer);
    PROBE_EXPECT_MSG(r, r.message);
    PROBE_EXPECT_MSG(WaitUntil([&]() { return lone.seen->descriptions.load() > 0; }),
                     "no LocalDescription event within 5 s");
    PROBE_EXPECT_MSG(lone.seen->lastSdpType.load() == static_cast<int>(UltraRtcSdpType::Offer),
                     "the description produced was not an offer");
    PROBE_EXPECT(UltraRtc_SetLocalDescription(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    return Working("with autoNegotiation off nothing is offered until it is called; then the offer arrives as a "
                   "LocalDescription event");
}

ULTRARTC_PROBE("Peers", UltraRtc_SetRemoteDescription) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        PROBE_EXPECT_MSG(pair.a.descriptionsSent > 0 && pair.b.descriptionsSent > 0,
                         "the pair connected without an offer and an answer");
        PROBE_EXPECT_MSG(pair.a.descriptionsRefused == 0 && pair.b.descriptionsRefused == 0,
                         "a description was refused: " + pair.a.lastRefusal + pair.b.lastRefusal);
    }
    LonePeer lone;
    PROBE_EXPECT_MSG(lone.created, lone.created.message);
    const UltraRtcResult garbage = UltraRtc_SetRemoteDescription(lone.peer, UltraRtcSdpType::Offer, "this is not SDP");
    PROBE_EXPECT_MSG(!garbage, "text that is not SDP was accepted");
    return Working("the offer and the answer were applied on a pair that then connected; text that is not SDP is "
                   "refused");
}

ULTRARTC_PROBE("Peers", UltraRtc_AddIceCandidate) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    int sent, refused;
    std::string why;
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        sent = pair.a.candidatesSent + pair.b.candidatesSent;
        refused = pair.a.candidatesRefused + pair.b.candidatesRefused;
        why = pair.a.lastRefusal + pair.b.lastRefusal;
    }
    PROBE_EXPECT_MSG(refused == 0, std::to_string(refused) + " candidate(s) of the other side were refused: " + why);
    PROBE_EXPECT(UltraRtc_AddIceCandidate(UltraRtcInvalidHandle, "candidate:1 1 UDP 1 127.0.0.1 9 typ host", "0").code ==
                 UltraRtcResultCode::InvalidHandle);
    if (sent == 0) return Implemented("the pair connected without a trickled candidate, so none was applied");
    return Working(std::to_string(sent) + " candidates of the other side were applied on a pair that then connected");
}

ULTRARTC_PROBE("Peers", UltraRtc_GetLocalDescription) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    UltraRtcSdpType typeA = UltraRtcSdpType::Unspecified, typeB = UltraRtcSdpType::Unspecified;
    std::string sdpA, sdpB;
    PROBE_EXPECT(UltraRtc_GetLocalDescription(pair.a.peer, typeA, sdpA));
    PROBE_EXPECT(UltraRtc_GetLocalDescription(pair.b.peer, typeB, sdpB));
    PROBE_EXPECT_MSG(typeA == UltraRtcSdpType::Offer && typeB == UltraRtcSdpType::Answer,
                     "the caller's is not the offer or the callee's not the answer");
    PROBE_EXPECT_MSG(sdpA.find("a=fingerprint:") != std::string::npos, "no DTLS fingerprint in the SDP");

    LonePeer lone(false);
    UltraRtcSdpType none;
    std::string empty;
    PROBE_EXPECT_MSG(!UltraRtc_GetLocalDescription(lone.peer, none, empty),
                     "a peer that never negotiated answered with a description");
    return Working("the caller's offer and the callee's answer, each with its DTLS fingerprint; nothing before "
                   "negotiation");
}

ULTRARTC_PROBE("Peers", UltraRtc_GetRemoteDescription) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    UltraRtcSdpType typeA = UltraRtcSdpType::Unspecified, typeB = UltraRtcSdpType::Unspecified;
    std::string sdpA, sdpB;
    PROBE_EXPECT(UltraRtc_GetRemoteDescription(pair.a.peer, typeA, sdpA));
    PROBE_EXPECT(UltraRtc_GetRemoteDescription(pair.b.peer, typeB, sdpB));
    PROBE_EXPECT_MSG(typeA == UltraRtcSdpType::Answer && typeB == UltraRtcSdpType::Offer,
                     "the caller does not hold the answer or the callee not the offer");
    PROBE_EXPECT_MSG(sdpB.find("a=fingerprint:") != std::string::npos, "no DTLS fingerprint in the SDP");
    return Working("the caller holds the callee's answer and the callee the caller's offer");
}

ULTRARTC_PROBE("Peers", UltraRtc_GetConnectionState) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    const UltraRtcConnectionState a = UltraRtc_GetConnectionState(pair.a.peer);
    const UltraRtcConnectionState b = UltraRtc_GetConnectionState(pair.b.peer);
    PROBE_EXPECT_MSG(a == UltraRtcConnectionState::Connected && b == UltraRtcConnectionState::Connected,
                     std::string("a connected pair reads ") + Name(a) + " / " + Name(b));
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        PROBE_EXPECT_MSG(pair.a.connected && pair.b.connected, "no ConnectionStateChange event said Connected");
    }
    LonePeer lone;
    PROBE_EXPECT(UltraRtc_GetConnectionState(lone.peer) == UltraRtcConnectionState::New);
    PROBE_EXPECT(UltraRtc_GetConnectionState(UltraRtcInvalidHandle) == UltraRtcConnectionState::Closed);
    return Working("New on a fresh peer, Connected on both sides of the pair (as the events said), Closed for an "
                   "unknown handle");
}

ULTRARTC_PROBE("Peers", UltraRtc_GetIceGatheringState) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    LonePeer lone;
    PROBE_EXPECT(UltraRtc_GetIceGatheringState(lone.peer) == UltraRtcIceGatheringState::New);
    const bool complete = WaitUntil([&]() {
        return UltraRtc_GetIceGatheringState(pair.a.peer) == UltraRtcIceGatheringState::Complete &&
               UltraRtc_GetIceGatheringState(pair.b.peer) == UltraRtcIceGatheringState::Complete;
    }, 10000);
    if (!complete) return Implemented("New on a fresh peer; the pair was still gathering after 10 s");
    return Working("New on a fresh peer, Complete on both sides once the host candidates were gathered");
}

ULTRARTC_PROBE("Peers", UltraRtc_GetSignalingState) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT_MSG(UltraRtc_GetSignalingState(pair.a.peer) == UltraRtcSignalingState::Stable &&
                         UltraRtc_GetSignalingState(pair.b.peer) == UltraRtcSignalingState::Stable,
                     "a negotiated pair is not Stable");
    LonePeer lone(false);
    PROBE_EXPECT_MSG(lone.created, lone.created.message);
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreateDataChannel(lone.peer, "offer", UltraRtcDataChannelInit(), channel));
    PROBE_EXPECT(UltraRtc_SetLocalDescription(lone.peer, UltraRtcSdpType::Offer));
    PROBE_EXPECT_MSG(WaitUntil([&]() {
        return UltraRtc_GetSignalingState(lone.peer) == UltraRtcSignalingState::HaveLocalOffer;
    }), "a peer with an offer out does not read HaveLocalOffer");
    return Working("HaveLocalOffer while an offer is out, Stable on both sides once it was answered");
}
