// Plugins/UltraIM/direct/DirectCalls.h
// ChatCube Direct - 1:1 voice calls: the invite / accept / reject / hang-up
// exchange over the provider's signalling, and one UltraRTC peer with an
// Opus track per call
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraIM/UltraIMTypes.h"
#include "UltraRTC/UltraRTCTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace UltraIMDirect {

struct CallTimings {
    int64_t ringTimeoutMs = 45000;      // unanswered by then: "no answer" / "missed"
    int64_t connectTimeoutMs = 20000;   // answered but no media by then: failed
};

// Signalling (all signed by the provider, session = the call id):
//   call-invite  {"video":false}   caller -> callee
//   call-accept                     callee -> caller; the caller then offers
//   call-reject  {"reason":"..."}   callee -> caller ("declined", "busy")
//   offer / answer / ice            the media peer, as for a chat link
//   hangup                          either side, in any state
class CallManager {
public:
    // false: the signal could not be sent (the relay socket is down).
    std::function<bool(const std::string& to, const std::string& kind,
                       const std::string& session, const std::string& data)> sendSignal;
    std::function<void(const UltraIMEvent& event)> emit;

    void SetProviderId(const std::string& providerId);
    void SetLocalUserId(const std::string& userId);
    void SetIceServers(const std::vector<UltraRtcIceServer>& servers);
    void SetTimings(const CallTimings& timings);
    // false: a call does not open the microphone and speaker (tests feed
    // and read the track themselves).
    void SetUseDevices(bool useDevices);

    static bool IsCallSession(const std::string& session);

    UltraIMResult Start(const std::string& peerId, bool video, std::string& outCallId);
    UltraIMResult Accept(const std::string& callId);
    // Hangs up, cancels or declines, whatever the call's state.
    UltraIMResult End(const std::string& callId);
    UltraIMResult SetMuted(const std::string& callId, bool muted);
    bool InCall();

    void HandleSignal(const std::string& from, const std::string& kind,
                      const std::string& session, const std::string& data);
    // The relay could not reach the other side.
    void HandleUndeliverable(const std::string& to, const std::string& session);
    void ExpireStale(int64_t nowMs);
    // Signing out: every call ends at once.
    void EndAll(const std::string& reason);

    // The call's audio track (UltraRtcInvalidHandle before the media peer exists).
    UltraRtcHandle GetAudioTrack(const std::string& callId);

private:
    struct Call {
        std::string      id;
        std::string      peerId;
        bool             outgoing = false;
        bool             video = false;
        UltraIMCallState state = UltraIMCallState::Ringing;
        int64_t          sinceMs = 0;       // when the state was entered
        UltraRtcHandle   peer = UltraRtcInvalidHandle;
        UltraRtcHandle   track = UltraRtcInvalidHandle;
        bool             muted = false;
    };

    // Creates the media peer and its audio track; `offer` produces our offer.
    bool StartMedia(const std::string& callId, bool offer);
    void OnPeerEvent(const std::string& callId, const UltraRtcPeerEvent& event);
    void SetState(const std::string& callId, UltraIMCallState state, const std::string& reason = std::string());
    // Ends the call: closes its peer, emits the final state, forgets it.
    void Finish(const std::string& callId, UltraIMCallState state, const std::string& reason,
                const char* signalKind = nullptr, const std::string& signalData = std::string());
    void EmitState(const Call& call, const std::string& reason);
    bool Signal(const std::string& to, const char* kind, const std::string& session,
                const std::string& data = std::string());

    std::mutex mutex_;
    std::string providerId_;
    std::string localUserId_;
    std::vector<UltraRtcIceServer> iceServers_;
    CallTimings timings_;
    bool useDevices_ = true;
    std::map<std::string, Call> calls_;     // call id -> call
};

} // namespace UltraIMDirect
