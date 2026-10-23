// Apps/DemoApp/UltraCanvasSpectrogramExamples.cpp
// Dedicated demo page for the audio spectrogram (UltraCanvasSpectrogramElement).
// A control panel exercises every STFT option (signal source, FFT size, overlap,
// window function, magnitude mode, dynamic range, frequency cap) plus the
// inherited heatmap display options (colour map, reverse, colour bar, log scale),
// with a live "frames x bins" readout of the most recent transform.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the panel and
//                  the chart in cards, the panel a column whose labels, controls
//                  and checkboxes size to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasSpectrogram.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// SIGNAL PRESETS
// =============================================================================
// Each preset fills `out` with a mono float signal in [-1, 1] and returns its
// sample rate. Synthetic presets use 16 kHz so the spectrogram shows content up
// to an 8 kHz Nyquist; the sample-track preset keeps the file's native rate.

    namespace {

    constexpr double kSynthRate = 16000.0;
    constexpr double kSynthSeconds = 4.0;
    constexpr double kTrackSeconds = 8.0;   // decoded track is clipped to this

    void SynthChirpAndTones(std::vector<float>& out) {
        const int n = static_cast<int>(kSynthRate * kSynthSeconds);
        out.resize(n);
        for (int i = 0; i < n; ++i) {
            double t = i / kSynthRate;
            double chirp = std::sin(2 * M_PI * (400.0 + 700.0 * t) * t);   // rising chirp
            double tone1 = 0.5 * std::sin(2 * M_PI * 2000.0 * t);
            double tone2 = 0.3 * std::sin(2 * M_PI * 5200.0 * t);
            out[i] = static_cast<float>(0.45 * chirp + tone1 + tone2) * 0.6f;
        }
    }

    void SynthLogSweep(std::vector<float>& out) {
        const int n = static_cast<int>(kSynthRate * kSynthSeconds);
        out.resize(n);
        const double f0 = 100.0, f1 = 7000.0;
        const double k = std::log(f1 / f0) / kSynthSeconds;
        for (int i = 0; i < n; ++i) {
            double t = i / kSynthRate;
            double phase = 2 * M_PI * f0 * (std::exp(k * t) - 1.0) / k;
            out[i] = static_cast<float>(0.8 * std::sin(phase));
        }
    }

    void SynthToneLadder(std::vector<float>& out) {
        const int n = static_cast<int>(kSynthRate * kSynthSeconds);
        out.resize(n);
        // Eight ascending steps, each with a soft attack/release envelope so the
        // spectrogram shows clean horizontal bars.
        const double freqs[8] = {440, 660, 880, 1320, 1760, 2640, 3520, 5280};
        const double stepLen = kSynthSeconds / 8.0;
        for (int i = 0; i < n; ++i) {
            double t = i / kSynthRate;
            int step = std::min(7, static_cast<int>(t / stepLen));
            double local = (t - step * stepLen) / stepLen;             // 0..1 in step
            double env = std::sin(M_PI * local);                       // fade in/out
            out[i] = static_cast<float>(0.8 * env * std::sin(2 * M_PI * freqs[step] * t));
        }
    }

    void SynthTremoloAndBursts(std::vector<float>& out) {
        const int n = static_cast<int>(kSynthRate * kSynthSeconds);
        out.resize(n);
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> uni(-1.0, 1.0);
        for (int i = 0; i < n; ++i) {
            double t = i / kSynthRate;
            double am = 0.5 * (1.0 + std::sin(2 * M_PI * 4.0 * t));    // 4 Hz tremolo
            double tone = am * std::sin(2 * M_PI * 1500.0 * t);
            // A broadband noise burst every second, 120 ms long.
            double inBurst = std::fmod(t, 1.0) < 0.12 ? 1.0 : 0.0;
            out[i] = static_cast<float>(0.6 * tone + 0.35 * inBurst * uni(rng));
        }
    }

    void SynthWhiteNoise(std::vector<float>& out) {
        const int n = static_cast<int>(kSynthRate * kSynthSeconds);
        out.resize(n);
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> uni(-1.0, 1.0);
        for (auto& s : out) s = static_cast<float>(0.7 * uni(rng));
    }

    // Colour maps that make sense for a spectrogram, best-first.
    const std::vector<std::pair<std::string, HeatmapColormap>>& SpecColormapMenu() {
        static const std::vector<std::pair<std::string, HeatmapColormap>> menu = {
            {"Inferno",   HeatmapColormap::Inferno},
            {"Magma",     HeatmapColormap::Magma},
            {"Viridis",   HeatmapColormap::Viridis},
            {"Plasma",    HeatmapColormap::Plasma},
            {"Cividis",   HeatmapColormap::Cividis},
            {"Turbo",     HeatmapColormap::Turbo},
            {"Hot",       HeatmapColormap::Hot},
            {"Grayscale", HeatmapColormap::Grayscale},
            {"Jet",       HeatmapColormap::Jet},
        };
        return menu;
    }

    // Shared page state: the current signal plus the pieces the control
    // callbacks need to talk to each other.
    struct SpectrogramDemoState {
        std::shared_ptr<UltraCanvasSpectrogramElement> spec;
        std::shared_ptr<UltraCanvasLabel> info;

        std::vector<float> trackMono;      // decoded sample track (may be empty)
        double trackRate = 0.0;
        std::string trackName;

        int fftSize = 1024;
        int hopDivisor = 4;                // hop = fftSize / hopDivisor
    };

    void ApplyHop(const std::shared_ptr<SpectrogramDemoState>& st) {
        st->spec->SetHopSize(std::max(1, st->fftSize / st->hopDivisor));
    }

    void ApplySignalPreset(const std::shared_ptr<SpectrogramDemoState>& st, int index) {
        std::vector<float> sig;
        double rate = kSynthRate;
        switch (index) {
            case 0:
                if (!st->trackMono.empty()) { sig = st->trackMono; rate = st->trackRate; break; }
                [[fallthrough]];           // track unavailable -> chirp preset
            case 1: SynthChirpAndTones(sig);    break;
            case 2: SynthLogSweep(sig);         break;
            case 3: SynthToneLadder(sig);       break;
            case 4: SynthTremoloAndBursts(sig); break;
            default: SynthWhiteNoise(sig);      break;
        }
        st->spec->SetSignal(sig, rate);
    }

    void RefreshInfo(const std::shared_ptr<SpectrogramDemoState>& st) {
        st->spec->Recompute();
        const SpectrogramResult& r = st->spec->GetResult();
        const SpectrogramParams& p = st->spec->GetParams();
        const char* windowNames[] = {"Rectangular", "Hann", "Hamming", "Blackman"};
        std::ostringstream oss;
        oss << "STFT: " << r.frames << " frames x " << r.bins << " bins @ "
            << static_cast<int>(r.sampleRate) << " Hz    (FFT " << p.fftSize
            << ", hop " << (p.hopSize > 0 ? p.hopSize : p.fftSize / 4) << ", "
            << windowNames[static_cast<int>(p.window)] << ", "
            << (p.magnitude == SpectrogramMagnitude::Decibels ? "dB" : "linear") << ")";
        st->info->SetText(oss.str());
    }

    } // namespace

// =============================================================================
// DEMO PAGE
// =============================================================================
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateSpectrogramExamples() {
        const int PANEL_W = 300;

        auto root = DemoKit::CreatePage("SpecRoot", "Spectrogram - Time x Frequency Analysis",
                "Short-Time Fourier Transform of an audio signal rendered through the "
                "heatmap engine. Every STFT parameter and display option is live: pick a "
                "signal, then experiment with FFT size, overlap, window, magnitude mode, "
                "dynamic range, frequency cap and colour maps.");

        // ----- State + spectrogram element -----
        auto st = std::make_shared<SpectrogramDemoState>();

        // Decode the bundled sample track once (first kTrackSeconds seconds, mono).
        const std::string audioPath = NormalizePath(
                GetResourcesDir() + "media/audios/the_mountain-cinematic-489998.mp3");
        if (auto audio = UCAudio::LoadFromFile(audioPath)) {
            if (audio->IsValid()) {
                st->trackMono = AudioToMonoFloat(*audio);
                st->trackRate = audio->GetSampleRate();
                const size_t maxSamples =
                        static_cast<size_t>(st->trackRate * kTrackSeconds);
                if (st->trackMono.size() > maxSamples) st->trackMono.resize(maxSamples);
                size_t slash = audioPath.find_last_of("/\\");
                st->trackName = slash == std::string::npos ? audioPath
                                                           : audioPath.substr(slash + 1);
            }
        }

        st->spec = CreateSpectrogramElement("SpecChart", 0, 0, 820, 620);
        st->spec->SetColormap(HeatmapColormap::Inferno);
        st->spec->SetShowColorBar(true);
        st->spec->SetFFTSize(st->fftSize);
        ApplyHop(st);
        ApplySignalPreset(st, st->trackMono.empty() ? 1 : 0);

        // ----- Main row: control panel (card) + spectrogram -----
        // The chart goes under the panel when the window is too narrow.
        auto row = DemoKit::AddRow(root, "SpecRow");

        // The panel is as wide as its widest text, at least PANEL_W, so a
        // longer caption or checkbox widens it instead of being cut.
        auto panel = DemoKit::AddCard(row, "SpecPanel");
        panel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetFlexBasis(CSSLayout::Dimension::Auto());
        CSSLayout::BoxConstraints panelLimits;
        panelLimits.minWidth = CSSLayout::Dimension::Px(PANEL_W);
        panel->boxConstraints = panelLimits;

        // --- Panel controls: a column, each control as wide as the panel ---
        const int pw = PANEL_W - 2 * 16;
        const int ctrlH = 26;

        auto addHeading = [&](const std::string& id, const std::string& text) {
            auto l = DemoKit::AddHeading(panel, id, text);
            l->SetMargin(4, 0, 0, 0);
        };
        auto addLabel = [&](const std::string& id, const std::string& text) {
            auto l = DemoKit::AddText(panel, id, text);
            l->SetFontSize(11);
            l->SetTextColor(Color(100, 110, 125, 255));
        };
        auto addDropdown = [&](const std::string& id,
                               const std::vector<std::string>& items, int selected,
                               std::function<void(int)> apply) {
            auto dd = std::make_shared<UltraCanvasDropdown>(id, 0, 0, pw, ctrlH);
            for (const auto& it : items) dd->AddItem(it);
            dd->SetSelectedIndex(selected);
            dd->onSelectionChanged = [apply](int index, const DropdownItem&) { apply(index); };
            DemoKit::AddStretched(panel, dd);
            return dd;
        };
        // A checkbox as wide as its text.
        auto addCheck = [&](const std::string& id, const std::string& text, bool initial,
                            std::function<void(bool)> apply) {
            auto cb = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, 0, 22, text);
            cb->SetChecked(initial);
            cb->onStateChanged = [apply](CheckedState, CheckedState ns) {
                apply(ns == CheckedState::Checked);
            };
            DemoKit::Add(panel, cb);
        };

        addHeading("SpecSigHead", "Signal");
        {
            std::string trackItem = st->trackMono.empty()
                    ? "Sample track (unavailable)"
                    : "Sample track (first 8 s)";
            addDropdown("SpecSignal",
                        {trackItem, "Chirp + steady tones", "Log sweep 100 Hz - 7 kHz",
                         "Tone ladder (8 steps)", "Tremolo + noise bursts", "White noise"},
                        st->trackMono.empty() ? 1 : 0,
                        [st](int index) { ApplySignalPreset(st, index); RefreshInfo(st); });
        }

        addHeading("SpecStftHead", "STFT parameters");

        addLabel("SpecFftLbl", "FFT size (frequency resolution):");
        addDropdown("SpecFft", {"256", "512", "1024", "2048", "4096"}, 2, [st](int index) {
            static const int sizes[] = {256, 512, 1024, 2048, 4096};
            st->fftSize = sizes[std::clamp(index, 0, 4)];
            st->spec->SetFFTSize(st->fftSize);
            ApplyHop(st);
            RefreshInfo(st);
        });

        addLabel("SpecHopLbl", "Overlap (hop size):");
        addDropdown("SpecHop",
                    {"87.5%  (hop = FFT/8)", "75%  (hop = FFT/4)",
                     "50%  (hop = FFT/2)", "None  (hop = FFT)"}, 1, [st](int index) {
            static const int divisors[] = {8, 4, 2, 1};
            st->hopDivisor = divisors[std::clamp(index, 0, 3)];
            ApplyHop(st);
            RefreshInfo(st);
        });

        addLabel("SpecWinLbl", "Window function:");
        addDropdown("SpecWin", {"Hann", "Hamming", "Blackman", "Rectangular"}, 0, [st](int index) {
            static const SpectrogramWindow windows[] = {
                SpectrogramWindow::Hann, SpectrogramWindow::Hamming,
                SpectrogramWindow::Blackman, SpectrogramWindow::Rectangular};
            st->spec->SetWindow(windows[std::clamp(index, 0, 3)]);
            RefreshInfo(st);
        });

        addLabel("SpecMagLbl", "Magnitude mode:");
        addDropdown("SpecMag", {"Decibels (log amplitude)", "Linear"}, 0, [st](int index) {
            st->spec->SetMagnitudeMode(index == 1 ? SpectrogramMagnitude::Linear
                                                  : SpectrogramMagnitude::Decibels);
            RefreshInfo(st);
        });

        addLabel("SpecDynLbl", "Dynamic range (dB mode):  30 - 120 dB");
        {
            auto slider = std::make_shared<UltraCanvasSlider>("SpecDyn", 0, 0, pw, ctrlH);
            slider->SetRange(30.0f, 120.0f);
            slider->SetStep(5.0f);
            slider->SetValue(80.0f);
            slider->onValueChanged = [st](float v) {
                st->spec->SetDynamicRangeDb(v);
                RefreshInfo(st);
            };
            DemoKit::AddStretched(panel, slider);
        }

        addLabel("SpecMaxFLbl", "Displayed frequency range:");
        addDropdown("SpecMaxF",
                    {"Full (up to Nyquist)", "Lower half (Nyquist/2)",
                     "Lower quarter (Nyquist/4)", "Lower eighth (Nyquist/8)"}, 0, [st](int index) {
            // 0 => Nyquist. The cap follows the *current* signal's sample rate.
            const SpectrogramResult& r = st->spec->GetResult();
            double rate = r.sampleRate > 0 ? r.sampleRate : kSynthRate;
            static const double fractions[] = {0.0, 0.25, 0.125, 0.0625}; // of sample rate
            double frac = fractions[std::clamp(index, 0, 3)];
            st->spec->SetMaxFrequency(frac <= 0.0 ? 0.0 : rate * frac);
            RefreshInfo(st);
        });

        addHeading("SpecDispHead", "Display");

        addLabel("SpecCmapLbl", "Colour map:");
        addDropdown("SpecCmap", [] {
            std::vector<std::string> names;
            for (const auto& m : SpecColormapMenu()) names.push_back(m.first);
            return names;
        }(), 0, [st](int index) {
            const auto& menu = SpecColormapMenu();
            if (index >= 0 && index < static_cast<int>(menu.size()))
                st->spec->SetColormap(menu[index].second);
        });

        addCheck("SpecReverse", "Reverse colour map", false,
                 [st](bool on) { st->spec->SetReverseColormap(on); });
        addCheck("SpecColorBar", "Colour bar", true,
                 [st](bool on) { st->spec->SetShowColorBar(on); });
        addCheck("SpecLogScale", "Logarithmic colour scale (for linear mode)", false,
                 [st](bool on) {
                     st->spec->SetScale(on ? HeatmapScale::Logarithmic : HeatmapScale::Linear);
                 });

        // The chart takes the width the panel leaves; its height stays.
        auto chartCard = DemoKit::AddCard(row, "SpecChartCard", "", "", 420);
        DemoKit::AddStretched(chartCard, st->spec);

        // ----- Info line: live readout of the most recent transform -----
        st->info = DemoKit::AddText(root, "SpecInfo", "");
        st->info->SetFontSize(11);
        st->info->SetTextColor(Color(100, 110, 125, 255));
        RefreshInfo(st);

        return root;
    }

} // namespace UltraCanvas
