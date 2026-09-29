// VideoFX/core/VideoFXProbe.cpp
// Media inspection: VideoFX_Probe, frame and thumbnail extraction, saving a
// frame as PNG / JPEG.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXBackend.h"
#include "VideoFX/VideoFX.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8, OpenFileUtf8

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace VideoFX {

using namespace Internal;

namespace {

double ToSeconds(int64_t ts, AVRational tb) {
    return ts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(ts) * av_q2d(tb);
}

VideoFXStreamKind KindOf(AVMediaType type) {
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:      return VideoFXStreamKind::Video;
        case AVMEDIA_TYPE_AUDIO:      return VideoFXStreamKind::Audio;
        case AVMEDIA_TYPE_SUBTITLE:   return VideoFXStreamKind::Subtitle;
        case AVMEDIA_TYPE_DATA:       return VideoFXStreamKind::Data;
        case AVMEDIA_TYPE_ATTACHMENT: return VideoFXStreamKind::Attachment;
        default:                      return VideoFXStreamKind::Unknown;
    }
}

std::string LowerExtension(const std::string& path) {
    std::string ext = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// ===== single-frame decoding =====

struct OpenVideo {
    FormatInputPtr fmt;
    CodecContextPtr dec;
    int stream = -1;
    int rotation = 0;
    AVRational tb{1, 1};
    int64_t startTs = 0;          // stream timestamps are relative to this
    double duration = 0.0;
};

VideoFXResult OpenVideoStream(const std::string& path, OpenVideo& v) {
    VideoFXResult r = OpenInput(path, v.fmt);
    if (r != VideoFXResult::Ok) return r;
    v.stream = av_find_best_stream(v.fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v.stream < 0) return Fail(VideoFXResult::NoMediaStreams, "No video stream in " + path);
    r = OpenDecoder(v.fmt.get(), v.stream, v.dec);
    if (r != VideoFXResult::Ok) return r;
    AVStream* st = v.fmt->streams[v.stream];
    v.tb = st->time_base;
    v.rotation = GetStreamRotation(st);
    v.startTs = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    if (st->duration != AV_NOPTS_VALUE) v.duration = ToSeconds(st->duration, st->time_base);
    else if (v.fmt->duration != AV_NOPTS_VALUE) v.duration = v.fmt->duration / static_cast<double>(AV_TIME_BASE);
    return VideoFXResult::Ok;
}

// Decode the frame displayed at `seconds` into `out`
VideoFXResult DecodeFrameAt(OpenVideo& v, double seconds, AVFrame* out, double& frameTime) {
    AVStream* st = v.fmt->streams[v.stream];
    const bool attachedPicture = (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
    seconds = std::max(0.0, seconds);
    if (v.duration > 0.0) seconds = std::min(seconds, v.duration);

    if (!attachedPicture) {
        const int64_t target = v.startTs + static_cast<int64_t>(seconds / av_q2d(v.tb));
        if (av_seek_frame(v.fmt.get(), v.stream, target, AVSEEK_FLAG_BACKWARD) < 0)
            av_seek_frame(v.fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);   // unseekable: from the start
        avcodec_flush_buffers(v.dec.get());
    }

    const double halfFrame = st->avg_frame_rate.num > 0 ? 0.5 / av_q2d(st->avg_frame_rate) : 0.02;
    PacketPtr pkt = MakePacket();
    FramePtr frame = MakeFrame();
    bool haveFrame = false;
    bool draining = false;

    while (true) {
        if (!draining) {
            int err = av_read_frame(v.fmt.get(), pkt.get());
            if (err < 0) {
                draining = true;
                avcodec_send_packet(v.dec.get(), nullptr);
            } else {
                if (pkt->stream_index == v.stream) avcodec_send_packet(v.dec.get(), pkt.get());
                av_packet_unref(pkt.get());
            }
        }
        while (true) {
            int err = avcodec_receive_frame(v.dec.get(), frame.get());
            if (err == AVERROR(EAGAIN)) break;
            if (err < 0) {
                if (haveFrame) return VideoFXResult::Ok;
                return Fail(VideoFXResult::DecodeError, "No frame at the requested time");
            }
            const int64_t ts = frame->best_effort_timestamp;
            const double t = ts == AV_NOPTS_VALUE ? seconds : ToSeconds(ts - v.startTs, v.tb);
            av_frame_unref(out);
            av_frame_move_ref(out, frame.get());
            frameTime = t;
            haveFrame = true;
            if (attachedPicture || t >= seconds - halfFrame) return VideoFXResult::Ok;
        }
        if (draining && !haveFrame) return Fail(VideoFXResult::DecodeError, "No frame at the requested time");
    }
}

// Convert a decoded frame to upright RGBA no larger than maxWidth x maxHeight
VideoFXResult ConvertToRgba(const AVFrame* src, int rotation, int maxWidth, int maxHeight, VideoFXFrame& dst) {
    // Display size: sample aspect ratio applied, then rotation
    double dispW = src->width, dispH = src->height;
    if (src->sample_aspect_ratio.num > 0 && src->sample_aspect_ratio.den > 0)
        dispW *= av_q2d(src->sample_aspect_ratio);
    const bool quarterTurn = rotation == 90 || rotation == 270;
    if (quarterTurn) std::swap(dispW, dispH);

    double scale = 1.0;
    if (maxWidth > 0) scale = std::min(scale, maxWidth / dispW);
    if (maxHeight > 0) scale = std::min(scale, maxHeight / dispH);
    int outW = std::max(1, static_cast<int>(std::lround(dispW * scale)));
    int outH = std::max(1, static_cast<int>(std::lround(dispH * scale)));

    // Scale in the stored orientation, rotate afterwards
    const int scaledW = quarterTurn ? outH : outW;
    const int scaledH = quarterTurn ? outW : outH;
    SwsPtr sws(sws_getContext(src->width, src->height, static_cast<AVPixelFormat>(src->format),
                              scaledW, scaledH, AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!sws) return Fail(VideoFXResult::DecodeError, "Cannot convert the frame's pixel format");

    std::vector<uint8_t> scaled(static_cast<size_t>(scaledW) * scaledH * 4);
    uint8_t* dstData[4] = {scaled.data(), nullptr, nullptr, nullptr};
    int dstLines[4] = {scaledW * 4, 0, 0, 0};
    sws_scale(sws.get(), src->data, src->linesize, 0, src->height, dstData, dstLines);

    dst.width = outW;
    dst.height = outH;
    if (rotation == 0) {
        dst.pixels = std::move(scaled);
        return VideoFXResult::Ok;
    }
    dst.pixels.assign(static_cast<size_t>(outW) * outH * 4, 0);
    const uint32_t* in = reinterpret_cast<const uint32_t*>(scaled.data());
    uint32_t* o = reinterpret_cast<uint32_t*>(dst.pixels.data());
    for (int y = 0; y < scaledH; ++y) {
        for (int x = 0; x < scaledW; ++x) {
            int dx, dy;
            if (rotation == 90)       { dx = scaledH - 1 - y; dy = x; }
            else if (rotation == 180) { dx = scaledW - 1 - x; dy = scaledH - 1 - y; }
            else                      { dx = y;               dy = scaledW - 1 - x; }
            o[static_cast<size_t>(dy) * outW + dx] = in[static_cast<size_t>(y) * scaledW + x];
        }
    }
    return VideoFXResult::Ok;
}

} // namespace

// ============================================================================
// PROBE
// ============================================================================

VideoFXResult VideoFX_Probe(const std::string& path, VideoFXMediaInfo& info) {
    ClearError();
    info = VideoFXMediaInfo{};
    FormatInputPtr fmt;
    VideoFXResult r = OpenInput(path, fmt);
    if (r != VideoFXResult::Ok) return r;

    info.path = path;
    if (fmt->iformat) {
        info.formatName = fmt->iformat->name ? fmt->iformat->name : "";
        info.formatLongName = fmt->iformat->long_name ? fmt->iformat->long_name : "";
    }
    if (fmt->duration != AV_NOPTS_VALUE) info.duration = fmt->duration / static_cast<double>(AV_TIME_BASE);
    info.bitRate = fmt->bit_rate;
    std::error_code ec;
    const auto size = std::filesystem::file_size(UltraCanvas::PathFromUtf8(path), ec);
    if (!ec) info.fileSize = static_cast<int64_t>(size);

    const AVDictionaryEntry* tag = nullptr;
    while ((tag = av_dict_get(fmt->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        info.metadata[tag->key] = tag->value;

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream* st = fmt->streams[i];
        const AVCodecParameters* par = st->codecpar;
        VideoFXStreamInfo s;
        s.index = static_cast<int>(i);
        s.kind = KindOf(par->codec_type);
        s.codecName = avcodec_get_name(par->codec_id);
        if (const AVCodecDescriptor* d = avcodec_descriptor_get(par->codec_id))
            s.codecLongName = d->long_name ? d->long_name : "";
        if (const AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0))
            s.language = lang->value;
        if (st->duration != AV_NOPTS_VALUE) s.duration = ToSeconds(st->duration, st->time_base);
        s.bitRate = par->bit_rate;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            s.width = par->width;
            s.height = par->height;
            AVRational rate = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
            if (rate.num > 0 && rate.den > 0) s.frameRate = av_q2d(rate);
            s.rotation = GetStreamRotation(st);
            if (const char* pf = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) s.pixelFormat = pf;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            s.sampleRate = par->sample_rate;
            s.channels = GetChannels(par);
            s.channelLayout = DescribeChannelLayout(par);
            if (const char* sf = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format))) s.sampleFormat = sf;
        }
        info.streams.push_back(s);
    }

    // Default streams: FFmpeg's own choice, skipping cover art for "video"
    int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v >= 0 && (fmt->streams[v]->disposition & AV_DISPOSITION_ATTACHED_PIC)) v = -1;
    const int a = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (v >= 0) {
        const VideoFXStreamInfo& s = info.streams[v];
        info.videoStreamIndex = v;
        const bool quarterTurn = s.rotation == 90 || s.rotation == 270;
        info.width = quarterTurn ? s.height : s.width;
        info.height = quarterTurn ? s.width : s.height;
        info.frameRate = s.frameRate;
    }
    if (a >= 0) {
        info.audioStreamIndex = a;
        info.sampleRate = info.streams[a].sampleRate;
        info.channels = info.streams[a].channels;
    }
    if (info.duration <= 0.0) {
        for (const auto& s : info.streams) info.duration = std::max(info.duration, s.duration);
    }
    if (!info.HasVideo() && !info.HasAudio())
        return Fail(VideoFXResult::NoMediaStreams, "No audio or video stream in " + path);
    return VideoFXResult::Ok;
}

// ============================================================================
// FRAMES
// ============================================================================

VideoFXResult VideoFX_ExtractFrame(const std::string& path, double seconds, VideoFXFrame& frame,
                                   int maxWidth, int maxHeight) {
    ClearError();
    frame = VideoFXFrame{};
    if (!std::isfinite(seconds)) return Fail(VideoFXResult::InvalidArgument, "Time is not a number");
    OpenVideo v;
    VideoFXResult r = OpenVideoStream(path, v);
    if (r != VideoFXResult::Ok) return r;
    FramePtr decoded = MakeFrame();
    double t = 0.0;
    r = DecodeFrameAt(v, seconds, decoded.get(), t);
    if (r != VideoFXResult::Ok) return r;
    r = ConvertToRgba(decoded.get(), v.rotation, maxWidth, maxHeight, frame);
    frame.timestamp = t;
    return r;
}

VideoFXResult VideoFX_ExtractThumbnails(const std::string& path, int count, std::vector<VideoFXFrame>& frames,
                                        int maxWidth, int maxHeight) {
    ClearError();
    frames.clear();
    if (count <= 0 || count > 10000) return Fail(VideoFXResult::InvalidArgument, "Thumbnail count must be 1..10000");
    OpenVideo v;
    VideoFXResult r = OpenVideoStream(path, v);
    if (r != VideoFXResult::Ok) return r;
    FramePtr decoded = MakeFrame();
    for (int i = 0; i < count; ++i) {
        // Centre of each of `count` equal slices, so neither end is a black frame
        const double t = v.duration > 0.0 ? v.duration * (i + 0.5) / count : 0.0;
        double frameTime = 0.0;
        r = DecodeFrameAt(v, t, decoded.get(), frameTime);
        if (r != VideoFXResult::Ok) return r;
        VideoFXFrame f;
        r = ConvertToRgba(decoded.get(), v.rotation, maxWidth, maxHeight, f);
        if (r != VideoFXResult::Ok) return r;
        f.timestamp = frameTime;
        frames.push_back(std::move(f));
    }
    return VideoFXResult::Ok;
}

VideoFXResult VideoFX_SaveFrameImage(const VideoFXFrame& frame, const std::string& path) {
    ClearError();
    EnsureBackendInitialised();
    if (!frame.IsValid()) return Fail(VideoFXResult::InvalidArgument, "Frame is empty");
    const std::string ext = LowerExtension(path);
    const bool jpeg = ext == ".jpg" || ext == ".jpeg";
    if (!jpeg && ext != ".png") return Fail(VideoFXResult::InvalidArgument, "Frame images are .png or .jpg");

    const AVCodec* codec = avcodec_find_encoder(jpeg ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_PNG);
    if (!codec) return Fail(VideoFXResult::EncoderNotAvailable, jpeg ? "No JPEG encoder" : "No PNG encoder");

    AVPixelFormat pixFmt = AV_PIX_FMT_RGBA;
    if (jpeg) {
        pixFmt = AV_PIX_FMT_YUV420P;
        if (const AVPixelFormat* fmts = SupportedPixelFormats(codec)) {
            for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
                if (*p == AV_PIX_FMT_YUVJ420P) { pixFmt = AV_PIX_FMT_YUVJ420P; break; }
        }
    }

    CodecContextPtr enc(avcodec_alloc_context3(codec));
    if (!enc) return Fail(VideoFXResult::EncodeError, "Out of memory");
    enc->width = frame.width;
    enc->height = frame.height;
    enc->pix_fmt = pixFmt;
    enc->time_base = AVRational{1, 25};
    if (jpeg) {
        enc->color_range = AVCOL_RANGE_JPEG;
        enc->flags |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * 2;
    }
    int err = avcodec_open2(enc.get(), codec, nullptr);
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Cannot open image encoder", err);

    FramePtr f = MakeFrame();
    f->format = pixFmt;
    f->width = frame.width;
    f->height = frame.height;
    if (jpeg) f->color_range = AVCOL_RANGE_JPEG;
    err = av_frame_get_buffer(f.get(), 0);
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Out of memory", err);

    const uint8_t* srcData[4] = {frame.pixels.data(), nullptr, nullptr, nullptr};
    const int srcLines[4] = {frame.width * 4, 0, 0, 0};
    SwsPtr sws(sws_getContext(frame.width, frame.height, AV_PIX_FMT_RGBA, frame.width, frame.height, pixFmt,
                              SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!sws) return Fail(VideoFXResult::EncodeError, "Cannot convert the frame");
    sws_scale(sws.get(), srcData, srcLines, 0, frame.height, f->data, f->linesize);
    f->pts = 0;

    std::vector<uint8_t> bytes;
    PacketPtr pkt = MakePacket();
    avcodec_send_frame(enc.get(), f.get());
    avcodec_send_frame(enc.get(), nullptr);
    while (avcodec_receive_packet(enc.get(), pkt.get()) >= 0) {
        bytes.insert(bytes.end(), pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt.get());
    }
    if (bytes.empty()) return Fail(VideoFXResult::EncodeError, "The image encoder produced nothing");

    std::FILE* out = UltraCanvas::OpenFileUtf8(path, "wb");
    if (!out) return Fail(VideoFXResult::WriteError, "Cannot create " + path);
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != bytes.size() || !closed) return Fail(VideoFXResult::WriteError, "Cannot write " + path);
    return VideoFXResult::Ok;
}

} // namespace VideoFX
