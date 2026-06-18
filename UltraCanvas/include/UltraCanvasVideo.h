// include/UltraCanvasVideo.h
// Base types for cross-platform video resources (decoded frames, format metadata)
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEO_H
#define ULTRACANVASVIDEO_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== CONTAINER FORMAT =====
enum class VideoContainer {
    Autodetect,
    MP4,        // .mp4 / .m4v  (H.264/H.265 + AAC)
    MOV,        // QuickTime
    MKV,        // Matroska
    WebM,       // VP8/VP9/AV1 + Opus/Vorbis
    AVI,
    Unknown
};

// ===== VIDEO CODEC =====
enum class VideoCodec {
    Autodetect,
    H264,       // AVC
    H265,       // HEVC
    VP8,
    VP9,
    AV1,
    MJPEG,
    Unknown
};

// ===== DECODED FRAME PIXEL FORMAT =====
// Frames handed to the UI layer are always packed 8-bit; the backend converts
// from its native (often planar YUV) format before delivery so the renderer
// only ever deals with one layout.
enum class VideoPixelFormat {
    RGBA32,     // 0xAABBGGRR little-endian word == Cairo ARGB32 premultiplied-compatible
    BGRA32      // Some capture paths deliver BGRA; recorded here for the converter
};

// ===== FRAME METADATA =====
struct VideoFrameInfo {
    int    width  = 0;
    int    height = 0;
    VideoPixelFormat pixelFormat = VideoPixelFormat::RGBA32;
    int    stride = 0;            // bytes per row (>= width*4); 0 = tightly packed
    double pts    = 0.0;          // presentation timestamp in seconds
    uint64_t frameIndex = 0;      // monotonic counter since stream start
};

// ===== STREAM-LEVEL METADATA =====
struct VideoStreamInfo {
    int    width  = 0;
    int    height = 0;
    double frameRate = 0.0;       // nominal fps
    double duration  = 0.0;       // seconds (0 for live capture)
    VideoCodec   codec     = VideoCodec::Unknown;
    VideoContainer container = VideoContainer::Unknown;
    bool   hasAudio = false;
    int    bitRate  = 0;          // bits/sec (0 if unknown)
};

// ===== UCVIDEOFRAME RESOURCE =====
// A single decoded RGBA frame plus its metadata. Parallels UCAudio / UCImage.
// Cheap to move; the engine recycles these between the decode thread and the
// UI thread behind a mutex.
class UCVideoFrame {
public:
    UCVideoFrame() = default;

    static std::shared_ptr<UCVideoFrame> FromRGBA(const uint8_t* pixels,
                                                  int width, int height,
                                                  int stride = 0,
                                                  double pts = 0.0);

    bool IsValid() const { return valid && !data.empty() && info.width > 0; }
    const VideoFrameInfo& GetInfo() const { return info; }
    int  GetWidth()  const { return info.width; }
    int  GetHeight() const { return info.height; }
    double GetPTS()  const { return info.pts; }

    // Packed pixel access. Row stride is GetStride(); rows are top-to-bottom.
    const uint8_t* GetData() const { return data.data(); }
    size_t GetDataSize() const { return data.size(); }
    int  GetStride() const { return info.stride; }

    // Mutable population path (used by backends decoding straight into a frame).
    std::vector<uint8_t>& MutableData() { return data; }
    void SetInfo(const VideoFrameInfo& i) { info = i; valid = true; }

private:
    VideoFrameInfo info;
    std::vector<uint8_t> data;   // RGBA32, info.stride * info.height bytes
    bool valid = false;
};

using UCVideoFramePtr = std::shared_ptr<UCVideoFrame>;

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEO_H
