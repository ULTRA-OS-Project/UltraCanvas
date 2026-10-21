// core/UltraRTC/UltraRTCVideoCodec.cpp
// UltraRTC - the VP8 codec for call video, on FFmpeg
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTCVideoCodec.h"

#if defined(ULTRARTC_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}
#endif

#include <cstring>

namespace UltraRtcInternal {

#if defined(ULTRARTC_HAVE_FFMPEG)

namespace {

// FFmpeg reports at "info" by default ("[libvpx] v1.14.0" per encoder);
// a call wants its errors only.
void QuietLogs() {
    static const bool once = []() {
        if (av_log_get_level() > AV_LOG_ERROR) av_log_set_level(AV_LOG_ERROR);
        return true;
    }();
    (void)once;
}

const AVCodec* FindVp8Encoder() {
    // libvpx is the VP8 encoder FFmpeg has; a hardware one would do as well.
    if (const AVCodec* codec = avcodec_find_encoder_by_name("libvpx")) return codec;
    return avcodec_find_encoder(AV_CODEC_ID_VP8);
}

// Converts `src` (any size, either format) into `dst` (its own size and
// format), reusing the scaler while the sizes stay the same.
bool Convert(SwsContext*& sws, const uint8_t* const src[4], const int srcStride[4], int srcW, int srcH,
             AVPixelFormat srcFormat, uint8_t* const dst[4], const int dstStride[4], int dstW, int dstH,
             AVPixelFormat dstFormat) {
    sws = sws_getCachedContext(sws, srcW, srcH, srcFormat, dstW, dstH, dstFormat, SWS_BILINEAR,
                               nullptr, nullptr, nullptr);
    if (!sws) return false;
    return sws_scale(sws, src, srcStride, 0, srcH, dst, dstStride) > 0;
}

} // namespace

bool VideoCodecAvailable() {
    return FindVp8Encoder() != nullptr && avcodec_find_decoder(AV_CODEC_ID_VP8) != nullptr;
}

// ---- encoder ----------------------------------------------------------------

struct Vp8Encoder::Impl {
    AVCodecContext* context = nullptr;
    AVFrame* picture = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    int64_t lastPts = -1;

    ~Impl() {
        if (scaler) sws_freeContext(scaler);
        if (packet) av_packet_free(&packet);
        if (picture) av_frame_free(&picture);
        if (context) avcodec_free_context(&context);
    }
};

Vp8Encoder::Vp8Encoder() = default;
Vp8Encoder::~Vp8Encoder() = default;

bool Vp8Encoder::IsOpen() const {
    return impl_ != nullptr;
}

bool Vp8Encoder::Open(int width, int height, int fps, int bitrateKbps) {
    impl_.reset();
    QuietLogs();
    const AVCodec* codec = FindVp8Encoder();
    if (!codec || width <= 0 || height <= 0 || fps <= 0) return false;
    auto impl = std::make_unique<Impl>();
    impl->context = avcodec_alloc_context3(codec);
    if (!impl->context) return false;
    AVCodecContext* c = impl->context;
    c->width = width & ~1;          // 4:2:0 wants even sizes
    c->height = height & ~1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->time_base = AVRational{1, 1000};
    c->framerate = AVRational{fps, 1};
    c->bit_rate = static_cast<int64_t>(bitrateKbps) * 1000;
    c->gop_size = fps * 3;          // a key frame at least every 3 s
    c->max_b_frames = 0;
    c->qmin = 4;
    c->qmax = 56;
    c->thread_count = 2;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "deadline", "realtime", 0);
    av_dict_set(&options, "cpu-used", "8", 0);          // fastest real-time speed
    av_dict_set(&options, "lag-in-frames", "0", 0);     // a frame out for every frame in
    av_dict_set(&options, "auto-alt-ref", "0", 0);
    av_dict_set(&options, "error-resilient", "default", 0);
    const int opened = avcodec_open2(c, codec, &options);
    av_dict_free(&options);
    if (opened < 0) return false;

    impl->picture = av_frame_alloc();
    impl->packet = av_packet_alloc();
    if (!impl->picture || !impl->packet) return false;
    impl->picture->format = c->pix_fmt;
    impl->picture->width = c->width;
    impl->picture->height = c->height;
    if (av_frame_get_buffer(impl->picture, 0) < 0) return false;
    impl_ = std::move(impl);
    return true;
}

bool Vp8Encoder::Encode(const uint8_t* pixels, int width, int height, int stride, bool bgra, int64_t ptsMs,
                        bool forceKey, std::vector<uint8_t>& outFrame, bool& outKey) {
    outFrame.clear();
    outKey = false;
    if (!impl_ || !pixels || width <= 1 || height <= 1) return false;
    if (stride <= 0) stride = width * 4;
    AVCodecContext* c = impl_->context;
    AVFrame* picture = impl_->picture;
    if (av_frame_make_writable(picture) < 0) return false;
    const uint8_t* src[4] = {pixels, nullptr, nullptr, nullptr};
    const int srcStride[4] = {stride, 0, 0, 0};
    if (!Convert(impl_->scaler, src, srcStride, width, height, bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA,
                 picture->data, picture->linesize, c->width, c->height, AV_PIX_FMT_YUV420P))
        return false;

    if (ptsMs <= impl_->lastPts) ptsMs = impl_->lastPts + 1;
    impl_->lastPts = ptsMs;
    picture->pts = ptsMs;
    picture->pict_type = forceKey ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#if defined(AV_FRAME_FLAG_KEY)
    if (forceKey) picture->flags |= AV_FRAME_FLAG_KEY;
    else picture->flags &= ~AV_FRAME_FLAG_KEY;
#endif
    if (avcodec_send_frame(c, picture) < 0) return false;
    while (avcodec_receive_packet(c, impl_->packet) == 0) {
        AVPacket* p = impl_->packet;
        outFrame.insert(outFrame.end(), p->data, p->data + p->size);
        if (p->flags & AV_PKT_FLAG_KEY) outKey = true;
        av_packet_unref(p);
    }
    return !outFrame.empty();
}

// ---- decoder ----------------------------------------------------------------

struct Vp8Decoder::Impl {
    AVCodecContext* context = nullptr;
    AVFrame* picture = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;

    ~Impl() {
        if (scaler) sws_freeContext(scaler);
        if (packet) av_packet_free(&packet);
        if (picture) av_frame_free(&picture);
        if (context) avcodec_free_context(&context);
    }
};

Vp8Decoder::Vp8Decoder() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_VP8);
    if (!codec) return;
    auto impl = std::make_unique<Impl>();
    impl->context = avcodec_alloc_context3(codec);
    if (!impl->context) return;
    impl->context->thread_count = 1;     // frame threads would add a frame of delay
    if (avcodec_open2(impl->context, codec, nullptr) < 0) return;
    impl->picture = av_frame_alloc();
    impl->packet = av_packet_alloc();
    if (!impl->picture || !impl->packet) return;
    impl_ = std::move(impl);
}

Vp8Decoder::~Vp8Decoder() = default;

bool Vp8Decoder::Decode(const uint8_t* frame, size_t size, RgbaImage& out) {
    if (!impl_ || !frame || size == 0) return false;
    AVPacket* packet = impl_->packet;
    if (av_new_packet(packet, static_cast<int>(size)) < 0) return false;   // zero-padded as FFmpeg wants
    std::memcpy(packet->data, frame, size);
    const int sent = avcodec_send_packet(impl_->context, packet);
    av_packet_unref(packet);
    if (sent < 0) return false;
    bool got = false;
    AVFrame* picture = impl_->picture;
    while (avcodec_receive_frame(impl_->context, picture) == 0) {
        if (picture->width > 0 && picture->height > 0) {
            out.width = picture->width;
            out.height = picture->height;
            out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4);
            uint8_t* dst[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
            const int dstStride[4] = {out.width * 4, 0, 0, 0};
            got = Convert(impl_->scaler, picture->data, picture->linesize, picture->width, picture->height,
                          static_cast<AVPixelFormat>(picture->format), dst, dstStride, out.width, out.height,
                          AV_PIX_FMT_RGBA);
        }
        av_frame_unref(picture);
    }
    return got;
}

#else // no FFmpeg: no video

bool VideoCodecAvailable() {
    return false;
}

struct Vp8Encoder::Impl {};
Vp8Encoder::Vp8Encoder() = default;
Vp8Encoder::~Vp8Encoder() = default;
bool Vp8Encoder::Open(int, int, int, int) { return false; }
bool Vp8Encoder::IsOpen() const { return false; }
bool Vp8Encoder::Encode(const uint8_t*, int, int, int, bool, int64_t, bool, std::vector<uint8_t>& outFrame,
                        bool& outKey) {
    outFrame.clear();
    outKey = false;
    return false;
}

struct Vp8Decoder::Impl {};
Vp8Decoder::Vp8Decoder() = default;
Vp8Decoder::~Vp8Decoder() = default;
bool Vp8Decoder::Decode(const uint8_t*, size_t, RgbaImage&) { return false; }

#endif

} // namespace UltraRtcInternal
