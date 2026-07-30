// Apps/DemoApp/UltraCanvasCircularProgressChartExamples.cpp
// Circular progress chart demonstration: concentric rings, single ring,
// progress pie and the extended donut KPI pie.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"
#include "Plugins/Charts/UltraCanvasPieChart.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <cstdio>

namespace UltraCanvas {

// ===== SAMPLE CHART BUILDERS =====

    // The classic "circular chart infographic": four concentric rings, one
    // independent percentage each, numbered legend chips and a centre disc.
    static std::shared_ptr<UltraCanvasCircularProgressChart> BuildInfographicRings(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreateCircularProgressChart(id, x, y, w, h);
        chart->AddRing("Research",  48.0, Color(255,  62, 133, 255));   // magenta
        chart->AddRing("Marketing", 54.0, Color(255, 200,  40, 255));   // yellow
        chart->AddRing("Sales",     64.0, Color(150, 200,  60, 255));   // green
        chart->AddRing("Support",   83.0, Color(120,  90, 200, 255));   // purple

        chart->SetCapStyle(RingCapStyle::Round);
        chart->SetTrackMode(RingTrackMode::AutoTint);
        chart->SetTipLabelContent(RingTipLabel::Percentage);
        chart->SetCenterDisc(Color(225, 30, 40, 255), Colors::White);
        chart->SetCenterText("Lorem", "ipsum");
        chart->SetCenterTextColor(Colors::White);
        chart->SetLegendStyle(CircularLegendStyle::NumberedChips,
                              CircularLegendPosition::Left);
        chart->SetTooltipsEnabled(true);
        return chart;
    }

    // The "Radial Bar Chart" card look: concentric arcs with a stacked column
    // of "Legend NN%" rows aligned with each ring's start point.
    static std::shared_ptr<UltraCanvasCircularProgressChart> BuildStackedLabelRings(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreateConcentricRingChart(id, x, y, w, h, {
            {"Legend", 22.0},
            {"Legend", 27.0},
            {"Legend", 38.0},
            {"Legend", 39.0}
        });
        chart->SetColorPalette({
            Color(243, 156,  18, 255),   // orange
            Color(241, 196,  15, 255),   // yellow
            Color( 66, 133, 244, 255),   // blue
            Color( 26, 188, 156, 255)    // teal
        });
        chart->SetTipLabelContent(RingTipLabel::NoLabel);
        chart->SetRingNameLabels(RingNamePosition::StackedStart);
        chart->SetTrackMode(RingTrackMode::Hidden);
        chart->SetInnerRadiusFraction(0.35f);
        chart->SetTooltipsEnabled(true);
        return chart;
    }

    // The "Completed 72%" card: one thick ring over a grey track with a
    // caption + value in the centre.
    static std::shared_ptr<UltraCanvasCircularProgressChart> BuildCompletionRing(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreateCircularProgressChart(id, x, y, w, h);
        chart->SetSubStyle(CircularProgressStyle::SingleRing);
        chart->AddRing("Completed", 72.0, Color(120, 90, 220, 255));
        chart->SetInnerRadiusFraction(0.68f);
        chart->SetTrackMode(RingTrackMode::Explicit);
        chart->SetTrackColor(Color(240, 240, 243, 255));
        chart->SetTipLabelContent(RingTipLabel::NoLabel);
        chart->SetCenterText("72%", "Completed");
        chart->SetCenterFont("Arial", 22.0f, FontWeight::Bold);
        chart->SetTooltipsEnabled(true);
        return chart;
    }

// ===== CONTROL PANEL =====

    static void CreateCircularProgressControlPanel(
        std::shared_ptr<UltraCanvasContainer> container,
        std::vector<std::shared_ptr<UltraCanvasCircularProgressChart>> charts,
        int x, int y)
    {
        int yOffset = y;
        int controlHeight = 25;
        int spacing = 8;

        auto capHeader = std::make_shared<UltraCanvasLabel>("CpCapHeader", x, yOffset, 200, 20);
        capHeader->SetText("Arc Caps:");
        capHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(capHeader);
        yOffset += 25;

        auto capDropdown = std::make_shared<UltraCanvasDropdown>("CpCap", x, yOffset, 180, controlHeight);
        capDropdown->AddItem("Round Cap");
        capDropdown->AddItem("Butt Cap");
        capDropdown->SetSelectedIndex(0);
        capDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            for (auto& c : charts) {
                c->SetCapStyle(index == 1 ? RingCapStyle::Butt : RingCapStyle::Round);
            }
        };
        container->AddChild(capDropdown);
        yOffset += controlHeight + spacing;

        auto trackDropdown = std::make_shared<UltraCanvasDropdown>("CpTrack", x, yOffset, 180, controlHeight);
        trackDropdown->AddItem("Auto-Tint Tracks");
        trackDropdown->AddItem("Grey Tracks");
        trackDropdown->AddItem("No Tracks");
        trackDropdown->SetSelectedIndex(0);
        trackDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            RingTrackMode mode;
            switch (index) {
                case 1:  mode = RingTrackMode::Explicit; break;
                case 2:  mode = RingTrackMode::Hidden;   break;
                default: mode = RingTrackMode::AutoTint; break;
            }
            for (auto& c : charts) c->SetTrackMode(mode);
        };
        container->AddChild(trackDropdown);
        yOffset += controlHeight + spacing;

        auto tipDropdown = std::make_shared<UltraCanvasDropdown>("CpTip", x, yOffset, 180, controlHeight);
        tipDropdown->AddItem("Tip: Percentage");
        tipDropdown->AddItem("Tip: Value");
        tipDropdown->AddItem("Tip: None");
        tipDropdown->SetSelectedIndex(0);
        tipDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            RingTipLabel content;
            switch (index) {
                case 1:  content = RingTipLabel::Value;   break;
                case 2:  content = RingTipLabel::NoLabel; break;
                default: content = RingTipLabel::Percentage; break;
            }
            for (auto& c : charts) c->SetTipLabelContent(content);
        };
        container->AddChild(tipDropdown);
        yOffset += controlHeight + spacing * 2;

        auto innerHeader = std::make_shared<UltraCanvasLabel>("CpInnerHeader", x, yOffset, 200, 20);
        innerHeader->SetText("Centre Hole Size:");
        innerHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(innerHeader);
        yOffset += 25;

        auto innerSlider = std::make_shared<UltraCanvasSlider>("CpInner", x, yOffset, 180, controlHeight);
        innerSlider->SetRange(0.1, 0.6);
        innerSlider->SetStep(0.01);
        innerSlider->SetValue(0.28);
        innerSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetInnerRadiusFraction(static_cast<float>(value));
        };
        container->AddChild(innerSlider);
        yOffset += controlHeight + spacing;

        auto spacingHeader = std::make_shared<UltraCanvasLabel>("CpSpacingHeader", x, yOffset, 200, 20);
        spacingHeader->SetText("Ring Spacing (px):");
        spacingHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(spacingHeader);
        yOffset += 25;

        auto spacingSlider = std::make_shared<UltraCanvasSlider>("CpSpacing", x, yOffset, 180, controlHeight);
        spacingSlider->SetRange(0.0, 16.0);
        spacingSlider->SetStep(0.5);
        spacingSlider->SetValue(4.0);
        spacingSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetRingSpacing(static_cast<float>(value));
        };
        container->AddChild(spacingSlider);
        yOffset += controlHeight + spacing * 2;

        auto clockwiseCheckbox = std::make_shared<UltraCanvasCheckbox>("CpClockwise", x, yOffset, 180, controlHeight);
        clockwiseCheckbox->SetText("Clockwise");
        clockwiseCheckbox->SetChecked(true);
        clockwiseCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetClockwise(newState == CheckedState::Checked);
        };
        container->AddChild(clockwiseCheckbox);
        yOffset += controlHeight + spacing;

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("CpHover", x, yOffset, 180, controlHeight);
        hoverCheckbox->SetText("Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        container->AddChild(hoverCheckbox);
    }

// ===== MAIN CIRCULAR PROGRESS CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCircularProgressChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("CircularProgressContainer", 0, 0, 1200, 810);

        // === TITLE ===
        auto titleLabel = std::make_shared<UltraCanvasLabel>("CpTitleLabel", 20, 10, 1160, 35);
        titleLabel->SetText("Circular Progress Chart Examples - Angle-Encoded Progress Rings");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(titleLabel);

        // === DESCRIPTION ===
        auto descLabel = std::make_shared<UltraCanvasLabel>("CpDescLabel", 20, 55, 1160, 60);
        descLabel->SetText(
            "One value per ring, encoded as arc sweep. Sub-styles: concentric rings, single progress ring and\n"
            "progress pie. Features tracks (auto-tint / explicit / hidden), round caps, arc-tip callouts, stacked\n"
            "start labels, centre disc with text, numbered legend chips, tooltips, hover and click callbacks."
        );
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: INFOGRAPHIC RINGS (the reference image) =====
        auto infoLabel = std::make_shared<UltraCanvasLabel>("CpInfoLabel", 240, 130, 460, 25);
        infoLabel->SetText("Circular Infographic (Numbered Legend, Centre Disc, Tip Callouts)");
        infoLabel->SetFontSize(13);
        infoLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(infoLabel);

        auto infoChart = BuildInfographicRings("InfographicRings", 240, 160, 460, 320);
        container->AddChild(infoChart);

        // ===== EXAMPLE 2: STACKED START LABELS =====
        auto stackedLabel = std::make_shared<UltraCanvasLabel>("CpStackedLabel", 720, 130, 460, 25);
        stackedLabel->SetText("Radial Bar Card (Stacked Start Labels, No Tracks)");
        stackedLabel->SetFontSize(13);
        stackedLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(stackedLabel);

        auto stackedChart = BuildStackedLabelRings("StackedLabelRings", 720, 160, 460, 320);
        container->AddChild(stackedChart);

        // ===== EXAMPLE 3: SINGLE COMPLETION RING + PROGRESS PIE =====
        auto singleLabel = std::make_shared<UltraCanvasLabel>("CpSingleLabel", 240, 490, 460, 25);
        singleLabel->SetText("Single Completion Ring and Progress Pie");
        singleLabel->SetFontSize(13);
        singleLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(singleLabel);

        auto completionRing = BuildCompletionRing("CompletionRing", 240, 520, 230, 280);
        container->AddChild(completionRing);

        auto progressPie = CreateProgressPieChart("ProgressPie", 470, 520, 230, 280,
                                                  "Storage used", 65.0,
                                                  Color(40, 140, 230, 255));
        progressPie->SetTrackColor(Color(236, 240, 244, 255));
        progressPie->SetTrackMode(RingTrackMode::Explicit);
        progressPie->SetTooltipsEnabled(true);
        container->AddChild(progressPie);

        // ===== EXAMPLE 4: DONUT KPI PIE (new pie element features) =====
        auto donutLabel = std::make_shared<UltraCanvasLabel>("CpDonutLabel", 720, 490, 460, 25);
        donutLabel->SetText("Donut KPI (Pie Element: Start Angle, Centre KPI, Click Callback)");
        donutLabel->SetFontSize(13);
        donutLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(donutLabel);

        auto revenueData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> revenue = {
            ChartDataPoint(1, 520, 0, "Subscriptions", 520),
            ChartDataPoint(2, 310, 0, "Services", 310),
            ChartDataPoint(3, 205, 0, "Licenses", 205),
            ChartDataPoint(4, 165, 0, "Other", 165)
        };
        revenueData->LoadFromArray(revenue);
        auto donut = CreatePieChartElement("KpiDonut", 720, 520, 460, 280);
        donut->SetDataSource(revenueData);
        donut->SetDonutMode(true);
        donut->SetInnerRadius(0.62f);
        donut->SetStartAngle(-90.0f);
        donut->SetCenterKPI("$1.2M", "Total revenue");
        donut->SetLabelPosition(LabelPosition::Outside);
        donut->SetLabelContent(LabelContent::NamePercentage);
        donut->onSliceClick = [donut](size_t index) {
            // Pop the clicked slice out and reset the others.
            donut->ClearAllSliceExplosions();
            donut->SetSliceExplosion(index, 0.08f);
        };
        donut->SetTooltipsEnabled(true);
        container->AddChild(donut);

        // === CONTROL PANEL (left column) ===
        CreateCircularProgressControlPanel(container,
                                           {infoChart, stackedChart},
                                           20, 130);

        return container;
    }

} // namespace UltraCanvas
