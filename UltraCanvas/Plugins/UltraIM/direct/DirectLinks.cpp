// Plugins/UltraIM/direct/DirectLinks.cpp
// ChatCube Direct - one WebRTC data channel per contact
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectLinks.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraRTC/UltraRTCCore.h"

#include <chrono>

namespace UltraIMDirect {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

const char* SdpTypeName(UltraRtcSdpType type) {
    return type == UltraRtcSdpType::Answer ? "answer" : "offer";
}

} // namespace

void LinkManager::SetLocalUserId(const std::string& userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    localUserId_ = userId;
}

void LinkManager::SetIceServers(const std::vector<UltraRtcIceServer>& servers) {
    std::lock_guard<std::mutex> lock(mutex_);
    iceServers_ = servers;
}

UltraRtcPeerEventCallback LinkManager::MakeHandler(const std::string& peerId, const std::string& session) {
    return [this, peerId, session](const UltraRtcPeerEvent& event) { OnPeerEvent(peerId, session, event); };
}

bool LinkManager::StartPeer(const std::string& peerId, const std::string& session, UltraRtcHandle& outPeer) {
    UltraRtcPeerConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config.iceServers = iceServers_;
    }
    return static_cast<bool>(UltraRtc_CreatePeer(config, MakeHandler(peerId, session), outPeer));
}

void LinkManager::Ensure(const std::string& peerId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (links_.count(peerId) || localUserId_.empty() || peerId == localUserId_) return;
        Link link;
        link.peerId = peerId;
        link.session = UltraIM_NewClientMessageId();
        link.startedMs = NowMs();
        links_[peerId] = link;
    }
    std::string session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session = links_[peerId].session;
    }
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    if (!StartPeer(peerId, session, peer)) {
        Drop(peerId, true);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(peerId);
        if (it == links_.end() || it->second.session != session) {
            UltraRtc_ClosePeer(peer);
            return;
        }
        it->second.peer = peer;
    }
    // Opening the channel produces the offer (auto-negotiation).
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    if (!UltraRtc_CreateDataChannel(peer, "chatcube", UltraRtcDataChannelInit(), channel)) {
        Drop(peerId, true);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = links_.find(peerId);
    if (it != links_.end() && it->second.session == session) it->second.channel = channel;
}

void LinkManager::HandleSignal(const std::string& from, const std::string& kind,
                               const std::string& session, const std::string& data) {
    UltraCanvas::JSONParseResult parsed;
    UltraCanvas::JSONValue payload = UltraCanvas::JSON::Parse(data.empty() ? "{}" : data, &parsed);

    if (kind == "offer") {
        bool replace = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = links_.find(from);
            if (it != links_.end() && it->second.session != session) {
                // Both sides offered at once: the smaller user id keeps its own.
                if (!it->second.open && localUserId_ < from) return;
                replace = true;
            }
        }
        if (replace) Drop(from, false);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!links_.count(from)) {
                Link link;
                link.peerId = from;
                link.session = session;
                link.startedMs = NowMs();
                links_[from] = link;
            }
        }
        UltraRtcHandle peer = UltraRtcInvalidHandle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            peer = links_[from].peer;
        }
        if (peer == UltraRtcInvalidHandle) {
            if (!StartPeer(from, session, peer)) {
                Drop(from, true);
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            links_[from].peer = peer;
        }
        // A remote offer produces our answer (auto-negotiation).
        if (!UltraRtc_SetRemoteDescription(peer, UltraRtcSdpType::Offer, payload["sdp"].GetString()))
            Drop(from, true);
        return;
    }

    UltraRtcHandle peer = UltraRtcInvalidHandle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(from);
        if (it == links_.end() || it->second.session != session) return;   // a stale link
        peer = it->second.peer;
    }
    if (peer == UltraRtcInvalidHandle) return;
    if (kind == "answer") {
        if (!UltraRtc_SetRemoteDescription(peer, UltraRtcSdpType::Answer, payload["sdp"].GetString()))
            Drop(from, true);
    } else if (kind == "ice") {
        UltraRtc_AddIceCandidate(peer, payload["candidate"].GetString(), payload["mid"].GetString());
    } else if (kind == "hangup") {
        Drop(from, true);
    }
}

void LinkManager::OnPeerEvent(const std::string& peerId, const std::string& session, const UltraRtcPeerEvent& event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(peerId);
        if (it == links_.end() || it->second.session != session) return;
        if (event.type == UltraRtcPeerEventType::DataChannel && it->second.channel == UltraRtcInvalidHandle)
            it->second.channel = event.channel;
    }
    switch (event.type) {
        case UltraRtcPeerEventType::LocalDescription: {
            UltraCanvas::JSONValue p = UltraCanvas::JSONValue::MakeObject();
            p.Set("sdp", UltraCanvas::JSONValue(event.sdp));
            if (sendSignal) sendSignal(peerId, SdpTypeName(event.sdpType), session, UltraCanvas::JSON::Serialize(p));
            break;
        }
        case UltraRtcPeerEventType::IceCandidate: {
            UltraCanvas::JSONValue p = UltraCanvas::JSONValue::MakeObject();
            p.Set("candidate", UltraCanvas::JSONValue(event.candidate));
            p.Set("mid", UltraCanvas::JSONValue(event.sdpMid));
            if (sendSignal) sendSignal(peerId, "ice", session, UltraCanvas::JSON::Serialize(p));
            break;
        }
        case UltraRtcPeerEventType::DataChannelOpen: {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = links_.find(peerId);
                if (it != links_.end()) it->second.open = true;
            }
            if (onOpen) onOpen(peerId);
            break;
        }
        case UltraRtcPeerEventType::DataChannelMessage:
            if (!event.isBinary && onMessage) onMessage(peerId, event.text);
            break;
        case UltraRtcPeerEventType::DataChannelClose:
            Drop(peerId, true);
            break;
        case UltraRtcPeerEventType::ConnectionStateChange:
            if (event.connectionState == UltraRtcConnectionState::Failed ||
                event.connectionState == UltraRtcConnectionState::Closed ||
                event.connectionState == UltraRtcConnectionState::Disconnected)
                Drop(peerId, true);
            break;
        default:
            break;
    }
}

bool LinkManager::IsOpen(const std::string& peerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = links_.find(peerId);
    return it != links_.end() && it->second.open;
}

bool LinkManager::Send(const std::string& peerId, const std::string& text) {
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(peerId);
        if (it == links_.end() || !it->second.open) return false;
        channel = it->second.channel;
    }
    return static_cast<bool>(UltraRtc_DataChannelSendText(channel, text));
}

void LinkManager::Drop(const std::string& peerId, bool notify) {
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(peerId);
        if (it == links_.end()) return;
        peer = it->second.peer;
        links_.erase(it);
    }
    if (peer != UltraRtcInvalidHandle) UltraRtc_ClosePeer(peer);
    if (notify && onClosed) onClosed(peerId);
}

void LinkManager::Close(const std::string& peerId) {
    std::string session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(peerId);
        if (it == links_.end()) return;
        session = it->second.session;
    }
    if (sendSignal) sendSignal(peerId, "hangup", session, std::string());
    Drop(peerId, false);
}

void LinkManager::CloseAll() {
    std::vector<std::string> peers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : links_) peers.push_back(entry.first);
    }
    for (const auto& peer : peers) Drop(peer, false);
}

void LinkManager::ExpireStale(int64_t nowMs, int64_t timeoutMs) {
    std::vector<std::string> stale;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : links_) {
            if (!entry.second.open && nowMs - entry.second.startedMs > timeoutMs) stale.push_back(entry.first);
        }
    }
    for (const auto& peer : stale) Drop(peer, true);
}

} // namespace UltraIMDirect
