// libspecific/Video/VideoBackendGStreamer.cpp
// GStreamer-backed video backend. On Linux this is the platform-native media
// framework (GStreamer dispatches to V4L2 for cameras and VA-API / hardware
// decoders under the hood). Compiled only when ULTRACANVAS_ENABLE_VIDEO is ON
// and gstreamer-1.0 (+ app, + video, + pbutils) dev packages are present;
// otherwise the null backend is used.
//
// Threading model: a private GLib main loop runs on a dedicated thread so
// GStreamer bus messages and appsink callbacks fire off the UI thread. Decoded
// frames are converted to packed BGRA and handed up via the session callbacks;
// the engine buffers the latest frame for the UI thread to upload to a pixmap.
//
// Version: 0.1.7
// Last Modified: 2026-06-24
// Author: UltraCanvas Framework

#include "IVideoBackend.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/pbutils/pbutils.h>

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <cstring>

namespace UltraCanvas {

namespace {

// ---- One-time GStreamer init + shared main loop thread -------------------

// We run a *private* GLib main context on our own thread. Crucially we do NOT
// use the GLib default context: on Linux that context is owned by GTK, whose
// native file dialogs pump it from the UI thread via gtk_dialog_run(). Two
// threads iterating the same GMainContext corrupts GObject refcount/disposal
// state (observed as a heap double-free inside gtk_dialog_run). By owning a
// dedicated context here — and attaching every bus watch to it explicitly (see
// gst_bus_create_watch + g_source_attach in the sessions) — GStreamer's GLib
// activity stays fully isolated from GTK. Bus callbacks fire on this thread.
struct GstRuntime {
    GMainContext* ctx = nullptr;   // private context, isolated from GTK's default
    GMainLoop* loop = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};

    bool Start() {
        if (running.load()) return true;
        GError* err = nullptr;
        if (!gst_init_check(nullptr, nullptr, &err)) {
            if (err) g_error_free(err);
            return false;
        }
        ctx = g_main_context_new();
        loop = g_main_loop_new(ctx, FALSE);
        running.store(true);
        thread = std::thread([this]() {
            // Make our context the thread-default so any sources GStreamer
            // creates internally on this thread also land off the GTK default.
            g_main_context_push_thread_default(ctx);
            g_main_loop_run(loop);
            g_main_context_pop_thread_default(ctx);
        });
        return true;
    }

    void Stop() {
        if (!running.load()) return;
        running.store(false);
        if (loop) g_main_loop_quit(loop);
        if (thread.joinable()) thread.join();
        if (loop) { g_main_loop_unref(loop); loop = nullptr; }
        if (ctx) { g_main_context_unref(ctx); ctx = nullptr; }
    }

    GMainContext* Context() { return ctx; }

    ~GstRuntime() { Stop(); }
};

GstRuntime& Runtime() {
    static GstRuntime rt;
    return rt;
}

// Detach + destroy a bus watch GSource safely. The source is attached to the
// runtime's private GMainContext, which is being iterated on the loop thread;
// mutating its source list from another (UI) thread races GLib and corrupts the
// list. So we marshal the destroy onto the loop thread and wait. If the loop has
// already stopped (backend shutdown) we're single-threaded and can destroy here.
void DestroyBusSource(GSource* src) {
    if (!src) return;
    if (Runtime().running.load()) {
        std::promise<void> done;
        auto fut = done.get_future();
        struct C { GSource* s; std::promise<void>* p; } c{ src, &done };
        g_main_context_invoke_full(
            Runtime().Context(), G_PRIORITY_DEFAULT,
            [](gpointer p) -> gboolean {
                auto* c = static_cast<C*>(p);
                g_source_destroy(c->s);
                g_source_unref(c->s);
                c->p->set_value();
                return G_SOURCE_REMOVE;
            },
            &c, nullptr);
        fut.wait();
    } else {
        g_source_destroy(src);
        g_source_unref(src);
    }
}

// Build a UCVideoFrame from a GstSample whose caps are video/x-raw,format=BGRA.
// BGRA byte order matches Cairo's CAIRO_FORMAT_ARGB32 memory layout on
// little-endian hosts, so the UI layer can memcpy frames straight into a pixmap.
UCVideoFramePtr SampleToFrame(GstSample* sample) {
    if (!sample) return nullptr;
    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buf = gst_sample_get_buffer(sample);
    if (!caps || !buf) return nullptr;

    GstVideoInfo vinfo;
    if (!gst_video_info_from_caps(&vinfo, caps)) return nullptr;

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) return nullptr;

    const int w = GST_VIDEO_INFO_WIDTH(&vinfo);
    const int h = GST_VIDEO_INFO_HEIGHT(&vinfo);
    const int srcStride = GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0);

    auto frame = std::make_shared<UCVideoFrame>();
    VideoFrameInfo fi;
    fi.width = w;
    fi.height = h;
    fi.pixelFormat = VideoPixelFormat::BGRA32;
    fi.stride = w * 4;   // we repack tightly

    auto& out = frame->MutableData();
    out.resize(static_cast<size_t>(fi.stride) * h);
    for (int y = 0; y < h; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * fi.stride,
                    map.data + static_cast<size_t>(y) * srcStride,
                    static_cast<size_t>(w) * 4);
    }

    GstClockTime ts = GST_BUFFER_PTS(buf);
    if (GST_CLOCK_TIME_IS_VALID(ts)) fi.pts = static_cast<double>(ts) / GST_SECOND;

    frame->SetInfo(fi);
    gst_buffer_unmap(buf, &map);
    return frame;
}

// ---- One-shot thumbnail grab ---------------------------------------------

// Decode a single frame at a chosen timestamp using a throwaway pipeline that
// only touches the video track. We preroll to PAUSED (cheap; no audio sink, no
// rendering), seek accurately to the target time, then pull the preroll sample.
// Fully synchronous and self-contained: no main-loop thread or bus watch needed
// because every step here blocks until GStreamer reports completion.
UCVideoFramePtr GstGrabThumbnail(const std::string& source, const VideoThumbnailRequest& req) {
    if (!gst_is_initialized()) {
        if (!gst_init_check(nullptr, nullptr, nullptr)) return nullptr;
    }

    gchar* uri = gst_uri_is_valid(source.c_str())
                     ? g_strdup(source.c_str())
                     : gst_filename_to_uri(source.c_str(), nullptr);
    if (!uri) return nullptr;

    // uridecodebin auto-plugs a demuxer + (hardware) decoder and exposes a
    // video pad that parse-launch delay-links to videoconvert. Any audio pad is
    // simply left unlinked. videoscale is a no-op here (we scale in software
    // afterwards) but keeps the caps negotiable across odd pixel-aspect ratios.
    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "uridecodebin name=ucthumb-dec ! videoconvert ! videoscale ! "
        "video/x-raw,format=BGRA,pixel-aspect-ratio=1/1 ! "
        "appsink name=ucthumb-sink sync=false max-buffers=1 drop=false",
        &err);
    if (err) g_error_free(err);
    if (!pipeline) { g_free(uri); return nullptr; }

    GstElement* dec  = gst_bin_get_by_name(GST_BIN(pipeline), "ucthumb-dec");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "ucthumb-sink");
    if (dec) { g_object_set(dec, "uri", uri, nullptr); }
    g_free(uri);

    UCVideoFramePtr result;
    auto cleanup = [&]() {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        if (dec)  gst_object_unref(dec);
        if (sink) gst_object_unref(sink);
    };

    if (!dec || !sink) { cleanup(); return result; }

    // Preroll. Blocks until the first frame is decoded and held, or failure.
    if (gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        cleanup(); return result;
    }
    if (gst_element_get_state(pipeline, nullptr, nullptr, 8 * GST_SECOND)
            != GST_STATE_CHANGE_SUCCESS) {
        cleanup(); return result;
    }

    // Decide where to grab. Default to a short way in (skip black/title frames),
    // clamped inside the clip.
    double target = req.timeSeconds;
    if (target < 0.0) {
        gint64 dur = 0;
        if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur) && dur > 0) {
            double durSec = static_cast<double>(dur) / GST_SECOND;
            target = std::min(durSec * 0.1, durSec > 1.0 ? 1.0 : durSec * 0.5);
        } else {
            target = 0.0;
        }
    }

    if (target > 0.0) {
        gint64 pos = static_cast<gint64>(target * GST_SECOND);
        if (gst_element_seek_simple(
                pipeline, GST_FORMAT_TIME,
                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                pos)) {
            // Wait for the post-seek preroll to settle so pull_preroll returns
            // the frame at the requested position rather than the old one.
            gst_element_get_state(pipeline, nullptr, nullptr, 8 * GST_SECOND);
        }
    }

    GstSample* sample = gst_app_sink_pull_preroll(GST_APP_SINK(sink));
    if (sample) {
        result = SampleToFrame(sample);
        gst_sample_unref(sample);
    }

    cleanup();
    return result;
}

// ---- Decode session (playbin + BGRA appsink) -----------------------------

class GstDecodeSession : public IVideoDecodeSession {
public:
    explicit GstDecodeSession(const std::string& source) : sourceUri(source) {}

    ~GstDecodeSession() override { Teardown(); }

    bool Build() {
        pipeline = gst_element_factory_make("playbin", "ucvideo-playbin");
        if (!pipeline) return false;

        appsink = gst_element_factory_make("appsink", "ucvideo-sink");
        if (!appsink) { Teardown(); return false; }

        GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "BGRA", nullptr);
        g_object_set(appsink, "caps", caps, "emit-signals", TRUE,
                     "sync", TRUE, "max-buffers", 2, "drop", TRUE, nullptr);
        gst_caps_unref(caps);
        g_signal_connect(appsink, "new-sample", G_CALLBACK(&GstDecodeSession::OnNewSample), this);
        // While PAUSED (e.g. a paused scrub) the appsink emits new-preroll, not
        // new-sample, so without this the shown frame wouldn't refresh on a
        // paused seek.
        g_signal_connect(appsink, "new-preroll", G_CALLBACK(&GstDecodeSession::OnNewPreroll), this);

        // Route playbin's video through our appsink; audio uses the auto sink.
        g_object_set(pipeline, "video-sink", appsink, nullptr);

        // Pin the pipeline to the monotonic system clock. pulsesink's provided
        // GstAudioClock can latch a constant (garbage) value after a flushing
        // seek on PulseAudio, killing the pipeline clock so every sync=TRUE sink
        // (our video appsink + pulsesink) freezes forever on gst_clock_id_wait().
        // With the system clock forced, pulsesink runs slaved (skew) and a
        // misbehaving PA clock can no longer stall rendering. Set before the
        // first state change so clock distribution uses it from the start.
        GstClock* sysClock = gst_system_clock_obtain();
        gst_pipeline_use_clock(GST_PIPELINE(pipeline), sysClock);
        gst_object_unref(sysClock);

        // Audio sink. With the pipeline pinned to the system clock, pulsesink
        // runs as a clock SLAVE. The audio path's fixed device latency
        // (~buffer-time) is a constant ~67ms offset the system clock can't model;
        // the default slave-method=skew re-"corrects" that fixed offset every
        // cycle (thousands of "correct clock skew" warnings) until audio dies.
        // slave-method=none renders at the device's natural rate and treats the
        // offset as a constant (sub-perceptual) lip-sync delta — FLUSH|ACCURATE
        // seeks re-anchor audio+video together so it never compounds.
        // provide-clock=false also makes pulsesink unselectable as the pipeline
        // clock, so the post-flush garbage-clock freeze can't recur.
        if (GstElement* asink = gst_element_factory_make("pulsesink", "ucvideo-asink")) {
            g_object_set(asink,
                         "slave-method", 2,      // GST_AUDIO_BASE_SINK_SLAVE_NONE
                         "provide-clock", FALSE, nullptr);
            g_object_set(pipeline, "audio-sink", asink, nullptr);  // playbin takes the ref
        }
        // else: pulsesink unavailable (rare) → leave playbin's autoaudiosink default.

        gchar* uri = MakeUri(sourceUri);
        g_object_set(pipeline, "uri", uri, nullptr);
        g_free(uri);

        bus = gst_element_get_bus(pipeline);
        // Attach the bus watch to our private context (NOT the default one GTK
        // owns). gst_bus_add_watch() only ever targets the default context, so
        // we build the GSource ourselves and attach it explicitly.
        busSource = gst_bus_create_watch(bus);
        g_source_set_callback(busSource, (GSourceFunc)&GstDecodeSession::OnBus, this, nullptr);
        g_source_attach(busSource, Runtime().Context());

        // Preroll to PAUSED so duration/caps become queryable.
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        return true;
    }

    bool Play() override  { return RequestState(GST_STATE_PLAYING); }
    bool Pause() override { return RequestState(GST_STATE_PAUSED); }
    bool Stop() override {
        bool ok = RequestState(GST_STATE_PAUSED);
        Seek(0.0);
        return ok;
    }

    bool Seek(double seconds) override {
        if (!pipeline) return false;
        {
            std::lock_guard<std::mutex> lk(seekMutex);
            ReleaseStaleSeekLocked();           // don't stay wedged on a lost ASYNC_DONE
            if (seekInFlight) {                 // coalesce to newest target
                pendingSeekSeconds = seconds;
                seekPending = true;
                return true;
            }
            seekInFlight = true;
            seekIssuedUs = g_get_monotonic_time();
        }
        return DoSeek(seconds);                 // issue outside the lock
    }

    double GetPosition() const override {
        if (!pipeline) return 0.0;
        gint64 pos = 0;
        if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos))
            return static_cast<double>(pos) / GST_SECOND;
        return 0.0;
    }
    double GetDuration() const override {
        if (!pipeline) return 0.0;
        gint64 dur = 0;
        if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur) && dur > 0)
            return static_cast<double>(dur) / GST_SECOND;
        return 0.0;
    }
    const VideoStreamInfo& GetStreamInfo() const override { return streamInfo; }

    void SetVolume(float v) override {
        if (pipeline) g_object_set(pipeline, "volume", static_cast<double>(v), nullptr);
    }
    void SetMute(bool mute) override {
        if (pipeline) g_object_set(pipeline, "mute", mute ? TRUE : FALSE, nullptr);
    }
    void SetLoop(bool loop) override { looping = loop; }
    void SetPlaybackRate(float r) override {
        rate = (r > 0.01f) ? r : 1.0f;
        Seek(GetPosition());
    }

private:
    static gchar* MakeUri(const std::string& src) {
        if (gst_uri_is_valid(src.c_str())) return g_strdup(src.c_str());
        return gst_filename_to_uri(src.c_str(), nullptr);
    }

    bool SetState(GstState s) {
        if (!pipeline) return false;
        return gst_element_set_state(pipeline, s) != GST_STATE_CHANGE_FAILURE;
    }

    // Apply a play/pause state, but defer it if a flushing seek is still in
    // flight: changing state mid-seek makes the pipeline compute base-time
    // against a stale running-time (→ fast catch-up playback) and can drop the
    // request. The deferred state is applied from the ASYNC_DONE handler once
    // the in-flight (and any coalesced) seek has fully settled.
    bool RequestState(GstState s) {
        if (!pipeline) return false;
        {
            std::lock_guard<std::mutex> lk(seekMutex);
            targetState = s;
            ReleaseStaleSeekLocked();           // let play/pause through to recover a wedged pipeline
            if (seekInFlight) { statePending = true; return true; }
        }
        return SetState(s);
    }

    // Caller must hold seekMutex. If a flushing seek has been "in flight" far
    // longer than any real seek takes, its ASYNC_DONE was lost (e.g. the clock
    // stalled mid-seek). Free the slot so subsequent seeks / play-pause aren't
    // deferred forever — a Play/Pause that gets through re-bases the clock and
    // recovers the pipeline. The window is generous so it never preempts a seek
    // that is merely still settling (which would re-introduce overlap wedges).
    void ReleaseStaleSeekLocked() {
        if (seekInFlight && (g_get_monotonic_time() - seekIssuedUs) > kSeekStallUs) {
            seekInFlight = false;
            seekPending  = false;
        }
    }

    // Issues the actual flushing seek. The caller must have already claimed the
    // in-flight slot (seekInFlight == true). On success the slot is released
    // when the pipeline posts ASYNC_DONE (see OnBus); on outright refusal it is
    // released here so we don't wait for an ASYNC_DONE that will never come.
    // Never call this while holding seekMutex — gst_element_seek may block.
    bool DoSeek(double seconds) {
        gint64 pos = static_cast<gint64>(seconds * GST_SECOND);
        // ACCURATE (not KEY_UNIT): land the video pad on the exact requested
        // time so it matches where the audio pad — which owns the pipeline clock
        // — lands. KEY_UNIT snaps video to the preceding keyframe, leaving it
        // behind the audio clock so it races to catch up (fast playback) or
        // stalls against a clock it can't reach (frozen frame, audio plays on).
        gboolean ok = gst_element_seek(
            pipeline, rate, GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
            GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        if (!ok) {
            std::lock_guard<std::mutex> lk(seekMutex);
            seekInFlight = false;
            seekPending  = false;
        }
        return ok == TRUE;
    }

    static GstFlowReturn OnNewSample(GstAppSink* sink, gpointer user) {
        auto* self = static_cast<GstDecodeSession*>(user);
        GstSample* sample = gst_app_sink_pull_sample(sink);
        if (!sample) return GST_FLOW_OK;
        if (auto frame = SampleToFrame(sample)) {
            if (!self->infoReady) self->PopulateStreamInfo(sample);
            if (self->onFrame) self->onFrame(std::move(frame));
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // Mirrors OnNewSample for the preroll path: while PAUSED the appsink holds a
    // prerolled buffer (e.g. the target frame after a paused seek) and emits
    // new-preroll instead of new-sample. A buffer is delivered through preroll
    // OR sample, never both, so this can't double-deliver.
    static GstFlowReturn OnNewPreroll(GstAppSink* sink, gpointer user) {
        auto* self = static_cast<GstDecodeSession*>(user);
        GstSample* sample = gst_app_sink_pull_preroll(sink);
        if (!sample) return GST_FLOW_OK;
        if (auto frame = SampleToFrame(sample)) {
            if (!self->infoReady) self->PopulateStreamInfo(sample);
            if (self->onFrame) self->onFrame(std::move(frame));
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    void PopulateStreamInfo(GstSample* sample) {
        GstCaps* caps = gst_sample_get_caps(sample);
        GstVideoInfo vinfo;
        if (caps && gst_video_info_from_caps(&vinfo, caps)) {
            streamInfo.width  = GST_VIDEO_INFO_WIDTH(&vinfo);
            streamInfo.height = GST_VIDEO_INFO_HEIGHT(&vinfo);
            int fn = GST_VIDEO_INFO_FPS_N(&vinfo);
            int fd = GST_VIDEO_INFO_FPS_D(&vinfo);
            if (fd > 0) streamInfo.frameRate = static_cast<double>(fn) / fd;
        }
        streamInfo.duration = GetDuration();
        infoReady = true;
        if (onLoaded) onLoaded();
    }

    static gboolean OnBus(GstBus*, GstMessage* msg, gpointer user) {
        auto* self = static_cast<GstDecodeSession*>(user);
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_EOS:
                if (self->looping) {
                    self->Seek(0.0);
                    self->RequestState(GST_STATE_PLAYING);   // defer behind the seek
                } else if (self->onEnded) {
                    self->onEnded();
                }
                break;
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr; gchar* dbg = nullptr;
                gst_message_parse_error(msg, &err, &dbg);
                if (self->onError) self->onError(err ? err->message : "GStreamer error");
                if (err) g_error_free(err);
                g_free(dbg);
                break;
            }
            case GST_MESSAGE_CLOCK_LOST:
                // The selected (audio) clock became invalid. The documented
                // recovery is to drop to PAUSED and back to PLAYING so the
                // pipeline re-selects/redistributes a clock.
                if (self->pipeline) {
                    self->SetState(GST_STATE_PAUSED);
                    self->SetState(GST_STATE_PLAYING);
                }
                break;
            case GST_MESSAGE_ASYNC_DONE:
                // Only the whole pipeline's ASYNC_DONE marks a completed seek
                // (child elements post their own, which we must ignore). Release
                // the in-flight slot, or issue the coalesced pending target.
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline)) {
                    double next = 0.0; bool issue = false;
                    bool applyState = false; GstState apply = GST_STATE_PAUSED;
                    {
                        std::lock_guard<std::mutex> lk(self->seekMutex);
                        if (self->seekPending) {
                            self->seekPending = false;
                            next = self->pendingSeekSeconds;
                            self->seekIssuedUs = g_get_monotonic_time();  // restart stall window
                            issue = true;          // keep seekInFlight == true
                        } else {
                            self->seekInFlight = false;
                            if (self->statePending) {   // a Play/Pause was deferred
                                self->statePending = false;
                                applyState = true;
                                apply = self->targetState;
                            }
                        }
                    }
                    // Outside the lock: re-issue the coalesced seek, or apply the
                    // play/pause that was deferred until the seek finished.
                    if (issue)           self->DoSeek(next);
                    else if (applyState) self->SetState(apply);
                }
                break;
            default: break;
        }
        return TRUE;
    }

    void Teardown() {
        // Order matters. (1) Stop dataflow and JOIN the streaming threads first
        // so no appsink callback or bus message can fire into this object during
        // teardown. (2) Only then remove the bus watch, and do it on the loop
        // thread that owns its context (DestroyBusSource) — tearing the GSource
        // down from the UI thread while the loop iterates the context corrupts
        // GLib's source list. (3) Finally drop the refs.
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_element_get_state(pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        }
        if (busSource) { DestroyBusSource(busSource); busSource = nullptr; }
        if (pipeline) {
            gst_object_unref(pipeline);   // also drops the embedded appsink
            pipeline = nullptr;
            appsink = nullptr;
        }
        if (bus) { gst_object_unref(bus); bus = nullptr; }
    }

    std::string sourceUri;
    GstElement* pipeline = nullptr;
    GstElement* appsink = nullptr;
    GstBus* bus = nullptr;
    GSource* busSource = nullptr;
    VideoStreamInfo streamInfo;
    std::atomic<bool> infoReady{false};
    bool   looping = false;
    gdouble rate = 1.0;

    // Seek serialization: a flushing seek must not be issued while a previous
    // one is still settling (ASYNC_DONE not yet received). Overlapping flushing
    // seeks corrupt the pipeline base-time/clock and wedge the appsink so it
    // stops emitting new-sample. Keep at most one seek in flight; coalesce rapid
    // scrub-drag seeks to the latest target and issue it on ASYNC_DONE.
    // Play/pause requests are also deferred behind an in-flight seek (see
    // RequestState) and applied from the ASYNC_DONE handler, so a Play issued
    // mid-seek can't race the flush and trigger fast catch-up playback.
    static constexpr gint64 kSeekStallUs = 2500000;  // 2.5s: lost-ASYNC_DONE watchdog window
    std::mutex seekMutex;
    bool       seekInFlight = false;   // a flushing seek awaits ASYNC_DONE
    bool       seekPending  = false;   // a newer seek target arrived mid-flight
    double     pendingSeekSeconds = 0.0;
    gint64     seekIssuedUs = 0;       // monotonic time the in-flight seek was issued
    GstState   targetState  = GST_STATE_PAUSED;  // latest requested play/pause state
    bool       statePending = false;             // a state change deferred behind a seek
};

// ---- Capture session (camera + optional mic → tee → preview + file) -------

class GstCaptureSession : public IVideoCaptureSession {
public:
    explicit GstCaptureSession(const VideoCaptureParams& p) : params(p) {}
    ~GstCaptureSession() override { Close(); }

    bool Open() override {
        if (pipeline) return true;

        // Preview + a valved record branch. The record branch's valve drops
        // buffers until Start() opens it, so the file only contains footage
        // captured between Start() and Stop().
        std::string enc = EncoderFor(params.codec);
        std::string mux = MuxerFor(params.container);
        std::string dev = params.cameraId.empty() ? "" : (" device=" + params.cameraId);

        std::string desc =
            "v4l2src" + dev + " ! videoconvert ! "
            "video/x-raw,width=" + std::to_string(params.width) +
            ",height=" + std::to_string(params.height) + " ! tee name=t "
            "t. ! queue ! videoconvert ! video/x-raw,format=BGRA ! "
            "appsink name=preview emit-signals=true sync=false max-buffers=2 drop=true "
            "t. ! queue ! valve name=recvalve drop=true ! videoconvert ! " + enc +
            " ! " + mux + " name=mux ! filesink name=fsink location=\"" + params.outputPath + "\"";

        if (params.captureAudio) {
            desc += " autoaudiosrc ! queue ! audioconvert ! voaacenc ! mux.";
        }

        GError* err = nullptr;
        pipeline = gst_parse_launch(desc.c_str(), &err);
        if (!pipeline) {
            if (onError) onError(err ? err->message : "Failed to build capture pipeline");
            if (err) g_error_free(err);
            return false;
        }

        GstElement* preview = gst_bin_get_by_name(GST_BIN(pipeline), "preview");
        if (preview) {
            g_signal_connect(preview, "new-sample",
                             G_CALLBACK(&GstCaptureSession::OnPreview), this);
            gst_object_unref(preview);
        }
        recValve = gst_bin_get_by_name(GST_BIN(pipeline), "recvalve");

        bus = gst_element_get_bus(pipeline);
        // Attach to our private context, not GTK's default one (see GstRuntime).
        busSource = gst_bus_create_watch(bus);
        g_source_set_callback(busSource, (GSourceFunc)&GstCaptureSession::OnBus, this, nullptr);
        g_source_attach(busSource, Runtime().Context());

        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            if (onError) onError("Failed to start camera preview");
            Close();
            return false;
        }
        opened = true;
        return true;
    }

    void Close() override {
        recording = false;
        // Stop dataflow + join streaming threads first, then remove the bus
        // watch on its owning loop thread (see DestroyBusSource), then drop refs.
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_element_get_state(pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        }
        if (busSource) { DestroyBusSource(busSource); busSource = nullptr; }
        if (recValve) { gst_object_unref(recValve); recValve = nullptr; }
        if (pipeline) {
            gst_object_unref(pipeline);
            pipeline = nullptr;
        }
        if (bus) { gst_object_unref(bus); bus = nullptr; }
        opened = false;
    }

    bool IsOpen() const override { return opened; }

    bool Start() override {
        if (!opened && !Open()) return false;
        if (recValve) g_object_set(recValve, "drop", FALSE, nullptr);
        recording = true;
        startTime = g_get_monotonic_time();
        if (onStarted) onStarted();
        return true;
    }
    bool Pause() override {
        if (recValve) g_object_set(recValve, "drop", TRUE, nullptr);
        return true;
    }
    bool Resume() override {
        if (recValve) g_object_set(recValve, "drop", FALSE, nullptr);
        return true;
    }
    bool Stop() override {
        if (!recording) return false;
        recording = false;
        if (recValve) g_object_set(recValve, "drop", TRUE, nullptr);
        // Send EOS so the muxer finalizes the file header/index cleanly.
        gst_element_send_event(pipeline, gst_event_new_eos());
        if (onStopped) onStopped();
        return true;
    }

    double GetElapsed() const override {
        if (!recording) return 0.0;
        return static_cast<double>(g_get_monotonic_time() - startTime) / 1e6;
    }
    const VideoCaptureParams& GetParams() const override { return params; }

private:
    static std::string EncoderFor(VideoCodec c) {
        switch (c) {
            case VideoCodec::H265: return "x265enc";
            case VideoCodec::VP8:  return "vp8enc";
            case VideoCodec::VP9:  return "vp9enc";
            case VideoCodec::MJPEG:return "jpegenc";
            case VideoCodec::H264:
            default:               return "x264enc tune=zerolatency";
        }
    }
    static std::string MuxerFor(VideoContainer c) {
        switch (c) {
            case VideoContainer::MKV:  return "matroskamux";
            case VideoContainer::WebM: return "webmmux";
            case VideoContainer::MOV:  return "qtmux";
            case VideoContainer::AVI:  return "avimux";
            case VideoContainer::MP4:
            default:                   return "mp4mux";
        }
    }

    static GstFlowReturn OnPreview(GstAppSink* sink, gpointer user) {
        auto* self = static_cast<GstCaptureSession*>(user);
        GstSample* sample = gst_app_sink_pull_sample(sink);
        if (!sample) return GST_FLOW_OK;
        if (auto frame = SampleToFrame(sample)) {
            if (self->onPreviewFrame) self->onPreviewFrame(std::move(frame));
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    static gboolean OnBus(GstBus*, GstMessage* msg, gpointer user) {
        auto* self = static_cast<GstCaptureSession*>(user);
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError* err = nullptr; gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            if (self->onError) self->onError(err ? err->message : "GStreamer capture error");
            if (err) g_error_free(err);
            g_free(dbg);
        }
        return TRUE;
    }

    VideoCaptureParams params;
    GstElement* pipeline = nullptr;
    GstElement* recValve = nullptr;
    GstBus* bus = nullptr;
    GSource* busSource = nullptr;
    bool opened = false;
    std::atomic<bool> recording{false};
    gint64 startTime = 0;
};

// ---- Backend --------------------------------------------------------------

class GstBackend : public IVideoBackend {
public:
    bool Initialize() override { return Runtime().Start(); }
    void Shutdown() override { Runtime().Stop(); }
    std::string GetName() const override { return "gstreamer"; }

    std::vector<VideoDeviceInfo> ListCameras() override {
        std::vector<VideoDeviceInfo> out;
        GstDeviceMonitor* mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
        if (gst_device_monitor_start(mon)) {
            GList* devs = gst_device_monitor_get_devices(mon);
            bool first = true;
            for (GList* l = devs; l != nullptr; l = l->next) {
                auto* dev = static_cast<GstDevice*>(l->data);
                VideoDeviceInfo info;
                gchar* name = gst_device_get_display_name(dev);
                info.name = name ? name : "Camera";
                g_free(name);
                // V4L2 device path lives in the props structure.
                GstStructure* props = gst_device_get_properties(dev);
                if (props) {
                    const gchar* path = gst_structure_get_string(props, "device.path");
                    if (path) info.id = path;
                    gst_structure_free(props);
                }
                info.isDefault = first;
                first = false;
                out.push_back(std::move(info));
                gst_object_unref(dev);
            }
            g_list_free(devs);
            gst_device_monitor_stop(mon);
        }
        gst_object_unref(mon);
        return out;
    }

    VideoDeviceInfo GetDefaultCamera() override {
        auto cams = ListCameras();
        return cams.empty() ? VideoDeviceInfo{} : cams.front();
    }

    // Linux/V4L2 has no per-process camera permission gate.
    CameraPermission GetCameraPermission() override { return CameraPermission::Granted; }
    void RequestCameraPermission(std::function<void(bool)> cb) override { if (cb) cb(true); }

    std::unique_ptr<IVideoDecodeSession> OpenDecoder(const std::string& source) override {
        auto s = std::make_unique<GstDecodeSession>(source);
        if (!s->Build()) return nullptr;
        return s;
    }
    std::unique_ptr<IVideoCaptureSession> OpenCapture(const VideoCaptureParams& p) override {
        return std::make_unique<GstCaptureSession>(p);
    }

    UCVideoFramePtr GrabThumbnail(const std::string& source,
                                 const VideoThumbnailRequest& req) override {
        return GstGrabThumbnail(source, req);
    }
};

} // namespace

IVideoBackend* GetNullVideoBackend();   // from VideoBackendNull.cpp

IVideoBackend* GetVideoBackend() {
    static GstBackend backend;
    static IVideoBackend* selected =
        backend.Initialize() ? static_cast<IVideoBackend*>(&backend)
                             : GetNullVideoBackend();
    return selected;
}

} // namespace UltraCanvas
