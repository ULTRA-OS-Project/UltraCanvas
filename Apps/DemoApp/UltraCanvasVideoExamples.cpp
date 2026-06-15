// Apps/DemoApp/UltraCanvasVideoExamples.cpp
// Video player & recorder example screen
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasVideoRecorderElement.h"
#include "UltraCanvasVideoDevices.h"
#include "UltraCanvasButton.h"

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
            status->SetText("Video backend not compiled in. Install the platform media "
                            "framework (GStreamer on Linux) and rebuild with "
                            "-DULTRACANVAS_ENABLE_VIDEO=ON.");
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
        player->onFileOpened = [status](const std::string& path) {
            status->SetText("Loaded: " + path);
        };
        container->AddChild(player);

        auto openBtn = CreateButton("OpenVideo", 10, 430, 130, 36, "Open video...");
        auto playerWeak = std::weak_ptr<UltraCanvasVideoPlayerElement>(player);
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
        camBtn->onClick = [recWeak]() {
            if (auto r = recWeak.lock()) r->OpenCamera();
        };
        container->AddChild(camBtn);

        auto recBtn = CreateButton("RecToFile", 660, 430, 150, 36, "Record to file...");
        recBtn->onClick = [recWeak]() {
            if (auto r = recWeak.lock()) r->ShowSaveDialog();
        };
        container->AddChild(recBtn);

        return container;
    }

} // namespace UltraCanvas
