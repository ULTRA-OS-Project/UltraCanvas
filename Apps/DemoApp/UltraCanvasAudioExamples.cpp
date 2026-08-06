// Apps/DemoApp/UltraCanvasAudioExamples.cpp
// Audio Player & Recorder demo page: playback, recording, save-with-format
// selection (driven by the runtime supported-format inventory) and a live
// horizontal VU strip fed by the recorder input or the playback output.
// Version: 1.1.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasAudioRecorderElement.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "Plugins/Charts/UltraCanvasSTFT.h"   // AudioToMonoFloat
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {

    // Shared page state: the audio most recently loaded or recorded (save
    // source), its mono downmix for the playback-driven VU strip, and the
    // smoothed levels used for decay when nothing is audible.
    struct AudioDemoState {
        std::shared_ptr<UCAudio> currentAudio;   // last loaded file or recording
        std::string currentName;                 // display/base name of currentAudio
        std::vector<float> mono;                 // currentAudio downmixed to [-1,1]
        double rate = 0.0;                       // currentAudio sample rate
        float lastPeak = 0.0f;
        float lastRms = 0.0f;
    };

    // Map a file extension (as reported by the supported-format inventory)
    // to the AudioFormat enum used by the save/encode APIs.
    AudioFormat AudioFormatFromExtension(const std::string& ext) {
        std::string e;
        e.reserve(ext.size());
        for (char c : ext) e.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (e == "wav")                return AudioFormat::WAV;
        if (e == "mp3")                return AudioFormat::MP3;
        if (e == "ogg" || e == "oga")  return AudioFormat::OGG;
        if (e == "flac")               return AudioFormat::FLAC;
        if (e == "aac" || e == "m4a")  return AudioFormat::AAC;
        if (e == "opus")               return AudioFormat::Opus;
        return AudioFormat::WAV;
    }

    std::string StripExtension(const std::string& name) {
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos ? name : name.substr(0, dot);
    }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAudioExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("AudioExamples", 0, 0, 1000, 600);

        auto st = std::make_shared<AudioDemoState>();

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
        // Open through the shared FileLoader facade: it shows the native dialog
        // (with default audio filters) and hands back a decoded UCAudio buffer,
        // which we then feed straight into the player element.
        loadBtn->onClick = [playerWeak, fileLabel, status, baseName, st]() {
            auto p = playerWeak.lock();
            if (!p) return;

            FileDialogOptions opts;
            opts.SetTitle("Open Audio File")
                .SetParentWindow(p->GetWindow());

            UltraCanvasFileLoader::OpenAudio(opts,
                [playerWeak, fileLabel, status, baseName, st](
                        const FileLoadResult& res, std::shared_ptr<UCAudio> audio) {
                    if (res.dialogResult != DialogResult::OK) {
                        status->SetText("Open cancelled.");
                        return;
                    }
                    if (!res.IsSuccess() || !audio) {
                        status->SetText("Load failed: " + res.loadError);
                        return;
                    }
                    auto pl = playerWeak.lock();
                    if (!pl) return;
                    pl->LoadFromAudio(audio);
                    std::string name = baseName(audio->GetSourcePath());
                    fileLabel->SetText("File: " + name);
                    pl->SetTrackTitle(name);
                    status->SetText("Loaded: " + name);
                    // Remember the loaded audio: it becomes the save source and
                    // feeds the VU strip while it plays.
                    st->currentAudio = audio;
                    st->currentName = name;
                    st->mono = AudioToMonoFloat(*audio);
                    st->rate = audio->GetSampleRate();
                });
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
        auto recWeak = std::weak_ptr<UltraCanvasAudioRecorderElement>(recorder);

        // ----- Save with format selection -----
        // The dropdown lists every audio format THIS build can encode, straight
        // from the runtime supported-format inventory — new encoder plugins show
        // up here automatically.
        auto saveFmtLabel = std::make_shared<UltraCanvasLabel>("SaveFmtLabel", 620, 224, 200, 16);
        saveFmtLabel->SetText("Save format:");
        saveFmtLabel->SetFontSize(11);
        saveFmtLabel->SetTextColor(Color(90, 90, 90));
        container->AddChild(saveFmtLabel);

        auto formatDd = std::make_shared<UltraCanvasDropdown>("SaveFormat", 620, 244, 150, 26);
        {
            bool any = false;
            auto formats = UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory::Audio);
            for (const auto& f : formats) {
                if (!f.canSave) continue;
                std::string upper = f.extension;
                std::transform(upper.begin(), upper.end(), upper.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                formatDd->AddItem(upper + " (*." + f.extension + ")", f.extension);
                any = true;
            }
            // Backend-less builds report no encodable formats; keep the control
            // populated so the page renders (saving then fails with a message).
            if (!any) formatDd->AddItem("WAV (*.wav)", "wav");
            formatDd->SetSelectedIndex(0, false);
        }
        container->AddChild(formatDd);

        recorder->onSaved = [status](const std::string& path) {
            status->SetText("Saved to " + path);
        };
        recorder->onSaveCancelled = [status]() {
            status->SetText("Save cancelled or no recording available.");
        };

        auto saveBtn = CreateButton("SaveAudio", 780, 239, 95, 36, "Save...");
        auto formatDdWeak = std::weak_ptr<UltraCanvasDropdown>(formatDd);
        // Saves whatever audio the page currently holds — the captured recording
        // when one is available, otherwise the loaded file — encoding it in the
        // format picked in the dropdown.
        saveBtn->onClick = [recWeak, formatDdWeak, status, st]() {
            auto rec = recWeak.lock();
            auto dd = formatDdWeak.lock();
            if (!dd) return;

            bool haveRecording = false;
            if (rec) {
                auto core = rec->GetRecorder();
                haveRecording = core &&
                                core->GetState() == AudioRecordingState::Stopped &&
                                core->GetSampleCount() > 0;
            }
            bool haveLoaded = st->currentAudio && st->currentAudio->IsValid();
            if (!haveRecording && !haveLoaded) {
                status->SetText("Nothing to save — load a file or record something first.");
                return;
            }

            const DropdownItem* item = dd->GetSelectedItem();
            if (!item) {
                status->SetText("No save format available in this build.");
                return;
            }
            const std::string ext = item->value;
            const AudioFormat fmt = AudioFormatFromExtension(ext);

            std::string base = haveRecording
                    ? "recording"
                    : (st->currentName.empty() ? "audio" : StripExtension(st->currentName));

            FileDialogOptions opts;
            opts.SetTitle("Save Audio")
                .SetDefaultFileName(base + "." + ext)
                .AddFilter(item->text, ext)
                .AddFilter("All files (*.*)", "*");
            if (rec) opts.SetParentWindow(rec->GetWindow());

            UltraCanvasFileLoader::SaveFileDialog(opts,
                [recWeak, status, st, fmt, ext, haveRecording](
                        DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Save cancelled.");
                        return;
                    }
                    bool ok = false;
                    if (haveRecording) {
                        if (auto r = recWeak.lock()) ok = r->SaveToFile(path, fmt);
                    } else {
                        ok = st->currentAudio->SaveToFile(path, fmt);
                    }
                    if (ok) status->SetText("Saved to " + path);
                    else    status->SetText("Save failed — no ." + ext +
                                            " encoder available in this build.");
                });
        };
        container->AddChild(saveBtn);

        auto playRecBtn = CreateButton("PlayRec", 885, 239, 105, 36, "Play back");
        playRecBtn->onClick = [player, recorder, status, fileLabel, st]() {
            auto audio = recorder->TakeBuffer();
            if (audio && audio->IsValid()) {
                st->currentAudio = audio;
                st->currentName = "recording";
                st->mono = AudioToMonoFloat(*audio);
                st->rate = audio->GetSampleRate();
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

        // ----- Horizontal VU strip (bottom) -----
        auto vuLabel = std::make_shared<UltraCanvasLabel>("VuLabel", 10, 520, 400, 22);
        vuLabel->SetText("Level (VU)");
        vuLabel->SetFontWeight(FontWeight::Bold);
        vuLabel->SetTextColor(Color(90, 90, 90));
        container->AddChild(vuLabel);

        auto vuStrip = std::make_shared<UltraCanvasLevelMeter>("AudioVuStrip", 980, 24);
        vuStrip->SetElementAbsolutePosition(Point2Df(10, 548));
        container->AddChild(vuStrip);

        auto vuCaption = std::make_shared<UltraCanvasLabel>("VuCaption", 10, 578, 980, 16);
        vuCaption->SetText("Live level: microphone input while recording, playback output "
                           "while playing. Fill = peak, dark tick = RMS.");
        vuCaption->SetFontSize(10);
        vuCaption->SetTextColor(Color(130, 138, 152));
        container->AddChild(vuCaption);

        // Drive the strip at ~30 fps: recorder levels win while capturing;
        // otherwise compute peak/RMS of the audible 40 ms window from the
        // decoded PCM at the playhead; else let the bar decay gently.
        std::weak_ptr<UltraCanvasContainer> rootWeak = container;
        std::weak_ptr<UltraCanvasLevelMeter> vuWeak = vuStrip;
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            app->StartTimer(33, true, [rootWeak, vuWeak, recWeak, playerWeak, st](TimerId id) {
                if (rootWeak.expired()) {
                    if (auto* a = UltraCanvasApplication::GetInstance()) a->StopTimer(id);
                    return;
                }
                auto vu = vuWeak.lock();
                if (!vu) return;

                float peak = -1.0f, rms = 0.0f;
                if (auto rec = recWeak.lock()) {
                    auto core = rec->GetRecorder();
                    if (core && core->GetState() == AudioRecordingState::Recording) {
                        peak = core->GetCurrentPeakLevel();
                        rms  = core->GetCurrentRMSLevel();
                    }
                }
                if (peak < 0.0f && !st->mono.empty() && st->rate > 0.0) {
                    if (auto p = playerWeak.lock()) {
                        if (auto raw = p->GetPlayer(); raw && raw->IsPlaying()) {
                            size_t end = std::min(st->mono.size(),
                                    static_cast<size_t>(raw->GetPosition() * st->rate));
                            size_t window = static_cast<size_t>(0.040 * st->rate);
                            size_t start = end > window ? end - window : 0;
                            float mx = 0.0f;
                            double sumSq = 0.0;
                            for (size_t i = start; i < end; ++i) {
                                mx = std::max(mx, std::fabs(st->mono[i]));
                                sumSq += static_cast<double>(st->mono[i]) * st->mono[i];
                            }
                            size_t count = end - start;
                            peak = mx;
                            rms = count ? static_cast<float>(std::sqrt(sumSq / count)) : 0.0f;
                        }
                    }
                }
                if (peak < 0.0f) {   // idle: gentle fall-back
                    peak = st->lastPeak * 0.82f;
                    rms  = st->lastRms * 0.82f;
                }
                st->lastPeak = peak;
                st->lastRms = rms;
                vu->SetLevel(peak, rms);
            });
        }

        return container;
    }

} // namespace UltraCanvas
