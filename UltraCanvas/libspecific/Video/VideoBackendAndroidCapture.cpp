// libspecific/Video/VideoBackendAndroidCapture.cpp
// Android camera capture: NDK Camera2 for the device, an AImageReader for the
// live preview, and for recording a MediaCodec video encoder fed straight from
// the camera through its input surface, an AAC encoder fed from an AAudio
// input stream, and AMediaMuxer writing the MP4.
//
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "VideoBackendAndroid.h"
#include "../../OS/Android/UltraCanvasAndroidPermissions.h"
#include "UltraCanvasDebug.h"

#include <aaudio/AAudio.h>
#include <android/native_window.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace UltraCanvas {
namespace AndroidVideo {

namespace {

using SteadyClock = std::chrono::steady_clock;

// MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface: the encoder takes its
// frames from an input surface (here: straight from the camera).
constexpr int32_t kColorFormatSurface = 0x7F000789;
// MediaCodec.BUFFER_FLAG_KEY_FRAME; the NDK constant is newer than minSdk 26.
constexpr uint32_t kBufferFlagKeyFrame = 1;
// MediaCodecInfo.CodecProfileLevel.AACObjectLC.
constexpr int32_t kAacProfileLc = 2;

struct CameraDescription {
    std::string id;
    uint8_t facing = ACAMERA_LENS_FACING_BACK;
    int32_t sensorOrientation = 0;
    std::vector<CameraMode> modes;   // YUV_420_888 output sizes
    std::vector<std::pair<int32_t, int32_t>> fpsRanges;
};

bool Describe(ACameraManager* manager, const char* id, CameraDescription& out) {
    ACameraMetadata* meta = nullptr;
    if (ACameraManager_getCameraCharacteristics(manager, id, &meta) != ACAMERA_OK || !meta) return false;
    out.id = id;
    ACameraMetadata_const_entry entry{};
    if (ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK && entry.count > 0) {
        out.facing = entry.data.u8[0];
    }
    if (ACameraMetadata_getConstEntry(meta, ACAMERA_SENSOR_ORIENTATION, &entry) == ACAMERA_OK && entry.count > 0) {
        out.sensorOrientation = entry.data.i32[0];
    }
    double maxFps = 30.0;
    if (ACameraMetadata_getConstEntry(meta, ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &entry) == ACAMERA_OK) {
        maxFps = 0.0;
        for (uint32_t i = 0; i + 1 < entry.count; i += 2) {
            out.fpsRanges.emplace_back(entry.data.i32[i], entry.data.i32[i + 1]);
            maxFps = std::max(maxFps, static_cast<double>(entry.data.i32[i + 1]));
        }
    }
    if (ACameraMetadata_getConstEntry(meta, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
        for (uint32_t i = 0; i + 3 < entry.count; i += 4) {
            const int32_t format = entry.data.i32[i];
            const int32_t isInput = entry.data.i32[i + 3];
            if (format != AIMAGE_FORMAT_YUV_420_888 ||
                isInput != ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) continue;
            CameraMode mode;
            mode.width = entry.data.i32[i + 1];
            mode.height = entry.data.i32[i + 2];
            mode.frameRate = maxFps;
            out.modes.push_back(mode);
        }
    }
    ACameraMetadata_free(meta);
    return true;
}

std::vector<CameraDescription> DescribeAll(ACameraManager* manager) {
    std::vector<CameraDescription> cameras;
    ACameraIdList* ids = nullptr;
    if (ACameraManager_getCameraIdList(manager, &ids) != ACAMERA_OK || !ids) return cameras;
    for (int i = 0; i < ids->numCameras; ++i) {
        CameraDescription d;
        if (Describe(manager, ids->cameraIds[i], d)) cameras.push_back(std::move(d));
    }
    ACameraManager_deleteCameraIdList(ids);
    return cameras;
}

// The default is the first back-facing camera: the one a phone user means by
// "the camera".
int DefaultIndex(const std::vector<CameraDescription>& cameras) {
    for (size_t i = 0; i < cameras.size(); ++i) {
        if (cameras[i].facing == ACAMERA_LENS_FACING_BACK) return static_cast<int>(i);
    }
    return cameras.empty() ? -1 : 0;
}

std::string CameraName(const CameraDescription& d, int externalIndex) {
    switch (d.facing) {
        case ACAMERA_LENS_FACING_FRONT: return "Front camera";
        case ACAMERA_LENS_FACING_BACK:  return "Back camera";
        default: return "External camera " + std::to_string(externalIndex);
    }
}

// The output size closest to the request: an exact match, else the nearest
// area among sizes of the same shape, else the nearest area of any shape.
CameraMode ChooseMode(const std::vector<CameraMode>& modes, int width, int height) {
    CameraMode best;
    if (modes.empty()) return best;
    const double wantArea = static_cast<double>(std::max(1, width)) * std::max(1, height);
    const double wantAspect = static_cast<double>(std::max(1, width)) / std::max(1, height);
    double bestScore = 1e300;
    for (const CameraMode& m : modes) {
        if (m.width == width && m.height == height) return m;
        const double aspect = static_cast<double>(m.width) / std::max(1, m.height);
        const double areaRatio = std::fabs(std::log((static_cast<double>(m.width) * m.height) / wantArea));
        const double score = areaRatio + (std::fabs(aspect - wantAspect) > 0.01 ? 10.0 : 0.0);
        if (score < bestScore) {
            bestScore = score;
            best = m;
        }
    }
    return best;
}

// The fixed or variable frame-rate range that best serves `fps`: one whose
// top is the request, preferring a fixed rate (steadier for recording).
std::pair<int32_t, int32_t> ChooseFpsRange(const std::vector<std::pair<int32_t, int32_t>>& ranges,
                                           double fps) {
    std::pair<int32_t, int32_t> best{0, 0};
    double bestScore = 1e300;
    const int want = static_cast<int>(std::lround(fps > 0.0 ? fps : 30.0));
    for (const auto& r : ranges) {
        const double score = std::abs(r.second - want) * 10.0 + (r.second - r.first);
        if (score < bestScore) {
            bestScore = score;
            best = r;
        }
    }
    return best;
}

// ===== CAPTURE SESSION =====

class AndroidCaptureSession : public IVideoCaptureSession {
public:
    explicit AndroidCaptureSession(const VideoCaptureParams& p) : params(p) {}
    ~AndroidCaptureSession() override { Close(); }

    bool Open() override {
        if (opened) return true;
        if (!AndroidPermissions::IsGranted(AndroidPermissions::Camera)) {
            Fail("Camera permission not granted: call "
                 "UltraCanvasVideoDevices::RequestCameraPermission first, and declare "
                 "android.permission.CAMERA in the app's manifest");
            return false;
        }
        manager = ACameraManager_create();
        if (!manager) { Fail("Camera service unavailable"); return false; }

        const auto cameras = DescribeAll(manager);
        int index = -1;
        for (size_t i = 0; i < cameras.size(); ++i) {
            if (cameras[i].id == params.cameraId) index = static_cast<int>(i);
        }
        if (index < 0) index = DefaultIndex(cameras);
        if (index < 0) { Fail("No camera available"); CloseCamera(); return false; }
        camera = cameras[static_cast<size_t>(index)];

        mode = ChooseMode(camera.modes, params.width, params.height);
        if (mode.width <= 0 || mode.height <= 0) {
            Fail("The camera offers no YUV output");
            CloseCamera();
            return false;
        }
        fpsRange = ChooseFpsRange(camera.fpsRanges, params.frameRate);

        // Turn frames upright for the display as it is now: the sensor is
        // mounted at its own angle, and a front camera turns the other way.
        const int display = AndroidPermissions::DisplayRotationDegrees();
        mirror = camera.facing == ACAMERA_LENS_FACING_FRONT;
        rotation = mirror ? (camera.sensorOrientation + display) % 360
                          : (camera.sensorOrientation - display + 360) % 360;

        if (AImageReader_new(mode.width, mode.height, AIMAGE_FORMAT_YUV_420_888, 4,
                             &previewReader) != AMEDIA_OK) {
            Fail("Cannot create the preview image reader");
            CloseCamera();
            return false;
        }
        AImageReader_getWindow(previewReader, &previewWindow);
        previewListener.context = this;
        previewListener.onImageAvailable = &AndroidCaptureSession::OnPreviewImage;
        AImageReader_setImageListener(previewReader, &previewListener);

        deviceCallbacks.context = this;
        deviceCallbacks.onDisconnected = &AndroidCaptureSession::OnDeviceDisconnected;
        deviceCallbacks.onError = &AndroidCaptureSession::OnDeviceError;
        if (ACameraManager_openCamera(manager, camera.id.c_str(), &deviceCallbacks, &device) != ACAMERA_OK ||
            !device) {
            Fail("Cannot open camera " + camera.id);
            CloseCamera();
            return false;
        }
        if (!StartCameraSession(nullptr)) {
            Fail("Cannot start the camera preview");
            CloseCamera();
            return false;
        }
        opened = true;
        return true;
    }

    void Close() override {
        if (recording) Stop();
        CloseCamera();
        opened = false;
    }
    bool IsOpen() const override { return opened; }

    bool Start() override {
        if (recording) return false;
        if (!opened && !Open()) return false;

        const bool mp4 = params.container == VideoContainer::MP4 ||
                         params.container == VideoContainer::MOV ||
                         params.container == VideoContainer::Autodetect;
        const char* videoMime = params.codec == VideoCodec::H265 ? "video/hevc"
                              : (params.codec == VideoCodec::H264 ||
                                 params.codec == VideoCodec::Autodetect) ? "video/avc" : nullptr;
        if (!mp4 || !videoMime) {
            Fail("Android records MP4 with H.264 or H.265 video and AAC audio");
            return false;
        }

        outputFd = open(params.outputPath.c_str(), O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0644);
        if (outputFd < 0) { Fail("Cannot write " + params.outputPath); return false; }
        muxer = AMediaMuxer_new(outputFd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!muxer) { Fail("Cannot create the MP4 writer"); ReleaseRecorder(); return false; }
        // The file plays upright without turning the pixels; a player that
        // honours the hint shows a front camera unmirrored, as recorded.
        AMediaMuxer_setOrientationHint(muxer, rotation);

        if (!CreateVideoEncoder(videoMime)) {
            Fail(std::string("No ") + videoMime + " encoder for " + std::to_string(mode.width) +
                 "x" + std::to_string(mode.height));
            ReleaseRecorder();
            return false;
        }
        audioEnabled = params.captureAudio && CreateAudioRecorder();
        expectedTracks = audioEnabled ? 2 : 1;

        if (!StartCameraSession(encoderWindow)) {
            Fail("Cannot start recording from the camera");
            ReleaseRecorder();
            StartCameraSession(nullptr);
            return false;
        }

        stopping = false;
        paused = false;
        pausedTotal = SteadyClock::duration::zero();
        startTime = SteadyClock::now();
        recording = true;
        videoThread = std::thread([this] { DrainVideo(); });
        if (audioEnabled) audioThread = std::thread([this] { RecordAudio(); });
        if (onStarted) onStarted();
        return true;
    }

    bool Pause() override {
        if (!recording || paused) return false;
        pauseStart = SteadyClock::now();
        paused = true;
        return true;
    }

    bool Resume() override {
        if (!recording || !paused) return false;
        pausedTotal += SteadyClock::now() - pauseStart;
        resumed = true;
        paused = false;
        // The first frame after the gap must be decodable on its own.
        if (videoEncoder) {
            AMediaFormat* p = AMediaFormat_new();
            AMediaFormat_setInt32(p, "request-sync", 0);   // MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME
            AMediaCodec_setParameters(videoEncoder, p);
            AMediaFormat_delete(p);
        }
        return true;
    }

    bool Stop() override {
        if (!recording) return false;
        stopping = true;
        AMediaCodec_signalEndOfInputStream(videoEncoder);
        if (videoThread.joinable()) videoThread.join();
        if (audioThread.joinable()) audioThread.join();
        // Back to preview only before the encoder (and its surface) go away:
        // a camera still drawing into a released surface errors out.
        StartCameraSession(nullptr);
        {
            std::lock_guard<std::mutex> lock(muxerMutex);
            if (muxerStarted) AMediaMuxer_stop(muxer);
        }
        const bool wroteSomething = muxerStarted;
        ReleaseRecorder();
        recording = false;
        if (!wroteSomething) Fail("Recording produced no frames");
        if (onStopped) onStopped();
        return true;
    }

    double GetElapsed() const override {
        if (!recording) return 0.0;
        auto active = SteadyClock::now() - startTime - pausedTotal;
        if (paused) active -= SteadyClock::now() - pauseStart;
        return std::max(0.0, std::chrono::duration<double>(active).count());
    }
    const VideoCaptureParams& GetParams() const override { return params; }

private:
    void Fail(const std::string& message) {
        debugOutput << "Android capture: " << message << std::endl;
        if (onError) onError(message);
    }

    // ---- Camera ----

    // (Re)build the capture session: the preview always, plus the encoder's
    // input surface while recording. Camera2 cannot add a target to a running
    // session, so starting and stopping a recording rebuilds it.
    bool StartCameraSession(ANativeWindow* encoderSurface) {
        CloseCameraSession();
        if (!device || !previewWindow) return false;

        if (ACaptureSessionOutputContainer_create(&outputs) != ACAMERA_OK) return false;
        ANativeWindow* windows[2] = { previewWindow, encoderSurface };
        const int count = encoderSurface ? 2 : 1;
        for (int i = 0; i < count; ++i) {
            if (ACaptureSessionOutput_create(windows[i], &sessionOutputs[i]) != ACAMERA_OK) return false;
            ACaptureSessionOutputContainer_add(outputs, sessionOutputs[i]);
        }
        sessionCallbacks.context = this;
        sessionCallbacks.onClosed = &AndroidCaptureSession::OnSessionEvent;
        sessionCallbacks.onReady = &AndroidCaptureSession::OnSessionEvent;
        sessionCallbacks.onActive = &AndroidCaptureSession::OnSessionEvent;
        if (ACameraDevice_createCaptureSession(device, outputs, &sessionCallbacks, &session) != ACAMERA_OK) {
            session = nullptr;
            return false;
        }
        if (ACameraDevice_createCaptureRequest(device, encoderSurface ? TEMPLATE_RECORD : TEMPLATE_PREVIEW,
                                               &request) != ACAMERA_OK) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            if (ACameraOutputTarget_create(windows[i], &targets[i]) != ACAMERA_OK) return false;
            ACaptureRequest_addTarget(request, targets[i]);
        }
        if (fpsRange.second > 0) {
            const int32_t range[2] = { fpsRange.first, fpsRange.second };
            ACaptureRequest_setEntry_i32(request, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, range);
        }
        return ACameraCaptureSession_setRepeatingRequest(session, nullptr, 1, &request, nullptr) == ACAMERA_OK;
    }

    void CloseCameraSession() {
        if (session) {
            ACameraCaptureSession_stopRepeating(session);
            ACameraCaptureSession_close(session);
            session = nullptr;
        }
        if (request) { ACaptureRequest_free(request); request = nullptr; }
        for (auto& t : targets) {
            if (t) { ACameraOutputTarget_free(t); t = nullptr; }
        }
        for (auto& o : sessionOutputs) {
            if (o) {
                if (outputs) ACaptureSessionOutputContainer_remove(outputs, o);
                ACaptureSessionOutput_free(o);
                o = nullptr;
            }
        }
        if (outputs) { ACaptureSessionOutputContainer_free(outputs); outputs = nullptr; }
    }

    void CloseCamera() {
        CloseCameraSession();
        if (device) { ACameraDevice_close(device); device = nullptr; }
        if (previewReader) {
            AImageReader_setImageListener(previewReader, nullptr);
            AImageReader_delete(previewReader);   // also releases previewWindow
            previewReader = nullptr;
            previewWindow = nullptr;
        }
        if (manager) { ACameraManager_delete(manager); manager = nullptr; }
    }

    // Camera YUV is full-range BT.601 (JFIF), whatever the resolution.
    static void OnPreviewImage(void* context, AImageReader* reader) {
        auto* self = static_cast<AndroidCaptureSession*>(context);
        AImage* image = nullptr;
        if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image) return;
        int64_t timestampNs = 0;
        AImage_getTimestamp(image, &timestampNs);
        const YuvColorInfo jfif{false, true};
        UCVideoFramePtr frame = FrameFromYuvImage(image, self->rotation, self->mirror, jfif,
                                                  static_cast<double>(timestampNs) / 1e9);
        AImage_delete(image);
        if (frame && self->onPreviewFrame) self->onPreviewFrame(std::move(frame));
    }

    static void OnDeviceDisconnected(void* context, ACameraDevice*) {
        static_cast<AndroidCaptureSession*>(context)->Fail("The camera was disconnected");
    }
    static void OnDeviceError(void* context, ACameraDevice*, int error) {
        static_cast<AndroidCaptureSession*>(context)->Fail("Camera error " + std::to_string(error));
    }
    static void OnSessionEvent(void*, ACameraCaptureSession*) {}

    // ---- Recording ----

    bool CreateVideoEncoder(const char* mime) {
        videoEncoder = AMediaCodec_createEncoderByType(mime);
        if (!videoEncoder) return false;
        const double fps = fpsRange.second > 0 ? fpsRange.second
                         : (params.frameRate > 0.0 ? params.frameRate : 30.0);
        const int32_t bitRate = params.videoBitRate > 0 ? params.videoBitRate
                : static_cast<int32_t>(std::min(50e6, mode.width * mode.height * fps * 0.15));
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, mode.width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, mode.height);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatSurface);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitRate);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<int32_t>(std::lround(fps)));
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
        frameDurationUs = static_cast<int64_t>(1e6 / fps);
        const bool ok =
                AMediaCodec_configure(videoEncoder, format, nullptr, nullptr,
                                      AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
                AMediaCodec_createInputSurface(videoEncoder, &encoderWindow) == AMEDIA_OK &&
                AMediaCodec_start(videoEncoder) == AMEDIA_OK;
        AMediaFormat_delete(format);
        return ok;
    }

    // Microphone + AAC encoder. False (record video only) without
    // RECORD_AUDIO or a working microphone - a silent recording is better
    // than none.
    bool CreateAudioRecorder() {
        if (!AndroidPermissions::IsGranted(AndroidPermissions::RecordAudio)) {
            debugOutput << "Android capture: no RECORD_AUDIO permission, recording without sound"
                        << std::endl;
            return false;
        }
        AAudioStreamBuilder* builder = nullptr;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setSampleRate(builder, 48000);
        AAudioStreamBuilder_setChannelCount(builder, 1);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        const aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &micStream);
        AAudioStreamBuilder_delete(builder);
        if (result != AAUDIO_OK) {
            micStream = nullptr;
            debugOutput << "Android capture: microphone unavailable ("
                        << AAudio_convertResultToText(result) << "), recording without sound" << std::endl;
            return false;
        }
        micRate = AAudioStream_getSampleRate(micStream);
        micChannels = AAudioStream_getChannelCount(micStream);

        audioEncoder = AMediaCodec_createEncoderByType("audio/mp4a-latm");
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, micRate);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, micChannels);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, 128000);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, kAacProfileLc);
        const bool ok = audioEncoder &&
                AMediaCodec_configure(audioEncoder, format, nullptr, nullptr,
                                      AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
                AMediaCodec_start(audioEncoder) == AMEDIA_OK &&
                AAudioStream_requestStart(micStream) == AAUDIO_OK;
        AMediaFormat_delete(format);
        if (!ok) {
            debugOutput << "Android capture: no AAC encoder, recording without sound" << std::endl;
            ReleaseAudio();
        }
        return ok;
    }

    void ReleaseAudio() {
        if (micStream) { AAudioStream_requestStop(micStream); AAudioStream_close(micStream); micStream = nullptr; }
        if (audioEncoder) { AMediaCodec_stop(audioEncoder); AMediaCodec_delete(audioEncoder); audioEncoder = nullptr; }
    }

    void ReleaseRecorder() {
        ReleaseAudio();
        if (videoEncoder) { AMediaCodec_stop(videoEncoder); AMediaCodec_delete(videoEncoder); videoEncoder = nullptr; }
        if (encoderWindow) { ANativeWindow_release(encoderWindow); encoderWindow = nullptr; }
        if (muxer) { AMediaMuxer_delete(muxer); muxer = nullptr; }
        if (outputFd >= 0) { close(outputFd); outputFd = -1; }
        std::lock_guard<std::mutex> lock(muxerMutex);
        muxerStarted = false;
        tracksAdded = 0;
        videoTrack = audioTrack = -1;
        pendingSamples.clear();
        videoBaseUs = audioBaseUs = -1;
        videoShiftUs = 0;
        lastVideoUs = -1;
    }

    // ---- Muxing ----
    // The muxer can only start once every track's format is known (each
    // encoder reports its own when its first output is ready), so earlier
    // samples wait in pendingSamples.

    struct PendingSample {
        int track;
        std::vector<uint8_t> data;
        AMediaCodecBufferInfo info;
    };

    void AddTrack(bool video, AMediaFormat* format) {
        std::lock_guard<std::mutex> lock(muxerMutex);
        const ssize_t track = AMediaMuxer_addTrack(muxer, format);
        if (track < 0) return;
        (video ? videoTrack : audioTrack) = static_cast<int>(track);
        if (++tracksAdded == expectedTracks && AMediaMuxer_start(muxer) == AMEDIA_OK) {
            muxerStarted = true;
            for (const PendingSample& p : pendingSamples) {
                AMediaMuxer_writeSampleData(muxer, static_cast<size_t>(p.track), p.data.data(), &p.info);
            }
            pendingSamples.clear();
        }
    }

    // `data` points at the sample itself, so the offset the encoder reported
    // is already applied: the muxer must not add it a second time.
    void WriteSample(bool video, const uint8_t* data, AMediaCodecBufferInfo info) {
        info.offset = 0;
        std::lock_guard<std::mutex> lock(muxerMutex);
        const int track = video ? videoTrack : audioTrack;
        if (track < 0) return;
        if (muxerStarted) {
            AMediaMuxer_writeSampleData(muxer, static_cast<size_t>(track), data, &info);
        } else {
            pendingSamples.push_back({ track, std::vector<uint8_t>(data, data + info.size), info });
        }
    }

    // Video: the camera stamps each frame with its sensor time. Each track is
    // made to start at zero on its own, and a pause is cut out by shifting
    // everything after it back by the gap.
    void DrainVideo() {
        auto deadline = SteadyClock::time_point::max();
        bool waitingForKeyFrame = false;
        for (;;) {
            if (stopping && deadline == SteadyClock::time_point::max()) {
                deadline = SteadyClock::now() + std::chrono::seconds(2);
            }
            if (SteadyClock::now() > deadline) break;   // the encoder never sent EOS
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(videoEncoder, &info, 10000);
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (AMediaFormat* format = AMediaCodec_getOutputFormat(videoEncoder)) {
                    AddTrack(true, format);
                    AMediaFormat_delete(format);
                }
                continue;
            }
            if (index < 0) continue;
            size_t size = 0;
            uint8_t* data = AMediaCodec_getOutputBuffer(videoEncoder, static_cast<size_t>(index), &size);
            const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
            if (data && info.size > 0 && !config) {
                if (paused) {
                    waitingForKeyFrame = true;          // dropped: inside the pause
                } else {
                    if (resumed.exchange(false)) waitingForKeyFrame = true;
                    const bool key = (info.flags & kBufferFlagKeyFrame) != 0;
                    if (!waitingForKeyFrame || key) {
                        if (waitingForKeyFrame && lastVideoUs >= 0) {
                            // Close the gap the pause left.
                            videoShiftUs += info.presentationTimeUs - lastVideoUs - frameDurationUs;
                        }
                        waitingForKeyFrame = false;
                        if (videoBaseUs < 0) videoBaseUs = info.presentationTimeUs;
                        lastVideoUs = info.presentationTimeUs;
                        AMediaCodecBufferInfo out = info;
                        out.presentationTimeUs = std::max<int64_t>(
                                0, info.presentationTimeUs - videoBaseUs - videoShiftUs);
                        WriteSample(true, data + info.offset, out);
                    }
                }
            }
            AMediaCodec_releaseOutputBuffer(videoEncoder, static_cast<size_t>(index), false);
            if (eos) break;
        }
    }

    // Audio: microphone -> AAC encoder -> muxer. Timestamps count the samples
    // kept, so a pause (whose samples are read and thrown away) leaves no gap.
    void RecordAudio() {
        const int32_t chunkFrames = 1024;   // one AAC frame
        std::vector<int16_t> buffer(static_cast<size_t>(chunkFrames) * micChannels);
        int64_t framesKept = 0;
        bool eosQueued = false;
        auto drain = [&](int64_t timeoutUs) -> bool {   // true once EOS came out
            for (;;) {
                AMediaCodecBufferInfo info{};
                const ssize_t index = AMediaCodec_dequeueOutputBuffer(audioEncoder, &info, timeoutUs);
                if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                    if (AMediaFormat* format = AMediaCodec_getOutputFormat(audioEncoder)) {
                        AddTrack(false, format);
                        AMediaFormat_delete(format);
                    }
                    continue;
                }
                if (index < 0) return false;
                size_t size = 0;
                uint8_t* data = AMediaCodec_getOutputBuffer(audioEncoder, static_cast<size_t>(index), &size);
                const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                if (data && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                    if (audioBaseUs < 0) audioBaseUs = info.presentationTimeUs;
                    AMediaCodecBufferInfo out = info;
                    out.presentationTimeUs -= audioBaseUs;
                    WriteSample(false, data + info.offset, out);
                }
                AMediaCodec_releaseOutputBuffer(audioEncoder, static_cast<size_t>(index), false);
                if (eos) return true;
            }
        };

        const auto giveUp = [] { return SteadyClock::now() + std::chrono::seconds(2); };
        auto deadline = SteadyClock::time_point::max();
        for (;;) {
            if (stopping && !eosQueued) {
                const ssize_t index = AMediaCodec_dequeueInputBuffer(audioEncoder, 10000);
                if (index >= 0) {
                    AMediaCodec_queueInputBuffer(audioEncoder, static_cast<size_t>(index), 0, 0,
                                                 static_cast<uint64_t>(framesKept * 1000000LL / micRate),
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    eosQueued = true;
                    deadline = giveUp();
                }
            }
            if (eosQueued) {
                if (drain(10000) || SteadyClock::now() > deadline) break;
                continue;
            }

            const aaudio_result_t n = AAudioStream_read(micStream, buffer.data(), chunkFrames,
                                                        100 * 1000 * 1000LL);
            if (n > 0 && !paused) {
                const ssize_t index = AMediaCodec_dequeueInputBuffer(audioEncoder, 10000);
                if (index >= 0) {
                    size_t capacity = 0;
                    uint8_t* in = AMediaCodec_getInputBuffer(audioEncoder, static_cast<size_t>(index), &capacity);
                    const size_t bytes = std::min(capacity, static_cast<size_t>(n) * micChannels * sizeof(int16_t));
                    if (in) std::memcpy(in, buffer.data(), bytes);
                    AMediaCodec_queueInputBuffer(audioEncoder, static_cast<size_t>(index), 0, in ? bytes : 0,
                                                 static_cast<uint64_t>(framesKept * 1000000LL / micRate), 0);
                    framesKept += n;
                }
            }
            drain(0);
        }
    }

    VideoCaptureParams params;

    // Camera
    ACameraManager* manager = nullptr;
    ACameraDevice* device = nullptr;
    ACameraDevice_StateCallbacks deviceCallbacks{};
    ACameraCaptureSession* session = nullptr;
    ACameraCaptureSession_stateCallbacks sessionCallbacks{};
    ACaptureSessionOutputContainer* outputs = nullptr;
    ACaptureSessionOutput* sessionOutputs[2] = {};
    ACameraOutputTarget* targets[2] = {};
    ACaptureRequest* request = nullptr;
    AImageReader* previewReader = nullptr;
    ANativeWindow* previewWindow = nullptr;   // owned by previewReader
    AImageReader_ImageListener previewListener{};
    CameraDescription camera;
    CameraMode mode;
    std::pair<int32_t, int32_t> fpsRange{0, 0};
    int rotation = 0;
    bool mirror = false;
    bool opened = false;

    // Recording
    AMediaCodec* videoEncoder = nullptr;
    ANativeWindow* encoderWindow = nullptr;   // ours to release
    AMediaCodec* audioEncoder = nullptr;
    AAudioStream* micStream = nullptr;
    int32_t micRate = 48000;
    int32_t micChannels = 1;
    bool audioEnabled = false;
    AMediaMuxer* muxer = nullptr;
    int outputFd = -1;
    std::thread videoThread, audioThread;
    std::atomic<bool> recording{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> resumed{false};
    SteadyClock::time_point startTime{}, pauseStart{};
    SteadyClock::duration pausedTotal{};
    int64_t frameDurationUs = 33333;

    // Muxer state; guarded by muxerMutex.
    std::mutex muxerMutex;
    bool muxerStarted = false;
    int expectedTracks = 1;
    int tracksAdded = 0;
    int videoTrack = -1, audioTrack = -1;
    std::vector<PendingSample> pendingSamples;
    // Video drain thread only.
    int64_t videoBaseUs = -1, videoShiftUs = 0, lastVideoUs = -1;
    // Audio thread only.
    int64_t audioBaseUs = -1;
};

} // namespace

std::vector<VideoDeviceInfo> ListAndroidCameras() {
    std::vector<VideoDeviceInfo> out;
    ACameraManager* manager = ACameraManager_create();
    if (!manager) return out;
    const auto cameras = DescribeAll(manager);
    ACameraManager_delete(manager);
    const int defaultIndex = DefaultIndex(cameras);
    int external = 0;
    for (size_t i = 0; i < cameras.size(); ++i) {
        VideoDeviceInfo info;
        info.id = cameras[i].id;
        if (cameras[i].facing == ACAMERA_LENS_FACING_EXTERNAL) ++external;
        info.name = CameraName(cameras[i], external);
        info.isDefault = static_cast<int>(i) == defaultIndex;
        info.supportedModes = cameras[i].modes;
        out.push_back(std::move(info));
    }
    return out;
}

std::unique_ptr<IVideoCaptureSession> CreateAndroidCaptureSession(const VideoCaptureParams& params) {
    return std::make_unique<AndroidCaptureSession>(params);
}

} // namespace AndroidVideo
} // namespace UltraCanvas
