// VideoFX/core/VideoFXRemux.cpp
// VideoFX_TrimLossless: cut by copying packets (no decode, no re-encode).
// The cut starts at the video keyframe at or before `start`; the other
// streams are aligned to it.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXBackend.h"
#include "VideoFX/VideoFX.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace VideoFX {

using namespace Internal;

namespace {

class Remuxer {
public:
    ~Remuxer() { Close(); }
    VideoFXResult Run(const std::string& inputPath, const std::string& outputPath, double start, double end,
                      const VideoFXProgressCallback& progress);

private:
    void Close() {
        if (oc) {
            if (ioOpened) avio_closep(&oc->pb);
            avformat_free_context(oc);
            oc = nullptr;
        }
        ioOpened = false;
    }
    AVFormatContext* oc = nullptr;
    bool ioOpened = false;
};

VideoFXResult Remuxer::Run(const std::string& inputPath, const std::string& outputPath, double start, double end,
                           const VideoFXProgressCallback& progress) {
    if (!(start >= 0.0) || !(end >= 0.0) || (end > 0.0 && end <= start))
        return Fail(VideoFXResult::InvalidArgument, "Bad trim range");
    std::error_code ec;
    if (std::filesystem::equivalent(UltraCanvas::PathFromUtf8(inputPath), UltraCanvas::PathFromUtf8(outputPath), ec))
        return Fail(VideoFXResult::InvalidArgument, "The output file is also the input");

    FormatInputPtr in;
    VideoFXResult r = OpenInput(inputPath, in);
    if (r != VideoFXResult::Ok) return r;
    const double duration = in->duration != AV_NOPTS_VALUE ? in->duration / static_cast<double>(AV_TIME_BASE) : 0.0;
    if (duration > 0.0 && start >= duration) return Fail(VideoFXResult::InvalidArgument, "Trim start lies after the end");
    if (end <= 0.0 || (duration > 0.0 && end > duration)) end = duration;

    int err = avformat_alloc_output_context2(&oc, nullptr, nullptr, outputPath.c_str());
    if (err < 0 || !oc) return Fail(VideoFXResult::InvalidArgument, "No known format for the extension of " + outputPath, err);

    // Map every stream the output container can hold
    std::vector<int> map(in->nb_streams, -1);
    int videoIn = -1;
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream* st = in->streams[i];
        const AVMediaType type = st->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE) continue;
        if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
        if (avformat_query_codec(oc->oformat, st->codecpar->codec_id, FF_COMPLIANCE_NORMAL) == 0) continue;
        AVStream* out = avformat_new_stream(oc, nullptr);
        if (!out) return Fail(VideoFXResult::WriteError, "Out of memory");
        err = avcodec_parameters_copy(out->codecpar, st->codecpar);
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot copy stream parameters", err);
        out->codecpar->codec_tag = 0;
        out->time_base = st->time_base;
        av_dict_copy(&out->metadata, st->metadata, 0);
        map[i] = out->index;
        if (type == AVMEDIA_TYPE_VIDEO && videoIn < 0) videoIn = static_cast<int>(i);
    }
    if (oc->nb_streams == 0)
        return Fail(VideoFXResult::UnsupportedFormat, "No stream of " + inputPath + " fits the output container");
    av_dict_copy(&oc->metadata, in->metadata, 0);

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        err = avio_open(&oc->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot create " + outputPath, err);
        ioOpened = true;
    }
    err = avformat_write_header(oc, nullptr);
    if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot write the file header", err);

    const int64_t origin = in->start_time != AV_NOPTS_VALUE ? in->start_time : 0;
    if (start > 0.0) av_seek_frame(in.get(), -1, origin + static_cast<int64_t>(start * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);

    // Output time zero = the first kept video keyframe (or first packet)
    int64_t shift = AV_NOPTS_VALUE;                 // AV_TIME_BASE units, relative to origin
    std::vector<bool> past(in->nb_streams, false);
    PacketPtr pkt = MakePacket();
    while (true) {
        err = av_read_frame(in.get(), pkt.get());
        if (err < 0) break;
        const int si = pkt->stream_index;
        if (si < 0 || si >= static_cast<int>(map.size()) || map[si] < 0 || past[si]) {
            av_packet_unref(pkt.get());
            continue;
        }
        AVStream* ist = in->streams[si];
        const int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        if (ts == AV_NOPTS_VALUE) { av_packet_unref(pkt.get()); continue; }
        const int64_t t = av_rescale_q(ts, ist->time_base, AV_TIME_BASE_Q) - origin;

        if (shift == AV_NOPTS_VALUE) {
            const bool anchor = videoIn < 0 || (si == videoIn && (pkt->flags & AV_PKT_FLAG_KEY));
            if (!anchor) { av_packet_unref(pkt.get()); continue; }
            shift = t;
        }
        if (t < shift) { av_packet_unref(pkt.get()); continue; }
        if (end > 0.0 && t >= static_cast<int64_t>(end * AV_TIME_BASE)) {
            past[si] = true;
            av_packet_unref(pkt.get());
            bool all = true;
            for (size_t i = 0; i < map.size(); ++i) if (map[i] >= 0 && !past[i]) all = false;
            if (all) break;
            continue;
        }

        AVStream* ost = oc->streams[map[si]];
        const int64_t offset = av_rescale_q(shift + origin, AV_TIME_BASE_Q, ist->time_base);
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= offset;
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= offset;
        av_packet_rescale_ts(pkt.get(), ist->time_base, ost->time_base);
        pkt->stream_index = ost->index;
        pkt->pos = -1;
        err = av_interleaved_write_frame(oc, pkt.get());
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot write " + outputPath, err);

        if (progress && end > 0.0) {
            const double span = end - shift / static_cast<double>(AV_TIME_BASE);
            const double done = (t - shift) / static_cast<double>(AV_TIME_BASE);
            if (span > 0.0 && !progress(std::clamp(done / span, 0.0, 0.999)))
                return Fail(VideoFXResult::Cancelled, "Cut cancelled");
        }
    }
    if (shift == AV_NOPTS_VALUE) return Fail(VideoFXResult::DecodeError, "Nothing to copy in the requested range");

    err = av_write_trailer(oc);
    if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot finish " + outputPath, err);
    if (ioOpened) {
        err = avio_closep(&oc->pb);
        ioOpened = false;
        if (err < 0) return Fail(VideoFXResult::WriteError, "Cannot close " + outputPath, err);
    }
    if (progress) progress(1.0);
    return VideoFXResult::Ok;
}

} // namespace

VideoFXResult VideoFX_TrimLossless(const std::string& inputPath, const std::string& outputPath,
                                   double start, double end, const VideoFXProgressCallback& progress) {
    EnsureBackendInitialised();
    ClearError();
    VideoFXResult r;
    {
        Remuxer remuxer;
        r = remuxer.Run(inputPath, outputPath, start, end, progress);
    }
    if (r != VideoFXResult::Ok && r != VideoFXResult::InvalidArgument && r != VideoFXResult::FileNotFound &&
        r != VideoFXResult::OpenFailed) {
        std::error_code ec;
        std::filesystem::remove(UltraCanvas::PathFromUtf8(outputPath), ec);
    }
    return r;
}

} // namespace VideoFX
