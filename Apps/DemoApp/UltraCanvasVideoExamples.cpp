// Apps/DemoApp/UltraCanvasVideoExamples.cpp
// Video player & recorder example screen
// Version: 0.1.1
// Last Modified: 2026-07-21
// V0.1.1: Opening a video now auto-plays it (the decode backends only deliver
//   frames while playing, so the surface previously sat on "Buffering..."
//   forever). "Start camera" now reports why the camera failed to open instead
//   of silently showing "Camera off".
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasVideoRecorderElement.h"
#include "UltraCanvasVideoDevices.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVideoExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VideoExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("VideoTitle", 10, 10, 600, 30);
        title->SetText("Video Player & Recorder");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto status = std::make_shared<UltraCanvasLabel>("VideoStatus", 10, 45, 980, 25);
        if (UltraCanvasVideoDevices::IsAvailable()) {
            status->SetText("Backend: " + UltraCanvasVideoDevices::GetBackendName() + " — ready");
            status->SetTextColor(Color(40, 120, 40));
        } else {
#if defined(_WIN32)
            const char* framework = "Media Foundation (ships with Windows)";
#elif defined(__APPLE__)
            const char* framework = "AVFoundation (ships with macOS)";
#else
            const char* framework = "GStreamer (gstreamer-1.0 + app/video/pbutils plugins)";
#endif
            status->SetText(std::string("Video backend not available. Install/enable ") +
                            framework + " and rebuild with -DULTRACANVAS_ENABLE_VIDEO=ON.");
            status->SetTextColor(Color(180, 60, 60));
        }
        status->SetFontSize(11);
        container->AddChild(status);

        // ----- Playback -----
        auto playLabel = std::make_shared<UltraCanvasLabel>("VPlayLabel", 10, 90, 400, 22);
        playLabel->SetText("Playback");
        playLabel->SetFontWeight(FontWeight::Bold);
        playLabel->SetTextColor(Color(0, 100, 200));
        container->AddChild(playLabel);

        auto player = CreateVideoPlayer("DemoVideoPlayer", 10, 120, 460, 300);
        auto playerWeak = std::weak_ptr<UltraCanvasVideoPlayerElement>(player);
        // Start playback as soon as a file is opened, the same way the Album
        // viewer does (UltraCanvasAlbumExamples.cpp): the platform decode
        // backends only deliver video frames while the pipeline is *playing*, so
        // a freshly loaded-but-paused clip shows nothing but the "Buffering..."
        // placeholder until Play() is called.
        player->onFileOpened = [status, playerWeak](const std::string& path) {
            status->SetText("Playing: " + path);
            status->SetTextColor(Color(40, 120, 40));
            if (auto p = playerWeak.lock()) p->Play();
        };
        container->AddChild(player);

        auto openBtn = CreateButton("OpenVideo", 10, 430, 130, 36, "Open video...");
        openBtn->onClick = [playerWeak]() {
            if (auto p = playerWeak.lock()) p->ShowOpenDialog();
        };
        container->AddChild(openBtn);

        // ----- Recording -----
        auto recLabel = std::make_shared<UltraCanvasLabel>("VRecLabel", 520, 90, 400, 22);
        recLabel->SetText("Recording");
        recLabel->SetFontWeight(FontWeight::Bold);
        recLabel->SetTextColor(Color(200, 60, 60));
        container->AddChild(recLabel);

        auto recorder = CreateVideoRecorder("DemoVideoRecorder", 520, 120, 460, 300);
        recorder->onRecordStarted = [status]() { status->SetText("Recording..."); };
        recorder->onRecordStopped = [status]() { status->SetText("Recording stopped."); };
        recorder->onSaved = [status](const std::string& path) {
            status->SetText("Saved to " + path);
        };
        container->AddChild(recorder);

        auto camBtn = CreateButton("StartCam", 520, 430, 130, 36, "Start camera");
        auto recWeak = std::weak_ptr<UltraCanvasVideoRecorderElement>(recorder);
        camBtn->onClick = [recWeak, status]() {
            auto r = recWeak.lock();
            if (!r) return;
            // OpenCamera() returns false when the device can't be activated
            // (no camera, or the OS denied access). Surface the backend's reason
            // instead of leaving a silent "Camera off" so the demo is diagnosable.
            if (r->OpenCamera()) {
                status->SetText("Camera preview started.");
                status->SetTextColor(Color(40, 120, 40));
            } else {
                std::string why = r->GetRecorder() ? r->GetRecorder()->GetLastError() : "";
                status->SetText(why.empty()
                                    ? "Could not start the camera (no device or access denied)."
                                    : "Could not start the camera: " + why);
                status->SetTextColor(Color(180, 60, 60));
            }
        };
        container->AddChild(camBtn);

        auto recBtn = CreateButton("RecToFile", 660, 430, 150, 36, "Record to file...");
        recBtn->onClick = [recWeak]() {
            if (auto r = recWeak.lock()) r->ShowSaveDialog();
        };
        container->AddChild(recBtn);

        // ----- Camera selection -----
        // Lists the auto-selected default plus every other camera the backend
        // reports, and switches the recorder's input device on change.
        auto camSelLabel = std::make_shared<UltraCanvasLabel>("VCamSelLabel", 520, 478, 62, 22);
        camSelLabel->SetText("Camera:");
        container->AddChild(camSelLabel);

        auto camDrop = CreateDropdown("CameraSelect", 586, 474, 394, 28);
        camDrop->AddItem("Default camera (auto-selected)", "");
        for (const auto& cam : UltraCanvasVideoDevices::ListCameras()) {
            camDrop->AddItem(cam.name + (cam.isDefault ? "  (auto-selected)" : ""), cam.id);
        }
        camDrop->SetSelectedIndex(0, false);
        camDrop->onSelectionChanged = [recWeak](int, const DropdownItem& item) {
            if (auto r = recWeak.lock()) {
                std::string label = item.value.empty() ? "Default camera" : item.text;
                r->SelectCamera(item.value, label);
            }
        };
        container->AddChild(camDrop);

        return container;
    }

} // namespace UltraCanvas
