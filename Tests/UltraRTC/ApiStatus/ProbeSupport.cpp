// Tests/UltraRTC/ApiStatus/ProbeSupport.cpp
// UltraRtcApiStatus: the peer pairs the probes share, and the tone and the
// pictures they send through them.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace ultrartc_apistatus {

namespace {

constexpr double kPi = 3.14159265358979323846;   // M_PI is not standard C++

// ---- what B decoded ----------------------------------------------------------

struct ReceivedAudio {
    std::mutex           mutex;
    std::vector<int16_t> samples;
    size_t               frames = 0;
    bool                 formatOk = true;
};
ReceivedAudio& Audio() {
    static ReceivedAudio* audio = new ReceivedAudio();   // engine threads may outlive main()
    return *audio;
}

struct ReceivedPictures {
    std::mutex mutex;
    int        count = 0;
    int        width = 0;
    int        height = 0;
    uint8_t    centre[3] = {0, 0, 0};
};
ReceivedPictures& Pictures() {
    static ReceivedPictures* pictures = new ReceivedPictures();
    return *pictures;
}

Pair& NewPair() {
    return *new Pair();     // never destroyed, for the same reason
}

void ResetSide(Side& side) { side = Side(); }

void ResetPair(Pair& pair) {
    std::lock_guard<std::mutex> lock(pair.mutex);
    ResetSide(pair.a);
    ResetSide(pair.b);
    pair.built = false;
    pair.ready = false;
    pair.error.clear();
}

// Creates the two peers of a pair. False, with pair.error set, when the
// engine refuses.
bool CreatePeers(Pair& pair) {
    UltraRtcPeerConfig config;      // no ICE servers: host candidates only
    UltraRtcHandle a = UltraRtcInvalidHandle, b = UltraRtcInvalidHandle;
    UltraRtcResult ra = UltraRtc_CreatePeer(config, pair.Handler(true), a);
    UltraRtcResult rb = UltraRtc_CreatePeer(config, pair.Handler(false), b);
    std::lock_guard<std::mutex> lock(pair.mutex);
    pair.a.peer = a;
    pair.b.peer = b;
    if (!ra || !rb) {
        pair.error = "UltraRtc_CreatePeer failed: " + (ra ? rb.message : ra.message);
        return false;
    }
    return true;
}

void Fail(Pair& pair, const std::string& why) {
    std::lock_guard<std::mutex> lock(pair.mutex);
    pair.ready = false;
    if (pair.error.empty()) pair.error = why;
}

bool Contains(const std::vector<UltraRtcHandle>& handles, UltraRtcHandle handle) {
    return std::find(handles.begin(), handles.end(), handle) != handles.end();
}

void BuildDataPair(Pair& pair) {
    if (!CreatePeers(pair)) return;
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    UltraRtcResult r = UltraRtc_CreateDataChannel(pair.a.peer, "probe", UltraRtcDataChannelInit(), channel);
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        pair.a.channel = channel;
    }
    if (!r) return Fail(pair, "UltraRtc_CreateDataChannel failed: " + r.message);
    const bool open = pair.WaitFor([&]() {
        return pair.b.channel != UltraRtcInvalidHandle && Contains(pair.a.channelsOpened, pair.a.channel) &&
               Contains(pair.b.channelsOpened, pair.b.channel);
    });
    if (!open) return Fail(pair, "the data channel did not open on both sides within 15 s");
    std::lock_guard<std::mutex> lock(pair.mutex);
    pair.a.channelOpen = pair.b.channelOpen = true;
    pair.ready = true;
}

void BuildAudioPair(Pair& pair) {
    if (!CreatePeers(pair)) return;
    // Both sides add the track before the offer: one shared m-line.
    UltraRtcHandle ta = UltraRtcInvalidHandle, tb = UltraRtcInvalidHandle;
    UltraRtcResult ra = UltraRtc_AddAudioTrack(pair.a.peer, "audio", UltraRtcDirection::SendRecv, ta);
    UltraRtcResult rb = UltraRtc_AddAudioTrack(pair.b.peer, "audio", UltraRtcDirection::SendRecv, tb);
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        pair.a.track = ta;
        pair.b.track = tb;
    }
    if (!ra || !rb) return Fail(pair, "UltraRtc_AddAudioTrack failed: " + (ra ? rb.message : ra.message));
    UltraRtcResult rc = InstallAudioCollector();
    if (!rc) return Fail(pair, "UltraRtc_SetAudioFrameCallback failed: " + rc.message);
    UltraRtcResult ro = UltraRtc_SetLocalDescription(pair.a.peer, UltraRtcSdpType::Offer);
    if (!ro) return Fail(pair, "UltraRtc_SetLocalDescription failed: " + ro.message);
    if (!pair.WaitFor([&]() { return pair.a.tracksOpen > 0 && pair.b.tracksOpen > 0; }))
        return Fail(pair, "the audio track did not open on both sides within 15 s");
    std::lock_guard<std::mutex> lock(pair.mutex);
    pair.ready = true;
}

void BuildVideoPair(Pair& pair) {
    if (!CreatePeers(pair)) return;
    UltraRtcVideoConfig video;
    video.width = 320;
    video.height = 240;
    video.fps = 25;
    video.bitrateKbps = 400;
    UltraRtcHandle ta = UltraRtcInvalidHandle, tb = UltraRtcInvalidHandle;
    UltraRtcResult ra = UltraRtc_AddVideoTrack(pair.a.peer, "video", UltraRtcDirection::SendRecv, video, ta);
    UltraRtcResult rb = UltraRtc_AddVideoTrack(pair.b.peer, "video", UltraRtcDirection::SendRecv, video, tb);
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        pair.a.track = ta;
        pair.b.track = tb;
    }
    if (!ra || !rb) return Fail(pair, "UltraRtc_AddVideoTrack failed: " + (ra ? rb.message : ra.message));
    UltraRtcResult rc = UltraRtc_SetVideoFrameCallback(tb, [](const UltraRtcVideoFrame& frame) {
        ReceivedPictures& p = Pictures();
        std::lock_guard<std::mutex> lock(p.mutex);
        ++p.count;
        p.width = frame.width;
        p.height = frame.height;
        const size_t at = (static_cast<size_t>(frame.height / 2) * static_cast<size_t>(frame.width) +
                           static_cast<size_t>(frame.width / 2)) * 4;
        if (frame.rgba.size() >= at + 3) {
            p.centre[0] = frame.rgba[at];
            p.centre[1] = frame.rgba[at + 1];
            p.centre[2] = frame.rgba[at + 2];
        }
    });
    if (!rc) return Fail(pair, "UltraRtc_SetVideoFrameCallback failed: " + rc.message);
    UltraRtcResult ro = UltraRtc_SetLocalDescription(pair.a.peer, UltraRtcSdpType::Offer);
    if (!ro) return Fail(pair, "UltraRtc_SetLocalDescription failed: " + ro.message);
    if (!pair.WaitFor([&]() { return pair.a.tracksOpen > 0 && pair.b.tracksOpen > 0; }))
        return Fail(pair, "the video track did not open on both sides within 15 s");
    std::lock_guard<std::mutex> lock(pair.mutex);
    pair.ready = true;
}

// Builds `pair` once per engine start; every later call returns it as it is.
Pair& Built(Pair& pair, void (*build)(Pair&)) {
    bool needed;
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        needed = !pair.built;
        pair.built = true;
    }
    if (needed) {
        EnsureInitialized();
        build(pair);
    }
    return pair;
}

Pair& RawDataPair()  { static Pair& pair = NewPair(); return pair; }
Pair& RawAudioPair() { static Pair& pair = NewPair(); return pair; }
Pair& RawVideoPair() { static Pair& pair = NewPair(); return pair; }

} // namespace

// ---------------------------------------------------------------------------

bool Pair::WaitFor(const std::function<bool()>& condition, int milliseconds) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::milliseconds(milliseconds), condition);
}

UltraRtcPeerEventCallback Pair::Handler(bool forA) {
    // The pair is never destroyed, so the engine may call this at any time.
    return [this, forA](const UltraRtcPeerEvent& e) {
        Side& self = forA ? a : b;
        Side& other = forA ? b : a;

        // Signalling: relayed untouched, outside the lock (applying it can
        // raise further events on this very thread).
        if (e.type == UltraRtcPeerEventType::LocalDescription || e.type == UltraRtcPeerEventType::IceCandidate) {
            UltraRtcHandle target;
            {
                std::lock_guard<std::mutex> lock(mutex);
                target = other.peer;
            }
            const bool isDescription = e.type == UltraRtcPeerEventType::LocalDescription;
            const UltraRtcResult r = isDescription ? UltraRtc_SetRemoteDescription(target, e.sdpType, e.sdp)
                                                   : UltraRtc_AddIceCandidate(target, e.candidate, e.sdpMid);
            std::lock_guard<std::mutex> lock(mutex);
            if (isDescription) {
                ++self.descriptionsSent;
                if (!r) ++self.descriptionsRefused;
            } else {
                ++self.candidatesSent;
                if (!r) ++self.candidatesRefused;
            }
            if (!r) self.lastRefusal = r.message;
            changed.notify_all();
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);
        switch (e.type) {
            case UltraRtcPeerEventType::DataChannel:
                self.channelsAnnounced.push_back(e.channelLabel);
                if (self.channel == UltraRtcInvalidHandle) self.channel = e.channel;
                else self.extraChannels.emplace_back(e.channelLabel, e.channel);
                break;
            case UltraRtcPeerEventType::DataChannelOpen:
                self.channelsOpened.push_back(e.channel);
                break;
            case UltraRtcPeerEventType::DataChannelClose:
                self.channelsClosed.push_back(e.channel);
                break;
            case UltraRtcPeerEventType::DataChannelMessage:
                if (e.isBinary) self.binaries.push_back(e.binary);
                else self.texts.push_back(e.text);
                break;
            case UltraRtcPeerEventType::TrackOpen:
                ++self.tracksOpen;
                break;
            case UltraRtcPeerEventType::ConnectionStateChange:
                if (e.connectionState == UltraRtcConnectionState::Connected) self.connected = true;
                break;
            case UltraRtcPeerEventType::IceGatheringStateChange:
                if (e.gatheringState == UltraRtcIceGatheringState::Complete) self.gatheringComplete = true;
                break;
            default:
                break;
        }
        changed.notify_all();
    };
}

void Pair::Close() {
    UltraRtcHandle pa, pb;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pa = a.peer;
        pb = b.peer;
    }
    if (pa != UltraRtcInvalidHandle) UltraRtc_ClosePeer(pa);
    if (pb != UltraRtcInvalidHandle) UltraRtc_ClosePeer(pb);
}

Pair& DataPair()  { return Built(RawDataPair(), BuildDataPair); }
Pair& AudioPair() { return Built(RawAudioPair(), BuildAudioPair); }
Pair& VideoPair() { return Built(RawVideoPair(), BuildVideoPair); }

void ClosePairs() {
    for (Pair* pair : {&RawDataPair(), &RawAudioPair(), &RawVideoPair()}) {
        pair->Close();
        ResetPair(*pair);
    }
}

bool WaitUntil(const std::function<bool()>& condition, int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

bool EngineMissing(Outcome& out) {
    if (UltraRtc_IsAvailable()) return false;
    out = NotImplemented("this build has no WebRTC engine (UltraRTC was built without OpenSSL)");
    return true;
}

bool AudioMissing(Outcome& out) {
    if (UltraRtc_GetCapabilities().audio) return false;
    out = NotImplemented("this build has no Opus codec (libopus was not found)");
    return true;
}

bool VideoMissing(Outcome& out) {
    if (UltraRtc_GetCapabilities().video) return false;
    out = NotImplemented("this build has no VP8 codec (FFmpeg with libvpx was not found)");
    return true;
}

bool PairFailed(Pair& pair, Outcome& out) {
    std::lock_guard<std::mutex> lock(pair.mutex);
    if (pair.ready) return false;
    out = Broken("two peers of this process could not be set up: " + pair.error);
    return true;
}

void EnsureInitialized() {
    UltraRtc_Initialize();      // a second call is harmless
}

// ---- audio -------------------------------------------------------------------

UltraRtcResult InstallAudioCollector() {
    Pair& pair = RawAudioPair();
    UltraRtcHandle track;
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        track = pair.b.track;
    }
    return UltraRtc_SetAudioFrameCallback(track, [](const UltraRtcAudioFrame& frame) {
        ReceivedAudio& audio = Audio();
        std::lock_guard<std::mutex> lock(audio.mutex);
        if (frame.sampleRate != 48000 || frame.channels != 1) audio.formatOk = false;
        audio.samples.insert(audio.samples.end(), frame.samples.begin(), frame.samples.end());
        ++audio.frames;
    });
}

size_t AudioFramesReceived() {
    ReceivedAudio& audio = Audio();
    std::lock_guard<std::mutex> lock(audio.mutex);
    return audio.frames;
}

ToneResult SendTone(double hz, double seconds) {
    ToneResult result;
    Pair& pair = RawAudioPair();
    UltraRtcHandle track;
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        track = pair.a.track;
    }
    ReceivedAudio& audio = Audio();
    size_t framesBefore, samplesBefore;
    {
        std::lock_guard<std::mutex> lock(audio.mutex);
        framesBefore = audio.frames;
        samplesBefore = audio.samples.size();
    }

    UltraRtcAudioFrame block;
    block.samples.resize(480);      // 10 ms; the engine cuts 20 ms codec frames
    static size_t phase = 0;
    const int blocks = static_cast<int>(seconds * 100);
    result.sent = true;
    for (int n = 0; n < blocks; ++n) {
        for (auto& s : block.samples) {
            s = static_cast<int16_t>(12000.0 * std::sin(2.0 * kPi * hz * static_cast<double>(phase++) / 48000.0));
        }
        const UltraRtcResult r = UltraRtc_SendAudioFrame(track, block);
        if (!r) {
            result.sent = false;
            result.sendError = r.message;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));    // the last packets in flight

    std::lock_guard<std::mutex> lock(audio.mutex);
    result.frames = audio.frames - framesBefore;
    result.formatOk = audio.formatOk;
    // Loudness and pitch (from zero crossings) of the newest 200 ms, or of
    // all that arrived when it was less.
    const size_t arrived = audio.samples.size() - samplesBefore;
    const size_t count = std::min<size_t>(arrived, 9600);
    if (count > 1) {
        const int16_t* s = audio.samples.data() + (audio.samples.size() - count);
        double sum = 0;
        int crossings = 0;
        for (size_t i = 0; i < count; ++i) {
            sum += static_cast<double>(s[i]) * s[i];
            if (i > 0 && (s[i - 1] < 0) != (s[i] < 0)) ++crossings;
        }
        result.rms = std::sqrt(sum / static_cast<double>(count));
        result.hz = crossings / 2.0 / (static_cast<double>(count) / 48000.0);
    }
    // Keep the buffer from growing over a long run.
    if (audio.samples.size() > 48000 * 4) audio.samples.erase(audio.samples.begin(), audio.samples.end() - 48000);
    return result;
}

// ---- video -------------------------------------------------------------------

PictureResult SendPictures(int count, uint8_t r, uint8_t g, uint8_t b) {
    PictureResult result;
    Pair& pair = RawVideoPair();
    UltraRtcHandle track;
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        track = pair.a.track;
    }
    ReceivedPictures& pictures = Pictures();
    int before;
    {
        std::lock_guard<std::mutex> lock(pictures.mutex);
        before = pictures.count;
    }

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
        if (!UltraRtc_SendVideoFrame(track, frame)) ++result.sendFailures;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));     // a 25 fps camera
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::lock_guard<std::mutex> lock(pictures.mutex);
    result.count = pictures.count - before;
    result.width = pictures.width;
    result.height = pictures.height;
    result.centre[0] = pictures.centre[0];
    result.centre[1] = pictures.centre[1];
    result.centre[2] = pictures.centre[2];
    return result;
}

} // namespace ultrartc_apistatus
