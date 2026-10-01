- **Can WebRTC be linked into UltraCanvas as a wrapper-style module the
  browser and the applications share?** Two research documents answer it.
  `Docs/Research/BrowserWebRTCInvestigation.md` establishes the facts: WebRTC
  is a browser-native API, so the scripts a call site loads from a CDN are
  wrappers and never the engine; Ladybird — an independent third-party
  browser, used as the worked example because UltraCanvas once replaced its
  Qt UI layer in a demonstration build — has no `RTCPeerConnection` on
  `master` yet, but its maintainer's branch implements it in-tree as a helper
  process on the pure-Rust `webrtc` crate (Opus audio on a live
  Discord call; no video, no jitter buffer, sandbox still open), and the
  engines a browser can vendor (`libwebrtc`, `webrtc-rs`, `str0m`,
  `libdatachannel`, GStreamer `webrtcbin`, an own stack) are compared and
  sized. `Docs/Research/UltraRTCDesignProposal.md` then designs **UltraRTC**:
  an UltraCanvas-owned `UltraRtc_*` API over that engine (tier 2
  `libdatachannel`, the host browser's `RTCPeerConnection` on WebAssembly),
  the media pipeline built on the audio recorder, the IODeviceManager camera,
  libopus and FFmpeg the framework already carries, the jitter buffer,
  bandwidth estimator and vendored audio processing the engines leave out,
  and the one-call-per-W3C-method binding at that helper-process seam, for
  any browser that links the framework.
  Everything is vendored, so no library is fetched at build time — which is
  what "not loaded from the internet" comes down to. Proposal only, no code.
