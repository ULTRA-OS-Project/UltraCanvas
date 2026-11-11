// Tests/UltraRTC/ApiStatus/ProbeDataChannels.cpp
// UltraRtcApiStatus: data channels - opening and closing them, text and
// binary messages, and what a channel says about itself.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <algorithm>

using namespace ultrartc_apistatus;

ULTRARTC_PROBE("DataChannels", UltraRtc_CreateDataChannel) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        PROBE_EXPECT(pair.a.channel != UltraRtcInvalidHandle && pair.b.channel != UltraRtcInvalidHandle);
        PROBE_EXPECT_MSG(!pair.b.channelsAnnounced.empty() && pair.b.channelsAnnounced[0] == "probe",
                         "the other side was not told of the channel by its label");
    }
    UltraRtcDataChannelInit both;
    both.maxRetransmits = 3;
    both.maxPacketLifeTimeMs = 500;
    UltraRtcHandle refused = 7;
    const UltraRtcResult r = UltraRtc_CreateDataChannel(pair.a.peer, "both", both, refused);
    PROBE_EXPECT_MSG(!r && r.code == UltraRtcResultCode::InvalidArgument && refused == UltraRtcInvalidHandle,
                     "maxRetransmits together with maxPacketLifeTimeMs was accepted");
    PROBE_EXPECT(UltraRtc_CreateDataChannel(UltraRtcInvalidHandle, "none", UltraRtcDataChannelInit(), refused).code ==
                 UltraRtcResultCode::InvalidHandle);
    return Working("a channel opened by one side arrived at the other with its label and opened on both; the two "
                   "reliability limits together are refused");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_DataChannelSendText) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    const std::string text = "gr\xC3\xBC\xC3\x9F" "e \xE2\x9C\x93 probe";    // UTF-8 beyond ASCII
    UltraRtcResult r = UltraRtc_DataChannelSendText(pair.a.channel, text);
    PROBE_EXPECT_MSG(r, r.message);
    PROBE_EXPECT_MSG(pair.WaitFor([&]() {
        return std::find(pair.b.texts.begin(), pair.b.texts.end(), text) != pair.b.texts.end();
    }, 5000), "the text did not arrive unchanged within 5 s");
    PROBE_EXPECT(UltraRtc_DataChannelSendText(UltraRtcInvalidHandle, "x").code == UltraRtcResultCode::InvalidHandle);
    return Working("a UTF-8 text sent by the caller arrived unchanged as a DataChannelMessage event");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_DataChannelSendBinary) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    std::vector<uint8_t> payload(256);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);    // every byte value
    UltraRtcResult r = UltraRtc_DataChannelSendBinary(pair.b.channel, payload);          // the callee answers
    PROBE_EXPECT_MSG(r, r.message);
    PROBE_EXPECT_MSG(pair.WaitFor([&]() {
        return std::find(pair.a.binaries.begin(), pair.a.binaries.end(), payload) != pair.a.binaries.end();
    }, 5000), "the bytes did not arrive unchanged within 5 s");
    return Working("all 256 byte values sent by the callee arrived unchanged, marked binary");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_DataChannelClose) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    UltraRtcHandle mine = UltraRtcInvalidHandle;
    UltraRtcResult created = UltraRtc_CreateDataChannel(pair.a.peer, "closing", UltraRtcDataChannelInit(), mine);
    PROBE_EXPECT_MSG(created, created.message);
    UltraRtcHandle theirs = UltraRtcInvalidHandle;
    const bool open = pair.WaitFor([&]() {
        for (const auto& extra : pair.b.extraChannels) {
            if (extra.first == "closing") theirs = extra.second;
        }
        return theirs != UltraRtcInvalidHandle &&
               std::find(pair.b.channelsOpened.begin(), pair.b.channelsOpened.end(), theirs) !=
                   pair.b.channelsOpened.end() &&
               std::find(pair.a.channelsOpened.begin(), pair.a.channelsOpened.end(), mine) !=
                   pair.a.channelsOpened.end();
    }, 10000);
    PROBE_EXPECT_MSG(open, "a second channel on the connected pair did not open within 10 s");

    UltraRtcResult closed = UltraRtc_DataChannelClose(mine);
    PROBE_EXPECT_MSG(closed, closed.message);
    PROBE_EXPECT(UltraRtc_GetDataChannelState(mine) == UltraRtcDataChannelState::Closed);
    PROBE_EXPECT_MSG(UltraRtc_DataChannelClose(mine).code == UltraRtcResultCode::InvalidHandle,
                     "closing a closed channel did not answer InvalidHandle");
    const bool told = pair.WaitFor([&]() {
        return std::find(pair.b.channelsClosed.begin(), pair.b.channelsClosed.end(), theirs) !=
               pair.b.channelsClosed.end();
    }, 10000);
    PROBE_EXPECT_MSG(told, "the other side got no DataChannelClose event within 10 s");
    PROBE_EXPECT_MSG(UltraRtc_GetDataChannelState(pair.a.channel) == UltraRtcDataChannelState::Open,
                     "closing one channel closed another on the same peer");
    return Working("the channel closed here and the other side was told by a DataChannelClose event; the peer's "
                   "other channel stayed open");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_GetDataChannelState) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_GetDataChannelState(pair.a.channel) == UltraRtcDataChannelState::Open);
    PROBE_EXPECT(UltraRtc_GetDataChannelState(pair.b.channel) == UltraRtcDataChannelState::Open);
    PROBE_EXPECT(UltraRtc_GetDataChannelState(UltraRtcInvalidHandle) == UltraRtcDataChannelState::Closed);

    // A channel on a peer nobody answers stays Connecting.
    UltraRtcHandle lone = UltraRtcInvalidHandle, waiting = UltraRtcInvalidHandle;
    PROBE_EXPECT(UltraRtc_CreatePeer(UltraRtcPeerConfig(), nullptr, lone));
    const bool made = static_cast<bool>(UltraRtc_CreateDataChannel(lone, "waiting", UltraRtcDataChannelInit(), waiting));
    const UltraRtcDataChannelState state = UltraRtc_GetDataChannelState(waiting);
    UltraRtc_ClosePeer(lone);
    PROBE_EXPECT(made);
    PROBE_EXPECT_MSG(state == UltraRtcDataChannelState::Connecting, "a channel nobody answered does not read Connecting");
    return Working("Connecting before the other side answers, Open on both sides of the pair, Closed for an "
                   "unknown handle");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_GetDataChannelLabel) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_GetDataChannelLabel(pair.a.channel) == "probe");
    PROBE_EXPECT_MSG(UltraRtc_GetDataChannelLabel(pair.b.channel) == "probe",
                     "the label did not reach the other side");
    PROBE_EXPECT(UltraRtc_GetDataChannelLabel(UltraRtcInvalidHandle).empty());
    return Working("\"probe\" on the side that opened the channel and on the side that received it");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_GetDataChannelPeer) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_GetDataChannelPeer(pair.a.channel) == pair.a.peer);
    PROBE_EXPECT_MSG(UltraRtc_GetDataChannelPeer(pair.b.channel) == pair.b.peer,
                     "a channel the other side opened does not name the peer it arrived on");
    PROBE_EXPECT(UltraRtc_GetDataChannelPeer(UltraRtcInvalidHandle) == UltraRtcInvalidHandle);
    return Working("each end of the channel names its own peer");
}

ULTRARTC_PROBE("DataChannels", UltraRtc_GetDataChannelBufferedAmount) {
    REQUIRE_ENGINE();
    Pair& pair = DataPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT_MSG(WaitUntil([&]() { return UltraRtc_GetDataChannelBufferedAmount(pair.a.channel) == 0; }),
                     "an idle channel does not drain to 0");
    PROBE_EXPECT(UltraRtc_GetDataChannelBufferedAmount(UltraRtcInvalidHandle) == 0);

    // 4 MiB at once: more than leaves in the instant before the next line.
    const std::vector<uint8_t> chunk(16 * 1024, 0x5A);
    size_t most = 0;
    for (int n = 0; n < 256; ++n) {
        if (!UltraRtc_DataChannelSendBinary(pair.a.channel, chunk)) break;
        most = std::max(most, UltraRtc_GetDataChannelBufferedAmount(pair.a.channel));
    }
    const bool drained = WaitUntil([&]() { return UltraRtc_GetDataChannelBufferedAmount(pair.a.channel) == 0; }, 15000);
    PROBE_EXPECT_MSG(drained, "the burst never drained");
    // The burst is not a message any other probe looks for.
    {
        std::lock_guard<std::mutex> lock(pair.mutex);
        pair.b.binaries.clear();
    }
    if (most == 0) return Implemented("0 on an idle channel; a 4 MiB burst left before it could be read");
    return Working("0 on an idle channel, up to " + std::to_string(most / 1024) +
                   " KiB queued during a 4 MiB burst, 0 again once it was sent");
}
