// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.1
// Last Modified: 2026-05-01
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasAudioRecorderElement.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <random>
#include <map>
namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAudioExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("AudioExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("AudioTitle", 10, 10, 600, 30);
        title->SetText("Audio Player & Recorder");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto status = std::make_shared<UltraCanvasLabel>("AudioStatus", 10, 45, 980, 25);
        if (UltraCanvasAudioDevices::IsAvailable()) {
            status->SetText("Backend: " + UltraCanvasAudioDevices::GetBackendName() +
                            " — ready");
            status->SetTextColor(Color(40, 120, 40));
        } else {
            status->SetText("Audio backend not compiled in. "
                            "Rebuild with -DULTRACANVAS_ENABLE_AUDIO=ON.");
            status->SetTextColor(Color(180, 60, 60));
        }
        status->SetFontSize(11);
        container->AddChild(status);

        // ----- Playback section -----
        auto playbackLabel = std::make_shared<UltraCanvasLabel>("PlaybackLabel", 10, 90, 400, 22);
        playbackLabel->SetText("Playback");
        playbackLabel->SetFontWeight(FontWeight::Bold);
        playbackLabel->SetTextColor(Color(0, 100, 200));
        container->AddChild(playbackLabel);

        auto player = CreateAudioPlayer("DemoPlayer", 10, 120, 600, 56);
        container->AddChild(player);

        // Shows the name of the file currently loaded into the player.
        auto fileLabel = std::make_shared<UltraCanvasLabel>("PlaybackFile", 10, 180, 720, 18);
        fileLabel->SetText("No file loaded.");
        fileLabel->SetFontSize(11);
        fileLabel->SetTextColor(Color(90, 90, 90));
        container->AddChild(fileLabel);

        // Strip any directory part so we display just the file name.
        auto baseName = [](const std::string& p) -> std::string {
            size_t slash = p.find_last_of("/\\");
            return slash == std::string::npos ? p : p.substr(slash + 1);
        };

        auto loadBtn = CreateButton("LoadAudio", 620, 130, 110, 36, "Open...");
        auto playerWeak = std::weak_ptr<UltraCanvasAudioPlayerElement>(player);
        player->onFileOpened = [status, fileLabel, playerWeak, baseName](const std::string& path) {
            std::string name = baseName(path);
            fileLabel->SetText("File: " + name);
            if (auto p = playerWeak.lock()) p->SetTrackTitle(name);
            status->SetText("Loaded: " + name);
        };
        player->onOpenCancelled = [status]() {
            status->SetText("Open cancelled.");
        };
        loadBtn->onClick = [playerWeak]() {
            if (auto p = playerWeak.lock()) p->ShowOpenDialog();
        };
        container->AddChild(loadBtn);

        // ----- Recording section -----
        auto recLabel = std::make_shared<UltraCanvasLabel>("RecLabel", 10, 200, 400, 22);
        recLabel->SetText("Recording");
        recLabel->SetFontWeight(FontWeight::Bold);
        recLabel->SetTextColor(Color(200, 60, 60));
        container->AddChild(recLabel);

        auto recorder = CreateAudioRecorder("DemoRecorder", 10, 230, 600, 72);
        recorder->onRecordStarted = [status]() { status->SetText("Recording..."); };
        recorder->onRecordStopped = [status]() { status->SetText("Recording stopped."); };
        container->AddChild(recorder);

        auto saveBtn = CreateButton("SaveRec", 620, 244, 110, 36, "Save WAV...");
        recorder->onSaved = [status](const std::string& path) {
            status->SetText("Saved to " + path);
        };
        recorder->onSaveCancelled = [status]() {
            status->SetText("Save cancelled or no recording available.");
        };
        auto recWeak = std::weak_ptr<UltraCanvasAudioRecorderElement>(recorder);
        saveBtn->onClick = [recWeak]() {
            if (auto r = recWeak.lock()) r->ShowSaveDialog();
        };
        container->AddChild(saveBtn);

        auto playRecBtn = CreateButton("PlayRec", 740, 244, 110, 36, "Play back");
        playRecBtn->onClick = [player, recorder, status, fileLabel]() {
            auto audio = recorder->TakeBuffer();
            if (audio && audio->IsValid()) {
                player->LoadFromAudio(audio);
                player->SetTrackTitle("(just recorded)");
                fileLabel->SetText("File: (just recorded)");
                player->Play();
                status->SetText("Playing back recording.");
            } else {
                status->SetText("No recording captured yet.");
            }
        };
        container->AddChild(playRecBtn);

        return container;
    }

} // namespace UltraCanvas