// Tests/UltraRTC/ApiStatus/UltraRtcApiStatus.h
// UltraRtcApiStatus: one probe per UltraRtc_* function, reporting WORKING /
// IMPLEMENTED / NOT IMPLEMENTED / BROKEN for this build on this machine.
//
// The registry, the runner and the text / Markdown / JSON renderers are
// UltraNetApiStatus's (Tests/UltraNet/ApiStatus/ApiStatus.h); this header
// gives them UltraRTC names and adds the peers the probes share.
//
// Every probe runs against peers this tool creates itself, two at a time in
// one process, connected over host candidates on the loopback and LAN
// interfaces: no STUN or TURN server, no signalling service, no internet.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include "../../UltraNet/ApiStatus/ApiStatus.h"

#include "UltraRTC/UltraRTCCore.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ultrartc_apistatus {

using namespace ultranet_apistatus;     // Outcome, Working(), Implemented(), ...

// ---------------------------------------------------------------------------
// What one side of a pair saw. Written from engine threads (the tool
// installs no dispatcher), read by the probes: both under Pair::mutex.
// ---------------------------------------------------------------------------
struct Side {
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcHandle channel = UltraRtcInvalidHandle;     // ours, or the one the other side opened
    UltraRtcHandle track = UltraRtcInvalidHandle;       // the media track added on this side
    bool channelOpen = false;
    bool connected = false;
    bool gatheringComplete = false;
    int  tracksOpen = 0;
    int  descriptionsSent = 0;          // LocalDescription events relayed to the other side
    int  descriptionsRefused = 0;       // ... that the other side's SetRemoteDescription refused
    int  candidatesSent = 0;            // IceCandidate events relayed
    int  candidatesRefused = 0;         // ... that the other side's AddIceCandidate refused
    std::string lastRefusal;
    std::vector<std::string>          texts;
    std::vector<std::vector<uint8_t>> binaries;
    std::vector<std::string>          channelsAnnounced;    // labels of DataChannel events
    std::vector<UltraRtcHandle>       channelsClosed;       // DataChannelClose events
    // Channels the other side opened after the first, by label.
    std::vector<std::pair<std::string, UltraRtcHandle>> extraChannels;
    std::vector<UltraRtcHandle>       channelsOpened;       // DataChannelOpen events
};

// Two peers of this process and the signalling between them: whatever one
// produces is handed to the other untouched, as an application's signalling
// channel would.
struct Pair {
    std::mutex              mutex;
    std::condition_variable changed;
    Side a;                 // the offering side
    Side b;                 // the answering side
    bool        built = false;      // an attempt was made (see ready / error)
    bool        ready = false;      // built and connected
    std::string error;              // why not, when !ready

    // Waits (under the mutex) until `condition` holds; false on timeout.
    bool WaitFor(const std::function<bool()>& condition, int milliseconds = 15000);
    // The event handler of one side; relays signalling to the other.
    UltraRtcPeerEventCallback Handler(bool forA);
    // Closes both peers. Safe to call twice.
    void Close();
};

// The three pairs, each built on first use and kept until the tool exits.
// A pair that cannot be built stays !ready with `error` set, and the probes
// that need it report that instead of each timing out again.
Pair& DataPair();       // one data channel "probe", opened by A
Pair& AudioPair();      // one Opus track per side, mid "audio"
Pair& VideoPair();      // one VP8 track per side, mid "video", 320 x 240

// Closes whatever was built and forgets it, so the next use builds afresh.
// Called before UltraRtc_Shutdown - at the end, and by the lifecycle probes
// that shut the engine down in the middle.
void ClosePairs();

// Polls `condition` every 10 ms; false when `milliseconds` pass first. For
// state no event announces (a temporary peer's counters, a getter).
bool WaitUntil(const std::function<bool()>& condition, int milliseconds = 5000);

// True, with `out` set to NOT IMPLEMENTED and the reason, when this build
// cannot do the thing at all; false when it can. Used through the macros
// below.
bool EngineMissing(Outcome& out);
bool AudioMissing(Outcome& out);
bool VideoMissing(Outcome& out);
// A pair that could not be built is BROKEN for every probe that needs it.
bool PairFailed(Pair& pair, Outcome& out);

// Makes sure the engine is initialized (the lifecycle probes shut it down
// and bring it back; a run filtered to one area never ran them).
void EnsureInitialized();

// Audio measured on the receiving side of AudioPair().
struct ToneResult {
    bool        sent = false;
    std::string sendError;
    size_t      frames = 0;         // 20 ms frames received
    bool        formatOk = true;    // every frame 48 kHz mono
    double      rms = 0;
    double      hz = 0;
};
// Sends `seconds` of a sine from A and reports what B decoded. Paced like a
// microphone (10 ms blocks).
ToneResult SendTone(double hz, double seconds);
// 20 ms frames B has decoded so far, from any source (a tone, a microphone).
size_t AudioFramesReceived();
// Puts B's collecting callback (back) on its track; AudioPair() installs it.
UltraRtcResult InstallAudioCollector();

// Pictures counted on the receiving side of VideoPair().
struct PictureResult {
    int     sendFailures = 0;
    int     count = 0;
    int     width = 0;
    int     height = 0;
    uint8_t centre[3] = {0, 0, 0};
};
// Sends `count` pictures of one colour from A (400 x 300, so the scaler
// runs) and reports what B decoded while they were sent.
PictureResult SendPictures(int count, uint8_t r, uint8_t g, uint8_t b);

} // namespace ultrartc_apistatus

// ---------------------------------------------------------------------------
// Probe declaration: ULTRARTC_PROBE("Area", UltraRtc_Function) { ... }
// ---------------------------------------------------------------------------
#define ULTRARTC_PROBE(areaName, symbol) ULTRANET_PROBE(areaName, symbol)

#define REQUIRE_ENGINE()                                                      \
    do {                                                                      \
        ::ultrartc_apistatus::Outcome missing;                                \
        if (::ultrartc_apistatus::EngineMissing(missing)) return missing;     \
        ::ultrartc_apistatus::EnsureInitialized();                            \
    } while (0)

#define REQUIRE_AUDIO()                                                       \
    do {                                                                      \
        REQUIRE_ENGINE();                                                     \
        ::ultrartc_apistatus::Outcome missing;                                \
        if (::ultrartc_apistatus::AudioMissing(missing)) return missing;      \
    } while (0)

#define REQUIRE_VIDEO()                                                       \
    do {                                                                      \
        REQUIRE_ENGINE();                                                     \
        ::ultrartc_apistatus::Outcome missing;                                \
        if (::ultrartc_apistatus::VideoMissing(missing)) return missing;      \
    } while (0)

#define REQUIRE_PAIR(pair)                                                    \
    do {                                                                      \
        ::ultrartc_apistatus::Outcome failed;                                 \
        if (::ultrartc_apistatus::PairFailed((pair), failed)) return failed;  \
    } while (0)
