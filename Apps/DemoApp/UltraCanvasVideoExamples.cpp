// Apps/DemoApp/UltraCanvasVideoExamples.cpp
// Video player & recorder example screen
// Version: 0.2.0 - the shared page look (UltraCanvasDemoPageKit): playback and
//                  recording as cards side by side, the status line and the
//                  buttons sized to their text
// Version: 0.1.5
// Last Modified: 2026-10-23
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
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasVideoRecorderElement.h"
#include "UltraCanvasVideoDevices.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"

namespace UltraCanvas {

    // A button as wide as its text, at least the width it had.
    static std::shared_ptr<UltraCanvasButton> MakeVideoButton(const std::string& id, const std::string& text,
                                                              float minWidth) {
        auto button = CreateButton(id, 0, 0, 0, 36, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(minWidth);
        button->boxConstraints = limits;
        return button;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVideoExamples() {
        auto page = DemoKit::CreatePage("VideoExamples", "Video Player & Recorder", "");

        auto status = DemoKit::AddText(page, "VideoStatus", "");
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

        auto cardRow = DemoKit::AddRow(page, "VideoCards");

        // ----- Playback -----
        auto playCard = DemoKit::AddCard(cardRow, "VPlayLabel", "Playback");

        auto player = CreateVideoPlayer("DemoVideoPlayer", 0, 0, 460, 300);
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
        DemoKit::AddFixed(playCard, player);

        auto playButtons = DemoKit::AddRow(playCard, "VPlayButtons", 10);
        auto openBtn = MakeVideoButton("OpenVideo", "Open video", 130);
        openBtn->onClick = [playerWeak]() {
            if (auto p = playerWeak.lock()) p->ShowOpenDialog();
        };
        DemoKit::Add(playButtons, openBtn);

        // ----- Recording -----
        auto recCard = DemoKit::AddCard(cardRow, "VRecLabel", "Recording");

        auto recorder = CreateVideoRecorder("DemoVideoRecorder", 0, 0, 460, 300);
        DemoKit::AddFixed(recCard, recorder);
        auto recWeak = std::weak_ptr<UltraCanvasVideoRecorderElement>(recorder);

        // Start/Stop-camera toggle. The camera starts off; "Start camera" opens the
        // live feed and the button becomes "Stop camera", which stops any recording
        // and closes the camera — back to the initial state. The page is destroyed on
        // navigation (DisplayDemoItem/ClearDisplay), which closes the camera, so it is
        // never left running in the background.
        auto recButtons = DemoKit::AddRow(recCard, "VRecButtons", 10);
        auto camBtn = MakeVideoButton("StartCam", "Start camera", 130);
        DemoKit::Add(recButtons, camBtn);
        std::weak_ptr<UltraCanvasButton> camBtnWeak = camBtn;

        recorder->onRecordStarted = [status, camBtnWeak]() {
            status->SetText("Recording...");
            status->SetTextColor(Color(200, 60, 60));
            // Recording always runs the live camera; keep the toggle label in sync
            // when a recording is started from "Record to file" rather than here.
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

        auto recBtn = MakeVideoButton("RecToFile", "Record to file", 150);
        recBtn->onClick = [recWeak]() {
            if (auto r = recWeak.lock()) r->ShowSaveDialog();
        };
        DemoKit::Add(recButtons, recBtn);

        // ----- Camera selection -----
        // Lists the auto-selected default plus every other camera the backend
        // reports, and switches the recorder's input device on change.
        auto camRow = DemoKit::AddRow(recCard, "VCamSelRow", 8);
        camRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::AddText(camRow, "VCamSelLabel", "Camera:");

        // Takes the rest of the line; a long label moves it onto its own.
        auto camDrop = CreateDropdown("CameraSelect", 0, 0, 300, 28);
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
        DemoKit::Add(camRow, camDrop, 1);

        return page;
    }

} // namespace UltraCanvas
