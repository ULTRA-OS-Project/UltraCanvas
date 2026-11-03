// Apps/DemoApp/UltraCanvasWaveformExamples.cpp
// Demo page for the amplitude Waveform chart (UltraCanvasWaveformElement),
// wired to an audio player that ships with a sample track pre-selected.
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): the waveform as
//                  wide as the page in a card, the player and the pickers in a
//                  wrapping row under it, every label sized to its text
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasWaveformElement.h"
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UltraCanvas {

// =============================================================================
// PCM -> WAVEFORM ENVELOPE
// =============================================================================
// Decode an interleaved PCM buffer into a module-agnostic WaveformChannelData,
// summarising the signal as `targetBlocks` min/max/rms triplets. Channels are
// down-mixed to mono by averaging so the envelope represents the whole track.
    static WaveformChannelData BuildWaveformFromAudio(
            const std::shared_ptr<UCAudio>& audio, int targetBlocks = 2000) {
        WaveformChannelData wf;
        if (!audio || !audio->IsValid()) return wf;

        const AudioBufferInfo& info = audio->GetInfo();
        const int channels = std::max(1, info.channels);
        const size_t frames = info.frameCount;
        if (frames == 0) return wf;

        const uint8_t* bytes = audio->GetData();

        // Returns the normalised [-1, 1] sample for (frame, channel).
        auto sampleAt = [&](size_t frame, int ch) -> float {
            size_t idx = frame * channels + ch;   // interleaved
            switch (info.sampleType) {
                case AudioSampleType::PCM_S16: {
                    const int16_t* s = reinterpret_cast<const int16_t*>(bytes);
                    return s[idx] / 32768.0f;
                }
                case AudioSampleType::PCM_S32: {
                    const int32_t* s = reinterpret_cast<const int32_t*>(bytes);
                    return static_cast<float>(s[idx] / 2147483648.0);
                }
                case AudioSampleType::PCM_F32: {
                    const float* s = reinterpret_cast<const float*>(bytes);
                    return s[idx];
                }
                case AudioSampleType::PCM_S24: {
                    const uint8_t* p = bytes + idx * 3;
                    int32_t v = (p[0]) | (p[1] << 8) | (p[2] << 16);
                    if (v & 0x800000) v |= ~0xFFFFFF;   // sign-extend
                    return static_cast<float>(v / 8388608.0);
                }
            }
            return 0.0f;
        };

        const int blocks = std::min<int>(targetBlocks, static_cast<int>(frames));
        const size_t framesPerBlock = std::max<size_t>(1, frames / blocks);

        wf.minValues.reserve(blocks);
        wf.maxValues.reserve(blocks);
        wf.rmsValues.reserve(blocks);

        for (int b = 0; b < blocks; ++b) {
            size_t start = static_cast<size_t>(b) * framesPerBlock;
            size_t end   = (b == blocks - 1) ? frames
                                             : std::min(frames, start + framesPerBlock);
            float mn = 1.0f, mx = -1.0f;
            double sumSq = 0.0;
            size_t count = 0;
            for (size_t f = start; f < end; ++f) {
                float mono = 0.0f;
                for (int c = 0; c < channels; ++c) mono += sampleAt(f, c);
                mono /= channels;
                mn = std::min(mn, mono);
                mx = std::max(mx, mono);
                sumSq += static_cast<double>(mono) * mono;
                ++count;
            }
            if (count == 0) { mn = 0.0f; mx = 0.0f; }
            wf.minValues.push_back(mn);
            wf.maxValues.push_back(mx);
            wf.rmsValues.push_back(static_cast<float>(
                    count ? std::sqrt(sumSq / count) : 0.0));
        }

        wf.samplesPerBlock = static_cast<int>(framesPerBlock);
        wf.totalBlocks = blocks;
        wf.duration = audio->GetDuration();
        return wf;
    }

// =============================================================================
// DEMO PAGE
// =============================================================================
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateWaveformExamples() {
        auto container = DemoKit::CreatePage("WaveformExamples", "Waveform Chart",
                "Amplitude min/max envelope with optional RMS band and a click/drag "
                "playhead, synced to the audio player below.");

        // Status line (audio backend availability + load result).
        auto status = DemoKit::AddText(container, "WaveformStatus", "");
        status->SetFontSize(11);

        auto card = DemoKit::AddCard(container, "WaveformCard");

        // The sample track that ships with the framework.
        const std::string audioPath = NormalizePath(
                GetResourcesDir() + "media/audios/the_mountain-cinematic-489998.mp3");

        // ----- Waveform element -----
        auto waveform = CreateWaveformElement("WaveformChart", 0, 0, 960, 200);
        waveform->SetOverlay(WaveformOverlay::RMSAndZeroAxis);
        DemoKit::AddStretched(card, waveform);

        // The player and the pickers beside it; they wrap under it when narrow.
        auto controlRow = DemoKit::AddRow(card, "WaveformControls", 20);

        // ----- Audio player (sample track pre-selected) -----
        auto player = CreateAudioPlayer("WaveformPlayer", 0, 0, 640, 56);
        DemoKit::AddFixed(controlRow, player);

        // A picker: its label over its dropdown.
        auto addPicker = [&controlRow](const std::string& id, const std::string& labelId,
                                       const std::string& text,
                                       const std::shared_ptr<UltraCanvasDropdown>& drop) {
            auto column = DemoKit::CreateColumn(id, 2);
            DemoKit::AddText(column, labelId, text)->SetFontSize(11);
            DemoKit::AddFixed(column, drop);
            DemoKit::Add(controlRow, column);
        };

        auto fileLabel = DemoKit::CreateText("WaveformFile", "");
        fileLabel->SetFontSize(11);
        fileLabel->SetTextColor(Color(90, 90, 90));

        // Strip any directory part for display.
        auto baseName = [](const std::string& p) -> std::string {
            size_t slash = p.find_last_of("/\\");
            return slash == std::string::npos ? p : p.substr(slash + 1);
        };
        const std::string trackName = baseName(audioPath);

        if (!UltraCanvasAudioDevices::IsAvailable()) {
            status->SetText("Audio backend not compiled in — rebuild with "
                            "-DULTRACANVAS_ENABLE_AUDIO=ON. Showing static waveform only.");
            status->SetTextColor(Color(180, 60, 60));
        }

        // Decode the track once for the waveform envelope, and load it into the
        // player for transport. Both share the same on-disk file.
        if (auto audio = UCAudio::LoadFromFile(audioPath)) {
            if (audio->IsValid()) {
                waveform->SetData(BuildWaveformFromAudio(audio));
                player->LoadFromAudio(audio);
                player->SetTrackTitle(trackName);
                fileLabel->SetText("File: " + trackName);
                if (UltraCanvasAudioDevices::IsAvailable()) {
                    status->SetText("Loaded: " + trackName + "  (" +
                                    std::to_string(audio->GetSampleRate()) + " Hz, " +
                                    std::to_string(audio->GetChannels()) + " ch)");
                    status->SetTextColor(Color(40, 120, 40));
                }
            } else {
                status->SetText("Could not decode sample track: " + trackName);
                status->SetTextColor(Color(180, 60, 60));
            }
        } else {
            status->SetText("Sample track not found: " + audioPath);
            status->SetTextColor(Color(180, 60, 60));
        }

        // ----- Sync playhead <-> transport -----
        // Drive the waveform playhead from playback position. Chain the player's
        // existing position callback (the element uses it to update its own seek
        // slider) so both stay in sync.
        std::weak_ptr<UltraCanvasWaveformElement> waveformWeak = waveform;
        if (auto rawPlayer = player->GetPlayer()) {
            auto prevPos = rawPlayer->onPositionChanged;
            rawPlayer->onPositionChanged = [prevPos, waveformWeak](double seconds) {
                if (prevPos) prevPos(seconds);
                if (auto w = waveformWeak.lock()) w->SetPlayheadTime(seconds);
            };
        }

        // Clicking/dragging the waveform seeks the player.
        std::weak_ptr<UltraCanvasAudioPlayerElement> playerWeak = player;
        waveform->onSeek = [playerWeak](double seconds) {
            if (auto p = playerWeak.lock()) p->Seek(seconds);
        };

        // ----- Render-style picker -----
        auto styleDrop = CreateDropdown("WaveformStyleDrop", 0, 0, 150, 30);
        styleDrop->AddItem("Filled");
        styleDrop->AddItem("Outline");
        styleDrop->AddItem("Bars");
        styleDrop->SetSelectedIndex(0);
        styleDrop->onSelectionChanged = [waveformWeak](int index, const DropdownItem&) {
            auto w = waveformWeak.lock();
            if (!w) return;
            switch (index) {
                case 1:  w->SetRenderStyle(WaveformRenderStyle::Outline); break;
                case 2:  w->SetRenderStyle(WaveformRenderStyle::Bars);    break;
                default: w->SetRenderStyle(WaveformRenderStyle::Filled);  break;
            }
        };
        addPicker("WaveformStylePicker", "WaveformStyleLabel", "Render style:", styleDrop);

        // ----- Overlay picker -----
        auto overlayDrop = CreateDropdown("WaveformOverlayDrop", 0, 0, 150, 30);
        overlayDrop->AddItem("None");
        overlayDrop->AddItem("RMS");
        overlayDrop->AddItem("Zero axis");
        overlayDrop->AddItem("RMS + Zero axis");
        overlayDrop->SetSelectedIndex(3);
        overlayDrop->onSelectionChanged = [waveformWeak](int index, const DropdownItem&) {
            auto w = waveformWeak.lock();
            if (!w) return;
            switch (index) {
                case 1:  w->SetOverlay(WaveformOverlay::RMS);            break;
                case 2:  w->SetOverlay(WaveformOverlay::ZeroAxis);       break;
                case 3:  w->SetOverlay(WaveformOverlay::RMSAndZeroAxis); break;
                default: w->SetOverlay(WaveformOverlay::NoOverlay);           break;
            }
        };
        addPicker("WaveformOverlayPicker", "WaveformOverlayLabel", "Overlay:", overlayDrop);

        // ----- Display-range picker -----
        // Limits the visible portion of the track to a trailing window that
        // scrolls with the playhead, or shows the whole track ("All audio").
        auto rangeDrop = CreateDropdown("WaveformRangeDrop", 0, 0, 150, 30);
        rangeDrop->AddItem("Last 10 seconds");
        rangeDrop->AddItem("Last 60 seconds");
        rangeDrop->AddItem("All audio");
        rangeDrop->SetSelectedIndex(2);   // default: whole track
        rangeDrop->onSelectionChanged = [waveformWeak](int index, const DropdownItem&) {
            auto w = waveformWeak.lock();
            if (!w) return;
            switch (index) {
                case 0:  w->SetVisibleWindowSeconds(10.0); break;
                case 1:  w->SetVisibleWindowSeconds(60.0); break;
                default: w->SetVisibleWindowSeconds(0.0);  break;   // all
            }
        };
        addPicker("WaveformRangePicker", "WaveformRangeLabel", "Display range:", rangeDrop);

        DemoKit::Add(card, fileLabel);

        return container;
    }

} // namespace UltraCanvas
