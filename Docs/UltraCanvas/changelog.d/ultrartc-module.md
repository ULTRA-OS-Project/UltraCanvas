- **UltraRTC: WebRTC peer connections and data channels.** A new UI-free
  module (`UltraCanvas/{include,core}/UltraRTC/`, target `UltraRTC`) wraps
  libdatachannel 0.24.6 - vendored with libjuice, usrsctp, libsrtp and plog
  under `UltraCanvas/third_party/libdatachannel/` - behind `UltraRtc_*`
  functions and opaque handles: peers with STUN / TURN servers, SDP and
  candidates passed through untouched, DTLS always on, the W3C state
  machines as events through an injectable dispatcher, and data channels
  carrying text and binary messages. Built where OpenSSL is found; without
  it every call answers `Unsupported`. Audio tracks: Opus (48 kHz
  mono, in-band FEC, loss concealment) with the microphone and the speaker
  through `UltraCanvasAudioRecorder` (live mode) and `UltraCanvasAudioPlayer`
  (push sink), or fed and read by the application; needs libopus.
  Video tracks: VP8 (libvpx through FFmpeg's libavcodec, an RFC 7741 payload
  format of UltraRTC's own, key-frame recovery by RTCP PLI after a loss)
  with the camera through `UltraCanvasVideoRecorder`'s live preview, or fed
  and read by the application as RGBA. The RTCP handlers that answer the
  receiver (NACK, PLI) now sit where its reports reach them. ChatCube's
  peer-to-peer chat, voice and video calls run on it. Doc: `Docs/Modules/UltraRTC/README.md`;
  tests: `Tests/UltraRTC` (two peers in one process).
