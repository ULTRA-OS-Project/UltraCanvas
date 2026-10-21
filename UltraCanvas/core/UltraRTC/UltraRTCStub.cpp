// core/UltraRTC/UltraRTCStub.cpp
// UltraRTC - the UltraRtc_* API in a build without an engine (no OpenSSL):
// every call answers Unsupported, so callers can offer the feature or not.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTC/UltraRTCCore.h"

namespace {
UltraRtcResult NoEngine() {
    return UltraRtcResult::Error(UltraRtcResultCode::Unsupported,
                                 "This build has no WebRTC engine (UltraRTC was built without OpenSSL)");
}
} // namespace

UltraRtcResult UltraRtc_Initialize() { return NoEngine(); }
void UltraRtc_Shutdown() {}
bool UltraRtc_IsAvailable() { return false; }
UltraRtcCapabilities UltraRtc_GetCapabilities() { return UltraRtcCapabilities(); }
void UltraRtc_SetEventDispatcher(UltraRtcDispatcher) {}

UltraRtcResult UltraRtc_CreatePeer(const UltraRtcPeerConfig&, UltraRtcPeerEventCallback, UltraRtcHandle& outPeer) {
    outPeer = UltraRtcInvalidHandle;
    return NoEngine();
}
UltraRtcResult UltraRtc_ClosePeer(UltraRtcHandle) { return NoEngine(); }
UltraRtcResult UltraRtc_SetPeerEventCallback(UltraRtcHandle, UltraRtcPeerEventCallback) { return NoEngine(); }
UltraRtcResult UltraRtc_SetLocalDescription(UltraRtcHandle, UltraRtcSdpType) { return NoEngine(); }
UltraRtcResult UltraRtc_SetRemoteDescription(UltraRtcHandle, UltraRtcSdpType, const std::string&) { return NoEngine(); }
UltraRtcResult UltraRtc_AddIceCandidate(UltraRtcHandle, const std::string&, const std::string&) { return NoEngine(); }
UltraRtcResult UltraRtc_GetLocalDescription(UltraRtcHandle, UltraRtcSdpType&, std::string&) { return NoEngine(); }
UltraRtcResult UltraRtc_GetRemoteDescription(UltraRtcHandle, UltraRtcSdpType&, std::string&) { return NoEngine(); }
UltraRtcConnectionState UltraRtc_GetConnectionState(UltraRtcHandle) { return UltraRtcConnectionState::Closed; }
UltraRtcIceGatheringState UltraRtc_GetIceGatheringState(UltraRtcHandle) { return UltraRtcIceGatheringState::New; }
UltraRtcSignalingState UltraRtc_GetSignalingState(UltraRtcHandle) { return UltraRtcSignalingState::Stable; }

UltraRtcResult UltraRtc_CreateDataChannel(UltraRtcHandle, const std::string&, const UltraRtcDataChannelInit&,
                                          UltraRtcHandle& outChannel) {
    outChannel = UltraRtcInvalidHandle;
    return NoEngine();
}
UltraRtcResult UltraRtc_DataChannelSendText(UltraRtcHandle, const std::string&) { return NoEngine(); }
UltraRtcResult UltraRtc_DataChannelSendBinary(UltraRtcHandle, const std::vector<uint8_t>&) { return NoEngine(); }
UltraRtcResult UltraRtc_DataChannelClose(UltraRtcHandle) { return NoEngine(); }
UltraRtcDataChannelState UltraRtc_GetDataChannelState(UltraRtcHandle) { return UltraRtcDataChannelState::Closed; }
std::string UltraRtc_GetDataChannelLabel(UltraRtcHandle) { return std::string(); }
UltraRtcHandle UltraRtc_GetDataChannelPeer(UltraRtcHandle) { return UltraRtcInvalidHandle; }
size_t UltraRtc_GetDataChannelBufferedAmount(UltraRtcHandle) { return 0; }
