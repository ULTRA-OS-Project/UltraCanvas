// Apps/DemoApp/UltraCanvasHeatmapExamples.cpp
// Comprehensive heatmap demonstration. Each variant lives on its own tab so it
// gets the full display area: an interactive heatmap driven by a panel of
// dropdowns / checkboxes / sliders that exercises every option, the spectrogram,
// calendar and hexbin variants, plus a "Job Gains & Losses" tab that recreates a
// NYT-style dot-density chart purely with the heatmap API.
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): the tabs in a card
//                  that fills the page area, the charts as wide as their tab, the
//                  option panel and every description sized to its text
// Version: 2.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasCalendarHeatmap.h"
#include "Plugins/Charts/UltraCanvasHexbinChart.h"
#include "Plugins/Charts/UltraCanvasSpectrogram.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"

#include <random>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>
#include <limits>

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

// ===== "JOB GAINS & LOSSES" RECREATION (NYT-style dot-density chart) =====
// Demonstrates that the dot-density "winners and losers" chart can be built
// purely with the heatmap engine: one grid column per month, sectors stacked
// above/below a zero baseline as circular cells, coloured by their monthly
// percentage change through a diverging red->beige->green->blue ramp. Empty grid
// slots are left transparent (NaN), which yields the irregular column heights.
    static std::shared_ptr<UltraCanvasHeatmapChartElement> BuildJobGainsLosses(const std::string& id) {
        const int COLS   = 108;             // Jan 2007 .. Dec 2015 (9 years)
        const int MAXR   = 20;              // up to 20 sectors per side
        const int ROWS   = 2 * MAXR + 1;    // 41 rows, zero baseline in the middle
        const int center = MAXR;
        const double NaNv = std::numeric_limits<double>::quiet_NaN();

        std::vector<double> v(COLS * ROWS, NaNv);

        std::mt19937 rng(2024);
        std::normal_distribution<double> noise(0.0, 0.45);

        // Smooth recession dip centred on early 2009 (~month 24), a few months wide.
        auto recession = [](int m) {
            double d = (m - 24.0) / 8.0;
            return std::exp(-0.5 * d * d);
        };

        for (int m = 0; m < COLS; ++m) {
            double sentiment = 0.45 - 1.55 * recession(m);   // strongly negative in the slump
            const int nSectors = 16;
            std::vector<double> pos, neg;
            for (int k = 0; k < nSectors; ++k) {
                double change = sentiment + noise(rng);      // monthly % change for this sector
                if (change >= 0) pos.push_back(change);
                else             neg.push_back(change);
            }
            std::sort(pos.begin(), pos.end());                        // smallest gain nearest the baseline
            std::sort(neg.begin(), neg.end(), std::greater<double>()); // smallest loss nearest the baseline

            int up = std::min(static_cast<int>(pos.size()), MAXR);
            int dn = std::min(static_cast<int>(neg.size()), MAXR);
            for (int i = 0; i < up; ++i) v[(center - 1 - i) * COLS + m] = pos[i]; // sectors rising
            for (int i = 0; i < dn; ++i) v[(center + 1 + i) * COLS + m] = neg[i]; // sectors falling
        }

        auto hm = CreateHeatmapChartElement(id, 0, 0, 940, 480);
        hm->SetData(v, COLS, ROWS);

        // Diverging colour scale resembling the NYT red -> beige -> green -> blue ramp.
        hm->SetCustomColormap({
            Color(165, 0, 38),    Color(215, 48, 39),   Color(244, 109, 67),
            Color(253, 174, 97),  Color(255, 255, 191),                       // middle ~ 0 %
            Color(217, 239, 139), Color(166, 217, 106),
            Color(102, 189, 99),  Color(44, 123, 182),
        });
        hm->SetValueRange(-1.6, 1.6);
        hm->SetDiverging(true, 0.0);              // centre 0 % on the middle of the ramp
        hm->SetCellShape(HeatmapCellShape::Circle);
        hm->SetCellGap(0.18);
        hm->SetRenderMode(HeatmapRenderMode::Cells);
        hm->SetNaNColor(Colors::Transparent);     // empty grid slots disappear
        hm->SetShowColorBar(true);
        hm->SetShowRowLabels(false);

        // Year labels along the axis (only at each January).
        std::vector<std::string> cols(COLS, "");
        for (int m = 0; m < COLS; m += 12) cols[m] = std::to_string(2007 + m / 12);
        hm->SetColumnLabels(cols);
        hm->SetAxisTitles("", "sectors falling   <   0   >   sectors rising");
        return hm;
    }

// ===== SMALL HELPERS =====
    static std::shared_ptr<UltraCanvasLabel> MakeTabDescription(const std::string& id, const std::string& text) {
        auto lbl = DemoKit::CreateText(id, text);
        lbl->SetFontSize(11);
        lbl->SetTextColor(Color(70, 70, 80, 255));
        lbl->SetMargin(10, 12, 4, 12);   // top, right, bottom, left — inset from the tab edge
        return lbl;
    }

    // Stretch a flex child on the cross axis and give it the requested grow factor.
    static void AddFlexChild(const std::shared_ptr<UltraCanvasContainer>& parent,
                             const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    }

    // A chart takes its tab's width (whatever the page area gives) and the
    // height the description leaves.
    static void AddChart(const std::shared_ptr<UltraCanvasContainer>& parent,
                         const std::shared_ptr<UltraCanvasUIElement>& chart) {
        chart->size.width = CSSLayout::Dimension::Auto();
        AddFlexChild(parent, chart, 1);
    }

    // Build the interactive option panel (dropdowns / checkboxes / sliders) into
    // `panel`, wiring every control to the supplied heatmap.
    static void BuildHeatmapControlPanel(const std::shared_ptr<UltraCanvasContainer>& panel,
                                         const std::shared_ptr<UltraCanvasHeatmapChartElement>& hm) {
        // The panel is a column: every control is as wide as the panel and
        // every label and checkbox as tall as its text.
        const int pw = 272;
        const int ctrlH = 26;

        DemoKit::AddHeading(panel, "HmCtrlHeading", "Options");

        auto addLabel = [&](const std::string& id, const std::string& text) {
            auto l = DemoKit::CreateText(id, text);
            l->SetFontSize(11);
            l->SetFontWeight(FontWeight::Bold);
            l->SetMargin(6, 0, 0, 0);
            DemoKit::Add(panel, l);
        };

        // Preset (dataset) dropdown
        addLabel("HmPresetLbl", "Dataset preset:");
        auto presetDd = std::make_shared<UltraCanvasDropdown>("HmPreset", 0, 0, pw, ctrlH);
        for (const char* n : {"Gradient", "Random", "Peaks", "Correlation matrix", "Checkerboard"})
            presetDd->AddItem(n);
        presetDd->SetSelectedIndex(2);   // Peaks (matches the heatmap's preconfigured dataset)
        presetDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            ApplyPreset(hm, index);
        };
        DemoKit::AddStretched(panel, presetDd);

        // Colour map dropdown
        addLabel("HmCmapLbl", "Colour map:");
        auto cmapDd = std::make_shared<UltraCanvasDropdown>("HmCmap", 0, 0, pw, ctrlH);
        for (const auto& m : ColormapMenu()) cmapDd->AddItem(m.first);
        cmapDd->SetSelectedIndex(1);   // Inferno
        cmapDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            const auto& menu = ColormapMenu();
            if (index >= 0 && index < static_cast<int>(menu.size()))
                hm->SetColormap(menu[index].second);
        };
        DemoKit::AddStretched(panel, cmapDd);

        // Render mode dropdown
        addLabel("HmModeLbl", "Render mode:");
        auto modeDd = std::make_shared<UltraCanvasDropdown>("HmMode", 0, 0, pw, ctrlH);
        for (const char* n : {"Auto", "Cells", "Image"}) modeDd->AddItem(n);
        modeDd->SetSelectedIndex(0);
        modeDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            hm->SetRenderMode(index == 1 ? HeatmapRenderMode::Cells
                              : index == 2 ? HeatmapRenderMode::Image
                                           : HeatmapRenderMode::Auto);
        };
        DemoKit::AddStretched(panel, modeDd);

        // Cell shape dropdown
        addLabel("HmShapeLbl", "Cell shape:");
        auto shapeDd = std::make_shared<UltraCanvasDropdown>("HmShape", 0, 0, pw, ctrlH);
        for (const char* n : {"Rectangle", "Rounded rectangle", "Circle"}) shapeDd->AddItem(n);
        shapeDd->SetSelectedIndex(0);
        shapeDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            hm->SetCellShape(index == 1 ? HeatmapCellShape::RoundedRectangle
                             : index == 2 ? HeatmapCellShape::Circle
                                          : HeatmapCellShape::Rectangle);
        };
        DemoKit::AddStretched(panel, shapeDd);

        // Scale dropdown
        addLabel("HmScaleLbl", "Value scale:");
        auto scaleDd = std::make_shared<UltraCanvasDropdown>("HmScale", 0, 0, pw, ctrlH);
        for (const char* n : {"Linear", "Logarithmic"}) scaleDd->AddItem(n);
        scaleDd->SetSelectedIndex(1);   // Logarithmic
        scaleDd->onSelectionChanged = [hm](int index, const DropdownItem&) {
            hm->SetScale(index == 1 ? HeatmapScale::Logarithmic : HeatmapScale::Linear);
        };
        DemoKit::AddStretched(panel, scaleDd);

        // Triangular mask dropdown
        addLabel("HmMaskLbl", "Triangular mask:");
        auto maskDd = std::make_shared<UltraCanvasDropdown>("HmMask", 0, 0, pw, ctrlH);
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
        DemoKit::AddStretched(panel, maskDd);

        // Checkboxes
        auto addCheck = [&](const std::string& id, const std::string& text, bool initial,
                            std::function<void(bool)> apply) {
            auto cb = std::make_shared<UltraCanvasCheckbox>(id, text);
            cb->SetChecked(initial);
            cb->onStateChanged = [apply](CheckedState, CheckedState ns) {
                apply(ns == CheckedState::Checked);
            };
            DemoKit::Add(panel, cb);
        };

        addCheck("HmReverse", "Reverse colour map", false, [hm](bool on) { hm->SetReverseColormap(on); });
        addCheck("HmDiverging", "Diverging (midpoint 0)", false, [hm](bool on) { hm->SetDiverging(on, 0.0); });
        addCheck("HmBorders", "Cell borders", false,
                 [hm](bool on) { hm->SetShowCellBorders(on, Color(255, 255, 255, 255), 1.0f); });
        addCheck("HmValues", "Cell values", false, [hm](bool on) { hm->SetShowCellValues(on); });
        addCheck("HmColorBar", "Colour bar", true, [hm](bool on) { hm->SetShowColorBar(on); });

        // Sliders
        addLabel("HmLevelsLbl", "Discrete colour levels (0 = continuous):");
        auto levelsSlider = std::make_shared<UltraCanvasSlider>("HmLevels", 0, 0, pw, ctrlH);
        levelsSlider->SetRange(0.0, 10.0);
        levelsSlider->SetStep(1.0);
        levelsSlider->SetValue(0.0);
        levelsSlider->onValueChanged = [hm](double v) { hm->SetColorLevels(static_cast<int>(v)); };
        DemoKit::AddStretched(panel, levelsSlider);

        addLabel("HmGapLbl", "Cell gap:");
        auto gapSlider = std::make_shared<UltraCanvasSlider>("HmGap", 0, 0, pw, ctrlH);
        gapSlider->SetRange(0.0, 0.5);
        gapSlider->SetStep(0.05);
        gapSlider->SetValue(0.0);
        gapSlider->onValueChanged = [hm](double v) { hm->SetCellGap(v); };
        DemoKit::AddStretched(panel, gapSlider);
    }

// ===== MAIN CREATOR =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateHeatmapExamples() {
        auto page = DemoKit::CreatePage("HeatmapRoot",
                "Heatmap Charts - Interactive Options & Variants",
                "Each variant has its own tab so it can use the full area. "
                "Tip: on the Interactive tab pick the Correlation preset, then enable "
                "Diverging + an RdBu colour map + a triangular mask.");
        // The page is the display area's height and the tabs take what the
        // title and description leave; each chart follows its tab's size.
        page->SetFillsArea(true);

        auto card = DemoKit::CreateCard("HeatmapTabsCard");
        card->SetPadding(4);
        DemoKit::Add(page, card, 1);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("HeatmapTabs", 0, 0, 0, 0);
        tabs->SetTabStyle(TabStyle::Rounded);
        tabs->SetTabHeight(30);
        tabs->SetTabCornerRadius(8.0f);
        tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

        // The size each tab and chart is built with; the tabbed container
        // arranges the active tab into its content area.
        const int INNER_W = 1140;
        const int INNER_H = 660;

        // ---- Tab 1: Interactive Heatmap (control panel + large heatmap) ----
        auto tab1 = std::make_shared<UltraCanvasContainer>("HmTabInteractive", 0, 0, INNER_W, INNER_H);
        tab1->SetBackgroundColor(Color(255, 255, 255, 255));
        tab1->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // As wide as its widest option (300 px at least) and the tab's height;
        // it scrolls when the options are taller than the tab.
        auto panel = std::make_shared<UltraCanvasContainer>("HmPanel");
        panel->SetBackgroundColor(Color(247, 247, 250, 255));
        panel->SetPadding(12, 14, 12, 14);
        panel->layout.SetFlexColumn().SetFlexGap(4)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        {
            ContainerStyle panelStyle;
            panelStyle.autoShowScrollbars = true;
            panelStyle.autoShowHorizontalScrollbar = false;
            panel->SetContainerStyle(panelStyle);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(300);
            panel->boxConstraints = limits;
        }

        auto hm = CreateHeatmapChartElement("HmMain", 0, 0, 760, INNER_H);
        hm->SetColormap(HeatmapColormap::Inferno);
        hm->SetShowColorBar(true);
        ApplyPreset(hm, 2);                          // Peaks dataset
        hm->SetCellShape(HeatmapCellShape::Rectangle);
        hm->SetScale(HeatmapScale::Logarithmic);     // after ApplyPreset, which forces Linear

        BuildHeatmapControlPanel(panel, hm);

        AddFlexChild(tab1, panel, 0);   // as wide as its options
        AddFlexChild(tab1, hm, 1);      // heatmap fills the remaining width

        // ---- Tab 2: Spectrogram (STFT) ----
        auto tab2 = std::make_shared<UltraCanvasContainer>("HmTabSpec", 0, 0, INNER_W, INNER_H);
        tab2->SetBackgroundColor(Color(255, 255, 255, 255));
        tab2->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab2, MakeTabDescription("HmSpecDesc",
                     "Short-Time Fourier Transform of a synthetic signal (two steady tones + a rising chirp). "
                     "The same heatmap engine renders each STFT frame as a column."), 0);
        auto spec = CreateSpectrogramElement("HmSpec", 0, 0, INNER_W, INNER_H - 50);
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
        AddChart(tab2, spec);

        // ---- Tab 3: Calendar Heatmap ----
        auto tab3 = std::make_shared<UltraCanvasContainer>("HmTabCal", 0, 0, INNER_W, INNER_H);
        tab3->SetBackgroundColor(Color(255, 255, 255, 255));
        tab3->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab3, MakeTabDescription("HmCalDesc",
                     "Calendar (contribution-graph) heatmap: a full year of synthetic daily activity, "
                     "one cell per day arranged by weekday and week."), 0);
        auto cal = CreateCalendarHeatmapElement("HmCal", 0, 0, INNER_W, INNER_H - 50);
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
        AddChart(tab3, cal);

        // ---- Tab 4: Hexbin Density ----
        auto tab4 = std::make_shared<UltraCanvasContainer>("HmTabHex", 0, 0, INNER_W, INNER_H);
        tab4->SetBackgroundColor(Color(255, 255, 255, 255));
        tab4->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab4, MakeTabDescription("HmHexDesc",
                     "Hexbin density: 6000 clustered points binned into a hexagonal lattice, "
                     "each bin coloured by point count."), 0);
        auto hex = CreateHexbinChartElement("HmHex", 0, 0, INNER_W, INNER_H - 50);
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
        AddChart(tab4, hex);

        // ---- Tab 5: "Job Gains & Losses" recreation ----
        auto tab5 = std::make_shared<UltraCanvasContainer>("HmTabJobs", 0, 0, INNER_W, INNER_H);
        tab5->SetBackgroundColor(Color(255, 255, 255, 255));
        tab5->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab5, MakeTabDescription("HmJobsDesc",
                     "Winners & Losers (NYT-style): proof that a dot-density \"sectors rising / falling\" "
                     "chart is just a heatmap. One column per month (2007-2015); each sector is a circular "
                     "cell stacked above/below the zero baseline, coloured by its % change through a diverging "
                     "red -> beige -> green -> blue ramp. Empty grid slots are transparent (NaN), so the "
                     "columns take their natural irregular heights, with the 2008-09 slump showing as a red mass below zero."), 0);
        auto jobs = BuildJobGainsLosses("HmJobs");
        AddChart(tab5, jobs);

        tabs->AddTab("Interactive Heatmap", tab1);
        tabs->AddTab("Spectrogram (STFT)", tab2);
        tabs->AddTab("Calendar Heatmap", tab3);
        tabs->AddTab("Hexbin Density", tab4);
        tabs->AddTab("Job Gains & Losses", tab5);
        tabs->SetActiveTab(0);

        DemoKit::Add(card, tabs, 1);
        return page;
    }

} // namespace UltraCanvas
