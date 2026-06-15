// libspecific/Video/VideoBackendMediaFoundation.cpp
// Media Foundation video backend (Windows platform-native).
// Compiled when ULTRACANVAS_ENABLE_VIDEO is ON and the platform is Windows;
// otherwise the null backend is used.
//
//   Playback : IMFMediaSession driving a topology with a *sample-grabber sink*
//              on the video stream (RGB32 frames delivered to us) and the
//              Streaming Audio Renderer (SAR) on the audio stream. The session
//              owns the clock, so audio renders and A/V stays in sync while we
//              receive decoded video frames.
//   Capture  : IMFSourceReader pulls RGB32 webcam frames on a worker thread for
//              the live preview, and (while recording) feeds them to an
//              IMFSinkWriter encoding H.264/MP4.
//
// RGB32 in MF memory order is B,G,R,A, matching Cairo's ARGB32 layout, so frames
// are delivered as VideoPixelFormat::BGRA32 and need no swizzle.
//
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "IVideoBackend.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfobjects.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace UltraCanvas {

namespace {

const UINT32 kHnsPerSecond = 10000000; // 100-ns units per second

UCVideoFramePtr FrameFromRGB32(const uint8_t* data, int width, int height,
                               int stride, double pts) {
    if (!data || width <= 0 || height <= 0) return nullptr;
    auto frame = std::make_shared<UCVideoFrame>();
    VideoFrameInfo fi;
    fi.width = width; fi.height = height;
    fi.pixelFormat = VideoPixelFormat::BGRA32;
    fi.stride = width * 4;
    fi.pts = pts;
    auto& out = frame->MutableData();
    out.resize((size_t)fi.stride * height);
    const int srcStride = (stride != 0) ? stride : width * 4;
    for (int y = 0; y < height; ++y) {
        memcpy(out.data() + (size_t)y * fi.stride,
               data + (size_t)y * srcStride, (size_t)width * 4);
    }
    frame->SetInfo(fi);
    return frame;
}

// ---- Sample-grabber callback: receives decoded RGB32 video frames ---------

class GrabberCallback : public IMFSampleGrabberSinkCallback {
public:
    GrabberCallback(int w, int h) : width(w), height(h) {}

    std::function<void(UCVideoFramePtr)> onFrame;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMFSampleGrabberSinkCallback)) {
            *ppv = static_cast<IMFSampleGrabberSinkCallback*>(this); AddRef(); return S_OK;
        }
        if (riid == __uuidof(IMFClockStateSink)) {
            *ppv = static_cast<IMFClockStateSink*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref; if (c == 0) delete this; return c;
    }

    // IMFClockStateSink (required by the sink callback interface)
    STDMETHODIMP OnClockStart(MFTIME, LONGLONG) override { return S_OK; }
    STDMETHODIMP OnClockStop(MFTIME) override { return S_OK; }
    STDMETHODIMP OnClockPause(MFTIME) override { return S_OK; }
    STDMETHODIMP OnClockRestart(MFTIME) override { return S_OK; }
    STDMETHODIMP OnClockSetRate(MFTIME, float) override { return S_OK; }

    // IMFSampleGrabberSinkCallback
    STDMETHODIMP OnSetPresentationClock(IMFPresentationClock*) override { return S_OK; }
    STDMETHODIMP OnShutdown() override { return S_OK; }
    STDMETHODIMP OnProcessSample(REFGUID, DWORD, LONGLONG llSampleTime, LONGLONG,
                                 const BYTE* pSampleBuffer, DWORD dwSampleSize) override {
        if (onFrame && pSampleBuffer && dwSampleSize >= (DWORD)(width * height * 4)) {
            double pts = (double)llSampleTime / kHnsPerSecond;
            if (auto f = FrameFromRGB32(pSampleBuffer, width, height, width * 4, pts))
                onFrame(std::move(f));
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref{1};
    int width, height;
};

// ---- Decode session (Media Session + sample grabber + SAR) ----------------

class MFDecodeSession : public IMFAsyncCallback, public IVideoDecodeSession {
public:
    explicit MFDecodeSession(const std::string& source) : sourceStr(source) {}
    ~MFDecodeSession() override { Teardown(); }

    // IUnknown (for the async event callback)
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMFAsyncCallback)) {
            *ppv = static_cast<IMFAsyncCallback*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref; if (c == 0) delete this; return c;
    }
    STDMETHODIMP GetParameters(DWORD*, DWORD*) override { return E_NOTIMPL; }

    bool Build() {
        std::wstring wsrc = Widen(sourceStr);

        if (FAILED(MFCreateMediaSession(nullptr, &session))) return false;

        ComPtr<IMFSourceResolver> resolver;
        if (FAILED(MFCreateSourceResolver(&resolver))) return false;
        MF_OBJECT_TYPE objType = MF_OBJECT_INVALID;
        ComPtr<IUnknown> srcUnk;
        if (FAILED(resolver->CreateObjectFromURL(wsrc.c_str(), MF_RESOLUTION_MEDIASOURCE,
                                                 nullptr, &objType, &srcUnk)))
            return false;
        if (FAILED(srcUnk.As(&mediaSource))) return false;

        if (!BuildTopology()) return false;

        closeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // Begin the async event loop, then set the topology.
        session->BeginGetEvent(this, nullptr);
        return SUCCEEDED(session->SetTopology(0, topology.Get()));
    }

    bool Play() override {
        PROPVARIANT v; PropVariantInit(&v);
        HRESULT hr = session ? session->Start(&GUID_NULL, &v) : E_FAIL;
        PropVariantClear(&v);
        return SUCCEEDED(hr);
    }
    bool Pause() override { return session && SUCCEEDED(session->Pause()); }
    bool Stop() override  { return session && SUCCEEDED(session->Stop()); }

    bool Seek(double seconds) override {
        if (!session) return false;
        PROPVARIANT v; PropVariantInit(&v);
        v.vt = VT_I8;
        v.hVal.QuadPart = (LONGLONG)(seconds * kHnsPerSecond);
        HRESULT hr = session->Start(&GUID_NULL, &v);
        PropVariantClear(&v);
        return SUCCEEDED(hr);
    }

    double GetPosition() const override {
        if (!session) return 0.0;
        ComPtr<IMFClock> clock;
        if (FAILED(session->GetClock(&clock)) || !clock) return 0.0;
        ComPtr<IMFPresentationClock> pc;
        if (FAILED(clock.As(&pc))) return 0.0;
        MFTIME t = 0;
        if (FAILED(pc->GetTime(&t))) return 0.0;
        return (double)t / kHnsPerSecond;
    }
    double GetDuration() const override { return durationSeconds; }
    const VideoStreamInfo& GetStreamInfo() const override { return streamInfo; }

    void SetVolume(float vol) override {
        ComPtr<IMFSimpleAudioVolume> v;
        if (session && SUCCEEDED(MFGetService(session.Get(), MR_POLICY_VOLUME_SERVICE,
                                              IID_PPV_ARGS(&v))))
            v->SetMasterVolume(vol);
    }
    void SetMute(bool mute) override {
        ComPtr<IMFSimpleAudioVolume> v;
        if (session && SUCCEEDED(MFGetService(session.Get(), MR_POLICY_VOLUME_SERVICE,
                                              IID_PPV_ARGS(&v))))
            v->SetMute(mute ? TRUE : FALSE);
    }
    void SetLoop(bool loop) override { looping = loop; }
    void SetPlaybackRate(float r) override {
        rate = (r > 0.01f) ? r : 1.0f;
        ComPtr<IMFRateControl> rc;
        if (session && SUCCEEDED(MFGetService(session.Get(), MF_RATE_CONTROL_SERVICE,
                                              IID_PPV_ARGS(&rc))))
            rc->SetRate(FALSE, rate);
    }

    // IMFAsyncCallback::Invoke — session event pump
    STDMETHODIMP Invoke(IMFAsyncResult* result) override {
        if (!session) return S_OK;
        ComPtr<IMFMediaEvent> ev;
        if (FAILED(session->EndGetEvent(result, &ev)) || !ev) return S_OK;

        MediaEventType met = MEUnknown;
        ev->GetType(&met);
        if (met == MESessionEnded) {
            if (looping) { Seek(0.0); Play(); }
            else if (onEnded) onEnded();
        } else if (met == MESessionTopologyStatus) {
            UINT32 status = 0;
            ev->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);
            if (status == MF_TOPOSTATUS_READY) {
                ReadDurationAndInfo();
                if (onLoaded) onLoaded();
            }
        } else if (met == MEError) {
            if (onError) onError("Media Foundation session error");
        }

        if (met == MESessionClosed) {
            // Final event: stop the loop and let Teardown proceed.
            if (closeEvent) SetEvent(closeEvent);
        } else {
            session->BeginGetEvent(this, nullptr);
        }
        return S_OK;
    }

private:
    static std::wstring Widen(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    bool BuildTopology() {
        if (FAILED(mediaSource->CreatePresentationDescriptor(&presDesc))) return false;
        if (FAILED(MFCreateTopology(&topology))) return false;

        DWORD cStreams = 0;
        presDesc->GetStreamDescriptorCount(&cStreams);
        bool anyVideo = false;
        for (DWORD i = 0; i < cStreams; ++i) {
            BOOL selected = FALSE;
            ComPtr<IMFStreamDescriptor> sd;
            if (FAILED(presDesc->GetStreamDescriptorByIndex(i, &selected, &sd)) || !selected)
                continue;

            ComPtr<IMFMediaTypeHandler> handler;
            if (FAILED(sd->GetMediaTypeHandler(&handler))) continue;
            GUID major = GUID_NULL;
            handler->GetMajorType(&major);

            ComPtr<IMFActivate> sinkActivate;
            if (major == MFMediaType_Video) {
                ComPtr<IMFMediaType> nativeType;
                handler->GetCurrentMediaType(&nativeType);
                UINT32 w = 0, h = 0;
                if (nativeType) MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &w, &h);
                if (w == 0 || h == 0) { w = 1280; h = 720; }
                videoW = (int)w; videoH = (int)h;

                ComPtr<IMFMediaType> grabType;
                MFCreateMediaType(&grabType);
                grabType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                grabType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                MFSetAttributeSize(grabType.Get(), MF_MT_FRAME_SIZE, w, h);
                grabType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4)); // top-down

                grabber = new GrabberCallback((int)w, (int)h);
                grabber->onFrame = [this](UCVideoFramePtr f) { if (onFrame) onFrame(std::move(f)); };
                if (FAILED(MFCreateSampleGrabberSinkActivate(grabType.Get(), grabber, &sinkActivate)))
                    continue;
                anyVideo = true;
            } else if (major == MFMediaType_Audio) {
                if (FAILED(MFCreateAudioRendererActivate(&sinkActivate))) continue;
            } else {
                continue;
            }

            ComPtr<IMFTopologyNode> srcNode, outNode;
            MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &srcNode);
            srcNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource.Get());
            srcNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, presDesc.Get());
            srcNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, sd.Get());

            MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &outNode);
            outNode->SetObject(sinkActivate.Get());
            outNode->SetUINT32(MF_TOPONODE_STREAMID, 0);
            outNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);

            topology->AddNode(srcNode.Get());
            topology->AddNode(outNode.Get());
            srcNode->ConnectOutput(0, outNode.Get(), 0);
        }
        return anyVideo;
    }

    void ReadDurationAndInfo() {
        if (presDesc) {
            UINT64 dur = 0;
            if (SUCCEEDED(presDesc->GetUINT64(MF_PD_DURATION, &dur)))
                durationSeconds = (double)dur / kHnsPerSecond;
        }
        streamInfo.width = videoW;
        streamInfo.height = videoH;
        streamInfo.duration = durationSeconds;
    }

    void Teardown() {
        if (session) {
            // Close() is async; wait for MESessionClosed (handled in Invoke) so
            // Media Foundation releases all its references to this callback
            // before we free the object.
            session->Close();
            if (closeEvent) WaitForSingleObject(closeEvent, 5000);
            session->Shutdown();
        }
        if (mediaSource) mediaSource->Shutdown();
        if (grabber) { grabber->Release(); grabber = nullptr; }
        session.Reset(); mediaSource.Reset(); topology.Reset(); presDesc.Reset();
        if (closeEvent) { CloseHandle(closeEvent); closeEvent = nullptr; }
    }

    std::atomic<ULONG> ref{1};
    std::string sourceStr;
    HANDLE closeEvent = nullptr;
    ComPtr<IMFMediaSession> session;
    ComPtr<IMFMediaSource> mediaSource;
    ComPtr<IMFTopology> topology;
    ComPtr<IMFPresentationDescriptor> presDesc;
    GrabberCallback* grabber = nullptr;   // owned by the sink activate; we keep one ref
    VideoStreamInfo streamInfo;
    double durationSeconds = 0.0;
    int videoW = 0, videoH = 0;
    bool looping = false;
    float rate = 1.0f;
};

// ---- Capture session (SourceReader preview + SinkWriter recording) --------

class MFCaptureSession : public IVideoCaptureSession {
public:
    explicit MFCaptureSession(const VideoCaptureParams& p) : params(p) {}
    ~MFCaptureSession() override { Close(); }

    bool Open() override {
        if (opened) return true;
        // Resolve the camera media source by symbolic link (id) or first device.
        ComPtr<IMFAttributes> attrs;
        MFCreateAttributes(&attrs, 1);
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        ComPtr<IMFMediaSource> camSource;
        if (!params.cameraId.empty()) {
            attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                             Widen(params.cameraId).c_str());
            if (FAILED(MFCreateDeviceSource(attrs.Get(), &camSource))) { Fail("Camera open failed"); return false; }
        } else {
            IMFActivate** devs = nullptr; UINT32 count = 0;
            if (FAILED(MFEnumDeviceSources(attrs.Get(), &devs, &count)) || count == 0) {
                if (devs) CoTaskMemFree(devs);
                Fail("No camera found"); return false;
            }
            devs[0]->ActivateObject(IID_PPV_ARGS(&camSource));
            for (UINT32 i = 0; i < count; ++i) devs[i]->Release();
            CoTaskMemFree(devs);
        }
        if (!camSource) { Fail("Camera activate failed"); return false; }

        if (FAILED(MFCreateSourceReaderFromMediaSource(camSource.Get(), nullptr, &reader))) {
            Fail("SourceReader create failed"); return false;
        }
        // Request RGB32 output on the first video stream.
        ComPtr<IMFMediaType> rgb;
        MFCreateMediaType(&rgb);
        rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               nullptr, rgb.Get()))) {
            Fail("RGB32 not supported by camera"); return false;
        }
        // Learn the negotiated size.
        ComPtr<IMFMediaType> cur;
        reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
        UINT32 w = 0, h = 0;
        if (cur) MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
        camW = (int)(w ? w : params.width);
        camH = (int)(h ? h : params.height);

        opened = true;
        running = true;
        worker = std::thread([this]() { ReadLoop(); });
        return true;
    }

    void Close() override {
        running = false;
        recording = false;
        if (worker.joinable()) worker.join();
        FinalizeWriter();
        reader.Reset();
        opened = false;
    }
    bool IsOpen() const override { return opened; }

    bool Start() override {
        if (!opened && !Open()) return false;
        if (!InitWriter()) return false;
        startTime = GetTickCount64();
        recording = true;
        if (onStarted) onStarted();
        return true;
    }
    bool Pause() override  { recording = false; return true; }
    bool Resume() override { recording = true; return true; }
    bool Stop() override {
        if (!recording) return false;
        recording = false;
        FinalizeWriter();
        if (onStopped) onStopped();
        return true;
    }

    double GetElapsed() const override {
        return recording ? (double)(GetTickCount64() - startTime) / 1000.0 : 0.0;
    }
    const VideoCaptureParams& GetParams() const override { return params; }

private:
    static std::wstring Widen(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }
    void Fail(const std::string& msg) { if (onError) onError(msg); }

    bool InitWriter() {
        std::lock_guard<std::mutex> lk(writerMutex);
        if (FAILED(MFCreateSinkWriterFromURL(Widen(params.outputPath).c_str(),
                                             nullptr, nullptr, &writer)))
            { Fail("SinkWriter create failed"); return false; }

        // Output (encoded) H.264 type
        ComPtr<IMFMediaType> outType;
        MFCreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        outType->SetUINT32(MF_MT_AVG_BITRATE,
                           params.videoBitRate ? params.videoBitRate : 4000000);
        outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, camW, camH);
        MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE,
                            (UINT32)(params.frameRate > 0 ? params.frameRate : 30), 1);
        MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(writer->AddStream(outType.Get(), &videoStreamIndex))) {
            Fail("AddStream failed"); writer.Reset(); return false;
        }

        // Input (uncompressed) RGB32 type
        ComPtr<IMFMediaType> inType;
        MFCreateMediaType(&inType);
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, camW, camH);
        MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE,
                            (UINT32)(params.frameRate > 0 ? params.frameRate : 30), 1);
        MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(writer->SetInputMediaType(videoStreamIndex, inType.Get(), nullptr))) {
            Fail("SetInputMediaType failed"); writer.Reset(); return false;
        }

        if (FAILED(writer->BeginWriting())) { Fail("BeginWriting failed"); writer.Reset(); return false; }
        writeStartHns = 0;
        rtStart = 0;
        return true;
    }

    void FinalizeWriter() {
        std::lock_guard<std::mutex> lk(writerMutex);
        if (writer) { writer->Finalize(); writer.Reset(); }
    }

    void WriteFrame(IMFSample* sample, LONGLONG sampleTime) {
        std::lock_guard<std::mutex> lk(writerMutex);
        if (!writer) return;
        if (rtStart == 0) rtStart = sampleTime;
        sample->SetSampleTime(sampleTime - rtStart);
        LONGLONG dur = (LONGLONG)(kHnsPerSecond / (params.frameRate > 0 ? params.frameRate : 30));
        sample->SetSampleDuration(dur);
        writer->WriteSample(videoStreamIndex, sample);
    }

    void ReadLoop() {
        while (running) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                            0, &streamIndex, &flags, &timestamp, &sample);
            if (FAILED(hr)) break;
            if (!sample) continue;

            // Preview frame
            ComPtr<IMFMediaBuffer> buf;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
                BYTE* data = nullptr; DWORD maxLen = 0, curLen = 0;
                if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
                    double pts = (double)timestamp / kHnsPerSecond;
                    if (auto f = FrameFromRGB32(data, camW, camH, camW * 4, pts)) {
                        if (onPreviewFrame) onPreviewFrame(std::move(f));
                    }
                    buf->Unlock();
                }
            }
            if (recording) WriteFrame(sample.Get(), timestamp);
        }
    }

    VideoCaptureParams params;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFSinkWriter> writer;
    std::mutex writerMutex;
    DWORD videoStreamIndex = 0;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> recording{false};
    bool opened = false;
    int camW = 0, camH = 0;
    ULONGLONG startTime = 0;
    LONGLONG writeStartHns = 0;
    LONGLONG rtStart = 0;
};

// ---- Backend --------------------------------------------------------------

class MFBackend : public IVideoBackend {
public:
    bool Initialize() override {
        if (initialized) return true;
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) { CoUninitialize(); return false; }
        initialized = true;
        return true;
    }
    void Shutdown() override {
        if (!initialized) return;
        MFShutdown();
        CoUninitialize();
        initialized = false;
    }
    std::string GetName() const override { return "mediafoundation"; }

    std::vector<VideoDeviceInfo> ListCameras() override {
        std::vector<VideoDeviceInfo> out;
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 1))) return out;
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devs = nullptr; UINT32 count = 0;
        if (FAILED(MFEnumDeviceSources(attrs.Get(), &devs, &count))) return out;
        for (UINT32 i = 0; i < count; ++i) {
            VideoDeviceInfo info;
            WCHAR* name = nullptr; UINT32 nameLen = 0;
            if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                      &name, &nameLen)) && name) {
                info.name = Narrow(name); CoTaskMemFree(name);
            }
            WCHAR* link = nullptr; UINT32 linkLen = 0;
            if (SUCCEEDED(devs[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &link, &linkLen)) && link) {
                info.id = Narrow(link); CoTaskMemFree(link);
            }
            info.isDefault = (i == 0);
            out.push_back(std::move(info));
            devs[i]->Release();
        }
        CoTaskMemFree(devs);
        return out;
    }
    VideoDeviceInfo GetDefaultCamera() override {
        auto cams = ListCameras();
        return cams.empty() ? VideoDeviceInfo{} : cams.front();
    }

    // Windows surfaces camera consent through the Settings privacy UI; there is
    // no synchronous per-process gate, so we report Granted and let device
    // activation fail if the user has blocked access.
    CameraPermission GetCameraPermission() override { return CameraPermission::Granted; }
    void RequestCameraPermission(std::function<void(bool)> cb) override { if (cb) cb(true); }

    std::unique_ptr<IVideoDecodeSession> OpenDecoder(const std::string& source) override {
        // The session is COM-refcounted (IMFAsyncCallback) and starts at ref 1.
        // Build()'s Teardown-on-failure path releases it; on success we hand the
        // single reference to the unique_ptr. Its default `delete` runs the
        // virtual destructor, whose Teardown performs the close-wait so MF holds
        // no references by the time the memory is freed.
        auto* s = new MFDecodeSession(source);
        if (!s->Build()) { s->Release(); return nullptr; }
        return std::unique_ptr<IVideoDecodeSession>(s);
    }
    std::unique_ptr<IVideoCaptureSession> OpenCapture(const VideoCaptureParams& p) override {
        return std::make_unique<MFCaptureSession>(p);
    }

private:
    static std::string Narrow(const WCHAR* w) {
        if (!w) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "";
        std::string s(n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
        return s;
    }
    bool initialized = false;
};

} // namespace

IVideoBackend* GetNullVideoBackend();   // from VideoBackendNull.cpp

IVideoBackend* GetVideoBackend() {
    static MFBackend backend;
    static IVideoBackend* selected =
        backend.Initialize() ? static_cast<IVideoBackend*>(&backend)
                             : GetNullVideoBackend();
    return selected;
}

} // namespace UltraCanvas
