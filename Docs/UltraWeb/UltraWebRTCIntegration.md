# UltraRTC in UltraWeb

**Status:** W1, W2 and W3 done (WebSockets, peers and data channels for
apps, ABI v3; audio and video calls with the permission bar, ABI v4); W4
onward planned.
**Last Modified:** 2026-11-11
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

ABI v3 and v4 of `ultraweb.h` (built in W1 to W3; the contract is the header):
ids are small numbers in per-guest tables that map to the service's own
handles, so a guest never holds an engine handle, and every call returns
`UC_OK` or a `UC_ERR_*`. An app asks for v3 with `uc_abi_version()`; an
older UltraWeb refuses it.

| Import | UltraRTC call | Notes |
|---|---|---|
| `uc_rtc_peer_open(configJson, len)` → peer | `UltraRtc_CreatePeer` | `RTCConfiguration`-shaped JSON (`{"iceServers": [...]}`) |
| `uc_rtc_peer_close(peer)` | `UltraRtc_ClosePeer` | also on teardown |
| `uc_rtc_set_remote(peer, type, sdp, len)` | `UltraRtc_SetRemoteDescription` | SDP untouched |
| `uc_rtc_add_candidate(peer, cand, len, mid, len)` | `UltraRtc_AddIceCandidate` | |
| `uc_rtc_channel_open(peer, label, len, flags, maxRetransmits)` → channel | `UltraRtc_CreateDataChannel` | `UC_RTC_UNORDERED`; a resend limit or reliable |
| `uc_rtc_send(channel, data, len, binary)` | `UltraRtc_DataChannelSendText` / `Binary` | |
| `uc_rtc_channel_close(channel)` | `UltraRtc_DataChannelClose` | |
| `uc_rtc_poll(peer, info, out, capacity)` | - | the peer's oldest event: kind, channel, number, and its text |
| `uc_rtc_audio_add(peer, mid, len, direction)` → track (v4) | `UltraRtc_AddAudioTrack` | |
| `uc_rtc_video_add(peer, mid, len, direction, w, h, fps, kbps)` → track (v4) | `UltraRtc_AddVideoTrack` | `UltraRtcVideoConfig`; 0 takes the default |
| `uc_rtc_offer(peer)` (v4) | `UltraRtc_SetLocalDescription(Offer)` | tracks do not start a negotiation by themselves |
| `uc_rtc_microphone(track, on)`, `uc_rtc_camera(track, on, selfView)` (v4) | `UltraRtc_StartMicrophone` / `StartCamera` and the Stops | asks the user first (rules below) |
| `uc_rtc_playback(track, on)` (v4) | `UltraRtc_StartPlayback` | the speaker |
| `uc_rtc_video_show(track, tile)` (v4) | `UltraRtc_SetVideoFrameCallback` → the tile | below |
| `uc_rtc_track_enabled(track, on)` (v4) | `UltraRtc_SetTrackEnabled` | mute, camera off |

The calls became ABI v4 rather than part of v3, which had shipped: an app
that needs them asks for 4, and an UltraWeb with only v3 refuses it with a
message instead of failing at an import it does not have.

Negotiation is automatic (UltraRTC's `autoNegotiation`): the first channel
makes the offer, a remote offer makes the answer, so the planned
`uc_rtc_set_local` was not needed. Events wait in a queue per peer:
`uc_event(UC_NO_HANDLE, UC_EVENT_RTC, peer)` comes when one arrives while none
was waiting, and the app reads them with `uc_rtc_poll` until
`UC_ERR_NOT_FOUND` - so a burst of messages is one call into the app, not
one each. An event is taken off the queue only once its text fitted the
app's buffer. At most 1,000 events wait; past that the oldest message goes
(a state change or a description is kept), and a log line says so.

The bridge sees WebRTC through an interface (`RtcService` / `RtcPeer`,
`Apps/UltraWeb/host/UltraWebRtc.h`), not UltraRTC itself: the browser
runtime builds the same bridge with Emscripten, where the service would be
the page's own `RTCPeerConnection`. UltraRTC's events are posted to the UI
thread by that service; UltraWeb installs no process-wide dispatcher, which
another module in the process might want.

### Pictures: the VideoTile element

A new element kind, `UC_KIND_VIDEO_TILE` ("VideoTile"), is an
`UltraCanvasVideoTile` in the app's tree. `uc_rtc_video_show(track, tile)`
connects a remote track's frames (`UltraRtc_SetVideoFrameCallback` →
`PushFrame`, from the media thread, which the tile accepts) and, for a local
track, the camera's self view (mirrored), given to `uc_rtc_camera`. The app
lays the tiles out like any other element; the pixels never pass through
guest memory, which keeps a 640 x 480 call at 24 fps out of the 2 s per call
budget. A tile the app releases while pictures still arrive is safe: each
track's sink pushes through a slot whose lock the UI thread takes to empty
it before the tile goes.

### Signalling

WebRTC leaves signalling to the app. An app can relay the SDP through
`uc_fetch` to its server, under CORS; that works for a demo and polls for
the answer. A call app needs a push channel: the **`uc_ws_*` imports**
(W1) on UltraNet's WebSocket. They follow a browser's rules, not fetch's:
there is no CORS for WebSockets, so the handshake carries the app's origin
in an `Origin` header and the server decides; no cookies; `wss:` only from
an https app. (The plan first had a CORS-style check on the handshake's
answer. Browsers do not do that, and a server written for browsers would
not send the header, so the browser's rule was kept.) That is useful beyond
calls (chat, live updates).

### The rules

- **Camera and microphone ask the user.** The first `uc_rtc_camera` or
  `uc_rtc_microphone` of an origin shows a bar above the tab's area - "x.org
  wants to use your camera: Allow / Block" - and the call returns `UC_OK`
  at once; `UC_RTC_PERMISSION` says what became of it (on, blocked, or
  allowed but the device would not open). The decision is kept per origin
  in `<settings folder>/UltraWeb/permissions.txt`, and an origin the user
  blocked is refused at once after that; Ctrl+Shift+Delete forgets the
  decisions with the cookies. *Not yet:* a mark on the tab strip while a
  device runs, and holding back a background tab's request until it is
  shown.
- **Local addresses stay local.** ICE host candidates carry the machine's
  LAN addresses to the other side. A browser hides them (mDNS candidates);
  libjuice does not. Until UltraRTC can, an app from the network gets
  `iceTransportPolicy` Relay - in W2 always, so it needs a TURN server (the
  console says so when its configuration names none) - and an app from
  `about:` or a file gets All. Letting the user allow direct connections
  for an origin, through the same bar, is still open.
- **Limits per app:** 4 peers, 16 data channels a peer, 256 KB a message, 8
  ICE servers, only `stun:`, `turn:`, `turns:`, 4 video and 4 audio tracks
  a peer.
- **DTLS is always on**, as in UltraRTC; the app relays SDP untouched, so the
  fingerprint check holds end to end.
- **Teardown:** the guest's destructor closes its peers (UltraRTC stops the
  devices it started), so a closed tab is a hung-up call.

Tests (`Tests/UltraWebRealtimeTest.cpp`, W1 and W2): the socket and
configuration rules; guests written as WebAssembly text against fake socket
and peer services (open, refuse, read until empty, write, close by either
side, lose, poll descriptions, candidates, channels and messages, tear
down); UltraNet's sockets against a loopback WebSocket server that checks
the `Origin` header and the subprotocol; and two UltraRTC peers that connect
in one process through the test's signalling and exchange text and binary
data-channel messages. W3 adds a call guest against a fake peer (tracks, the
offer, both answers to the bar, a blocked device refused at once, pictures
into a tile, a track the other side added, nothing reaching a tile after the
guest goes), the permission store, and video between two real UltraRTC
peers (pictures fed to the engine reach the other side's sink; no camera
needed); `Tests/UltraWebTabTest.cpp` shows the bar in a tab, answers it, and
opens the app again unasked.

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
| W1 | **Done.** `uc_ws_*`: WebSockets for apps, under a browser's rules | UltraNet WebSocket (exists) |
| W2 | **Done.** `uc_rtc_*` data channels and peers; events posted to the UI thread; teardown; tests with two real peers | W1 for real apps; tests need none |
| W3 | **Done.** Audio and video tracks, `VideoTile`, the permission bar and its per-origin memory (ABI v4) | W2 |
| W4 | `about:call`: a built-in demo app, a two-tab call through a loopback relay | W3 |
| P1 | Pages: the JavaScript binding (seam 2) | JavaScript in UltraWeb |

## Recommendation

W1 to W3 are done: WebAssembly apps have WebSockets, peer-to-peer data
channels and audio and video calls behind the user's permission. W4, a
built-in `about:call`, is next. Leave pages to the JavaScript phase. The host code from W2 and W3 is
what that phase binds to, so it is not written twice.
