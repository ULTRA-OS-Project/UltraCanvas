// core/UltraRTC/UltraRTCEngine.cpp
// UltraRTC - the UltraRtc_* API over libdatachannel (peers and data channels)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTC/UltraRTCCore.h"

#include <rtc/rtc.hpp>

#include <atomic>
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

    outPeer = handle;
    return UltraRtcResult::Ok();
}

UltraRtcResult UltraRtc_ClosePeer(UltraRtcHandle peer) {
    std::shared_ptr<rtc::PeerConnection> connection;
    std::vector<std::shared_ptr<rtc::DataChannel>> channels;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        auto it = State().peers.find(peer);
        if (it == State().peers.end()) return BadHandle();
        connection = it->second.connection;
        for (UltraRtcHandle channel : it->second.channels) {
            auto c = State().channels.find(channel);
            if (c != State().channels.end()) {
                channels.push_back(c->second.channel);
                State().channels.erase(c);
            }
        }
        State().peers.erase(it);
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
