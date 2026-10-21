# UltraRTC — real-time communication

WebRTC behind an UltraCanvas-owned API: peer connections, data channels and
(next) audio and video calls, as `UltraRtc_*` free functions with opaque
`UltraRtcHandle`s and an `UltraRtcResult` from every call. First user:
ChatCube Direct, for peer-to-peer chat and calls. Design:
[`Docs/Research/UltraRTCDesignProposal.md`](../../Research/UltraRTCDesignProposal.md);
engine choice and phases: [`Docs/Research/ChatCubeDirectPlan.md`](../../Research/ChatCubeDirectPlan.md).

| | |
|---|---|
| Headers | `<UltraRTC/UltraRTC.h>` (umbrella), `UltraRTCTypes.h`, `UltraRTCCore.h` |
| Sources | `UltraCanvas/core/UltraRTC/` (`UltraRTCEngine.cpp`; `UltraRTCStub.cpp` without an engine) |
| Target | `UltraRTC` (archive `uc-rtc`, absorbed into the shared core) |
| Option | `ULTRACANVAS_ENABLE_ULTRARTC` (ON) |
| Engine | libdatachannel 0.24.6 + libjuice, usrsctp, libsrtp, plog — vendored, `UltraCanvas/third_party/libdatachannel/` |
| Needs | OpenSSL (the engine's DTLS). Without it the module builds and every call answers `Unsupported` |
| Tests | `Tests/UltraRTC` (`-DULTRACANVAS_BUILD_ULTRARTC_TESTS=ON`, binary `UltraRTCTests`) |

## Rules

- **SDP and candidates pass through untouched.** Send the `sdp` of a
  `LocalDescription` event and the `candidate` / `sdpMid` of an
  `IceCandidate` event to the other side over your signalling channel as
  they are; hand what arrives to `UltraRtc_SetRemoteDescription` and
  `UltraRtc_AddIceCandidate` as it is.
- **DTLS is always on.** The remote certificate is checked against the
  fingerprint in the remote SDP, so a signalling channel that authenticates
  the SDP (ChatCube signs it with the user's identity key) authenticates the
  link end to end; a relay in the middle cannot substitute itself.
- **No engine type in a public header.** The engine can be replaced (the
  proposal's Rust tier) without touching a caller.
- **Threading:** the engine runs its own threads. Events go through the
  dispatcher installed with `UltraRtc_SetEventDispatcher`; without one they
  run on an engine thread and must not block. Every function may be called
  from any thread, also from inside an event callback (closing a peer from
  its own callback included). Events queued for a peer that was closed are
  dropped.

## Connecting two peers

```cpp
UltraRtc_Initialize();

UltraRtcPeerConfig config;
config.iceServers = {{"stun:stun.example.org:3478"},
                     {"turn:turn.example.org:3478", "user", "secret"}};

UltraRtcHandle peer;
UltraRtc_CreatePeer(config, [&](const UltraRtcPeerEvent& e) {
    switch (e.type) {
        case UltraRtcPeerEventType::LocalDescription: Signal("sdp", e.sdpType, e.sdp); break;
        case UltraRtcPeerEventType::IceCandidate:     Signal("ice", e.candidate, e.sdpMid); break;
        case UltraRtcPeerEventType::DataChannel:      remoteChannel = e.channel; break;   // the other side opened one
        case UltraRtcPeerEventType::DataChannelOpen:  /* ready */ break;
        case UltraRtcPeerEventType::DataChannelMessage: Show(e.isBinary ? Decode(e.binary) : e.text); break;
        default: break;
    }
}, peer);

// The caller opens a channel; with autoNegotiation (the default) that
// produces the offer as a LocalDescription event.
UltraRtcHandle channel;
UltraRtc_CreateDataChannel(peer, "chat", UltraRtcDataChannelInit(), channel);

// What the signalling channel delivers from the other side:
UltraRtc_SetRemoteDescription(peer, type, sdp);   // a remote offer produces our answer
UltraRtc_AddIceCandidate(peer, candidate, sdpMid);

UltraRtc_DataChannelSendText(channel, "hello");
UltraRtc_ClosePeer(peer);
UltraRtc_Shutdown();
```

With `autoNegotiation = false` call `UltraRtc_SetLocalDescription(peer)`
yourself once the channels are added and after each remote offer.

## Status

Phase 1 implemented: peers, ICE servers (STUN / TURN / TURNS), transport
policy, port range, the W3C state machines as events, data channels (ordered
or not, reliable or partially reliable, negotiated ids), text and binary
messages. Next: audio (Opus) and video tracks with device capture and
playback, then the `UltraCanvasVideoTile` / `UltraCanvasCallView` elements.
