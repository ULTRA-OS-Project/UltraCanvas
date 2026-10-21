- **UltraRTC: WebRTC peer connections and data channels.** A new UI-free
  module (`UltraCanvas/{include,core}/UltraRTC/`, target `UltraRTC`) wraps
  libdatachannel 0.24.6 - vendored with libjuice, usrsctp, libsrtp and plog
  under `UltraCanvas/third_party/libdatachannel/` - behind `UltraRtc_*`
  functions and opaque handles: peers with STUN / TURN servers, SDP and
  candidates passed through untouched, DTLS always on, the W3C state
  machines as events through an injectable dispatcher, and data channels
  carrying text and binary messages. Built where OpenSSL is found; without
  it every call answers `Unsupported`. ChatCube's peer-to-peer chat runs on
  it; audio and video tracks are next. Doc: `Docs/Modules/UltraRTC/README.md`;
  tests: `Tests/UltraRTC` (two peers in one process).
