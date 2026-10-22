// core/UltraRTC/UltraRTCVp8Rtp.h
// UltraRTC - VP8 over RTP (RFC 7741) as libdatachannel media handlers
// (internal; libdatachannel ships packetizers for H.264, H.265 and AV1 only)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <rtc/rtc.hpp>

#include <cstdint>
#include <functional>

namespace UltraRtcInternal {

// Cuts each VP8 frame into packets with the payload descriptor: S on the
// first packet, a 15-bit picture id growing by one per frame.
class Vp8RtpPacketizer final : public rtc::RtpPacketizer {
public:
    explicit Vp8RtpPacketizer(std::shared_ptr<rtc::RtpPacketizationConfig> config,
                              size_t maxFragmentSize = rtc::RtpPacketizer::DefaultMaxFragmentSize);

protected:
    std::vector<rtc::binary> fragment(rtc::binary data) override;

private:
    size_t   maxFragmentSize_;
    uint16_t pictureId_;
};

// Puts the packets of each frame back together. A frame with a packet
// missing - or a picture id that skipped one - is dropped and reported
// through onLoss, so the receiver can ask for a key frame.
class Vp8RtpDepacketizer final : public rtc::VideoRtpDepacketizer {
public:
    explicit Vp8RtpDepacketizer(std::function<void()> onLoss);

protected:
    rtc::message_ptr reassemble(message_buffer& buffer) override;

private:
    std::function<void()> onLoss_;
    int lastPictureId_ = -1;
};

} // namespace UltraRtcInternal
