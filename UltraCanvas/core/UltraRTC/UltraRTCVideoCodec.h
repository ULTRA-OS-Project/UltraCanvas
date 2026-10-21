// core/UltraRTC/UltraRTCVideoCodec.h
// UltraRTC - the VP8 codec for call video (internal; FFmpeg's libavcodec
// and libswscale, linked privately)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace UltraRtcInternal {

// An RGBA image, 8 bits per channel, rows top first, no padding.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

class Vp8Encoder {
public:
    Vp8Encoder();
    ~Vp8Encoder();
    Vp8Encoder(const Vp8Encoder&) = delete;
    Vp8Encoder& operator=(const Vp8Encoder&) = delete;

    // Real-time settings: no lag, no B-frames, a key frame every few
    // seconds. Pictures of any size are scaled to width x height.
    bool Open(int width, int height, int fps, int bitrateKbps);
    bool IsOpen() const;
    // One picture (RGBA, or BGRA with `bgra`; `stride` bytes per row) ->
    // one VP8 frame. `ptsMs` must grow. `forceKey` asks for a key frame
    // (the receiver lost something).
    bool Encode(const uint8_t* pixels, int width, int height, int stride, bool bgra, int64_t ptsMs,
                bool forceKey, std::vector<uint8_t>& outFrame, bool& outKey);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Vp8Decoder {
public:
    Vp8Decoder();
    ~Vp8Decoder();
    Vp8Decoder(const Vp8Decoder&) = delete;
    Vp8Decoder& operator=(const Vp8Decoder&) = delete;

    // One VP8 frame -> the picture, as RGBA. false when nothing came out
    // (a broken frame; ask the sender for a key frame).
    bool Decode(const uint8_t* frame, size_t size, RgbaImage& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A VP8 frame's first byte says whether it is a key frame (RFC 6386 9.1).
inline bool IsVp8KeyFrame(const uint8_t* frame, size_t size) {
    return size > 0 && (frame[0] & 0x01) == 0;
}

// Whether this build can encode and decode VP8.
bool VideoCodecAvailable();

} // namespace UltraRtcInternal
