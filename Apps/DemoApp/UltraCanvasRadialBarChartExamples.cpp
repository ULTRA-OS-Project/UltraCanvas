// Apps/DemoApp/UltraCanvasRadialBarChartExamples.cpp
// Comprehensive radial bar / radial line chart demonstration
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls in a
//                  card above the charts, one card per chart, checkboxes sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasRadialBarChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <cstdio>
#include <random>

namespace UltraCanvas {

// ===== SAMPLE DATA BUILDERS =====

    // Six sensors with 24 hourly readings each — the classic multi-sensor
    // "ray" dashboard. A fixed seed keeps the demo reproducible.
    static std::shared_ptr<UltraCanvasRadialBarChart> BuildSensorChart(
        const std::string& id, int x, int y, int w, int h)
    {
        std::mt19937 gen(42);
        std::vector<std::pair<std::string, std::vector<double>>> sensorData;

        struct SensorSpec { std::string name; double mean; double stddev; };
        const SensorSpec specs[] = {
            {TrF("Sensor {0}", "01"), 467.0, 100.0},
            {TrF("Sensor {0}", "02"), 258.0,  80.0},
            {TrF("Sensor {0}", "03"), 993.0, 200.0},
            {TrF("Sensor {0}", "04"),  50.0,  20.0},
            {TrF("Sensor {0}", "05"), 650.0, 150.0},
            {TrF("Sensor {0}", "06"), 985.0, 100.0}
        };
        for (const auto& spec : specs) {
            std::normal_distribution<> dist(spec.mean, spec.stddev);
            std::vector<double> readings;
            readings.reserve(24);
            for (int i = 0; i < 24; ++i) {
                readings.push_back(std::max(5.0, dist(gen)));
            }
            sensorData.emplace_back(spec.name, std::move(readings));
        }

        auto chart = CreateRadialBarChartFromSeries(id, x, y, w, h, sensorData);
        chart->SetColorPalette({
            Color(139,   0,   0, 255),   // Dark red
            Color(178,  34,  34, 255),   // Fire brick
            Color(220,  20,  60, 255),   // Crimson
            Color(255, 105, 180, 255),   // Hot pink
            Color(255, 182, 193, 255),   // Light pink
            Color(255, 160, 122, 255)    // Light salmon
        });
        return chart;
    }

    // Median age per country, one series per continent — radial line style.
    static std::shared_ptr<UltraCanvasRadialBarChart> BuildMedianAgeRays(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreateRadialBarChart(id, x, y, w, h);

        chart->AddSeries(Tr("North America"), {
            {Tr("United States"), 38.5}, {Tr("Canada"), 41.8}, {Tr("Mexico"), 29.2},
            {Tr("Guatemala"), 22.1}, {Tr("Costa Rica"), 32.6}, {Tr("Cuba"), 42.1},
            {Tr("Jamaica"), 31.4}, {Tr("Panama"), 29.8}
        }, Color(33, 150, 243, 255));

        chart->AddSeries(Tr("South America"), {
            {Tr("Brazil"), 33.2}, {Tr("Argentina"), 31.9}, {Tr("Chile"), 35.4},
            {Tr("Peru"), 28.0}, {Tr("Colombia"), 31.0}, {Tr("Ecuador"), 27.2},
            {Tr("Uruguay"), 35.0}, {Tr("Bolivia"), 25.4}
        }, Color(255, 193, 7, 255));

        chart->AddSeries(Tr("Europe"), {
            {Tr("Germany"), 47.8}, {Tr("France"), 41.7}, {Tr("United Kingdom"), 40.6},
            {Tr("Italy"), 46.5}, {Tr("Spain"), 44.9}, {Tr("Poland"), 41.9},
            {Tr("Netherlands"), 42.8}, {Tr("Greece"), 45.3}, {Tr("Sweden"), 41.1},
            {Tr("Ireland"), 37.8}
        }, Color(3, 169, 244, 255));

        chart->AddSeries(Tr("Africa"), {
            {Tr("Nigeria"), 18.1}, {Tr("Ethiopia"), 19.5}, {Tr("Egypt"), 24.6},
            {Tr("South Africa"), 27.3}, {Tr("Kenya"), 20.1}, {Tr("Ghana"), 21.4},
            {Tr("Morocco"), 29.3}, {Tr("Tunisia"), 32.7}, {Tr("Senegal"), 19.4},
            {Tr("Uganda"), 16.7}
        }, Color(76, 175, 80, 255));

        chart->AddSeries(Tr("Asia"), {
            {Tr("China"), 38.4}, {Tr("India"), 28.2}, {Tr("Japan"), 48.6},
            {Tr("Indonesia"), 30.2}, {Tr("Vietnam"), 32.5}, {Tr("Thailand"), 39.0},
            {Tr("South Korea"), 43.7}, {Tr("Turkey"), 32.2}, {Tr("Israel"), 30.4},
            {Tr("Singapore"), 42.2}
        }, Color(233, 30, 99, 255));

        chart->AddSeries(Tr("Oceania"), {
            {Tr("Australia"), 38.7}, {Tr("New Zealand"), 38.4}, {Tr("Fiji"), 28.9},
            {Tr("Samoa"), 21.4}, {Tr("Palau"), 34.5}
        }, Color(156, 39, 176, 255));

        return chart;
    }

    // Monthly rainfall — a small single-series fan for the partial-sweep case.
    static std::shared_ptr<UltraCanvasRadialBarChart> BuildRainfallChart(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreateRadialBarChart(id, x, y, w, h);
        chart->AddSeries(Tr("Rainfall (mm)"), {
            {Tr("Jan"), 78}, {Tr("Feb"), 64}, {Tr("Mar"), 82}, {Tr("Apr"), 110},
            {Tr("May"), 145}, {Tr("Jun"), 180}, {Tr("Jul"), 210}, {Tr("Aug"), 196},
            {Tr("Sep"), 152}, {Tr("Oct"), 118}, {Tr("Nov"), 96}, {Tr("Dec"), 84}
        }, Color(0, 122, 204, 255));
        return chart;
    }

// ===== CONTROL PANEL =====

    // The groups sit side by side and wrap under each other when narrow;
    // dropdowns and sliders keep their 180 px, checkboxes fit their text.
    static void CreateRadialBarControlPanel(
        std::shared_ptr<UltraCanvasContainer> container,
        std::vector<std::shared_ptr<UltraCanvasRadialBarChart>> charts)
    {
        const int controlWidth = 180;
        const int controlHeight = 25;

        auto groups = DemoKit::AddRow(container, "RbControlGroups", 32);
        auto addGroup = [&groups](const std::string& id, const std::string& headerId,
                                  const std::string& header) {
            auto group = DemoKit::CreateColumn(id, 8);
            DemoKit::Add(groups, group);
            auto label = DemoKit::AddText(group, headerId, header);
            label->SetFontWeight(FontWeight::Bold);
            return group;
        };

        auto styleGroup = addGroup("RbStyleGroup", "RbStyleHeader", Tr("Bar Style:"));

        auto styleDropdown = std::make_shared<UltraCanvasDropdown>("RbStyle", 0, 0, controlWidth, controlHeight);
        styleDropdown->AddItem(Tr("Bars"));
        styleDropdown->AddItem(Tr("Lines"));
        styleDropdown->SetSelectedIndex(0);
        styleDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            RadialBarStyle style = index == 1 ? RadialBarStyle::Lines
                                              : RadialBarStyle::Bars;
            for (auto& c : charts) c->SetBarStyle(style);
        };
        DemoKit::AddFixed(styleGroup, styleDropdown);

        auto capDropdown = std::make_shared<UltraCanvasDropdown>("RbCap", 0, 0, controlWidth, controlHeight);
        capDropdown->AddItem(Tr("Round Cap"));
        capDropdown->AddItem(Tr("Butt Cap"));
        capDropdown->AddItem(Tr("Square Cap"));
        capDropdown->AddItem(Tr("Arrow Cap"));
        capDropdown->SetSelectedIndex(0);
        capDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            RadialBarCapStyle cap;
            switch (index) {
                case 1:  cap = RadialBarCapStyle::Butt; break;
                case 2:  cap = RadialBarCapStyle::Square; break;
                case 3:  cap = RadialBarCapStyle::Arrow; break;
                default: cap = RadialBarCapStyle::Round; break;
            }
            for (auto& c : charts) c->SetCapStyle(cap);
        };
        DemoKit::AddFixed(styleGroup, capDropdown);

        auto gradientCheckbox = std::make_shared<UltraCanvasCheckbox>("RbGradient", Tr("Gradient Fade"));
        gradientCheckbox->SetChecked(true);
        gradientCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetGradientFade(newState == CheckedState::Checked);
        };
        DemoKit::Add(styleGroup, gradientCheckbox);

        auto scaleGroup = addGroup("RbScaleGroup", "RbScaleHeader", Tr("Value Scaling:"));

        auto normDropdown = std::make_shared<UltraCanvasDropdown>("RbNorm", 0, 0, controlWidth, controlHeight);
        normDropdown->AddItem(Tr("Global Scale"));
        normDropdown->AddItem(Tr("Per-Series Scale"));
        normDropdown->SetSelectedIndex(0);
        normDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            RadialBarNormalization mode = index == 1
                ? RadialBarNormalization::PerSeries
                : RadialBarNormalization::Global;
            for (auto& c : charts) c->SetNormalization(mode);
        };
        DemoKit::AddFixed(scaleGroup, normDropdown);

        auto holeGroup = addGroup("RbHoleGroup", "RbHoleHeader", Tr("Inner Circle Size:"));

        auto innerSlider = std::make_shared<UltraCanvasSlider>("RbInner", 0, 0, controlWidth, controlHeight);
        innerSlider->SetRange(0.1, 0.6);
        innerSlider->SetStep(0.01);
        innerSlider->SetValue(0.25);
        innerSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetInnerRadiusFraction(static_cast<float>(value));
        };
        DemoKit::AddFixed(holeGroup, innerSlider);

        auto gapHeader = DemoKit::AddText(holeGroup, "RbGapHeader", Tr("Series Gap (degrees):"));
        gapHeader->SetFontWeight(FontWeight::Bold);

        auto gapSlider = std::make_shared<UltraCanvasSlider>("RbGap", 0, 0, controlWidth, controlHeight);
        gapSlider->SetRange(0.0, 15.0);
        gapSlider->SetStep(0.5);
        gapSlider->SetValue(3.0);
        gapSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetSeriesGapAngle(static_cast<float>(value));
        };
        DemoKit::AddFixed(holeGroup, gapSlider);

        auto extrasGroup = addGroup("RbExtrasGroup", "RbExtrasHeader", Tr("Extras:"));

        auto guidesCheckbox = std::make_shared<UltraCanvasCheckbox>("RbGuides", Tr("Ring Guides"));
        guidesCheckbox->SetChecked(false);
        guidesCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetShowRingGuides(newState == CheckedState::Checked);
        };
        DemoKit::Add(extrasGroup, guidesCheckbox);

        auto seriesLabelsCheckbox = std::make_shared<UltraCanvasCheckbox>("RbSeriesLabels", Tr("Series Labels"));
        seriesLabelsCheckbox->SetChecked(true);
        seriesLabelsCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetShowSeriesLabels(newState == CheckedState::Checked);
        };
        DemoKit::Add(extrasGroup, seriesLabelsCheckbox);

        auto peakCheckbox = std::make_shared<UltraCanvasCheckbox>("RbPeaks", Tr("Peak (Max) Labels"));
        peakCheckbox->SetChecked(true);
        peakCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetShowPeakLabels(newState == CheckedState::Checked);
        };
        DemoKit::Add(extrasGroup, peakCheckbox);

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("RbHover", Tr("Hover Highlight"));
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(extrasGroup, hoverCheckbox);
    }

// ===== MAIN RADIAL BAR CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRadialBarChartExamples() {
        auto page = DemoKit::CreatePage("RadialBarChartContainer",
            Tr("Radial Bar Chart Examples - Value-Driven Rays Around a Circle"),
            Tr("Rays radiate from a base ring; ray length is proportional to value. Series occupy contiguous angular "
            "sectors with configurable gaps. Features bar and line styles, cap shapes including arrows, gradient fade, "
            "global/per-series/manual scaling, ring guides, peak annotations, tooltips and hover highlighting.")
        );

        auto controlsCard = DemoKit::AddCard(page, "RbControls");
        // A card holds a 460 px chart without squeezing it.
        constexpr float chartCardWidth = 460 + 2 * DemoKit::kCardPadding + 2;
        auto charts = DemoKit::AddRow(page, "RbCharts");

        // ===== EXAMPLE 1: MULTI-SENSOR RADIAL BARS =====
        auto sensorCard = DemoKit::AddCard(charts, "RbSensorLabel", Tr("Multi-Sensor Readings (6 Sensors × 24 Hours)"), "", chartCardWidth);

        auto sensorChart = BuildSensorChart("SensorRadialBars", 0, 0, 460, 320);
        sensorChart->SetCenterText(Tr("Sensors"));
        sensorChart->SetGradientFade(true);
        sensorChart->SetCapStyle(RadialBarCapStyle::Round);
        sensorChart->SetShowPeakLabels(true);
        sensorChart->SetNormalization(RadialBarNormalization::Global);
        sensorChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(sensorCard, sensorChart);

        // ===== EXAMPLE 2: GEOGRAPHIC RADIAL LINES =====
        auto geoCard = DemoKit::AddCard(charts, "RbGeoLabel", Tr("Median Age by Country (Radial Lines per Continent)"), "", chartCardWidth);

        auto geoChart = BuildMedianAgeRays("GeoRadialLines", 0, 0, 460, 320);
        geoChart->SetBarStyle(RadialBarStyle::Lines);
        geoChart->SetGradientFade(false);
        geoChart->SetInnerRadiusFraction(0.18f);
        geoChart->SetSeriesGapAngle(6.0f);
        geoChart->SetCenterText(Tr("Median Age"));
        geoChart->SetCenterFont("Arial", 11.0f, FontWeight::Bold);
        geoChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            return TrF("{0} yrs", buf);
        });
        geoChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(geoCard, geoChart);

        // ===== EXAMPLE 3: PARTIAL-SWEEP RAINFALL FAN =====
        auto rainCard = DemoKit::AddCard(charts, "RbRainLabel", Tr("Monthly Rainfall (270° Fan, Ring Guides, Square Caps)"), "", chartCardWidth);

        auto rainChart = BuildRainfallChart("RainfallFan", 0, 0, 460, 280);
        rainChart->SetSweepAngle(270.0f);
        rainChart->SetStartAngle(-225.0f);
        rainChart->SetCapStyle(RadialBarCapStyle::Square);
        rainChart->SetGradientFade(false);
        rainChart->SetShowRingGuides(true);
        rainChart->SetRingGuideCount(4);
        rainChart->SetInnerRadiusFraction(0.3f);
        rainChart->SetCenterText("2026");
        rainChart->SetShowPeakLabels(true);
        rainChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f mm", v);
            return std::string(buf);
        });
        rainChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(rainCard, rainChart);

        // ===== EXAMPLE 4: ARROW RAYS / MANUAL RANGE =====
        auto kpiCard = DemoKit::AddCard(charts, "RbKpiLabel", Tr("KPI Attainment (Arrow Caps, Manual 0–150% Range)"), "", chartCardWidth);

        auto kpiChart = CreateRadialBarChart("KpiRadialBars", 0, 0, 460, 280);
        kpiChart->AddSeries(Tr("Sales"), {
            {Tr("Q1"), 92}, {Tr("Q2"), 108}, {Tr("Q3"), 117}, {Tr("Q4"), 131}
        }, Color(15, 157, 88, 255));
        kpiChart->AddSeries(Tr("Support"), {
            {Tr("Q1"), 74}, {Tr("Q2"), 88}, {Tr("Q3"), 95}, {Tr("Q4"), 103}
        }, Color(66, 133, 244, 255));
        kpiChart->AddSeries(Tr("Engineering"), {
            {Tr("Q1"), 99}, {Tr("Q2"), 112}, {Tr("Q3"), 104}, {Tr("Q4"), 121}
        }, Color(244, 180, 0, 255));
        kpiChart->SetValueRange(0.0, 150.0);
        kpiChart->SetCapStyle(RadialBarCapStyle::Arrow);
        kpiChart->SetGradientFade(false);
        kpiChart->SetBarWidth(5.0f);
        kpiChart->SetSeriesGapAngle(10.0f);
        kpiChart->SetInnerRadiusFraction(0.28f);
        kpiChart->SetShowRingGuides(true);
        kpiChart->SetRingGuideCount(3);
        kpiChart->SetCenterText(Tr("KPI %"));
        kpiChart->SetValueFormatter([](double v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f%%", v);
            return std::string(buf);
        });
        kpiChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(kpiCard, kpiChart);

        // === CONTROL PANEL (above the charts) ===
        CreateRadialBarControlPanel(controlsCard,
                                    {sensorChart, geoChart, rainChart, kpiChart});

        return page;
    }

} // namespace UltraCanvas
