// include/UltraCanvasVideoRecorderElement.h
// Composite UI control wrapping UltraCanvasVideoRecorder: camera preview + REC controls
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasVideoRecorder.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== VISUAL STYLE =====
struct VideoRecorderStyle {
    Color backgroundColor = Color(16, 16, 16);
    Color controlBarColor = Color(0, 0, 0, 160);
    Color recordButtonColor = Color(220, 30, 30);
    Color recordingPulseColor = Color(255, 90, 90);
    Color textColor = Color(235, 235, 235);
    Color iconColor = Color(235, 235, 235);
    Color disabledColor = Color(120, 120, 120);

    int   controlBarHeight = 44;
    int   buttonSize = 34;
    bool  showElapsedTime = true;
    bool  showCameraSelect = true;
    int   targetFps = 30;
};

// ===== VIDEO RECORDER UI ELEMENT =====
class UltraCanvasVideoRecorderElement : public UltraCanvasUIElement {
public:
    UltraCanvasVideoRecorderElement(const std::string& identifier = "VideoRecorder",
                                    long x = 0, long y = 0, long w = 640, long h = 420);
    ~UltraCanvasVideoRecorderElement() override;

    // ===== SESSION =====
    bool OpenCamera();                       // start the live preview
    void CloseCamera();

    // ===== TRANSPORT =====
    void StartRecording();
    void PauseRecording();
    void ResumeRecording();
    void StopRecording();
    void ToggleRecording();

    // Opens the native save dialog and records to the chosen path.
    void ShowSaveDialog();

    // ===== CONFIG =====
    void SetConfig(const VideoCaptureConfig& cfg);
    const VideoCaptureConfig& GetConfig() const;
    void SetOutputPath(const std::string& path);
    void SetCamera(const std::string& deviceId);

    // Select a camera by backend id (empty = system default) and update the
    // on-screen label. If the preview is already live (and not mid-recording),
    // it is restarted on the newly chosen device.
    void SelectCamera(const std::string& deviceId, const std::string& displayLabel);
    const std::string& GetCameraLabel() const { return currentCameraLabel; }

    // ===== STYLE =====
    void SetStyle(const VideoRecorderStyle& s);
    const VideoRecorderStyle& GetStyle() const { return style; }

    // ===== ACCESS =====
    std::shared_ptr<UltraCanvasVideoRecorder> GetRecorder() const { return recorder; }

    // ===== EVENTS =====
    std::function<void()> onRecordStarted;
    std::function<void()> onRecordStopped;
    std::function<void(const std::string& filePath)> onSaved;
    std::function<void(CameraPermission)> onPermissionChanged;

    // ===== UIElement OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    std::shared_ptr<UltraCanvasVideoRecorder> recorder;
    VideoRecorderStyle style;

    std::shared_ptr<UCPixmap> framePixmap;
    UCVideoFramePtr shownFrame;
    bool haveFrame = false;
    int  frameW = 0, frameH = 0;

    TimerId frameTimerId = InvalidTimerId;
    bool pulseOn = false;

    Rect2Di videoRect;
    Rect2Di controlBarRect;
    Rect2Di recordButtonRect;
    Rect2Di timeLabelRect;
    Rect2Di cameraSelectRect;

    std::string currentCameraLabel = "Default camera";
    std::shared_ptr<class UltraCanvasMenu> cameraPopupMenu;

    void HookRecorderCallbacks();
    void Relayout();
    void StartFrameTimer();
    void StopFrameTimer();
    void OnFrameTick();
    void UploadPreviewFrame();
    void DrawVideoSurface(IRenderContext* ctx);
    void DrawControlBar(IRenderContext* ctx);
    void OpenCameraPicker();
    static std::string FormatTime(double seconds);
    static Rect2Di FitContain(const Rect2Di& dst, int srcW, int srcH);
};

// ===== FACTORIES =====
inline std::shared_ptr<UltraCanvasVideoRecorderElement> CreateVideoRecorder(
        const std::string& identifier, long x, long y, long w, long h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasVideoRecorderElement>(identifier, x, y, w, h);
}

} // namespace UltraCanvas
