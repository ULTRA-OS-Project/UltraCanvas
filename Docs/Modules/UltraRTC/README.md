# UltraRTC — real-time communication

WebRTC behind an UltraCanvas-owned API: peer connections, data channels,
audio and video calls, as `UltraRtc_*` free functions with opaque
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
| Needs | OpenSSL (the engine's DTLS). Without it the module builds and every call answers `Unsupported`. Audio also needs libopus (pkg-config `opus`); without it `caps.audio` is false and `UltraRtc_AddAudioTrack` answers `Unsupported`. Video needs FFmpeg's libavcodec and libswscale (VideoFX's dependency) with a VP8 encoder (libvpx); without them `caps.video` is false |
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

## Audio

An audio track is Opus at 48 kHz, mono, 20 ms frames, with in-band FEC: a
lost packet is rebuilt from the next one where it can be, and concealed
otherwise. Both sides add the track, with the same mid, before the offer is
made (and before the remote offer is applied), so the two share one
send-and-receive m-line. Media alone does not trigger auto-negotiation: the
caller asks with `UltraRtc_SetLocalDescription(peer, Offer)`; the answer
follows by itself.

```cpp
UltraRtcHandle track = UltraRtcInvalidHandle;
UltraRtc_AddAudioTrack(peer, "audio", UltraRtcDirection::SendRecv, track);
UltraRtc_SetLocalDescription(peer, UltraRtcSdpType::Offer);   // the caller only

// On the TrackOpen event for `track`: the microphone and the speaker.
UltraRtc_StartMicrophone(track);       // "" = the default device
UltraRtc_StartPlayback(track);
UltraRtc_SetTrackEnabled(track, false);   // mute
```

`UltraRtc_SendAudioFrame` / `UltraRtc_SetAudioFrameCallback` feed and read
the track directly (tests, an application with its own audio): PCM of any
length goes in, decoded 20 ms frames come out on a media thread. The devices
are the framework's audio backend - `UltraCanvasAudioRecorder` in live mode
for the microphone, `UltraCanvasAudioPlayer`'s push sink for the speaker,
which drops the oldest audio when more than 120 ms piles up, keeping the
delay of a call short. A device that cannot be opened makes the Start call
fail; the track still carries the other direction. Closing the peer stops
both devices.

## Video

A video track is VP8: libvpx through FFmpeg's libavcodec in its real-time
mode (no frame lag, no B-frames, a key frame at least every 3 s), decoded by
FFmpeg's own VP8 decoder, pictures scaled and converted by libswscale. The
RTP payload format (RFC 7741) is UltraRTC's own - libdatachannel has
packetizers for H.264, H.265 and AV1 only. Pictures go in and come out as
RGBA. `UltraRtcVideoConfig` sets what is sent: the size (pictures of
another size are scaled to it), the frame rate (faster pictures are
dropped) and the bit rate. As with audio, both sides add the track with the
same mid before the offer.

```cpp
UltraRtcVideoConfig video;          // 640 x 480, 24 fps, 700 kbit/s
UltraRtcHandle track = UltraRtcInvalidHandle;
UltraRtc_AddVideoTrack(peer, "video", UltraRtcDirection::SendRecv, video, track);

// What arrives, decoded, on a media thread - into a tile, which takes
// pictures from any thread (UltraCanvasVideoTile):
UltraRtc_SetVideoFrameCallback(track, [](const UltraRtcVideoFrame& frame) {
    (void)frame;    // remoteTile->PushFrame(frame.rgba.data(), frame.width, frame.height);
});
// On TrackOpen: the camera, with its pictures for the self view.
UltraRtc_StartCamera(track, "", [](const UltraRtcVideoFrame& frame) {
    (void)frame;    // selfTile->PushFrame(frame.rgba.data(), frame.width, frame.height);
});
UltraRtc_SetTrackEnabled(track, false);    // camera off: nothing is sent
```

Loss: a frame with a packet missing is dropped by the depacketizer, and the
receiver shows nothing more until a key frame arrives - it asks for one with
an RTCP PLI (at most every 300 ms), which the sender answers with its next
picture. Re-enabling a track starts it with a key frame too.
`UltraRtc_SendVideoFrame` feeds a track directly (tests, an application with
its own pictures). The camera is the framework's video capture -
`UltraCanvasVideoRecorder`'s live preview with nothing recorded (GStreamer
on Linux, Media Foundation, AVFoundation, Android); a camera that cannot be
opened makes `UltraRtc_StartCamera` fail, and the track still receives.

## Status

Phase 1 implemented: peers, ICE servers (STUN / TURN / TURNS), transport
policy, port range, the W3C state machines as events, data channels (ordered
or not, reliable or partially reliable, negotiated ids), text and binary
messages. Phase 2 implemented: Opus audio tracks, the microphone and the
speaker. Phase 3 implemented: VP8 video tracks, the camera, key-frame
recovery after loss; pictures shown with `UltraCanvasVideoTile`. Not yet:
bandwidth estimation (the bit rate is fixed), NACK retransmission requests
from the receiving side, simulcast. Known limit: the miniaudio backend
opens the default devices whatever `deviceId` says.
