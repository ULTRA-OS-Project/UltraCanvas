// VideoFX/core/VideoFXRecorder.cpp
// VideoFXFrameRecorder: a live source's frames, pushed one by one with their
// time, encoded into a file as they arrive. The encoder is chosen and set up
// as an export's is (the same quality, preset and hardware rules); the frame
// clock is the caller's, mapped onto the file's rate.
// Version: 0.17.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"
#include "VideoFXBackend.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace VideoFX {

using namespace Internal;

namespace {

const char* RecorderMuxerName(VideoFXContainer c) {
    switch (c) {
        case VideoFXContainer::MP4:  return "mp4";
        case VideoFXContainer::MOV:  return "mov";
        case VideoFXContainer::MKV:  return "matroska";
        case VideoFXContainer::WebM: return "webm";
        case VideoFXContainer::AVI:  return "avi";
        default:                     return nullptr;
    }
}

const char* RecorderCodecName(VideoFXVideoCodec c) {
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
        default:                        return "video";
    }
}

// The container's usual codecs, most compatible first (as an export chooses)
std::vector<VideoFXVideoCodec> RecorderDefaultCodecs(const std::string& muxer) {
    if (muxer == "webm") return {VideoFXVideoCodec::VP9, VideoFXVideoCodec::VP8, VideoFXVideoCodec::AV1};
    if (muxer == "avi")  return {VideoFXVideoCodec::MPEG4, VideoFXVideoCodec::MJPEG};
    return {VideoFXVideoCodec::H264, VideoFXVideoCodec::MPEG4};
}

int EvenDown(int v) { return std::max(2, v & ~1); }

} // namespace

struct VideoFXFrameRecorder::Impl {
    std::string outputPath;
    VideoFXRecorderSettings settings;
    bool open = false;              // Open() succeeded: the file is being written, or waits for a size
    bool encoderOpen = false;       // the encoder and the file header exist
    bool ioOpened = false;

    AVFormatContext* oc = nullptr;
    const AVOutputFormat* ofmt = nullptr;
    std::string muxer;
    const AVCodec* vcodec = nullptr;
    CodecContextPtr venc;
    AVStream* vst = nullptr;
    AVPixelFormat pixFmt = AV_PIX_FMT_YUV420P;
    AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
    BufferRefPtr hwDevice, hwFrames;
    FramePtr hwFrame;
    FramePtr frame;                 // the frame handed to the encoder, in pixFmt
    SwsPtr sws;
    int swsSrcW = 0, swsSrcH = 0;
    AVPixelFormat swsSrcFmt = AV_PIX_FMT_NONE;
    AVRational frameRate{30, 1};
    int width = 0, height = 0;
    int64_t lastPts = -1;
    int64_t frames = 0;

    VideoFXResult PlanOutput();
    VideoFXResult OpenEncoder(int srcWidth, int srcHeight);
    VideoFXResult WritePackets();
    VideoFXResult Encode(AVFrame* f);
    VideoFXResult Push(const uint8_t* pixels, int w, int h, int stride, double seconds, VideoFXPixelLayout layout);
    VideoFXResult Finish();
    void Release();
};

VideoFXResult VideoFXFrameRecorder::Impl::PlanOutput() {
    if (settings.container == VideoFXContainer::Auto) {
        ofmt = av_guess_format(nullptr, outputPath.c_str(), nullptr);
        if (!ofmt) return Fail(VideoFXResult::InvalidArgument, "No known format for the extension of " + outputPath);
    } else {
        const char* name = RecorderMuxerName(settings.container);
        ofmt = name ? av_guess_format(name, nullptr, nullptr) : nullptr;
        if (!ofmt) return Fail(VideoFXResult::UnsupportedFormat, "A recording needs a video container (MP4, MOV, MKV, WebM, AVI)");
    }
    muxer = ofmt->name;
    if (muxer == "gif" || muxer == "mp3" || muxer == "ipod" || muxer == "wav" || muxer == "flac" || muxer == "ogg")
        return Fail(VideoFXResult::UnsupportedFormat, "A recording cannot be written as " + muxer);

    std::vector<VideoFXVideoCodec> candidates;
    if (settings.videoCodec == VideoFXVideoCodec::Auto) candidates = RecorderDefaultCodecs(muxer);
    else candidates = {settings.videoCodec};
    for (VideoFXVideoCodec c : candidates) {
        if ((vcodec = FindVideoEncoder(c, settings.acceleration))) break;
    }
    if (!vcodec) {
        const VideoFXAcceleration a = settings.acceleration;
        if (a == VideoFXAcceleration::Auto || a == VideoFXAcceleration::Hardware)
            return Fail(VideoFXResult::EncoderNotAvailable,
                        std::string("No ") + RecorderCodecName(candidates.front()) + " encoder in this build");
        std::string built;
        for (const EncoderCandidate& e : VideoEncoderCandidates(candidates.front()))
            if (e.kind == a && FindEncoderByName(e.name)) built = e.name;
        const std::string what = std::string(VideoFX_AccelerationName(a)) + " " + RecorderCodecName(candidates.front());
        return Fail(VideoFXResult::EncoderNotAvailable,
                    built.empty() ? "No " + what + " encoder in this FFmpeg build"
                                  : "The " + what + " encoder (" + built + ") does not work on this machine: "
                                    "no such graphics hardware or driver");
    }
    if (avformat_query_codec(ofmt, vcodec->id, FF_COMPLIANCE_NORMAL) == 0)
        return Fail(VideoFXResult::UnsupportedFormat, std::string(vcodec->name) + " cannot be stored in " + muxer);
    return VideoFXResult::Ok;
}

VideoFXResult VideoFXFrameRecorder::Impl::OpenEncoder(int srcWidth, int srcHeight) {
    width = EvenDown(settings.width > 0 ? settings.width : srcWidth);
    height = EvenDown(settings.height > 0 ? settings.height : srcHeight);

    int err = avformat_alloc_output_context2(&oc, const_cast<AVOutputFormat*>(ofmt), nullptr, outputPath.c_str());
    if (err < 0 || !oc) return Fail(VideoFXResult::WriteError, "Cannot create the output container", err);

    venc.reset(avcodec_alloc_context3(vcodec));
    if (!venc) return Fail(VideoFXResult::EncodeError, "Out of memory");
    AVCodecContext* c = venc.get();
    const std::string name = vcodec->name;

    // Pixel format, as the exporter picks it: 4:2:0 where the encoder takes
    // it, else its first system-memory format
    AVPixelFormat preferred = AV_PIX_FMT_YUV420P;
    if (name.rfind("prores", 0) == 0) preferred = AV_PIX_FMT_YUV422P10LE;
    else if (name == "mjpeg") preferred = AV_PIX_FMT_YUVJ420P;
    pixFmt = preferred;
    if (const AVPixelFormat* fmts = SupportedPixelFormats(vcodec)) {
        bool found = false;
        for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == preferred) { found = true; break; }
        if (!found) {
            pixFmt = fmts[0];
            for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
                const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*p);
                if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) { pixFmt = *p; break; }
            }
            for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
                if (*p == AV_PIX_FMT_YUV420P || *p == AV_PIX_FMT_NV12) { pixFmt = *p; break; }
        }
    }
    if (NeedsHardwareFrames(vcodec, &hwFormat)) pixFmt = AV_PIX_FMT_NV12;

    c->width = width;
    c->height = height;
    c->pix_fmt = pixFmt;
    if (hwFormat != AV_PIX_FMT_NONE) {
        const int herr = SetUpHardwareFrames(c, hwFormat, hwDevice, hwFrames);
        if (herr < 0) return Fail(VideoFXResult::EncoderNotAvailable,
                                  std::string("Cannot open the graphics device for ") + name, herr);
        c->pix_fmt = hwFormat;
        hwFrame = MakeFrame();
        if (!hwFrame) return Fail(VideoFXResult::EncodeError, "Out of memory");
    }
    c->sample_aspect_ratio = AVRational{1, 1};
    c->time_base = av_inv_q(frameRate);
    c->framerate = frameRate;
    c->thread_count = settings.threads;
    if (name == "mjpeg") c->color_range = AVCOL_RANGE_JPEG;
    if (ofmt->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

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
        av_opt_set(c->priv_data, "x265-params", "log-level=error", 0);
    } else if (name == "libvpx-vp9" || name == "libaom-av1") {
        c->bit_rate = 0;
        av_opt_set_int(c->priv_data, "crf", crfFor(name == "libaom-av1" ? 32 : 33, 63.0), 0);
    } else if (name == "libsvtav1") {
        av_opt_set_int(c->priv_data, "crf", crfFor(35, 63.0), 0);
    } else if (name == "libvpx") {
        c->bit_rate = std::max<int64_t>(500000, pixelRate / 5);
        av_opt_set_int(c->priv_data, "crf", crfFor(10, 63.0), 0);
    } else if (name == "mpeg4" || name == "mjpeg") {
        const int qs = q < 0 ? 3 : std::clamp(static_cast<int>(std::lround(31 - q * 0.30)), 1, 31);
        c->flags |= AV_CODEC_FLAG_QSCALE;
        c->global_quality = FF_QP2LAMBDA * qs;
    } else if (name == "prores_ks") {
        av_opt_set(c->priv_data, "profile", q >= 80 ? "hq" : "standard", 0);
    } else if (name != "ffv1" && name.rfind("prores", 0) != 0) {
        const double factor = q < 0 ? 0.1 : 0.03 + q * 0.0017;
        c->bit_rate = std::max<int64_t>(300000, static_cast<int64_t>(pixelRate * factor));
    }
    if (name == "libaom-av1") {
        av_opt_set_int(c->priv_data, "cpu-used", 8, 0);   // a live source: the fastest it has
        av_opt_set_int(c->priv_data, "row-mt", 1, 0);
    }
    if (name == "libvpx-vp9") {
        av_opt_set_int(c->priv_data, "row-mt", 1, 0);
        av_opt_set_int(c->priv_data, "cpu-used", 8, 0);
        av_opt_set(c->priv_data, "deadline", "realtime", 0);
    }
    if (name == "libx264" || name == "libx265") {
        // A live source cannot be read ahead: no look-ahead, so a frame leaves
        // the encoder as soon as it is in
        av_opt_set(c->priv_data, "tune", "zerolatency", 0);
    }
    if (!settings.encoderPreset.empty()) av_opt_set(c->priv_data, "preset", settings.encoderPreset.c_str(), 0);
    if (settings.keyframeInterval > 0.0) {
        c->gop_size = std::max(1, static_cast<int>(std::lround(settings.keyframeInterval * fps)));
        if (c->gop_size < 3) c->max_b_frames = 0;
    }

    err = avcodec_open2(c, vcodec, nullptr);
    if (err < 0) return Fail(VideoFXResult::EncodeError, std::string("Cannot open the ") + name + " encoder", err);

    vst = avformat_new_stream(oc, nullptr);
    if (!vst) return Fail(VideoFXResult::EncodeError, "Out of memory");
    err = avcodec_parameters_from_context(vst->codecpar, c);
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Cannot set up the video stream", err);
    vst->time_base = c->time_base;
    vst->avg_frame_rate = frameRate;

    frame = MakeFrame();
    if (!frame) return Fail(VideoFXResult::EncodeError, "Out of memory");
    frame->format = pixFmt;
    frame->width = width;
    frame->height = height;
    frame->sample_aspect_ratio = AVRational{1, 1};
    err = av_frame_get_buffer(frame.get(), 0);
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Out of memory", err);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        err = avio_open(&oc->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot create " + outputPath, err);
        ioOpened = true;
    }
    AVDictionary* opts = nullptr;
    // A recording that is cut short (the program dies) must still play: the
    // fragmented layout writes a complete file every keyframe, where plain
    // MP4 holds its index for the trailer
    if (muxer == "mp4" || muxer == "mov")
        av_dict_set(&opts, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
    err = avformat_write_header(oc, &opts);
    av_dict_free(&opts);
    if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot write the file header", err);
    encoderOpen = true;
    return VideoFXResult::Ok;
}

VideoFXResult VideoFXFrameRecorder::Impl::WritePackets() {
    PacketPtr pkt = MakePacket();
    while (true) {
        int err = avcodec_receive_packet(venc.get(), pkt.get());
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return VideoFXResult::Ok;
        if (err < 0) return Fail(VideoFXResult::EncodeError, "Encoding failed", err);
        av_packet_rescale_ts(pkt.get(), venc->time_base, vst->time_base);
        pkt->stream_index = vst->index;
        err = av_interleaved_write_frame(oc, pkt.get());
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot write " + outputPath, err);
    }
}

VideoFXResult VideoFXFrameRecorder::Impl::Encode(AVFrame* f) {
    if (f && hwFrames) {
        av_frame_unref(hwFrame.get());
        int herr = av_hwframe_get_buffer(hwFrames.get(), hwFrame.get(), 0);
        if (herr >= 0) herr = av_hwframe_transfer_data(hwFrame.get(), f, 0);
        if (herr >= 0) herr = av_frame_copy_props(hwFrame.get(), f);
        if (herr < 0) return Fail(VideoFXResult::EncodeError, "Cannot upload a frame to the graphics device", herr);
        f = hwFrame.get();
    }
    int err = avcodec_send_frame(venc.get(), f);
    if (err < 0 && err != AVERROR_EOF) return Fail(VideoFXResult::EncodeError, "Video encoding failed", err);
    return WritePackets();
}

VideoFXResult VideoFXFrameRecorder::Impl::Push(const uint8_t* pixels, int w, int h, int stride, double seconds,
                                                VideoFXPixelLayout layout) {
    if (!open) return Fail(VideoFXResult::InvalidArgument, "The recorder is not open");
    if (!pixels || w <= 0 || h <= 0 || stride < w * 4)
        return Fail(VideoFXResult::InvalidArgument, "An empty frame cannot be recorded");
    if (!(seconds >= 0.0)) seconds = 0.0;
    if (!encoderOpen) {
        VideoFXResult r = OpenEncoder(w, h);
        if (r != VideoFXResult::Ok) return r;
    }
    const int64_t pts = static_cast<int64_t>(std::llround(seconds * av_q2d(frameRate)));
    if (pts <= lastPts) return VideoFXResult::Ok;     // the tick is written: a duplicate

    const AVPixelFormat srcFmt = layout == VideoFXPixelLayout::BGRA ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA;
    if (!sws || swsSrcW != w || swsSrcH != h || swsSrcFmt != srcFmt) {
        sws.reset(sws_getContext(w, h, srcFmt, width, height, pixFmt, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!sws) return Fail(VideoFXResult::EncodeError, "Cannot convert the frame for the encoder");
        swsSrcW = w;
        swsSrcH = h;
        swsSrcFmt = srcFmt;
    }
    int err = av_frame_make_writable(frame.get());
    if (err < 0) return Fail(VideoFXResult::EncodeError, "Out of memory", err);
    const uint8_t* srcData[4] = {pixels, nullptr, nullptr, nullptr};
    const int srcStride[4] = {stride, 0, 0, 0};
    sws_scale(sws.get(), srcData, srcStride, 0, h, frame->data, frame->linesize);
    frame->pts = pts;
    frame->pict_type = AV_PICTURE_TYPE_NONE;
    VideoFXResult r = Encode(frame.get());
    if (r != VideoFXResult::Ok) return r;
    lastPts = pts;
    ++frames;
    return VideoFXResult::Ok;
}

VideoFXResult VideoFXFrameRecorder::Impl::Finish() {
    if (!encoderOpen) return VideoFXResult::Ok;
    VideoFXResult r = Encode(nullptr);
    if (r != VideoFXResult::Ok) return r;
    int err = av_write_trailer(oc);
    if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot finish " + outputPath, err);
    if (ioOpened) {
        err = avio_closep(&oc->pb);
        ioOpened = false;
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot close " + outputPath, err);
    }
    return VideoFXResult::Ok;
}

void VideoFXFrameRecorder::Impl::Release() {
    sws.reset();
    frame.reset();
    hwFrame.reset();
    hwFrames.reset();
    hwDevice.reset();
    venc.reset();
    if (oc) {
        if (ioOpened) avio_closep(&oc->pb);
        avformat_free_context(oc);
        oc = nullptr;
    }
    ioOpened = false;
    encoderOpen = false;
    open = false;
    vst = nullptr;
    vcodec = nullptr;
    ofmt = nullptr;
    hwFormat = AV_PIX_FMT_NONE;
    swsSrcW = swsSrcH = 0;
    swsSrcFmt = AV_PIX_FMT_NONE;
}

VideoFXFrameRecorder::VideoFXFrameRecorder() : impl(new Impl) {}
VideoFXFrameRecorder::~VideoFXFrameRecorder() { Close(); }

VideoFXResult VideoFXFrameRecorder::Open(const std::string& outputPath, const VideoFXRecorderSettings& settings) {
    EnsureBackendInitialised();
    Close();
    ClearError();
    if (outputPath.empty()) return Fail(VideoFXResult::InvalidArgument, "No output path");
    if (!(settings.frameRate > 0.0)) return Fail(VideoFXResult::InvalidArgument, "The frame rate must be above 0");
    impl->outputPath = outputPath;
    impl->settings = settings;
    impl->frameRate = av_d2q(settings.frameRate, 100000);
    impl->lastPts = -1;
    impl->frames = 0;
    impl->width = impl->height = 0;
    VideoFXResult r = impl->PlanOutput();
    if (r != VideoFXResult::Ok) { impl->Release(); return r; }
    impl->open = true;
    if (settings.width > 0 && settings.height > 0) {
        r = impl->OpenEncoder(settings.width, settings.height);
        if (r != VideoFXResult::Ok) { impl->Release(); return r; }
    }
    return VideoFXResult::Ok;
}

VideoFXResult VideoFXFrameRecorder::PushFrame(const uint8_t* pixels, int width, int height, int stride,
                                              double seconds, VideoFXPixelLayout layout) {
    return impl->Push(pixels, width, height, stride, seconds, layout);
}

VideoFXResult VideoFXFrameRecorder::PushFrame(const VideoFXFrame& frame) {
    if (!frame.IsValid()) return Fail(VideoFXResult::InvalidArgument, "An empty frame cannot be recorded");
    return impl->Push(frame.pixels.data(), frame.width, frame.height, frame.width * 4, frame.timestamp,
                      VideoFXPixelLayout::RGBA);
}

VideoFXResult VideoFXFrameRecorder::Close() {
    if (!impl->open) return VideoFXResult::Ok;
    VideoFXResult r = impl->Finish();
    impl->Release();
    return r;
}

bool VideoFXFrameRecorder::IsOpen() const { return impl->open; }
int64_t VideoFXFrameRecorder::GetFrameCount() const { return impl->frames; }
double VideoFXFrameRecorder::GetDuration() const {
    return impl->lastPts < 0 ? 0.0 : static_cast<double>(impl->lastPts + 1) * av_q2d(av_inv_q(impl->frameRate));
}
int VideoFXFrameRecorder::GetWidth() const { return impl->width; }
int VideoFXFrameRecorder::GetHeight() const { return impl->height; }
std::string VideoFXFrameRecorder::GetEncoderName() const { return impl->vcodec ? impl->vcodec->name : ""; }

} // namespace VideoFX
