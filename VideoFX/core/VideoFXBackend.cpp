// VideoFX/core/VideoFXBackend.cpp
// Errors, backend setup, version shims, codec selection and the module-level
// VideoFX_* functions.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXBackend.h"
#include "VideoFX/VideoFX.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <mutex>

namespace VideoFX {

#ifndef VIDEOFX_VERSION_STRING
#define VIDEOFX_VERSION_STRING "0.2.0"
#endif

namespace Internal {

namespace {
    thread_local std::string lastError;
    std::once_flag initOnce;
    std::atomic<bool> verboseLogging{false};
}

VideoFXResult Fail(VideoFXResult result, const std::string& message) {
    lastError = message;
    return result;
}

VideoFXResult Fail(VideoFXResult result, const std::string& message, int averror) {
    lastError = message + ": " + AvErrorText(averror);
    return result;
}

void ClearError() { lastError.clear(); }

std::string AvErrorText(int averror) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(averror, buf, sizeof(buf));
    return buf;
}

void EnsureBackendInitialised() {
    std::call_once(initOnce, [] {
        // FFmpeg prints warnings for perfectly usable files (timestamp
        // oddities, unknown tags); errors are enough unless asked for more.
        av_log_set_level(verboseLogging.load() ? AV_LOG_INFO : AV_LOG_ERROR);
    });
}

VideoFXResult OpenInput(const std::string& path, FormatInputPtr& out) {
    EnsureBackendInitialised();
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(UltraCanvas::PathFromUtf8(path), ec))
        return Fail(VideoFXResult::FileNotFound, "File not found: " + path);

    AVFormatContext* raw = nullptr;
    // FFmpeg's file protocol takes UTF-8 on every platform (it converts to
    // UTF-16 for _wfopen on Windows), so the path goes in as it is.
    int err = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
    if (err < 0) return Fail(VideoFXResult::OpenFailed, "Cannot open " + path, err);
    FormatInputPtr fmt(raw);
    err = avformat_find_stream_info(fmt.get(), nullptr);
    if (err < 0) return Fail(VideoFXResult::OpenFailed, "Cannot read stream info of " + path, err);
    out = std::move(fmt);
    return VideoFXResult::Ok;
}

VideoFXResult OpenDecoder(AVFormatContext* fmt, int index, CodecContextPtr& out) {
    AVStream* st = fmt->streams[index];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec)
        return Fail(VideoFXResult::DecoderNotAvailable,
                    std::string("No decoder for ") + avcodec_get_name(st->codecpar->codec_id));
    CodecContextPtr ctx(avcodec_alloc_context3(dec));
    if (!ctx) return Fail(VideoFXResult::DecodeError, "Out of memory");
    int err = avcodec_parameters_to_context(ctx.get(), st->codecpar);
    if (err < 0) return Fail(VideoFXResult::DecodeError, "Bad codec parameters", err);
    ctx->pkt_timebase = st->time_base;
    ctx->thread_count = 0;
    err = avcodec_open2(ctx.get(), dec, nullptr);
    if (err < 0) return Fail(VideoFXResult::DecodeError, std::string("Cannot open decoder ") + dec->name, err);
    out = std::move(ctx);
    return VideoFXResult::Ok;
}

// ===== CHANNEL LAYOUT SHIMS =====

#if VIDEOFX_HAS_CH_LAYOUT

int GetChannels(const AVCodecParameters* par) { return par->ch_layout.nb_channels; }
int GetChannels(const AVCodecContext* ctx)    { return ctx->ch_layout.nb_channels; }
int GetChannels(const AVFrame* frame)         { return frame->ch_layout.nb_channels; }

namespace {
    std::string Describe(const AVChannelLayout& layout) {
        AVChannelLayout tmp;
        if (layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&tmp, layout.nb_channels);
        } else if (av_channel_layout_copy(&tmp, &layout) < 0) {
            return "";
        }
        char buf[128] = {0};
        av_channel_layout_describe(&tmp, buf, sizeof(buf));
        av_channel_layout_uninit(&tmp);
        return buf;
    }
}

std::string DescribeChannelLayout(const AVCodecParameters* par) { return Describe(par->ch_layout); }
std::string DescribeChannelLayout(const AVCodecContext* ctx)    { return Describe(ctx->ch_layout); }

void SetEncoderChannels(AVCodecContext* enc, int channels) {
    av_channel_layout_uninit(&enc->ch_layout);
    av_channel_layout_default(&enc->ch_layout, channels);
}

std::string AudioBufferSourceArgs(const AVFrame* frame, AVRational timeBase) {
    return "time_base=" + std::to_string(timeBase.num) + "/" + std::to_string(timeBase.den) +
           ":sample_rate=" + std::to_string(frame->sample_rate) +
           ":sample_fmt=" + av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)) +
           ":channel_layout=" + Describe(frame->ch_layout);
}

bool SetFrameChannels(AVFrame* frame, const AVCodecContext* ctx) {
    return av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout) >= 0;
}

#else   // FFmpeg < 5.1: channels + channel_layout bitmask

int GetChannels(const AVCodecParameters* par) { return par->channels; }
int GetChannels(const AVCodecContext* ctx)    { return ctx->channels; }
int GetChannels(const AVFrame* frame)         { return frame->channels; }

namespace {
    uint64_t LayoutOrDefault(uint64_t layout, int channels) {
        return layout ? layout : static_cast<uint64_t>(av_get_default_channel_layout(channels));
    }
    std::string Describe(uint64_t layout, int channels) {
        char buf[128] = {0};
        av_get_channel_layout_string(buf, sizeof(buf), channels, LayoutOrDefault(layout, channels));
        return buf;
    }
}

std::string DescribeChannelLayout(const AVCodecParameters* par) {
    return Describe(par->channel_layout, par->channels);
}
std::string DescribeChannelLayout(const AVCodecContext* ctx) {
    return Describe(ctx->channel_layout, ctx->channels);
}

void SetEncoderChannels(AVCodecContext* enc, int channels) {
    enc->channels = channels;
    enc->channel_layout = static_cast<uint64_t>(av_get_default_channel_layout(channels));
}

std::string AudioBufferSourceArgs(const AVFrame* frame, AVRational timeBase) {
    char layout[32];
    snprintf(layout, sizeof(layout), "0x%llx",
             static_cast<unsigned long long>(LayoutOrDefault(frame->channel_layout, frame->channels)));
    return "time_base=" + std::to_string(timeBase.num) + "/" + std::to_string(timeBase.den) +
           ":sample_rate=" + std::to_string(frame->sample_rate) +
           ":sample_fmt=" + av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)) +
           ":channels=" + std::to_string(frame->channels) +
           ":channel_layout=" + layout;
}

bool SetFrameChannels(AVFrame* frame, const AVCodecContext* ctx) {
    frame->channels = ctx->channels;
    frame->channel_layout = ctx->channel_layout;
    return true;
}

#endif

// ===== ROTATION =====

int GetStreamRotation(const AVStream* stream) {
    const uint8_t* matrix = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 30, 100)
    const AVPacketSideData* sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                         stream->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX);
    if (sd && sd->size >= 9 * sizeof(int32_t)) matrix = sd->data;
#endif
#if LIBAVFORMAT_VERSION_MAJOR < 61
    if (!matrix) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        matrix = av_stream_get_side_data(stream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    }
#endif
    if (!matrix) return 0;
    const double ccw = av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix));
    if (std::isnan(ccw)) return 0;
    long cw = std::lround(-ccw / 90.0) * 90;
    cw = ((cw % 360) + 360) % 360;
    return static_cast<int>(cw);
}

// ===== SUPPORTED CONFIGURATIONS =====

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
namespace {
    const void* QueryConfig(const AVCodec* codec, enum AVCodecConfig which) {
        const void* configs = nullptr;
        int count = 0;
        if (avcodec_get_supported_config(nullptr, codec, which, 0, &configs, &count) < 0) return nullptr;
        return configs;
    }
}
const enum AVPixelFormat* SupportedPixelFormats(const AVCodec* codec) {
    return static_cast<const enum AVPixelFormat*>(QueryConfig(codec, AV_CODEC_CONFIG_PIX_FORMAT));
}
const enum AVSampleFormat* SupportedSampleFormats(const AVCodec* codec) {
    return static_cast<const enum AVSampleFormat*>(QueryConfig(codec, AV_CODEC_CONFIG_SAMPLE_FORMAT));
}
const int* SupportedSampleRates(const AVCodec* codec) {
    return static_cast<const int*>(QueryConfig(codec, AV_CODEC_CONFIG_SAMPLE_RATE));
}
#else
const enum AVPixelFormat* SupportedPixelFormats(const AVCodec* codec)   { return codec->pix_fmts; }
const enum AVSampleFormat* SupportedSampleFormats(const AVCodec* codec) { return codec->sample_fmts; }
const int* SupportedSampleRates(const AVCodec* codec)                   { return codec->supported_samplerates; }
#endif

// ===== ENCODER SELECTION =====

namespace {
    const AVCodec* FirstEncoder(std::initializer_list<const char*> names) {
        for (const char* n : names) {
            if (const AVCodec* c = avcodec_find_encoder_by_name(n)) return c;
        }
        return nullptr;
    }
}

const AVCodec* FindVideoEncoder(VideoFXVideoCodec codec) {
    EnsureBackendInitialised();
    switch (codec) {
        case VideoFXVideoCodec::H264:   return FirstEncoder({"libx264", "h264_videotoolbox", "h264_mf", "libopenh264"});
        case VideoFXVideoCodec::H265:   return FirstEncoder({"libx265", "hevc_videotoolbox", "hevc_mf"});
        case VideoFXVideoCodec::VP8:    return FirstEncoder({"libvpx"});
        case VideoFXVideoCodec::VP9:    return FirstEncoder({"libvpx-vp9"});
        case VideoFXVideoCodec::AV1:    return FirstEncoder({"libsvtav1", "libaom-av1", "librav1e"});
        case VideoFXVideoCodec::MPEG4:  return FirstEncoder({"mpeg4"});
        case VideoFXVideoCodec::MJPEG:  return FirstEncoder({"mjpeg"});
        case VideoFXVideoCodec::ProRes: return FirstEncoder({"prores_ks", "prores", "prores_videotoolbox"});
        case VideoFXVideoCodec::FFV1:   return FirstEncoder({"ffv1"});
        case VideoFXVideoCodec::GIF:    return FirstEncoder({"gif"});
        default:                        return nullptr;
    }
}

const AVCodec* FindAudioEncoder(VideoFXAudioCodec codec) {
    EnsureBackendInitialised();
    switch (codec) {
        case VideoFXAudioCodec::AAC:    return FirstEncoder({"aac", "aac_at", "libfdk_aac"});
        case VideoFXAudioCodec::MP3:    return FirstEncoder({"libmp3lame", "mp3_mf"});
        case VideoFXAudioCodec::Opus:   return FirstEncoder({"libopus", "opus"});
        case VideoFXAudioCodec::Vorbis: return FirstEncoder({"libvorbis", "vorbis"});
        case VideoFXAudioCodec::FLAC:   return FirstEncoder({"flac"});
        case VideoFXAudioCodec::PCM16:  return FirstEncoder({"pcm_s16le"});
        default:                        return nullptr;
    }
}

} // namespace Internal

// ============================================================================
// MODULE FUNCTIONS
// ============================================================================

std::string VideoFX_GetVersion() { return VIDEOFX_VERSION_STRING; }

std::string VideoFX_GetBackendVersion() {
    const unsigned v = avformat_version();
    return std::string("FFmpeg ") + av_version_info() + " (libavformat " + std::to_string(AV_VERSION_MAJOR(v)) +
           "." + std::to_string(AV_VERSION_MINOR(v)) + "." + std::to_string(AV_VERSION_MICRO(v)) + ")";
}

bool VideoFX_IsAvailable() { return true; }

std::string VideoFX_GetLastError() { return Internal::lastError; }

bool VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec codec) {
    return Internal::FindVideoEncoder(codec) != nullptr;
}

bool VideoFX_IsAudioEncoderAvailable(VideoFXAudioCodec codec) {
    return Internal::FindAudioEncoder(codec) != nullptr;
}

bool VideoFX_IsTextOverlayAvailable() {
    return avfilter_get_by_name("drawtext") != nullptr;
}

void VideoFX_SetVerboseLogging(bool verbose) {
    Internal::verboseLogging = verbose;
    Internal::EnsureBackendInitialised();
    av_log_set_level(verbose ? AV_LOG_INFO : AV_LOG_ERROR);
}

const char* VideoFX_ResultToString(VideoFXResult result) {
    switch (result) {
        case VideoFXResult::Ok:                  return "Ok";
        case VideoFXResult::InvalidArgument:     return "Invalid argument";
        case VideoFXResult::FileNotFound:        return "File not found";
        case VideoFXResult::OpenFailed:          return "Cannot open media";
        case VideoFXResult::NoMediaStreams:      return "No usable stream";
        case VideoFXResult::DecoderNotAvailable: return "Decoder not available";
        case VideoFXResult::EncoderNotAvailable: return "Encoder not available";
        case VideoFXResult::UnsupportedFormat:   return "Codec not supported by container";
        case VideoFXResult::FilterError:         return "Effect error";
        case VideoFXResult::DecodeError:         return "Decode error";
        case VideoFXResult::EncodeError:         return "Encode error";
        case VideoFXResult::WriteError:          return "Write error";
        case VideoFXResult::Cancelled:           return "Cancelled";
        case VideoFXResult::NotAvailable:        return "VideoFX not available";
    }
    return "Unknown";
}

} // namespace VideoFX
