// Plugins/UltraIM/direct/DirectCalls.cpp
// ChatCube Direct - 1:1 voice calls
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectCalls.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraRTC/UltraRTCCore.h"

#include <chrono>

namespace UltraIMDirect {

using UltraCanvas::JSONValue;

namespace {

const char* const kCallPrefix = "call:";
const char* const kAudioMid = "audio";

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

bool IsFinal(UltraIMCallState state) {
    return state == UltraIMCallState::Ended || state == UltraIMCallState::Failed;
}

UltraIMResult NoSuchCall() {
    return UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no such call");
}

} // namespace

void CallManager::SetProviderId(const std::string& providerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    providerId_ = providerId;
}

void CallManager::SetLocalUserId(const std::string& userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    localUserId_ = userId;
}

void CallManager::SetIceServers(const std::vector<UltraRtcIceServer>& servers) {
    std::lock_guard<std::mutex> lock(mutex_);
    iceServers_ = servers;
}

void CallManager::SetTimings(const CallTimings& timings) {
    std::lock_guard<std::mutex> lock(mutex_);
    timings_ = timings;
}

void CallManager::SetUseDevices(bool useDevices) {
    std::lock_guard<std::mutex> lock(mutex_);
    useDevices_ = useDevices;
}

bool CallManager::IsCallSession(const std::string& session) {
    return session.rfind(kCallPrefix, 0) == 0;
}

bool CallManager::InCall() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !calls_.empty();
}

UltraRtcHandle CallManager::GetAudioTrack(const std::string& callId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = calls_.find(callId);
    return it == calls_.end() ? UltraRtcInvalidHandle : it->second.track;
}

bool CallManager::Signal(const std::string& to, const char* kind, const std::string& session,
                         const std::string& data) {
    return sendSignal && sendSignal(to, kind, session, data);
}

void CallManager::EmitState(const Call& call, const std::string& reason) {
    if (!emit) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::CallStateChange;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.providerId = providerId_;
    }
    event.callId = call.id;
    event.conversationId = call.peerId;
    event.contactId = call.peerId;
    event.isVideo = call.video;
    event.callState = call.state;
    event.errorText = reason;
    emit(event);
}

void CallManager::SetState(const std::string& callId, UltraIMCallState state, const std::string& reason) {
    Call copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end() || it->second.state == state) return;
        it->second.state = state;
        it->second.sinceMs = NowMs();
        copy = it->second;
    }
    EmitState(copy, reason);
}

void CallManager::Finish(const std::string& callId, UltraIMCallState state, const std::string& reason,
                         const char* signalKind, const std::string& signalData) {
    Call copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end()) return;
        copy = it->second;
        calls_.erase(it);
    }
    if (signalKind) Signal(copy.peerId, signalKind, copy.id, signalData);
    // Closing the peer stops its microphone and speaker too.
    if (copy.peer != UltraRtcInvalidHandle) UltraRtc_ClosePeer(copy.peer);
    copy.state = state;
    EmitState(copy, reason);
}

// ============================================================================
// PLACING, ANSWERING, ENDING
// ============================================================================

UltraIMResult CallManager::Start(const std::string& peerId, bool video, std::string& outCallId) {
    outCallId.clear();
    if (video)
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Video calls are not available yet");
    Call call;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (localUserId_.empty())
            return UltraIMResult::Error(UltraIMResultCode::NotAuthenticated, "Not signed in");
        if (!calls_.empty())
            return UltraIMResult::Error(UltraIMResultCode::InvalidState, "Another call is in progress");
        call.id = std::string(kCallPrefix) + UltraIM_NewClientMessageId();
        call.peerId = peerId;
        call.outgoing = true;
        call.sinceMs = NowMs();
        calls_[call.id] = call;
    }
    outCallId = call.id;
    JSONValue invite = JSONValue::MakeObject();
    invite.Set("video", JSONValue(false));
    if (!Signal(peerId, "call-invite", call.id, UltraCanvas::JSON::Serialize(invite))) {
        std::lock_guard<std::mutex> lock(mutex_);
        calls_.erase(call.id);
        outCallId.clear();
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not connected to the relay");
    }
    EmitState(call, std::string());     // Ringing
    return UltraIMResult::Ok();
}

UltraIMResult CallManager::Accept(const std::string& callId) {
    std::string peerId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end()) return NoSuchCall();
        if (it->second.outgoing || it->second.state != UltraIMCallState::Ringing)
            return UltraIMResult::Error(UltraIMResultCode::InvalidState, "The call is not ringing here");
        peerId = it->second.peerId;
    }
    // The peer exists before the caller's offer can arrive.
    if (!StartMedia(callId, false)) {
        Finish(callId, UltraIMCallState::Failed, "The call could not be set up", "hangup");
        return UltraIMResult::Error(UltraIMResultCode::PluginError, "The call could not be set up");
    }
    SetState(callId, UltraIMCallState::Connecting);
    Signal(peerId, "call-accept", callId);
    return UltraIMResult::Ok();
}

UltraIMResult CallManager::End(const std::string& callId) {
    bool declining = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end()) return NoSuchCall();
        declining = !it->second.outgoing && it->second.state == UltraIMCallState::Ringing;
    }
    if (declining) {
        JSONValue reject = JSONValue::MakeObject();
        reject.Set("reason", JSONValue("declined"));
        Finish(callId, UltraIMCallState::Ended, "declined", "call-reject", UltraCanvas::JSON::Serialize(reject));
    } else {
        Finish(callId, UltraIMCallState::Ended, std::string(), "hangup");
    }
    return UltraIMResult::Ok();
}

UltraIMResult CallManager::SetMuted(const std::string& callId, bool muted) {
    UltraRtcHandle track = UltraRtcInvalidHandle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end()) return NoSuchCall();
        it->second.muted = muted;
        track = it->second.track;
    }
    if (track != UltraRtcInvalidHandle) UltraRtc_SetTrackEnabled(track, !muted);
    return UltraIMResult::Ok();
}

void CallManager::EndAll(const std::string& reason) {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : calls_) ids.push_back(entry.first);
    }
    for (const auto& id : ids) Finish(id, UltraIMCallState::Ended, reason, "hangup");
}

void CallManager::ExpireStale(int64_t nowMs) {
    std::vector<std::pair<std::string, int>> expired;    // 0 no answer, 1 missed, 2 no media
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : calls_) {
            const Call& call = entry.second;
            const int64_t age = nowMs - call.sinceMs;
            if (call.state == UltraIMCallState::Ringing && age > timings_.ringTimeoutMs)
                expired.push_back({call.id, call.outgoing ? 0 : 1});
            else if (call.state == UltraIMCallState::Connecting && age > timings_.connectTimeoutMs)
                expired.push_back({call.id, 2});
        }
    }
    for (const auto& entry : expired) {
        if (entry.second == 0) Finish(entry.first, UltraIMCallState::Ended, "no answer", "hangup");
        else if (entry.second == 1) Finish(entry.first, UltraIMCallState::Ended, "missed");
        else Finish(entry.first, UltraIMCallState::Failed, "The call could not connect", "hangup");
    }
}

// ============================================================================
// MEDIA
// ============================================================================

bool CallManager::StartMedia(const std::string& callId, bool offer) {
    UltraRtcPeerConfig config;
    bool muted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end() || it->second.peer != UltraRtcInvalidHandle) return false;
        config.iceServers = iceServers_;
        muted = it->second.muted;
    }
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    auto onEvent = [this, callId](const UltraRtcPeerEvent& event) { OnPeerEvent(callId, event); };
    if (!UltraRtc_CreatePeer(config, onEvent, peer)) return false;
    UltraRtcHandle track = UltraRtcInvalidHandle;
    if (!UltraRtc_AddAudioTrack(peer, kAudioMid, UltraRtcDirection::SendRecv, track)) {
        UltraRtc_ClosePeer(peer);
        return false;
    }
    if (muted) UltraRtc_SetTrackEnabled(track, false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end()) {           // ended meanwhile
            UltraRtc_ClosePeer(peer);
            return false;
        }
        it->second.peer = peer;
        it->second.track = track;
    }
    // Media only: nothing triggers auto-negotiation, so the caller asks.
    if (offer && !UltraRtc_SetLocalDescription(peer, UltraRtcSdpType::Offer)) return false;
    return true;
}

void CallManager::OnPeerEvent(const std::string& callId, const UltraRtcPeerEvent& event) {
    std::string peerId;
    UltraRtcHandle track = UltraRtcInvalidHandle;
    bool useDevices = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(callId);
        if (it == calls_.end() || it->second.peer != event.peer) return;
        peerId = it->second.peerId;
        track = it->second.track;
        useDevices = useDevices_;
    }
    switch (event.type) {
        case UltraRtcPeerEventType::LocalDescription: {
            JSONValue p = JSONValue::MakeObject();
            p.Set("sdp", JSONValue(event.sdp));
            Signal(peerId, event.sdpType == UltraRtcSdpType::Answer ? "answer" : "offer", callId,
                   UltraCanvas::JSON::Serialize(p));
            break;
        }
        case UltraRtcPeerEventType::IceCandidate: {
            JSONValue p = JSONValue::MakeObject();
            p.Set("candidate", JSONValue(event.candidate));
            p.Set("mid", JSONValue(event.sdpMid));
            Signal(peerId, "ice", callId, UltraCanvas::JSON::Serialize(p));
            break;
        }
        case UltraRtcPeerEventType::TrackOpen: {
            if (event.track != track) break;
            std::string problem;
            if (useDevices) {
                UltraRtcResult mic = UltraRtc_StartMicrophone(track);
                if (!mic) problem = "No microphone: " + mic.message;
                UltraRtcResult speaker = UltraRtc_StartPlayback(track);
                if (!speaker) problem += (problem.empty() ? "" : "; ") + std::string("No speaker: ") + speaker.message;
            }
            SetState(callId, UltraIMCallState::Active, problem);
            break;
        }
        case UltraRtcPeerEventType::ConnectionStateChange:
            if (event.connectionState == UltraRtcConnectionState::Failed ||
                event.connectionState == UltraRtcConnectionState::Disconnected ||
                event.connectionState == UltraRtcConnectionState::Closed)
                Finish(callId, UltraIMCallState::Failed, "The connection was lost", "hangup");
            break;
        default:
            break;
    }
}

// ============================================================================
// SIGNALLING IN
// ============================================================================

void CallManager::HandleSignal(const std::string& from, const std::string& kind,
                               const std::string& session, const std::string& data) {
    UltraCanvas::JSONParseResult parsed;
    JSONValue payload = UltraCanvas::JSON::Parse(data.empty() ? "{}" : data, &parsed);

    if (kind == "call-invite") {
        std::string crossed;            // our own outgoing call to the same contact
        bool busy = false;
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            known = calls_.count(session) > 0;
            for (const auto& entry : calls_) {
                if (entry.first == session) continue;
                const Call& call = entry.second;
                if (call.peerId == from && call.outgoing && call.state == UltraIMCallState::Ringing) crossed = call.id;
                else busy = true;
            }
            // Both called each other at once: the smaller user id's call goes ahead.
            if (!crossed.empty() && localUserId_ < from) return;
        }
        if (known) return;              // a repeated invite
        if (busy) {
            JSONValue reject = JSONValue::MakeObject();
            reject.Set("reason", JSONValue("busy"));
            Signal(from, "call-reject", session, UltraCanvas::JSON::Serialize(reject));
            return;
        }
        if (!crossed.empty()) Finish(crossed, UltraIMCallState::Ended, "crossed");
        Call call;
        call.id = session;
        call.peerId = from;
        call.outgoing = false;
        call.video = payload["video"].GetBoolean(false);
        call.sinceMs = NowMs();
        std::string providerId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            calls_[call.id] = call;
            providerId = providerId_;
        }
        if (emit) {
            UltraIMEvent event;
            event.type = UltraIMEventType::CallReceive;
            event.providerId = providerId;
            event.callId = call.id;
            event.conversationId = from;
            event.contactId = from;
            event.isVideo = call.video;
            event.callState = UltraIMCallState::Ringing;
            emit(event);
        }
        // We were calling them too: this is the call we wanted.
        if (!crossed.empty()) Accept(call.id);
        return;
    }

    Call call;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(session);
        if (it == calls_.end() || it->second.peerId != from) return;   // not ours, or over
        call = it->second;
    }

    if (kind == "call-accept") {
        if (!call.outgoing || call.state != UltraIMCallState::Ringing) return;
        SetState(session, UltraIMCallState::Connecting);
        if (!StartMedia(session, true)) Finish(session, UltraIMCallState::Failed, "The call could not be set up", "hangup");
    } else if (kind == "call-reject") {
        if (!call.outgoing) return;
        const std::string reason = payload["reason"].GetString();
        Finish(session, UltraIMCallState::Ended, reason.empty() ? "declined" : reason);
    } else if (kind == "hangup") {
        // An incoming call the caller gave up on was missed.
        const bool missed = !call.outgoing && call.state == UltraIMCallState::Ringing;
        Finish(session, UltraIMCallState::Ended, missed ? "missed" : "");
    } else if (kind == "offer") {
        if (call.outgoing || call.peer == UltraRtcInvalidHandle) return;
        // The answer follows by itself (auto-negotiation).
        if (!UltraRtc_SetRemoteDescription(call.peer, UltraRtcSdpType::Offer, payload["sdp"].GetString()))
            Finish(session, UltraIMCallState::Failed, "The call could not be set up", "hangup");
    } else if (kind == "answer") {
        if (!call.outgoing || call.peer == UltraRtcInvalidHandle) return;
        if (!UltraRtc_SetRemoteDescription(call.peer, UltraRtcSdpType::Answer, payload["sdp"].GetString()))
            Finish(session, UltraIMCallState::Failed, "The call could not be set up", "hangup");
    } else if (kind == "ice") {
        if (call.peer != UltraRtcInvalidHandle)
            UltraRtc_AddIceCandidate(call.peer, payload["candidate"].GetString(), payload["mid"].GetString());
    }
}

void CallManager::HandleUndeliverable(const std::string& to, const std::string& session) {
    bool ringingOut = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(session);
        if (it == calls_.end() || it->second.peerId != to) return;
        ringingOut = it->second.outgoing && it->second.state == UltraIMCallState::Ringing;
    }
    if (ringingOut) Finish(session, UltraIMCallState::Ended, "offline");
    else Finish(session, UltraIMCallState::Failed, "The connection was lost");
}

} // namespace UltraIMDirect
