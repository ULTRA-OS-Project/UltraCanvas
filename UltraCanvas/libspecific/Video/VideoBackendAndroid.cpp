// libspecific/Video/VideoBackendAndroid.cpp
// Android video backend: the NDK's own media stack, so it needs nothing from
// the cross-compiled dependency sysroot.
//
//   Playback : AMediaExtractor demuxes; AMediaCodec decodes video into an
//              AImageReader (YUV_420_888, which every decoder can produce,
//              whatever its private layout) and audio to PCM played through
//              AAudio. A session clock, slaved to the audio device while
//              audio plays, decides when each video frame is shown.
//   Thumbnail: the same video pipeline, one frame, synchronously.
//   Capture  : NDK Camera2 + MediaCodec encoders + AMediaMuxer - in
//              VideoBackendAndroidCapture.cpp.
//
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "IVideoBackend.h"
#include "VideoBackendAndroid.h"
#include "VideoBackendAndroidImage.h"
#include "../../OS/Android/UltraCanvasAndroidPermissions.h"
#include "UltraCanvasDebug.h"

#include <aaudio/AAudio.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace UltraCanvas {
namespace AndroidVideo {

// ===== SHARED HELPERS (declared in VideoBackendAndroid.h) =====

VideoCodec CodecFromMime(const std::string& mime) {
    if (mime == "video/avc") return VideoCodec::H264;
    if (mime == "video/hevc") return VideoCodec::H265;
    if (mime == "video/x-vnd.on2.vp8") return VideoCodec::VP8;
    if (mime == "video/x-vnd.on2.vp9") return VideoCodec::VP9;
    if (mime == "video/av01") return VideoCodec::AV1;
    if (mime == "video/mjpeg" || mime == "video/x-motion-jpeg") return VideoCodec::MJPEG;
    return VideoCodec::Unknown;
}

bool OpenExtractor(AMediaExtractor* extractor, const std::string& source) {
    if (!extractor || source.empty()) return false;
    std::string path = source;
    if (path.rfind("file://", 0) == 0) {
        path = path.substr(7);
    } else if (path.find("://") != std::string::npos) {
        // http(s) and the like: the platform's own data source fetches it.
        return AMediaExtractor_setDataSource(extractor, path.c_str()) == AMEDIA_OK;
    }
    // A local file goes in by descriptor: the path form of setDataSource is
    // documented for URLs, and an app-private path is not readable by the
    // media server process that would otherwise open it.
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    const bool ok = fstat(fd, &st) == 0 &&
                    AMediaExtractor_setDataSourceFd(extractor, fd, 0, st.st_size) == AMEDIA_OK;
    close(fd);   // the extractor keeps its own duplicate
    return ok;
}

YuvColorInfo ColorInfoFromFormat(AMediaFormat* format, int height) {
    YuvColorInfo info;
    info.bt709 = height >= 720;
    int32_t value = 0;
    // MediaFormat.KEY_COLOR_STANDARD / KEY_COLOR_RANGE, spelled out: the NDK
    // constants for them only exist from API 28.
    if (format && AMediaFormat_getInt32(format, "color-standard", &value)) {
        if (value == 1 || value == 6) info.bt709 = true;        // BT.709, BT.2020
        else if (value == 2 || value == 4) info.bt709 = false;  // BT.601 PAL / NTSC
    }
    if (format && AMediaFormat_getInt32(format, "color-range", &value)) {
        info.fullRange = value == 1;
    }
    return info;
}

// ===== VIDEO PIPELINE =====
// Extractor (video track only) -> decoder -> AImageReader. Used by the
// playback session's video thread and by the synchronous thumbnail grab, so it
// is single-threaded: whoever owns it drives it.

class VideoPipeline {
public:
    ~VideoPipeline() { Close(); }

    bool Open(const std::string& source) {
        extractor = AMediaExtractor_new();
        if (!OpenExtractor(extractor, source)) return false;

        const size_t tracks = AMediaExtractor_getTrackCount(extractor);
        for (size_t i = 0; i < tracks; ++i) {
            AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, i);
            const char* mime = nullptr;
            if (format && AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && mime &&
                std::strncmp(mime, "video/", 6) == 0) {
                trackFormat = format;
                trackMime = mime;
                AMediaExtractor_selectTrack(extractor, i);
                break;
            }
            if (format) AMediaFormat_delete(format);
        }
        if (!trackFormat) return false;

        AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_WIDTH, &width);
        AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_HEIGHT, &height);
        // MediaFormat.KEY_ROTATION ("rotation-degrees"): phones record
        // sideways and store the turn as metadata.
        AMediaFormat_getInt32(trackFormat, "rotation-degrees", &rotation);
        if (width <= 0 || height <= 0) return false;
        color = ColorInfoFromFormat(trackFormat, height);

        // Three images: one being converted, one the decoder renders into
        // next, one spare - the decoder stalls if it has none to render to.
        if (AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, 3, &reader) != AMEDIA_OK) {
            return false;
        }
        ANativeWindow* window = nullptr;
        if (AImageReader_getWindow(reader, &window) != AMEDIA_OK || !window) return false;

        codec = AMediaCodec_createDecoderByType(trackMime.c_str());
        if (!codec) return false;
        if (AMediaCodec_configure(codec, trackFormat, window, nullptr, 0) != AMEDIA_OK) return false;
        if (AMediaCodec_start(codec) != AMEDIA_OK) return false;
        started = true;
        return true;
    }

    void Close() {
        if (codec) {
            if (started) AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
        started = false;
        if (reader) { AImageReader_delete(reader); reader = nullptr; }
        if (trackFormat) { AMediaFormat_delete(trackFormat); trackFormat = nullptr; }
        if (extractor) { AMediaExtractor_delete(extractor); extractor = nullptr; }
    }

    // Back to the sync frame at or before `seconds`; frames before it are
    // then decoded and dropped by the caller.
    void SeekTo(double seconds) {
        AMediaExtractor_seekTo(extractor, static_cast<int64_t>(seconds * 1e6),
                               AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        AMediaCodec_flush(codec);
        inputDone = false;
        outputDone = false;
    }

    // Hand the decoder every compressed sample it has room for.
    void Feed() {
        while (!inputDone) {
            const ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 0);
            if (index < 0) return;
            size_t capacity = 0;
            uint8_t* buffer = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(index), &capacity);
            const ssize_t size = buffer ? AMediaExtractor_readSampleData(extractor, buffer, capacity) : -1;
            if (size < 0) {
                AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(index), 0, 0, 0,
                                             AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                inputDone = true;
                return;
            }
            const int64_t timeUs = AMediaExtractor_getSampleTime(extractor);
            AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(index), 0,
                                         static_cast<size_t>(size),
                                         static_cast<uint64_t>(std::max<int64_t>(0, timeUs)), 0);
            AMediaExtractor_advance(extractor);
        }
    }

    // A decoded frame waiting to be shown or dropped, or index < 0.
    struct Output {
        ssize_t index = -1;
        double pts = 0.0;
    };

    // Wait up to `timeoutUs` for the next decoded frame. Notes end of stream
    // in outputDone and picks up colour changes on the way.
    Output Next(int64_t timeoutUs) {
        Output out;
        if (outputDone) return out;
        Feed();
        AMediaCodecBufferInfo info{};
        const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (AMediaFormat* format = AMediaCodec_getOutputFormat(codec)) {
                color = ColorInfoFromFormat(format, height);
                AMediaFormat_delete(format);
            }
            return out;
        }
        if (index < 0) return out;
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) outputDone = true;
        if (info.size <= 0 && (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)) {
            AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
            return out;
        }
        out.index = index;
        out.pts = static_cast<double>(info.presentationTimeUs) / 1e6;
        return out;
    }

    void Drop(const Output& o) {
        if (o.index >= 0) AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(o.index), false);
    }

    // Render the frame into the image reader and convert it.
    UCVideoFramePtr Show(const Output& o) {
        if (o.index < 0) return nullptr;
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(o.index), true);
        // Rendering is asynchronous: the image reaches the reader a moment
        // after the release.
        AImage* image = nullptr;
        for (int attempt = 0; attempt < 50; ++attempt) {
            if (AImageReader_acquireLatestImage(reader, &image) == AMEDIA_OK && image) break;
            image = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!image) return nullptr;
        UCVideoFramePtr frame = FrameFromYuvImage(image, rotation, false, color, o.pts);
        AImage_delete(image);
        return frame;
    }

    int width = 0, height = 0, rotation = 0;
    bool outputDone = false;

private:
    AMediaExtractor* extractor = nullptr;
    AMediaFormat* trackFormat = nullptr;
    std::string trackMime;
    AMediaCodec* codec = nullptr;
    AImageReader* reader = nullptr;
    YuvColorInfo color;
    bool started = false;
    bool inputDone = false;
};

namespace {

using SteadyClock = std::chrono::steady_clock;

// ===== SESSION CLOCK =====
// Media time as a function of wall time: anchored at a position and an
// instant, advancing at the playback rate while running. The audio thread
// slaves it to the audio device (whose clock is what the user hears) by
// re-anchoring when the two drift apart.
class MediaClock {
public:
    double Position() const {
        std::lock_guard<std::mutex> lock(mutex);
        return PositionLocked();
    }
    void Set(double position) {
        std::lock_guard<std::mutex> lock(mutex);
        anchorPos = position;
        anchorTime = SteadyClock::now();
    }
    void Run(bool run) {
        std::lock_guard<std::mutex> lock(mutex);
        anchorPos = PositionLocked();
        anchorTime = SteadyClock::now();
        running = run;
    }
    void SetRate(float r) {
        std::lock_guard<std::mutex> lock(mutex);
        anchorPos = PositionLocked();
        anchorTime = SteadyClock::now();
        rate = r;
    }

private:
    double PositionLocked() const {
        if (!running) return anchorPos;
        const double elapsed = std::chrono::duration<double>(SteadyClock::now() - anchorTime).count();
        return anchorPos + elapsed * rate;
    }
    mutable std::mutex mutex;
    double anchorPos = 0.0;
    SteadyClock::time_point anchorTime = SteadyClock::now();
    bool running = false;
    float rate = 1.0f;
};

// ===== DECODE SESSION =====

class AndroidDecodeSession : public IVideoDecodeSession {
public:
    AndroidDecodeSession(std::string src, const VideoDecodeOptions& o)
        : source(std::move(src)), opts(o) {}

    ~AndroidDecodeSession() override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quit = true;
        }
        wake.notify_all();
        if (videoThread.joinable()) videoThread.join();
        if (audioThread.joinable()) audioThread.join();
    }

    // Probe the source on the caller's thread: duration, size and tracks are
    // known before OpenDecoder returns, as the engine expects (the thumbnail
    // fallback asks GetDuration() before playing).
    bool Probe() {
        AMediaExtractor* extractor = AMediaExtractor_new();
        if (!OpenExtractor(extractor, source)) {
            AMediaExtractor_delete(extractor);
            return false;
        }
        bool hasVideo = false;
        int64_t durationUs = 0;
        const size_t tracks = AMediaExtractor_getTrackCount(extractor);
        for (size_t i = 0; i < tracks; ++i) {
            AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, i);
            if (!format) continue;
            const char* mime = nullptr;
            AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
            int64_t trackUs = 0;
            if (AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &trackUs)) {
                durationUs = std::max(durationUs, trackUs);
            }
            if (mime && std::strncmp(mime, "video/", 6) == 0 && !hasVideo) {
                hasVideo = true;
                int32_t w = 0, h = 0, rot = 0, bitRate = 0;
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &w);
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &h);
                AMediaFormat_getInt32(format, "rotation-degrees", &rot);
                const bool sideways = rot == 90 || rot == 270;
                info.width = sideways ? h : w;
                info.height = sideways ? w : h;
                int32_t fpsInt = 0;
                float fps = 0.0f;
                if (AMediaFormat_getFloat(format, AMEDIAFORMAT_KEY_FRAME_RATE, &fps) && fps > 0.0f) {
                    info.frameRate = fps;
                } else if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, &fpsInt)) {
                    info.frameRate = fpsInt;
                }
                if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, &bitRate)) {
                    info.bitRate = bitRate;
                }
                info.codec = CodecFromMime(mime);
            } else if (mime && std::strncmp(mime, "audio/", 6) == 0 && audioTrack < 0) {
                audioTrack = static_cast<int>(i);
                info.hasAudio = true;
            }
            AMediaFormat_delete(format);
        }
        AMediaExtractor_delete(extractor);
        if (!hasVideo) return false;

        duration = static_cast<double>(durationUs) / 1e6;
        info.duration = duration;
        info.container = ContainerFromPath(source);
        return true;
    }

    bool Play() override {
        EnsureStarted();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (ended) {
                // Play after the end starts over, as every other backend does.
                RequestSeekLocked(0.0);
            }
            playing = true;
        }
        clock.Run(true);
        wake.notify_all();
        return true;
    }

    bool Pause() override {
        EnsureStarted();
        {
            std::lock_guard<std::mutex> lock(mutex);
            playing = false;
        }
        clock.Run(false);
        wake.notify_all();
        return true;
    }

    bool Stop() override {
        Pause();
        return Seek(0.0);
    }

    bool Seek(double seconds) override {
        EnsureStarted();
        {
            std::lock_guard<std::mutex> lock(mutex);
            RequestSeekLocked(seconds);
        }
        wake.notify_all();
        return true;
    }

    double GetPosition() const override {
        const double p = clock.Position();
        return duration > 0.0 ? std::clamp(p, 0.0, duration) : std::max(0.0, p);
    }
    double GetDuration() const override { return duration; }
    const VideoStreamInfo& GetStreamInfo() const override { return info; }

    void SetVolume(float v) override { EnsureStarted(); volume = std::clamp(v, 0.0f, 1.0f); }
    void SetMute(bool m) override { EnsureStarted(); muted = m; }
    void SetLoop(bool l) override { EnsureStarted(); looping = l; }
    void SetPlaybackRate(float r) override {
        EnsureStarted();
        const float rate = (r > 0.01f) ? r : 1.0f;
        playbackRate = rate;
        clock.SetRate(rate);
        wake.notify_all();
    }

private:
    static VideoContainer ContainerFromPath(const std::string& path) {
        std::string ext;
        const size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == "mp4" || ext == "m4v" || ext == "3gp") return VideoContainer::MP4;
        if (ext == "mov") return VideoContainer::MOV;
        if (ext == "mkv") return VideoContainer::MKV;
        if (ext == "webm") return VideoContainer::WebM;
        if (ext == "avi") return VideoContainer::AVI;
        return VideoContainer::Unknown;
    }

    // Threads start on the first call into the session rather than in
    // OpenDecoder: the engine assigns onFrame/onLoaded/... only after
    // OpenDecoder returns, and a thread already running could race those
    // assignments. Every caller makes at least one call (SetVolume, Play,
    // Seek, ...) before it waits for a frame.
    void EnsureStarted() {
        std::call_once(startOnce, [this] {
            if (onLoaded) onLoaded();
            videoThread = std::thread([this] { VideoLoop(); });
            if (audioTrack >= 0 && !opts.disableAudio) {
                audioThread = std::thread([this] { AudioLoop(); });
            } else {
                audioFinished = true;
            }
        });
    }

    void RequestSeekLocked(double seconds) {
        seekTarget = duration > 0.0 ? std::clamp(seconds, 0.0, duration) : std::max(0.0, seconds);
        ++seekSerial;
        ended = false;
        endedReported = false;
        audioFinished = audioTrack < 0 || opts.disableAudio;
        clock.Set(seekTarget);
    }

    // ---- Video thread ----

    void VideoLoop() {
        VideoPipeline pipe;
        if (!pipe.Open(source)) {
            if (onError) onError("Android video: cannot decode " + source);
            return;
        }

        uint64_t seenSerial = 0;
        double dropBefore = 0.0;
        bool preroll = true;   // show the first frame even while paused
        VideoPipeline::Output pending;

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (quit) break;
                if (seekSerial != seenSerial) {
                    seenSerial = seekSerial;
                    dropBefore = seekTarget;
                    lock.unlock();
                    pipe.Drop(pending);
                    pending = {};
                    pipe.SeekTo(dropBefore);
                    preroll = true;
                    continue;
                }
                if (!playing && !preroll) {
                    wake.wait(lock);
                    continue;
                }
                if (ended) {
                    wake.wait(lock);
                    continue;
                }
            }

            if (pending.index < 0) {
                if (pipe.outputDone) {
                    FinishOrLoop();
                    continue;
                }
                pending = pipe.Next(10000);
                if (pending.index < 0) continue;
                // Frames before the seek target exist only to reach it.
                const double halfFrame = info.frameRate > 0.0 ? 0.5 / info.frameRate : 0.02;
                if (pending.pts + halfFrame < dropBefore) {
                    pipe.Drop(pending);
                    pending = {};
                    continue;
                }
            }

            if (preroll) {
                ShowFrame(pipe, pending);
                pending = {};
                preroll = false;
                continue;
            }

            // Wait for the frame's time, but never longer than 20 ms at once,
            // so a seek, a pause or the end of the session is noticed.
            const double ahead = (pending.pts - clock.Position()) / std::max(0.01f, playbackRate.load());
            if (ahead > 0.002) {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::duration<double>(std::min(ahead, 0.02)));
                continue;
            }
            // Hopelessly late (the device cannot keep up): skip rather than
            // fall further behind, but show something at least every 250 ms.
            if (ahead < -0.1 && SteadyClock::now() - lastShown < std::chrono::milliseconds(250)) {
                pipe.Drop(pending);
            } else {
                ShowFrame(pipe, pending);
            }
            pending = {};
        }
        pipe.Drop(pending);
    }

    void ShowFrame(VideoPipeline& pipe, const VideoPipeline::Output& o) {
        if (UCVideoFramePtr frame = pipe.Show(o)) {
            lastShown = SteadyClock::now();
            if (onFrame) onFrame(std::move(frame));
        }
    }

    // The video track is finished. Wait for the audio to finish too, then
    // loop or report the end, once.
    void FinishOrLoop() {
        std::unique_lock<std::mutex> lock(mutex);
        if (quit || ended) return;
        const bool audioDone = audioFinished || clock.Position() >= duration;
        if (!audioDone) {
            wake.wait_for(lock, std::chrono::milliseconds(20));
            return;
        }
        if (looping) {
            RequestSeekLocked(0.0);
            lock.unlock();
            wake.notify_all();
            return;
        }
        if (!endedReported) {
            endedReported = true;
            ended = true;
            playing = false;
            lock.unlock();
            clock.Run(false);
            if (duration > 0.0) clock.Set(duration);
            if (onEnded) onEnded();
        }
    }

    // ---- Audio thread ----

    void AudioLoop() {
        AMediaExtractor* extractor = AMediaExtractor_new();
        AMediaCodec* codec = nullptr;
        AAudioStream* stream = nullptr;
        AMediaFormat* format = nullptr;
        auto cleanup = [&] {
            if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); }
            if (codec) { AMediaCodec_stop(codec); AMediaCodec_delete(codec); }
            if (format) AMediaFormat_delete(format);
            AMediaExtractor_delete(extractor);
            std::lock_guard<std::mutex> lock(mutex);
            audioFinished = true;
            wake.notify_all();
        };
        const char* mime = nullptr;
        if (!OpenExtractor(extractor, source) ||
            AMediaExtractor_selectTrack(extractor, static_cast<size_t>(audioTrack)) != AMEDIA_OK ||
            !(format = AMediaExtractor_getTrackFormat(extractor, static_cast<size_t>(audioTrack))) ||
            !AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) || !mime ||
            !(codec = AMediaCodec_createDecoderByType(mime)) ||
            AMediaCodec_configure(codec, format, nullptr, nullptr, 0) != AMEDIA_OK ||
            AMediaCodec_start(codec) != AMEDIA_OK) {
            // A track the device cannot decode leaves the video playing
            // silently rather than failing the whole session.
            debugOutput << "Android video: audio track not decodable, playing silently" << std::endl;
            if (codec) { AMediaCodec_delete(codec); codec = nullptr; }
            cleanup();
            return;
        }

        int32_t sampleRate = 0, channels = 0;
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);

        uint64_t seenSerial = 0;
        double dropBefore = 0.0;
        bool inputDone = false, outputDone = false;
        bool streamRunning = false;
        // Media time of the next sample written, and the stream frame it
        // lands on: the pair that maps the device's position to media time.
        bool segmentAnchored = false;
        double segmentPts = 0.0;
        int64_t segmentFrame = 0;
        auto lastSync = SteadyClock::now();
        std::vector<int16_t> scratch;

        auto openStream = [&]() -> bool {
            if (stream) return true;
            if (sampleRate <= 0 || channels <= 0) return false;
            AAudioStreamBuilder* builder = nullptr;
            if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
            AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
            AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
            AAudioStreamBuilder_setSampleRate(builder, sampleRate);
            AAudioStreamBuilder_setChannelCount(builder, channels);
            AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
            AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
            const aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
            AAudioStreamBuilder_delete(builder);
            if (result != AAUDIO_OK) {
                stream = nullptr;
                debugOutput << "Android video: AAudio open failed: "
                            << AAudio_convertResultToText(result) << std::endl;
                return false;
            }
            return true;
        };
        auto pauseStream = [&] {
            if (stream && streamRunning) {
                AAudioStream_requestPause(stream);
                streamRunning = false;
            }
        };
        auto flushStream = [&] {
            if (!stream) return;
            pauseStream();
            aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
            AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_PAUSING, &next,
                                            100 * 1000 * 1000LL);
            AAudioStream_requestFlush(stream);
            AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_FLUSHING, &next,
                                            100 * 1000 * 1000LL);
        };

        for (;;) {
            bool wantRun;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (quit) break;
                if (seekSerial != seenSerial) {
                    seenSerial = seekSerial;
                    dropBefore = seekTarget;
                    lock.unlock();
                    AMediaExtractor_seekTo(extractor, static_cast<int64_t>(dropBefore * 1e6),
                                           AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                    AMediaCodec_flush(codec);
                    flushStream();
                    inputDone = outputDone = false;
                    segmentAnchored = false;
                    continue;
                }
                // Only real-time playback is heard: AAudio cannot change
                // speed, and pitch-shifted or stuttering sound is worse than
                // none. A rate change back to 1 resynchronises by seeking.
                wantRun = playing && !ended && playbackRate.load() == 1.0f;
                if (!wantRun) {
                    pauseStream();
                    const bool silencedByRate = playing && playbackRate.load() != 1.0f;
                    wake.wait(lock);
                    if (silencedByRate && playbackRate.load() == 1.0f && !quit &&
                        seekSerial == seenSerial) {
                        RequestSeekLocked(clock.Position());
                        lock.unlock();
                        wake.notify_all();
                    }
                    continue;
                }
                if (outputDone) {
                    // Everything is written; the device plays out what it
                    // holds while the stream keeps running.
                    audioFinished = true;
                    wake.notify_all();
                    wake.wait(lock);
                    continue;
                }
            }

            // Feed compressed samples.
            while (!inputDone) {
                const ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 0);
                if (index < 0) break;
                size_t capacity = 0;
                uint8_t* buffer = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(index), &capacity);
                const ssize_t size = buffer ? AMediaExtractor_readSampleData(extractor, buffer, capacity) : -1;
                if (size < 0) {
                    AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(index), 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                    break;
                }
                const int64_t timeUs = AMediaExtractor_getSampleTime(extractor);
                AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(index), 0,
                                             static_cast<size_t>(size),
                                             static_cast<uint64_t>(std::max<int64_t>(0, timeUs)), 0);
                AMediaExtractor_advance(extractor);
            }

            AMediaCodecBufferInfo bi{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec, &bi, 10000);
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (AMediaFormat* out = AMediaCodec_getOutputFormat(codec)) {
                    int32_t rate = 0, ch = 0;
                    AMediaFormat_getInt32(out, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
                    AMediaFormat_getInt32(out, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
                    AMediaFormat_delete(out);
                    if ((rate > 0 && rate != sampleRate) || (ch > 0 && ch != channels)) {
                        // The decoder knows better than the container: reopen
                        // the device to match what it actually produces.
                        if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); }
                        stream = nullptr;
                        streamRunning = false;
                        segmentAnchored = false;
                        if (rate > 0) sampleRate = rate;
                        if (ch > 0) channels = ch;
                    }
                }
                continue;
            }
            if (index < 0) continue;
            if (bi.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) outputDone = true;

            size_t bufferSize = 0;
            uint8_t* data = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), &bufferSize);
            if (data && bi.size > 0 && openStream()) {
                const int16_t* pcm = reinterpret_cast<const int16_t*>(data + bi.offset);
                int64_t frames = bi.size / static_cast<int32_t>(sizeof(int16_t) * channels);
                double pts = static_cast<double>(bi.presentationTimeUs) / 1e6;
                // Trim what lies before a seek target, to the sample.
                if (pts < dropBefore) {
                    const int64_t skip = std::min<int64_t>(
                            frames, static_cast<int64_t>((dropBefore - pts) * sampleRate));
                    pcm += skip * channels;
                    frames -= skip;
                    pts += static_cast<double>(skip) / sampleRate;
                }
                if (frames > 0) {
                    // AAudio has no volume control: scale the samples.
                    const float gain = muted.load() ? 0.0f : volume.load();
                    scratch.assign(pcm, pcm + frames * channels);
                    if (gain < 0.999f) {
                        for (int16_t& s : scratch) s = static_cast<int16_t>(s * gain);
                    }
                    if (!segmentAnchored) {
                        segmentAnchored = true;
                        segmentPts = pts;
                        segmentFrame = AAudioStream_getFramesWritten(stream);
                    }
                    if (!streamRunning) {
                        AAudioStream_requestStart(stream);
                        streamRunning = true;
                    }
                    // Blocking writes pace this thread to the device. Written
                    // in slices so a pause or seek is never more than 50 ms
                    // away.
                    int64_t written = 0;
                    while (written < frames) {
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (quit || seekSerial != seenSerial || !playing ||
                                playbackRate.load() != 1.0f) break;
                        }
                        const aaudio_result_t n = AAudioStream_write(
                                stream, scratch.data() + written * channels,
                                static_cast<int32_t>(frames - written), 50 * 1000 * 1000LL);
                        if (n < 0) break;
                        written += n;
                        SyncClock(stream, segmentPts, segmentFrame, sampleRate, lastSync);
                    }
                }
            }
            AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
        }
        cleanup();
    }

    // Re-anchor the session clock on what the device is actually playing.
    void SyncClock(AAudioStream* stream, double segmentPts, int64_t segmentFrame,
                   int32_t sampleRate, SteadyClock::time_point& lastSync) {
        const auto now = SteadyClock::now();
        if (now - lastSync < std::chrono::milliseconds(250)) return;
        lastSync = now;
        int64_t framePosition = 0, timeNs = 0;
        if (AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &framePosition, &timeNs) != AAUDIO_OK) {
            return;
        }
        if (framePosition < segmentFrame || sampleRate <= 0) return;   // still the old segment
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const int64_t nowNs = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
        const double heard = segmentPts +
                static_cast<double>(framePosition - segmentFrame) / sampleRate +
                static_cast<double>(nowNs - timeNs) / 1e9;
        // Small differences are jitter in the timestamp itself; correcting
        // them would only make the video stutter.
        if (std::fabs(heard - clock.Position()) > 0.03) clock.Set(heard);
    }

    const std::string source;
    const VideoDecodeOptions opts;
    VideoStreamInfo info;
    double duration = 0.0;
    int audioTrack = -1;

    std::once_flag startOnce;
    std::thread videoThread, audioThread;
    MediaClock clock;

    // Guarded by mutex; `wake` signals any change.
    std::mutex mutex;
    std::condition_variable wake;
    bool quit = false;
    bool playing = false;
    bool ended = false;
    bool endedReported = false;
    bool audioFinished = false;
    uint64_t seekSerial = 0;
    double seekTarget = 0.0;

    std::atomic<float> volume{1.0f};
    std::atomic<bool> muted{false};
    std::atomic<bool> looping{false};
    std::atomic<float> playbackRate{1.0f};
    SteadyClock::time_point lastShown{};
};

// ===== BACKEND =====

class AndroidVideoBackend : public IVideoBackend {
public:
    bool Initialize() override { return true; }
    void Shutdown() override {}
    std::string GetName() const override { return "android"; }

    std::vector<VideoDeviceInfo> ListCameras() override { return ListAndroidCameras(); }
    VideoDeviceInfo GetDefaultCamera() override {
        for (const auto& camera : ListAndroidCameras()) {
            if (camera.isDefault) return camera;
        }
        return {};
    }

    CameraPermission GetCameraPermission() override {
        // Android cannot tell "never asked" from "refused but may ask again"
        // without asking; both are a state in which Request still prompts.
        return AndroidPermissions::IsGranted(AndroidPermissions::Camera)
                       ? CameraPermission::Granted
                       : CameraPermission::Undetermined;
    }
    void RequestCameraPermission(std::function<void(bool)> cb) override {
        AndroidPermissions::Request(AndroidPermissions::Camera, std::move(cb));
    }

    std::unique_ptr<IVideoDecodeSession> OpenDecoder(const std::string& source,
                                                     const VideoDecodeOptions& opts) override {
        auto session = std::make_unique<AndroidDecodeSession>(source, opts);
        if (!session->Probe()) return nullptr;
        return session;
    }

    std::unique_ptr<IVideoCaptureSession> OpenCapture(const VideoCaptureParams& p) override {
        return CreateAndroidCaptureSession(p);
    }

    // One frame through the same pipeline, synchronously: no threads, no
    // audio device.
    UCVideoFramePtr GrabThumbnail(const std::string& source,
                                  const VideoThumbnailRequest& req) override {
        VideoPipeline pipe;
        if (!pipe.Open(source)) return nullptr;
        double target = req.timeSeconds;
        if (target < 0.0) {
            // As the other backends: ~10% in, at most 1 s, past black intros.
            AMediaExtractor* probe = AMediaExtractor_new();
            int64_t durationUs = 0;
            if (OpenExtractor(probe, source)) {
                for (size_t i = 0; i < AMediaExtractor_getTrackCount(probe); ++i) {
                    if (AMediaFormat* f = AMediaExtractor_getTrackFormat(probe, i)) {
                        int64_t us = 0;
                        if (AMediaFormat_getInt64(f, AMEDIAFORMAT_KEY_DURATION, &us)) {
                            durationUs = std::max(durationUs, us);
                        }
                        AMediaFormat_delete(f);
                    }
                }
            }
            AMediaExtractor_delete(probe);
            const double dur = static_cast<double>(durationUs) / 1e6;
            target = dur > 1.0 ? std::min(dur * 0.1, 1.0) : 0.0;
        }
        if (target > 0.0) pipe.SeekTo(target);

        // A poster needs no exact frame: the first one within half a second
        // of the target will do, which saves decoding the rest of the GOP.
        const auto deadline = SteadyClock::now() + std::chrono::seconds(3);
        while (SteadyClock::now() < deadline && !pipe.outputDone) {
            VideoPipeline::Output o = pipe.Next(10000);
            if (o.index < 0) continue;
            if (o.pts + 0.5 < target) {
                pipe.Drop(o);
                continue;
            }
            return pipe.Show(o);
        }
        return nullptr;
    }
};

} // namespace
} // namespace AndroidVideo

IVideoBackend* GetNullVideoBackend();   // from VideoBackendNull.cpp

IVideoBackend* GetVideoBackend() {
    static AndroidVideo::AndroidVideoBackend backend;
    static IVideoBackend* selected =
        backend.Initialize() ? static_cast<IVideoBackend*>(&backend) : GetNullVideoBackend();
    return selected;
}

} // namespace UltraCanvas
