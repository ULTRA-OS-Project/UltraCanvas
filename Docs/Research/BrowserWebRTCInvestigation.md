# Can the Browser Carry Its Own WebRTC?

**Date:** 2026-10-01
**Status:** Investigation, no code changes. Everything below was read from
public sources on the date above (the Ladybird repository and the maintainer
branch named in §3, the library READMEs, Mozilla's and WebKit's trees); nothing
was built or run. Where a figure is quoted, its source is named.
**Scope:** a browser built on UltraCanvas. The worked example throughout is
[Ladybird](https://ladybird.org/), an **independent third-party browser
project** that is not part of ULTRA OS or of this repository. UltraCanvas's
only relation to it is a demonstration build, maintained outside this
repository, in which UltraCanvas replaced Ladybird's Qt UI layer; that build
is called *the demonstration build* below, and nothing in it is Ladybird's
doing or ours to direct. The findings apply to any browser that links the
framework. The question put to this document:

> Could the browser create its own WebRTC support, so that no WebRTC library
> has to be loaded from the internet?

"Loaded from the internet" has two readings, and both are answered:

1. **At page load** — a web page pulls a WebRTC JavaScript library from a CDN
   before a call can start.
2. **At build time** — the browser's build fetches a WebRTC engine
   (`libwebrtc`, `libdatachannel`, a Rust crate) from the network instead of
   compiling it from sources it owns.

---

## 1. Executive summary

- **WebRTC is a browser-native API, not a library.** `RTCPeerConnection`,
  `getUserMedia` and `RTCDataChannel` are implemented *inside* Chrome, Firefox
  and Safari in C++; the JavaScript libraries pages load from CDNs
  (`adapter.js`, PeerJS, simple-peer, the vendor SDKs of Daily, Twilio, Agora,
  LiveKit) are wrappers and signalling helpers *around* that native API. None
  of them contains a WebRTC engine, because a page cannot open a UDP socket.
  So reading 1 is already true of every browser that supports WebRTC: a
  browser with native WebRTC needs no library from the internet, and a browser
  *without* it cannot be rescued by one. The only thing a page-loaded library
  can do is detect that `RTCPeerConnection` is missing and give up.
- **Ladybird does not have WebRTC today.** Its `master`
  ships `getUserMedia` / `MediaStream` (merged 2026-04-21) and the Web Audio
  groundwork "needed to get webrtc working" (merged 2026-09-09), but no
  `RTCPeerConnection`. Its `vcpkg.json` lists no WebRTC engine.
- **A maintainer has already built it — in-tree, with no `libwebrtc`.**
  Ali Mohammad Pur's (`alimpfard`) `webrtc` branch (33 commits, 289 files,
  last pushed 2026-09-05) adds `Libraries/LibWeb/WebRTC/` with the full
  interface set (`RTCPeerConnection` through `RTCRtpScriptTransform`), a new
  `Services/WebRTCClient` helper process, and `Libraries/LibWebRTC/` — a
  **Rust crate** (`ladybird_webrtc`) on top of the pure-Rust `webrtc` crate
  0.17, `audiopus` for Opus and `cpal` for playback. Its `Documentation/WebRTC.md`
  records a live Discord voice call with the Krisp noise-suppression worklet
  running. Audio only: no video, no jitter buffer, no echo cancellation yet,
  and the renderer sandbox still kills WebContent when enabled (§3).
- **So the answer to the question is yes, on both readings — and the second
  one is a vendoring decision, not an engineering one.** The engine the branch
  uses is fetched by `cargo` from crates.io at build time, exactly like
  `vcpkg` fetches FFmpeg and OpenSSL for the rest of the browser. `cargo
  vendor` turns that into a checked-in `third_party/` directory with one
  command and a two-line `.cargo/config.toml`; the C++ alternatives
  (§4) vendor the same way. Writing the protocol stack itself from the RFCs —
  ICE, DTLS, SRTP, SCTP, RTP/RTCP, congestion control, audio processing — is
  possible and is what the four big engines did at various times, but it is
  the size of a browser subsystem (§5: Pion is ~100 k lines of Go for the
  transport alone, `libwebrtc` is millions), and nothing about "not loading
  from the internet" requires it.
- **Recommendation (§7):** track the upstream `webrtc` branch rather than
  starting a second implementation; vendor its crate graph into the
  demonstration build's tree the day that build rebases onto it; and expect
  the audio-processing gap (echo cancellation, noise suppression, gain) to
  stay open until upstream or a wrapper module fills it, because no small
  engine ships it. The follow-on question —
  whether that engine should instead sit behind an UltraCanvas-owned wrapper
  module that the browser *and* the native applications link — is answered
  yes in [`UltraRTCDesignProposal.md`](UltraRTCDesignProposal.md), which
  designs the module and the browser binding.

---

## 2. What "WebRTC support" is, and where it lives

WebRTC is a bundle of IETF protocols exposed through W3C JavaScript APIs. A
browser that "supports WebRTC" implements all of the following natively:

| Layer | Standard | What it does | Who implements it in Chrome / Firefox / Safari |
|---|---|---|---|
| **JavaScript API** | W3C WebRTC 1.0, Media Capture and Streams | `RTCPeerConnection`, `RTCDataChannel`, `getUserMedia`, `RTCRtpSender/Receiver/Transceiver`, stats | The engine (Blink / Gecko / WebKit), bound to the stack below |
| **Session description** | JSEP (RFC 8829), SDP (RFC 8866), BUNDLE (RFC 9143) | Offer/answer text the page relays through its own signalling channel | `libwebrtc` |
| **Connectivity** | ICE (RFC 8445), STUN (RFC 8489), TURN (RFC 8656), trickle (RFC 8838) | Finds a UDP path through NATs, falls back to a relay | `libwebrtc` |
| **Key exchange** | DTLS 1.2 (RFC 6347), DTLS-SRTP (RFC 5764) | Authenticated handshake over the ICE path; exports SRTP keys | `libwebrtc` + BoringSSL / NSS |
| **Media transport** | RTP/RTCP (RFC 3550), SRTP (RFC 3711), RTX, NACK, PLI/FIR, TWCC (the `transport-wide-cc` RTCP extension), REMB | Packetises encoded frames, recovers loss, measures the path | `libwebrtc` |
| **Congestion control** | Google Congestion Control (GCC), TWCC-based bandwidth estimation | Decides the bitrate so the call does not collapse the link | `libwebrtc` |
| **Jitter buffer & pacing** | — | Reorders, conceals loss, smooths playout; paces sends | `libwebrtc` (NetEQ for audio) |
| **Data channels** | SCTP over DTLS (RFC 8261, 8831, 8832) | Reliable / unreliable message channels | `libwebrtc` (dcSCTP) |
| **Codecs** | Opus (RFC 7587), VP8 (RFC 7741), VP9, H.264 (RFC 6184), AV1 | Encode and decode | `libwebrtc` + libopus, libvpx, OpenH264, dav1d / hardware |
| **Audio processing** | — | Acoustic echo cancellation (AEC3), noise suppression, automatic gain control | `libwebrtc` APM |
| **Capture & playback** | — | Microphone, camera, screen; speaker | The engine's platform layer |

**Every major engine vendors Google's `libwebrtc` for the middle rows.**
Firefox keeps it at `third_party/libwebrtc/` and "fast-forwards" it release by
release (Mozilla's documented vendoring process; recent bugs track versions
v153 → v154). WebKit keeps it at `Source/ThirdParty/libwebrtc`. Only the
GTK/WPE WebKit ports are moving off it, to GStreamer's `webrtcbin`, and the
reasons they give — BoringSSL's licence blocking GPL embedders, the tarball
footprint, fragile integration with their own decoders, no hardware encoding —
are the reasons an independent browser would give too.

**What the page-loaded libraries are.** The scripts a WebRTC site fetches
(`adapter.js`, `simple-peer`, `peerjs`, a vendor SDK) do three things: paper
over historical prefix and behaviour differences between browsers, hide
offer/answer and ICE-candidate plumbing behind a friendlier API, and talk to
the vendor's signalling and TURN servers. They run on top of
`window.RTCPeerConnection`. A page cannot implement WebRTC in JavaScript
because the Web platform exposes no UDP, no raw DTLS and no access to the
packetised media path; WebTransport and WebSockets are TCP/QUIC to a server,
not peer-to-peer UDP. Hence:

- a browser *with* native WebRTC never needs such a library to be present —
  the site may still load one, but it is a convenience, not the engine;
- a browser *without* native WebRTC cannot gain it from any script; the
  site's library will report "unsupported", which is what a call site's front
  end does when it finds no `RTCPeerConnection`.

The "library loaded from the internet" is therefore never the thing that
makes calls work. The question collapses into: **does the browser have a
native stack, and where does that stack's source come from.**

---

## 3. Where Ladybird stands (October 2026)

### 3.1 `master`

| Piece | State | Evidence |
|---|---|---|
| `getUserMedia`, `MediaStream`, `MediaStreamTrack`, `enumerateDevices` | **Merged** 2026-04-21 | PR 7909 "LibWeb+LibMedia: MediaCapture API wiring" (jonbgamble). Its own description said "no webRTC in sight" at the time |
| Microphone capture (PulseAudio, CoreAudio), `MediaStreamAudioSourceNode` / `DestinationNode`, `AnalyserNode`, `AudioWorklet` | **Merged** 2026-09-09 | PR 11209 "LibWeb: Most of the audio stuff needed to get webrtc working" (alimpfard) |
| Wasm threads-proposal atomics (non-shared paths) | **Merged** 2026-09-14 | PR 11789 — "more offshoots from my webrtc branch; this is enough for Krisp to run" |
| CSP `webrtc` directive | Merged 2025-08-07 | PR 5765 |
| `RTCPeerConnection` and everything under it | **Absent** | No `Libraries/LibWeb/WebRTC/` on `master`; no WebRTC engine in `vcpkg.json` (deps: angle, curl, ffmpeg, openssl, skia, sdl3, … — no libdatachannel, libjuice, libsrtp, usrsctp, webrtc) |
| Codecs | Present via FFmpeg | `vcpkg.json` builds FFmpeg with `avcodec, avformat, swresample, dav1d`; the `webrtc` branch adds `opus, openh264, vpx, theora, vorbis` |

So upstream has, deliberately and in merged form, laid the capture and audio
foundations; the peer-connection half is on a branch.

### 3.2 The `webrtc` branch (`alimpfard/ladybird`, pushed 2026-09-05)

33 commits, 289 files against upstream `master`. What it adds:

- **`Libraries/LibWeb/WebRTC/`** — IDL and C++ for `RTCPeerConnection`,
  `RTCSessionDescription`, `RTCIceCandidate`, `RTCDtlsTransport`,
  `RTCIceTransport`, `RTCSctpTransport`, `RTCRtpSender` / `Receiver` /
  `Transceiver`, `RTCDataChannel`, `RTCCertificate`, `RTCError`,
  `RTCStatsReport`, `RTCDTMFSender`, `RTCEncodedAudioFrame` /
  `VideoFrame`, `RTCRtpScriptTransform(er)`, the SFrame transforms, the
  event classes, and a process-wide `WebRTCAgent` that routes IPC replies and
  events by id.
- **`Services/WebRTCClient/`** — a new helper process, one per peer
  connection, started lazily by WebContent. `WebRTCClientClient.ipc` /
  `WebRTCClientServer.ipc` define the wire; `Meta+LibIPC: Generate Rust
  crates for IPC endpoints` teaches Ladybird's IPC compiler to emit Rust.
- **`Libraries/LibWebRTC/`** — `CMakeLists.txt` calls `build_rust_binary()` on
  `Rust/Cargo.toml`. The crate `ladybird_webrtc` (edition 2024) depends on
  `webrtc = "0.17.1"`, `tokio`, `audiopus` (Opus), `cpal` (interim audio
  playback), `serde`, `bytes`, `base64`, plus the generated IPC crates.
  **No `libwebrtc`, no `libdatachannel`, no C WebRTC dependency at all.**
- `Documentation/WebRTC.md` — the branch's own status page.

What that document says works and does not (quoted or paraphrased from it):

| Works | Does not yet |
|---|---|
| ICE, DTLS, RTP, Opus send and receive; transceiver negotiation and reuse; `replaceTrack()` keeping sender and SSRC | Video send, receive or rendering (H.264 is negotiated receive-only and never decoded) |
| Outgoing path: `MediaStreamTrack → WebAudio → resample 48 kHz → Opus → optional worker transform → Rust RTP/DTLS/ICE` | Incoming playback "is not driven by `HTMLMediaElement.srcObject`" — volume, pause, mute and sink selection cannot control it |
| `AudioWorklet` in the pipeline with bounded render-thread queues; Krisp runs at 2.31 ms per 10 ms frame | RTP reordering, loss concealment and jitter handling on receive |
| Per-peer cleanup; connections closed on document teardown | Echo cancellation, noise suppression, automatic gain |
| Basic transport statistics | Sender-parameter application, ICE glare, track-scoped stats, DTMF, SFrame natively, codec controls, certificate management, data-channel buffering and typed errors |
| Validated: 28 browser checks, 5 Rust service tests, Discord loopback with an echo bot, **one live speech call of acceptable quality** with uneven pacing | **Renderer sandbox**: WebContent terminates with `SIGSYS` when it is enabled; no long-running tests for navigation, helper death, device unplug, network change |

Two things about this matter for a browser built on UltraCanvas:

1. **It is Ladybird's own direction**, decided by Ladybird's maintainers, and
   the pieces are landing on their `master` one PR at a time (capture, Web
   Audio, Wasm atomics, next presumably the IPC-to-Rust generator). The
   demonstration build receives it by rebasing onto upstream; nobody in this
   project writes it, and this project has no say in it.
2. **It brings a Rust toolchain into the browser build.** This repository
   already has one (the Vectorizer plugin builds the `vtracer` crate through
   corrosion — `Docs/Dependencies.md`, row *cargo / rustc*), so a build
   environment that already compiles UltraCanvas will not be the first to
   need it.

---

## 4. The engines a browser could carry, compared

If a browser built on UltraCanvas ever has to choose for itself (Ladybird's
branch stalls, or video is wanted before upstream has it), these are the
realistic candidates. All
of them can be vendored; the *Fetched by* column is what each does out of the
box.

| Engine | Language, licence | Implements | Leaves to the browser | Build weight | Fetched by | Who ships it |
|---|---|---|---|---|---|---|
| **`libwebrtc`** (Google) | C++, BSD-3; BoringSSL inside | Everything in §2 including GCC congestion control, NetEQ jitter buffer, AEC3 / NS / AGC, simulcast, SVC, all codecs | Capture, rendering, the JS binding | Checkout 6.4 GB on Linux (its own docs), GN + Ninja + `depot_tools`; community CMake wrappers exist | `fetch webrtc` + `gclient sync` | Chrome, Firefox, Safari, Electron |
| **`webrtc` crate** (webrtc-rs) | Rust, MIT / Apache-2.0; `ring` crypto by default | ICE, DTLS, SRTP, SCTP, RTP/RTCP, interceptors (NACK, TWCC reports), data channels, a PeerConnection API with "95 %+ W3C compliance"; sans-IO core since 2026 | Codecs, jitter buffer, bandwidth *estimation* (TWCC feedback is produced, the estimator is yours), audio processing | A cargo dependency tree; compiles in minutes | `cargo` from crates.io | **Ladybird's `webrtc` branch**; many servers |
| **`str0m`** | Rust, MIT / Apache-2.0; pluggable crypto (aws-lc-rs, RustCrypto, OpenSSL, CryptoKit, CNG) | ICE, DTLS, SRTP, SCTP, RTP, **TWCC bandwidth estimation**, simulcast, NACK, packetisation, fixed depacketise buffer | Capture, codecs, adaptive jitter buffer, TURN management, interface enumeration; no PeerConnection-shaped API | Small; sans-IO, no threads | `cargo` | SFUs (Lookback, others) |
| **`libdatachannel`** + `libjuice` | C++17 / C, MPL-2.0 | Data channels (SCTP via `usrsctp`), media transport (SRTP via `libsrtp`, RTX), JSEP, trickle ICE; `libjuice` is a from-scratch ICE/STUN/TURN agent with no dependencies | Codecs, jitter buffer, congestion control, audio processing; BUNDLE-only | Small CMake tree; OpenSSL / GnuTLS / mbedTLS | `git` submodules or vcpkg | Many native apps, game engines |
| **GStreamer `webrtcbin`** | C, LGPL-2.1 | ICE (libnice), DTLS, SRTP, SCTP, RTP with GStreamer's jitter buffers and congestion control elements, codecs via plugins, hardware encode | Audio processing (`webrtcdsp` plugin wraps Google's APM) | Pulls in all of GStreamer | distro packages | WebKitGTK / WPE (in progress, FOSDEM 2026 talk), Servo (option under discussion) |
| **Own stack from the RFCs** | whatever the browser chooses | — | Everything | — | nothing | Nobody today; Pion (Go), str0m and webrtc-rs are the only from-scratch stacks that reached production |

Observations:

- **Only `libwebrtc` and `webrtcbin` carry a jitter buffer, a bandwidth
  estimator and audio processing.** Every lightweight engine stops at the
  transport. A browser that adopts one of them has to write or vendor those
  three pieces itself; the Ladybird branch's "known gaps" list is exactly
  that list. Standalone pieces exist: PulseAudio's `webrtc-audio-processing`
  package is Google's APM split out of `libwebrtc` (BSD-3, Meson build, ~50 k
  lines), and `speexdsp` is a smaller echo canceller and noise suppressor.
- **Codecs are a solved problem for a Ladybird-based build.** FFmpeg is
  already linked, and the branch simply enables `opus`, `openh264` and `vpx` features in vcpkg.
  Packetisation (RFC 7587 / 6184 / 7741) is in the Rust crates.
- **`libwebrtc` is the one that cannot reasonably be vendored into this
  port.** It needs Chromium's build system and a 6 GB checkout, its BoringSSL
  clashes with the OpenSSL the rest of the browser links, and Firefox and
  WebKit each keep a dedicated process to re-vendor it every release. That is
  why the branch does not use it and why the GTK ports are leaving it.

---

## 5. What "writing it ourselves" would mean

The question allows for the browser *creating* its own support, so the size
of that is worth stating plainly. The transport-only reference points:

| Stack | Scope | Size (order of magnitude) |
|---|---|---|
| Pion (Go) — `ice`, `dtls`, `srtp`, `sctp`, `rtp`, `rtcp`, `interceptor`, `webrtc` | Transport, no codecs, no jitter buffer, no BWE | ~100 k lines across the repositories; several years of a community |
| `libjuice` | ICE / STUN / TURN only, UDP only, single component | ~10 k lines of C |
| `usrsctp` | SCTP user-space stack (BSD kernel code extracted) | ~30 k lines of C |
| `str0m` | Transport incl. TWCC BWE and simulcast, sans-IO | ~50 k lines of Rust |
| `libwebrtc` | Everything | millions of lines |

What Ladybird already has in-tree that an own stack could build on: `LibTLS`
(TLS 1.2/1.3 over TCP — DTLS needs a different record layer: explicit
sequence numbers and epochs, the cookie exchange, handshake retransmission,
and the RFC 5764 key extractor), `LibCrypto` (AES-GCM, AES-CTR, HMAC-SHA1 —
enough for SRTP's two cipher suites), `LibDNS`, `LibCore` sockets, FFmpeg.
Missing entirely: ICE, SCTP, RTP/RTCP and everything above them.

A realistic plan for an own implementation that reaches *audio parity with
the branch* is the Pion-shaped list above minus SCTP (data channels can wait):
ICE agent with STUN and TURN, DTLS 1.2 handshake and record layer, SRTP,
RTP/RTCP with NACK/RTX, SDP/JSEP, Opus packetisation. Video parity with Chrome
adds the parts that took Google a decade of tuning — GCC, pacing, NetEQ-class
buffering, keyframe and layer management. Nothing about keeping the engine
off the network at build time needs any of this; vendoring does the same job
in an afternoon.

---

## 6. Keeping the engine out of the build's internet access

Whichever engine, the "loaded from the internet" part is answered the same
way Ladybird's build already answers it for FFmpeg, Skia and OpenSSL:

| Engine | How to pin it into the tree |
|---|---|
| Rust crates (`webrtc`, `str0m`, `audiopus`, …) | `cargo vendor third_party/rust` once; commit the directory; add `[source.crates-io] replace-with = "vendored"` / `[source.vendored] directory = "third_party/rust"` to `.cargo/config.toml`. Builds are then `--offline`-clean. `Cargo.lock` already pins every version and checksum, so the vendored tree is reproducible and auditable. |
| `libdatachannel` + `libjuice` + `libsrtp` + `usrsctp` | git submodules or a plain copy under `third_party/`, built with `add_subdirectory`; this repository's policy (`AGENTS.md`, *Third-party code*) is exactly that, with the licence rows in `THIRD_PARTY_LICENSES.md`. |
| `webrtc-audio-processing`, `speexdsp` | Same; Meson respectively autotools, both wrap in CMake easily. |
| `libwebrtc` | Not practical — see §4. |

The thing to decide is only *when* to vendor: the moment the demonstration
build rebases onto the branch, so that a release can be rebuilt from the tag
without crates.io or GitHub answering.

---

## 7. Recommendations

1. **Do not start a second WebRTC implementation for a Ladybird-based
   build.** Ladybird's maintainer is landing one, in-tree, with no
   `libwebrtc`, and its pieces are already reaching `master`. The
   demonstration build's only job is to rebase onto upstream; the open items
   (audio device routing, the sandbox profile that currently kills WebContent
   with `SIGSYS`) are Ladybird's to close, not this project's.
2. **Watch three upstream markers** and treat each as a trigger to re-test
   the demonstration build: the IPC-generator-to-Rust commit landing on `master`,
   `Libraries/LibWeb/WebRTC/` appearing, and the first release that ships
   `Services/WebRTCClient`. The branch's `Documentation/WebRTC.md` is the
   status page to read.
3. **Vendor the crate graph into the demonstration build's tree on
   adoption** (§6) so a release builds offline and the engine's exact sources
   are in the repository. Record the licences (`webrtc`, `ring`, `audiopus`,
   `cpal` and their trees are MIT / Apache-2.0 / ISC) in that build's licence
   file as this repository does in `THIRD_PARTY_LICENSES.md`.
4. **Plan for the three gaps every lightweight engine leaves** — jitter
   buffer, bandwidth estimation, audio processing. If upstream does not fill
   them by the time calls are wanted on a laptop microphone without
   headphones, the shortest path is `webrtc-audio-processing` (Google's
   APM, standalone) for AEC/NS/AGC, and `str0m`'s TWCC estimator as the
   reference for bandwidth estimation.
5. **Put the engine behind an UltraCanvas-owned wrapper and let the browser
   link that.** The framework rule that engines are wrapped behind an
   UltraCanvas-owned API (`UltraNet` over libcurl, `UltraCanvasJSON` over
   yyjson) applies here too, and Ladybird's helper-process design is the
   seam a library slots into. [`UltraRTCDesignProposal.md`](UltraRTCDesignProposal.md)
   lays out the module: the `UltraRtc_*` surface, the engine tiers, the media
   pipeline the framework already mostly has, and the one-call-per-W3C-method
   binding table for a Ladybird-based build.

---

## 8. Sources

- Ladybird upstream: `vcpkg.json` on `master`; PR 7909 (MediaCapture API
  wiring, merged 2026-04-21); PR 11209 (Web Audio for WebRTC, merged
  2026-09-09); PR 11789 (Wasm atomics, merged 2026-09-14); PR 5765 (CSP
  `webrtc` directive, 2025-08-07).
- `alimpfard/ladybird`, branch `webrtc` (2026-09-05): `Libraries/LibWebRTC/`
  (`CMakeLists.txt`, `Rust/Cargo.toml`), `Libraries/LibWebRTCClient/`,
  `Services/WebRTCClient/`, `Libraries/LibWeb/WebRTC/`, `Documentation/WebRTC.md`,
  and the branch's `vcpkg.json`.
- `libdatachannel` and `libjuice` READMEs (paullouisageneau); `str0m` README
  (algesten); `webrtc-rs/webrtc` README.
- WebRTC native-code development docs (chromium.googlesource.com, checkout
  sizes); Mozilla's libwebrtc vendoring process and bugs; WebKit
  `Source/ThirdParty/libwebrtc`; Igalia's "WebRTC support in WebKitGTK and
  WPEWebKit with GStreamer" (FOSDEM 2026) abstract; Servo issue 41396.
- W3C WebRTC 1.0; IETF RFCs 8445, 8489, 8656, 6347, 5764, 3550, 3711, 8261,
  8831, 8832, 8829, 7587, 6184, 7741.
