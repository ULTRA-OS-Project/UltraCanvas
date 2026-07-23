// Apps/DemoApp/UltraCanvasVideoExamples.cpp
// Video player & recorder example screen
// Version: 0.1.5
// Last Modified: 2026-07-23
// V0.1.5: "Start camera" now fully closes any prior (stopped) session and clears the
//   stale recording path before reopening, so it reliably returns to a clean live
//   preview after a recording was stopped (previously it rebuilt the record pipeline
//   and came up blank).
// V0.1.4: "Start camera" is now a real Start/Stop toggle: the camera starts off,
//   the button opens the live feed and becomes "Stop camera", and Stop stops any
//   recording then closes the camera (reset to initial state). The page-load still
//   preview was dropped so the initial/after-stop states match.
// V0.1.3: Hook the recorder's onError to the status label so camera/pipeline
//   failures are shown (red) instead of a stale "Live camera preview." message.
// V0.1.2: The camera is activated on page open and shows a frozen still frame;
//   "Start camera" switches it to the live feed. It also reports why the camera
//   failed to open instead of silently showing "Camera off".
// V0.1.1: Opening a video now auto-plays it (the decode backends only deliver
//   frames while playing, so the surface previously sat on "Buffering..."
//   forever).
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
        container->AddChild(recorder);
        auto recWeak = std::weak_ptr<UltraCanvasVideoRecorderElement>(recorder);

        // Start/Stop-camera toggle. The camera starts off; "Start camera" opens the
        // live feed and the button becomes "Stop camera", which stops any recording
        // and closes the camera — back to the initial state. The page is destroyed on
        // navigation (DisplayDemoItem/ClearDisplay), which closes the camera, so it is
        // never left running in the background.
        auto camBtn = CreateButton("StartCam", 520, 430, 130, 36, "Start camera");
        container->AddChild(camBtn);
        std::weak_ptr<UltraCanvasButton> camBtnWeak = camBtn;

        recorder->onRecordStarted = [status, camBtnWeak]() {
            status->SetText("Recording...");
            status->SetTextColor(Color(200, 60, 60));
            // Recording always runs the live camera; keep the toggle label in sync
            // when a recording is started from "Record to file..." rather than here.
            if (auto b = camBtnWeak.lock()) b->SetText("Stop camera");
        };
        recorder->onRecordStopped = [status, recWeak, camBtnWeak]() {
            status->SetText("Recording stopped.");
            // A stop can come from the in-surface record button too; keep the toggle
            // label truthful about whether the camera is still open afterwards.
            if (auto r = recWeak.lock()) {
                auto rec = r->GetRecorder();
                if (auto b = camBtnWeak.lock())
                    b->SetText(rec && rec->IsOpen() ? "Stop camera" : "Start camera");
            }
        };
        recorder->onSaved = [status](const std::string& path) {
            status->SetText("Saved to " + path);
            status->SetTextColor(Color(40, 120, 40));
        };
        // Surface backend failures (camera can't open, caps negotiation fails, etc.)
        // so the status line reflects reality instead of a stale "preview" message.
        recorder->onError = [status](const std::string& msg) {
            status->SetText("Camera error: " + msg);
            status->SetTextColor(Color(180, 60, 60));
        };

        camBtn->onClick = [recWeak, camBtnWeak, status]() {
            auto r = recWeak.lock();
            if (!r) return;
            auto b = camBtnWeak.lock();
            auto rec = r->GetRecorder();
            const bool live = rec && rec->IsOpen() && r->IsPreviewLive();
            if (live) {
                // Stop camera: finalize any recording, then close (reset to initial).
                if (rec->GetState() == VideoRecordingState::Recording ||
                    rec->GetState() == VideoRecordingState::Paused)
                    r->StopRecording();
                r->CloseCamera();
                if (b) b->SetText("Start camera");
                status->SetText("Camera off.");
                status->SetTextColor(Color(120, 120, 120));
            } else {
                // Start camera: fully close any prior session first — after a
                // recording the session is torn down but the recorder still holds the
                // output path, so a plain reopen would rebuild the heavy record
                // pipeline (encoder/muxer/mic + filesink on the last file) and fail to
                // reinitialise, leaving the surface blank. Drop the stale path so we
                // reopen a clean, lightweight live PREVIEW, just like the first open.
                r->CloseCamera();
                r->SetOutputPath("");
                if (!r->OpenCamera(/*live=*/true)) {
                    std::string why = rec ? rec->GetLastError() : "";
                    status->SetText(why.empty()
                                        ? "Could not start the camera (no device or access denied)."
                                        : "Could not start the camera: " + why);
                    status->SetTextColor(Color(180, 60, 60));
                    return;
                }
                if (b) b->SetText("Stop camera");
                status->SetText("Live camera preview.");
                status->SetTextColor(Color(40, 120, 40));
            }
        };

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
