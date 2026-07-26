// Apps/DemoApp/UltraCanvasLevelMeterExamples.cpp
// Dedicated demo page for UltraCanvasLevelMeter, the three-zone VU meter /
// scrolling waveform strip used by the audio recorder element. Shows both render
// modes side by side in several colour themes, driven live by a selectable
// source: real playback levels of the bundled sample track, two synthetic
// generators, or manual peak/RMS sliders — plus a dBFS readout and Reset().
// Version: 1.0.0
// Last Modified: 2026-07-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasAudioRecorderElement.h"   // UltraCanvasLevelMeter
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasAudio.h"
#include "Plugins/Charts/UltraCanvasSTFT.h"    // AudioToMonoFloat
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {

// =============================================================================
// SHARED PAGE STATE (owned by the timer + control callbacks)
// =============================================================================
    struct LevelMeterDemoState {
        // All meters get SetLevel(); waveform-mode strips also get a sample push.
        std::vector<std::weak_ptr<UltraCanvasLevelMeter>> meters;
        std::vector<std::weak_ptr<UltraCanvasLevelMeter>> strips;
        std::weak_ptr<UltraCanvasAudioPlayerElement> player;
        std::weak_ptr<UltraCanvasLabel> readout;

        std::vector<float> mono;      // decoded sample track (mono, [-1, 1])
        double rate = 0.0;

        int source = 1;               // 0 playback, 1 beat, 2 breathing, 3 manual
        float manualPeak = 0.7f;
        float manualRms = 0.45f;

        double t = 0.0;               // synthetic-source clock (advanced per tick)
        float lastPeak = 0.0f;        // smoothed values, also used for decay
        float lastRms = 0.0f;
    };

    std::string ToDb(float v) {
        if (v <= 0.001f) return "-inf";
        std::ostringstream oss;
        double db = 20.0 * std::log10(static_cast<double>(v));
        oss.setf(std::ios::fixed);
        oss.precision(1);
        oss << db;
        return oss.str();
    }

// Compute the current peak/RMS pair for the selected source. `dt` is the timer
// period in seconds.
    void ComputeLevels(const std::shared_ptr<LevelMeterDemoState>& st, double dt,
                       float& peak, float& rms) {
        st->t += dt;
        switch (st->source) {
            case 0: {   // Sample-track playback: levels of the audible 40 ms window
                auto player = st->player.lock();
                bool playing = false;
                if (player && !st->mono.empty()) {
                    if (auto raw = player->GetPlayer()) {
                        playing = raw->IsPlaying();
                        if (playing) {
                            size_t end = std::min(st->mono.size(),
                                    static_cast<size_t>(raw->GetPosition() * st->rate));
                            size_t window = static_cast<size_t>(0.040 * st->rate);
                            size_t start = end > window ? end - window : 0;
                            float mx = 0.0f;
                            double sumSq = 0.0;
                            for (size_t i = start; i < end; ++i) {
                                float s = std::fabs(st->mono[i]);
                                mx = std::max(mx, s);
                                sumSq += static_cast<double>(st->mono[i]) * st->mono[i];
                            }
                            size_t count = end - start;
                            peak = mx;
                            rms = count ? static_cast<float>(std::sqrt(sumSq / count)) : 0.0f;
                        }
                    }
                }
                if (!playing) {   // paused/stopped: let the meters fall back gently
                    peak = st->lastPeak * 0.82f;
                    rms = st->lastRms * 0.82f;
                }
                break;
            }
            case 1: {   // Synthetic beat: kick every 0.54 s + off-beat hat + hum
                double beat = std::fmod(st->t, 0.54);
                double kick = std::exp(-7.0 * beat);
                double hat = std::fmod(st->t + 0.27, 0.54) < 0.06 ? 0.35 : 0.0;
                double hum = 0.10 + 0.05 * std::sin(2 * M_PI * 0.9 * st->t);
                peak = static_cast<float>(std::clamp(hum + 0.85 * kick + hat, 0.0, 1.0));
                rms = st->lastRms + 0.25f * (0.68f * peak - st->lastRms);   // lazy RMS
                break;
            }
            case 2: {   // Breathing sine: slow swell between quiet and loud
                double swell = 0.5 * (1.0 + std::sin(2 * M_PI * 0.35 * st->t - M_PI / 2));
                peak = static_cast<float>(0.08 + 0.88 * swell);
                rms = 0.72f * peak;
                break;
            }
            default:    // Manual sliders
                peak = st->manualPeak;
                rms = st->manualRms;
                break;
        }
        st->lastPeak = peak;
        st->lastRms = rms;
    }

    } // namespace

// =============================================================================
// DEMO PAGE
// =============================================================================
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateLevelMeterExamples() {
        const int CONTAINER_W = 1180;
        const int CONTAINER_H = 800;
        const int PAD = 16;

        auto root = std::make_shared<UltraCanvasContainer>("LvlRoot", 0, 0, CONTAINER_W, CONTAINER_H);
        root->SetBackgroundColor(Color(248, 249, 252, 255));
        root->SetPadding(PAD);
        root->layout.SetFlexColumn().SetFlexGap(10)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto addFlex = [](const std::shared_ptr<UltraCanvasContainer>& parent,
                          const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
            child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            parent->AddChild(child);
        };
        auto fixedHeight = [](const std::shared_ptr<UltraCanvasUIElement>& el, float h) {
            el->size.height = CSSLayout::Dimension::Px(h);
        };

        auto title = std::make_shared<UltraCanvasLabel>("LvlTitle", 0, 0, 0, 30);
        title->SetText("Level Meter & VU Strip");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(30, 41, 59, 255));
        addFlex(root, title, 0);

        auto subtitle = std::make_shared<UltraCanvasLabel>("LvlSubtitle", 0, 0, 0, 34);
        subtitle->SetText("UltraCanvasLevelMeter in both render modes: the three-zone VU bar "
                          "(green / yellow / red peak fill with an RMS tick) and the scrolling "
                          "waveform strip. Colours are fully themeable via SetColors(). Drive the "
                          "meters from real playback levels of the sample track, a synthetic "
                          "generator, or the manual sliders.");
        subtitle->SetFontSize(11);
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetTextColor(Color(100, 110, 125, 255));
        addFlex(root, subtitle, 0);

        auto st = std::make_shared<LevelMeterDemoState>();

        // ----- Card helpers -----
        auto makeCard = [](const std::string& id) {
            auto card = std::make_shared<UltraCanvasContainer>(id, 0, 0, 560, 300);
            card->SetBackgroundColor(Color(255, 255, 255, 255));
            card->SetBorders(1.0f, Color(224, 227, 235, 255), 10.0f);
            card->SetPadding(14);
            card->layout.SetFlexColumn().SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            return card;
        };
        auto makeCardTitle = [&](const std::string& id, const std::string& text) {
            auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
            l->SetText(text);
            l->SetFontSize(13);
            l->SetFontWeight(FontWeight::Bold);
            l->SetTextColor(Color(30, 41, 59, 255));
            return l;
        };
        auto makeCaption = [&](const std::string& id, const std::string& text) {
            auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 16);
            l->SetText(text);
            l->SetFontSize(10);
            l->SetTextColor(Color(130, 138, 152, 255));
            return l;
        };

        // ----- Card A: three-zone VU meters in four themes -----
        auto vuCard = makeCard("LvlVuCard");
        {
            auto cardTitle = makeCardTitle("LvlVuTitle", "Three-zone VU meter");
            fixedHeight(cardTitle, 20);
            addFlex(vuCard, cardTitle, 0);

            struct VuTheme {
                const char* name;
                float height;
                bool custom;
                Color bg, low, mid, high;
            };
            const VuTheme themes[] = {
                {"Classic (default colours)", 18, false, {}, {}, {}, {}},
                {"Modern accent", 18, true, Color(235, 238, 243), Color(34, 197, 94),
                        Color(250, 204, 21), Color(239, 68, 68)},
                {"Ocean on dark", 18, true, Color(15, 30, 48), Color(56, 189, 248),
                        Color(99, 102, 241), Color(244, 63, 94)},
                {"Large broadcast bar", 30, true, Color(24, 24, 32), Color(74, 222, 128),
                        Color(253, 224, 71), Color(248, 113, 113)},
            };
            int idx = 0;
            for (const auto& theme : themes) {
                auto caption = makeCaption("LvlVuCap" + std::to_string(idx), theme.name);
                fixedHeight(caption, 16);
                addFlex(vuCard, caption, 0);

                auto meter = std::make_shared<UltraCanvasLevelMeter>(
                        "LvlVu" + std::to_string(idx), 500, theme.height);
                if (theme.custom)
                    meter->SetColors(theme.bg, theme.low, theme.mid, theme.high);
                fixedHeight(meter, theme.height);
                addFlex(vuCard, meter, 0);
                st->meters.push_back(meter);
                ++idx;
            }

            auto note = makeCaption("LvlVuNote",
                    "Fill = peak level across the green / yellow / red zones "
                    "(60% / 85% thresholds); the dark tick marks the RMS level.");
            fixedHeight(note, 30);
            note->SetWrap(TextWrap::WrapWord);
            addFlex(vuCard, note, 1);
        }

        // ----- Card B: scrolling waveform strips in two themes -----
        auto stripCard = makeCard("LvlStripCard");
        {
            auto cardTitle = makeCardTitle("LvlStripTitle", "Scrolling waveform strip");
            fixedHeight(cardTitle, 20);
            addFlex(stripCard, cardTitle, 0);

            auto cap1 = makeCaption("LvlStripCap0", "Classic (default colours)");
            fixedHeight(cap1, 16);
            addFlex(stripCard, cap1, 0);
            auto strip1 = std::make_shared<UltraCanvasLevelMeter>("LvlStrip0", 500, 66);
            strip1->SetWaveformMode(true);
            fixedHeight(strip1, 66);
            addFlex(stripCard, strip1, 0);
            st->strips.push_back(strip1);

            auto cap2 = makeCaption("LvlStripCap1", "Violet on dark");
            fixedHeight(cap2, 16);
            addFlex(stripCard, cap2, 0);
            auto strip2 = std::make_shared<UltraCanvasLevelMeter>("LvlStrip1", 500, 66);
            strip2->SetWaveformMode(true);
            strip2->SetColors(Color(24, 24, 32), Color(167, 139, 250),
                              Color(196, 181, 253), Color(233, 213, 255));
            fixedHeight(strip2, 66);
            addFlex(stripCard, strip2, 0);
            st->strips.push_back(strip2);

            auto note = makeCaption("LvlStripNote",
                    "Each tick pushes the current peak into a 200-sample history "
                    "(PushWaveformSample), drawn as mirrored bars around the centre line.");
            fixedHeight(note, 30);
            note->SetWrap(TextWrap::WrapWord);
            addFlex(stripCard, note, 1);
        }

        auto cardsRow = std::make_shared<UltraCanvasContainer>("LvlCards", 0, 0, CONTAINER_W - 2 * PAD, 310);
        cardsRow->layout.SetFlexRow().SetFlexGap(12)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        fixedHeight(cardsRow, 310);
        addFlex(cardsRow, vuCard, 1);
        addFlex(cardsRow, stripCard, 1);
        addFlex(root, cardsRow, 0);

        // ----- Source / control card -----
        auto srcCard = makeCard("LvlSrcCard");
        {
            auto cardTitle = makeCardTitle("LvlSrcTitle", "Signal source");
            fixedHeight(cardTitle, 20);
            addFlex(srcCard, cardTitle, 0);

            // One row: source dropdown | manual sliders | reset | dBFS readout.
            auto ctrlRow = std::make_shared<UltraCanvasContainer>("LvlCtrlRow", 0, 0, 0, 56);
            ctrlRow->layout.SetFlexRow().SetFlexGap(16)
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            fixedHeight(ctrlRow, 56);

            auto makeStack = [&](const std::string& id, const std::string& labelText,
                                 const std::shared_ptr<UltraCanvasUIElement>& control,
                                 float width) {
                auto stack = std::make_shared<UltraCanvasContainer>(id, 0, 0, width, 56);
                stack->layout.SetFlexColumn().SetFlexGap(4)
                             .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
                stack->size.width = CSSLayout::Dimension::Px(width);
                auto l = makeCaption(id + "Lbl", labelText);
                fixedHeight(l, 14);
                addFlex(stack, l, 0);
                fixedHeight(control, 26);
                addFlex(stack, control, 0);
                return stack;
            };

            auto srcDd = std::make_shared<UltraCanvasDropdown>("LvlSource", 0, 0, 230, 26);
            srcDd->AddItem("Sample track playback");
            srcDd->AddItem("Synthetic beat");
            srcDd->AddItem("Breathing sine swell");
            srcDd->AddItem("Manual sliders");
            srcDd->SetSelectedIndex(st->source);
            srcDd->onSelectionChanged = [st](int index, const DropdownItem&) {
                st->source = index;
                st->t = 0.0;
                // Start playing automatically when the playback source is chosen.
                if (index == 0) {
                    if (auto p = st->player.lock()) p->Play();
                }
            };
            addFlex(ctrlRow, makeStack("LvlSrcStack", "Source:", srcDd, 230), 0);

            auto peakSlider = std::make_shared<UltraCanvasSlider>("LvlManPeak", 0, 0, 200, 26);
            peakSlider->SetRange(0.0f, 1.0f);
            peakSlider->SetStep(0.01f);
            peakSlider->SetValue(st->manualPeak);
            peakSlider->onValueChanged = [st](float v) { st->manualPeak = v; };
            addFlex(ctrlRow, makeStack("LvlPeakStack", "Manual peak:", peakSlider, 200), 0);

            auto rmsSlider = std::make_shared<UltraCanvasSlider>("LvlManRms", 0, 0, 200, 26);
            rmsSlider->SetRange(0.0f, 1.0f);
            rmsSlider->SetStep(0.01f);
            rmsSlider->SetValue(st->manualRms);
            rmsSlider->onValueChanged = [st](float v) { st->manualRms = v; };
            addFlex(ctrlRow, makeStack("LvlRmsStack", "Manual RMS:", rmsSlider, 200), 0);

            auto resetBtn = std::make_shared<UltraCanvasButton>("LvlReset", 0, 0, 110, 26, "Reset meters");
            resetBtn->SetOnClick([st] {
                for (auto& w : st->meters) if (auto m = w.lock()) m->Reset();
                for (auto& w : st->strips) if (auto m = w.lock()) m->Reset();
                st->lastPeak = st->lastRms = 0.0f;
            });
            addFlex(ctrlRow, makeStack("LvlResetStack", "Reset() clears levels & history:",
                                       resetBtn, 200), 0);

            addFlex(srcCard, ctrlRow, 0);

            // dBFS readout, updated every tick.
            auto readout = std::make_shared<UltraCanvasLabel>("LvlReadout", 0, 0, 0, 20);
            readout->SetFontSize(12);
            readout->SetFontWeight(FontWeight::Bold);
            readout->SetTextColor(Color(30, 41, 59, 255));
            readout->SetText("Peak  -inf dBFS      RMS  -inf dBFS");
            fixedHeight(readout, 20);
            addFlex(srcCard, readout, 0);
            st->readout = readout;

            // Embedded audio player with the bundled sample track pre-loaded.
            auto playerLabel = makeCaption("LvlPlayerLbl",
                    "Sample track for the playback source (levels are computed from the "
                    "decoded PCM at the playhead):");
            fixedHeight(playerLabel, 16);
            addFlex(srcCard, playerLabel, 0);

            auto player = CreateAudioPlayer("LvlPlayer", 0, 0, 640, 56);
            fixedHeight(player, 56);
            addFlex(srcCard, player, 0);
            st->player = player;

            auto status = makeCaption("LvlStatus", "");
            fixedHeight(status, 16);
            addFlex(srcCard, status, 0);

            const std::string audioPath = NormalizePath(
                    GetResourcesDir() + "media/audios/the_mountain-cinematic-489998.mp3");
            if (auto audio = UCAudio::LoadFromFile(audioPath)) {
                if (audio->IsValid()) {
                    st->mono = AudioToMonoFloat(*audio);
                    st->rate = audio->GetSampleRate();
                    player->LoadFromAudio(audio);
                    size_t slash = audioPath.find_last_of("/\\");
                    player->SetTrackTitle(slash == std::string::npos
                                          ? audioPath : audioPath.substr(slash + 1));
                }
            }
            if (st->mono.empty()) {
                status->SetText("Sample track unavailable — the playback source will stay silent.");
                status->SetTextColor(Color(180, 60, 60, 255));
            } else if (!UltraCanvasAudioDevices::IsAvailable()) {
                status->SetText("Audio backend not compiled in — playback is silent, but the "
                                "synthetic and manual sources still drive the meters.");
                status->SetTextColor(Color(180, 60, 60, 255));
            }
        }
        addFlex(root, srcCard, 0);

        // ----- Animation timer (~30 fps). Stops itself once the page is gone. -----
        const double dt = 0.033;
        std::weak_ptr<UltraCanvasContainer> rootWeak = root;
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            app->StartTimer(33, true, [st, rootWeak, dt](TimerId id) {
                if (rootWeak.expired()) {
                    if (auto* a = UltraCanvasApplication::GetInstance()) a->StopTimer(id);
                    return;
                }
                float peak = 0.0f, rms = 0.0f;
                ComputeLevels(st, dt, peak, rms);
                for (auto& w : st->meters) if (auto m = w.lock()) m->SetLevel(peak, rms);
                for (auto& w : st->strips) if (auto m = w.lock()) m->PushWaveformSample(peak);
                if (auto r = st->readout.lock())
                    r->SetText("Peak  " + ToDb(peak) + " dBFS      RMS  " + ToDb(rms) + " dBFS");
            });
        }

        return root;
    }

} // namespace UltraCanvas
