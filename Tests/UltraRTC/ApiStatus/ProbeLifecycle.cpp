// Tests/UltraRTC/ApiStatus/ProbeLifecycle.cpp
// UltraRtcApiStatus: the lifecycle functions - version, availability,
// capabilities, bringing the engine up and down, the event dispatcher.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <atomic>
#include <memory>

using namespace ultrartc_apistatus;

namespace {

// In a build without an engine the lifecycle still answers, and what it
// answers is part of the contract: every call says Unsupported.
bool AnswersUnsupported(const UltraRtcResult& r) {
    return !r && r.code == UltraRtcResultCode::Unsupported;
}

} // namespace

ULTRARTC_PROBE("Lifecycle", UltraRtc_GetVersion) {
    const std::string version = UltraRtc_GetVersion();
    PROBE_EXPECT(!version.empty());
    int dots = 0;
    for (char c : version) {
        PROBE_EXPECT_MSG((c >= '0' && c <= '9') || c == '.', "not a dotted number: " + version);
        if (c == '.') ++dots;
    }
    PROBE_EXPECT_MSG(dots == 2, "not major.minor.patch: " + version);
    return Working("\"" + version + "\"");
}

ULTRARTC_PROBE("Lifecycle", UltraRtc_IsAvailable) {
    const bool available = UltraRtc_IsAvailable();
    const UltraRtcCapabilities caps = UltraRtc_GetCapabilities();
    PROBE_EXPECT_MSG(available == caps.available, "it disagrees with UltraRtc_GetCapabilities().available");
    if (!available) {
        PROBE_EXPECT(AnswersUnsupported(UltraRtc_Initialize()));
        return Working("false: this build has no engine, and UltraRtc_Initialize says Unsupported");
    }
    return Working("true: an engine is compiled in");
}

ULTRARTC_PROBE("Lifecycle", UltraRtc_GetCapabilities) {
    const UltraRtcCapabilities caps = UltraRtc_GetCapabilities();
    if (!caps.available) {
        PROBE_EXPECT(caps.engine.empty() && !caps.dataChannels && !caps.audio && !caps.video);
        return Working("no engine: nothing is offered");
    }
    PROBE_EXPECT(!caps.engine.empty());
    PROBE_EXPECT(caps.dataChannels);
    return Working(caps.engine + "; data channels yes, audio " + (caps.audio ? "yes" : "no (no libopus)") +
                   ", video " + (caps.video ? "yes" : "no (no FFmpeg VP8)"));
}

ULTRARTC_PROBE("Lifecycle", UltraRtc_Initialize) {
    Outcome missing;
    if (EngineMissing(missing)) {
        PROBE_EXPECT(AnswersUnsupported(UltraRtc_Initialize()));
        return missing;
    }
    // From a known state: down.
    ClosePairs();
    UltraRtc_Shutdown();
    UltraRtcHandle peer = 7;
    UltraRtcResult before = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, peer);
    PROBE_EXPECT_MSG(!before && before.code == UltraRtcResultCode::NotInitialized,
                     "a peer could be created before UltraRtc_Initialize");
    PROBE_EXPECT(peer == UltraRtcInvalidHandle);

    UltraRtcResult first = UltraRtc_Initialize();
    PROBE_EXPECT_MSG(first, first.message);
    UltraRtcResult second = UltraRtc_Initialize();
    PROBE_EXPECT_MSG(second, "a second call failed: " + second.message);
    UltraRtcResult after = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, peer);
    PROBE_EXPECT_MSG(after, "no peer after UltraRtc_Initialize: " + after.message);
    UltraRtc_ClosePeer(peer);
    return Working("peers are refused before it (NotInitialized) and created after it; a second call is harmless");
}

ULTRARTC_PROBE("Lifecycle", UltraRtc_Shutdown) {
    Outcome missing;
    if (EngineMissing(missing)) {
        UltraRtc_Shutdown();        // must simply return
        return missing;
    }
    ClosePairs();
    EnsureInitialized();
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcResult created = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, peer);
    PROBE_EXPECT_MSG(created, created.message);

    UltraRtc_Shutdown();
    PROBE_EXPECT_MSG(UltraRtc_ClosePeer(peer).code == UltraRtcResultCode::InvalidHandle,
                     "a peer was still open after UltraRtc_Shutdown");
    UltraRtcHandle late = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, late).code == UltraRtcResultCode::NotInitialized);
    UltraRtc_Shutdown();            // twice: nothing to do

    // And up again: the rest of the report needs the engine, and a caller
    // may well start it a second time in one process.
    UltraRtcResult again = UltraRtc_Initialize();
    PROBE_EXPECT_MSG(again, "the engine did not come back after a shutdown: " + again.message);
    UltraRtcResult recreated = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, late);
    PROBE_EXPECT_MSG(recreated, "no peer after a second UltraRtc_Initialize: " + recreated.message);
    UltraRtc_ClosePeer(late);
    return Working("closes the open peers and refuses new ones; the engine starts again afterwards");
}

ULTRARTC_PROBE("Lifecycle", UltraRtc_SetEventDispatcher) {
    Outcome missing;
    if (EngineMissing(missing)) {
        UltraRtc_SetEventDispatcher(nullptr);       // must simply return
        return missing;
    }
    EnsureInitialized();
    // Shared, not on this stack: an engine thread may still hold the
    // dispatcher or the callback for a moment after they are replaced.
    auto dispatched = std::make_shared<std::atomic<int>>(0);
    auto delivered = std::make_shared<std::atomic<int>>(0);
    UltraRtc_SetEventDispatcher([dispatched](std::function<void()> task) {
        ++*dispatched;
        task();
    });

    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcResult created =
        UltraRtc_CreatePeer(UltraRtcPeerConfig(), [delivered](const UltraRtcPeerEvent&) { ++*delivered; }, peer);
    UltraRtcHandle channel = UltraRtcInvalidHandle;
    // With auto-negotiation a new channel produces the offer: an event.
    UltraRtcResult opened = created ? UltraRtc_CreateDataChannel(peer, "dispatch", UltraRtcDataChannelInit(), channel)
                                    : created;
    const bool arrived = opened && WaitUntil([&]() { return dispatched->load() > 0 && delivered->load() > 0; });

    // Without a dispatcher again: a second peer's events run on an engine
    // thread, past the counter above.
    UltraRtc_SetEventDispatcher(nullptr);
    const int dispatchedBefore = dispatched->load();
    const int deliveredBefore = delivered->load();
    UltraRtcHandle secondPeer = UltraRtcInvalidHandle;
    UltraRtcHandle secondChannel = UltraRtcInvalidHandle;
    bool direct = false;
    if (arrived &&
        UltraRtc_CreatePeer(UltraRtcPeerConfig(), [delivered](const UltraRtcPeerEvent&) { ++*delivered; }, secondPeer) &&
        UltraRtc_CreateDataChannel(secondPeer, "direct", UltraRtcDataChannelInit(), secondChannel)) {
        direct = WaitUntil([&]() { return delivered->load() > deliveredBefore; });
    }
    const int dispatchedAfter = dispatched->load();
    if (peer != UltraRtcInvalidHandle) UltraRtc_ClosePeer(peer);
    if (secondPeer != UltraRtcInvalidHandle) UltraRtc_ClosePeer(secondPeer);

    PROBE_EXPECT_MSG(created, created.message);
    PROBE_EXPECT_MSG(opened, opened.message);
    PROBE_EXPECT_MSG(arrived, "no event went through the dispatcher within 5 s");
    PROBE_EXPECT_MSG(dispatchedAfter == dispatchedBefore, "the removed dispatcher was still called");
    if (!direct) return Implemented("events went through the dispatcher; none followed its removal to compare with");
    return Working("events are handed to the dispatcher; with none installed they are delivered directly");
}
