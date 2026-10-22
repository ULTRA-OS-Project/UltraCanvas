// core/UltraRTC/UltraRTCOpus.cpp
// UltraRTC - the Opus codec for call audio
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTCOpus.h"

#if defined(ULTRARTC_HAVE_OPUS)
#include <opus.h>
#endif

namespace UltraRtcInternal {

#if defined(ULTRARTC_HAVE_OPUS)

bool OpusAvailable() {
    return true;
}

OpusEncoder::OpusEncoder() {
    int error = OPUS_OK;
    ::OpusEncoder* encoder = opus_encoder_create(kOpusSampleRate, 1, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || !encoder) return;
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));          // a lost packet is rebuilt from the next
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    state_ = encoder;
}

OpusEncoder::~OpusEncoder() {
    if (state_) opus_encoder_destroy(static_cast<::OpusEncoder*>(state_));
}

bool OpusEncoder::Encode(const int16_t* frame, std::vector<uint8_t>& outPacket) {
    if (!state_) return false;
    outPacket.resize(1276);     // the largest packet Opus produces
    const opus_int32 n = opus_encode(static_cast<::OpusEncoder*>(state_), frame, kOpusFrameSamples,
                                     outPacket.data(), static_cast<opus_int32>(outPacket.size()));
    if (n < 0) return false;
    outPacket.resize(static_cast<size_t>(n));
    return true;
}

OpusDecoder::OpusDecoder() {
    int error = OPUS_OK;
    ::OpusDecoder* decoder = opus_decoder_create(kOpusSampleRate, 1, &error);
    if (error == OPUS_OK) state_ = decoder;
}

OpusDecoder::~OpusDecoder() {
    if (state_) opus_decoder_destroy(static_cast<::OpusDecoder*>(state_));
}

bool OpusDecoder::Decode(const uint8_t* packet, size_t size, bool fec, std::vector<int16_t>& outPcm) {
    if (!state_) return false;
    const size_t start = outPcm.size();
    outPcm.resize(start + 5760);    // 120 ms, the longest an Opus packet can carry
    const int n = opus_decode(static_cast<::OpusDecoder*>(state_), size ? packet : nullptr,
                              static_cast<opus_int32>(size), outPcm.data() + start,
                              fec ? kOpusFrameSamples : 5760, fec ? 1 : 0);
    if (n < 0) {
        outPcm.resize(start);
        return false;
    }
    outPcm.resize(start + static_cast<size_t>(n));
    return true;
}

#else

bool OpusAvailable() { return false; }
OpusEncoder::OpusEncoder() = default;
OpusEncoder::~OpusEncoder() = default;
bool OpusEncoder::Encode(const int16_t*, std::vector<uint8_t>&) { return false; }
OpusDecoder::OpusDecoder() = default;
OpusDecoder::~OpusDecoder() = default;
bool OpusDecoder::Decode(const uint8_t*, size_t, bool, std::vector<int16_t>&) { return false; }

#endif

} // namespace UltraRtcInternal
