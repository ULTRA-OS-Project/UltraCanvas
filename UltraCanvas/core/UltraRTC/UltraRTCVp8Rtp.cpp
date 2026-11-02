// core/UltraRTC/UltraRTCVp8Rtp.cpp
// UltraRTC - VP8 over RTP (RFC 7741)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTCVp8Rtp.h"

#include <algorithm>
#include <random>

namespace UltraRtcInternal {

namespace {

// The payload descriptor we write: X | S, then I, then a 15-bit picture id.
constexpr size_t kDescriptorSize = 4;
constexpr uint8_t kExtended = 0x80;     // X
constexpr uint8_t kStart = 0x10;        // S
constexpr uint8_t kHasPictureId = 0x80; // I
constexpr uint8_t kHasTl0 = 0x40;       // L
constexpr uint8_t kHasTid = 0x20;       // T
constexpr uint8_t kHasKeyIdx = 0x10;    // K

uint8_t At(const rtc::binary& b, size_t i) {
    return std::to_integer<uint8_t>(b[i]);
}

} // namespace

Vp8RtpPacketizer::Vp8RtpPacketizer(std::shared_ptr<rtc::RtpPacketizationConfig> config, size_t maxFragmentSize)
    : rtc::RtpPacketizer(std::move(config)), maxFragmentSize_(maxFragmentSize) {
    std::mt19937 random(std::random_device{}());
    pictureId_ = static_cast<uint16_t>(random() & 0x7FFF);
}

std::vector<rtc::binary> Vp8RtpPacketizer::fragment(rtc::binary data) {
    std::vector<rtc::binary> payloads;
    if (data.empty()) return payloads;
    const size_t room = maxFragmentSize_ > kDescriptorSize + 1 ? maxFragmentSize_ - kDescriptorSize : 1;
    const uint16_t pictureId = pictureId_;
    pictureId_ = static_cast<uint16_t>((pictureId_ + 1) & 0x7FFF);
    for (size_t at = 0; at < data.size(); at += room) {
        const size_t n = std::min(room, data.size() - at);
        rtc::binary payload(kDescriptorSize + n);
        payload[0] = std::byte{static_cast<uint8_t>(kExtended | (at == 0 ? kStart : 0))};
        payload[1] = std::byte{kHasPictureId};
        payload[2] = std::byte{static_cast<uint8_t>(0x80 | (pictureId >> 8))};   // M: 15-bit id
        payload[3] = std::byte{static_cast<uint8_t>(pictureId & 0xFF)};
        std::copy(data.begin() + static_cast<std::ptrdiff_t>(at),
                  data.begin() + static_cast<std::ptrdiff_t>(at + n), payload.begin() + kDescriptorSize);
        payloads.push_back(std::move(payload));
    }
    return payloads;
}

Vp8RtpDepacketizer::Vp8RtpDepacketizer(std::function<void()> onLoss) : onLoss_(std::move(onLoss)) {}

rtc::message_ptr Vp8RtpDepacketizer::reassemble(message_buffer& buffer) {
    if (buffer.empty()) return nullptr;
    auto lost = [this]() {
        if (onLoss_) onLoss_();
        return rtc::message_ptr();
    };

    const auto* firstHeader = reinterpret_cast<const rtc::RtpHeader*>((*buffer.begin())->data());
    const uint32_t timestamp = firstHeader->timestamp();
    const uint8_t payloadType = firstHeader->payloadType();
    uint16_t expected = firstHeader->seqNumber();
    bool lastHadMarker = false;
    int pictureId = -1;

    rtc::binary frame;
    bool first = true;
    for (const auto& packet : buffer) {
        const auto* header = reinterpret_cast<const rtc::RtpHeader*>(packet->data());
        if (header->seqNumber() != expected) return lost();      // a packet of this frame is missing
        expected = static_cast<uint16_t>(expected + 1);
        lastHadMarker = header->marker() != 0;

        size_t at = header->getSize() + header->getExtensionHeaderSize();
        size_t end = packet->size();
        if (header->padding() && end > at) end -= std::min<size_t>(end - at, At(*packet, end - 1));
        if (end <= at) return lost();

        // The payload descriptor.
        const uint8_t b0 = At(*packet, at++);
        if (first && !(b0 & kStart)) return lost();               // the frame's start is missing
        if (b0 & kExtended) {
            if (at >= end) return lost();
            const uint8_t ext = At(*packet, at++);
            if (ext & kHasPictureId) {
                if (at >= end) return lost();
                const uint8_t p0 = At(*packet, at++);
                if (p0 & 0x80) {
                    if (at >= end) return lost();
                    pictureId = ((p0 & 0x7F) << 8) | At(*packet, at++);
                } else {
                    pictureId = p0 & 0x7F;
                }
            }
            if (ext & kHasTl0) ++at;
            if (ext & (kHasTid | kHasKeyIdx)) ++at;
        }
        if (at > end) return lost();
        frame.insert(frame.end(), packet->begin() + static_cast<std::ptrdiff_t>(at),
                     packet->begin() + static_cast<std::ptrdiff_t>(end));
        first = false;
    }
    if (!lastHadMarker) return lost();                            // the frame's end is missing

    // A whole frame between this one and the last one went missing.
    if (pictureId >= 0 && lastPictureId_ >= 0) {
        const int step = (pictureId - lastPictureId_) & 0x7FFF;
        if (step != 1) {
            lastPictureId_ = pictureId;
            if (onLoss_) onLoss_();
            // Still delivered: a key frame needs nothing before it, and the
            // receiver decides what to do with the rest.
        }
    }
    lastPictureId_ = pictureId;
    return rtc::make_message(std::move(frame), createFrameInfo(timestamp, payloadType));
}

} // namespace UltraRtcInternal
