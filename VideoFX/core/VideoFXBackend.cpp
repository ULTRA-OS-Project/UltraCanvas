// VideoFX/core/VideoFXBackend.cpp
// Errors, backend setup, version shims, codec selection and the module-level
// VideoFX_* functions.
// Version: 0.12.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "VideoFXBackend.h"
#include "VideoFX/VideoFX.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
#include <mutex>

namespace VideoFX {

// The version has one home, project(VideoFX VERSION ...) in VideoFX/CMakeLists.txt,
// which passes it in; no copy of the number lives in the code.
#ifndef VIDEOFX_VERSION_STRING
#error "VIDEOFX_VERSION_STRING is set by VideoFX/CMakeLists.txt - build VideoFX through CMake"
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

int GetFrameRotation(const AVFrame* frame) {
    if (const AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
        if (sd->size > 0 && static_cast<size_t>(sd->size) >= 9 * sizeof(int32_t)) {   // int in FFmpeg 4.x
            const double ccw = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
            if (!std::isnan(ccw)) {
                long cw = std::lround(-ccw / 90.0) * 90;
                return static_cast<int>(((cw % 360) + 360) % 360);
            }
        }
    }
    if (const AVDictionaryEntry* e = av_dict_get(frame->metadata, "Orientation", nullptr, 0)) {
        switch (std::atoi(e->value)) {        // EXIF values; the mirrored ones (2, 4, 5, 7) are left as stored
            case 3: return 180;
            case 6: return 90;
            case 8: return 270;
            default: return 0;
        }
    }
    return -1;
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

    // The automatic order of 0.11 and before: software, then the platform's
    // own encoder (never probed: VideoToolbox and Media Foundation are the
    // system's, and present wherever FFmpeg built them)
    const AVCodec* AutoEncoder(VideoFXVideoCodec codec) {
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

    bool IsHardwareFormat(AVPixelFormat f) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
        return d && (d->flags & AV_PIX_FMT_FLAG_HWACCEL);
    }

    // One small frame through the encoder: does it run on this machine?
    bool TryEncoder(const AVCodec* codec) {
        CodecContextPtr ctx(avcodec_alloc_context3(codec));
        if (!ctx) return false;
        AVCodecContext* c = ctx.get();
        c->width = 320;                         // above every encoder's minimum (NVENC: 145)
        c->height = 240;
        c->time_base = AVRational{1, 25};
        c->framerate = AVRational{25, 1};
        c->bit_rate = 1000000;
        c->gop_size = 25;
        c->max_b_frames = 0;

        AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
        BufferRefPtr device, frames;
        AVPixelFormat swFormat = AV_PIX_FMT_NV12;
        if (NeedsHardwareFrames(codec, &hwFormat)) {
            if (SetUpHardwareFrames(c, hwFormat, device, frames) < 0) return false;
            c->pix_fmt = hwFormat;
        } else {
            const AVPixelFormat* fmts = SupportedPixelFormats(codec);
            swFormat = AV_PIX_FMT_NONE;
            for (const AVPixelFormat* p = fmts; p && *p != AV_PIX_FMT_NONE; ++p) {
                if (*p == AV_PIX_FMT_NV12 || *p == AV_PIX_FMT_YUV420P) { swFormat = *p; break; }
            }
            for (const AVPixelFormat* p = fmts; swFormat == AV_PIX_FMT_NONE && p && *p != AV_PIX_FMT_NONE; ++p) {
                if (!IsHardwareFormat(*p)) swFormat = *p;
            }
            if (swFormat == AV_PIX_FMT_NONE) swFormat = AV_PIX_FMT_YUV420P;
            c->pix_fmt = swFormat;
        }
        // FFmpeg's own log for a missing device is noise here: this is a question
        const int level = av_log_get_level();
        av_log_set_level(AV_LOG_QUIET);
        bool ok = avcodec_open2(c, codec, nullptr) >= 0;
        if (ok) {
            FramePtr frame(av_frame_alloc());
            FramePtr hw(av_frame_alloc());
            PacketPtr pkt(av_packet_alloc());
            ok = frame && hw && pkt;
            if (ok) {
                frame->format = swFormat;
                frame->width = c->width;
                frame->height = c->height;
                ok = av_frame_get_buffer(frame.get(), 0) >= 0;
            }
            if (ok) {
                ptrdiff_t lines[4];
                for (int i = 0; i < 4; ++i) lines[i] = frame->linesize[i];
                av_image_fill_black(frame->data, lines, swFormat, AVCOL_RANGE_MPEG, c->width, c->height);
                frame->pts = 0;
                AVFrame* send = frame.get();
                if (frames) {
                    ok = av_hwframe_get_buffer(frames.get(), hw.get(), 0) >= 0 &&
                         av_hwframe_transfer_data(hw.get(), frame.get(), 0) >= 0;
                    hw->pts = 0;
                    send = hw.get();
                }
                ok = ok && avcodec_send_frame(c, send) >= 0 && avcodec_send_frame(c, nullptr) >= 0;
                bool gotPacket = false;
                while (ok) {
                    const int err = avcodec_receive_packet(c, pkt.get());
                    if (err == AVERROR_EOF) break;
                    if (err < 0) { ok = false; break; }
                    gotPacket = true;
                    av_packet_unref(pkt.get());
                }
                ok = ok && gotPacket;
            }
        }
        av_log_set_level(level);
        return ok;
    }

    std::mutex workingMutex;
    std::map<std::string, bool> workingEncoders;     // name -> works here, tried once
}

std::vector<EncoderCandidate> VideoEncoderCandidates(VideoFXVideoCodec codec) {
    using A = VideoFXAcceleration;
    switch (codec) {
        case VideoFXVideoCodec::H264:
            return {{"libx264", A::Software}, {"libopenh264", A::Software}, {"h264_nvenc", A::NVENC},
                    {"h264_qsv", A::QuickSync}, {"h264_amf", A::AMF}, {"h264_vaapi", A::VAAPI},
                    {"h264_videotoolbox", A::VideoToolbox}, {"h264_mf", A::MediaFoundation}};
        case VideoFXVideoCodec::H265:
            return {{"libx265", A::Software}, {"hevc_nvenc", A::NVENC}, {"hevc_qsv", A::QuickSync},
                    {"hevc_amf", A::AMF}, {"hevc_vaapi", A::VAAPI}, {"hevc_videotoolbox", A::VideoToolbox},
                    {"hevc_mf", A::MediaFoundation}};
        case VideoFXVideoCodec::AV1:
            return {{"libsvtav1", A::Software}, {"libaom-av1", A::Software}, {"librav1e", A::Software},
                    {"av1_nvenc", A::NVENC}, {"av1_qsv", A::QuickSync}, {"av1_amf", A::AMF},
                    {"av1_vaapi", A::VAAPI}, {"av1_mf", A::MediaFoundation}};
        case VideoFXVideoCodec::VP9:
            return {{"libvpx-vp9", A::Software}, {"vp9_qsv", A::QuickSync}, {"vp9_vaapi", A::VAAPI}};
        case VideoFXVideoCodec::VP8:    return {{"libvpx", A::Software}, {"vp8_vaapi", A::VAAPI}};
        case VideoFXVideoCodec::MPEG4:  return {{"mpeg4", A::Software}};
        case VideoFXVideoCodec::MJPEG:
            return {{"mjpeg", A::Software}, {"mjpeg_qsv", A::QuickSync}, {"mjpeg_vaapi", A::VAAPI}};
        case VideoFXVideoCodec::ProRes:
            return {{"prores_ks", A::Software}, {"prores", A::Software}, {"prores_videotoolbox", A::VideoToolbox}};
        case VideoFXVideoCodec::FFV1:   return {{"ffv1", A::Software}};
        case VideoFXVideoCodec::GIF:    return {{"gif", A::Software}};
        default:                        return {};
    }
}

VideoFXAcceleration AccelerationOfEncoder(const std::string& name) {
    auto ends = [&](const char* tail) {
        const size_t n = std::char_traits<char>::length(tail);
        return name.size() >= n && name.compare(name.size() - n, n, tail) == 0;
    };
    if (ends("_nvenc")) return VideoFXAcceleration::NVENC;
    if (ends("_qsv")) return VideoFXAcceleration::QuickSync;
    if (ends("_vaapi")) return VideoFXAcceleration::VAAPI;
    if (ends("_amf")) return VideoFXAcceleration::AMF;
    if (ends("_videotoolbox")) return VideoFXAcceleration::VideoToolbox;
    if (ends("_mf")) return VideoFXAcceleration::MediaFoundation;
    return VideoFXAcceleration::Software;
}

bool NeedsHardwareFrames(const AVCodec* codec, AVPixelFormat* hwFormat) {
    const AVPixelFormat* fmts = SupportedPixelFormats(codec);
    if (!fmts) return false;
    AVPixelFormat hw = AV_PIX_FMT_NONE;
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (!IsHardwareFormat(*p)) return false;        // takes system memory: it uploads itself
        if (hw == AV_PIX_FMT_NONE) hw = *p;
    }
    if (hw == AV_PIX_FMT_NONE) return false;
    if (hwFormat) *hwFormat = hw;
    return true;
}

int SetUpHardwareFrames(AVCodecContext* c, AVPixelFormat hwFormat, BufferRefPtr& device, BufferRefPtr& frames) {
    // The device type has the hardware format's name ("vaapi", "qsv", "cuda", ...)
    const char* name = av_get_pix_fmt_name(hwFormat);
    const AVHWDeviceType type = name ? av_hwdevice_find_type_by_name(name) : AV_HWDEVICE_TYPE_NONE;
    if (type == AV_HWDEVICE_TYPE_NONE) return AVERROR(ENOSYS);
    AVBufferRef* dev = nullptr;
    int err = av_hwdevice_ctx_create(&dev, type, nullptr, nullptr, 0);
    if (err < 0) return err;
    device.reset(dev);
    AVBufferRef* fr = av_hwframe_ctx_alloc(device.get());
    if (!fr) return AVERROR(ENOMEM);
    frames.reset(fr);
    auto* fc = reinterpret_cast<AVHWFramesContext*>(frames->data);
    fc->format = hwFormat;
    fc->sw_format = AV_PIX_FMT_NV12;
    fc->width = c->width;
    fc->height = c->height;
    fc->initial_pool_size = 20;
    if ((err = av_hwframe_ctx_init(frames.get())) < 0) return err;
    c->hw_frames_ctx = av_buffer_ref(frames.get());
    return c->hw_frames_ctx ? 0 : AVERROR(ENOMEM);
}

bool EncoderWorks(const AVCodec* codec) {
    if (!codec) return false;
    if (AccelerationOfEncoder(codec->name) == VideoFXAcceleration::Software) return true;
    std::lock_guard<std::mutex> lock(workingMutex);
    auto it = workingEncoders.find(codec->name);
    if (it != workingEncoders.end()) return it->second;
    const bool works = TryEncoder(codec);
    workingEncoders.emplace(codec->name, works);
    return works;
}

const AVCodec* FindVideoEncoder(VideoFXVideoCodec codec, VideoFXAcceleration acceleration) {
    EnsureBackendInitialised();
    if (acceleration == VideoFXAcceleration::Auto) return AutoEncoder(codec);
    const std::vector<EncoderCandidate> candidates = VideoEncoderCandidates(codec);
    auto first = [&](auto&& wanted) -> const AVCodec* {
        for (const EncoderCandidate& e : candidates) {
            if (!wanted(e.kind)) continue;
            const AVCodec* c = avcodec_find_encoder_by_name(e.name);
            if (c && EncoderWorks(c)) return c;
        }
        return nullptr;
    };
    const auto software = [](VideoFXAcceleration k) { return k == VideoFXAcceleration::Software; };
    switch (acceleration) {
        case VideoFXAcceleration::Software: return first(software);
        case VideoFXAcceleration::Hardware: {
            const AVCodec* hw = first([](VideoFXAcceleration k) { return k != VideoFXAcceleration::Software; });
            return hw ? hw : first(software);
        }
        default: return first([acceleration](VideoFXAcceleration k) { return k == acceleration; });
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

bool VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec codec, VideoFXAcceleration acceleration) {
    const AVCodec* c = Internal::FindVideoEncoder(codec, acceleration);
    if (!c) return false;
    // Hardware asks for a hardware encoder, not the software one it falls back to
    return acceleration != VideoFXAcceleration::Hardware ||
           Internal::AccelerationOfEncoder(c->name) != VideoFXAcceleration::Software;
}

std::vector<VideoFXEncoderInfo> VideoFX_ListVideoEncoders(VideoFXVideoCodec codec) {
    Internal::EnsureBackendInitialised();
    static const VideoFXVideoCodec all[] = {VideoFXVideoCodec::H264, VideoFXVideoCodec::H265, VideoFXVideoCodec::AV1,
                                            VideoFXVideoCodec::VP9, VideoFXVideoCodec::VP8, VideoFXVideoCodec::MPEG4,
                                            VideoFXVideoCodec::MJPEG, VideoFXVideoCodec::ProRes,
                                            VideoFXVideoCodec::FFV1, VideoFXVideoCodec::GIF};
    std::vector<VideoFXEncoderInfo> out;
    for (VideoFXVideoCodec c : all) {
        if (codec != VideoFXVideoCodec::Auto && codec != c) continue;
        for (const Internal::EncoderCandidate& e : Internal::VideoEncoderCandidates(c)) {
            const AVCodec* enc = avcodec_find_encoder_by_name(e.name);
            if (!enc) continue;                         // not in this FFmpeg build
            VideoFXEncoderInfo info;
            info.name = e.name;
            info.codec = c;
            info.acceleration = e.kind;
            info.works = Internal::EncoderWorks(enc);
            out.push_back(std::move(info));
        }
    }
    return out;
}

std::string VideoFX_ChooseVideoEncoder(VideoFXVideoCodec codec, VideoFXAcceleration acceleration) {
    const AVCodec* c = Internal::FindVideoEncoder(codec, acceleration);
    return c ? c->name : "";
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
