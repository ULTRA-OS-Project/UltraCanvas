# UltraRTC — Real-Time Communication Module (Design Proposal)

**Date:** 2026-10-01
**Status:** Proposal — for review; no implementation yet
**Companion:** [`BrowserWebRTCInvestigation.md`](BrowserWebRTCInvestigation.md)
(the survey of what WebRTC is, where Ladybird stands, and which engines
exist; this document assumes it)
**On Ladybird:** [Ladybird](https://ladybird.org/) is an independent
third-party browser project, not part of ULTRA OS or of this repository.
UltraCanvas's only relation to it is a demonstration build, maintained
outside this repository, in which UltraCanvas replaced Ladybird's Qt UI
layer — *the demonstration build* below. It is the worked example of a
browser that links the framework; the design holds for any such browser.
**Sibling in shape:** [`UltraWinDesignProposal.md`](UltraWinDesignProposal.md)
and [`UltraAndroidDesignProposal.md`](UltraAndroidDesignProposal.md) — an
UltraCanvas-owned API over open source engines, so the engine can be swapped
without touching callers.

---

## 1. Purpose

The question this answers: **can WebRTC be linked into UltraCanvas and the
sibling modules as a wrapper-style library — one engine behind one
UltraCanvas-owned API — so that the browser and the native applications
share it, instead of the browser carrying a WebRTC engine of its own?**

Yes. The framework already holds every peripheral piece a WebRTC stack needs
except the protocol engine itself (§3); the demonstration build shows that a
browser can link UltraCanvas as a library and drive its event loop; and
Ladybird's own WebRTC work (its `webrtc` branch) is shaped as a separate
helper process with an IPC surface — exactly the seam a wrapper library slots
into (§5).

UltraRTC is that wrapper. One API, PascalCase free functions with the
`UltraRtc_` prefix, opaque handles, `UltraRtcResult` from every blocking call,
and the engine chosen per build:

| Tier | Engine | When used |
|---|---|---|
| 1 (default) | **`webrtc` crate** (webrtc-rs, MIT / Apache-2.0) through a C ABI shim built with corrosion, as the Vectorizer plugin builds `vtracer` today | Every desktop platform and Android. The engine the Ladybird branch uses, so the browser and the native apps run one stack and one bug list |
| 2 (fallback) | **`libdatachannel` + `libjuice`** (MPL-2.0, C++17 / C, CMake) | A build without a Rust toolchain. Data channels and media transport; the module supplies the same jitter buffer and bandwidth estimation on top as for tier 1 |
| WebAssembly | the host browser's `RTCPeerConnection` via Emscripten bindings | A WASM build of an UltraCanvas app runs inside a browser that already has WebRTC; the wrapper maps onto it rather than shipping an engine |

Who calls it:

| Consumer | What it gets |
|---|---|
| **A browser built on UltraCanvas** (the demonstration build of Ladybird is the example) | `RTCPeerConnection` and friends in its engine bound to UltraRTC instead of to a browser-private Rust process (§5) |
| **UltraMessage / UltraSocial / UltraMail** | Voice and video calls between ULTRA OS users and with any browser or SIP-over-WebRTC gateway |
| **UltraAI** | Realtime voice sessions with providers that speak WebRTC (OpenAI Realtime, LiveKit-hosted agents) — audio in and out without a WebSocket round-trip per chunk |
| **UltraDesktop / UltraCloud** | Screen sharing and remote-assistance sessions to a browser, through a data channel plus a video track |
| **Any UltraCanvas app** | `UltraCanvasCallView`-class elements (§4.4) for a call window built from catalogue elements |

UltraRTC rules (mirroring the UltraNet registry entry):

- Clear structure; function and type names understandable on sight.
- Every blocking operation returns `UltraRtcResult`; every object with a
  lifetime (peer, transceiver, data channel, device track) is an
  `UltraRtcHandle`.
- **SDP passes through untouched.** The wrapper never rewrites an offer or
  answer; the engine's JSEP behaviour is what the caller sees. This is the
  one rule that keeps a browser binding faithful (§5.3).
- DTLS is mandatory and certificates are per peer connection; there is no
  insecure mode to opt into.
- No third-party type in a public header. Engine objects live behind the
  handle table in `UltraCanvas/core/UltraRTC/`.

---

## 2. Proposed public function surface

Lifecycle and capability discovery:

- `UltraRtc_Initialize`, `UltraRtc_Shutdown`, `UltraRtc_Version`
- `UltraRtc_GetCapabilities` — engine tier, codecs available in this build
  (Opus; VP8 / VP9 / H.264 / AV1 as FFmpeg provides them), hardware encoders,
  audio-processing availability, screen capture availability

Peer connections (the `RTCPeerConnection` state machine, one call per W3C
method so a browser binding is a table, not a translation):

- `UltraRtc_CreatePeer(const UltraRtcPeerConfig&) → UltraRtcHandle` — ICE
  servers, ICE transport policy, bundle policy, certificate
- `UltraRtc_ClosePeer`
- `UltraRtc_CreateOffer(peer, options, outSdp)`, `UltraRtc_CreateAnswer`
- `UltraRtc_SetLocalDescription(peer, type, sdp)`,
  `UltraRtc_SetRemoteDescription`
- `UltraRtc_AddIceCandidate(peer, candidate, sdpMid, sdpMLineIndex)`,
  `UltraRtc_RestartIce`
- `UltraRtc_GetLocalDescription`, `UltraRtc_GetRemoteDescription`,
  `UltraRtc_GetSignalingState`, `UltraRtc_GetIceGatheringState`,
  `UltraRtc_GetIceConnectionState`, `UltraRtc_GetConnectionState`
- `UltraRtc_GetStats(peer, outJson)` — the W3C stats dictionary as JSON,
  through `UltraCanvasJSON`

Transceivers, senders and receivers:

- `UltraRtc_AddTransceiver(peer, kind, direction, sendEncodings) → UltraRtcHandle`
- `UltraRtc_AddTrack(peer, track, streamIds) → sender handle`,
  `UltraRtc_RemoveTrack`, `UltraRtc_ReplaceTrack`
- `UltraRtc_SetTransceiverDirection`, `UltraRtc_StopTransceiver`,
  `UltraRtc_SetCodecPreferences`
- `UltraRtc_GetSenderParameters`, `UltraRtc_SetSenderParameters` — bitrate
  caps, simulcast layers, degradation preference

Media in and out (two levels, because the browser needs both):

- **PCM / raw frames** — the common case for native apps. The engine encodes
  and decodes:
  `UltraRtc_SendAudioFrame(sender, const UltraRtcAudioFrame&)` (interleaved
  16-bit or float PCM, any rate; the module resamples to 48 kHz),
  `UltraRtc_SendVideoFrame(sender, const UltraRtcVideoFrame&)` (I420 / NV12
  or an `UCImage`), with `OnAudioFrame` and `OnVideoFrame` callbacks on the
  receiver.
- **Encoded frames** — for a caller that owns the codec, or for the W3C
  encoded-transform path (`RTCRtpScriptTransform`, SFrame): 
  `UltraRtc_SendEncodedFrame`, `OnEncodedFrame`, and
  `UltraRtc_SetEncodedFrameHook(sender|receiver, hook)` to intercept frames
  between codec and packetiser in either direction.

Device tracks (the `getUserMedia` half, built on the framework's own capture):

- `UltraRtc_OpenMicrophoneTrack(deviceId, const UltraRtcAudioConstraints&) → track`
  — over `UltraCanvasAudioRecorder`; constraints are the W3C ones
  (`echoCancellation`, `noiseSuppression`, `autoGainControl`, `sampleRate`,
  `channelCount`)
- `UltraRtc_OpenCameraTrack(deviceId, const UltraRtcVideoConstraints&) → track`
  — over IODeviceManager's `CameraDevice` streaming callback
- `UltraRtc_OpenScreenTrack(windowOrDisplayId) → track` — over the desktop
  shell's capture path
- `UltraRtc_OpenCustomTrack(kind) → track` — fed by `UltraRtc_Send*Frame`
- `UltraRtc_CloseTrack`, `UltraRtc_SetTrackEnabled` (mute),
  `UltraRtc_GetTrackSettings`, `UltraRtc_ListDevices(kind)` (over
  `UltraCanvasAudioDevices` and the IODeviceManager)

Data channels:

- `UltraRtc_CreateDataChannel(peer, label, const UltraRtcDataChannelInit&) → channel`
  — ordered, `maxRetransmits`, `maxPacketLifeTime`, protocol, negotiated id
- `UltraRtc_DataChannelSend(channel, bytes | text)`,
  `UltraRtc_DataChannelClose`, `UltraRtc_GetDataChannelState`,
  `UltraRtc_GetDataChannelBufferedAmount`,
  `UltraRtc_SetDataChannelBufferedAmountLowThreshold`

Audio processing and playback:

- `UltraRtc_SetAudioProcessing(track, const UltraRtcAudioProcessing&)` —
  AEC, NS, AGC on a microphone track (§3, *what is missing*)
- `UltraRtc_AttachPlayback(receiver, deviceId)` — route a received audio
  track to an output device through the framework's audio backend, with
  `UltraRtc_SetPlaybackVolume` / `UltraRtc_SetPlaybackMuted`; the W3C
  `setSinkId` maps here

Events (one callback per peer, typed):

- `UltraRtc_SetPeerEventCallback(peer, fn)` delivering
  `NegotiationNeeded`, `IceCandidate`, `IceCandidateError`,
  `SignalingStateChange`, `IceGatheringStateChange`,
  `IceConnectionStateChange`, `ConnectionStateChange`, `Track`,
  `DataChannel`; every event is delivered on the caller's event loop through
  the framework's dispatch, never from an engine thread.

Diagnostics:

- `UltraRtc_SetLogLevel`, `UltraRtc_SetLogCallback`,
  `UltraRtc_DumpSdp(peer)` (both descriptions, for a bug report),
  `UltraRtc_ProbeStunServer(url)` (over `UltraNet_UdpOpen`, the one place
  UltraNet's UDP API is used — see §3)

Types: `UltraRtcHandle` (`uint64_t`, zero invalid), `UltraRtcResult`
(`code`, `message`), `UltraRtcResultCode` (`Success`, `InvalidHandle`,
`InvalidState`, `InvalidSdp`, `IceFailed`, `DtlsFailed`, `CodecUnavailable`,
`DeviceNotFound`, `PermissionDenied`, `Unsupported`, `Unknown`), the config
and frame structs above, and the enums for kinds, directions and states with
the W3C spellings.

---

## 3. What the framework already has, and what is missing

| Need | Framework today | Gap |
|---|---|---|
| Microphone capture, device list | `UltraCanvasAudioRecorder` (miniaudio / pulse / wasapi / coreaudio backends; `echoCancel` and `noiseSuppress` are backend hints only), `UltraCanvasAudioDevices` | A continuous-callback mode: the recorder accumulates into a `UCAudio` buffer for `TakeBuffer()`; a call wants 10 ms frames delivered as they arrive |
| Speaker playback | `UltraCanvasAudioPlayer` plays a file or a loaded `UCAudio` | A streaming PCM sink (push 10 ms frames, with a playout clock) — the audio backend has the device; the push API does not exist yet |
| Camera capture, device list | IODeviceManager `CameraDevice` with frame streaming to a callback; Linux backend (V4L2) | Windows (Media Foundation) and macOS (AVFoundation) camera backends, if not present by then |
| Screen capture | The desktop shell owns the compositor on ULTRA OS; platform capture elsewhere | A capture API that yields frames at a frame rate |
| Audio codec | libopus is already a dependency (`Docs/Dependencies.md`) | Nothing — Opus encode/decode with in-band FEC and PLC |
| Video codecs | FFmpeg through VideoFX (libavcodec: VP8/VP9 via libvpx, H.264 via OpenH264 or x264, AV1 via dav1d / SVT-AV1), hardware encoders where FFmpeg exposes them | Low-latency encoder configuration (zero-lookahead, keyframe on request); x264 is GPL and stays out of the default build as `Dependencies.md` already notes |
| TLS / crypto | OpenSSL on Linux and Android, platform TLS elsewhere | Nothing the engine does not bring: tier 1 uses `ring`, tier 2 OpenSSL / mbedTLS — the engine's DTLS is self-contained |
| UDP sockets | `UltraNet_UdpOpen` / `Send` / `Receive` — blocking, polled | **Not used for the media path.** Both engines own their sockets, as libcurl owns its inside UltraNet; UltraRTC exposes them to nothing. UltraNet's UDP serves the STUN probe only |
| JSON | `UltraCanvasJSON` | Nothing — stats and config go through it |
| Rust toolchain in the build | corrosion + `vtracer-c` (Vectorizer plugin) | A second crate; the vendoring rule in §7 |
| Event delivery to the UI thread | `UltraCanvasApplication` dispatch | Nothing |
| **ICE, DTLS, SRTP, SCTP, RTP/RTCP, JSEP** | — | **The engine** (tier 1 or 2) |
| **Jitter buffer** | — | Written in UltraRTC: an adaptive playout buffer per receiver, reordering by sequence number, Opus PLC for gaps, target delay from observed jitter. Neither tier's engine has one |
| **Bandwidth estimation** | — | Tier 1 produces TWCC feedback; the estimator (a GCC-style delay-based controller, `str0m`'s is the readable reference) is UltraRTC's. Phase 3 (§8) |
| **Echo cancellation, noise suppression, gain** | Backend hints that PulseAudio honours and the others ignore | Vendor `webrtc-audio-processing` (Google's APM split out of `libwebrtc`, BSD-3) behind `UltraRtc_SetAudioProcessing`; `speexdsp` as the small fallback |

The honest summary: the capture, playback, codec, JSON and event plumbing is
all here or one callback mode away; the protocol engine is the engine's job;
and the three pieces every small engine leaves out are UltraRTC's own code,
in that order of difficulty — jitter buffer (days), bandwidth estimation
(weeks), audio processing (vendor it, do not write it).

---

## 4. Architecture

```
UltraRtc public API (C-style free functions, opaque handles, UltraRtcResult)
├── Handle table + event marshalling to the UltraCanvas event loop
├── Media pipeline (UltraCanvas-owned, engine-independent)
│   ├── Device tracks: AudioRecorder / CameraDevice / screen capture → frames
│   ├── Audio processing: webrtc-audio-processing (AEC/NS/AGC)  [phase 4]
│   ├── Codecs: libopus; FFmpeg video encode/decode, low-latency profile
│   ├── Jitter buffer per receiver (Opus PLC, adaptive delay)
│   ├── Bandwidth estimator (TWCC → target bitrate → encoder)  [phase 3]
│   └── Playback sink: streaming PCM into the audio backend
├── Engine adapter (one compiled in per build)
│   ├── RustEngine     — ultrartc-sys crate: C ABI over the `webrtc` crate
│   │                    (ICE, DTLS, SRTP, SCTP, RTP/RTCP, JSEP, data channels)
│   ├── DataChannelEngine — libdatachannel + libjuice (tier 2)
│   └── BrowserEngine  — Emscripten → window.RTCPeerConnection (WASM)
└── Elements (catalogue): UltraCanvasCallView, UltraCanvasVideoTile,
    UltraCanvasDeviceSelector
```

### 4.1 Where the engine boundary is

The engine sees **encoded RTP payloads in, encoded RTP payloads out**, SDP
strings, ICE candidates, data-channel messages and state events. Everything
that touches a device, a codec or a clock is UltraRTC's, in C++, so that the
two engine tiers and the WASM binding behave identically to a caller and so
that the audio pipeline is the same one a browser binding ends up using.

### 4.2 Threads

The Rust engine runs its own tokio runtime inside the shim (one per
process, started by `UltraRtc_Initialize`); libdatachannel runs its own
threads. Neither reaches a caller directly: frames arrive on a dedicated
media thread per peer, events are posted to the UltraCanvas loop. The
capture callbacks already run on backend threads today, so this changes
nothing for the audio recorder.

### 4.3 Process placement

UltraRTC is a library and takes no position on processes. A native app
links it into its process. A browser puts it in its WebRTC helper process
(§5.2), which keeps UDP sockets and DTLS out of the renderer sandbox — the
sandbox problem Ladybird's branch still has open is solved by placement, not
by the engine.

### 4.4 Elements

A call window is built from catalogue elements, per the house rule:
`UltraCanvasVideoTile` (one remote or local video track, painting frames
like `UltraCanvasVideoPlayerElement`), `UltraCanvasCallView` (a container
of tiles with mute / camera / share / hang-up `UltraCanvasButton`s and a
`UltraCanvasDeviceSelector` dropdown pair). Each gets a row in
`Docs/UltraCanvas/UltraCanvasUIElements.md` in the same change that adds it.

---

## 5. Linking the browser to it

### 5.1 Why it fits

The demonstration build already embeds UltraCanvas: the framework's
application object has hooks so a host loop can drive one iteration, and the
Windows backend services a host's IPC sockets from inside the UltraCanvas
loop (`UltraCanvasApplication.cpp`, `UltraCanvasWindowsApplication.cpp`).
That build's splash and diagnostics are framework elements. So a browser that
links the library can reach a module in it.

Ladybird's `webrtc` branch shapes WebRTC as **WebContent → IPC →
`Services/WebRTCClient` (one helper process per peer connection) → engine**.
The helper's job is exactly UltraRTC's API: create a peer, set descriptions,
add candidates, move frames, raise events. That is the seam.

### 5.2 Two ways to bind, one recommended

| Option | What changes in a Ladybird-based build | Verdict |
|---|---|---|
| **A. UltraRTC inside the helper process** | `Services/WebRTCClient` keeps its `.ipc` files and its one-process-per-peer model; its `src/` becomes a thin server that translates each IPC message to one `UltraRtc_*` call and each UltraRTC event to one IPC event. LibWeb's `WebRTC/` classes are untouched. | **Recommended.** Smallest diff against upstream, the renderer sandbox stays closed, and the build keeps rebasing onto upstream's LibWeb work |
| B. UltraRTC in WebContent | LibWeb's `RTCPeerConnection` calls UltraRTC directly; no helper process | Simpler, but puts sockets and DTLS in the renderer and reopens the `SIGSYS` sandbox problem; rejected |

With option A Ladybird's own engine (the `ladybird_webrtc` crate) is
replaced by the module in that build, and the build drops its direct
dependency on the `webrtc` crate because UltraRTC's vendored tree carries it.
If Ladybird later changes engine, the build is unaffected: only the module's
tier 1 is. This is a change carried in the demonstration build, downstream of
Ladybird; it does not ask anything of Ladybird.

### 5.3 The binding table

Each W3C method becomes one call, which is why §2 is shaped as it is:

| W3C (`LibWeb/WebRTC`) | UltraRTC |
|---|---|
| `new RTCPeerConnection(config)` | `UltraRtc_CreatePeer` |
| `createOffer` / `createAnswer` | `UltraRtc_CreateOffer` / `CreateAnswer` |
| `setLocalDescription` / `setRemoteDescription` | the two `SetXDescription` calls, SDP passed through unmodified |
| `addIceCandidate`, `restartIce` | same names |
| `addTransceiver`, `addTrack`, `removeTrack`, `sender.replaceTrack` | same names |
| `sender.setParameters` / `getParameters` | `UltraRtc_SetSenderParameters` / `Get…` |
| `createDataChannel`, `channel.send`, `channel.close` | the data-channel calls |
| `getStats` | `UltraRtc_GetStats` — the JSON is the stats report |
| `RTCRtpScriptTransform`, SFrame | `UltraRtc_SetEncodedFrameHook` — the worker transform sits between codec and packetiser |
| `onicecandidate`, `ontrack`, `ondatachannel`, state events | the peer event callback |
| `MediaStreamTrack` from `getUserMedia` | either a LibWeb-captured track pushed with `UltraRtc_SendAudioFrame` (what upstream does today, through `MediaStreamTrack → WebAudio → resample → Opus`) or an UltraRTC device track opened in the helper; both are supported so a build can start with Ladybird's path |
| `HTMLMediaElement.srcObject` playback | `UltraRtc_AttachPlayback` in the helper, with volume / mute / `setSinkId` mapped — which closes the "playback not driven by `srcObject`" gap in upstream's status page |

What the wrapper must *not* do is normalise SDP, reorder m-lines or hide
transceiver identity: sites like Discord munge SDP and rely on JSEP's exact
rules, and a layer that "helps" there breaks them. Hence the pass-through
rule in §1.

### 5.4 What stays in the browser

The W3C object model, the IDL, the JavaScript-visible state machines,
permission prompts, the `MediaStream` graph and Web Audio. UltraRTC provides
the engine-level behaviour, and nothing of the browser's.

---

## 6. Platform matrix

| | Linux / ULTRA OS | Windows | macOS | Android | WebAssembly |
|---|---|---|---|---|---|
| Engine tier 1 (`webrtc` crate via corrosion) | yes | yes (MSVC or clang-cl Rust target) | yes | yes (NDK target; the Vectorizer crate already cross-builds) | — |
| Engine tier 2 (libdatachannel) | yes | yes | yes | yes | — |
| Browser engine (`window.RTCPeerConnection`) | — | — | — | — | yes |
| Microphone / speaker | pulse / miniaudio | wasapi | coreaudio | miniaudio (AAudio / OpenSL) | Web Audio |
| Camera | V4L2 (exists) | Media Foundation (to add) | AVFoundation (to add) | Camera2 via NDK (to add) | `getUserMedia` |
| Screen capture | desktop shell / PipeWire portal | DXGI duplication | ScreenCaptureKit | MediaProjection | `getDisplayMedia` |
| Video codecs | FFmpeg (+ VAAPI) | FFmpeg (+ Media Foundation) | FFmpeg (+ VideoToolbox) | FFmpeg (+ MediaCodec) | browser's |

---

## 7. Dependencies (when implementation starts)

All vendored, so a release builds without network access; licences recorded
in `THIRD_PARTY_LICENSES.md`, rows added to `Docs/Dependencies.md` and
`master_dependencies.yaml`.

| Dependency | Licence | Tier | How it is pinned |
|---|---|---|---|
| `webrtc` crate 0.17 and its tree (`ring`, `tokio`, `rtp`, `rtcp`, `sctp`, `dtls`, `srtp`, `ice`, `interceptor`, …) | MIT / Apache-2.0 / ISC | 1 | `cargo vendor` into `UltraCanvas/third_party/rust/`, `.cargo/config.toml` source replacement, `--offline` builds; the same `Cargo.lock` the demonstration build uses |
| `libdatachannel`, `libjuice`, `libsrtp`, `usrsctp` | MPL-2.0, MPL-2.0, BSD-3, BSD-3 | 2 | copies under `UltraCanvas/third_party/`, `add_subdirectory` |
| `webrtc-audio-processing` (Google APM, standalone) | BSD-3 | both, phase 4 | copy under `third_party/`, Meson replaced by a small CMake file |
| libopus | BSD-3 | both | already a dependency |
| FFmpeg (libavcodec, libswscale) | LGPL-2.1 (GPL parts excluded) | both | already a dependency through VideoFX |

Nothing new is fetched at build time that is not already in the tree. That
is the whole of the "no library loaded from the internet" requirement, and
it is satisfied the same way for the browser and for the native apps.

---

## 8. Phasing

| Phase | Deliverable | Depends on |
|---|---|---|
| 0 | This proposal reviewed; registry entry in `Masterfile_modules.md`; `UltraCanvas/{include,core}/UltraRTC/` skeleton with `UltraRtcResult`, handles, `Initialize` / `Shutdown` / `GetCapabilities` | approval |
| 1 | **Data channels and Opus audio, Linux, tier 1.** Peer connection lifecycle, ICE/STUN/TURN config, data channels, microphone track with the recorder's new callback mode, streaming playback sink, a first jitter buffer. Test: a call between two UltraCanvas processes and between UltraCanvas and Chrome, through a public STUN server | corrosion shim crate; recorder callback mode; player push API |
| 2 | **Browser binding.** `Services/WebRTCClient` in the demonstration build re-implemented over UltraRTC (option A); Ladybird's 28 browser checks pass; Discord voice call reproduces Ladybird's result | phase 1; the demonstration build rebased onto a Ladybird that carries `LibWeb/WebRTC/` |
| 3 | **Video and bandwidth estimation.** Camera and screen tracks, FFmpeg low-latency encode/decode, TWCC-driven estimator, simulcast parameters. Test: a two-way video call with Chrome on a throttled link holds without freezing | phase 1 |
| 4 | **Audio processing.** `webrtc-audio-processing` vendored; AEC/NS/AGC behind `UltraRtc_SetAudioProcessing`; a laptop call without headphones has no echo | phase 1 |
| 5 | **Windows, macOS, Android**, camera and screen backends per §6; WASM binding | phases 1–3 |
| 6 | **Tier 2** (`libdatachannel`) brought to parity for Rust-less builds | phases 1–4; only if a Rust-less build is needed |

Each phase ships with its `Docs/Modules/UltraRTC/README.md` section, the
element-catalogue rows for the elements it adds, and `UltraRtcApiStatus` —
the equivalent of `UltraNetApiStatus` — printing WORKING / IMPLEMENTED / NOT
IMPLEMENTED per function.

---

## 9. Decisions proposed (for review)

1. **One engine for the browser and the apps, and it is the one Ladybird
   chose.** Tier 1 is the `webrtc` crate. Sharing Ladybird's engine is worth
   more than a C++-native engine: one SDP behaviour, one set of interop bugs
   against Chrome, and the demonstration build's rebases onto Ladybird keep
   working.
2. **The wrapper owns the media pipeline; the engine owns the transport.**
   Codecs, jitter buffer, bandwidth estimation, audio processing and devices
   are UltraRTC's, so the tiers and the WASM binding are interchangeable to
   a caller and the browser gets the same audio path as the apps.
3. **Bind the browser at the helper-process seam** (option A), never inside
   WebContent.
4. **SDP is never rewritten by the wrapper.**
5. **Vendor everything on day one**, with the demonstration build and the
   framework sharing one `Cargo.lock`.
6. **Do not write the audio processing.** Vendor Google's APM; it is the
   component nobody has rewritten well.

---

## 10. Open questions

- **Who carries the helper-process replacement?** If Ladybird keeps evolving
  `Services/WebRTCClient` with its own engine, the demonstration build carries
  a replacement `src/` indefinitely, downstream. The alternative is to offer
  UltraRTC's C ABI to Ladybird as a pluggable engine; whether a third-party
  project wants that is the Ladybird maintainers' decision, not a design
  decision here.
- **Rust in the framework's core.** Today Rust is confined to one plugin.
  Tier 1 puts a Rust crate under `UltraCanvas/core/UltraRTC/`. If that is
  not acceptable, tier 2 becomes tier 1 and the JSEP-fidelity risk of
  `libdatachannel`'s simplified negotiation (BUNDLE-only, a flatter
  transceiver model) has to be measured against the sites ULTRA OS wants
  calls on.
- **Hardware video encoders.** FFmpeg exposes VAAPI, VideoToolbox, Media
  Foundation and MediaCodec encoders, but their low-latency and
  keyframe-on-demand behaviour differs per driver; phase 3 should start with
  software VP8 and add hardware per platform with a test.
- **TURN credentials.** The module takes ICE servers from the caller; whether
  ULTRA OS runs a TURN service for its users (coturn) is a product decision.
- **Interop targets.** Chrome and Firefox are the baseline; Safari, Jitsi,
  LiveKit, Janus and a SIP gateway (Asterisk WebRTC) are the second ring.
  The list decides which codec and SDP features phase 3 must carry.
