// VideoFX/core/VideoFXBackend.h
// Internal: FFmpeg includes, owning wrappers and version shims shared by the
// VideoFX sources. Never included from a public header.
//
// Supported FFmpeg range: 4.4 (Ubuntu 22.04, libavcodec 58) to 8.x. The
// channel-layout API (AVChannelLayout, FFmpeg 5.1), the stream side-data move
// (codecpar->coded_side_data, 6.1) and the supported-config query (7.1) are
// wrapped here so the pipeline code does not care which one it runs on.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#define VIDEOFX_HAS_CH_LAYOUT 1
#else
#define VIDEOFX_HAS_CH_LAYOUT 0
#endif

namespace VideoFX {
namespace Internal {

// ===== ERRORS =====
// Sets the calling thread's last error and returns `result`, so a failure is
// one line: `return Fail(VideoFXResult::OpenFailed, "Cannot open " + path);`
VideoFXResult Fail(VideoFXResult result, const std::string& message);
VideoFXResult Fail(VideoFXResult result, const std::string& message, int averror);
void ClearError();
std::string AvErrorText(int averror);

// One-time backend setup (log level, network init)
void EnsureBackendInitialised();

// ===== OWNING WRAPPERS =====
struct FormatInputDeleter  { void operator()(AVFormatContext* c) const { avformat_close_input(&c); } };
struct CodecContextDeleter { void operator()(AVCodecContext* c) const { avcodec_free_context(&c); } };
struct FrameDeleter        { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct PacketDeleter       { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct FilterGraphDeleter  { void operator()(AVFilterGraph* g) const { avfilter_graph_free(&g); } };
struct SwsDeleter          { void operator()(SwsContext* s) const { sws_freeContext(s); } };
struct AudioFifoDeleter    { void operator()(AVAudioFifo* f) const { av_audio_fifo_free(f); } };

using FormatInputPtr  = std::unique_ptr<AVFormatContext, FormatInputDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr        = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr       = std::unique_ptr<AVPacket, PacketDeleter>;
using FilterGraphPtr  = std::unique_ptr<AVFilterGraph, FilterGraphDeleter>;
using SwsPtr          = std::unique_ptr<SwsContext, SwsDeleter>;
using AudioFifoPtr    = std::unique_ptr<AVAudioFifo, AudioFifoDeleter>;

inline FramePtr MakeFrame()   { return FramePtr(av_frame_alloc()); }
inline PacketPtr MakePacket() { return PacketPtr(av_packet_alloc()); }

// Open a media file for reading (UTF-8 path) and read its stream info
VideoFXResult OpenInput(const std::string& path, FormatInputPtr& out);

// Open a decoder for stream `index`
VideoFXResult OpenDecoder(AVFormatContext* fmt, int index, CodecContextPtr& out);

// ===== VERSION SHIMS =====
int GetChannels(const AVCodecParameters* par);
int GetChannels(const AVCodecContext* ctx);
int GetChannels(const AVFrame* frame);
std::string DescribeChannelLayout(const AVCodecParameters* par);
std::string DescribeChannelLayout(const AVCodecContext* ctx);
// Default layout for `channels` on the encoder
void SetEncoderChannels(AVCodecContext* enc, int channels);
// "abuffer" source arguments for frames like `frame` (its rate, format and
// layout - taken from a decoded frame, not the decoder, because HE-AAC and a
// few others only settle those once the first frame is out)
std::string AudioBufferSourceArgs(const AVFrame* frame, AVRational timeBase);
// Copy the channel layout of `ctx` to a freshly allocated audio frame
bool SetFrameChannels(AVFrame* frame, const AVCodecContext* ctx);

// Display rotation of a video stream, clockwise degrees 0/90/180/270
int GetStreamRotation(const AVStream* stream);

// Rotation a single decoded frame asks for - a photo's EXIF orientation:
// display-matrix side data (FFmpeg 6.1+) or the "Orientation" tag the JPEG
// decoder puts in the frame metadata (older). -1 = the frame says nothing.
int GetFrameRotation(const AVFrame* frame);

// Formats / rates an encoder accepts; AV_PIX_FMT_NONE / AV_SAMPLE_FMT_NONE /
// 0 terminated, or nullptr when it accepts anything.
const enum AVPixelFormat* SupportedPixelFormats(const AVCodec* codec);
const enum AVSampleFormat* SupportedSampleFormats(const AVCodec* codec);
const int* SupportedSampleRates(const AVCodec* codec);

// ===== FONTS (VideoFXFonts.cpp) =====
// Font file for text overlays with no fontPath; "" = none found
std::string ResolveDefaultFont();
// Whether drawtext can load fontconfig's "Sans" (the last resort); probed once
bool FontconfigCanDrawText();

// ===== CODEC SELECTION =====
// Encoder for a codec choice, trying the usual implementations in order
// (libx264, then platform encoders, ...). nullptr = none in this build.
const AVCodec* FindVideoEncoder(VideoFXVideoCodec codec);
const AVCodec* FindAudioEncoder(VideoFXAudioCodec codec);

} // namespace Internal
} // namespace VideoFX
