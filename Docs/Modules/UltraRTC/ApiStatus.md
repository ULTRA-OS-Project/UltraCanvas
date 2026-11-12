# UltraRTC API status report (`UltraRtcApiStatus`)

A console tool that walks the **entire public UltraRTC surface** — every
function in `UltraRTC/UltraRTCCore.h` — and reports what state each is in
*in this build, on the machine running it*.

It answers a different question from `UltraRTCTests`. The test suite answers
"does the code still behave?" and prints pass or fail. This tool answers
"which parts of the API can I rely on here?" — in a build that may have no
engine, no Opus or no VP8, on a machine that may have no microphone or camera
— and prints one of four statuses per function.

| Status | Meaning |
|---|---|
| **WORKING** | The probe drove the real code path between two peers of its own process and the result matched what the API promises. |
| **IMPLEMENTED** | The implementation exists and was reached, but this machine cannot confirm the behaviour: no microphone, no camera, nothing to hear a speaker with, or an effect the API has no accessor for. |
| **NOT IMPLEMENTED** | This build lacks what the function needs: the engine (no OpenSSL), Opus (no libopus) or VP8 (no FFmpeg with libvpx). |
| **BROKEN** | The probe ran and the result contradicted the API. |

`BROKEN` exists on purpose: a status tool that can only report good news
would turn a regression into a reassuring "implemented".

It is the UltraRTC counterpart of
[`UltraNetApiStatus`](../UltraNet/ApiStatus.md) and shares its framework: the
probe registry, the runner and the three renderers are one piece of code
(`Tests/UltraNet/ApiStatus/ApiStatusReport.cpp`), compiled into both tools.

---

## Building and running

```bash
cmake -S . -B build -DULTRACANVAS_BUILD_ULTRARTC_TESTS=ON
cmake --build build --target UltraRtcApiStatus
./build/UltraRtcApiStatus
```

It is registered with CTest, where it fails only when a function is `BROKEN`,
next to the check that every function has a probe:

```bash
ctest --test-dir build -R UltraRtcApiStatus
```

### Options

| Option | Effect |
|---|---|
| `--format=text\|markdown\|json` | Report format (default `text`). |
| `--area=<name>` | Probe one area only: `Lifecycle`, `Peers`, `DataChannels`, `Audio`, `Video`. |
| `--output=<path>` | Write the report to a file instead of stdout. |
| `--strict` | Exit non-zero unless **every** entry is `WORKING`. |

Exit code is `0` when nothing is `BROKEN` (and, with `--strict`, when
everything is `WORKING`), `1` otherwise, `2` on a usage error. The report is
the only thing on stdout: the line naming the API version and the engine, the
progress ticker and the engine's own warnings go to stderr, so
`--format=json | jq` works.

---

## What a run looks like

A Linux build with OpenSSL, libopus and FFmpeg, on a machine with no sound
card and no camera (a CI runner, a container), takes about eight seconds:

```
Lifecycle  [6 working / 6 probed]
  UltraRtc_GetVersion                    WORKING          "0.1.0"
  UltraRtc_GetCapabilities               WORKING          libdatachannel 0.24.6; data channels yes, audio yes, video yes
  UltraRtc_Shutdown                      WORKING          closes the open peers and refuses new ones; the engine starts again afterwards
  ...
Audio  [5 working / 9 probed]
  UltraRtc_SetAudioFrameCallback         WORKING          decoded 20 ms frames at 48 kHz mono: a 660 Hz tone arrived as 660 Hz; nullptr stops the delivery
  UltraRtc_StartMicrophone               IMPLEMENTED      no microphone could be opened on this machine: failed to open input stream
  ...
TOTAL 40 API entries probed:  33 working, 7 implemented (unverified here), 0 not implemented, 0 broken
```

The seven are the four device functions for audio, the two for the camera,
and `UltraRtc_RequestKeyFrame`, whose effect the API has no accessor for. On
a machine with a microphone and a camera the device entries turn `WORKING`
(the microphone's audio is counted at the other peer, the camera's pictures
at the self view); the speaker stays `IMPLEMENTED` everywhere, because
nothing in the process can hear it.

In a build without OpenSSL the module is the stub: `UltraRtc_GetVersion`,
`UltraRtc_IsAvailable` and `UltraRtc_GetCapabilities` are `WORKING` (they
answer truthfully that there is no engine) and the other 37 are
`NOT IMPLEMENTED`.

---

## How it verifies things without a network

Every probe runs against peers the tool creates itself, two at a time in one
process, with the signalling between them done by the tool: whatever one
peer produces as a `LocalDescription` or `IceCandidate` event is handed to
the other untouched, as an application's signalling channel would. No ICE
server is configured, so the peers connect over host candidates. No STUN or
TURN server, no signalling service and no internet access are involved.

Three pairs are built, each on first use and kept for the run:

* **the data pair** — one data channel, `probe`, opened by the caller. The
  peer, signalling, state and data-channel probes read and use it: a UTF-8
  text one way, all 256 byte values the other, a second channel opened and
  closed, a 4 MiB burst to see the buffered amount rise and drain.
* **the audio pair** — one Opus track per side. A sine is sent in 10 ms
  blocks, paced like a microphone; what the other side decodes is counted
  and measured, loudness and pitch (from zero crossings), so "a 660 Hz tone
  arrived as 660 Hz" is a measurement.
* **the video pair** — one VP8 track per side at 320 x 240. Pictures of one
  colour are sent at 400 x 300, so the scaler runs; what arrives is counted
  and its size and centre colour compared.

Probes that need a state of their own — a peer with `autoNegotiation` off, a
peer that is closed, a channel nobody answers — create a peer for themselves
and close it. The lifecycle probes shut the engine down and start it again;
that a second `UltraRtc_Initialize` in one process works is one of the
things reported.

---

## Adding a probe

Probes live in `Tests/UltraRTC/ApiStatus/Probe<Area>.cpp`, one per function,
in header order (the report keeps declaration order within an area).
`UltraRtcApiStatus.h` has the pairs and the helpers.

```cpp
ULTRARTC_PROBE("DataChannels", UltraRtc_GetDataChannelLabel) {
    REQUIRE_ENGINE();                       // NOT IMPLEMENTED without one
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);                     // BROKEN if two peers cannot connect
    PROBE_EXPECT(UltraRtc_GetDataChannelLabel(pair.a.channel) == "probe");
    PROBE_EXPECT_MSG(UltraRtc_GetDataChannelLabel(pair.b.channel) == "probe",
                     "the label did not reach the other side");
    return Working("\"probe\" on the side that opened the channel and on the side that received it");
}
```

Rules that keep the report trustworthy:

* A failed `PROBE_EXPECT` means the API misbehaved: `BROKEN`, never
  "unverified".
* `Working` only for what the probe observed. A call that returned Ok
  without an observable effect is `Implemented`, and the detail says what
  could not be seen.
* `REQUIRE_ENGINE()`, `REQUIRE_AUDIO()` and `REQUIRE_VIDEO()` first: what a
  build lacks is `NOT IMPLEMENTED`, with the missing library named.
* A probe leaves the shared pair as it found it — a track enabled again, the
  collecting callback back in place, a device stopped.
* **Every function has exactly one probe.** The `UltraRtcApiStatusCoverage`
  test reads `UltraRTCCore.h` and the probe files and fails when a function
  has none, or a probe names a function the header does not declare. A
  function added to the header without a probe fails CI rather than being
  absent from a report that then reads as complete.
