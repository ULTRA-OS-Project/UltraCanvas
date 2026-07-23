// libspecific/Video/VideoBackendAVFoundation.mm
// AVFoundation-backed video backend (macOS platform-native). Objective-C++.
// Compiled with -fobjc-arc when ULTRACANVAS_ENABLE_VIDEO is ON and the platform
// is macOS; otherwise the null backend is used.
//
//   Playback : AVPlayer + AVPlayerItemVideoOutput. AVPlayer renders the audio
//              track itself, so A/V stays in sync against its clock; we pull
//              BGRA frames on a display-rate GCD timer.
//   Capture  : AVCaptureSession with an AVCaptureVideoDataOutput (live preview)
//              and an AVCaptureMovieFileOutput (records video + audio to file).
//
// Version: 0.1.2
// Last Modified: 2026-07-23
// V0.1.2: Camera capture adapts to any camera mode. The session preset is chosen from
//   a graceful fallback chain (a tier near the requested height, else High/Medium/Low)
//   instead of hard-pinning 1280x720, and frame conversion defensively refuses any
//   non-BGRA pixel buffer. Resolution/stride are already read from the pixel buffer.
// Author: UltraCanvas Framework

#include "IVideoBackend.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>

#include <atomic>
#include <mutex>

namespace UltraCanvas {

namespace {

// Build a UCVideoFrame (packed BGRA) from a CoreVideo pixel buffer.
UCVideoFramePtr FrameFromPixelBuffer(CVPixelBufferRef pb, double pts) {
    if (!pb) return nullptr;
    // We only fast-path packed 32-bit BGRA (what capture + decode both request). A
    // planar YUV buffer would make the w*4 row copy read garbage / out of bounds, so
    // refuse anything else rather than corrupt memory.
    if (CVPixelBufferGetPixelFormatType(pb) != kCVPixelFormatType_32BGRA) return nullptr;
    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    const int w = (int)CVPixelBufferGetWidth(pb);
    const int h = (int)CVPixelBufferGetHeight(pb);
    const int srcStride = (int)CVPixelBufferGetBytesPerRow(pb);
    const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddress(pb);

    UCVideoFramePtr frame;
    if (base && w > 0 && h > 0) {
        frame = std::make_shared<UCVideoFrame>();
        VideoFrameInfo fi;
        fi.width = w; fi.height = h;
        fi.pixelFormat = VideoPixelFormat::BGRA32;
        fi.stride = w * 4;
        fi.pts = pts;
        auto& out = frame->MutableData();
        out.resize((size_t)fi.stride * h);
        for (int y = 0; y < h; ++y) {
            memcpy(out.data() + (size_t)y * fi.stride,
                   base + (size_t)y * srcStride, (size_t)w * 4);
        }
        frame->SetInfo(fi);
    }
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    return frame;
}

} // namespace
} // namespace UltraCanvas

// ===== ObjC delegate helpers (file scope) =====

@interface UCAVFrameDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, copy) void (^onFrame)(CVPixelBufferRef, double);
@end

@implementation UCAVFrameDelegate
- (void)captureOutput:(AVCaptureOutput *)output
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
    if (!self.onFrame) return;
    CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
    CMTime t = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    double pts = CMTIME_IS_VALID(t) ? CMTimeGetSeconds(t) : 0.0;
    if (pb) self.onFrame(pb, pts);
}
@end

@interface UCAVRecordDelegate : NSObject <AVCaptureFileOutputRecordingDelegate>
@property (nonatomic, copy) void (^onStart)(void);
@property (nonatomic, copy) void (^onFinish)(NSError *);
@end

@implementation UCAVRecordDelegate
- (void)captureOutput:(AVCaptureFileOutput *)output
didStartRecordingToOutputFileAtURL:(NSURL *)fileURL
      fromConnections:(NSArray<AVCaptureConnection *> *)connections {
    if (self.onStart) self.onStart();
}
- (void)captureOutput:(AVCaptureFileOutput *)output
didFinishRecordingToOutputFileAtURL:(NSURL *)outputFileURL
      fromConnections:(NSArray<AVCaptureConnection *> *)connections
                error:(NSError *)error {
    if (self.onFinish) self.onFinish(error);
}
@end

namespace UltraCanvas {

namespace {

// ---- Decode session -------------------------------------------------------

class AVFDecodeSession : public IVideoDecodeSession {
public:
    explicit AVFDecodeSession(const std::string& source) : sourceStr(source) {}
    ~AVFDecodeSession() override { Teardown(); }

    bool Build() {
        NSString* s = [NSString stringWithUTF8String:sourceStr.c_str()];
        NSURL* url = [s containsString:@"://"] ? [NSURL URLWithString:s]
                                               : [NSURL fileURLWithPath:s];
        if (!url) return false;

        item = [AVPlayerItem playerItemWithURL:url];
        if (!item) return false;

        NSDictionary* attrs = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        videoOutput = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];
        [item addOutput:videoOutput];

        player = [AVPlayer playerWithPlayerItem:item];
        player.volume = 1.0f;

        // End-of-stream notification (block-based; token removed on teardown).
        __block AVFDecodeSession* self_ = this;
        endObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                        object:item
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
            if (self_->looping) {
                [self_->player seekToTime:kCMTimeZero];
                [self_->player play];
            } else if (self_->onEnded) {
                self_->onEnded();
            }
        }];

        StartFrameTimer();
        return true;
    }

    bool Play() override  { [player play]; player.rate = rate; return true; }
    bool Pause() override { [player pause]; return true; }
    bool Stop() override  { [player pause]; [player seekToTime:kCMTimeZero]; return true; }
    bool Seek(double seconds) override {
        CMTime t = CMTimeMakeWithSeconds(seconds, 600);
        [player seekToTime:t toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        return true;
    }

    double GetPosition() const override {
        if (!player) return 0.0;
        double s = CMTimeGetSeconds(player.currentTime);
        return std::isfinite(s) ? s : 0.0;
    }
    double GetDuration() const override {
        if (!item) return 0.0;
        double s = CMTimeGetSeconds(item.duration);
        return std::isfinite(s) ? s : 0.0;
    }
    const VideoStreamInfo& GetStreamInfo() const override { return streamInfo; }

    void SetVolume(float v) override { if (player) player.volume = v; }
    void SetMute(bool mute) override { if (player) player.muted = mute ? YES : NO; }
    void SetLoop(bool loop) override { looping = loop; }
    void SetPlaybackRate(float r) override {
        rate = (r > 0.01f) ? r : 1.0f;
        if (player && player.rate != 0.0f) player.rate = rate;
    }

private:
    void StartFrameTimer() {
        timerQueue = dispatch_queue_create("ultracanvas.avf.decode", DISPATCH_QUEUE_SERIAL);
        frameTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, timerQueue);
        uint64_t intervalNs = (uint64_t)(NSEC_PER_SEC / 60);  // poll at 60 Hz
        dispatch_source_set_timer(frameTimer, DISPATCH_TIME_NOW, intervalNs, intervalNs / 10);
        __block AVFDecodeSession* self_ = this;
        dispatch_source_set_event_handler(frameTimer, ^{ self_->PullFrame(); });
        dispatch_resume(frameTimer);
    }

    void PullFrame() {
        if (!videoOutput || !item) return;
        if (!infoReady && item.status == AVPlayerItemStatusReadyToPlay) {
            CGSize sz = item.presentationSize;
            streamInfo.width = (int)sz.width;
            streamInfo.height = (int)sz.height;
            streamInfo.duration = GetDuration();
            infoReady = true;
            if (onLoaded) onLoaded();
        }
        CMTime now = [item currentTime];
        if (![videoOutput hasNewPixelBufferForItemTime:now]) return;
        CMTime display;
        CVPixelBufferRef pb = [videoOutput copyPixelBufferForItemTime:now
                                                   itemTimeForDisplay:&display];
        if (!pb) return;
        double pts = CMTIME_IS_VALID(display) ? CMTimeGetSeconds(display) : GetPosition();
        if (auto f = FrameFromPixelBuffer(pb, pts)) {
            if (onFrame) onFrame(std::move(f));
        }
        CVPixelBufferRelease(pb);
    }

    void Teardown() {
        if (frameTimer) { dispatch_source_cancel(frameTimer); frameTimer = nullptr; }
        if (endObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:endObserver];
            endObserver = nil;
        }
        if (player) [player pause];
        player = nil; item = nil; videoOutput = nil;
    }

    std::string sourceStr;
    AVPlayer* player = nil;
    AVPlayerItem* item = nil;
    AVPlayerItemVideoOutput* videoOutput = nil;
    id endObserver = nil;
    dispatch_queue_t timerQueue = nullptr;
    dispatch_source_t frameTimer = nullptr;
    VideoStreamInfo streamInfo;
    std::atomic<bool> infoReady{false};
    bool looping = false;
    float rate = 1.0f;
};

// ---- Capture session ------------------------------------------------------

class AVFCaptureSession : public IVideoCaptureSession {
public:
    explicit AVFCaptureSession(const VideoCaptureParams& p) : params(p) {}
    ~AVFCaptureSession() override { Close(); }

    bool Open() override {
        if (session) return true;
        session = [[AVCaptureSession alloc] init];
        [session beginConfiguration];
        // Pick the first supported preset from a graceful fallback chain rather than
        // hard-pinning 1280x720: prefer a tier near the requested height, then step
        // down. AVCaptureSessionPresetHigh is always supported, so the session always
        // lands on a valid preset and adapts to whatever the camera provides (the
        // frame path reads the actual dimensions/stride).
        NSMutableArray<AVCaptureSessionPreset>* presets = [NSMutableArray array];
        if (params.height >= 1000) {
            // 1080p preset is macOS 10.15+; guard so it compiles/runs on older targets.
            if (@available(macOS 10.15, *)) [presets addObject:AVCaptureSessionPreset1920x1080];
        }
        [presets addObjectsFromArray:@[ AVCaptureSessionPreset1280x720,
                                        AVCaptureSessionPresetHigh,
                                        AVCaptureSessionPresetMedium,
                                        AVCaptureSessionPresetLow ]];
        for (AVCaptureSessionPreset p in presets) {
            if ([session canSetSessionPreset:p]) { session.sessionPreset = p; break; }
        }

        // ----- Camera input -----
        AVCaptureDevice* cam = nil;
        if (!params.cameraId.empty()) {
            NSString* uid = [NSString stringWithUTF8String:params.cameraId.c_str()];
            cam = [AVCaptureDevice deviceWithUniqueID:uid];
        }
        if (!cam) cam = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        if (!cam) { Fail("No camera available"); return false; }

        NSError* err = nil;
        AVCaptureDeviceInput* camIn = [AVCaptureDeviceInput deviceInputWithDevice:cam error:&err];
        if (!camIn || ![session canAddInput:camIn]) { Fail("Cannot add camera input"); return false; }
        [session addInput:camIn];

        // ----- Mic input (optional) -----
        if (params.captureAudio) {
            AVCaptureDevice* mic = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
            if (mic) {
                AVCaptureDeviceInput* micIn = [AVCaptureDeviceInput deviceInputWithDevice:mic error:&err];
                if (micIn && [session canAddInput:micIn]) [session addInput:micIn];
            }
        }

        // ----- Preview output -----
        frameDelegate = [[UCAVFrameDelegate alloc] init];
        __block AVFCaptureSession* self_ = this;
        frameDelegate.onFrame = ^(CVPixelBufferRef pb, double pts) {
            if (auto f = FrameFromPixelBuffer(pb, pts)) {
                if (self_->onPreviewFrame) self_->onPreviewFrame(std::move(f));
            }
        };
        dataOutput = [[AVCaptureVideoDataOutput alloc] init];
        dataOutput.videoSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        dataOutput.alwaysDiscardsLateVideoFrames = YES;
        previewQueue = dispatch_queue_create("ultracanvas.avf.preview", DISPATCH_QUEUE_SERIAL);
        [dataOutput setSampleBufferDelegate:frameDelegate queue:previewQueue];
        if ([session canAddOutput:dataOutput]) [session addOutput:dataOutput];

        // ----- Movie file output (records audio + video) -----
        movieOutput = [[AVCaptureMovieFileOutput alloc] init];
        if ([session canAddOutput:movieOutput]) [session addOutput:movieOutput];

        recordDelegate = [[UCAVRecordDelegate alloc] init];
        recordDelegate.onStart = ^{ if (self_->onStarted) self_->onStarted(); };
        recordDelegate.onFinish = ^(NSError* e) {
            if (e && self_->onError) self_->onError(e.localizedDescription.UTF8String);
            if (self_->onStopped) self_->onStopped();
        };

        [session commitConfiguration];
        [session startRunning];
        opened = true;
        return true;
    }

    void Close() override {
        recording = false;
        if (session) { [session stopRunning]; }
        session = nil; dataOutput = nil; movieOutput = nil;
        frameDelegate = nil; recordDelegate = nil;
        opened = false;
    }
    bool IsOpen() const override { return opened; }

    bool Start() override {
        if (!opened && !Open()) return false;
        NSString* path = [NSString stringWithUTF8String:params.outputPath.c_str()];
        NSURL* url = [NSURL fileURLWithPath:path];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
        [movieOutput startRecordingToOutputFileURL:url recordingDelegate:recordDelegate];
        recording = true;
        startTime = CFAbsoluteTimeGetCurrent();
        return true;
    }
    bool Pause() override  { return false; }   // AVCaptureMovieFileOutput has no pause
    bool Resume() override { return false; }
    bool Stop() override {
        if (!recording) return false;
        recording = false;
        [movieOutput stopRecording];           // onFinish fires onStopped
        return true;
    }

    double GetElapsed() const override {
        return recording ? (CFAbsoluteTimeGetCurrent() - startTime) : 0.0;
    }
    const VideoCaptureParams& GetParams() const override { return params; }

private:
    void Fail(const std::string& msg) { if (onError) onError(msg); }

    VideoCaptureParams params;
    AVCaptureSession* session = nil;
    AVCaptureVideoDataOutput* dataOutput = nil;
    AVCaptureMovieFileOutput* movieOutput = nil;
    UCAVFrameDelegate* frameDelegate = nil;
    UCAVRecordDelegate* recordDelegate = nil;
    dispatch_queue_t previewQueue = nullptr;
    bool opened = false;
    std::atomic<bool> recording{false};
    double startTime = 0.0;
};

// ---- Backend --------------------------------------------------------------

class AVFBackend : public IVideoBackend {
public:
    bool Initialize() override { return true; }
    void Shutdown() override {}
    std::string GetName() const override { return "avfoundation"; }

    std::vector<VideoDeviceInfo> ListCameras() override {
        std::vector<VideoDeviceInfo> out;
        // Built-in + USB cameras both surface as wide-angle devices through the
        // discovery session across supported macOS versions.
        NSArray<AVCaptureDeviceType>* types = @[ AVCaptureDeviceTypeBuiltInWideAngleCamera ];
        AVCaptureDeviceDiscoverySession* disc =
            [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                                   mediaType:AVMediaTypeVideo
                                                                    position:AVCaptureDevicePositionUnspecified];
        bool first = true;
        for (AVCaptureDevice* d in disc.devices) {
            VideoDeviceInfo info;
            info.id = d.uniqueID.UTF8String;
            info.name = d.localizedName.UTF8String;
            info.isDefault = first; first = false;
            out.push_back(std::move(info));
        }
        return out;
    }
    VideoDeviceInfo GetDefaultCamera() override {
        auto cams = ListCameras();
        return cams.empty() ? VideoDeviceInfo{} : cams.front();
    }

    CameraPermission GetCameraPermission() override {
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
            case AVAuthorizationStatusAuthorized:    return CameraPermission::Granted;
            case AVAuthorizationStatusDenied:        return CameraPermission::Denied;
            case AVAuthorizationStatusRestricted:    return CameraPermission::Restricted;
            case AVAuthorizationStatusNotDetermined:
            default:                                 return CameraPermission::Undetermined;
        }
    }
    void RequestCameraPermission(std::function<void(bool)> cb) override {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
            if (cb) cb(granted ? true : false);
        }];
    }

    std::unique_ptr<IVideoDecodeSession> OpenDecoder(const std::string& source,
                                                     const VideoDecodeOptions& opts) override {
        auto s = std::make_unique<AVFDecodeSession>(source);
        if (!s->Build()) return nullptr;
        // disableAudio degrades to a hard mute here (AVPlayer manages its own
        // audio session; a muted player doesn't hold the output device open).
        if (opts.disableAudio) s->SetMute(true);
        return s;
    }
    std::unique_ptr<IVideoCaptureSession> OpenCapture(const VideoCaptureParams& p) override {
        return std::make_unique<AVFCaptureSession>(p);
    }
};

} // namespace

IVideoBackend* GetNullVideoBackend();   // from VideoBackendNull.cpp

IVideoBackend* GetVideoBackend() {
    static AVFBackend backend;
    static IVideoBackend* selected =
        backend.Initialize() ? static_cast<IVideoBackend*>(&backend)
                             : GetNullVideoBackend();
    return selected;
}

} // namespace UltraCanvas
