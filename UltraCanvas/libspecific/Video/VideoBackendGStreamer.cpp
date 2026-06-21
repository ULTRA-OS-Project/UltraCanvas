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
// Version: 0.1.1
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "IVideoBackend.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/pbutils/pbutils.h>

#include <atomic>
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

        // Route playbin's video through our appsink; audio uses the auto sink.
        g_object_set(pipeline, "video-sink", appsink, nullptr);

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

    bool Play() override  { return SetState(GST_STATE_PLAYING); }
    bool Pause() override { return SetState(GST_STATE_PAUSED); }
    bool Stop() override {
        bool ok = SetState(GST_STATE_PAUSED);
        Seek(0.0);
        return ok;
    }

    bool Seek(double seconds) override {
        if (!pipeline) return false;
        gint64 pos = static_cast<gint64>(seconds * GST_SECOND);
        return gst_element_seek(pipeline, rate, GST_FORMAT_TIME,
                                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                GST_SEEK_TYPE_SET, pos,
                                GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE) == TRUE;
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
                    self->SetState(GST_STATE_PLAYING);
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
            default: break;
        }
        return TRUE;
    }

    void Teardown() {
        // Source was attached to a private context, so g_source_remove() (which
        // only searches the default context) won't find it — destroy via ptr.
        if (busSource) { g_source_destroy(busSource); g_source_unref(busSource); busSource = nullptr; }
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
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
        if (busSource) { g_source_destroy(busSource); g_source_unref(busSource); busSource = nullptr; }
        if (recValve) { gst_object_unref(recValve); recValve = nullptr; }
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
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
