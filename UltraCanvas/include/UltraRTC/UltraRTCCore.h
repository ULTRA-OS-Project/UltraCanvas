// include/UltraRTC/UltraRTCCore.h
// UltraRTC - the public real-time communication API (UltraRtc_* functions)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraRTCTypes.h"

#include <string>
#include <vector>

// Threading: the engine runs its own threads. Peer events are delivered
// through the dispatcher installed with UltraRtc_SetEventDispatcher (an app
// with a UI posts them to its UI thread); without one they run on an engine
// thread, where a callback must not block. Events queued for a peer that
// was closed meanwhile are dropped. Every function may be called from any
// thread, including from inside an event callback.
//
// SDP and candidates pass through untouched: the strings a LocalDescription
// or IceCandidate event carries are sent to the other side as they are, and
// what arrives is handed to UltraRtc_SetRemoteDescription /
// UltraRtc_AddIceCandidate as it is. DTLS is always on; the remote
// certificate is checked against the fingerprint in the remote SDP, so a
// signalling channel that authenticates the SDP authenticates the link.

// ---- Lifecycle ---------------------------------------------------------------
UltraRtcResult       UltraRtc_Initialize();
// Closes every peer and waits for the engine's threads to stop.
void                 UltraRtc_Shutdown();
bool                 UltraRtc_IsAvailable();
UltraRtcCapabilities UltraRtc_GetCapabilities();
void                 UltraRtc_SetEventDispatcher(UltraRtcDispatcher dispatcher);

// ---- Peers -------------------------------------------------------------------
UltraRtcResult UltraRtc_CreatePeer(const UltraRtcPeerConfig& config,
                                   UltraRtcPeerEventCallback onEvent,
                                   UltraRtcHandle& outPeer);
// Closes the peer and every data channel on it. No event for it follows.
UltraRtcResult UltraRtc_ClosePeer(UltraRtcHandle peer);
UltraRtcResult UltraRtc_SetPeerEventCallback(UltraRtcHandle peer, UltraRtcPeerEventCallback onEvent);

// Produces the local offer or answer; it arrives as a LocalDescription event.
UltraRtcResult UltraRtc_SetLocalDescription(UltraRtcHandle peer,
                                            UltraRtcSdpType type = UltraRtcSdpType::Unspecified);
UltraRtcResult UltraRtc_SetRemoteDescription(UltraRtcHandle peer, UltraRtcSdpType type,
                                             const std::string& sdp);
UltraRtcResult UltraRtc_AddIceCandidate(UltraRtcHandle peer, const std::string& candidate,
                                        const std::string& sdpMid);
UltraRtcResult UltraRtc_GetLocalDescription(UltraRtcHandle peer, UltraRtcSdpType& outType,
                                            std::string& outSdp);
UltraRtcResult UltraRtc_GetRemoteDescription(UltraRtcHandle peer, UltraRtcSdpType& outType,
                                             std::string& outSdp);
UltraRtcConnectionState   UltraRtc_GetConnectionState(UltraRtcHandle peer);
UltraRtcIceGatheringState UltraRtc_GetIceGatheringState(UltraRtcHandle peer);
UltraRtcSignalingState    UltraRtc_GetSignalingState(UltraRtcHandle peer);

// ---- Data channels -----------------------------------------------------------
UltraRtcResult UltraRtc_CreateDataChannel(UltraRtcHandle peer, const std::string& label,
                                          const UltraRtcDataChannelInit& init,
                                          UltraRtcHandle& outChannel);
UltraRtcResult UltraRtc_DataChannelSendText(UltraRtcHandle channel, const std::string& text);
UltraRtcResult UltraRtc_DataChannelSendBinary(UltraRtcHandle channel, const std::vector<uint8_t>& data);
UltraRtcResult UltraRtc_DataChannelClose(UltraRtcHandle channel);
UltraRtcDataChannelState UltraRtc_GetDataChannelState(UltraRtcHandle channel);
std::string    UltraRtc_GetDataChannelLabel(UltraRtcHandle channel);
UltraRtcHandle UltraRtc_GetDataChannelPeer(UltraRtcHandle channel);
size_t         UltraRtc_GetDataChannelBufferedAmount(UltraRtcHandle channel);
