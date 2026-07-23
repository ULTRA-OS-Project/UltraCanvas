// core/UltraCanvasVideoRecorderElement.cpp
// Composite UI control wrapping UltraCanvasVideoRecorder: camera preview + REC controls
// Version: 0.1.4
// Last Modified: 2026-07-23
// V0.1.4: CloseCamera() now clears the last preview frame so the surface shows
//   "Camera off" after closing (instead of a frozen still). ShowSaveDialog() appends
//   the recording format's extension when the chosen file name lacks/mismatches it
//   (out -> out.mp4, out.ddd -> out.ddd.mp4).
// V0.1.3: Forward the engine's onError to the element's onError so hosts can show
//   camera/pipeline failures instead of leaving a stale status message.
// V0.1.2: Add a frozen-still preview mode. OpenCamera(false) activates the camera
//   but holds the first captured frame as a still; SetPreviewLive(true) (or
//   starting a recording) switches to the moving feed. Lets a host show a still
//   camera image on load and only go live on demand.
// Author: UltraCanvas Framework

#include "UltraCanvasVideoRecorderElement.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasUtils.h"        // GetFileExtension
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace UltraCanvas {

namespace {
    constexpr int kPadding = 10;
    constexpr int kGap = 8;
    constexpr int kTimeWidth = 80;
    constexpr int kCamSelWidth = 150;

    inline bool Hit(const Rect2Di& r, const Point2Di& p) {
        return r.width > 0 && r.height > 0 &&
               p.x >= r.x && p.x < r.x + r.width &&
               p.y >= r.y && p.y < r.y + r.height;
    }

    // File extension (no dot) for a recording container, so the saved file's name
    // matches the format actually written.
    std::string ContainerExtension(VideoContainer c) {
        switch (c) {
            case VideoContainer::MKV:  return "mkv";
            case VideoContainer::WebM: return "webm";
            case VideoContainer::MOV:  return "mov";
            case VideoContainer::AVI:  return "avi";
            case VideoContainer::MP4:
            default:                   return "mp4";
        }
    }
}

UltraCanvasVideoRecorderElement::UltraCanvasVideoRecorderElement(
        const std::string& identifier, long x, long y, long w, long h)
    : UltraCanvasUIElement(identifier, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(w), static_cast<float>(h)) {
    recorder = std::make_shared<UltraCanvasVideoRecorder>();
    HookRecorderCallbacks();
    Relayout();
}

UltraCanvasVideoRecorderElement::~UltraCanvasVideoRecorderElement() {
    StopFrameTimer();
}

bool UltraCanvasVideoRecorderElement::OpenCamera(bool live) {
    previewLive = live;
    bool ok = recorder->Open();
    if (ok) StartFrameTimer();
    RequestRedraw();
    return ok;
}

void UltraCanvasVideoRecorderElement::SetPreviewLive(bool live) {
    if (previewLive == live) return;
    previewLive = live;
    // Going live re-arms the frame pump (a frozen still idles the timer once it
    // has its frame); no-op if the timer is already running.
    if (live && recorder->IsOpen()) StartFrameTimer();
    RequestRedraw();
}
void UltraCanvasVideoRecorderElement::CloseCamera() {
    recorder->Close();
    StopFrameTimer();
    // Drop the last preview frame so the surface shows the "Camera off" placeholder
    // instead of a frozen still — a closed camera should look closed.
    haveFrame = false;
    shownFrame.reset();
    RequestRedraw();
}

void UltraCanvasVideoRecorderElement::StartRecording() {
    if (recorder->Start()) {
        previewLive = true;            // a recording always shows the live feed
        StartFrameTimer();
        if (onRecordStarted) onRecordStarted();
    }
    RequestRedraw();
}
void UltraCanvasVideoRecorderElement::PauseRecording()  { recorder->Pause();  RequestRedraw(); }
void UltraCanvasVideoRecorderElement::ResumeRecording() { recorder->Resume(); RequestRedraw(); }
void UltraCanvasVideoRecorderElement::StopRecording() {
    recorder->Stop();
    if (onRecordStopped) onRecordStopped();
    RequestRedraw();
}
void UltraCanvasVideoRecorderElement::ToggleRecording() {
    if (recorder->GetState() == VideoRecordingState::Recording) StopRecording();
    else StartRecording();
}

void UltraCanvasVideoRecorderElement::ShowSaveDialog() {
    FileDialogOptions opts;
    opts.SetTitle("Record Video To...")
        .AddFilter("MP4 video (*.mp4)", "mp4")
        .AddFilter("Matroska (*.mkv)", "mkv")
        .AddFilter("WebM (*.webm)", "webm")
        .AddFilter("All files (*.*)", "*")
        .SetParentWindow(GetWindow());

    auto self = this;
    UltraCanvasFileLoader::SaveFileDialog(opts,
        [self](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) {
                // Ensure the file name ends with the recording format's extension.
                // Append (not replace) when it's missing or different, so "out" ->
                // "out.mp4" and "out.ddd" -> "out.ddd.mp4".
                std::string out = path;
                std::string ext = ContainerExtension(self->GetConfig().container);
                if (GetFileExtension(out) != ext) out += "." + ext;
                self->SetOutputPath(out);
                self->StartRecording();
            }
        });
}

void UltraCanvasVideoRecorderElement::SetConfig(const VideoCaptureConfig& cfg) { recorder->SetConfig(cfg); }
const VideoCaptureConfig& UltraCanvasVideoRecorderElement::GetConfig() const { return recorder->GetConfig(); }
void UltraCanvasVideoRecorderElement::SetOutputPath(const std::string& path) { recorder->SetOutputPath(path); }
void UltraCanvasVideoRecorderElement::SetCamera(const std::string& deviceId) { recorder->SetCamera(deviceId); }

void UltraCanvasVideoRecorderElement::SelectCamera(const std::string& deviceId,
                                                   const std::string& displayLabel) {
    currentCameraLabel = displayLabel.empty() ? "Default camera" : displayLabel;
    recorder->SetCamera(deviceId);
    // SetCamera only takes effect on the next Open(); restart a running preview
    // so the switch is visible immediately (but never interrupt a recording).
    if (recorder->IsOpen() && recorder->GetState() != VideoRecordingState::Recording) {
        CloseCamera();
        OpenCamera(previewLive);   // preserve still / live mode across the switch
    }
    RequestRedraw();
}

void UltraCanvasVideoRecorderElement::SetStyle(const VideoRecorderStyle& s) {
    style = s; Relayout(); RequestRedraw();
}

void UltraCanvasVideoRecorderElement::HookRecorderCallbacks() {
    auto self = this;
    recorder->onRecordingStateChanged = [self](VideoRecordingState st) {
        if (st == VideoRecordingState::Idle || st == VideoRecordingState::Error)
            self->StopFrameTimer();
        self->RequestRedraw();
    };
    recorder->onSaved = [self](const std::string& path) {
        if (self->onSaved) self->onSaved(path);
    };
    recorder->onPermissionChanged = [self](CameraPermission p) {
        if (self->onPermissionChanged) self->onPermissionChanged(p);
    };
    recorder->onError = [self](const std::string& msg) {
        if (self->onError) self->onError(msg);
    };
}

void UltraCanvasVideoRecorderElement::StartFrameTimer() {
    if (frameTimerId != InvalidTimerId) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    int fps = std::clamp(style.targetFps, 1, 120);
    unsigned int ms = static_cast<unsigned int>(1000 / fps);
    frameTimerId = app->StartTimer(ms, true, [this](TimerId) { OnFrameTick(); });
}
void UltraCanvasVideoRecorderElement::StopFrameTimer() {
    if (frameTimerId == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(frameTimerId);
    frameTimerId = InvalidTimerId;
}

void UltraCanvasVideoRecorderElement::OnFrameTick() {
    if (!IsVisible()) return;

    // Frozen still: the first frame has been captured and shown — hold it and idle
    // cheaply. The camera session stays open (still capturing), so a later switch
    // to the live feed is instant.
    if (!previewLive && haveFrame) return;

    bool hadFrame = haveFrame;
    UploadPreviewFrame();

    if (!previewLive) {
        // Still mode, first frame not captured yet: repaint once it lands, then the
        // early-out above idles the timer.
        if (haveFrame && !hadFrame) RequestRedraw();
        return;
    }

    // Live: blink the REC indicator at ~2Hz while recording.
    static int tick = 0;
    if (recorder->GetState() == VideoRecordingState::Recording) {
        int per = std::max(1, style.targetFps / 2);
        if (++tick % per == 0) pulseOn = !pulseOn;
    } else {
        pulseOn = false;
    }
    RequestRedraw();
}

void UltraCanvasVideoRecorderElement::UploadPreviewFrame() {
    UCVideoFramePtr f = recorder->GetPreviewFrame();
    if (!f || !f->IsValid() || f == shownFrame) return;
    shownFrame = f;

    const int w = f->GetWidth(), h = f->GetHeight();
    if (!framePixmap || frameW != w || frameH != h) {
        framePixmap = std::make_shared<UCPixmap>();
        framePixmap->Init(w, h);
        frameW = w; frameH = h;
    }
    if (!framePixmap || !framePixmap->IsValid()) return;

    cairo_surface_t* surf = framePixmap->GetSurface();
    if (!surf) return;
    unsigned char* dst = cairo_image_surface_get_data(surf);
    const int dstStride = cairo_image_surface_get_stride(surf);
    const uint8_t* src = f->GetData();
    const int srcStride = f->GetStride();
    const bool swapRB = (f->GetInfo().pixelFormat == VideoPixelFormat::RGBA32);

    for (int y = 0; y < h; ++y) {
        const uint8_t* sr = src + static_cast<size_t>(y) * srcStride;
        uint8_t* dr = dst + static_cast<size_t>(y) * dstStride;
        if (!swapRB) {
            std::memcpy(dr, sr, static_cast<size_t>(w) * 4);
        } else {
            for (int x = 0; x < w; ++x) {
                dr[x*4+0] = sr[x*4+2]; dr[x*4+1] = sr[x*4+1];
                dr[x*4+2] = sr[x*4+0]; dr[x*4+3] = sr[x*4+3];
            }
        }
    }
    framePixmap->MarkDirty();
    haveFrame = true;
}

Rect2Di UltraCanvasVideoRecorderElement::FitContain(const Rect2Di& dst, int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) return dst;
    double s = std::min(static_cast<double>(dst.width) / srcW,
                        static_cast<double>(dst.height) / srcH);
    int w = static_cast<int>(srcW * s), h = static_cast<int>(srcH * s);
    return Rect2Di(dst.x + (dst.width - w) / 2, dst.y + (dst.height - h) / 2, w, h);
}

void UltraCanvasVideoRecorderElement::Relayout() {
    Rect2Df b = GetLocalBounds();
    videoRect = Rect2Di((int)b.x, (int)b.y, (int)b.width, (int)b.height);

    int barH = style.controlBarHeight;
    controlBarRect = Rect2Di(videoRect.x, videoRect.y + videoRect.height - barH,
                             videoRect.width, barH);
    int cy = controlBarRect.y + barH / 2;
    int btn = style.buttonSize;

    recordButtonRect = Rect2Di(controlBarRect.x + kPadding, cy - btn / 2, btn, btn);

    int rightX = controlBarRect.x + controlBarRect.width - kPadding;
    if (style.showCameraSelect) {
        cameraSelectRect = Rect2Di(rightX - kCamSelWidth, cy - 12, kCamSelWidth, 24);
        rightX = cameraSelectRect.x - kGap;
    } else {
        cameraSelectRect = Rect2Di();
    }
    if (style.showElapsedTime) {
        timeLabelRect = Rect2Di(recordButtonRect.x + btn + kGap, cy - 8, kTimeWidth, 16);
    } else {
        timeLabelRect = Rect2Di();
    }
}

std::string UltraCanvasVideoRecorderElement::FormatTime(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    int total = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    return std::string(buf);
}

void UltraCanvasVideoRecorderElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    Relayout();
    DrawVideoSurface(ctx);
    DrawControlBar(ctx);
}

void UltraCanvasVideoRecorderElement::DrawVideoSurface(IRenderContext* ctx) {
    ctx->DrawFilledRectangle(Rect2Dd(videoRect.x, videoRect.y, videoRect.width, videoRect.height),
                             style.backgroundColor, 0.0f, Color(0, 0, 0, 0), 0);
    if (haveFrame && framePixmap && framePixmap->IsValid()) {
        Rect2Di fit = FitContain(videoRect, frameW, frameH);
        ctx->DrawPixmap(*framePixmap, Rect2Dd(fit.x, fit.y, fit.width, fit.height), ImageFitMode::Fill);
    } else {
        std::string msg = recorder->IsOpen() ? "Starting camera..." : "Camera off";
        ctx->SetFontSize(14);
        ctx->SetTextPaint(Color(160, 160, 160));
        ctx->DrawText(msg, Point2Dd(videoRect.x + videoRect.width / 2 - 50,
                                    videoRect.y + videoRect.height / 2));
    }

    // REC indicator (top-left) while recording
    if (recorder->GetState() == VideoRecordingState::Recording && pulseOn) {
        ctx->SetFillPaint(style.recordingPulseColor);
        ctx->FillCircle(Point2Dd(videoRect.x + 18, videoRect.y + 18), 7);
        ctx->SetFontSize(12);
        ctx->SetTextPaint(Colors::White);
        ctx->DrawText("REC", Point2Dd(videoRect.x + 30, videoRect.y + 22));
    }
}

void UltraCanvasVideoRecorderElement::DrawControlBar(IRenderContext* ctx) {
    ctx->DrawFilledRectangle(Rect2Dd(controlBarRect.x, controlBarRect.y,
                                     controlBarRect.width, controlBarRect.height),
                             style.controlBarColor, 0.0f, Color(0, 0, 0, 0), 0);

    bool recording = recorder->GetState() == VideoRecordingState::Recording;
    int cx = recordButtonRect.x + recordButtonRect.width / 2;
    int cy = recordButtonRect.y + recordButtonRect.height / 2;
    int r = recordButtonRect.width / 3;
    ctx->SetFillPaint(style.recordButtonColor);
    if (recording) {
        // Stop square
        ctx->FillRectangle(Rect2Dd(cx - r, cy - r, r * 2, r * 2));
    } else {
        // Record circle
        ctx->FillCircle(Point2Dd(cx, cy), r);
    }

    if (timeLabelRect.width > 0) {
        ctx->SetFontSize(12);
        ctx->SetTextPaint(style.textColor);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(FormatTime(recorder->GetElapsed()),
                            Rect2Dd(timeLabelRect.x, timeLabelRect.y,
                                    timeLabelRect.width, timeLabelRect.height));
        ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    }

    if (cameraSelectRect.width > 0) {
        ctx->DrawFilledRectangle(Rect2Dd(cameraSelectRect.x, cameraSelectRect.y,
                                         cameraSelectRect.width, cameraSelectRect.height),
                                 Color(60, 60, 60), 1.0f, Color(120, 120, 120), 3);
        ctx->SetFontSize(11);
        ctx->SetTextPaint(style.textColor);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(currentCameraLabel,
                            Rect2Dd(cameraSelectRect.x + 8, cameraSelectRect.y,
                                    cameraSelectRect.width - 8, cameraSelectRect.height));
        ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    }
}

void UltraCanvasVideoRecorderElement::OpenCameraPicker() {
    auto* window = GetWindow();
    if (!window) return;
    auto cameras = UltraCanvasVideoDevices::ListCameras();

    auto menu = std::make_shared<UltraCanvasMenu>(
        GetIdentifier() + ".CameraPicker", 0, 0, 220, 0);
    auto self = this;
    menu->AddItem(MenuItemData("System default", [self]() {
        self->SelectCamera("", "Default camera");
    }));
    if (cameras.empty()) {
        menu->AddItem(MenuItemData("(no cameras found)", []() {}));
    }
    for (const auto& cam : cameras) {
        std::string id = cam.id, display = cam.name;
        menu->AddItem(MenuItemData(cam.name + (cam.isDefault ? "  (default)" : ""),
            [self, id, display]() {
                self->SelectCamera(id, display);
            }));
    }
    Point2Di anchor(GetXInWindow() + cameraSelectRect.x,
                    GetYInWindow() + cameraSelectRect.y + cameraSelectRect.height);
    PopupElementSettings settings;
    menu->OpenMenu(anchor, *window, settings);
    cameraPopupMenu = menu;
}

bool UltraCanvasVideoRecorderElement::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    Point2Di p = event.pointer;

    switch (event.type) {
        case UCEventType::MouseDown: {
            if (event.button != UCMouseButton::Left) return false;
            if (Hit(recordButtonRect, p)) { ToggleRecording(); return true; }
            if (cameraSelectRect.width > 0 && Hit(cameraSelectRect, p)) {
                OpenCameraPicker(); return true;
            }
            return false;
        }
        default:
            return false;
    }
}

} // namespace UltraCanvas
