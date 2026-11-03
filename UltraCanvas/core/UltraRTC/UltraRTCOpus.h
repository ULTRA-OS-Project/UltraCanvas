// core/UltraRTC/UltraRTCOpus.h
// UltraRTC - the Opus codec for call audio (internal; libopus, linked privately)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace UltraRtcInternal {

// Calls run on one clock and one frame size: 48 kHz mono, 20 ms frames.
constexpr int kOpusSampleRate = 48000;
constexpr int kOpusFrameSamples = 960;

class OpusEncoder {
public:
    OpusEncoder();
    ~OpusEncoder();
    OpusEncoder(const OpusEncoder&) = delete;
    OpusEncoder& operator=(const OpusEncoder&) = delete;

    bool IsValid() const { return state_ != nullptr; }
    // One 20 ms frame of kOpusFrameSamples mono samples -> one packet.
    bool Encode(const int16_t* frame, std::vector<uint8_t>& outPacket);

private:
    void* state_ = nullptr;
};

class OpusDecoder {
public:
    OpusDecoder();
    ~OpusDecoder();
    OpusDecoder(const OpusDecoder&) = delete;
    OpusDecoder& operator=(const OpusDecoder&) = delete;

    bool IsValid() const { return state_ != nullptr; }
    // A packet -> PCM (appended). An empty packet asks for concealment of
    // one lost frame; `fec` decodes the frame before `packet` from its
    // in-band redundancy instead.
    bool Decode(const uint8_t* packet, size_t size, bool fec, std::vector<int16_t>& outPcm);

private:
    void* state_ = nullptr;
};

bool OpusAvailable();

} // namespace UltraRtcInternal
