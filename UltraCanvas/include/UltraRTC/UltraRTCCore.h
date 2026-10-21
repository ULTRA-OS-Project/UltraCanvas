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

// ---- Audio -------------------------------------------------------------------
// An Opus audio track (48 kHz, mono, in-band FEC). Add it on both sides,
// with the same mid, before the offer / before applying the remote offer:
// the two then share one send-and-receive m-line. A track the other side
// adds that we did not arrives as a Track event.
UltraRtcResult UltraRtc_AddAudioTrack(UltraRtcHandle peer, const std::string& mid,
                                      UltraRtcDirection direction, UltraRtcHandle& outTrack);
// Encodes and sends PCM (48 kHz; stereo is mixed down). For a test or an
// application that captures audio itself; UltraRtc_StartMicrophone does it
// from a device.
UltraRtcResult UltraRtc_SendAudioFrame(UltraRtcHandle track, const UltraRtcAudioFrame& frame);
// Decoded received audio, 20 ms at a time, on a media thread (lost packets
// are concealed). Replaces any earlier callback; nullptr removes it.
UltraRtcResult UltraRtc_SetAudioFrameCallback(UltraRtcHandle track, UltraRtcAudioFrameCallback onFrame);
// Mute / camera off: a disabled track sends nothing (the other side
// conceals silence, or keeps the last picture).
UltraRtcResult UltraRtc_SetTrackEnabled(UltraRtcHandle track, bool enabled);
bool           UltraRtc_IsTrackOpen(UltraRtcHandle track);

// Device audio: the microphone feeds the track, received audio plays on the
// output device. deviceId "" = the system default. Stopped by the Stop
// calls or when the peer closes.
UltraRtcResult UltraRtc_StartMicrophone(UltraRtcHandle track, const std::string& deviceId = std::string());
UltraRtcResult UltraRtc_StopMicrophone(UltraRtcHandle track);
UltraRtcResult UltraRtc_StartPlayback(UltraRtcHandle track, const std::string& deviceId = std::string());
UltraRtcResult UltraRtc_StopPlayback(UltraRtcHandle track);

// ---- Video -------------------------------------------------------------------
// A VP8 video track. As with audio, add it on both sides with the same mid
// before the offer. A receiver that loses part of a picture asks the sender
// for a key frame by itself and shows nothing broken meanwhile.
UltraRtcResult UltraRtc_AddVideoTrack(UltraRtcHandle peer, const std::string& mid, UltraRtcDirection direction,
                                      const UltraRtcVideoConfig& config, UltraRtcHandle& outTrack);
// Encodes and sends one picture (any size; scaled to the track's). Pictures
// faster than the track's frame rate are dropped.
UltraRtcResult UltraRtc_SendVideoFrame(UltraRtcHandle track, const UltraRtcVideoFrame& frame);
// Decoded received pictures. Replaces any earlier callback; nullptr removes it.
UltraRtcResult UltraRtc_SetVideoFrameCallback(UltraRtcHandle track, UltraRtcVideoFrameCallback onFrame);
// Asks the other side for a key frame (a receiver that joins late).
UltraRtcResult UltraRtc_RequestKeyFrame(UltraRtcHandle track);
// The camera feeds the track. onPreview gets every captured picture (the
// self view), on the capture thread. deviceId "" = the default camera.
// Stopped by UltraRtc_StopCamera or when the peer closes. A disabled track
// (UltraRtc_SetTrackEnabled) sends nothing; the preview goes on.
UltraRtcResult UltraRtc_StartCamera(UltraRtcHandle track, const std::string& deviceId = std::string(),
                                    UltraRtcVideoFrameCallback onPreview = nullptr);
UltraRtcResult UltraRtc_StopCamera(UltraRtcHandle track);
