// core/UltraRTC/UltraRTCEngine.cpp
// UltraRTC - the UltraRtc_* API over libdatachannel (peers, data channels,
// audio and video tracks)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTC/UltraRTCCore.h"
#include "UltraRTCAudioDevices.h"
#include "UltraRTCOpus.h"
#include "UltraRTCVideoCodec.h"
#include "UltraRTCVideoDevices.h"
#include "UltraRTCVp8Rtp.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <variant>

namespace {

// ============================================================================
// HANDLE TABLE
// The engine's objects live only here. The callbacks registered on them
// capture handles, never the objects, so an object never owns itself and
// closing a peer from inside one of its own callbacks is safe.
// ============================================================================

struct PeerEntry {
    std::shared_ptr<rtc::PeerConnection> connection;
    std::shared_ptr<UltraRtcPeerEventCallback> onEvent;
    std::set<UltraRtcHandle> channels;
    std::set<UltraRtcHandle> tracks;
};

// What an audio track's callbacks share. The track's callbacks own it; it
// points back at the track weakly, so nothing owns itself.
struct AudioMedia {
    // ---- sending (the microphone's thread, or a caller of SendAudioFrame) ----
    std::mutex sendMutex;
    UltraRtcInternal::OpusEncoder encoder;
    std::vector<int16_t> pending;           // less than one 20 ms frame
    uint32_t nextTimestamp = 0;
    bool enabled = true;
    bool canSend = false;                   // the track has our packetizer
    std::weak_ptr<rtc::Track> track;

    // ---- receiving (an engine thread) ----
    std::mutex receiveMutex;
    UltraRtcInternal::OpusDecoder decoder;
    bool haveTimestamp = false;
    uint32_t expectedTimestamp = 0;
    std::vector<int16_t> decoded;
    UltraRtcAudioFrameCallback onFrame;
    std::unique_ptr<UltraRtcInternal::SpeakerPlayback> speaker;

    // ---- devices ----
    std::mutex deviceMutex;
    std::unique_ptr<UltraRtcInternal::MicrophoneCapture> microphone;
};

// What a video track's callbacks share; owned like AudioMedia.
struct VideoMedia {
    // ---- sending (the camera's thread, or a caller of SendVideoFrame) ----
    std::mutex sendMutex;
    UltraRtcInternal::Vp8Encoder encoder;
    UltraRtcVideoConfig config;
    bool enabled = true;
    bool canSend = false;                   // the track has our packetizer
    std::atomic<bool> keyWanted{true};      // the first frame, and after every PLI
    int64_t startUs = 0;
    int64_t lastSentUs = -1;
    uint32_t timestampBase = 0;
    std::vector<uint8_t> encoded;
    std::weak_ptr<rtc::Track> track;

    // ---- receiving (an engine thread) ----
    std::mutex receiveMutex;
    UltraRtcInternal::Vp8Decoder decoder;
    std::atomic<bool> waitingForKey{true};  // nothing decodable until a key frame
    std::atomic<int64_t> lastKeyRequestMs{0};
    UltraRtcInternal::RgbaImage picture;
    UltraRtcVideoFrame frame;
    UltraRtcVideoFrameCallback onFrame;

    // ---- camera ----
    std::mutex deviceMutex;
    std::unique_ptr<UltraRtcInternal::CameraCapture> camera;
    std::mutex previewMutex;
    UltraRtcVideoFrameCallback onPreview;
    UltraRtcVideoFrame preview;
};

struct TrackEntry {
    std::shared_ptr<rtc::Track> track;
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcMediaKind kind = UltraRtcMediaKind::Audio;
    std::string mid;
    std::shared_ptr<AudioMedia> audio;      // one of the two, by kind
    std::shared_ptr<VideoMedia> video;
};

struct ChannelEntry {
    std::shared_ptr<rtc::DataChannel> channel;
    UltraRtcHandle peer = UltraRtcInvalidHandle;
};

struct EngineState {
    std::mutex mutex;
    bool initialized = false;
    std::atomic<UltraRtcHandle> nextHandle{1};
    std::map<UltraRtcHandle, PeerEntry> peers;
    std::map<UltraRtcHandle, ChannelEntry> channels;
    std::map<UltraRtcHandle, TrackEntry> tracks;
    UltraRtcDispatcher dispatcher;
};

EngineState& State() {
    static EngineState* state = new EngineState();   // never destroyed: engine threads may outlive main()
    return *state;
}

UltraRtcResult NotInitialized() {
    return UltraRtcResult::Error(UltraRtcResultCode::NotInitialized, "UltraRTC is not initialized");
}

UltraRtcResult BadHandle() {
    return UltraRtcResult::Error(UltraRtcResultCode::InvalidHandle, "Unknown handle");
}

// Delivers an event for a peer through the dispatcher - and only if the peer
// is still open when the task runs.
void Emit(UltraRtcPeerEvent event) {
    UltraRtcDispatcher dispatcher;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        dispatcher = State().dispatcher;
    }
    auto task = [event = std::move(event)]() {
        std::shared_ptr<UltraRtcPeerEventCallback> target;
        {
            std::lock_guard<std::mutex> lock(State().mutex);
            auto it = State().peers.find(event.peer);
            if (it == State().peers.end()) return;
            target = it->second.onEvent;
        }
        if (target && *target) (*target)(event);
    };
    if (dispatcher) {
        dispatcher(std::move(task));
    } else {
        task();
    }
}

std::shared_ptr<rtc::PeerConnection> FindPeer(UltraRtcHandle peer) {
    std::lock_guard<std::mutex> lock(State().mutex);
    auto it = State().peers.find(peer);
    return it == State().peers.end() ? nullptr : it->second.connection;
}

std::shared_ptr<rtc::DataChannel> FindChannel(UltraRtcHandle channel, UltraRtcHandle* outPeer = nullptr) {
    std::lock_guard<std::mutex> lock(State().mutex);
    auto it = State().channels.find(channel);
    if (it == State().channels.end()) return nullptr;
    if (outPeer) *outPeer = it->second.peer;
    return it->second.channel;
}

// Drops the last references away from the engine's threads: destroying a
// connection waits for its threads, which must not be the calling one.
void ReleaseLater(std::shared_ptr<rtc::PeerConnection> connection,
                  std::vector<std::shared_ptr<rtc::DataChannel>> channels) {
    std::thread([connection = std::move(connection), channels = std::move(channels)]() mutable {
        for (auto& channel : channels) {
            if (channel) channel->resetCallbacks();
        }
        channels.clear();
        connection.reset();
    }).detach();
}

// ---- conversions -------------------------------------------------------------

rtc::Description::Type ToEngine(UltraRtcSdpType type) {
    switch (type) {
        case UltraRtcSdpType::Offer:    return rtc::Description::Type::Offer;
        case UltraRtcSdpType::Answer:   return rtc::Description::Type::Answer;
        case UltraRtcSdpType::Pranswer: return rtc::Description::Type::Pranswer;
        case UltraRtcSdpType::Rollback: return rtc::Description::Type::Rollback;
        default:                        return rtc::Description::Type::Unspec;
    }
}

UltraRtcSdpType FromEngine(rtc::Description::Type type) {
    switch (type) {
        case rtc::Description::Type::Offer:    return UltraRtcSdpType::Offer;
        case rtc::Description::Type::Answer:   return UltraRtcSdpType::Answer;
        case rtc::Description::Type::Pranswer: return UltraRtcSdpType::Pranswer;
        case rtc::Description::Type::Rollback: return UltraRtcSdpType::Rollback;
        default:                               return UltraRtcSdpType::Unspecified;
    }
}

UltraRtcConnectionState FromEngine(rtc::PeerConnection::State state) {
    switch (state) {
        case rtc::PeerConnection::State::New:          return UltraRtcConnectionState::New;
        case rtc::PeerConnection::State::Connecting:   return UltraRtcConnectionState::Connecting;
        case rtc::PeerConnection::State::Connected:    return UltraRtcConnectionState::Connected;
        case rtc::PeerConnection::State::Disconnected: return UltraRtcConnectionState::Disconnected;
        case rtc::PeerConnection::State::Failed:       return UltraRtcConnectionState::Failed;
        default:                                       return UltraRtcConnectionState::Closed;
    }
}

UltraRtcIceGatheringState FromEngine(rtc::PeerConnection::GatheringState state) {
    switch (state) {
        case rtc::PeerConnection::GatheringState::InProgress: return UltraRtcIceGatheringState::InProgress;
        case rtc::PeerConnection::GatheringState::Complete:   return UltraRtcIceGatheringState::Complete;
        default:                                              return UltraRtcIceGatheringState::New;
    }
}

UltraRtcSignalingState FromEngine(rtc::PeerConnection::SignalingState state) {
    switch (state) {
        case rtc::PeerConnection::SignalingState::HaveLocalOffer:     return UltraRtcSignalingState::HaveLocalOffer;
        case rtc::PeerConnection::SignalingState::HaveRemoteOffer:    return UltraRtcSignalingState::HaveRemoteOffer;
        case rtc::PeerConnection::SignalingState::HaveLocalPranswer:  return UltraRtcSignalingState::HaveLocalPranswer;
        case rtc::PeerConnection::SignalingState::HaveRemotePranswer: return UltraRtcSignalingState::HaveRemotePranswer;
        default:                                                      return UltraRtcSignalingState::Stable;
    }
}

// Registers a channel (ours or the remote's) and wires its callbacks.
UltraRtcHandle AdoptChannel(UltraRtcHandle peer, std::shared_ptr<rtc::DataChannel> channel) {
    const UltraRtcHandle handle = State().nextHandle++;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        auto it = State().peers.find(peer);
        if (it == State().peers.end()) return UltraRtcInvalidHandle;
        it->second.channels.insert(handle);
        State().channels[handle] = ChannelEntry{channel, peer};
    }
    auto base = [peer, handle](UltraRtcPeerEventType type) {
        UltraRtcPeerEvent event;
        event.type = type;
        event.peer = peer;
        event.channel = handle;
        return event;
    };
    channel->onOpen([base]() { Emit(base(UltraRtcPeerEventType::DataChannelOpen)); });
    channel->onClosed([base]() { Emit(base(UltraRtcPeerEventType::DataChannelClose)); });
    channel->onError([base](std::string error) {
        UltraRtcPeerEvent event = base(UltraRtcPeerEventType::DataChannelError);
        event.error = std::move(error);
        Emit(std::move(event));
    });
    channel->onMessage([base](rtc::message_variant message) {
        UltraRtcPeerEvent event = base(UltraRtcPeerEventType::DataChannelMessage);
        if (auto* text = std::get_if<std::string>(&message)) {
            event.text = std::move(*text);
        } else if (auto* bytes = std::get_if<rtc::binary>(&message)) {
            event.isBinary = true;
            event.binary.resize(bytes->size());
            if (!bytes->empty()) std::memcpy(event.binary.data(), bytes->data(), bytes->size());
        }
        Emit(std::move(event));
    });
    return handle;
}

constexpr uint8_t kOpusPayloadType = 111;
constexpr int kMaxConcealedFrames = 5;      // a longer gap starts over instead

// Cuts PCM into 20 ms frames, encodes and sends them.
void SendPcm(AudioMedia& media, const int16_t* samples, size_t count) {
    std::lock_guard<std::mutex> lock(media.sendMutex);
    if (!media.canSend) return;
    media.pending.insert(media.pending.end(), samples, samples + count);
    auto track = media.track.lock();
    std::vector<uint8_t> packet;
    size_t used = 0;
    while (media.pending.size() - used >= static_cast<size_t>(UltraRtcInternal::kOpusFrameSamples)) {
        const int16_t* frame = media.pending.data() + used;
        used += UltraRtcInternal::kOpusFrameSamples;
        const uint32_t timestamp = media.nextTimestamp;
        media.nextTimestamp += UltraRtcInternal::kOpusFrameSamples;
        if (!media.enabled || !track || !track->isOpen()) continue;     // muted: the clock still runs
        if (!media.encoder.Encode(frame, packet)) continue;
        try {
            track->sendFrame(reinterpret_cast<const std::byte*>(packet.data()), packet.size(),
                             rtc::FrameInfo(timestamp));
        } catch (const std::exception&) {
            // The transport went away; the peer's state change reports it.
        }
    }
    media.pending.erase(media.pending.begin(), media.pending.begin() + static_cast<std::ptrdiff_t>(used));
}

// Decodes one received packet, concealing the frames lost before it, and
// hands out the PCM 20 ms at a time.
void ReceivePacket(AudioMedia& media, const std::byte* data, size_t size, uint32_t timestamp) {
    std::lock_guard<std::mutex> lock(media.receiveMutex);
    const auto* packet = reinterpret_cast<const uint8_t*>(data);
    media.decoded.clear();
    if (media.haveTimestamp) {
        const int32_t ahead = static_cast<int32_t>(timestamp - media.expectedTimestamp);
        if (ahead < 0) return;                          // late or repeated: already concealed
        const int lost = ahead / UltraRtcInternal::kOpusFrameSamples;
        if (lost > 0 && lost <= kMaxConcealedFrames) {
            for (int i = 0; i < lost - 1; ++i) media.decoder.Decode(nullptr, 0, false, media.decoded);
            media.decoder.Decode(packet, size, true, media.decoded);    // the frame just before, from FEC
        }
    }
    media.haveTimestamp = true;
    media.decoder.Decode(packet, size, false, media.decoded);
    media.expectedTimestamp = timestamp + UltraRtcInternal::kOpusFrameSamples;

    UltraRtcAudioFrame frame;
    for (size_t at = 0; at < media.decoded.size(); at += UltraRtcInternal::kOpusFrameSamples) {
        const size_t n = std::min<size_t>(UltraRtcInternal::kOpusFrameSamples, media.decoded.size() - at);
        if (media.speaker) media.speaker->Push(media.decoded.data() + at, n);
        if (media.onFrame) {
            frame.samples.assign(media.decoded.begin() + static_cast<std::ptrdiff_t>(at),
                                 media.decoded.begin() + static_cast<std::ptrdiff_t>(at + n));
            media.onFrame(frame);
        }
    }
}

constexpr uint8_t kVp8PayloadType = 96;
constexpr int64_t kKeyRequestIntervalMs = 300;

int64_t SteadyMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t RandomU32() {
    std::mt19937 random(std::random_device{}());
    return static_cast<uint32_t>(random());
}

// Asks the sender for a key frame, at most every kKeyRequestIntervalMs.
void RequestKey(VideoMedia& media) {
    const int64_t now = SteadyMicros() / 1000;
    int64_t last = media.lastKeyRequestMs.load();
    if (now - last < kKeyRequestIntervalMs) return;
    if (!media.lastKeyRequestMs.compare_exchange_strong(last, now)) return;
    if (auto track = media.track.lock()) {
        try {
            track->requestKeyframe();
        } catch (const std::exception&) {
            // The transport went away; the peer's state change reports it.
        }
    }
}

// Encodes and sends one picture, keeping to the track's frame rate.
void SendPicture(VideoMedia& media, const uint8_t* pixels, int width, int height, int stride, bool bgra) {
    std::lock_guard<std::mutex> lock(media.sendMutex);
    if (!media.canSend || !media.enabled) return;
    auto track = media.track.lock();
    if (!track || !track->isOpen()) return;
    const int64_t now = SteadyMicros();
    const int64_t interval = 1000000 / std::max(1, media.config.fps);
    if (media.lastSentUs >= 0 && now - media.lastSentUs < interval * 9 / 10) return;   // faster than the rate
    if (!media.encoder.IsOpen() &&
        !media.encoder.Open(media.config.width, media.config.height, media.config.fps, media.config.bitrateKbps))
        return;
    if (media.lastSentUs < 0) media.startUs = now;
    media.lastSentUs = now;
    const int64_t elapsedUs = now - media.startUs;
    bool key = false;
    const bool forceKey = media.keyWanted.exchange(false);
    if (!media.encoder.Encode(pixels, width, height, stride, bgra, elapsedUs / 1000, forceKey, media.encoded, key)) {
        if (forceKey) media.keyWanted = true;
        return;
    }
    const uint32_t timestamp = media.timestampBase + static_cast<uint32_t>(elapsedUs * 9 / 100);  // 90 kHz
    try {
        track->sendFrame(reinterpret_cast<const std::byte*>(media.encoded.data()), media.encoded.size(),
                         rtc::FrameInfo(timestamp));
    } catch (const std::exception&) {
        // The transport went away; the peer's state change reports it.
    }
}

// Decodes one received frame. Until a key frame arrives after a loss,
// nothing is shown: a picture decoded on a broken reference is garbage.
void ReceivePicture(VideoMedia& media, const std::byte* data, size_t size) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    std::lock_guard<std::mutex> lock(media.receiveMutex);
    if (UltraRtcInternal::IsVp8KeyFrame(bytes, size)) {
        media.waitingForKey = false;
    } else if (media.waitingForKey) {
        RequestKey(media);
        return;
    }
    if (!media.decoder.Decode(bytes, size, media.picture)) {
        media.waitingForKey = true;
        RequestKey(media);
        return;
    }
    if (!media.onFrame) return;
    media.frame.width = media.picture.width;
    media.frame.height = media.picture.height;
    media.frame.rgba.assign(media.picture.pixels.begin(), media.picture.pixels.end());
    media.onFrame(media.frame);
}

// The handler chain of a track. Outgoing runs first to last, incoming last
// to first, and RtcpReceivingSession consumes the RTCP it sees, so the
// handlers that answer the receiver's RTCP (NACK, PLI) come after it.
void SetChain(rtc::Track& track, std::shared_ptr<rtc::RtpPacketizer> packetizer,
              std::shared_ptr<rtc::RtpPacketizationConfig> config, std::shared_ptr<rtc::MediaHandler> depacketizer,
              std::function<void()> onPli) {
    std::shared_ptr<rtc::MediaHandler> head = depacketizer;
    if (packetizer) {
        packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(config));
        packetizer->addToChain(depacketizer);
        head = packetizer;
    }
    head->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
    if (packetizer) {
        head->addToChain(std::make_shared<rtc::RtcpNackResponder>());
        if (onPli) head->addToChain(std::make_shared<rtc::PliHandler>(std::move(onPli)));
    }
    track.setMediaHandler(head);
}

// Wires the receive side (and, when `sendSsrc` is set, the send side) of a
// track and registers it.
UltraRtcHandle AdoptTrack(UltraRtcHandle peer, std::shared_ptr<rtc::Track> track, UltraRtcMediaKind kind,
                          uint32_t sendSsrc, const UltraRtcVideoConfig& videoConfig = UltraRtcVideoConfig()) {
    std::shared_ptr<AudioMedia> audio;
    std::shared_ptr<VideoMedia> video;
    if (kind == UltraRtcMediaKind::Audio) {
        audio = std::make_shared<AudioMedia>();
        audio->track = track;
        std::shared_ptr<rtc::RtpPacketizationConfig> config;
        std::shared_ptr<rtc::RtpPacketizer> packetizer;
        if (sendSsrc != 0) {
            config = std::make_shared<rtc::RtpPacketizationConfig>(sendSsrc, "ultrartc", kOpusPayloadType,
                                                                   rtc::OpusRtpPacketizer::DefaultClockRate);
            packetizer = std::make_shared<rtc::OpusRtpPacketizer>(config);
            audio->canSend = true;
            audio->nextTimestamp = RandomU32();
        }
        SetChain(*track, packetizer, config, std::make_shared<rtc::OpusRtpDepacketizer>(), nullptr);
    } else {
        video = std::make_shared<VideoMedia>();
        video->track = track;
        video->config = videoConfig;
        std::shared_ptr<rtc::RtpPacketizationConfig> config;
        std::shared_ptr<rtc::RtpPacketizer> packetizer;
        if (sendSsrc != 0) {
            config = std::make_shared<rtc::RtpPacketizationConfig>(sendSsrc, "ultrartc", kVp8PayloadType,
                                                                   rtc::RtpPacketizer::VideoClockRate);
            packetizer = std::make_shared<UltraRtcInternal::Vp8RtpPacketizer>(config);
            video->canSend = true;
            video->timestampBase = RandomU32();
        }
        std::weak_ptr<VideoMedia> weak = video;
        auto depacketizer = std::make_shared<UltraRtcInternal::Vp8RtpDepacketizer>([weak]() {
            if (auto media = weak.lock()) {
                media->waitingForKey = true;
                RequestKey(*media);
            }
        });
        SetChain(*track, packetizer, config, depacketizer, [weak]() {
            if (auto media = weak.lock()) media->keyWanted = true;
        });
    }

    const UltraRtcHandle handle = State().nextHandle++;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        auto it = State().peers.find(peer);
        if (it == State().peers.end()) return UltraRtcInvalidHandle;
        it->second.tracks.insert(handle);
        TrackEntry entry;
        entry.track = track;
        entry.peer = peer;
        entry.kind = kind;
        entry.mid = track->mid();
        entry.audio = audio;
        entry.video = video;
        State().tracks[handle] = std::move(entry);
    }
    if (audio) {
        track->onFrame([audio](rtc::binary data, rtc::FrameInfo info) {
            ReceivePacket(*audio, data.data(), data.size(), info.timestamp);
        });
    } else {
        track->onFrame([video](rtc::binary data, rtc::FrameInfo) {
            ReceivePicture(*video, data.data(), data.size());
        });
    }
    auto base = [peer, handle](UltraRtcPeerEventType type) {
        UltraRtcPeerEvent event;
        event.type = type;
        event.peer = peer;
        event.track = handle;
        return event;
    };
    track->onOpen([base]() { Emit(base(UltraRtcPeerEventType::TrackOpen)); });
    track->onClosed([base]() { Emit(base(UltraRtcPeerEventType::TrackClose)); });
    return handle;
}

std::shared_ptr<VideoMedia> FindVideo(UltraRtcHandle track) {
    std::lock_guard<std::mutex> lock(State().mutex);
    auto it = State().tracks.find(track);
    return it == State().tracks.end() ? nullptr : it->second.video;
}

// Stops a video track's camera (outside the engine lock, as for audio).
void StopCamera(const std::shared_ptr<VideoMedia>& video) {
    if (!video) return;
    std::unique_ptr<UltraRtcInternal::CameraCapture> camera;
    {
        std::lock_guard<std::mutex> lock(video->deviceMutex);
        camera = std::move(video->camera);
    }
    if (camera) camera->Stop();
    {
        std::lock_guard<std::mutex> lock(video->previewMutex);
        video->onPreview = nullptr;
    }
    std::lock_guard<std::mutex> lock(video->receiveMutex);
    video->onFrame = nullptr;
}

std::shared_ptr<AudioMedia> FindAudio(UltraRtcHandle track) {
    std::lock_guard<std::mutex> lock(State().mutex);
    auto it = State().tracks.find(track);
    return it == State().tracks.end() ? nullptr : it->second.audio;
}

// Stops a track's devices (outside the engine lock: closing a device waits
// for its thread, which may be inside one of this track's callbacks).
void StopDevices(const std::shared_ptr<AudioMedia>& audio) {
    if (!audio) return;
    std::unique_ptr<UltraRtcInternal::MicrophoneCapture> microphone;
    {
        std::lock_guard<std::mutex> lock(audio->deviceMutex);
        microphone = std::move(audio->microphone);
    }
    if (microphone) microphone->Stop();
    std::unique_ptr<UltraRtcInternal::SpeakerPlayback> speaker;
    {
        std::lock_guard<std::mutex> lock(audio->receiveMutex);
        speaker = std::move(audio->speaker);
        audio->onFrame = nullptr;
    }
    if (speaker) speaker->Stop();
}

} // namespace

// ============================================================================
// LIFECYCLE
// ============================================================================

UltraRtcResult UltraRtc_Initialize() {
    std::lock_guard<std::mutex> lock(State().mutex);
    if (State().initialized) return UltraRtcResult::Ok();
    rtc::InitLogger(rtc::LogLevel::Warning);
    rtc::Preload();
    State().initialized = true;
    return UltraRtcResult::Ok();
}

void UltraRtc_Shutdown() {
    std::vector<UltraRtcHandle> peers;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().initialized) return;
        for (const auto& entry : State().peers) peers.push_back(entry.first);
    }
    for (UltraRtcHandle peer : peers) UltraRtc_ClosePeer(peer);
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        State().initialized = false;
        State().dispatcher = nullptr;
    }
    rtc::Cleanup().wait();
}

bool UltraRtc_IsAvailable() {
    return true;
}

UltraRtcCapabilities UltraRtc_GetCapabilities() {
    UltraRtcCapabilities caps;
    caps.available = true;
    caps.engine = "libdatachannel " RTC_VERSION;
    caps.dataChannels = true;
    caps.audio = UltraRtcInternal::OpusAvailable();
    caps.video = UltraRtcInternal::VideoCodecAvailable();
    return caps;
}

void UltraRtc_SetEventDispatcher(UltraRtcDispatcher dispatcher) {
    std::lock_guard<std::mutex> lock(State().mutex);
    State().dispatcher = std::move(dispatcher);
}

// ============================================================================
// PEERS
// ============================================================================

UltraRtcResult UltraRtc_CreatePeer(const UltraRtcPeerConfig& config,
                                   UltraRtcPeerEventCallback onEvent,
                                   UltraRtcHandle& outPeer) {
    outPeer = UltraRtcInvalidHandle;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().initialized) return NotInitialized();
    }

    rtc::Configuration engineConfig;
    try {
        for (const auto& server : config.iceServers) {
            rtc::IceServer ice(server.url);
            if (!server.username.empty()) ice.username = server.username;
            if (!server.credential.empty()) ice.password = server.credential;
            engineConfig.iceServers.push_back(std::move(ice));
        }
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument,
                                     std::string("Bad ICE server: ") + e.what());
    }
    engineConfig.iceTransportPolicy = config.iceTransportPolicy == UltraRtcIceTransportPolicy::Relay
                                          ? rtc::TransportPolicy::Relay : rtc::TransportPolicy::All;
    engineConfig.disableAutoNegotiation = !config.autoNegotiation;
    engineConfig.enableIceTcp = config.enableIceTcp;
    if (config.portRangeBegin != 0) {
        engineConfig.portRangeBegin = config.portRangeBegin;
        engineConfig.portRangeEnd = config.portRangeEnd != 0 ? config.portRangeEnd : config.portRangeBegin;
    }
    if (!config.bindAddress.empty()) engineConfig.bindAddress = config.bindAddress;

    std::shared_ptr<rtc::PeerConnection> connection;
    try {
        connection = std::make_shared<rtc::PeerConnection>(engineConfig);
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::Unknown, e.what());
    }

    const UltraRtcHandle handle = State().nextHandle++;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        PeerEntry entry;
        entry.connection = connection;
        entry.onEvent = std::make_shared<UltraRtcPeerEventCallback>(std::move(onEvent));
        State().peers[handle] = std::move(entry);
    }

    connection->onLocalDescription([handle](rtc::Description description) {
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::LocalDescription;
        event.peer = handle;
        event.sdpType = FromEngine(description.type());
        event.sdp = std::string(description);
        Emit(std::move(event));
    });
    connection->onLocalCandidate([handle](rtc::Candidate candidate) {
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::IceCandidate;
        event.peer = handle;
        event.candidate = candidate.candidate();
        event.sdpMid = candidate.mid();
        Emit(std::move(event));
    });
    connection->onStateChange([handle](rtc::PeerConnection::State state) {
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::ConnectionStateChange;
        event.peer = handle;
        event.connectionState = FromEngine(state);
        Emit(std::move(event));
    });
    connection->onGatheringStateChange([handle](rtc::PeerConnection::GatheringState state) {
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::IceGatheringStateChange;
        event.peer = handle;
        event.gatheringState = FromEngine(state);
        Emit(std::move(event));
    });
    connection->onSignalingStateChange([handle](rtc::PeerConnection::SignalingState state) {
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::SignalingStateChange;
        event.peer = handle;
        event.signalingState = FromEngine(state);
        Emit(std::move(event));
    });
    connection->onDataChannel([handle](std::shared_ptr<rtc::DataChannel> channel) {
        const std::string label = channel->label();
        const UltraRtcHandle channelHandle = AdoptChannel(handle, std::move(channel));
        if (channelHandle == UltraRtcInvalidHandle) return;
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::DataChannel;
        event.peer = handle;
        event.channel = channelHandle;
        event.channelLabel = label;
        Emit(std::move(event));
    });

    // A track the other side added that we did not: receive it.
    connection->onTrack([handle](std::shared_ptr<rtc::Track> track) {
        const rtc::Description::Media description = track->description();
        const UltraRtcMediaKind kind =
            description.type() == "video" ? UltraRtcMediaKind::Video : UltraRtcMediaKind::Audio;
        const std::string mid = track->mid();
        const UltraRtcHandle trackHandle = AdoptTrack(handle, std::move(track), kind, 0);
        if (trackHandle == UltraRtcInvalidHandle) return;
        UltraRtcPeerEvent event;
        event.type = UltraRtcPeerEventType::Track;
        event.peer = handle;
        event.track = trackHandle;
        event.mediaKind = kind;
        event.mid = mid;
        Emit(std::move(event));
    });

    outPeer = handle;
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_ClosePeer(UltraRtcHandle peer) {
    std::shared_ptr<rtc::PeerConnection> connection;
    std::vector<std::shared_ptr<rtc::DataChannel>> channels;
    std::vector<std::shared_ptr<AudioMedia>> audios;
    std::vector<std::shared_ptr<VideoMedia>> videos;
    std::vector<std::shared_ptr<rtc::Track>> trackObjects;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        auto it = State().peers.find(peer);
        if (it == State().peers.end()) return BadHandle();
        connection = it->second.connection;
        for (UltraRtcHandle track : it->second.tracks) {
            auto t = State().tracks.find(track);
            if (t != State().tracks.end()) {
                if (t->second.audio) audios.push_back(t->second.audio);
                if (t->second.video) videos.push_back(t->second.video);
                trackObjects.push_back(t->second.track);
                State().tracks.erase(t);
            }
        }
        for (UltraRtcHandle channel : it->second.channels) {
            auto c = State().channels.find(channel);
            if (c != State().channels.end()) {
                channels.push_back(c->second.channel);
                State().channels.erase(c);
            }
        }
        State().peers.erase(it);
    }
    for (const auto& audio : audios) StopDevices(audio);
    for (const auto& video : videos) StopCamera(video);
    for (const auto& track : trackObjects) {
        if (track) track->resetCallbacks();
    }
    try {
        connection->resetCallbacks();
        connection->close();
    } catch (const std::exception&) {
        // Closing is best effort; the release below still happens.
    }
    ReleaseLater(std::move(connection), std::move(channels));
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SetPeerEventCallback(UltraRtcHandle peer, UltraRtcPeerEventCallback onEvent) {
    std::lock_guard<std::mutex> lock(State().mutex);
    auto it = State().peers.find(peer);
    if (it == State().peers.end()) return BadHandle();
    it->second.onEvent = std::make_shared<UltraRtcPeerEventCallback>(std::move(onEvent));
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SetLocalDescription(UltraRtcHandle peer, UltraRtcSdpType type) {
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    try {
        connection->setLocalDescription(ToEngine(type));
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SetRemoteDescription(UltraRtcHandle peer, UltraRtcSdpType type,
                                             const std::string& sdp) {
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    try {
        connection->setRemoteDescription(rtc::Description(sdp, ToEngine(type)));
    } catch (const std::invalid_argument& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidSdp, e.what());
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_AddIceCandidate(UltraRtcHandle peer, const std::string& candidate,
                                        const std::string& sdpMid) {
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    try {
        connection->addRemoteCandidate(rtc::Candidate(candidate, sdpMid));
    } catch (const std::invalid_argument& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument, e.what());
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_GetLocalDescription(UltraRtcHandle peer, UltraRtcSdpType& outType,
                                            std::string& outSdp) {
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    auto description = connection->localDescription();
    if (!description) return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, "No local description yet");
    outType = FromEngine(description->type());
    outSdp = std::string(*description);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_GetRemoteDescription(UltraRtcHandle peer, UltraRtcSdpType& outType,
                                             std::string& outSdp) {
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    auto description = connection->remoteDescription();
    if (!description) return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, "No remote description yet");
    outType = FromEngine(description->type());
    outSdp = std::string(*description);
    return UltraRtcResult::Ok();
}

UltraRtcConnectionState UltraRtc_GetConnectionState(UltraRtcHandle peer) {
    auto connection = FindPeer(peer);
    return connection ? FromEngine(connection->state()) : UltraRtcConnectionState::Closed;
}

UltraRtcIceGatheringState UltraRtc_GetIceGatheringState(UltraRtcHandle peer) {
    auto connection = FindPeer(peer);
    return connection ? FromEngine(connection->gatheringState()) : UltraRtcIceGatheringState::New;
}

UltraRtcSignalingState UltraRtc_GetSignalingState(UltraRtcHandle peer) {
    auto connection = FindPeer(peer);
    return connection ? FromEngine(connection->signalingState()) : UltraRtcSignalingState::Stable;
}

// ============================================================================
// DATA CHANNELS
// ============================================================================

UltraRtcResult UltraRtc_CreateDataChannel(UltraRtcHandle peer, const std::string& label,
                                          const UltraRtcDataChannelInit& init,
                                          UltraRtcHandle& outChannel) {
    outChannel = UltraRtcInvalidHandle;
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    if (init.maxRetransmits >= 0 && init.maxPacketLifeTimeMs >= 0)
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument,
                                     "maxRetransmits and maxPacketLifeTimeMs are exclusive");

    rtc::DataChannelInit engineInit;
    engineInit.reliability.unordered = !init.ordered;
    if (init.maxRetransmits >= 0)
        engineInit.reliability.maxRetransmits = static_cast<unsigned int>(init.maxRetransmits);
    if (init.maxPacketLifeTimeMs >= 0)
        engineInit.reliability.maxPacketLifeTime = std::chrono::milliseconds(init.maxPacketLifeTimeMs);
    engineInit.protocol = init.protocol;
    if (init.negotiatedId >= 0) {
        engineInit.negotiated = true;
        engineInit.id = static_cast<uint16_t>(init.negotiatedId);
    }

    std::shared_ptr<rtc::DataChannel> channel;
    try {
        channel = connection->createDataChannel(label, engineInit);
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    outChannel = AdoptChannel(peer, std::move(channel));
    if (outChannel == UltraRtcInvalidHandle) return BadHandle();
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_DataChannelSendText(UltraRtcHandle channel, const std::string& text) {
    auto engineChannel = FindChannel(channel);
    if (!engineChannel) return BadHandle();
    if (!engineChannel->isOpen()) return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, "Channel is not open");
    try {
        engineChannel->send(text);
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_DataChannelSendBinary(UltraRtcHandle channel, const std::vector<uint8_t>& data) {
    auto engineChannel = FindChannel(channel);
    if (!engineChannel) return BadHandle();
    if (!engineChannel->isOpen()) return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, "Channel is not open");
    try {
        engineChannel->send(reinterpret_cast<const std::byte*>(data.data()), data.size());
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_DataChannelClose(UltraRtcHandle channel) {
    std::shared_ptr<rtc::DataChannel> engineChannel;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        auto it = State().channels.find(channel);
        if (it == State().channels.end()) return BadHandle();
        engineChannel = it->second.channel;
        auto peer = State().peers.find(it->second.peer);
        if (peer != State().peers.end()) peer->second.channels.erase(channel);
        State().channels.erase(it);
    }
    try {
        engineChannel->close();
    } catch (const std::exception&) {
        // Already closed.
    }
    return UltraRtcResult::Ok();
}

UltraRtcDataChannelState UltraRtc_GetDataChannelState(UltraRtcHandle channel) {
    auto engineChannel = FindChannel(channel);
    if (!engineChannel || engineChannel->isClosed()) return UltraRtcDataChannelState::Closed;
    return engineChannel->isOpen() ? UltraRtcDataChannelState::Open : UltraRtcDataChannelState::Connecting;
}

std::string UltraRtc_GetDataChannelLabel(UltraRtcHandle channel) {
    auto engineChannel = FindChannel(channel);
    return engineChannel ? engineChannel->label() : std::string();
}

UltraRtcHandle UltraRtc_GetDataChannelPeer(UltraRtcHandle channel) {
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    FindChannel(channel, &peer);
    return peer;
}

size_t UltraRtc_GetDataChannelBufferedAmount(UltraRtcHandle channel) {
    auto engineChannel = FindChannel(channel);
    return engineChannel ? engineChannel->bufferedAmount() : 0;
}

// ============================================================================
// AUDIO
// ============================================================================

UltraRtcResult UltraRtc_AddAudioTrack(UltraRtcHandle peer, const std::string& mid,
                                      UltraRtcDirection direction, UltraRtcHandle& outTrack) {
    outTrack = UltraRtcInvalidHandle;
    if (!UltraRtcInternal::OpusAvailable())
        return UltraRtcResult::Error(UltraRtcResultCode::Unsupported, "This build has no Opus codec");
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    if (mid.empty()) return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument, "A track needs a mid");

    rtc::Description::Direction engineDirection = rtc::Description::Direction::SendRecv;
    if (direction == UltraRtcDirection::SendOnly) engineDirection = rtc::Description::Direction::SendOnly;
    if (direction == UltraRtcDirection::RecvOnly) engineDirection = rtc::Description::Direction::RecvOnly;
    if (direction == UltraRtcDirection::Inactive) engineDirection = rtc::Description::Direction::Inactive;

    std::mt19937 random(std::random_device{}());
    uint32_t ssrc = static_cast<uint32_t>(random());
    if (ssrc == 0) ssrc = 1;
    rtc::Description::Audio description(mid, engineDirection);
    description.addOpusCodec(kOpusPayloadType);
    const bool sends = direction == UltraRtcDirection::SendRecv || direction == UltraRtcDirection::SendOnly;
    if (sends) description.addSSRC(ssrc, std::string("ultrartc"), std::string("ultrartc-stream"), mid);

    std::shared_ptr<rtc::Track> track;
    try {
        track = connection->addTrack(description);
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    outTrack = AdoptTrack(peer, std::move(track), UltraRtcMediaKind::Audio, sends ? ssrc : 0);
    if (outTrack == UltraRtcInvalidHandle) return BadHandle();
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SendAudioFrame(UltraRtcHandle track, const UltraRtcAudioFrame& frame) {
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    if (frame.sampleRate != UltraRtcInternal::kOpusSampleRate)
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument, "Audio frames must be 48 kHz");
    if (frame.channels <= 1) {
        SendPcm(*audio, frame.samples.data(), frame.samples.size());
        return UltraRtcResult::Ok();
    }
    std::vector<int16_t> mono(frame.samples.size() / static_cast<size_t>(frame.channels));
    for (size_t i = 0; i < mono.size(); ++i) {
        int sum = 0;
        for (int c = 0; c < frame.channels; ++c) sum += frame.samples[i * static_cast<size_t>(frame.channels) + static_cast<size_t>(c)];
        mono[i] = static_cast<int16_t>(sum / frame.channels);
    }
    SendPcm(*audio, mono.data(), mono.size());
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SetAudioFrameCallback(UltraRtcHandle track, UltraRtcAudioFrameCallback onFrame) {
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    std::lock_guard<std::mutex> lock(audio->receiveMutex);
    audio->onFrame = std::move(onFrame);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SetTrackEnabled(UltraRtcHandle track, bool enabled) {
    if (auto video = FindVideo(track)) {
        std::lock_guard<std::mutex> lock(video->sendMutex);
        if (enabled && !video->enabled) video->keyWanted = true;     // the other side resumes from a key frame
        video->enabled = enabled;
        return UltraRtcResult::Ok();
    }
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    std::lock_guard<std::mutex> lock(audio->sendMutex);
    audio->enabled = enabled;
    return UltraRtcResult::Ok();
}

bool UltraRtc_IsTrackOpen(UltraRtcHandle track) {
    std::lock_guard<std::mutex> lock(State().mutex);
    auto it = State().tracks.find(track);
    return it != State().tracks.end() && it->second.track && it->second.track->isOpen();
}

UltraRtcResult UltraRtc_StartMicrophone(UltraRtcHandle track, const std::string& deviceId) {
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    std::lock_guard<std::mutex> lock(audio->deviceMutex);
    if (audio->microphone) return UltraRtcResult::Ok();
    std::string error;
    std::weak_ptr<AudioMedia> weak = audio;
    audio->microphone = UltraRtcInternal::OpenMicrophone(deviceId, [weak](const int16_t* samples, size_t count) {
        if (auto media = weak.lock()) SendPcm(*media, samples, count);
    }, error);
    if (!audio->microphone) return UltraRtcResult::Error(UltraRtcResultCode::Unknown, error);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_StopMicrophone(UltraRtcHandle track) {
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    std::unique_ptr<UltraRtcInternal::MicrophoneCapture> microphone;
    {
        std::lock_guard<std::mutex> lock(audio->deviceMutex);
        microphone = std::move(audio->microphone);
    }
    if (microphone) microphone->Stop();
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_StartPlayback(UltraRtcHandle track, const std::string& deviceId) {
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    std::string error;
    auto speaker = UltraRtcInternal::OpenSpeaker(deviceId, error);
    if (!speaker) return UltraRtcResult::Error(UltraRtcResultCode::Unknown, error);
    std::lock_guard<std::mutex> lock(audio->receiveMutex);
    audio->speaker = std::move(speaker);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_StopPlayback(UltraRtcHandle track) {
    auto audio = FindAudio(track);
    if (!audio) return BadHandle();
    std::unique_ptr<UltraRtcInternal::SpeakerPlayback> speaker;
    {
        std::lock_guard<std::mutex> lock(audio->receiveMutex);
        speaker = std::move(audio->speaker);
    }
    if (speaker) speaker->Stop();
    return UltraRtcResult::Ok();
}

// ============================================================================
// VIDEO
// ============================================================================

UltraRtcResult UltraRtc_AddVideoTrack(UltraRtcHandle peer, const std::string& mid, UltraRtcDirection direction,
                                      const UltraRtcVideoConfig& config, UltraRtcHandle& outTrack) {
    outTrack = UltraRtcInvalidHandle;
    if (!UltraRtcInternal::VideoCodecAvailable())
        return UltraRtcResult::Error(UltraRtcResultCode::Unsupported, "This build has no VP8 codec (FFmpeg)");
    auto connection = FindPeer(peer);
    if (!connection) return BadHandle();
    if (mid.empty()) return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument, "A track needs a mid");
    if (config.width < 16 || config.height < 16 || config.fps < 1 || config.bitrateKbps < 50)
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument, "Unusable video settings");

    rtc::Description::Direction engineDirection = rtc::Description::Direction::SendRecv;
    if (direction == UltraRtcDirection::SendOnly) engineDirection = rtc::Description::Direction::SendOnly;
    if (direction == UltraRtcDirection::RecvOnly) engineDirection = rtc::Description::Direction::RecvOnly;
    if (direction == UltraRtcDirection::Inactive) engineDirection = rtc::Description::Direction::Inactive;

    uint32_t ssrc = RandomU32();
    if (ssrc == 0) ssrc = 1;
    rtc::Description::Video description(mid, engineDirection);
    description.addVP8Codec(kVp8PayloadType);
    const bool sends = direction == UltraRtcDirection::SendRecv || direction == UltraRtcDirection::SendOnly;
    if (sends) description.addSSRC(ssrc, std::string("ultrartc"), std::string("ultrartc-stream"), mid);

    std::shared_ptr<rtc::Track> track;
    try {
        track = connection->addTrack(description);
    } catch (const std::exception& e) {
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidState, e.what());
    }
    outTrack = AdoptTrack(peer, std::move(track), UltraRtcMediaKind::Video, sends ? ssrc : 0, config);
    if (outTrack == UltraRtcInvalidHandle) return BadHandle();
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SendVideoFrame(UltraRtcHandle track, const UltraRtcVideoFrame& frame) {
    auto video = FindVideo(track);
    if (!video) return BadHandle();
    if (frame.width <= 1 || frame.height <= 1 ||
        frame.rgba.size() < static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4)
        return UltraRtcResult::Error(UltraRtcResultCode::InvalidArgument, "Not a whole RGBA picture");
    SendPicture(*video, frame.rgba.data(), frame.width, frame.height, frame.width * 4, false);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_SetVideoFrameCallback(UltraRtcHandle track, UltraRtcVideoFrameCallback onFrame) {
    auto video = FindVideo(track);
    if (!video) return BadHandle();
    std::lock_guard<std::mutex> lock(video->receiveMutex);
    video->onFrame = std::move(onFrame);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_RequestKeyFrame(UltraRtcHandle track) {
    auto video = FindVideo(track);
    if (!video) return BadHandle();
    RequestKey(*video);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_StartCamera(UltraRtcHandle track, const std::string& deviceId,
                                    UltraRtcVideoFrameCallback onPreview) {
    auto video = FindVideo(track);
    if (!video) return BadHandle();
    {
        std::lock_guard<std::mutex> lock(video->previewMutex);
        video->onPreview = std::move(onPreview);
    }
    std::lock_guard<std::mutex> lock(video->deviceMutex);
    if (video->camera) return UltraRtcResult::Ok();
    std::weak_ptr<VideoMedia> weak = video;
    std::string error;
    UltraRtcVideoConfig config;
    {
        std::lock_guard<std::mutex> sendLock(video->sendMutex);
        config = video->config;
    }
    video->camera = UltraRtcInternal::OpenCamera(deviceId, config.width, config.height, config.fps,
        [weak](const uint8_t* pixels, int width, int height, int stride, bool bgra) {
            auto media = weak.lock();
            if (!media) return;
            SendPicture(*media, pixels, width, height, stride, bgra);
            std::lock_guard<std::mutex> previewLock(media->previewMutex);
            if (!media->onPreview) return;
            // The self view gets RGBA like every other picture.
            UltraRtcVideoFrame& p = media->preview;
            p.width = width;
            p.height = height;
            p.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            for (int row = 0; row < height; ++row) {
                const uint8_t* src = pixels + static_cast<size_t>(row) * static_cast<size_t>(stride);
                uint8_t* dst = p.rgba.data() + static_cast<size_t>(row) * static_cast<size_t>(width) * 4;
                if (!bgra) {
                    std::memcpy(dst, src, static_cast<size_t>(width) * 4);
                    continue;
                }
                for (int col = 0; col < width; ++col) {
                    dst[col * 4 + 0] = src[col * 4 + 2];
                    dst[col * 4 + 1] = src[col * 4 + 1];
                    dst[col * 4 + 2] = src[col * 4 + 0];
                    dst[col * 4 + 3] = src[col * 4 + 3];
                }
            }
            media->onPreview(p);
        }, error);
    if (!video->camera) return UltraRtcResult::Error(UltraRtcResultCode::Unknown, error);
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_StopCamera(UltraRtcHandle track) {
    auto video = FindVideo(track);
    if (!video) return BadHandle();
    std::unique_ptr<UltraRtcInternal::CameraCapture> camera;
    {
        std::lock_guard<std::mutex> lock(video->deviceMutex);
        camera = std::move(video->camera);
    }
    if (camera) camera->Stop();
    std::lock_guard<std::mutex> lock(video->previewMutex);
    video->onPreview = nullptr;
    return UltraRtcResult::Ok();
}
