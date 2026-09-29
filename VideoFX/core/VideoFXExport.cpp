// VideoFX/core/VideoFXExport.cpp
// The export pipeline: a list of segments is decoded, trimmed, sped up,
// filtered, fitted to one output size / rate / sample format, and encoded into
// a single file. The convenience calls (Transcode, Trim, Concatenate, ...) are
// all one-line timelines over VideoFX_Export.
//
//   segment 1 ─ decode ─ filter graph ─┐
//   segment 2 ─ decode ─ filter graph ─┼─ video encoder ─┐
//   ...                                └─ audio FIFO ─ audio encoder ─┴─ muxer
//
// Timeline bookkeeping: each segment starts where the longer of the two output
// streams ended. A stream that ran short is padded before the next segment -
// video by holding its last frame, audio with silence - so picture and sound
// stay in sync across any number of joins.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXBackend.h"
#include "VideoFXFilterBuilder.h"
#include "VideoFX/VideoFX.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

namespace VideoFX {

using namespace Internal;

namespace {

// ============================================================================
// PLANNING
// ============================================================================

// Muxers whose files carry sound only (their video_codec is cover art, if any)
const std::set<std::string> kAudioOnlyMuxers = {"mp3", "ipod", "wav", "flac", "ogg", "oga", "opus", "adts", "w64"};

const char* MuxerName(VideoFXContainer c) {
    switch (c) {
        case VideoFXContainer::MP4:  return "mp4";
        case VideoFXContainer::MOV:  return "mov";
        case VideoFXContainer::MKV:  return "matroska";
        case VideoFXContainer::WebM: return "webm";
        case VideoFXContainer::AVI:  return "avi";
        case VideoFXContainer::GIF:  return "gif";
        case VideoFXContainer::MP3:  return "mp3";
        case VideoFXContainer::M4A:  return "ipod";
        case VideoFXContainer::WAV:  return "wav";
        case VideoFXContainer::FLAC: return "flac";
        case VideoFXContainer::OGG:  return "ogg";
        default:                     return nullptr;
    }
}

const char* VideoCodecName(VideoFXVideoCodec c) {
    switch (c) {
        case VideoFXVideoCodec::H264:   return "H.264";
        case VideoFXVideoCodec::H265:   return "H.265";
        case VideoFXVideoCodec::VP8:    return "VP8";
        case VideoFXVideoCodec::VP9:    return "VP9";
        case VideoFXVideoCodec::AV1:    return "AV1";
        case VideoFXVideoCodec::MPEG4:  return "MPEG-4";
        case VideoFXVideoCodec::MJPEG:  return "Motion JPEG";
        case VideoFXVideoCodec::ProRes: return "ProRes";
        case VideoFXVideoCodec::FFV1:   return "FFV1";
        case VideoFXVideoCodec::GIF:    return "GIF";
        default:                        return "video";
    }
}

const char* AudioCodecName(VideoFXAudioCodec c) {
    switch (c) {
        case VideoFXAudioCodec::AAC:    return "AAC";
        case VideoFXAudioCodec::MP3:    return "MP3";
        case VideoFXAudioCodec::Opus:   return "Opus";
        case VideoFXAudioCodec::Vorbis: return "Vorbis";
        case VideoFXAudioCodec::FLAC:   return "FLAC";
        case VideoFXAudioCodec::PCM16:  return "PCM";
        default:                        return "audio";
    }
}

// The container's usual codecs, most compatible first
std::vector<VideoFXVideoCodec> DefaultVideoCodecs(const std::string& muxer) {
    if (muxer == "webm") return {VideoFXVideoCodec::VP9, VideoFXVideoCodec::VP8, VideoFXVideoCodec::AV1};
    if (muxer == "avi")  return {VideoFXVideoCodec::MPEG4, VideoFXVideoCodec::MJPEG};
    if (muxer == "gif")  return {VideoFXVideoCodec::GIF};
    return {VideoFXVideoCodec::H264, VideoFXVideoCodec::MPEG4};
}

std::vector<VideoFXAudioCodec> DefaultAudioCodecs(const std::string& muxer) {
    if (muxer == "webm") return {VideoFXAudioCodec::Opus, VideoFXAudioCodec::Vorbis};
    if (muxer == "ogg" || muxer == "oga") return {VideoFXAudioCodec::Vorbis, VideoFXAudioCodec::Opus, VideoFXAudioCodec::FLAC};
    if (muxer == "opus") return {VideoFXAudioCodec::Opus};
    if (muxer == "mp3")  return {VideoFXAudioCodec::MP3};
    if (muxer == "wav" || muxer == "w64") return {VideoFXAudioCodec::PCM16};
    if (muxer == "flac") return {VideoFXAudioCodec::FLAC};
    if (muxer == "avi")  return {VideoFXAudioCodec::MP3, VideoFXAudioCodec::PCM16};
    if (muxer == "matroska") return {VideoFXAudioCodec::AAC, VideoFXAudioCodec::Opus, VideoFXAudioCodec::Vorbis};
    return {VideoFXAudioCodec::AAC};
}

struct SegmentPlan {
    VideoFXSegment segment;
    bool fileVideo = false;         // File source has a video stream
    bool fileAudio = false;         // File source has an audio stream
    double sourceStart = 0.0;       // seconds into the file
    double sourceEnd = 0.0;         // 0 = to the end (duration unknown)
    double outDuration = 0.0;       // seconds on the output timeline, 0 = unknown
    int rotation = 0;
    double frameRate = 0.0;
    int sampleRate = 0;
    int channels = 0;
    int displayWidth = 0;           // after rotation, crop and quarter turns
    int displayHeight = 0;
    std::string videoEffects;       // filter chains built from segment.effects
    std::string audioEffects;
};

// Output frame size of a segment after its geometric effects
void ApplyEffectGeometry(const std::vector<VideoFXEffect>& effects, int& w, int& h) {
    for (const VideoFXEffect& e : effects) {
        switch (e.type) {
            case VideoFXEffectType::Rotate90:
            case VideoFXEffectType::Rotate270:
                std::swap(w, h);
                break;
            case VideoFXEffectType::Crop:
                w = e.width;
                h = e.height;
                break;
            default:
                break;
        }
    }
}

int EvenDown(int v) { return std::max(2, v & ~1); }

// ============================================================================
// EXPORTER
// ============================================================================

class Exporter {
public:
    Exporter(const std::string& outputPath, const VideoFXExportSettings& settings,
             const VideoFXProgressCallback& progress)
        : outputPath(outputPath), settings(settings), progress(progress) {}
    ~Exporter() { Close(); }

    VideoFXResult Run(const std::vector<VideoFXSegment>& segments);

private:
    // ---- planning ----
    VideoFXResult PlanSegments(const std::vector<VideoFXSegment>& segments);
    VideoFXResult PlanOutput();
    VideoFXResult OpenVideoEncoder();
    VideoFXResult OpenAudioEncoder();
    VideoFXResult OpenOutput();

    // ---- per segment ----
    VideoFXResult ProcessSegment(const SegmentPlan& plan);
    VideoFXResult ProcessFileSegment(const SegmentPlan& plan);
    VideoFXResult RunGeneratorGraphs(const SegmentPlan& plan, bool video, bool audio);
    std::string VideoTail(bool firstFrameSarNotSquare) const;
    std::string AudioTail() const;

    // ---- encoding ----
    VideoFXResult EmitVideo(AVFrame* filtered, AVRational sinkTb);
    VideoFXResult EmitAudio(AVFrame* filtered);
    VideoFXResult EncodeVideo(AVFrame* frame);
    VideoFXResult EncodeAudio(AVFrame* frame);
    VideoFXResult DrainAudioFifo(bool final);
    VideoFXResult PadTo(double seconds);
    VideoFXResult PadAudioSilence(int64_t samples);
    VideoFXResult WritePackets(AVCodecContext* enc, AVStream* st);
    VideoFXResult Finish();

    bool ReportProgress(double segmentSeconds);
    void Close();

    std::string outputPath;
    VideoFXExportSettings settings;
    VideoFXProgressCallback progress;

    std::vector<SegmentPlan> plans;
    double totalDuration = 0.0;

    // output
    const AVOutputFormat* ofmt = nullptr;
    std::string muxer;
    AVFormatContext* oc = nullptr;
    bool ioOpened = false;
    bool headerWritten = false;

    bool outVideo = false, outAudio = false;
    const AVCodec* vcodec = nullptr;
    const AVCodec* acodec = nullptr;
    CodecContextPtr venc, aenc;
    AVStream* vst = nullptr;
    AVStream* ast = nullptr;
    int width = 0, height = 0;
    AVRational frameRate{30, 1};
    AVPixelFormat pixFmt = AV_PIX_FMT_YUV420P;
    bool gif = false;
    int sampleRate = 48000, channels = 2;
    AVSampleFormat sampleFmt = AV_SAMPLE_FMT_FLTP;
    int audioFrameSize = 1024;

    // timeline state
    int64_t nextVideoPts = 0;           // in frames (encoder time base 1/fps)
    int64_t segmentVideoBase = 0;
    FramePtr lastVideoFrame;
    AudioFifoPtr fifo;
    int64_t audioSamplesQueued = 0;     // total samples written to the FIFO
    int64_t audioSamplesEncoded = 0;
    double segmentStart = 0.0;
    double progressDone = 0.0;          // seconds of finished segments
    std::chrono::steady_clock::time_point lastReport{};
    bool cancelled = false;
};

// ---------------------------------------------------------------------------
// planning
// ---------------------------------------------------------------------------

VideoFXResult Exporter::PlanSegments(const std::vector<VideoFXSegment>& segments) {
    if (segments.empty()) return Fail(VideoFXResult::InvalidArgument, "The timeline has no segments");
    const std::filesystem::path outPath = UltraCanvas::PathFromUtf8(outputPath);

    for (const VideoFXSegment& seg : segments) {
        SegmentPlan p;
        p.segment = seg;
        if (!(seg.speed >= 0.25 && seg.speed <= 4.0))
            return Fail(VideoFXResult::InvalidArgument, "Segment speed must be 0.25..4");

        if (seg.kind == VideoFXSourceKind::File) {
            std::error_code ec;
            if (std::filesystem::equivalent(UltraCanvas::PathFromUtf8(seg.path), outPath, ec))
                return Fail(VideoFXResult::InvalidArgument, "The output file is also an input: " + seg.path);
            VideoFXMediaInfo info;
            VideoFXResult r = VideoFX_Probe(seg.path, info);
            if (r != VideoFXResult::Ok) return r;
            if (!(seg.start >= 0.0) || !(seg.end >= 0.0) || (seg.end > 0.0 && seg.end <= seg.start))
                return Fail(VideoFXResult::InvalidArgument, "Bad trim range for " + seg.path);
            if (info.duration > 0.0 && seg.start >= info.duration)
                return Fail(VideoFXResult::InvalidArgument, "Trim start lies after the end of " + seg.path);
            p.fileVideo = info.HasVideo();
            p.fileAudio = info.HasAudio();
            p.sourceStart = seg.start;
            p.sourceEnd = seg.end;
            if (info.duration > 0.0 && (p.sourceEnd <= 0.0 || p.sourceEnd > info.duration)) p.sourceEnd = info.duration;
            if (p.sourceEnd > 0.0) p.outDuration = (p.sourceEnd - p.sourceStart) / seg.speed;
            if (p.fileVideo) {
                const VideoFXStreamInfo& vs = info.streams[info.videoStreamIndex];
                p.rotation = vs.rotation;
                p.frameRate = vs.frameRate;
                p.displayWidth = info.width;
                p.displayHeight = info.height;
            }
            if (p.fileAudio) {
                p.sampleRate = info.sampleRate;
                p.channels = info.channels;
            }
        } else {
            if (!(seg.duration > 0.0 && seg.duration <= 24 * 3600.0))
                return Fail(VideoFXResult::InvalidArgument, "Generated segments last 0..24 hours");
            p.outDuration = seg.duration;
            if (seg.kind == VideoFXSourceKind::TestPattern) {
                p.channels = 1;
            }
        }

        std::string error;
        if (!BuildVideoEffectChain(seg.effects, p.outDuration, p.videoEffects, error) ||
            !BuildAudioEffectChain(seg.effects, p.outDuration, p.audioEffects, error))
            return Fail(VideoFXResult::InvalidArgument, error);
        for (const VideoFXEffect& e : seg.effects) {
            std::error_code ec;
            if (e.type == VideoFXEffectType::LUT && !std::filesystem::exists(UltraCanvas::PathFromUtf8(e.path), ec))
                return Fail(VideoFXResult::FileNotFound, "LUT file not found: " + e.path);
        }
        totalDuration += p.outDuration;
        plans.push_back(std::move(p));
    }
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::PlanOutput() {
    // ---- container ----
    if (settings.container == VideoFXContainer::Auto) {
        ofmt = av_guess_format(nullptr, outputPath.c_str(), nullptr);
        if (!ofmt) return Fail(VideoFXResult::InvalidArgument, "No known format for the extension of " + outputPath);
    } else {
        ofmt = av_guess_format(MuxerName(settings.container), nullptr, nullptr);
        if (!ofmt) return Fail(VideoFXResult::UnsupportedFormat, "This build cannot write the requested container");
    }
    muxer = ofmt->name;
    const bool audioOnlyContainer = kAudioOnlyMuxers.count(muxer) > 0;
    gif = muxer == "gif";

    // ---- which streams ----
    bool anyVideo = false, anyAudio = false;
    for (const SegmentPlan& p : plans) {
        if (p.segment.kind != VideoFXSourceKind::File || p.fileVideo) anyVideo = true;
        const bool contributesAudio = !p.segment.mute &&
            ((p.segment.kind == VideoFXSourceKind::File && p.fileAudio) || p.segment.kind == VideoFXSourceKind::TestPattern);
        if (contributesAudio) anyAudio = true;
    }
    outVideo = anyVideo && !audioOnlyContainer && settings.videoCodec != VideoFXVideoCodec::Disabled;
    outAudio = anyAudio && !gif && settings.audioCodec != VideoFXAudioCodec::Disabled;
    if (audioOnlyContainer && !anyAudio)
        return Fail(VideoFXResult::NoMediaStreams, "No segment has sound for an audio-only output");
    if (!outVideo && !outAudio) return Fail(VideoFXResult::NoMediaStreams, "Nothing to export: no video or audio output");

    // ---- video codec ----
    if (outVideo) {
        std::vector<VideoFXVideoCodec> candidates;
        if (settings.videoCodec == VideoFXVideoCodec::Auto) candidates = DefaultVideoCodecs(muxer);
        else candidates = {settings.videoCodec};
        for (VideoFXVideoCodec c : candidates) {
            if ((vcodec = FindVideoEncoder(c))) break;
        }
        if (!vcodec)
            return Fail(VideoFXResult::EncoderNotAvailable,
                        std::string("No ") + VideoCodecName(candidates.front()) + " encoder in this build");
        if (avformat_query_codec(ofmt, vcodec->id, FF_COMPLIANCE_NORMAL) == 0)
            return Fail(VideoFXResult::UnsupportedFormat,
                        std::string(vcodec->name) + " cannot be stored in " + muxer);
    }

    // ---- audio codec ----
    if (outAudio) {
        std::vector<VideoFXAudioCodec> candidates;
        if (settings.audioCodec == VideoFXAudioCodec::Auto) candidates = DefaultAudioCodecs(muxer);
        else candidates = {settings.audioCodec};
        for (VideoFXAudioCodec c : candidates) {
            if ((acodec = FindAudioEncoder(c))) break;
        }
        if (!acodec)
            return Fail(VideoFXResult::EncoderNotAvailable,
                        std::string("No ") + AudioCodecName(candidates.front()) + " encoder in this build");
        if (avformat_query_codec(ofmt, acodec->id, FF_COMPLIANCE_NORMAL) == 0)
            return Fail(VideoFXResult::UnsupportedFormat,
                        std::string(acodec->name) + " cannot be stored in " + muxer);
    }

    // ---- geometry and rate from the first segment with a picture ----
    if (outVideo) {
        int srcW = 0, srcH = 0;
        double srcRate = 0.0;
        for (const SegmentPlan& p : plans) {
            if (p.segment.kind == VideoFXSourceKind::File && !p.fileVideo) continue;
            if (p.segment.kind == VideoFXSourceKind::File) {
                srcW = p.displayWidth;
                srcH = p.displayHeight;
                srcRate = p.frameRate;
            } else {
                srcW = 1280;
                srcH = 720;
            }
            ApplyEffectGeometry(p.segment.effects, srcW, srcH);
            break;
        }
        if (srcW <= 0 || srcH <= 0) { srcW = 1280; srcH = 720; }

        width = settings.width;
        height = settings.height;
        if (width > 0 && height <= 0) height = static_cast<int>(std::lround(static_cast<double>(width) * srcH / srcW));
        else if (height > 0 && width <= 0) width = static_cast<int>(std::lround(static_cast<double>(height) * srcW / srcH));
        else if (width <= 0 && height <= 0) { width = srcW; height = srcH; }
        if (width > 16384 || height > 16384 || width <= 0 || height <= 0)
            return Fail(VideoFXResult::InvalidArgument, "Output size must be 1..16384 pixels");
        if (!gif) { width = EvenDown(width); height = EvenDown(height); }

        double fps = settings.frameRate > 0.0 ? settings.frameRate : srcRate;
        if (!(fps > 0.0 && fps <= 240.0)) fps = gif ? 12.0 : 30.0;
        frameRate = av_d2q(fps, 1001000);
    }

    // ---- audio format ----
    if (outAudio) {
        int rate = settings.sampleRate;
        int maxChannels = 0;
        for (const SegmentPlan& p : plans) {
            if (p.segment.mute) continue;
            if (rate <= 0 && p.sampleRate > 0) rate = p.sampleRate;
            maxChannels = std::max(maxChannels, p.channels);
        }
        if (rate <= 0) rate = 48000;
        if (const int* rates = SupportedSampleRates(acodec)) {
            int best = rates[0];
            for (const int* r = rates; *r; ++r) {
                if (std::abs(*r - rate) < std::abs(best - rate) || *r == rate) best = *r;
            }
            rate = best;
        }
        sampleRate = rate;
        channels = settings.channels > 0 ? std::min(settings.channels, 8) : (maxChannels == 1 ? 1 : 2);
        const AVSampleFormat* fmts = SupportedSampleFormats(acodec);
        sampleFmt = fmts ? fmts[0] : AV_SAMPLE_FMT_S16;
    }
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::OpenVideoEncoder() {
    venc.reset(avcodec_alloc_context3(vcodec));
    if (!venc) return Fail(VideoFXResult::EncodeError, "Out of memory");
    AVCodecContext* c = venc.get();
    const std::string name = vcodec->name;

    // Pixel format: what players accept everywhere, when the encoder takes it
    if (gif) {
        pixFmt = AV_PIX_FMT_PAL8;
    } else {
        AVPixelFormat preferred = AV_PIX_FMT_YUV420P;
        if (name.rfind("prores", 0) == 0) preferred = AV_PIX_FMT_YUV422P10LE;
        else if (name == "mjpeg") preferred = AV_PIX_FMT_YUVJ420P;
        const AVPixelFormat* fmts = SupportedPixelFormats(vcodec);
        pixFmt = preferred;
        if (fmts) {
            bool found = false;
            for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
                if (*p == preferred) { found = true; break; }
            }
            if (!found) {
                pixFmt = fmts[0];
                for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
                    if (*p == AV_PIX_FMT_YUV420P || *p == AV_PIX_FMT_NV12) { pixFmt = *p; break; }
                }
            }
        }
    }

    c->width = width;
    c->height = height;
    c->pix_fmt = pixFmt;
    c->sample_aspect_ratio = AVRational{1, 1};
    c->time_base = av_inv_q(frameRate);
    c->framerate = frameRate;
    c->thread_count = settings.threads;
    if (name == "mjpeg") c->color_range = AVCOL_RANGE_JPEG;
    if (vcodec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if (ofmt->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Quality / rate control
    const int q = settings.quality < 0 ? -1 : std::min(settings.quality, 100);
    const double fps = av_q2d(frameRate);
    const int64_t pixelRate = static_cast<int64_t>(static_cast<double>(width) * height * fps);
    auto crfFor = [&](int dflt, double top) {
        return q < 0 ? dflt : static_cast<int>(std::lround(top - q * top / 100.0));
    };
    if (settings.videoBitRate > 0) {
        c->bit_rate = settings.videoBitRate;
    } else if (name == "libx264") {
        av_opt_set_int(c->priv_data, "crf", crfFor(23, 51.0), 0);
    } else if (name == "libx265") {
        av_opt_set_int(c->priv_data, "crf", crfFor(28, 51.0), 0);
        av_opt_set(c->priv_data, "x265-params", "log-level=error", 0);   // x265 logs on its own, past av_log
    } else if (name == "libvpx-vp9" || name == "libaom-av1") {
        c->bit_rate = 0;
        av_opt_set_int(c->priv_data, "crf", crfFor(name == "libaom-av1" ? 32 : 33, 63.0), 0);
    } else if (name == "libsvtav1") {
        av_opt_set_int(c->priv_data, "crf", crfFor(35, 63.0), 0);
    } else if (name == "libvpx") {
        // VP8 constant quality still needs a bit-rate ceiling
        c->bit_rate = std::max<int64_t>(500000, pixelRate / 5);
        av_opt_set_int(c->priv_data, "crf", crfFor(10, 63.0), 0);
    } else if (name == "mpeg4" || name == "mjpeg") {
        const int qs = q < 0 ? 3 : std::clamp(static_cast<int>(std::lround(31 - q * 0.30)), 1, 31);
        c->flags |= AV_CODEC_FLAG_QSCALE;
        c->global_quality = FF_QP2LAMBDA * qs;
    } else if (name == "prores_ks") {
        av_opt_set(c->priv_data, "profile", q >= 80 ? "hq" : "standard", 0);
    } else if (name != "gif" && name != "ffv1" && name.rfind("prores", 0) != 0) {
        // Platform / fixed-rate encoders (VideoToolbox, Media Foundation,
        // OpenH264): ~0.1 bit per pixel is good HD quality, scaled by quality
        const double factor = q < 0 ? 0.1 : 0.03 + q * 0.0017;
        c->bit_rate = std::max<int64_t>(300000, static_cast<int64_t>(pixelRate * factor));
    }
    if (name == "libaom-av1") {
        av_opt_set_int(c->priv_data, "cpu-used", 6, 0);   // default 1 is far too slow for an editor
        av_opt_set_int(c->priv_data, "row-mt", 1, 0);
    }
    if (name == "libvpx-vp9") {
        av_opt_set_int(c->priv_data, "row-mt", 1, 0);
        av_opt_set_int(c->priv_data, "cpu-used", 4, 0);
        av_opt_set(c->priv_data, "deadline", "good", 0);
    }
    if (!settings.encoderPreset.empty()) av_opt_set(c->priv_data, "preset", settings.encoderPreset.c_str(), 0);

    int err = avcodec_open2(c, vcodec, nullptr);
    if (err < 0) return Fail(VideoFXResult::EncodeError, std::string("Cannot open the ") + name + " encoder", err);

    vst = avformat_new_stream(oc, nullptr);
    if (!vst) return Fail(VideoFXResult::EncodeError, "Out of memory");
    err = avcodec_parameters_from_context(vst->codecpar, c);
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Cannot set up the video stream", err);
    vst->time_base = c->time_base;
    vst->avg_frame_rate = frameRate;
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::OpenAudioEncoder() {
    aenc.reset(avcodec_alloc_context3(acodec));
    if (!aenc) return Fail(VideoFXResult::EncodeError, "Out of memory");
    AVCodecContext* c = aenc.get();
    const std::string name = acodec->name;
    c->sample_fmt = sampleFmt;
    c->sample_rate = sampleRate;
    SetEncoderChannels(c, channels);
    c->time_base = AVRational{1, sampleRate};
    if (settings.audioBitRate > 0) {
        c->bit_rate = settings.audioBitRate;
    } else if (name == "aac" || name == "aac_at" || name == "libfdk_aac") {
        c->bit_rate = channels == 1 ? 96000 : 192000;
    } else if (name == "libmp3lame" || name == "mp3_mf") {
        c->bit_rate = channels == 1 ? 128000 : 192000;
    } else if (name == "libopus" || name == "opus") {
        c->bit_rate = channels == 1 ? 64000 : 128000;
    } else if (name == "libvorbis" || name == "vorbis") {
        c->bit_rate = channels == 1 ? 96000 : 160000;
    }
    if (acodec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if (ofmt->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    int err = avcodec_open2(c, acodec, nullptr);
    if (err < 0) return Fail(VideoFXResult::EncodeError, std::string("Cannot open the ") + name + " encoder", err);
    audioFrameSize = (c->frame_size > 0 && !(acodec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
                         ? c->frame_size : 1024;

    ast = avformat_new_stream(oc, nullptr);
    if (!ast) return Fail(VideoFXResult::EncodeError, "Out of memory");
    err = avcodec_parameters_from_context(ast->codecpar, c);
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Cannot set up the audio stream", err);
    ast->time_base = c->time_base;

    fifo.reset(av_audio_fifo_alloc(sampleFmt, channels, audioFrameSize * 4));
    if (!fifo) return Fail(VideoFXResult::EncodeError, "Out of memory");
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::OpenOutput() {
    // FFmpeg 4.x declares the format parameter non-const (ff_const59); it is
    // only read, so the cast is safe on every version.
    int err = avformat_alloc_output_context2(&oc, const_cast<AVOutputFormat*>(ofmt), nullptr, outputPath.c_str());
    if (err < 0 || !oc) return Fail(VideoFXResult::WriteError, "Cannot create the output container", err);

    VideoFXResult r;
    if (outVideo && (r = OpenVideoEncoder()) != VideoFXResult::Ok) return r;
    if (outAudio && (r = OpenAudioEncoder()) != VideoFXResult::Ok) return r;

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        // UTF-8 path; FFmpeg's file protocol handles the Windows conversion
        err = avio_open(&oc->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot create " + outputPath, err);
        ioOpened = true;
    }
    AVDictionary* opts = nullptr;
    if (muxer == "mp4" || muxer == "mov" || muxer == "ipod") av_dict_set(&opts, "movflags", "+faststart", 0);
    err = avformat_write_header(oc, &opts);
    av_dict_free(&opts);
    if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot write the file header", err);
    headerWritten = true;
    return VideoFXResult::Ok;
}

// ---------------------------------------------------------------------------
// filter text shared by every segment
// ---------------------------------------------------------------------------

// Fit to the output size, output rate and pixel format
std::string Exporter::VideoTail(bool sarNotSquare) const {
    const std::string W = std::to_string(width), H = std::to_string(height);
    std::string tail = "fps=" + std::to_string(frameRate.num) + "/" + std::to_string(frameRate.den);
    if (sarNotSquare) AppendFilter(tail, "scale=trunc(iw*sar/2)*2:ih,setsar=1");
    switch (settings.fitMode) {
        case VideoFXFitMode::Fill:
            AppendFilter(tail, "scale=" + W + ":" + H + ":force_original_aspect_ratio=increase,crop=" + W + ":" + H);
            break;
        case VideoFXFitMode::Stretch:
            AppendFilter(tail, "scale=" + W + ":" + H);
            break;
        default:
            AppendFilter(tail, "scale=" + W + ":" + H + ":force_original_aspect_ratio=decrease,pad=" + W + ":" + H +
                               ":(ow-iw)/2:(oh-ih)/2:color=black");
            break;
    }
    AppendFilter(tail, "setsar=1");
    if (gif) {
        AppendFilter(tail, "split[vfxpal0][vfxpal1];[vfxpal0]palettegen=stats_mode=diff[vfxpal];"
                           "[vfxpal1][vfxpal]paletteuse=dither=bayer:bayer_scale=5");
    } else {
        AppendFilter(tail, std::string("format=") + av_get_pix_fmt_name(pixFmt));
    }
    return tail;
}

std::string Exporter::AudioTail() const {
    const char* layout = channels == 1 ? "mono" : channels == 2 ? "stereo" : nullptr;
    std::string layoutText = layout ? layout : std::to_string(channels) + "c";
    return "aresample=" + std::to_string(sampleRate) + ",aformat=sample_fmts=" +
           av_get_sample_fmt_name(sampleFmt) + ":sample_rates=" + std::to_string(sampleRate) +
           ":channel_layouts=" + layoutText;
}

// ---------------------------------------------------------------------------
// graphs
// ---------------------------------------------------------------------------

struct Graph {
    FilterGraphPtr graph;
    AVFilterContext* src = nullptr;     // nullptr for generator graphs
    AVFilterContext* sink = nullptr;
    bool eof = false;
};

// Build `graph` from `desc`, fed by a buffer source created from `srcArgs`
// (empty = the description starts with its own source filter)
VideoFXResult BuildGraph(Graph& g, bool video, const std::string& srcArgs, const std::string& desc) {
    g.graph.reset(avfilter_graph_alloc());
    if (!g.graph) return Fail(VideoFXResult::FilterError, "Out of memory");
    g.graph->nb_threads = 0;
    int err;
    if (!srcArgs.empty()) {
        err = avfilter_graph_create_filter(&g.src, avfilter_get_by_name(video ? "buffer" : "abuffer"), "in",
                                           srcArgs.c_str(), nullptr, g.graph.get());
        if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot create the filter source", err);
    }
    err = avfilter_graph_create_filter(&g.sink, avfilter_get_by_name(video ? "buffersink" : "abuffersink"), "out",
                                       nullptr, nullptr, g.graph.get());
    if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot create the filter sink", err);

    AVFilterInOut* outputs = nullptr;
    if (g.src) {
        outputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = g.src;
        outputs->pad_idx = 0;
        outputs->next = nullptr;
    }
    AVFilterInOut* inputs = avfilter_inout_alloc();
    inputs->name = av_strdup("out");
    inputs->filter_ctx = g.sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    err = avfilter_graph_parse_ptr(g.graph.get(), desc.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot build the effect chain \"" + desc + "\"", err);
    err = avfilter_graph_config(g.graph.get(), nullptr);
    if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot configure the effect chain \"" + desc + "\"", err);
    return VideoFXResult::Ok;
}

// ---------------------------------------------------------------------------
// encoding
// ---------------------------------------------------------------------------

VideoFXResult Exporter::WritePackets(AVCodecContext* enc, AVStream* st) {
    PacketPtr pkt = MakePacket();
    while (true) {
        int err = avcodec_receive_packet(enc, pkt.get());
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return VideoFXResult::Ok;
        if (err < 0) return Fail(VideoFXResult::EncodeError, "Encoding failed", err);
        av_packet_rescale_ts(pkt.get(), enc->time_base, st->time_base);
        pkt->stream_index = st->index;
        err = av_interleaved_write_frame(oc, pkt.get());
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot write " + outputPath, err);
    }
}

VideoFXResult Exporter::EncodeVideo(AVFrame* frame) {
    int err = avcodec_send_frame(venc.get(), frame);
    if (err < 0 && err != AVERROR_EOF) return Fail(VideoFXResult::EncodeError, "Video encoding failed", err);
    return WritePackets(venc.get(), vst);
}

VideoFXResult Exporter::EncodeAudio(AVFrame* frame) {
    int err = avcodec_send_frame(aenc.get(), frame);
    if (err < 0 && err != AVERROR_EOF) return Fail(VideoFXResult::EncodeError, "Audio encoding failed", err);
    return WritePackets(aenc.get(), ast);
}

VideoFXResult Exporter::EmitVideo(AVFrame* f, AVRational sinkTb) {
    const int64_t rel = f->pts == AV_NOPTS_VALUE ? nextVideoPts - segmentVideoBase
                                                 : av_rescale_q(f->pts, sinkTb, venc->time_base);
    const int64_t pts = segmentVideoBase + rel;
    if (pts < nextVideoPts) return VideoFXResult::Ok;     // duplicate timestamp: drop
    f->pts = pts;
    f->pict_type = AV_PICTURE_TYPE_NONE;
    VideoFXResult r = EncodeVideo(f);
    if (r != VideoFXResult::Ok) return r;
    nextVideoPts = pts + 1;
    if (!lastVideoFrame) lastVideoFrame = MakeFrame();
    av_frame_unref(lastVideoFrame.get());
    av_frame_ref(lastVideoFrame.get(), f);
    if (!ReportProgress(static_cast<double>(nextVideoPts) * av_q2d(venc->time_base) - segmentStart))
        return Fail(VideoFXResult::Cancelled, "Export cancelled");
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::EmitAudio(AVFrame* f) {
    int err = av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(f->extended_data), f->nb_samples);
    if (err < f->nb_samples) return Fail(VideoFXResult::EncodeError, "Audio buffer overflow", err < 0 ? err : 0);
    audioSamplesQueued += f->nb_samples;
    VideoFXResult r = DrainAudioFifo(false);
    if (r != VideoFXResult::Ok) return r;
    if (!outVideo && !ReportProgress(static_cast<double>(audioSamplesQueued) / sampleRate - segmentStart))
        return Fail(VideoFXResult::Cancelled, "Export cancelled");
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::DrainAudioFifo(bool final) {
    while (av_audio_fifo_size(fifo.get()) >= audioFrameSize || (final && av_audio_fifo_size(fifo.get()) > 0)) {
        const int n = std::min(audioFrameSize, av_audio_fifo_size(fifo.get()));
        FramePtr frame = MakeFrame();
        frame->nb_samples = n;
        frame->format = sampleFmt;
        frame->sample_rate = sampleRate;
        SetFrameChannels(frame.get(), aenc.get());
        int err = av_frame_get_buffer(frame.get(), 0);
        if (err < 0) return Fail(VideoFXResult::EncodeError, "Out of memory", err);
        if (av_audio_fifo_read(fifo.get(), reinterpret_cast<void**>(frame->extended_data), n) < n)
            return Fail(VideoFXResult::EncodeError, "Audio buffer underrun");
        frame->pts = audioSamplesEncoded;
        audioSamplesEncoded += n;
        VideoFXResult r = EncodeAudio(frame.get());
        if (r != VideoFXResult::Ok) return r;
    }
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::PadAudioSilence(int64_t samples) {
    while (samples > 0) {
        const int n = static_cast<int>(std::min<int64_t>(samples, 4096));
        FramePtr frame = MakeFrame();
        frame->nb_samples = n;
        frame->format = sampleFmt;
        frame->sample_rate = sampleRate;
        SetFrameChannels(frame.get(), aenc.get());
        int err = av_frame_get_buffer(frame.get(), 0);
        if (err < 0) return Fail(VideoFXResult::EncodeError, "Out of memory", err);
        av_samples_set_silence(frame->extended_data, 0, n, channels, sampleFmt);
        if (av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(frame->extended_data), n) < n)
            return Fail(VideoFXResult::EncodeError, "Audio buffer overflow");
        audioSamplesQueued += n;
        samples -= n;
        VideoFXResult r = DrainAudioFifo(false);
        if (r != VideoFXResult::Ok) return r;
    }
    return VideoFXResult::Ok;
}

// Bring both output streams up to `seconds` on the timeline
VideoFXResult Exporter::PadTo(double seconds) {
    if (outVideo) {
        const int64_t target = static_cast<int64_t>(std::llround(seconds / av_q2d(venc->time_base)));
        if (target > nextVideoPts) {
            FramePtr hold = MakeFrame();
            if (lastVideoFrame) {
                av_frame_ref(hold.get(), lastVideoFrame.get());
            } else {
                // Nothing shown yet: black
                hold->format = pixFmt;
                hold->width = width;
                hold->height = height;
                if (av_frame_get_buffer(hold.get(), 0) < 0) return Fail(VideoFXResult::EncodeError, "Out of memory");
                const ptrdiff_t lines[4] = {hold->linesize[0], hold->linesize[1], hold->linesize[2], hold->linesize[3]};
                if (av_image_fill_black(hold->data, lines, pixFmt,
                                        pixFmt == AV_PIX_FMT_YUVJ420P ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG,
                                        width, height) < 0) {
                    for (int p = 0; p < 4 && hold->data[p]; ++p)
                        std::fill_n(hold->data[p], static_cast<size_t>(hold->linesize[p]) * height, 0);
                }
            }
            while (nextVideoPts < target) {
                FramePtr copy = MakeFrame();
                av_frame_ref(copy.get(), hold.get());
                copy->pts = nextVideoPts++;
                copy->pict_type = AV_PICTURE_TYPE_NONE;
                VideoFXResult r = EncodeVideo(copy.get());
                if (r != VideoFXResult::Ok) return r;
            }
        }
    }
    if (outAudio) {
        const int64_t target = static_cast<int64_t>(std::llround(seconds * sampleRate));
        if (target > audioSamplesQueued) return PadAudioSilence(target - audioSamplesQueued);
    }
    return VideoFXResult::Ok;
}

bool Exporter::ReportProgress(double segmentSeconds) {
    if (!progress || cancelled) return !cancelled;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastReport < std::chrono::milliseconds(100)) return true;
    lastReport = now;
    double fraction = totalDuration > 0.0 ? (progressDone + std::max(0.0, segmentSeconds)) / totalDuration : 0.0;
    fraction = std::clamp(fraction, 0.0, 0.999);
    if (!progress(fraction)) cancelled = true;
    return !cancelled;
}

// ---------------------------------------------------------------------------
// segments
// ---------------------------------------------------------------------------

// Pull everything the sink has; `flush` after EOF was sent to the source
template <class Emit>
VideoFXResult PullGraph(Graph& g, AVFrame* scratch, Emit emit) {
    if (!g.sink || g.eof) return VideoFXResult::Ok;
    while (true) {
        int err = av_buffersink_get_frame(g.sink, scratch);
        if (err == AVERROR(EAGAIN)) return VideoFXResult::Ok;
        if (err == AVERROR_EOF) { g.eof = true; return VideoFXResult::Ok; }
        if (err < 0) return Fail(VideoFXResult::FilterError, "Effect processing failed", err);
        VideoFXResult r = emit(scratch);
        av_frame_unref(scratch);
        if (r != VideoFXResult::Ok) return r;
    }
}

VideoFXResult Exporter::RunGeneratorGraphs(const SegmentPlan& plan, bool video, bool audio) {
    const double d = plan.outDuration;
    const std::string dur = FormatNumber(d);
    const std::string rate = std::to_string(frameRate.num) + "/" + std::to_string(frameRate.den);
    const std::string size = std::to_string(width) + "x" + std::to_string(height);
    Graph vg, ag;
    VideoFXResult r;
    if (video) {
        std::string desc;
        if (plan.segment.kind == VideoFXSourceKind::TestPattern) {
            desc = "testsrc2=s=" + size + ":r=" + rate + ":d=" + dur;
        } else {
            char color[16];
            snprintf(color, sizeof(color), "0x%06X", static_cast<unsigned>(plan.segment.kind == VideoFXSourceKind::Color
                                                                            ? plan.segment.color : 0));
            desc = std::string("color=c=") + color + ":s=" + size + ":r=" + rate + ":d=" + dur;
        }
        AppendFilter(desc, plan.segment.kind == VideoFXSourceKind::File ? "" : plan.videoEffects);
        AppendFilter(desc, VideoTail(false));
        if ((r = BuildGraph(vg, true, "", desc)) != VideoFXResult::Ok) return r;
    }
    if (audio) {
        std::string desc = "sine=frequency=1000:sample_rate=" + std::to_string(sampleRate) + ":duration=" + dur;
        AppendFilter(desc, plan.audioEffects);
        AppendFilter(desc, AudioTail());
        if ((r = BuildGraph(ag, false, "", desc)) != VideoFXResult::Ok) return r;
    }
    FramePtr scratch = MakeFrame();
    const AVRational vtb = vg.sink ? av_buffersink_get_time_base(vg.sink) : AVRational{1, 1};
    // Alternate so the muxer receives both streams interleaved
    while ((vg.sink && !vg.eof) || (ag.sink && !ag.eof)) {
        if (vg.sink && !vg.eof) {
            int err = av_buffersink_get_frame(vg.sink, scratch.get());
            if (err == AVERROR_EOF || err == AVERROR(EAGAIN)) vg.eof = true;
            else if (err < 0) return Fail(VideoFXResult::FilterError, "Generator failed", err);
            else {
                r = EmitVideo(scratch.get(), vtb);
                av_frame_unref(scratch.get());
                if (r != VideoFXResult::Ok) return r;
            }
        }
        if (ag.sink && !ag.eof) {
            int err = av_buffersink_get_frame(ag.sink, scratch.get());
            if (err == AVERROR_EOF || err == AVERROR(EAGAIN)) ag.eof = true;
            else if (err < 0) return Fail(VideoFXResult::FilterError, "Generator failed", err);
            else {
                r = EmitAudio(scratch.get());
                av_frame_unref(scratch.get());
                if (r != VideoFXResult::Ok) return r;
            }
        }
    }
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::ProcessFileSegment(const SegmentPlan& plan) {
    FormatInputPtr fmt;
    VideoFXResult r = OpenInput(plan.segment.path, fmt);
    if (r != VideoFXResult::Ok) return r;

    int vIndex = -1, aIndex = -1;
    if (outVideo && plan.fileVideo) {
        vIndex = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (vIndex >= 0 && (fmt->streams[vIndex]->disposition & AV_DISPOSITION_ATTACHED_PIC)) vIndex = -1;
    }
    const bool wantAudio = outAudio && plan.fileAudio && !plan.segment.mute;
    if (wantAudio) aIndex = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    // A picture-less file in a video export shows black for its length
    if (outVideo && vIndex < 0) {
        SegmentPlan black = plan;
        black.segment.kind = VideoFXSourceKind::Color;
        black.segment.color = 0x000000;
        if (black.outDuration > 0.0 && (r = RunGeneratorGraphs(black, true, false)) != VideoFXResult::Ok) return r;
        if (aIndex < 0) return VideoFXResult::Ok;
    }

    CodecContextPtr vdec, adec;
    if (vIndex >= 0 && (r = OpenDecoder(fmt.get(), vIndex, vdec)) != VideoFXResult::Ok) return r;
    if (aIndex >= 0 && (r = OpenDecoder(fmt.get(), aIndex, adec)) != VideoFXResult::Ok) return r;
    if (vIndex < 0 && aIndex < 0) return VideoFXResult::Ok;

    // All streams are timed from the container's start
    const int64_t origin = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0;
    if (plan.sourceStart > 0.0) {
        const int64_t ts = origin + static_cast<int64_t>(plan.sourceStart * AV_TIME_BASE);
        av_seek_frame(fmt.get(), -1, ts, AVSEEK_FLAG_BACKWARD);
    }

    const std::string trimArgs = "start=" + FormatNumber(plan.sourceStart) +
                                 (plan.segment.end > 0.0 ? ":end=" + FormatNumber(plan.sourceEnd) : std::string());
    const double speed = plan.segment.speed;

    Graph vg, ag;
    FramePtr decoded = MakeFrame();
    FramePtr scratch = MakeFrame();
    FramePtr converted;
    SwsPtr sws;
    int graphW = 0, graphH = 0, graphFmt = -1;
    int64_t audioNextPts = 0;
    AVRational vsinkTb{1, 1};

    auto emitVideo = [&](AVFrame* f) { return EmitVideo(f, vsinkTb); };
    auto emitAudio = [&](AVFrame* f) { return EmitAudio(f); };

    auto feedVideo = [&](AVFrame* frame) -> VideoFXResult {
        AVStream* st = fmt->streams[vIndex];
        if (!vg.graph) {
            const AVRational sar = frame->sample_aspect_ratio.num > 0 ? frame->sample_aspect_ratio : AVRational{1, 1};
            const std::string args = "video_size=" + std::to_string(frame->width) + "x" + std::to_string(frame->height) +
                                     ":pix_fmt=" + std::to_string(frame->format) +
                                     ":time_base=" + std::to_string(st->time_base.num) + "/" + std::to_string(st->time_base.den) +
                                     ":pixel_aspect=" + std::to_string(sar.num) + "/" + std::to_string(sar.den);
            std::string desc = "trim=" + trimArgs + ",setpts=PTS-STARTPTS";
            AppendFilter(desc, AutoRotateChain(plan.rotation));
            if (std::fabs(speed - 1.0) > 1e-6) AppendFilter(desc, "setpts=PTS/" + FormatNumber(speed));
            AppendFilter(desc, plan.videoEffects);
            AppendFilter(desc, VideoTail(sar.num != sar.den));
            VideoFXResult br = BuildGraph(vg, true, args, desc);
            if (br != VideoFXResult::Ok) return br;
            vsinkTb = av_buffersink_get_time_base(vg.sink);
            graphW = frame->width;
            graphH = frame->height;
            graphFmt = frame->format;
        }
        const int64_t ts = frame->best_effort_timestamp;
        frame->pts = ts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : ts - av_rescale_q(origin, AV_TIME_BASE_Q, st->time_base);
        AVFrame* push = frame;
        if (frame->width != graphW || frame->height != graphH || frame->format != graphFmt) {
            // Mid-stream size / format change: convert back to what the graph was built for
            sws.reset(sws_getCachedContext(sws.release(), frame->width, frame->height,
                                           static_cast<AVPixelFormat>(frame->format), graphW, graphH,
                                           static_cast<AVPixelFormat>(graphFmt), SWS_BICUBIC, nullptr, nullptr, nullptr));
            if (!sws) return Fail(VideoFXResult::DecodeError, "Cannot convert a resized frame");
            converted = MakeFrame();
            converted->width = graphW;
            converted->height = graphH;
            converted->format = graphFmt;
            if (av_frame_get_buffer(converted.get(), 0) < 0) return Fail(VideoFXResult::DecodeError, "Out of memory");
            sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, converted->data, converted->linesize);
            converted->pts = frame->pts;
            push = converted.get();
        }
        int err = av_buffersrc_add_frame_flags(vg.src, push, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot feed the effect chain", err);
        return PullGraph(vg, scratch.get(), emitVideo);
    };

    auto feedAudio = [&](AVFrame* frame) -> VideoFXResult {
        AVStream* st = fmt->streams[aIndex];
        const AVRational tb{1, frame->sample_rate};
        if (!ag.graph) {
            std::string desc = "atrim=" + trimArgs + ",asetpts=PTS-STARTPTS";
            AppendFilter(desc, AtempoChain(speed));
            AppendFilter(desc, plan.audioEffects);
            AppendFilter(desc, AudioTail());
            VideoFXResult br = BuildGraph(ag, false, AudioBufferSourceArgs(frame, tb), desc);
            if (br != VideoFXResult::Ok) return br;
        }
        const int64_t ts = frame->best_effort_timestamp;
        if (ts != AV_NOPTS_VALUE) {
            frame->pts = av_rescale_q(ts - av_rescale_q(origin, AV_TIME_BASE_Q, st->time_base), st->time_base, tb);
        } else {
            frame->pts = audioNextPts;
        }
        audioNextPts = frame->pts + frame->nb_samples;
        int err = av_buffersrc_add_frame_flags(ag.src, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot feed the audio effect chain", err);
        return PullGraph(ag, scratch.get(), emitAudio);
    };

    // Decode until both streams are past the segment end
    const double stopAfter = plan.segment.end > 0.0 ? plan.sourceEnd + 1.0 : 0.0;
    bool videoPast = vIndex < 0, audioPast = aIndex < 0;
    PacketPtr pkt = MakePacket();

    auto receiveAll = [&](AVCodecContext* dec, int index, bool& past) -> VideoFXResult {
        while (true) {
            int err = avcodec_receive_frame(dec, decoded.get());
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return VideoFXResult::Ok;
            if (err < 0) return Fail(VideoFXResult::DecodeError, "Decoding failed", err);
            AVStream* st = fmt->streams[index];
            if (stopAfter > 0.0 && decoded->best_effort_timestamp != AV_NOPTS_VALUE) {
                const double t = (decoded->best_effort_timestamp -
                                  av_rescale_q(origin, AV_TIME_BASE_Q, st->time_base)) * av_q2d(st->time_base);
                if (t > stopAfter) past = true;
            }
            VideoFXResult fr = index == vIndex ? feedVideo(decoded.get()) : feedAudio(decoded.get());
            av_frame_unref(decoded.get());
            if (fr != VideoFXResult::Ok) return fr;
        }
    };

    while (!(videoPast && audioPast)) {
        int err = av_read_frame(fmt.get(), pkt.get());
        if (err == AVERROR_EOF || err == AVERROR(EIO)) break;
        if (err < 0) return Fail(VideoFXResult::DecodeError, "Cannot read " + plan.segment.path, err);
        VideoFXResult fr = VideoFXResult::Ok;
        if (pkt->stream_index == vIndex && !videoPast) {
            err = avcodec_send_packet(vdec.get(), pkt.get());
            if (err < 0 && err != AVERROR(EAGAIN) && err != AVERROR_INVALIDDATA)
                fr = Fail(VideoFXResult::DecodeError, "Video decoding failed", err);
            else fr = receiveAll(vdec.get(), vIndex, videoPast);
        } else if (pkt->stream_index == aIndex && !audioPast) {
            err = avcodec_send_packet(adec.get(), pkt.get());
            if (err < 0 && err != AVERROR(EAGAIN) && err != AVERROR_INVALIDDATA)
                fr = Fail(VideoFXResult::DecodeError, "Audio decoding failed", err);
            else fr = receiveAll(adec.get(), aIndex, audioPast);
        }
        av_packet_unref(pkt.get());
        if (fr != VideoFXResult::Ok) return fr;
    }

    // Flush decoders, then the graphs
    if (vdec) {
        avcodec_send_packet(vdec.get(), nullptr);
        if ((r = receiveAll(vdec.get(), vIndex, videoPast)) != VideoFXResult::Ok) return r;
    }
    if (adec) {
        avcodec_send_packet(adec.get(), nullptr);
        if ((r = receiveAll(adec.get(), aIndex, audioPast)) != VideoFXResult::Ok) return r;
    }
    if (vg.src) {
        int err = av_buffersrc_add_frame_flags(vg.src, nullptr, 0);
        if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot finish the effect chain", err);
        if ((r = PullGraph(vg, scratch.get(), emitVideo)) != VideoFXResult::Ok) return r;
    }
    if (ag.src) {
        int err = av_buffersrc_add_frame_flags(ag.src, nullptr, 0);
        if (err < 0) return Fail(VideoFXResult::FilterError, "Cannot finish the audio effect chain", err);
        if ((r = PullGraph(ag, scratch.get(), emitAudio)) != VideoFXResult::Ok) return r;
    }
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::ProcessSegment(const SegmentPlan& plan) {
    // Line both streams up at the segment start
    VideoFXResult r = PadTo(segmentStart);
    if (r != VideoFXResult::Ok) return r;
    segmentVideoBase = nextVideoPts;

    if (plan.segment.kind == VideoFXSourceKind::File) {
        r = ProcessFileSegment(plan);
    } else {
        const bool tone = plan.segment.kind == VideoFXSourceKind::TestPattern && !plan.segment.mute;
        r = RunGeneratorGraphs(plan, outVideo, outAudio && tone);
    }
    if (r != VideoFXResult::Ok) return r;

    // The next segment starts where the longer stream ended
    double end = segmentStart;
    if (outVideo) end = std::max(end, nextVideoPts * av_q2d(venc->time_base));
    if (outAudio) end = std::max(end, static_cast<double>(audioSamplesQueued) / sampleRate);
    if (end <= segmentStart && plan.outDuration > 0.0) end = segmentStart + plan.outDuration;
    progressDone += plan.outDuration > 0.0 ? plan.outDuration : end - segmentStart;
    segmentStart = end;
    return VideoFXResult::Ok;
}

VideoFXResult Exporter::Finish() {
    VideoFXResult r;
    // Audio runs to the end of the picture (silence), then everything is flushed
    if (outAudio && outVideo) {
        const double videoEnd = nextVideoPts * av_q2d(venc->time_base);
        const int64_t target = static_cast<int64_t>(std::llround(videoEnd * sampleRate));
        if (target > audioSamplesQueued && (r = PadAudioSilence(target - audioSamplesQueued)) != VideoFXResult::Ok)
            return r;
    }
    if (outAudio) {
        if ((r = DrainAudioFifo(true)) != VideoFXResult::Ok) return r;
        if ((r = EncodeAudio(nullptr)) != VideoFXResult::Ok) return r;
    }
    if (outVideo && (r = EncodeVideo(nullptr)) != VideoFXResult::Ok) return r;
    int err = av_write_trailer(oc);
    if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot finish " + outputPath, err);
    headerWritten = false;
    if (ioOpened) {
        err = avio_closep(&oc->pb);
        ioOpened = false;
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot close " + outputPath, err);
    }
    if (progress) progress(1.0);
    return VideoFXResult::Ok;
}

void Exporter::Close() {
    venc.reset();
    aenc.reset();
    if (oc) {
        if (ioOpened) avio_closep(&oc->pb);
        avformat_free_context(oc);
        oc = nullptr;
    }
    ioOpened = false;
}

VideoFXResult Exporter::Run(const std::vector<VideoFXSegment>& segments) {
    EnsureBackendInitialised();
    ClearError();
    if (outputPath.empty()) return Fail(VideoFXResult::InvalidArgument, "No output file");
    VideoFXResult r = PlanSegments(segments);
    if (r == VideoFXResult::Ok) r = PlanOutput();
    if (r != VideoFXResult::Ok) return r;      // nothing written yet

    r = OpenOutput();
    for (size_t i = 0; r == VideoFXResult::Ok && i < plans.size(); ++i) r = ProcessSegment(plans[i]);
    if (r == VideoFXResult::Ok) r = Finish();

    if (r != VideoFXResult::Ok) {
        const std::string reason = VideoFX_GetLastError();
        Close();
        std::error_code ec;
        std::filesystem::remove(UltraCanvas::PathFromUtf8(outputPath), ec);   // no half-written files
        Fail(r, reason);
        return r;
    }
    Close();
    return VideoFXResult::Ok;
}

} // namespace

// ============================================================================
// PUBLIC CALLS
// ============================================================================

VideoFXResult VideoFX_Export(const std::vector<VideoFXSegment>& segments, const std::string& outputPath,
                             const VideoFXExportSettings& settings, const VideoFXProgressCallback& progress) {
    Exporter exporter(outputPath, settings, progress);
    return exporter.Run(segments);
}

VideoFXResult VideoFX_Transcode(const std::string& inputPath, const std::string& outputPath,
                                const VideoFXExportSettings& settings, const VideoFXProgressCallback& progress) {
    return VideoFX_Export({VideoFXSegment::FromFile(inputPath)}, outputPath, settings, progress);
}

VideoFXResult VideoFX_Trim(const std::string& inputPath, const std::string& outputPath, double start, double end,
                           const VideoFXExportSettings& settings, const VideoFXProgressCallback& progress) {
    return VideoFX_Export({VideoFXSegment::FromFile(inputPath, start, end)}, outputPath, settings, progress);
}

VideoFXResult VideoFX_ApplyEffects(const std::string& inputPath, const std::string& outputPath,
                                   const std::vector<VideoFXEffect>& effects,
                                   const VideoFXExportSettings& settings, const VideoFXProgressCallback& progress) {
    VideoFXSegment segment = VideoFXSegment::FromFile(inputPath);
    segment.effects = effects;
    return VideoFX_Export({segment}, outputPath, settings, progress);
}

VideoFXResult VideoFX_Concatenate(const std::vector<std::string>& inputPaths, const std::string& outputPath,
                                  const VideoFXExportSettings& settings, const VideoFXProgressCallback& progress) {
    std::vector<VideoFXSegment> segments;
    segments.reserve(inputPaths.size());
    for (const std::string& p : inputPaths) segments.push_back(VideoFXSegment::FromFile(p));
    return VideoFX_Export(segments, outputPath, settings, progress);
}

VideoFXResult VideoFX_ExtractAudio(const std::string& inputPath, const std::string& outputPath,
                                   const VideoFXExportSettings& settings, const VideoFXProgressCallback& progress) {
    VideoFXExportSettings audioOnly = settings;
    audioOnly.videoCodec = VideoFXVideoCodec::Disabled;
    return VideoFX_Export({VideoFXSegment::FromFile(inputPath)}, outputPath, audioOnly, progress);
}

VideoFXResult VideoFX_GenerateTestClip(const std::string& outputPath, double seconds, int width, int height,
                                       double frameRate, bool withAudio) {
    VideoFXSegment segment = VideoFXSegment::TestPattern(seconds);
    segment.mute = !withAudio;
    VideoFXExportSettings settings;
    settings.width = width;
    settings.height = height;
    settings.frameRate = frameRate;
    return VideoFX_Export({segment}, outputPath, settings);
}

} // namespace VideoFX
