// Apps/DemoApp/UltraCanvasAudioAnalysisExamples.cpp
// Audio analysis with the generic chart elements: a spectrum plot (averaged FFT
// magnitude as a gradient area chart), an amplitude envelope (RMS over time),
// and a correlogram (normalized autocorrelation vs lag as a bar chart). Each
// lives on its own tab with a selectable signal source, showing that the
// standard line / area / bar charts cover audio-derived data with no dedicated
// chart type needed.
// Version: 1.0.0
// Last Modified: 2026-07-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include "Plugins/Charts/UltraCanvasSTFT.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
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

    namespace {

// =============================================================================
// SIGNALS
// =============================================================================

    constexpr double kAnaRate = 16000.0;
    constexpr double kAnaSeconds = 4.0;
    constexpr double kAnaTrackSeconds = 8.0;

    // 0 = sample track (fallback: chirp+tones), 1 = chirp+tones, 2 = A4 tone
    // with harmonics, 3 = white noise.
    void MakeAnalysisSignal(int index, const std::vector<float>& trackMono,
                            double trackRate, std::vector<float>& out, double& rate) {
        if (index == 0 && !trackMono.empty()) {
            out = trackMono;
            rate = trackRate;
            return;
        }
        rate = kAnaRate;
        const int n = static_cast<int>(kAnaRate * kAnaSeconds);
        out.resize(n);
        switch (index) {
            case 2:   // 220 Hz fundamental + harmonics: clear pitch for the correlogram
                for (int i = 0; i < n; ++i) {
                    double t = i / kAnaRate;
                    out[i] = static_cast<float>(
                            0.55 * std::sin(2 * M_PI * 220.0 * t) +
                            0.25 * std::sin(2 * M_PI * 440.0 * t) +
                            0.12 * std::sin(2 * M_PI * 660.0 * t) +
                            0.06 * std::sin(2 * M_PI * 880.0 * t));
                }
                break;
            case 3: { // white noise: flat spectrum, near-zero autocorrelation
                std::mt19937 rng(7);
                std::uniform_real_distribution<double> uni(-1.0, 1.0);
                for (auto& s : out) s = static_cast<float>(0.7 * uni(rng));
                break;
            }
            default:  // rising chirp + two steady tones
                for (int i = 0; i < n; ++i) {
                    double t = i / kAnaRate;
                    double chirp = std::sin(2 * M_PI * (400.0 + 700.0 * t) * t);
                    out[i] = static_cast<float>(0.27 * chirp +
                            0.30 * std::sin(2 * M_PI * 2000.0 * t) +
                            0.18 * std::sin(2 * M_PI * 5200.0 * t));
                }
                break;
        }
    }

    std::string FreqLabel(double hz) {
        std::ostringstream oss;
        if (hz >= 1000.0) {
            oss.setf(std::ios::fixed);
            oss.precision(hz >= 10000.0 ? 0 : 1);
            oss << hz / 1000.0 << " kHz";
        } else {
            oss << static_cast<int>(hz) << " Hz";
        }
        return oss.str();
    }

// =============================================================================
// ANALYSES (signal -> ChartDataVector)
// =============================================================================

    // Average magnitude spectrum in dBFS: Hann-windowed STFT, mean magnitude per
    // bin across all frames, one chart point per bin (decimated to <= maxPoints).
    std::shared_ptr<ChartDataVector> BuildSpectrumData(
            const std::vector<float>& sig, double rate, int fftSize, int maxPoints = 512) {
        auto data = std::make_shared<ChartDataVector>();
        SpectrogramParams params;
        params.fftSize = fftSize;
        params.hopSize = fftSize / 2;
        params.window = SpectrogramWindow::Hann;
        params.magnitude = SpectrogramMagnitude::Linear;   // averaged, then converted
        SpectrogramResult r;
        if (!ComputeSpectrogram(sig.data(), sig.size(), rate, params, r) ||
            r.frames == 0 || r.bins == 0)
            return data;

        std::vector<double> avg(r.bins, 0.0);
        for (int b = 0; b < r.bins; ++b) {
            double sum = 0.0;
            for (int f = 0; f < r.frames; ++f) sum += r.magnitudes[b * r.frames + f];
            avg[b] = sum / r.frames;
        }
        double peak = 1e-12;
        for (double v : avg) peak = std::max(peak, v);

        const int step = std::max(1, r.bins / maxPoints);
        const double binHz = rate / 2.0 / std::max(1, r.bins - 1);
        // Label roughly every kHz-ish gridline (about 10 labels across the axis).
        const int labelEvery = std::max(1, r.bins / step / 10);

        std::vector<ChartDataPoint> pts;
        int idx = 0;
        for (int b = 0; b < r.bins; b += step, ++idx) {
            double db = 20.0 * std::log10(std::max(avg[b] / peak, 1e-6));   // >= -120 dB
            double hz = b * binHz;
            std::string label = (idx % labelEvery == 0) ? FreqLabel(hz) : "";
            pts.emplace_back(hz, db, 0.0, label, db);
        }
        data->LoadFromArray(pts);
        return data;
    }

    // RMS amplitude envelope: `blocks` equal time slices, y = block RMS [0, 1].
    std::shared_ptr<ChartDataVector> BuildEnvelopeData(
            const std::vector<float>& sig, double rate, int blocks = 240) {
        auto data = std::make_shared<ChartDataVector>();
        if (sig.empty()) return data;
        blocks = std::min<int>(blocks, static_cast<int>(sig.size()));
        const size_t perBlock = std::max<size_t>(1, sig.size() / blocks);
        const int labelEvery = std::max(1, blocks / 8);

        std::vector<ChartDataPoint> pts;
        for (int b = 0; b < blocks; ++b) {
            size_t start = static_cast<size_t>(b) * perBlock;
            size_t end = (b == blocks - 1) ? sig.size()
                                           : std::min(sig.size(), start + perBlock);
            double sumSq = 0.0;
            for (size_t i = start; i < end; ++i)
                sumSq += static_cast<double>(sig[i]) * sig[i];
            double rms = end > start ? std::sqrt(sumSq / (end - start)) : 0.0;
            double tSec = start / rate;
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(1);
            oss << tSec << " s";
            pts.emplace_back(tSec, rms, 0.0, (b % labelEvery == 0) ? oss.str() : "", rms);
        }
        data->LoadFromArray(pts);
        return data;
    }

    // Correlogram: normalized autocorrelation r(lag)/r(0) for lags up to
    // `maxLagMs`, decimated to <= maxBars bars. Computed over the first second
    // so the sample track stays fast.
    std::shared_ptr<ChartDataVector> BuildCorrelogramData(
            const std::vector<float>& sig, double rate,
            double maxLagMs = 20.0, int maxBars = 160) {
        auto data = std::make_shared<ChartDataVector>();
        if (sig.empty()) return data;
        const size_t n = std::min(sig.size(), static_cast<size_t>(rate));
        const size_t maxLag = std::min(n - 1,
                static_cast<size_t>(maxLagMs / 1000.0 * rate));
        if (maxLag == 0) return data;

        double r0 = 1e-12;
        for (size_t i = 0; i < n; ++i)
            r0 += static_cast<double>(sig[i]) * sig[i];

        const size_t step = std::max<size_t>(1, maxLag / maxBars);
        const int labelEvery = static_cast<int>(std::max<size_t>(1, maxLag / step / 8));

        std::vector<ChartDataPoint> pts;
        int idx = 0;
        for (size_t lag = 0; lag <= maxLag; lag += step, ++idx) {
            double sum = 0.0;
            for (size_t i = 0; i + lag < n; ++i)
                sum += static_cast<double>(sig[i]) * sig[i + lag];
            double r = sum / r0;
            double ms = lag / rate * 1000.0;
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(1);
            oss << ms;
            pts.emplace_back(ms, r, 0.0, (idx % labelEvery == 0) ? oss.str() : "", r);
        }
        data->LoadFromArray(pts);
        return data;
    }

    } // namespace

// =============================================================================
// DEMO PAGE
// =============================================================================
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateAudioAnalysisExamples() {
        const int CONTAINER_W = 1180;
        const int CONTAINER_H = 800;
        const int PAD = 16;

        auto root = std::make_shared<UltraCanvasContainer>("AnaRoot", 0, 0, CONTAINER_W, CONTAINER_H);
        root->SetBackgroundColor(Color(248, 249, 252, 255));
        root->SetPadding(PAD);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto addFlex = [](const std::shared_ptr<UltraCanvasContainer>& parent,
                          const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
            child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            parent->AddChild(child);
        };

        auto title = std::make_shared<UltraCanvasLabel>("AnaTitle", 0, 0, 0, 30);
        title->SetText("Audio Analysis Charts - Spectrum, Envelope & Correlogram");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(30, 41, 59, 255));
        addFlex(root, title, 0);

        auto subtitle = std::make_shared<UltraCanvasLabel>("AnaSubtitle", 0, 0, 0, 34);
        subtitle->SetText("Three classic audio-derived plots built entirely with the generic "
                          "chart elements: an averaged FFT magnitude spectrum (area chart), an "
                          "RMS amplitude envelope over time (area chart), and a normalized "
                          "autocorrelation correlogram (bar chart). Pick a signal per tab.");
        subtitle->SetFontSize(11);
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetTextColor(Color(100, 110, 125, 255));
        addFlex(root, subtitle, 0);

        // ----- Decode the bundled sample track once (shared by all tabs) -----
        auto trackMono = std::make_shared<std::vector<float>>();
        auto trackRate = std::make_shared<double>(0.0);
        const std::string audioPath = NormalizePath(
                GetResourcesDir() + "media/audios/the_mountain-cinematic-489998.mp3");
        if (auto audio = UCAudio::LoadFromFile(audioPath)) {
            if (audio->IsValid()) {
                *trackMono = AudioToMonoFloat(*audio);
                *trackRate = audio->GetSampleRate();
                const size_t maxSamples =
                        static_cast<size_t>(*trackRate * kAnaTrackSeconds);
                if (trackMono->size() > maxSamples) trackMono->resize(maxSamples);
            }
        }
        const std::string trackItem = trackMono->empty()
                ? "Sample track (unavailable)" : "Sample track (first 8 s)";
        const std::vector<std::string> signalItems = {
                trackItem, "Chirp + steady tones", "220 Hz tone + harmonics", "White noise"};
        const int defaultSignal = trackMono->empty() ? 1 : 0;

        // ----- Tabs -----
        auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
                "AnaTabs", 0, 0, CONTAINER_W - 2 * PAD, 680);
        tabs->SetTabStyle(TabStyle::Rounded);
        tabs->SetTabHeight(30);
        tabs->SetTabCornerRadius(8.0f);

        const int INNER_W = CONTAINER_W - 2 * PAD - 8;
        const int INNER_H = 640;

        auto makeTab = [&](const std::string& id, const std::string& descText,
                           const std::shared_ptr<UltraCanvasUIElement>& chart,
                           std::function<void(int)> onSignal) {
            auto tab = std::make_shared<UltraCanvasContainer>(id, 0, 0, INNER_W, INNER_H);
            tab->SetBackgroundColor(Color(255, 255, 255, 255));
            tab->layout.SetFlexColumn().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

            auto desc = std::make_shared<UltraCanvasLabel>(id + "Desc", 0, 0, 0, 34);
            desc->SetText(descText);
            desc->SetFontSize(11);
            desc->SetWrap(TextWrap::WrapWord);
            desc->SetTextColor(Color(100, 110, 125, 255));
            desc->SetMargin(10, 12, 0, 12);
            addFlex(tab, desc, 0);

            // Signal picker row.
            auto pickRow = std::make_shared<UltraCanvasContainer>(id + "Pick", 0, 0, INNER_W, 30);
            pickRow->layout.SetFlexRow().SetFlexGap(8)
                           .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            pickRow->size.height = CSSLayout::Dimension::Px(30);
            auto pickLbl = std::make_shared<UltraCanvasLabel>(id + "PickLbl", 0, 0, 60, 18);
            pickLbl->SetText("Signal:");
            pickLbl->SetFontSize(11);
            pickLbl->SetMargin(0, 0, 0, 12);
            pickLbl->size.width = CSSLayout::Dimension::Px(60);
            pickRow->AddChild(pickLbl);
            auto pickDd = std::make_shared<UltraCanvasDropdown>(id + "PickDd", 0, 0, 240, 26);
            for (const auto& it : signalItems) pickDd->AddItem(it);
            pickDd->SetSelectedIndex(defaultSignal);
            pickDd->onSelectionChanged = [onSignal](int index, const DropdownItem&) {
                onSignal(index);
            };
            pickDd->size.width = CSSLayout::Dimension::Px(240);
            pickRow->AddChild(pickDd);
            addFlex(tab, pickRow, 0);

            addFlex(tab, chart, 1);
            return tab;
        };

        // Regenerates the selected tab signal (shared by the three pickers).
        auto signalFor = [trackMono, trackRate](int index, std::vector<float>& sig, double& rate) {
            MakeAnalysisSignal(index, *trackMono, *trackRate, sig, rate);
        };

        // ---- Tab 1: Spectrum plot ----
        auto spectrum = CreateAreaChartElement("AnaSpectrum", 0, 0, INNER_W, INNER_H - 80);
        spectrum->SetChartTitle("Averaged magnitude spectrum (dB, 0 = peak bin)");
        spectrum->SetLineColor(Color(79, 70, 229, 255));
        spectrum->SetLineWidth(1.5);
        spectrum->SetFillGradientEnabled(true);
        spectrum->SetGradientColors(Color(99, 102, 241, 190), Color(99, 102, 241, 25));
        spectrum->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
        spectrum->SetShowValueLabels(false);   // hundreds of bins - values via tooltip
        spectrum->SetEnableTooltips(true);
        {
            std::vector<float> sig; double rate = kAnaRate;
            signalFor(defaultSignal, sig, rate);
            spectrum->SetDataSource(BuildSpectrumData(sig, rate, 2048));
        }
        auto tab1 = makeTab("AnaTabSpec",
                "Frequency content of the whole signal: Hann-windowed STFT frames averaged "
                "per bin, normalized to the loudest bin and shown in dB. Steady tones appear "
                "as sharp peaks, the chirp as a raised plateau, noise as a flat floor.",
                spectrum,
                [spectrum, signalFor](int index) {
                    std::vector<float> sig; double rate = kAnaRate;
                    signalFor(index, sig, rate);
                    spectrum->SetDataSource(BuildSpectrumData(sig, rate, 2048));
                });

        // ---- Tab 2: Amplitude envelope ----
        auto envelope = CreateAreaChartElement("AnaEnvelope", 0, 0, INNER_W, INNER_H - 80);
        envelope->SetChartTitle("RMS amplitude envelope");
        envelope->SetLineColor(Color(13, 148, 136, 255));
        envelope->SetLineWidth(2.0);
        envelope->SetSmoothingEnabled(true);
        envelope->SetFillGradientEnabled(true);
        envelope->SetGradientColors(Color(20, 184, 166, 190), Color(20, 184, 166, 25));
        envelope->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
        envelope->SetShowValueLabels(false);   // ~240 blocks - values via tooltip
        envelope->SetEnableTooltips(true);
        {
            std::vector<float> sig; double rate = kAnaRate;
            signalFor(defaultSignal, sig, rate);
            envelope->SetDataSource(BuildEnvelopeData(sig, rate));
        }
        auto tab2 = makeTab("AnaTabEnv",
                "Loudness over time: the signal is sliced into equal blocks and each block's "
                "RMS level becomes one point — the same reduction the waveform element uses "
                "for its RMS band, here as a standalone smoothed area chart.",
                envelope,
                [envelope, signalFor](int index) {
                    std::vector<float> sig; double rate = kAnaRate;
                    signalFor(index, sig, rate);
                    envelope->SetDataSource(BuildEnvelopeData(sig, rate));
                });

        // ---- Tab 3: Correlogram ----
        auto correlogram = CreateBarChartElement("AnaCorr", 0, 0, INNER_W, INNER_H - 80);
        correlogram->SetChartTitle("Normalized autocorrelation vs lag (ms)");
        correlogram->SetBarColor(Color(217, 119, 6, 220));
        correlogram->SetBarBorderColor(Color(180, 83, 9, 255));
        correlogram->SetBarBorderWidth(0.5);
        correlogram->SetBarSpacing(0.25);
        correlogram->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
        correlogram->SetShowValueLabels(false);   // up to 160 bars - values via tooltip
        correlogram->SetEnableTooltips(true);
        {
            std::vector<float> sig; double rate = kAnaRate;
            signalFor(defaultSignal, sig, rate);
            correlogram->SetDataSource(BuildCorrelogramData(sig, rate));
        }
        auto tab3 = makeTab("AnaTabCorr",
                "Self-similarity of the signal at increasing lags, normalized so lag 0 = 1. "
                "A pitched signal shows repeating peaks at multiples of its period (the 220 Hz "
                "preset peaks every ~4.5 ms); white noise collapses to zero immediately.",
                correlogram,
                [correlogram, signalFor](int index) {
                    std::vector<float> sig; double rate = kAnaRate;
                    signalFor(index, sig, rate);
                    correlogram->SetDataSource(BuildCorrelogramData(sig, rate));
                });

        tabs->AddTab("Spectrum Plot", tab1);
        tabs->AddTab("Amplitude Envelope", tab2);
        tabs->AddTab("Correlogram", tab3);
        tabs->SetActiveTab(0);
        addFlex(root, tabs, 1);

        return root;
    }

} // namespace UltraCanvas
