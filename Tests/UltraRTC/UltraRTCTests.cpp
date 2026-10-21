// Tests/UltraRTC/UltraRTCTests.cpp
// UltraRTC: peers, signalling pass-through, data channels, audio and video
// tracks, closing
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTC/UltraRTCCore.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;   // M_PI is not standard C++

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// What each side saw; written from engine threads (no dispatcher installed).
struct Side {
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcHandle channel = UltraRtcInvalidHandle;     // ours or the one the other side opened
    bool open = false;
    bool connected = false;
    int tracksOpen = 0;
    std::vector<std::string> texts;
    std::vector<std::vector<uint8_t>> binaries;
};

std::mutex g_mutex;
std::condition_variable g_changed;

bool WaitFor(const std::function<bool()>& condition, int seconds = 20) {
    std::unique_lock<std::mutex> lock(g_mutex);
    return g_changed.wait_for(lock, std::chrono::seconds(seconds), condition);
}

// The signalling channel: whatever one side produces goes to the other,
// untouched.
UltraRtcPeerEventCallback MakeHandler(Side& self, Side& other) {
    return [&self, &other](const UltraRtcPeerEvent& e) {
        switch (e.type) {
            case UltraRtcPeerEventType::LocalDescription:
                CHECK(UltraRtc_SetRemoteDescription(other.peer, e.sdpType, e.sdp));
                break;
            case UltraRtcPeerEventType::IceCandidate:
                UltraRtc_AddIceCandidate(other.peer, e.candidate, e.sdpMid);
                break;
            default:
                break;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        if (e.type == UltraRtcPeerEventType::DataChannel) {
            CHECK(e.channelLabel == "chat");
            self.channel = e.channel;
        }
        if (e.type == UltraRtcPeerEventType::DataChannelOpen) self.open = true;
        if (e.type == UltraRtcPeerEventType::TrackOpen) ++self.tracksOpen;
        if (e.type == UltraRtcPeerEventType::ConnectionStateChange &&
            e.connectionState == UltraRtcConnectionState::Connected)
            self.connected = true;
        if (e.type == UltraRtcPeerEventType::DataChannelMessage) {
            if (e.isBinary) self.binaries.push_back(e.binary);
            else self.texts.push_back(e.text);
        }
        g_changed.notify_all();
    };
}

void TestNotInitialized() {
    UltraRtcHandle peer = 7;
    UltraRtcResult r = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, peer);
    CHECK(!r);
    CHECK(peer == UltraRtcInvalidHandle);
}

void TestDataChannel() {
    Side a, b;
    UltraRtcPeerConfig config;   // no ICE servers: host candidates only
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(a, b), a.peer));
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(b, a), b.peer));
    CHECK(a.peer != b.peer);

    UltraRtcHandle channel = UltraRtcInvalidHandle;
    CHECK(UltraRtc_CreateDataChannel(a.peer, "chat", UltraRtcDataChannelInit(), channel));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        a.channel = channel;
    }
    CHECK(UltraRtc_GetDataChannelLabel(channel) == "chat");
    CHECK(UltraRtc_GetDataChannelPeer(channel) == a.peer);

    const bool bothOpen = WaitFor([&]() { return a.open && b.open && b.channel != UltraRtcInvalidHandle; });
    CHECK(bothOpen);
    if (!bothOpen) return;
    CHECK(UltraRtc_GetConnectionState(a.peer) == UltraRtcConnectionState::Connected);
    CHECK(UltraRtc_GetDataChannelState(a.channel) == UltraRtcDataChannelState::Open);

    UltraRtcSdpType type;
    std::string sdp;
    CHECK(UltraRtc_GetLocalDescription(a.peer, type, sdp) && type == UltraRtcSdpType::Offer);
    CHECK(sdp.find("a=fingerprint:") != std::string::npos);     // DTLS is always on

    CHECK(UltraRtc_DataChannelSendText(a.channel, "ping"));
    CHECK(WaitFor([&]() { return !b.texts.empty(); }));
    CHECK(!b.texts.empty() && b.texts[0] == "ping");

    const std::vector<uint8_t> payload = {0, 1, 2, 250, 255};
    CHECK(UltraRtc_DataChannelSendBinary(b.channel, payload));
    CHECK(WaitFor([&]() { return !a.binaries.empty(); }));
    CHECK(!a.binaries.empty() && a.binaries[0] == payload);

    CHECK(UltraRtc_ClosePeer(a.peer));
    CHECK(!UltraRtc_ClosePeer(a.peer));                          // already closed
    CHECK(UltraRtc_DataChannelSendText(a.channel, "late").code == UltraRtcResultCode::InvalidHandle);
    CHECK(UltraRtc_ClosePeer(b.peer));
}

// Received audio: how much arrived and what it sounds like.
struct Received {
    std::mutex mutex;
    std::vector<int16_t> samples;
    size_t frames = 0;
};

UltraRtcAudioFrameCallback Collect(Received& into) {
    return [&into](const UltraRtcAudioFrame& frame) {
        std::lock_guard<std::mutex> lock(into.mutex);
        CHECK(frame.sampleRate == 48000 && frame.channels == 1);
        into.samples.insert(into.samples.end(), frame.samples.begin(), frame.samples.end());
        ++into.frames;
    };
}

// Sends `seconds` of a sine, paced like a microphone (10 ms blocks, which
// the engine cuts into 20 ms codec frames).
void SendTone(UltraRtcHandle track, double hz, double seconds) {
    UltraRtcAudioFrame block;
    block.samples.resize(480);
    static size_t phase = 0;
    const int blocks = static_cast<int>(seconds * 100);
    for (int b = 0; b < blocks; ++b) {
        for (auto& s : block.samples) {
            s = static_cast<int16_t>(12000.0 * std::sin(2.0 * kPi * hz * static_cast<double>(phase++) / 48000.0));
        }
        CHECK(UltraRtc_SendAudioFrame(track, block));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// The loudness and the pitch (from zero crossings) of the newest `count`
// samples.
void Measure(Received& r, size_t count, double& rms, double& hz) {
    std::lock_guard<std::mutex> lock(r.mutex);
    rms = 0;
    hz = 0;
    if (r.samples.size() < count) return;
    const int16_t* s = r.samples.data() + (r.samples.size() - count);
    double sum = 0;
    int crossings = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += static_cast<double>(s[i]) * s[i];
        if (i > 0 && (s[i - 1] < 0) != (s[i] < 0)) ++crossings;
    }
    rms = std::sqrt(sum / static_cast<double>(count));
    hz = crossings / 2.0 / (static_cast<double>(count) / 48000.0);
}

void TestAudio() {
    Side a, b;
    UltraRtcPeerConfig config;
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(a, b), a.peer));
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(b, a), b.peer));

    // Both sides add the track before the offer: one shared m-line.
    UltraRtcHandle trackA = UltraRtcInvalidHandle, trackB = UltraRtcInvalidHandle;
    CHECK(UltraRtc_AddAudioTrack(a.peer, "audio", UltraRtcDirection::SendRecv, trackA));
    CHECK(UltraRtc_AddAudioTrack(b.peer, "audio", UltraRtcDirection::SendRecv, trackB));
    UltraRtcHandle unused = UltraRtcInvalidHandle;
    CHECK(!UltraRtc_AddAudioTrack(a.peer, "", UltraRtcDirection::SendRecv, unused));
    Received atA, atB;
    CHECK(UltraRtc_SetAudioFrameCallback(trackA, Collect(atA)));
    CHECK(UltraRtc_SetAudioFrameCallback(trackB, Collect(atB)));

    CHECK(UltraRtc_SetLocalDescription(a.peer, UltraRtcSdpType::Offer));
    const bool open = WaitFor([&]() { return a.tracksOpen > 0 && b.tracksOpen > 0; });
    CHECK(open);
    if (!open) return;
    CHECK(UltraRtc_IsTrackOpen(trackA) && UltraRtc_IsTrackOpen(trackB));
    UltraRtcSdpType type;
    std::string sdp;
    CHECK(UltraRtc_GetLocalDescription(a.peer, type, sdp) && sdp.find("opus/48000") != std::string::npos);

    UltraRtcAudioFrame wrongRate;
    wrongRate.sampleRate = 44100;
    wrongRate.samples.resize(441);
    CHECK(!UltraRtc_SendAudioFrame(trackA, wrongRate));

    // A to B: a 440 Hz tone arrives as a 440 Hz tone.
    SendTone(trackA, 440.0, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    double rms = 0, hz = 0;
    Measure(atB, 9600, rms, hz);
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        CHECK(atB.frames >= 40);
        std::printf("audio A->B: %zu frames, rms %.0f, %.0f Hz\n", atB.frames, rms, hz);
    }
    CHECK(rms > 4000.0);
    CHECK(std::fabs(hz - 440.0) < 20.0);

    // B to A, a different pitch.
    SendTone(trackB, 660.0, 0.6);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Measure(atA, 9600, rms, hz);
    CHECK(rms > 4000.0);
    CHECK(std::fabs(hz - 660.0) < 20.0);

    // Muted: nothing is sent.
    CHECK(UltraRtc_SetTrackEnabled(trackA, false));
    size_t before;
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        before = atB.frames;
    }
    SendTone(trackA, 440.0, 0.3);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        CHECK(atB.frames == before);
    }
    CHECK(UltraRtc_SetTrackEnabled(trackA, true));

    CHECK(UltraRtc_ClosePeer(a.peer));
    CHECK(UltraRtc_SendAudioFrame(trackA, wrongRate).code == UltraRtcResultCode::InvalidHandle);
    CHECK(!UltraRtc_IsTrackOpen(trackA));
    CHECK(UltraRtc_ClosePeer(b.peer));
}

// Received pictures: how many, their size, and the colour in the middle.
struct Pictures {
    std::mutex mutex;
    int count = 0;
    int width = 0;
    int height = 0;
    uint8_t centre[3] = {0, 0, 0};
};

UltraRtcVideoFrameCallback CollectPictures(Pictures& into) {
    return [&into](const UltraRtcVideoFrame& frame) {
        std::lock_guard<std::mutex> lock(into.mutex);
        ++into.count;
        into.width = frame.width;
        into.height = frame.height;
        const size_t at = (static_cast<size_t>(frame.height / 2) * static_cast<size_t>(frame.width) +
                           static_cast<size_t>(frame.width / 2)) * 4;
        if (frame.rgba.size() >= at + 3) {
            into.centre[0] = frame.rgba[at];
            into.centre[1] = frame.rgba[at + 1];
            into.centre[2] = frame.rgba[at + 2];
        }
    };
}

// Sends `count` pictures of one colour (a different size from the track's,
// so the scaler runs), paced like a camera.
void SendPictures(UltraRtcHandle track, int count, uint8_t r, uint8_t g, uint8_t b) {
    UltraRtcVideoFrame frame;
    frame.width = 400;
    frame.height = 300;
    frame.rgba.resize(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4);
    for (size_t i = 0; i < frame.rgba.size(); i += 4) {
        frame.rgba[i] = r;
        frame.rgba[i + 1] = g;
        frame.rgba[i + 2] = b;
        frame.rgba[i + 3] = 255;
    }
    for (int n = 0; n < count; ++n) {
        UltraRtc_SendVideoFrame(track, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

bool Near(int a, int b) {
    return a - b < 24 && b - a < 24;    // VP8 is lossy
}

void TestVideo() {
    Side a, b;
    UltraRtcPeerConfig config;
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(a, b), a.peer));
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(b, a), b.peer));

    UltraRtcVideoConfig video;
    video.width = 320;
    video.height = 240;
    video.fps = 25;
    video.bitrateKbps = 400;
    UltraRtcHandle trackA = UltraRtcInvalidHandle, trackB = UltraRtcInvalidHandle;
    CHECK(UltraRtc_AddVideoTrack(a.peer, "video", UltraRtcDirection::SendRecv, video, trackA));
    CHECK(UltraRtc_AddVideoTrack(b.peer, "video", UltraRtcDirection::SendRecv, video, trackB));
    UltraRtcVideoConfig tiny;
    tiny.width = 4;
    UltraRtcHandle unused = UltraRtcInvalidHandle;
    CHECK(!UltraRtc_AddVideoTrack(a.peer, "v2", UltraRtcDirection::SendRecv, tiny, unused));
    Pictures atA, atB;
    CHECK(UltraRtc_SetVideoFrameCallback(trackA, CollectPictures(atA)));
    CHECK(UltraRtc_SetVideoFrameCallback(trackB, CollectPictures(atB)));

    CHECK(UltraRtc_SetLocalDescription(a.peer, UltraRtcSdpType::Offer));
    const bool open = WaitFor([&]() { return a.tracksOpen > 0 && b.tracksOpen > 0; });
    CHECK(open);
    if (!open) return;
    UltraRtcSdpType type;
    std::string sdp;
    CHECK(UltraRtc_GetLocalDescription(a.peer, type, sdp) && sdp.find("VP8/90000") != std::string::npos);

    UltraRtcVideoFrame half;
    half.width = 10;
    half.height = 10;
    half.rgba.resize(10);
    CHECK(!UltraRtc_SendVideoFrame(trackA, half));

    // A to B: orange pictures arrive orange, at the track's size.
    SendPictures(trackA, 25, 240, 140, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        std::printf("video A->B: %d pictures %dx%d, centre %d,%d,%d\n", atB.count, atB.width, atB.height,
                    atB.centre[0], atB.centre[1], atB.centre[2]);
        CHECK(atB.count >= 15);
        CHECK(atB.width == 320 && atB.height == 240);
        CHECK(Near(atB.centre[0], 240) && Near(atB.centre[1], 140) && Near(atB.centre[2], 20));
    }

    // Disabled: nothing more goes out.
    CHECK(UltraRtc_SetTrackEnabled(trackA, false));
    int before;
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        before = atB.count;
    }
    SendPictures(trackA, 8, 240, 140, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        CHECK(atB.count == before);
    }
    // Enabled again: it resumes (from a key frame) in the new colour.
    CHECK(UltraRtc_SetTrackEnabled(trackA, true));
    SendPictures(trackA, 10, 30, 90, 220);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(atB.mutex);
        CHECK(atB.count > before);
        CHECK(Near(atB.centre[0], 30) && Near(atB.centre[1], 90) && Near(atB.centre[2], 220));
    }

    // B to A as well.
    SendPictures(trackB, 15, 20, 200, 60);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(atA.mutex);
        CHECK(atA.count >= 8);
        CHECK(Near(atA.centre[1], 200));
    }
    CHECK(UltraRtc_RequestKeyFrame(trackB));

    CHECK(UltraRtc_ClosePeer(a.peer));
    CHECK(UltraRtc_SendVideoFrame(trackA, half).code == UltraRtcResultCode::InvalidHandle);
    CHECK(UltraRtc_ClosePeer(b.peer));
}

} // namespace

int main() {
    TestNotInitialized();
    if (!UltraRtc_IsAvailable()) {
        std::printf("UltraRTCTests: no engine in this build - skipped\n");
        return 0;
    }
    CHECK(UltraRtc_Initialize());
    UltraRtcCapabilities caps = UltraRtc_GetCapabilities();
    CHECK(caps.available && caps.dataChannels);
    std::printf("UltraRTC engine: %s\n", caps.engine.c_str());
    TestDataChannel();
    if (caps.audio) {
        TestAudio();
    } else {
        std::printf("UltraRTCTests: no Opus in this build - audio skipped\n");
    }
    if (caps.video) {
        TestVideo();
    } else {
        std::printf("UltraRTCTests: no VP8 (FFmpeg) in this build - video skipped\n");
    }
    UltraRtc_Shutdown();
    std::printf("UltraRTCTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
