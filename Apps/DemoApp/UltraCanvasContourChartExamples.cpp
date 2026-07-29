// Apps/DemoApp/UltraCanvasContourChartExamples.cpp
// Contour chart demonstration. One tab per presentation style: an interactive
// tab that exercises every option, then recreations of the classic contour
// forms - filled contours with inline labels, a kernel-density contour of a
// scatter, a line-only contour, a dark-theme density plot, and the two 3D
// surface variants (banded/categorical and smooth terrain with isolines).
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasContourChart.h"
#include "Plugins/Charts/UltraCanvasContourSurface3D.h"
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
#include <cstdio>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// SHARED HELPERS
// =============================================================================

    static const std::vector<std::pair<std::string, HeatmapColormap>>& ContourColormapMenu() {
        static const std::vector<std::pair<std::string, HeatmapColormap>> menu = {
                {"Jet",       HeatmapColormap::Jet},
                {"Viridis",   HeatmapColormap::Viridis},
                {"Inferno",   HeatmapColormap::Inferno},
                {"Magma",     HeatmapColormap::Magma},
                {"Plasma",    HeatmapColormap::Plasma},
                {"Turbo",     HeatmapColormap::Turbo},
                {"Cividis",   HeatmapColormap::Cividis},
                {"Blues",     HeatmapColormap::Blues},
                {"Greens",    HeatmapColormap::Greens},
                {"Reds",      HeatmapColormap::Reds},
                {"Spectral",  HeatmapColormap::Spectral},
                {"RdBu",      HeatmapColormap::RdBu},
                {"Coolwarm",  HeatmapColormap::Coolwarm},
                {"Grayscale", HeatmapColormap::Grayscale},
        };
        return menu;
    }

    static std::shared_ptr<UltraCanvasLabel> MakeContourTabDescription(const std::string& id,
                                                                       const std::string& text) {
        auto lbl = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 44);
        lbl->SetText(text);
        lbl->SetFontSize(11);
        lbl->SetWrap(TextWrap::WrapWord);
        lbl->SetTextColor(Color(70, 70, 80, 255));
        lbl->SetMargin(10, 12, 4, 12);
        return lbl;
    }

    static void AddContourFlexChild(const std::shared_ptr<UltraCanvasContainer>& parent,
                                    const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    }

// ===== SMOOTH VALUE NOISE (for the terrain / random-field datasets) =====

    static double NoiseHash(int x, int y, uint32_t seed) {
        uint32_t h = seed;
        h ^= static_cast<uint32_t>(x) * 374761393u;
        h ^= static_cast<uint32_t>(y) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= (h >> 16);
        return (h / 4294967295.0) * 2.0 - 1.0;   // [-1, 1]
    }

    static double ValueNoise(double x, double y, uint32_t seed) {
        int xi = static_cast<int>(std::floor(x));
        int yi = static_cast<int>(std::floor(y));
        double tx = x - xi, ty = y - yi;
        // Smoothstep keeps the octaves free of lattice creases.
        double sx = tx * tx * (3.0 - 2.0 * tx);
        double sy = ty * ty * (3.0 - 2.0 * ty);

        double n00 = NoiseHash(xi,     yi,     seed);
        double n10 = NoiseHash(xi + 1, yi,     seed);
        double n01 = NoiseHash(xi,     yi + 1, seed);
        double n11 = NoiseHash(xi + 1, yi + 1, seed);

        double a = n00 + (n10 - n00) * sx;
        double b = n01 + (n11 - n01) * sx;
        return a + (b - a) * sy;
    }

    static double FractalNoise(double x, double y, uint32_t seed, int octaves) {
        double sum = 0.0, amp = 1.0, freq = 1.0, norm = 0.0;
        for (int o = 0; o < octaves; ++o) {
            sum += ValueNoise(x * freq, y * freq, seed + o * 7919u) * amp;
            norm += amp;
            amp *= 0.5;
            freq *= 2.0;
        }
        return (norm > 0.0) ? sum / norm : 0.0;
    }

// ===== DATASETS =====

    // A smooth, lumpy field in roughly [-1, 1] over [-4, 4]^2 - the classic
    // "random surface" that filled contour demos use.
    static std::vector<double> MakeLumpyField(int cols, int rows, uint32_t seed) {
        std::vector<double> v(static_cast<size_t>(cols) * rows);
        for (int r = 0; r < rows; ++r) {
            double y = -4.0 + 8.0 * r / (rows - 1);
            for (int c = 0; c < cols; ++c) {
                double x = -4.0 + 8.0 * c / (cols - 1);
                double n = FractalNoise((x + 4.0) * 0.34, (y + 4.0) * 0.34, seed, 3);
                v[static_cast<size_t>(r) * cols + c] = std::clamp(n * 1.5, -1.0, 1.0);
            }
        }
        return v;
    }

    // Two opposed Gaussian basins - the field behind the "My Plot" line contour.
    static std::vector<double> MakeTwinPeaksField(int cols, int rows) {
        std::vector<double> v(static_cast<size_t>(cols) * rows);
        for (int r = 0; r < rows; ++r) {
            double y = -2.0 + 4.0 * r / (rows - 1);
            for (int c = 0; c < cols; ++c) {
                double x = -2.0 + 5.0 * c / (cols - 1);
                double up = 1.8 * std::exp(-((x - 1.1) * (x - 1.1) + (y - 0.85) * (y - 0.85)) / 0.85);
                double down = -1.5 * std::exp(-((x + 0.6) * (x + 0.6) + (y + 0.7) * (y + 0.7)) / 1.1);
                v[static_cast<size_t>(r) * cols + c] = up + down;
            }
        }
        return v;
    }

    // A two-cluster point cloud for the kernel-density tabs.
    static std::vector<ContourPoint> MakeClusteredPoints(int count, uint32_t seed) {
        std::mt19937 rng(seed);
        std::normal_distribution<double> gx(-0.35, 0.62), gy(0.15, 0.55);
        std::normal_distribution<double> hx(0.75, 0.42), hy(-0.55, 0.40);
        std::uniform_real_distribution<double> pick(0.0, 1.0);

        std::vector<ContourPoint> pts;
        pts.reserve(count);
        for (int i = 0; i < count; ++i) {
            if (pick(rng) < 0.68) pts.emplace_back(gx(rng), gy(rng), 1.0);
            else                  pts.emplace_back(hx(rng), hy(rng), 1.0);
        }
        return pts;
    }

    // Population vs. median income, skewed the way real census data is: most
    // states bunched at low population, income centred near $42K.
    static std::vector<ContourPoint> MakeCensusPoints(int count, uint32_t seed) {
        std::mt19937 rng(seed);
        std::lognormal_distribution<double> pop(14.9, 0.95);      // people
        std::normal_distribution<double> income(42500.0, 6800.0); // dollars

        std::vector<ContourPoint> pts;
        pts.reserve(count);
        for (int i = 0; i < count; ++i) {
            double p = std::min(pop(rng), 3.4e7);
            double inc = std::clamp(income(rng) + p * 1.9e-4, 26000.0, 65000.0);
            pts.emplace_back(p, inc, 1.0);
        }
        return pts;
    }

// =============================================================================
// INTERACTIVE CONTROL PANEL
// =============================================================================

    static void BuildContourControlPanel(const std::shared_ptr<UltraCanvasContainer>& panel,
                                         const std::shared_ptr<UltraCanvasContourChartElement>& ct) {
        const int px = 14, pw = 268;
        const int ctrlH = 26;
        int y = 12;

        auto heading = std::make_shared<UltraCanvasLabel>("CtCtrlHeading", px, y, pw, 22);
        heading->SetText("Options");
        heading->SetFontSize(14);
        heading->SetFontWeight(FontWeight::Bold);
        panel->AddChild(heading);
        y += 28;

        auto addLabel = [&](const std::string& id, const std::string& text) {
            auto l = std::make_shared<UltraCanvasLabel>(id, px, y, pw, 18);
            l->SetText(text);
            l->SetFontSize(11);
            l->SetFontWeight(FontWeight::Bold);
            panel->AddChild(l);
            y += 19;
        };
        auto addCheck = [&](const std::string& id, const std::string& text, bool initial,
                            std::function<void(bool)> apply) {
            auto cb = std::make_shared<UltraCanvasCheckbox>(id, px, y, pw, 22);
            cb->SetText(text);
            cb->SetChecked(initial);
            cb->onStateChanged = [apply](CheckedState, CheckedState ns) {
                apply(ns == CheckedState::Checked);
            };
            panel->AddChild(cb);
            y += 24;
        };

        // ---- render mode ----
        addLabel("CtModeLbl", "Render mode:");
        auto modeDd = std::make_shared<UltraCanvasDropdown>("CtMode", px, y, pw, ctrlH);
        for (const char* n : {"Filled + lines", "Filled", "Lines only",
                              "Smooth + lines", "Heatmap + lines"}) {
            modeDd->AddItem(n);
        }
        modeDd->SetSelectedIndex(0);
        modeDd->onSelectionChanged = [ct](int index, const DropdownItem&) {
            ContourRenderMode m = ContourRenderMode::FilledWithLines;
            switch (index) {
                case 1: m = ContourRenderMode::Filled; break;
                case 2: m = ContourRenderMode::Lines; break;
                case 3: m = ContourRenderMode::Smooth; break;
                case 4: m = ContourRenderMode::HeatmapWithContours; break;
                default: break;
            }
            ct->SetRenderMode(m);
        };
        panel->AddChild(modeDd);
        y += ctrlH + 8;

        // ---- colour map ----
        addLabel("CtCmapLbl", "Colour map:");
        auto cmapDd = std::make_shared<UltraCanvasDropdown>("CtCmap", px, y, pw, ctrlH);
        for (const auto& m : ContourColormapMenu()) cmapDd->AddItem(m.first);
        cmapDd->SetSelectedIndex(0);   // Jet
        cmapDd->onSelectionChanged = [ct](int index, const DropdownItem&) {
            const auto& menu = ContourColormapMenu();
            if (index >= 0 && index < static_cast<int>(menu.size())) {
                ct->SetColormap(menu[index].second);
            }
        };
        panel->AddChild(cmapDd);
        y += ctrlH + 8;

        // ---- legend ----
        addLabel("CtLegendLbl", "Legend:");
        auto legendDd = std::make_shared<UltraCanvasDropdown>("CtLegend", px, y, pw, ctrlH);
        for (const char* n : {"Colour bar", "Bands (vertical)", "Bands (horizontal)", "None"}) {
            legendDd->AddItem(n);
        }
        legendDd->SetSelectedIndex(0);
        legendDd->onSelectionChanged = [ct](int index, const DropdownItem&) {
            ContourLegendMode m = ContourLegendMode::ColorBar;
            switch (index) {
                case 1: m = ContourLegendMode::DiscreteBands; break;
                case 2: m = ContourLegendMode::DiscreteBandsHorizontal; break;
                case 3: m = ContourLegendMode::NoLegend; break;
                default: break;
            }
            ct->SetLegendMode(m);
        };
        panel->AddChild(legendDd);
        y += ctrlH + 8;

        // ---- level count ----
        addLabel("CtLevelsLbl", "Contour levels:");
        auto levelSlider = std::make_shared<UltraCanvasSlider>("CtLevels", px, y, pw, ctrlH);
        levelSlider->SetRange(2.0, 24.0);
        levelSlider->SetStep(1.0);
        levelSlider->SetValue(10.0);
        levelSlider->onValueChanged = [ct](double v) { ct->SetLevelCount(static_cast<int>(v)); };
        panel->AddChild(levelSlider);
        y += ctrlH + 8;

        // ---- field smoothing ----
        addLabel("CtSmoothLbl", "Field smoothing (sigma):");
        auto smoothSlider = std::make_shared<UltraCanvasSlider>("CtSmooth", px, y, pw, ctrlH);
        smoothSlider->SetRange(0.0, 4.0);
        smoothSlider->SetStep(0.25);
        smoothSlider->SetValue(0.0);
        smoothSlider->onValueChanged = [ct](double v) { ct->SetGridSmoothing(v); };
        panel->AddChild(smoothSlider);
        y += ctrlH + 8;

        // ---- fill alpha ----
        addLabel("CtAlphaLbl", "Fill opacity:");
        auto alphaSlider = std::make_shared<UltraCanvasSlider>("CtAlpha", px, y, pw, ctrlH);
        alphaSlider->SetRange(0.15, 1.0);
        alphaSlider->SetStep(0.05);
        alphaSlider->SetValue(1.0);
        alphaSlider->onValueChanged = [ct](double v) { ct->SetFillAlpha(v); };
        panel->AddChild(alphaSlider);
        y += ctrlH + 10;

        // ---- toggles ----
        addCheck("CtLabels", "Inline contour labels", true,
                 [ct](bool on) { ct->SetShowLineLabels(on); });
        addCheck("CtBreak", "Break line under label", true,
                 [ct](bool on) { ct->SetLabelBreaksLine(on); });
        addCheck("CtHalo", "Label halo plate", false,
                 [ct](bool on) { ct->SetLabelHalo(on); });
        addCheck("CtDashed", "Dash negative levels", true,
                 [ct](bool on) { ct->SetNegativeLevelsDashed(on); });
        addCheck("CtLevelColor", "Colour lines by level", false,
                 [ct](bool on) {
                     ct->SetLineColorMode(on ? ContourLineColorMode::FromLevel
                                             : ContourLineColorMode::Fixed);
                 });
        addCheck("CtMajor", "Emphasise every 3rd contour", false,
                 [ct](bool on) { ct->SetMajorLineEvery(on ? 3 : 0, 2.2f); });
        addCheck("CtZero", "Highlight the zero level", false,
                 [ct](bool on) { ct->SetHighlightZeroLevel(on, Color(0, 0, 0, 255), 2.4f); });
        addCheck("CtNice", "Snap levels to nice numbers", true,
                 [ct](bool on) { ct->SetNiceLevels(on); });
        addCheck("CtLineSmooth", "Smooth contour polylines", false,
                 [ct](bool on) { ct->SetLineSmoothing(on ? 2 : 0); });
        addCheck("CtTicks", "Axis ticks", true,
                 [ct](bool on) { ct->SetShowAxisTicks(on); });
    }

// =============================================================================
// MAIN CREATOR
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateContourChartExamples() {
        const int CONTAINER_W = 1180;
        const int CONTAINER_H = 800;
        const int PAD = 16;

        auto root = std::make_shared<UltraCanvasContainer>("ContourRoot", 0, 0, CONTAINER_W, CONTAINER_H);
        root->SetBackgroundColor(Color(250, 250, 252, 255));
        root->SetPadding(PAD);
        root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto title = std::make_shared<UltraCanvasLabel>("CtTitle", 0, 0, 0, 30);
        title->SetText("Contour Charts - Isolines, Filled Bands, Density & 3D Surfaces");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 50, 120, 255));
        AddContourFlexChild(root, title, 0);

        auto desc = std::make_shared<UltraCanvasLabel>("CtDesc", 0, 0, 0, 22);
        desc->SetText("A contour chart draws curves of equal value through a scalar field. "
                      "Feed it a grid, or a raw point cloud it turns into a density field. "
                      "Drag with the mouse on the 3D tabs to orbit; the wheel zooms.");
        desc->SetFontSize(11);
        desc->SetWrap(TextWrap::WrapWord);
        desc->SetTextColor(Color(90, 90, 100, 255));
        AddContourFlexChild(root, desc, 0);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("ContourTabs", 0, 0,
                                                                 CONTAINER_W - 2 * PAD, 700);
        tabs->SetTabStyle(TabStyle::Rounded);
        tabs->SetTabHeight(30);
        tabs->SetTabCornerRadius(8.0f);

        const int INNER_W = CONTAINER_W - 2 * PAD - 8;
        const int INNER_H = 660;

        // =====================================================================
        // Tab 1: Interactive
        // =====================================================================
        auto tab1 = std::make_shared<UltraCanvasContainer>("CtTabInteractive", 0, 0, INNER_W, INNER_H);
        tab1->SetBackgroundColor(Color(255, 255, 255, 255));
        tab1->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto panel = std::make_shared<UltraCanvasContainer>("CtPanel", 0, 0, 296, INNER_H);
        panel->SetBackgroundColor(Color(247, 247, 250, 255));
        panel->size.width = CSSLayout::Dimension::Px(296);

        auto ctInteractive = CreateContourChartElement("CtMain", 0, 0, 760, INNER_H);
        {
            const int N = 64;
            ctInteractive->SetData(MakeLumpyField(N, N, 20260729u), N, N);
            ctInteractive->SetDataRange(-4.0, 4.0, -4.0, 4.0);
            ctInteractive->SetColormap(HeatmapColormap::Jet);
            ctInteractive->SetRenderMode(ContourRenderMode::FilledWithLines);
            ctInteractive->SetLevelCount(10);
            ctInteractive->SetShowLineLabels(true);
            ctInteractive->SetLabelDecimals(1);
            ctInteractive->SetNegativeLevelsDashed(true);
            ctInteractive->SetChartTitle("Interactive contour field");
            ctInteractive->SetAxisTitles("x", "y");
        }
        BuildContourControlPanel(panel, ctInteractive);

        AddContourFlexChild(tab1, panel, 0);
        AddContourFlexChild(tab1, ctInteractive, 1);

        // =====================================================================
        // Tab 2: Filled contour with labelled isolines
        // =====================================================================
        auto tab2 = std::make_shared<UltraCanvasContainer>("CtTabFilled", 0, 0, INNER_W, INNER_H);
        tab2->SetBackgroundColor(Color(255, 255, 255, 255));
        tab2->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddContourFlexChild(tab2, MakeContourTabDescription("CtFilledDesc",
                "Filled contour bands with black isolines stroked on top and the level value set "
                "inline along each curve - the line is broken underneath the text. The colour bar "
                "is centred on zero via the diverging normalisation."), 0);

        auto ctFilled = CreateContourChartElement("CtFilled", 0, 0, INNER_W, INNER_H - 60);
        {
            const int N = 56;
            ctFilled->SetData(MakeLumpyField(N, N, 424242u), N, N);
            ctFilled->SetDataRange(-4.0, 4.0, -4.0, 4.0);
            ctFilled->SetValueRange(-1.0, 1.0);
            ctFilled->SetColormap(HeatmapColormap::Jet);
            ctFilled->SetDiverging(true, 0.0);
            ctFilled->SetRenderMode(ContourRenderMode::FilledWithLines);
            ctFilled->SetLevelStep(0.2, 0.0);
            ctFilled->SetLineColor(Color(25, 25, 25, 255));
            ctFilled->SetLineWidth(1.0f);
            ctFilled->SetShowLineLabels(true);
            ctFilled->SetLabelDecimals(1);
            ctFilled->SetLabelBreaksLine(true);
            ctFilled->SetMinLabelLength(60.0);
            ctFilled->SetLegendMode(ContourLegendMode::ColorBar);
            ctFilled->SetLegendDecimals(1);
            ctFilled->SetTickCounts(9, 9);
            ctFilled->SetAxisTitles("x", "y");
        }
        AddContourFlexChild(tab2, ctFilled, 1);

        // =====================================================================
        // Tab 3: Kernel-density contour with a discrete band legend
        // =====================================================================
        auto tab3 = std::make_shared<UltraCanvasContainer>("CtTabDensity", 0, 0, INNER_W, INNER_H);
        tab3->SetBackgroundColor(Color(255, 255, 255, 255));
        tab3->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddContourFlexChild(tab3, MakeContourTabDescription("CtDensityDesc",
                "The points themselves are not drawn: 4000 samples from two overlapping clusters are "
                "turned into a kernel density estimate, contoured at a fixed step, and keyed by a "
                "discrete legend that names each band's interval."), 0);

        auto ctDensity = CreateContourChartElement("CtDensity", 0, 0, INNER_W, INNER_H - 60);
        {
            ctDensity->SetGriddingMethod(ContourGridding::KDE);
            ctDensity->SetGridResolution(128, 128);
            ctDensity->SetKDEBandwidth(0.0);              // Scott's rule
            ctDensity->SetPoints(MakeClusteredPoints(4000, 991));
            ctDensity->SetColormap(HeatmapColormap::Blues);
            ctDensity->SetRenderMode(ContourRenderMode::Filled);
            ctDensity->SetLevelStep(0.02, 0.0);
            ctDensity->SetLegendMode(ContourLegendMode::DiscreteBands);
            ctDensity->SetLegendTitle("level");
            ctDensity->SetIntervalFormat(ContourIntervalFormat::Brackets);
            ctDensity->SetLegendDecimals(2);
            ctDensity->SetAxisTitles("x", "y");
            ctDensity->SetTickCounts(7, 7);
        }
        AddContourFlexChild(tab3, ctDensity, 1);

        // =====================================================================
        // Tab 4: Line-only contour, each line coloured by its level
        // =====================================================================
        auto tab4 = std::make_shared<UltraCanvasContainer>("CtTabLines", 0, 0, INNER_W, INNER_H);
        tab4->SetBackgroundColor(Color(255, 255, 255, 255));
        tab4->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddContourFlexChild(tab4, MakeContourTabDescription("CtLinesDesc",
                "No fill at all: every isoline takes its own colour from the diverging ramp and is "
                "labelled with its value. Negative levels are dashed, which is the usual convention "
                "for reading a peak from a basin at a glance."), 0);

        auto ctLines = CreateContourChartElement("CtLines", 0, 0, INNER_W, INNER_H - 60);
        {
            const int N = 72;
            ctLines->SetData(MakeTwinPeaksField(N, N), N, N);
            ctLines->SetDataRange(-2.0, 3.0, -2.0, 2.0);
            ctLines->SetColormap(HeatmapColormap::Coolwarm);
            ctLines->SetDiverging(true, 0.0);
            ctLines->SetRenderMode(ContourRenderMode::Lines);
            ctLines->SetLevelStep(0.3, 0.0);
            ctLines->SetLineColorMode(ContourLineColorMode::FromLevel);
            ctLines->SetLineWidth(1.4f);
            ctLines->SetNegativeLevelsDashed(true);
            ctLines->SetShowLineLabels(true);
            ctLines->SetLabelDecimals(3);
            ctLines->SetLabelBreaksLine(true);
            ctLines->SetMinLabelLength(70.0);
            ctLines->SetLegendMode(ContourLegendMode::ColorBar);
            ctLines->SetLegendDecimals(1);
            ctLines->SetChartTitle("My Plot");
            ctLines->SetAxisTitles("X1", "X2");
            ctLines->SetTickCounts(6, 5);
        }
        AddContourFlexChild(tab4, ctLines, 1);

        // =====================================================================
        // Tab 5: Dark-theme density contour over its own scatter
        // =====================================================================
        auto tab5 = std::make_shared<UltraCanvasContainer>("CtTabDark", 0, 0, INNER_W, INNER_H);
        tab5->SetBackgroundColor(Color(255, 255, 255, 255));
        tab5->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddContourFlexChild(tab5, MakeContourTabDescription("CtDarkDesc",
                "The same density pipeline on a dark theme, with the source points faintly overlaid "
                "and axis tick formatters turning raw numbers into $27.50K / 10.00M. Heavier "
                "smoothing gives the soft, glowing bands."), 0);

        auto ctDark = CreateContourChartElement("CtDark", 0, 0, INNER_W, INNER_H - 60);
        {
            ctDark->SetGriddingMethod(ContourGridding::KDE);
            ctDark->SetGridResolution(140, 110);
            ctDark->SetPoints(MakeCensusPoints(3000, 5150));
            ctDark->SetColormap(HeatmapColormap::Blues);
            ctDark->SetRenderMode(ContourRenderMode::Filled);
            ctDark->SetLevelCount(9);
            ctDark->SetGridSmoothing(1.6);
            ctDark->SetLegendMode(ContourLegendMode::NoLegend);

            // Dark theme.
            ctDark->SetBackgroundColor(Color(8, 8, 10, 255));
            ctDark->SetTitleColor(Color(235, 235, 240, 255));
            ctDark->SetLabelColor(Color(190, 195, 205, 255));
            ctDark->SetFrameColor(Color(70, 72, 80, 255));
            ctDark->SetNaNColor(Color(8, 8, 10, 255));
            ctDark->SetShowSourcePoints(true);
            ctDark->SetSourcePointStyle(Color(210, 225, 255, 70), 1.4f);

            ctDark->SetXTickFormatter([](double v) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2fM", v / 1.0e6);
                return std::string(buf);
            });
            ctDark->SetYTickFormatter([](double v) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "$%.2fK", v / 1.0e3);
                return std::string(buf);
            });
            ctDark->SetAxisTitles("Population", "Median Income");
            ctDark->SetTickCounts(5, 6);
        }
        AddContourFlexChild(tab5, ctDark, 1);

        // =====================================================================
        // Tab 6: 3D banded contour surface over categorical axes
        // =====================================================================
        auto tab6 = std::make_shared<UltraCanvasContainer>("CtTab3DBands", 0, 0, INNER_W, INNER_H);
        tab6->SetBackgroundColor(Color(255, 255, 255, 255));
        tab6->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddContourFlexChild(tab6, MakeContourTabDescription("Ct3DBandsDesc",
                "The spreadsheet-style 3D contour: a coarse grid over two categorical axes, the "
                "surface flat-shaded by z-band with the bands named in the legend below. Drag to "
                "orbit, wheel to zoom."), 0);

        auto surfBands = CreateContourSurface3DElement("CtSurfBands", 0, 0, INNER_W, INNER_H - 60);
        {
            // 5 products x 4 regions, values 0..100.
            const int cols = 5, rows = 4;
            std::vector<double> v = {
                    35, 62, 48, 90, 74,
                    58, 41, 88, 52, 96,
                    22, 79, 35, 68, 44,
                    64, 30, 72, 38, 85,
            };
            surfBands->SetData(v, cols, rows);
            surfBands->SetValueRange(0.0, 100.0);
            surfBands->SetLevelStep(20.0, 0.0);
            surfBands->SetSurfaceColorMode(SurfaceColorMode::Banded);
            surfBands->SetColormap(HeatmapColormap::Turbo);
            surfBands->SetShowWireframe(true, Color(40, 40, 50, 150), 1.0f);
            surfBands->SetCategoryLabelsX({"A", "B", "C", "D", "E"});
            surfBands->SetCategoryLabelsY({"East", "North", "South", "West"});
            surfBands->SetAxisTitles("Product", "Region", "Units");
            surfBands->SetLegendMode(SurfaceLegendMode::Horizontal);
            surfBands->SetLegendDecimals(0);
            surfBands->SetChartTitle("Contour Chart");
            surfBands->SetCamera(-0.75, 0.40, 4.6);
            surfBands->SetZExaggeration(1.0);
        }
        AddContourFlexChild(tab6, surfBands, 1);

        // =====================================================================
        // Tab 7: 3D terrain with isolines drawn onto the surface
        // =====================================================================
        auto tab7 = std::make_shared<UltraCanvasContainer>("CtTab3DTerrain", 0, 0, INNER_W, INNER_H);
        tab7->SetBackgroundColor(Color(255, 255, 255, 255));
        tab7->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddContourFlexChild(tab7, MakeContourTabDescription("Ct3DTerrainDesc",
                "A smooth-shaded elevation surface with contour lines traced onto it. Each cell's "
                "isoline segments are drawn straight after that cell's quad, so the painter's-"
                "algorithm ordering occludes them exactly like the terrain itself."), 0);

        auto surfTerrain = CreateContourSurface3DElement("CtSurfTerrain", 0, 0, INNER_W, INNER_H - 60);
        {
            const int N = 72;
            std::vector<double> v(static_cast<size_t>(N) * N);
            for (int r = 0; r < N; ++r) {
                for (int c = 0; c < N; ++c) {
                    double x = c * 2.6 / (N - 1);
                    double y = r * 2.6 / (N - 1);
                    double h = FractalNoise(x, y, 31337u, 4);
                    // Push the ridges up so the peaks read clearly in relief.
                    v[static_cast<size_t>(r) * N + c] = 380.0 + 260.0 * h + 90.0 * h * h;
                }
            }
            surfTerrain->SetData(v, N, N);
            surfTerrain->SetDataRange(40.0, 40.2, 20.0, 20.35);
            surfTerrain->SetSurfaceColorMode(SurfaceColorMode::Smooth);
            surfTerrain->SetCustomColormap({
                    Color(18, 24, 130, 255),
                    Color(42, 78, 214, 255),
                    Color(112, 58, 208, 255),
                    Color(196, 52, 198, 255),
                    Color(126, 232, 158, 255),
            });
            surfTerrain->SetLevelCount(11);
            surfTerrain->SetShowSurfaceIsolines(true, Color(150, 220, 255, 170), 0.9f);
            surfTerrain->SetShowWireframe(false);
            surfTerrain->SetLighting(true, 0.5);
            surfTerrain->SetZExaggeration(1.15);
            surfTerrain->SetLegendMode(SurfaceLegendMode::NoLegend);

            // Dark theme with geographic axes.
            surfTerrain->SetBackgroundColor(Color(14, 14, 16, 255));
            surfTerrain->SetTitleColor(Color(235, 235, 240, 255));
            surfTerrain->SetAxisColors(Color(150, 210, 150, 220), Color(170, 220, 170, 230));
            surfTerrain->SetAxisTitles("Longitude", "Latitude", "Elevation / m");
            surfTerrain->SetTickCounts(3, 8, 4);
            surfTerrain->SetXTickFormatter([](double v) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1fE", v);
                return std::string(buf);
            });
            surfTerrain->SetYTickFormatter([](double v) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2fN", v);
                return std::string(buf);
            });
            surfTerrain->SetZTickFormatter([](double v) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.0f", v);
                return std::string(buf);
            });
            surfTerrain->SetCamera(-0.55, 0.30, 4.8);
        }
        AddContourFlexChild(tab7, surfTerrain, 1);

        tabs->AddTab("Interactive Contour", tab1);
        tabs->AddTab("Filled + Labels", tab2);
        tabs->AddTab("Density (KDE)", tab3);
        tabs->AddTab("Line Contour", tab4);
        tabs->AddTab("Dark Density", tab5);
        tabs->AddTab("3D Bands", tab6);
        tabs->AddTab("3D Terrain", tab7);

        AddContourFlexChild(root, tabs, 1);
        return root;
    }

} // namespace UltraCanvas
