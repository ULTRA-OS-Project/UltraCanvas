# UltraRTC in UltraWeb

**Status:** Plan, no code yet.
**Last Modified:** 2026-10-22
**Author:** UltraCanvas Framework / ULTRA OS
**Reads with:** [`UltraWeb.md`](UltraWeb.md) (what the browser is),
[`Docs/Modules/UltraRTC/README.md`](../Modules/UltraRTC/README.md) (the
module), [`UltraRTCDesignProposal.md`](../Research/UltraRTCDesignProposal.md)
§5 (binding a browser to it) and
[`BrowserWebRTCInvestigation.md`](../Research/BrowserWebRTCInvestigation.md).

The question: **UltraRTC is built. How does it get into UltraWeb?**

## Contents

- [The answer](#the-answer)
- [What there is to join](#what-there-is-to-join)
- [Seam 1: WebAssembly apps](#seam-1-webassembly-apps)
  - [The imports](#the-imports)
  - [Pictures: the VideoTile element](#pictures-the-videotile-element)
  - [Signalling](#signalling)
  - [The rules](#the-rules)
- [Seam 2: pages, once there is JavaScript](#seam-2-pages-once-there-is-javascript)
- [What UltraRTC still lacks for the open web](#what-ultrartc-still-lacks-for-the-open-web)
- [Phases](#phases)
- [Recommendation](#recommendation)

## The answer

UltraRTC is already linked into UltraWeb: it is a module home (`UltraRTC`,
archive `uc-rtc`) absorbed into the shared core that UltraWeb links. It
cannot reach a **web page** yet, because the WebRTC API is JavaScript
(`RTCPeerConnection`, `getUserMedia`) and UltraWeb runs no JavaScript. Nor is
there a library a page could load in its place: WebRTC is built into
browsers, and the scripts sites load (`adapter.js`, PeerJS, vendor SDKs) are
wrappers around the built-in API (`BrowserWebRTCInvestigation.md` §2).

It can reach UltraWeb's **WebAssembly apps** now. They already build their
UI from UltraCanvas elements through imports (`UltraWeb/guest/ultraweb.h`).
Adding `uc_rtc_*` imports, a `VideoTile` element kind and a WebSocket for
signalling gives an app peer connections, data channels and audio and video
calls on UltraRTC: the same engine as ChatCube Direct, behind the same
sandbox as the app's fetch and storage. That is seam 1, and it is the
recommended first step.

When JavaScript arrives (`UltraWebProposal.md`, the later phases), the W3C
objects bind to the same calls (seam 2). The proposal's §5.3 table was
written so that each W3C method becomes one UltraRTC call, so seam 1's host
code serves both: the JavaScript binding is a second front end to it.

## What there is to join

| UltraRTC has (Phases 1 to 3) | UltraWeb has |
|---|---|
| Peers, STUN / TURN / TURNS, transport policy, the W3C state machines as events | A guest ABI (v2) with handles, events through `uc_event`, timers, fetch, storage, clipboard |
| Data channels: ordered or not, reliable or partly, text and binary | A sandbox: per-origin rules, limits per app, failures reported, never a crash |
| Opus audio tracks, microphone, speaker | Cookies, video and audio playback for pages (`UltraWeb.md`) |
| VP8 video tracks, camera, key-frame recovery | No JavaScript, no permission prompts yet |
| `UltraCanvasVideoTile`: pictures pushed from any thread | Tabs: an app runs in its tab's area, and keeps running in the background |

Two facts shape the design:

- **UltraRTC's event dispatcher is process-wide**
  (`UltraRtc_SetEventDispatcher`). UltraWeb installs one, posting to the UI
  thread, at start-up; guests run on the UI thread, so an event reaches
  `uc_event` exactly as a timer or a fetch does. No other module in UltraWeb's
  process installs one. A second user in the same process would have to
  share it - worth a line in the module's README when this lands.
- **Events of a peer closed meanwhile are dropped** by UltraRTC, so a guest
  torn down (a trap, a closed tab) closes its peers in its destructor, as it
  cancels its fetches today, and nothing reaches it afterwards.

## Seam 1: WebAssembly apps

### The imports

ABI v3 of `ultraweb.h`, in its style: handles are small numbers in a
per-guest table that maps to `UltraRtcHandle`s, so a guest never holds an
engine handle, and every call returns `UC_OK` or a `UC_ERR_*`. An app asks
for v3 with `uc_abi_version()`; an older UltraWeb refuses it.

| Import | UltraRTC call | Notes |
|---|---|---|
| `uc_rtc_peer_create(configJson, len)` → peer | `UltraRtc_CreatePeer` | ICE servers as JSON (`[{"urls":"turn:...","username":..,"credential":..}]`), the shape `RTCConfiguration` has |
| `uc_rtc_peer_close(peer)` | `UltraRtc_ClosePeer` | also on teardown |
| `uc_rtc_set_local(peer, type)` | `UltraRtc_SetLocalDescription` | with `autoNegotiation` off: the app decides when to offer |
| `uc_rtc_set_remote(peer, type, sdp, len)` | `UltraRtc_SetRemoteDescription` | SDP untouched |
| `uc_rtc_add_candidate(peer, cand, len, mid, len)` | `UltraRtc_AddIceCandidate` | |
| `uc_rtc_channel_create(peer, label, len, flags)` → channel | `UltraRtc_CreateDataChannel` | flags: unordered, max retransmits |
| `uc_rtc_send(channel, data, len, binary)` | `UltraRtc_DataChannelSendText` / `Binary` | |
| `uc_rtc_audio_add(peer, mid, len, direction)` → track | `UltraRtc_AddAudioTrack` | |
| `uc_rtc_video_add(peer, mid, len, direction, w, h, fps, kbps)` → track | `UltraRtc_AddVideoTrack` | `UltraRtcVideoConfig` |
| `uc_rtc_microphone(track, on)`, `uc_rtc_camera(track, on)` | `UltraRtc_StartMicrophone` / `StartCamera` and the Stops | asks the user first (rules below) |
| `uc_rtc_playback(track, on)` | `UltraRtc_StartPlayback` | the speaker |
| `uc_rtc_track_enabled(track, on)` | `UltraRtc_SetTrackEnabled` | mute, camera off |
| `uc_rtc_read(id, out, capacity)` | - | the text of the event `id`: an SDP, a candidate, a message, a channel label |

Events arrive as `uc_event(handle, UC_EVENT_RTC, detail)`, `handle` the peer,
channel or track, `detail` naming what happened (a local description, a
candidate, a state change, a channel opened or closed, a message, a track
open); anything with text is read with `uc_rtc_read`, then dropped once the
app returns from `uc_event`, as `uc_fetch_body` works. Messages wait in a
queue per channel (at most 1 MB): an app that does not read loses the
oldest, and a log line says so.

### Pictures: the VideoTile element

A new element kind, `UC_KIND_VIDEO_TILE` ("VideoTile"), is an
`UltraCanvasVideoTile` in the app's tree. `uc_rtc_video_show(track, tile)`
connects a remote track's frames (`UltraRtc_SetVideoFrameCallback` →
`PushFrame`, from the media thread, which the tile accepts) and, for a local
track, the camera's self view (mirrored). The app lays the tiles out like any
other element; the pixels never pass through guest memory, which keeps a
640 x 480 call at 24 fps out of the 2 s per call budget.

### Signalling

WebRTC leaves signalling to the app. An app can already relay the SDP
through `uc_fetch` to its server, under CORS; that works for a demo and
polls for the answer. A call app needs a push channel: **`uc_ws_*`
imports** (`open`, `send`, `close`; messages as `UC_EVENT_WS`) on UltraNet's
WebSocket, under the same origin and CORS-style rules as fetch: the app's
own origin freely, another origin only when its handshake answers with an
`Access-Control-Allow-Origin` that admits the app, `wss:` only from an
https app. That is useful beyond calls (chat, live updates), so it is its
own step (phase W1 below).

### The rules

- **Camera and microphone ask the user.** The first `uc_rtc_camera` or
  `uc_rtc_microphone` of an origin shows a bar above the tab's area - "x.org
  wants to use your camera: Allow / Block" - and the call answers
  `UC_ERR_STATE` until the user decides (then `UC_EVENT_RTC` says which).
  The decision is kept per origin in UltraWeb's settings. A tab in the
  background cannot start a device. While a device is on, the tab strip
  shows it.
- **Local addresses stay local.** ICE host candidates carry the machine's
  LAN addresses to the other side. A browser hides them (mDNS candidates);
  libjuice does not. Until UltraRTC can, an app from the network gets
  `iceTransportPolicy` Relay unless the user allows it, as with the camera,
  and an app from `about:` or a file gets All.
- **Limits per app:** 4 peers, 16 data channels a peer, 256 KB a message, 4
  video and 4 audio tracks, ICE servers only `stun:`, `turn:`, `turns:`.
- **DTLS is always on**, as in UltraRTC; the app relays SDP untouched, so the
  fingerprint check holds end to end.
- **Teardown:** the guest's destructor closes its peers (UltraRTC stops the
  devices it started), so a closed tab is a hung-up call.

Tests: two guests in one `UltraWebGuestTest` process, connected through a
fake signalling relay, exchange a data-channel message and then audio frames
fed with `UltraRtc_SendAudioFrame` (no devices, no network: loopback ICE, as
`Tests/UltraRTC` does), with the permission bar answered by the test.

## Seam 2: pages, once there is JavaScript

The W3C objects map one-to-one onto the same host code
(`UltraRTCDesignProposal.md` §5.3):

| JavaScript | Through |
|---|---|
| `new RTCPeerConnection(config)` | the seam 1 peer, from the page's origin |
| `createOffer` / `setLocalDescription` / `setRemoteDescription` / `addIceCandidate` | the same calls; promises resolve on the state events |
| `createDataChannel`, `onmessage`, `send` | the channel calls and the per-channel queue |
| `navigator.mediaDevices.getUserMedia` | the permission bar, then the camera / microphone on a track |
| `addTrack`, `ontrack` | `UltraRtc_AddAudioTrack` / `AddVideoTrack`, the `Track` event |
| `<video>.srcObject = stream` | the HTML reader's `mediaFactory` (this release) giving a `VideoTile` instead of a player when the element plays a stream |

The last row is why the media work in this release keeps `MediaRequest`
and the factory with the host: a page's `<video>` already becomes whatever
element the host makes, so a live stream needs only another kind of answer.

## What UltraRTC still lacks for the open web

For apps that call each other on UltraRTC (seam 1, ChatCube) none of this is
blocking. For pages calling Chrome or Firefox users through a commercial
service, it is the list to work through (the module README's *Not yet* and
the proposal's phases 3 to 5):

- **Bandwidth estimation, NACK, simulcast:** the bit rate is fixed. Fine on a
  LAN, poor on a bad link and against an SFU that expects simulcast.
- **H.264:** many services negotiate it; VP8 is accepted by every browser,
  so a page that offers VP8 works.
- **Echo cancellation, noise suppression, gain** (proposal phase 4):
  without them a call without headphones echoes.
- **Screen sharing** (`getDisplayMedia`) and statistics (`getStats`).
- **The device choice:** the miniaudio backend opens the default devices
  whatever `deviceId` says.

## Phases

| Phase | What | Needs |
|---|---|---|
| W1 | `uc_ws_*`: WebSockets for apps, under the fetch rules | UltraNet WebSocket (exists) |
| W2 | `uc_rtc_*` data channels and peers; the event dispatcher; teardown; tests with two guests | W1 for real apps; tests need none |
| W3 | Audio and video tracks, `VideoTile`, the permission bar and its per-origin memory, Relay by default | W2 |
| W4 | `about:call`: a built-in demo app, a two-tab call through a loopback relay | W3 |
| P1 | Pages: the JavaScript binding (seam 2) | JavaScript in UltraWeb |

## Recommendation

Start with W1 and W2: they are small (one import table each, on code that
exists) and give WebAssembly apps peer-to-peer data at once. Do W3 together
with the permission bar, which UltraWeb needs for camera and microphone
anyway. Leave pages to the JavaScript phase. The host code from W2 and W3 is
what that phase binds to, so it is not written twice.
