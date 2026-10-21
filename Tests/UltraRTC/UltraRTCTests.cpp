// Tests/UltraRTC/UltraRTCTests.cpp
// UltraRTC: peers, signalling pass-through, data channels, closing
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTC/UltraRTCCore.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// What each side saw; written from engine threads (no dispatcher installed).
struct Side {
    UltraRtcHandle peer = UltraRtcInvalidHandle;
    UltraRtcHandle channel = UltraRtcInvalidHandle;     // ours or the one the other side opened
    bool open = false;
    bool connected = false;
    std::vector<std::string> texts;
    std::vector<std::vector<uint8_t>> binaries;
};

std::mutex g_mutex;
std::condition_variable g_changed;

bool WaitFor(const std::function<bool()>& condition, int seconds = 20) {
    std::unique_lock<std::mutex> lock(g_mutex);
    return g_changed.wait_for(lock, std::chrono::seconds(seconds), condition);
}

// The signalling channel: whatever one side produces goes to the other,
// untouched.
UltraRtcPeerEventCallback MakeHandler(Side& self, Side& other) {
    return [&self, &other](const UltraRtcPeerEvent& e) {
        switch (e.type) {
            case UltraRtcPeerEventType::LocalDescription:
                CHECK(UltraRtc_SetRemoteDescription(other.peer, e.sdpType, e.sdp));
                break;
            case UltraRtcPeerEventType::IceCandidate:
                UltraRtc_AddIceCandidate(other.peer, e.candidate, e.sdpMid);
                break;
            default:
                break;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        if (e.type == UltraRtcPeerEventType::DataChannel) {
            CHECK(e.channelLabel == "chat");
            self.channel = e.channel;
        }
        if (e.type == UltraRtcPeerEventType::DataChannelOpen) self.open = true;
        if (e.type == UltraRtcPeerEventType::ConnectionStateChange &&
            e.connectionState == UltraRtcConnectionState::Connected)
            self.connected = true;
        if (e.type == UltraRtcPeerEventType::DataChannelMessage) {
            if (e.isBinary) self.binaries.push_back(e.binary);
            else self.texts.push_back(e.text);
        }
        g_changed.notify_all();
    };
}

void TestNotInitialized() {
    UltraRtcHandle peer = 7;
    UltraRtcResult r = UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, peer);
    CHECK(!r);
    CHECK(peer == UltraRtcInvalidHandle);
}

void TestDataChannel() {
    Side a, b;
    UltraRtcPeerConfig config;   // no ICE servers: host candidates only
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(a, b), a.peer));
    CHECK(UltraRtc_CreatePeer(config, MakeHandler(b, a), b.peer));
    CHECK(a.peer != b.peer);

    UltraRtcHandle channel = UltraRtcInvalidHandle;
    CHECK(UltraRtc_CreateDataChannel(a.peer, "chat", UltraRtcDataChannelInit(), channel));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        a.channel = channel;
    }
    CHECK(UltraRtc_GetDataChannelLabel(channel) == "chat");
    CHECK(UltraRtc_GetDataChannelPeer(channel) == a.peer);

    const bool bothOpen = WaitFor([&]() { return a.open && b.open && b.channel != UltraRtcInvalidHandle; });
    CHECK(bothOpen);
    if (!bothOpen) return;
    CHECK(UltraRtc_GetConnectionState(a.peer) == UltraRtcConnectionState::Connected);
    CHECK(UltraRtc_GetDataChannelState(a.channel) == UltraRtcDataChannelState::Open);

    UltraRtcSdpType type;
    std::string sdp;
    CHECK(UltraRtc_GetLocalDescription(a.peer, type, sdp) && type == UltraRtcSdpType::Offer);
    CHECK(sdp.find("a=fingerprint:") != std::string::npos);     // DTLS is always on

    CHECK(UltraRtc_DataChannelSendText(a.channel, "ping"));
    CHECK(WaitFor([&]() { return !b.texts.empty(); }));
    CHECK(!b.texts.empty() && b.texts[0] == "ping");

    const std::vector<uint8_t> payload = {0, 1, 2, 250, 255};
    CHECK(UltraRtc_DataChannelSendBinary(b.channel, payload));
    CHECK(WaitFor([&]() { return !a.binaries.empty(); }));
    CHECK(!a.binaries.empty() && a.binaries[0] == payload);

    CHECK(UltraRtc_ClosePeer(a.peer));
    CHECK(!UltraRtc_ClosePeer(a.peer));                          // already closed
    CHECK(UltraRtc_DataChannelSendText(a.channel, "late").code == UltraRtcResultCode::InvalidHandle);
    CHECK(UltraRtc_ClosePeer(b.peer));
}

} // namespace

int main() {
    TestNotInitialized();
    if (!UltraRtc_IsAvailable()) {
        std::printf("UltraRTCTests: no engine in this build - skipped\n");
        return 0;
    }
    CHECK(UltraRtc_Initialize());
    UltraRtcCapabilities caps = UltraRtc_GetCapabilities();
    CHECK(caps.available && caps.dataChannels);
    std::printf("UltraRTC engine: %s\n", caps.engine.c_str());
    TestDataChannel();
    UltraRtc_Shutdown();
    std::printf("UltraRTCTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
