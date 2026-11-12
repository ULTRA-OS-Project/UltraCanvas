// Tests/UltraRTC/ApiStatus/ProbeAudio.cpp
// UltraRtcApiStatus: audio - the Opus track, PCM in and decoded frames out,
// muting, and the microphone and the speaker.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace ultrartc_apistatus;

namespace {

std::string Rounded(double value) {
    return std::to_string(std::lround(value));
}

// Frames that reached the other side while `milliseconds` passed.
size_t FramesDuring(int milliseconds) {
    const size_t before = AudioFramesReceived();
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return AudioFramesReceived() - before;
}

} // namespace

ULTRARTC_PROBE("Audio", UltraRtc_AddAudioTrack) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(pair.a.track != UltraRtcInvalidHandle && pair.b.track != UltraRtcInvalidHandle);
    UltraRtcSdpType type = UltraRtcSdpType::Unspecified;
    std::string sdp;
    PROBE_EXPECT(UltraRtc_GetLocalDescription(pair.a.peer, type, sdp));
    PROBE_EXPECT_MSG(sdp.find("opus/48000") != std::string::npos, "the offer does not name Opus at 48 kHz");
    UltraRtcHandle refused = 7;
    const UltraRtcResult noMid = UltraRtc_AddAudioTrack(pair.a.peer, "", UltraRtcDirection::SendRecv, refused);
    PROBE_EXPECT_MSG(!noMid && refused == UltraRtcInvalidHandle, "a track without a mid was accepted");
    PROBE_EXPECT(UltraRtc_AddAudioTrack(UltraRtcInvalidHandle, "audio", UltraRtcDirection::SendRecv, refused).code ==
                 UltraRtcResultCode::InvalidHandle);
    return Working("a track added on both sides with one mid negotiated as Opus at 48 kHz and opened; a track "
                   "without a mid is refused");
}

ULTRARTC_PROBE("Audio", UltraRtc_SendAudioFrame) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    const ToneResult tone = SendTone(440.0, 0.6);
    PROBE_EXPECT_MSG(tone.sent, tone.sendError);
    PROBE_EXPECT_MSG(tone.frames >= 15, "0.6 s of PCM arrived as only " + std::to_string(tone.frames) +
                                            " frames of 20 ms");
    UltraRtcAudioFrame wrongRate;
    wrongRate.sampleRate = 44100;
    wrongRate.samples.resize(441);
    PROBE_EXPECT_MSG(!UltraRtc_SendAudioFrame(pair.a.track, wrongRate), "PCM that is not 48 kHz was accepted");
    PROBE_EXPECT(UltraRtc_SendAudioFrame(UltraRtcInvalidHandle, wrongRate).code == UltraRtcResultCode::InvalidHandle);
    return Working("0.6 s of PCM in 10 ms blocks reached the other side as " + std::to_string(tone.frames) +
                   " Opus frames; PCM at another rate is refused");
}

ULTRARTC_PROBE("Audio", UltraRtc_SetAudioFrameCallback) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    const ToneResult tone = SendTone(660.0, 0.6);
    PROBE_EXPECT_MSG(tone.sent, tone.sendError);
    PROBE_EXPECT_MSG(tone.frames >= 15, "the callback got only " + std::to_string(tone.frames) + " frames");
    PROBE_EXPECT_MSG(tone.formatOk, "a frame was not 48 kHz mono");
    PROBE_EXPECT_MSG(tone.rms > 4000.0, "what arrived is too quiet to be the tone (rms " + Rounded(tone.rms) + ")");
    PROBE_EXPECT_MSG(std::fabs(tone.hz - 660.0) < 25.0, "a 660 Hz tone arrived as " + Rounded(tone.hz) + " Hz");

    // nullptr removes it: the same tone, and nothing is delivered.
    PROBE_EXPECT(UltraRtc_SetAudioFrameCallback(pair.b.track, nullptr));
    const ToneResult silent = SendTone(660.0, 0.3);
    const UltraRtcResult restored = InstallAudioCollector();
    PROBE_EXPECT_MSG(restored, restored.message);
    PROBE_EXPECT_MSG(silent.frames == 0, "frames were still delivered after the callback was removed");
    return Working("decoded 20 ms frames at 48 kHz mono: a 660 Hz tone arrived as " + Rounded(tone.hz) +
                   " Hz; nullptr stops the delivery");
}

ULTRARTC_PROBE("Audio", UltraRtc_SetTrackEnabled) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_SetTrackEnabled(pair.a.track, false));
    const ToneResult muted = SendTone(440.0, 0.3);
    const UltraRtcResult enabled = UltraRtc_SetTrackEnabled(pair.a.track, true);
    PROBE_EXPECT_MSG(enabled, enabled.message);
    PROBE_EXPECT_MSG(muted.frames == 0, std::to_string(muted.frames) + " frames were sent from a disabled track");
    const ToneResult audible = SendTone(440.0, 0.4);
    PROBE_EXPECT_MSG(audible.frames >= 10, "the track did not send again once enabled");
    PROBE_EXPECT(UltraRtc_SetTrackEnabled(UltraRtcInvalidHandle, true).code == UltraRtcResultCode::InvalidHandle);
    return Working("a disabled track sent nothing, and sent again once enabled");
}

ULTRARTC_PROBE("Audio", UltraRtc_IsTrackOpen) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_IsTrackOpen(pair.a.track) && UltraRtc_IsTrackOpen(pair.b.track));
    PROBE_EXPECT(!UltraRtc_IsTrackOpen(UltraRtcInvalidHandle));

    UltraRtcHandle lone = UltraRtcInvalidHandle, waiting = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, lone));
    const bool added = static_cast<bool>(UltraRtc_AddAudioTrack(lone, "audio", UltraRtcDirection::SendRecv, waiting));
    const bool openEarly = UltraRtc_IsTrackOpen(waiting);
    UltraRtc_ClosePeer(lone);
    PROBE_EXPECT(added);
    PROBE_EXPECT_MSG(!openEarly, "a track on a peer nobody answered reads open");
    PROBE_EXPECT_MSG(!UltraRtc_IsTrackOpen(waiting), "a track reads open after its peer was closed");
    return Working("true on both sides of the connected pair (after the TrackOpen events), false before the "
                   "connection and after the peer closed");
}

ULTRARTC_PROBE("Audio", UltraRtc_StartMicrophone) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_StartMicrophone(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    const UltraRtcResult started = UltraRtc_StartMicrophone(pair.a.track);
    if (!started) return Implemented("no microphone could be opened on this machine: " + started.message);
    const size_t frames = FramesDuring(1000);
    UltraRtc_StopMicrophone(pair.a.track);
    if (frames == 0) return Implemented("the default input device opened but delivered no audio in 1 s");
    return Working("the default microphone fed the track: " + std::to_string(frames) +
                   " frames reached the other side in 1 s");
}

ULTRARTC_PROBE("Audio", UltraRtc_StopMicrophone) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_StopMicrophone(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    PROBE_EXPECT_MSG(UltraRtc_StopMicrophone(pair.a.track), "stopping a microphone that was never started failed");
    const UltraRtcResult started = UltraRtc_StartMicrophone(pair.a.track);
    if (!started) return Implemented("no microphone on this machine to stop; with none started it answers Ok");
    const size_t running = FramesDuring(600);
    const UltraRtcResult stopped = UltraRtc_StopMicrophone(pair.a.track);
    PROBE_EXPECT_MSG(stopped, stopped.message);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));    // what was in flight
    const size_t after = FramesDuring(600);
    if (running == 0) return Implemented("the device opened and closed, but it delivered no audio to stop");
    PROBE_EXPECT_MSG(after == 0, std::to_string(after) + " frames still arrived after the microphone was stopped");
    return Working("the microphone's audio stopped arriving at the other side");
}

ULTRARTC_PROBE("Audio", UltraRtc_StartPlayback) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_StartPlayback(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    const UltraRtcResult started = UltraRtc_StartPlayback(pair.b.track);
    if (!started) return Implemented("no output device could be opened on this machine: " + started.message);
    const ToneResult tone = SendTone(440.0, 0.4);       // something to play
    UltraRtc_StopPlayback(pair.b.track);
    PROBE_EXPECT_MSG(tone.frames > 0, "with playback on, the frame callback got nothing");
    return Implemented("the default output device opened and was fed the received audio; what it played cannot "
                       "be heard from here");
}

ULTRARTC_PROBE("Audio", UltraRtc_StopPlayback) {
    REQUIRE_AUDIO();
    Pair& pair = AudioPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_StopPlayback(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    PROBE_EXPECT_MSG(UltraRtc_StopPlayback(pair.b.track), "stopping playback that was never started failed");
    const UltraRtcResult started = UltraRtc_StartPlayback(pair.b.track);
    if (!started) return Implemented("no output device on this machine to stop; with none started it answers Ok");
    const UltraRtcResult stopped = UltraRtc_StopPlayback(pair.b.track);
    PROBE_EXPECT_MSG(stopped, stopped.message);
    const ToneResult tone = SendTone(440.0, 0.3);
    PROBE_EXPECT_MSG(tone.frames > 0, "the track stopped receiving when playback was stopped");
    return Implemented("the output device opened and closed and the track kept receiving; the silence cannot be "
                       "heard from here");
}
