- **UltraRTC: `UltraRtcApiStatus` reports what every function does in this
  build, on this machine.** The module's proposal asked each phase to ship
  the counterpart of `UltraNetApiStatus`; it was the one piece of phase 0
  still missing. The tool has one probe for each of the 40 functions in
  `UltraRTCCore.h` and prints WORKING / IMPLEMENTED / NOT IMPLEMENTED /
  BROKEN per function, as text, Markdown or JSON. It needs no network
  service: the probes run between peers of its own process, connected over
  host candidates, with a data pair (text, every byte value, a second channel
  closed, a 4 MiB burst for the buffered amount), an audio pair (a sine sent
  like a microphone, counted and measured for loudness and pitch at the other
  side) and a video pair (pictures scaled to the track and compared by
  colour). On a Linux build with OpenSSL, libopus and FFmpeg in a container
  it reports 33 working and 7 implemented: the device functions, which have
  no microphone, speaker or camera there, and `UltraRtc_RequestKeyFrame`,
  whose effect the API has no accessor for. In a build without OpenSSL it
  reports the three functions that still answer as working and the other 37
  as not implemented. `UltraRtcApiStatusCoverage` fails when a function in
  the header has no probe, so a new function cannot be absent from a report
  that reads as complete. The report framework is UltraNet's, now shared:
  `SetReportIdentity` gives a tool its own title and area order, and
  `UltraNetApiStatus` is unchanged. Built with
  `-DULTRACANVAS_BUILD_ULTRARTC_TESTS=ON`; doc
  `Docs/Modules/UltraRTC/ApiStatus.md`.
  - **New: `UltraRtc_GetVersion()`,** the version of the `UltraRtc_*`
    surface ("0.1.0"), answered with or without an engine and before
    `UltraRtc_Initialize` — the lifecycle call the proposal listed and the
    module lacked, named as `UltraNet_GetVersion` and its siblings are.
  - **Fixed: the engine wrote its warnings to stdout.** libdatachannel logs
    to `std::cout` unless given a callback, so a line such as `juice: UDP
    socket creation failed` landed in the middle of whatever a command-line
    program using UltraRTC printed there; the status tool's JSON did not
    parse. The warnings now go to stderr, one line each, prefixed `UltraRTC
    engine warning:`.
  - The proposal's status line said phase 1; it now says what is there
    (phase 0 complete, peers, data channels, audio and video implemented)
    and what is not.
