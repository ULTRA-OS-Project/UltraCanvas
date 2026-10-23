// Apps/DemoApp/UltraCanvasAudioExamples.cpp
// Audio Player & Recorder demo page: playback, recording, save-with-format
// selection (driven by the runtime supported-format inventory) and a live
// horizontal VU strip fed by the recorder input or the playback output.
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): playback and
//                  recording as cards side by side, the VU strip in a card as wide
//                  as the page, every text sized to fit
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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

    std::string StripExtension(const std::string& name) {
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos ? name : name.substr(0, dot);
    }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAudioExamples() {
        auto page = DemoKit::CreatePage("AudioExamples", "Audio Player & Recorder",
                "Playback, recording, saving in a format this build can encode, and a live "
                "level strip fed by the recorder input or the playback output.");

        auto st = std::make_shared<AudioDemoState>();

        // Green when a backend is there, red when not; later the result of
        // the last action.
        auto status = DemoKit::AddText(page, "AudioStatus", "");
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

        auto sections = DemoKit::AddRow(page, "AudioSections");

        // ----- Playback section -----
        auto playbackCard = DemoKit::AddCard(sections, "PlaybackLabel", "Playback");

        auto player = CreateAudioPlayer("DemoPlayer", 0, 0, 600, 56);
        DemoKit::AddStretched(playbackCard, player);

        auto playbackRow = DemoKit::AddRow(playbackCard, "PlaybackRow", 8);
        playbackRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        // Shows the name of the file currently loaded into the player.
        auto fileLabel = DemoKit::CreateCaption("PlaybackFile", "No file loaded.");
        fileLabel->SetFontSize(11);

        // Strip any directory part so we display just the file name.
        auto baseName = [](const std::string& p) -> std::string {
            size_t slash = p.find_last_of("/\\");
            return slash == std::string::npos ? p : p.substr(slash + 1);
        };

        auto loadBtn = CreateButton("LoadAudio", 0, 0, 110, 36, "Open");
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
        DemoKit::Add(playbackRow, loadBtn);
        DemoKit::Add(playbackRow, fileLabel, 1);

        // ----- Recording section -----
        auto recCard = DemoKit::AddCard(sections, "RecLabel", "Recording");

        auto recorder = CreateAudioRecorder("DemoRecorder", 0, 0, 600, 72);
        recorder->onRecordStarted = [status]() { status->SetText("Recording..."); };
        recorder->onRecordStopped = [status]() { status->SetText("Recording stopped."); };
        DemoKit::AddStretched(recCard, recorder);
        auto recWeak = std::weak_ptr<UltraCanvasAudioRecorderElement>(recorder);

        // ----- Save with format selection -----
        // The dropdown lists every audio format THIS build can encode, straight
        // from the runtime supported-format inventory — new encoder plugins show
        // up here automatically.
        auto saveRow = DemoKit::AddRow(recCard, "SaveRow", 8);
        saveRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::AddCaption(saveRow, "SaveFmtLabel", "Save format:")->SetFontSize(11);

        auto formatDd = std::make_shared<UltraCanvasDropdown>("SaveFormat", 0, 0, 150, 26);
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
        DemoKit::AddFixed(saveRow, formatDd);

        recorder->onSaved = [status](const std::string& path) {
            status->SetText("Saved to " + path);
        };
        recorder->onSaveCancelled = [status]() {
            status->SetText("Save cancelled or no recording available.");
        };

        auto saveBtn = CreateButton("SaveAudio", 0, 0, 95, 36, "Save");
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
            AudioFormat fmt = AudioFormatFromExtension(ext);
            if (fmt == AudioFormat::Unknown) fmt = AudioFormat::WAV;

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
        DemoKit::Add(saveRow, saveBtn);

        auto playRecBtn = CreateButton("PlayRec", 0, 0, 105, 36, "Play back");
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
        DemoKit::Add(saveRow, playRecBtn);

        // ----- Horizontal VU strip (bottom) -----
        auto vuCard = DemoKit::AddCard(page, "VuLabel", "Level (VU)");

        auto vuStrip = std::make_shared<UltraCanvasLevelMeter>("AudioVuStrip", 980, 24);
        DemoKit::AddStretched(vuCard, vuStrip);

        DemoKit::AddCaption(vuCard, "VuCaption",
                "Live level: microphone input while recording, playback output "
                "while playing. Fill = peak, dark tick = RMS.");

        // Drive the strip at ~30 fps: recorder levels win while capturing;
        // otherwise compute peak/RMS of the audible 40 ms window from the
        // decoded PCM at the playhead; else let the bar decay gently.
        std::weak_ptr<UltraCanvasContainer> rootWeak = page;
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

        return page;
    }

} // namespace UltraCanvas
