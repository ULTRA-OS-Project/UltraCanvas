// Apps/DemoApp/UltraCanvasHeatmapExamples.cpp
// Comprehensive heatmap demonstration: an interactive heatmap driven by a panel
// of dropdowns / checkboxes / sliders that exercise every option, plus showcases
// of the spectrogram, calendar and hexbin variants.
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasCalendarHeatmap.h"
#include "Plugins/Charts/UltraCanvasHexbinChart.h"
#include "Plugins/Charts/UltraCanvasSpectrogram.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"

#include <random>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// ===== COLOUR-MAP MENU (keeps dropdown order <-> enum mapping in one place) =====
    static const std::vector<std::pair<std::string, HeatmapColormap>>& ColormapMenu() {
        static const std::vector<std::pair<std::string, HeatmapColormap>> menu = {
            {"Viridis",   HeatmapColormap::Viridis},
            {"Inferno",   HeatmapColormap::Inferno},
            {"Magma",     HeatmapColormap::Magma},
            {"Plasma",    HeatmapColormap::Plasma},
            {"Cividis",   HeatmapColormap::Cividis},
            {"Turbo",     HeatmapColormap::Turbo},
            {"Grayscale", HeatmapColormap::Grayscale},
            {"Jet",       HeatmapColormap::Jet},
            {"Hot",       HeatmapColormap::Hot},
            {"Cool",      HeatmapColormap::Cool},
            {"Blues",     HeatmapColormap::Blues},
            {"Greens",    HeatmapColormap::Greens},
            {"Reds",      HeatmapColormap::Reds},
            {"Oranges",   HeatmapColormap::Oranges},
            {"Spectral",  HeatmapColormap::Spectral},
            {"RdBu",      HeatmapColormap::RdBu},
            {"RdYlBu",    HeatmapColormap::RdYlBu},
            {"RdYlGn",    HeatmapColormap::RdYlGn},
            {"Coolwarm",  HeatmapColormap::Coolwarm},
        };
        return menu;
    }

// ===== SAMPLE DATA PRESETS FOR THE MAIN INTERACTIVE HEATMAP =====
    static void ApplyPreset(const std::shared_ptr<UltraCanvasHeatmapChartElement>& hm, int presetIndex) {
        std::mt19937 rng(12345);
        std::uniform_real_distribution<double> uni(0.0, 1.0);

        hm->SetScale(HeatmapScale::Linear);

        switch (presetIndex) {
            case 1: { // Random
                int cols = 30, rows = 20;
                std::vector<double> v(cols * rows);
                for (auto& x : v) x = uni(rng) * 100.0;
                hm->SetData(v, cols, rows);
                break;
            }
            case 2: { // Peaks (sum of 2D gaussians)
                int cols = 48, rows = 32;
                std::vector<double> v(cols * rows, 0.0);
                struct Peak { double cx, cy, amp, sigma; };
                Peak peaks[] = {
                    {0.30, 0.35, 1.0, 0.12}, {0.70, 0.55, 0.8, 0.16}, {0.50, 0.80, 0.6, 0.10}
                };
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        double nx = (c + 0.5) / cols, ny = (r + 0.5) / rows;
                        double s = 0.0;
                        for (auto& p : peaks) {
                            double dx = nx - p.cx, dy = ny - p.cy;
                            s += p.amp * std::exp(-(dx * dx + dy * dy) / (2 * p.sigma * p.sigma));
                        }
                        v[r * cols + c] = s;
                    }
                }
                hm->SetData(v, cols, rows);
                break;
            }
            case 3: { // Correlation matrix (symmetric, [-1, 1], diagonal 1) -> try Diverging + a mask
                int n = 12;
                std::vector<double> base(n);
                for (auto& b : base) b = uni(rng) * 2.0 - 1.0;
                std::vector<double> v(n * n);
                for (int r = 0; r < n; ++r) {
                    for (int c = 0; c < n; ++c) {
                        double corr = (r == c) ? 1.0 : (0.4 * base[r] * base[c] + 0.6 * (uni(rng) * 2 - 1));
                        corr = std::max(-1.0, std::min(1.0, corr));
                        v[r * n + c] = (r == c) ? 1.0 : corr;
                    }
                }
                // mirror to make it symmetric
                for (int r = 0; r < n; ++r)
                    for (int c = r + 1; c < n; ++c)
                        v[c * n + r] = v[r * n + c];
                hm->SetData(v, n, n);
                break;
            }
            case 4: { // Checkerboard
                int cols = 16, rows = 16;
                std::vector<double> v(cols * rows);
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < cols; ++c)
                        v[r * cols + c] = ((r + c) % 2 == 0) ? 1.0 : 0.0;
                hm->SetData(v, cols, rows);
                break;
            }
            case 0:
            default: { // Gradient
                int cols = 24, rows = 16;
                std::vector<double> v(cols * rows);
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < cols; ++c)
                        v[r * cols + c] = static_cast<double>(c + r);
                hm->SetData(v, cols, rows);
                break;
            }
        }
    }

// ===== SMALL HELPERS =====
    static std::shared_ptr<UltraCanvasLabel> MakeHeading(const std::string& id, const std::string& text,
                                                         int x, int y, int w, int fontSize = 13) {
        auto lbl = std::make_shared<UltraCanvasLabel>(id, x, y, w, 22);
        lbl->SetText(text);
        lbl->SetFontSize(fontSize);
        lbl->SetFontWeight(FontWeight::Bold);
        return lbl;
    }

// ===== MAIN CREATOR =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateHeatmapExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("HeatmapContainer", 0, 0, 1200, 820);

        // --- Title + description ---
        auto title = std::make_shared<UltraCanvasLabel>("HmTitle", 20, 10, 1160, 32);
        title->SetText("Heatmap Charts - Interactive Options & Variants");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetAlignment(TextAlignment::Center);
        title->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(title);

        auto desc = std::make_shared<UltraCanvasLabel>("HmDesc", 20, 48, 1160, 40);
        desc->SetText("Use the panel on the left to explore every heatmap option live. Below: the spectrogram, "
                      "calendar and hexbin variants built on the same engine. (Tip: pick the Correlation preset, "
                      "then enable Diverging + an RdBu colour map + a triangular mask.)");
        desc->SetFontSize(11);
        desc->SetWrap(TextWrap::WrapWord);
        container->AddChild(desc);

        // --- The main interactive heatmap ---
        container->AddChild(MakeHeading("HmMainTitle", "Interactive Heatmap", 310, 96, 400));
        auto hm = CreateHeatmapChartElement("HmMain", 310, 120, 560, 360);
        hm->SetColormap(HeatmapColormap::Viridis);
        hm->SetShowColorBar(true);
        ApplyPreset(hm, 0);
        container->AddChild(hm);

        // =====================================================================
        // CONTROL PANEL (left)
        // =====================================================================
        const int px = 20, pw = 270;
        int y = 96;
        const int ctrlH = 26;

        container->AddChild(MakeHeading("HmCtrlHeading", "Options", px, y, pw, 14));
        y += 26;

        auto addLabel = [&](const std::string& id, const std::string& text) {
            auto l = std::make_shared<UltraCanvasLabel>(id, px, y, pw, 18);
            l->SetText(text);
            l->SetFontSize(11);
            l->SetFontWeight(FontWeight::Bold);
            container->AddChild(l);
            y += 19;
        };

        // Preset (dataset) dropdown
        addLabel("HmPresetLbl", "Dataset preset:");
        auto presetDd = std::make_shared<UltraCanvasDropdown>("HmPreset", px, y, pw, ctrlH);
        for (const char* n : {"Gradient", "Random", "Peaks", "Correlation matrix", "Checkerboard"})
            presetDd->AddItem(n);
        presetDd->SetSelectedIndex(0);
        presetDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            ApplyPreset(hm, index);
        };
        container->AddChild(presetDd);
        y += ctrlH + 8;

        // Colour map dropdown
        addLabel("HmCmapLbl", "Colour map:");
        auto cmapDd = std::make_shared<UltraCanvasDropdown>("HmCmap", px, y, pw, ctrlH);
        for (const auto& m : ColormapMenu()) cmapDd->AddItem(m.first);
        cmapDd->SetSelectedIndex(0);
        cmapDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            const auto& menu = ColormapMenu();
            if (index >= 0 && index < static_cast<int>(menu.size()))
                hm->SetColormap(menu[index].second);
        };
        container->AddChild(cmapDd);
        y += ctrlH + 8;

        // Render mode dropdown
        addLabel("HmModeLbl", "Render mode:");
        auto modeDd = std::make_shared<UltraCanvasDropdown>("HmMode", px, y, pw, ctrlH);
        for (const char* n : {"Auto", "Cells", "Image"}) modeDd->AddItem(n);
        modeDd->SetSelectedIndex(0);
        modeDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            hm->SetRenderMode(index == 1 ? HeatmapRenderMode::Cells
                              : index == 2 ? HeatmapRenderMode::Image
                                           : HeatmapRenderMode::Auto);
        };
        container->AddChild(modeDd);
        y += ctrlH + 8;

        // Cell shape dropdown
        addLabel("HmShapeLbl", "Cell shape:");
        auto shapeDd = std::make_shared<UltraCanvasDropdown>("HmShape", px, y, pw, ctrlH);
        for (const char* n : {"Rectangle", "Rounded rectangle", "Circle"}) shapeDd->AddItem(n);
        shapeDd->SetSelectedIndex(0);
        shapeDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            hm->SetCellShape(index == 1 ? HeatmapCellShape::RoundedRectangle
                             : index == 2 ? HeatmapCellShape::Circle
                                          : HeatmapCellShape::Rectangle);
        };
        container->AddChild(shapeDd);
        y += ctrlH + 8;

        // Scale dropdown
        addLabel("HmScaleLbl", "Value scale:");
        auto scaleDd = std::make_shared<UltraCanvasDropdown>("HmScale", px, y, pw, ctrlH);
        for (const char* n : {"Linear", "Logarithmic"}) scaleDd->AddItem(n);
        scaleDd->SetSelectedIndex(0);
        scaleDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            hm->SetScale(index == 1 ? HeatmapScale::Logarithmic : HeatmapScale::Linear);
        };
        container->AddChild(scaleDd);
        y += ctrlH + 8;

        // Triangular mask dropdown
        addLabel("HmMaskLbl", "Triangular mask:");
        auto maskDd = std::make_shared<UltraCanvasDropdown>("HmMask", px, y, pw, ctrlH);
        for (const char* n : {"None", "Lower", "Lower (no diagonal)", "Upper", "Upper (no diagonal)"})
            maskDd->AddItem(n);
        maskDd->SetSelectedIndex(0);
        maskDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            HeatmapTriangularMask m = HeatmapTriangularMask::NoMask;
            switch (index) {
                case 1: m = HeatmapTriangularMask::Lower; break;
                case 2: m = HeatmapTriangularMask::LowerNoDiagonal; break;
                case 3: m = HeatmapTriangularMask::Upper; break;
                case 4: m = HeatmapTriangularMask::UpperNoDiagonal; break;
                default: break;
            }
            hm->SetTriangularMask(m);
        };
        container->AddChild(maskDd);
        y += ctrlH + 10;

        // Checkboxes
        auto addCheck = [&](const std::string& id, const std::string& text, bool initial,
                            std::function<void(bool)> apply) {
            auto cb = std::make_shared<UltraCanvasCheckbox>(id, px, y, pw, 22);
            cb->SetText(text);
            cb->SetChecked(initial);
            cb->onStateChanged = [apply](CheckedState, CheckedState ns) {
                apply(ns == CheckedState::Checked);
            };
            container->AddChild(cb);
            y += 24;
        };

        addCheck("HmReverse", "Reverse colour map", false, [hm](bool on) { hm->SetReverseColormap(on); });
        addCheck("HmDiverging", "Diverging (midpoint 0)", false, [hm](bool on) { hm->SetDiverging(on, 0.0); });
        addCheck("HmBorders", "Cell borders", false,
                 [hm](bool on) { hm->SetShowCellBorders(on, Color(255, 255, 255, 255), 1.0f); });
        addCheck("HmValues", "Cell values", false, [hm](bool on) { hm->SetShowCellValues(on); });
        addCheck("HmColorBar", "Colour bar", true, [hm](bool on) { hm->SetShowColorBar(on); });
        y += 6;

        // Sliders
        addLabel("HmLevelsLbl", "Discrete colour levels (0 = continuous):");
        auto levelsSlider = std::make_shared<UltraCanvasSlider>("HmLevels", px, y, pw, ctrlH);
        levelsSlider->SetRange(0.0, 10.0);
        levelsSlider->SetStep(1.0);
        levelsSlider->SetValue(0.0);
        levelsSlider->onValueChanged = [hm](double v) { hm->SetColorLevels(static_cast<int>(v)); };
        container->AddChild(levelsSlider);
        y += ctrlH + 8;

        addLabel("HmGapLbl", "Cell gap:");
        auto gapSlider = std::make_shared<UltraCanvasSlider>("HmGap", px, y, pw, ctrlH);
        gapSlider->SetRange(0.0, 0.5);
        gapSlider->SetStep(0.05);
        gapSlider->SetValue(0.0);
        gapSlider->onValueChanged = [hm](double v) { hm->SetCellGap(v); };
        container->AddChild(gapSlider);
        y += ctrlH + 8;

        // =====================================================================
        // VARIANT SHOWCASES
        // =====================================================================

        // Spectrogram (top right) - synthetic signal (two tones + a chirp)
        container->AddChild(MakeHeading("HmSpecTitle", "Spectrogram (STFT)", 890, 96, 300));
        auto spec = CreateSpectrogramElement("HmSpec", 890, 120, 290, 360);
        {
            const double sr = 8000.0;
            const int N = 24000; // 3 s
            std::vector<float> sig(N);
            for (int i = 0; i < N; ++i) {
                double t = i / sr;
                double chirp = std::sin(2 * M_PI * (300.0 + 500.0 * t) * t);
                double tone1 = 0.5 * std::sin(2 * M_PI * 1200.0 * t);
                double tone2 = 0.3 * std::sin(2 * M_PI * 2600.0 * t);
                sig[i] = static_cast<float>(0.5 * chirp + tone1 + tone2);
            }
            spec->SetSignal(sig, sr);
            spec->SetFFTSize(512);
            spec->SetHopSize(128);
        }
        container->AddChild(spec);

        // Calendar heatmap (bottom, wide) - a year of synthetic activity
        container->AddChild(MakeHeading("HmCalTitle", "Calendar heatmap (contribution graph)", 310, 496, 600));
        auto cal = CreateCalendarHeatmapElement("HmCal", 310, 520, 560, 200);
        {
            std::mt19937 rng(7);
            std::uniform_real_distribution<double> uni(0.0, 1.0);
            std::vector<double> daily(365);
            for (int i = 0; i < 365; ++i) {
                double burst = (uni(rng) < 0.15) ? uni(rng) * 10.0 : 0.0;
                daily[i] = std::round(uni(rng) * 3.0 + burst);
            }
            cal->SetDailyValues(2023, 1, 1, daily);
        }
        container->AddChild(cal);

        // Hexbin (bottom right) - binned clustered points
        container->AddChild(MakeHeading("HmHexTitle", "Hexbin density", 890, 496, 290));
        auto hex = CreateHexbinChartElement("HmHex", 890, 520, 290, 200);
        {
            std::mt19937 rng(99);
            std::normal_distribution<double> g1x(35, 12), g1y(40, 10);
            std::normal_distribution<double> g2x(70, 10), g2y(65, 12);
            std::vector<Point2Dd> pts;
            pts.reserve(6000);
            for (int i = 0; i < 3000; ++i) pts.push_back(Point2Dd(g1x(rng), g1y(rng)));
            for (int i = 0; i < 3000; ++i) pts.push_back(Point2Dd(g2x(rng), g2y(rng)));
            hex->SetBinnedData(pts, 26, 18, 0, 100, 0, 100);
            hex->SetColormap(HeatmapColormap::Turbo);
        }
        container->AddChild(hex);

        return container;
    }

} // namespace UltraCanvas
