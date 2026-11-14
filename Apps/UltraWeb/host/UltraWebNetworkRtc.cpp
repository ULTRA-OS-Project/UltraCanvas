// Apps/UltraWeb/host/UltraWebNetworkRtc.cpp
// An app's peer connections on UltraRTC (MakeEngineRtc in UltraWebRtc.h).
// UltraRTC reports a peer's events on its own threads (UltraWeb installs no
// process-wide dispatcher, which another module might want); each event is
// translated here and posted to the UI thread, where a closed peer drops it.
// Built without UltraRTC (ULTRAWEB_HAS_RTC unset), there is no engine.
// Version: 0.2.0 - calls: tracks, the offer, the microphone, the camera, the speaker,
//                  pictures to a sink; track events
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebRtc.h"

#include "ultraweb.h"   // UltraWeb/guest: the result codes and event kinds

#ifdef ULTRAWEB_HAS_RTC
#include <UltraRTC/UltraRTC.h>

#include <atomic>
#endif

namespace UltraWeb {

#ifdef ULTRAWEB_HAS_RTC

namespace {

int32_t ResultOf(const UltraRtcResult& result) {
    if (result.success) return UC_OK;
    if (result.code == UltraRtcResultCode::Unsupported || result.code == UltraRtcResultCode::NotInitialized)
        return UC_ERR_DENIED;
    if (result.code == UltraRtcResultCode::InvalidHandle) return UC_ERR_NOT_FOUND;
    return UC_ERR_STATE;
}

// UltraRTC's event in the ABI's terms; false for one an app is not told of.
bool Translate(const UltraRtcPeerEvent& e, RtcEvent& out) {
    out = RtcEvent{};
    switch (e.type) {
        case UltraRtcPeerEventType::LocalDescription:
            out.kind = UC_RTC_LOCAL_DESCRIPTION;
            out.number = e.sdpType == UltraRtcSdpType::Offer ? UC_RTC_OFFER : UC_RTC_ANSWER;
            out.text = e.sdp;
            return true;
        case UltraRtcPeerEventType::IceCandidate:
            out.kind = UC_RTC_CANDIDATE;
            out.text = e.sdpMid + "\n" + e.candidate;
            return true;
        case UltraRtcPeerEventType::ConnectionStateChange:
            out.kind = UC_RTC_STATE;
            out.number = uint32_t(e.connectionState);   // New .. Closed = UC_RTC_STATE_NEW .. CLOSED
            return true;
        case UltraRtcPeerEventType::DataChannel:
            out.kind = UC_RTC_CHANNEL;
            out.channel = e.channel;
            out.text = e.channelLabel;
            return true;
        case UltraRtcPeerEventType::DataChannelOpen:
            out.kind = UC_RTC_CHANNEL_OPEN;
            out.channel = e.channel;
            return true;
        case UltraRtcPeerEventType::DataChannelClose:
            out.kind = UC_RTC_CHANNEL_CLOSE;
            out.channel = e.channel;
            return true;
        case UltraRtcPeerEventType::DataChannelMessage:
            out.kind = UC_RTC_MESSAGE;
            out.channel = e.channel;
            out.number = e.isBinary ? 1u : 0u;
            out.text = e.isBinary ? std::string(e.binary.begin(), e.binary.end()) : e.text;
            return true;
        case UltraRtcPeerEventType::Track:
            out.kind = UC_RTC_TRACK;
            out.channel = e.track;
            out.number = e.mediaKind == UltraRtcMediaKind::Video ? UC_RTC_VIDEO : UC_RTC_AUDIO;
            out.text = e.mid;
            return true;
        case UltraRtcPeerEventType::TrackOpen:
            out.kind = UC_RTC_TRACK_OPEN;
            out.channel = e.track;
            return true;
        case UltraRtcPeerEventType::TrackClose:
            out.kind = UC_RTC_TRACK_CLOSE;
            out.channel = e.track;
            return true;
        default:
            return false;   // gathering and signalling states, errors
    }
}

UltraRtcDirection DirectionOf(uint32_t direction) {
    switch (direction) {
        case UC_RTC_SENDONLY: return UltraRtcDirection::SendOnly;
        case UC_RTC_RECVONLY: return UltraRtcDirection::RecvOnly;
        default: return UltraRtcDirection::SendRecv;
    }
}

// UltraRTC's picture to a sink; nullptr for none.
UltraRtcVideoFrameCallback ToEngine(RtcFrameSink sink) {
    if (!sink) return nullptr;
    return [sink = std::move(sink)](const UltraRtcVideoFrame& frame) {
        if (frame.width > 0 && frame.height > 0 && frame.rgba.size() >= size_t(frame.width) * size_t(frame.height) * 4)
            sink(frame.rgba.data(), frame.width, frame.height);
    };
}

class EnginePeer : public RtcPeer, public std::enable_shared_from_this<EnginePeer> {
public:
    EnginePeer(std::function<void(std::function<void()>)> post, std::function<void(RtcEvent)> onEvent)
        : post_(std::move(post)), onEvent_(std::move(onEvent)) {}
    ~EnginePeer() override { Close(); }

    bool Open(const RtcConfig& config, std::string& error) {
        UltraRtcPeerConfig engine;
        for (const RtcIceServer& server : config.iceServers)
            engine.iceServers.push_back({server.url, server.username, server.credential});
        engine.iceTransportPolicy = config.relayOnly ? UltraRtcIceTransportPolicy::Relay : UltraRtcIceTransportPolicy::All;
        engine.autoNegotiation = true;
        std::weak_ptr<EnginePeer> weak = shared_from_this();
        const UltraRtcResult result = UltraRtc_CreatePeer(engine, [weak](const UltraRtcPeerEvent& e) {
            RtcEvent event;
            if (!Translate(e, event)) return;
            auto self = weak.lock();
            if (!self || self->closed_) return;
            self->post_([weak, event = std::move(event)]() mutable {
                auto alive = weak.lock();
                if (alive && !alive->closed_ && alive->onEvent_) alive->onEvent_(std::move(event));
            });
        }, peer_);
        if (!result.success) {
            error = result.message;
            return false;
        }
        return true;
    }

    int32_t SetRemote(uint32_t type, const std::string& sdp) override {
        if (type != UC_RTC_OFFER && type != UC_RTC_ANSWER) return UC_ERR_STATE;
        return ResultOf(UltraRtc_SetRemoteDescription(
            peer_, type == UC_RTC_OFFER ? UltraRtcSdpType::Offer : UltraRtcSdpType::Answer, sdp));
    }
    int32_t AddCandidate(const std::string& candidate, const std::string& mid) override {
        return ResultOf(UltraRtc_AddIceCandidate(peer_, candidate, mid));
    }
    int32_t OpenChannel(const std::string& label, bool ordered, int32_t maxRetransmits, uint64_t& outChannel) override {
        UltraRtcDataChannelInit init;
        init.ordered = ordered;
        init.maxRetransmits = maxRetransmits < 0 ? -1 : maxRetransmits;
        UltraRtcHandle channel = UltraRtcInvalidHandle;
        const int32_t result = ResultOf(UltraRtc_CreateDataChannel(peer_, label, init, channel));
        outChannel = channel;
        return result;
    }
    int32_t Send(uint64_t channel, const std::vector<uint8_t>& data, bool binary) override {
        if (UltraRtc_GetDataChannelState(channel) != UltraRtcDataChannelState::Open) return UC_ERR_STATE;
        return ResultOf(binary ? UltraRtc_DataChannelSendBinary(channel, data)
                               : UltraRtc_DataChannelSendText(channel, std::string(data.begin(), data.end())));
    }
    int32_t CloseChannel(uint64_t channel) override { return ResultOf(UltraRtc_DataChannelClose(channel)); }

    int32_t AddTrack(bool video, const std::string& mid, uint32_t direction, const RtcVideoConfig& config,
                     uint64_t& outTrack) override {
        UltraRtcHandle track = UltraRtcInvalidHandle;
        UltraRtcResult result;
        if (video) {
            UltraRtcVideoConfig engine;
            engine.width = config.width;
            engine.height = config.height;
            engine.fps = config.fps;
            engine.bitrateKbps = config.kbps;
            result = UltraRtc_AddVideoTrack(peer_, mid, DirectionOf(direction), engine, track);
        } else {
            result = UltraRtc_AddAudioTrack(peer_, mid, DirectionOf(direction), track);
        }
        outTrack = track;
        return ResultOf(result);
    }
    int32_t Offer() override { return ResultOf(UltraRtc_SetLocalDescription(peer_, UltraRtcSdpType::Offer)); }
    int32_t SetMicrophone(uint64_t track, bool on) override {
        return ResultOf(on ? UltraRtc_StartMicrophone(track) : UltraRtc_StopMicrophone(track));
    }
    int32_t SetCamera(uint64_t track, bool on, RtcFrameSink selfView) override {
        return ResultOf(on ? UltraRtc_StartCamera(track, std::string(), ToEngine(std::move(selfView)))
                           : UltraRtc_StopCamera(track));
    }
    int32_t SetPlayback(uint64_t track, bool on) override {
        return ResultOf(on ? UltraRtc_StartPlayback(track) : UltraRtc_StopPlayback(track));
    }
    int32_t SetTrackEnabled(uint64_t track, bool enabled) override {
        return ResultOf(UltraRtc_SetTrackEnabled(track, enabled));
    }
    int32_t ShowVideo(uint64_t track, RtcFrameSink sink) override {
        return ResultOf(UltraRtc_SetVideoFrameCallback(track, ToEngine(std::move(sink))));
    }
    void Close() override {
        if (closed_.exchange(true)) return;
        if (peer_ != UltraRtcInvalidHandle) UltraRtc_ClosePeer(peer_);
        peer_ = UltraRtcInvalidHandle;
    }

private:
    std::function<void(std::function<void()>)> post_;
    std::function<void(RtcEvent)> onEvent_;   // the UI thread's
    std::atomic<bool> closed_{false};
    UltraRtcHandle peer_ = UltraRtcInvalidHandle;
};

} // namespace

RtcService MakeEngineRtc(std::function<void(std::function<void()>)> post) {
    if (!UltraRtc_Initialize().success || !UltraRtc_GetCapabilities().dataChannels) return {};
    return [post](const RtcConfig& config, std::function<void(RtcEvent)> onEvent,
                  std::string& error) -> std::shared_ptr<RtcPeer> {
        auto peer = std::make_shared<EnginePeer>(post, std::move(onEvent));
        if (!peer->Open(config, error)) return nullptr;
        return peer;
    };
}

#else

// A build without UltraRTC (ULTRACANVAS_ENABLE_ULTRARTC=OFF): no engine.
RtcService MakeEngineRtc(std::function<void(std::function<void()>)> post) {
    (void)post;
    return {};
}

#endif

} // namespace UltraWeb
