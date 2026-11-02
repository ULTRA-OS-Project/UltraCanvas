// Apps/DemoApp/UltraCanvasCircularProgressChartExamples.cpp
// Circular progress chart demonstration: concentric rings, single ring,
// progress pie and the extended donut KPI pie.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls
//                  and each example in a card, rows that wrap, text sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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

    // The controls stack in the panel card; a header is as wide as the card
    // and wraps there, every control keeps its own width.
    static void CreateCircularProgressControlPanel(
        std::shared_ptr<UltraCanvasContainer> panel,
        std::vector<std::shared_ptr<UltraCanvasCircularProgressChart>> charts)
    {
        const int controlWidth = 180;
        const int controlHeight = 25;

        auto addHeader = [&panel](const std::string& id, const std::string& text) {
            auto header = DemoKit::AddText(panel, id, text);
            header->SetFontWeight(FontWeight::Bold);
        };

        addHeader("CpCapHeader", "Arc Caps:");

        auto capDropdown = std::make_shared<UltraCanvasDropdown>("CpCap", 0, 0, controlWidth, controlHeight);
        capDropdown->AddItem("Round Cap");
        capDropdown->AddItem("Butt Cap");
        capDropdown->SetSelectedIndex(0);
        capDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            for (auto& c : charts) {
                c->SetCapStyle(index == 1 ? RingCapStyle::Butt : RingCapStyle::Round);
            }
        };
        DemoKit::AddFixed(panel, capDropdown);

        auto trackDropdown = std::make_shared<UltraCanvasDropdown>("CpTrack", 0, 0, controlWidth, controlHeight);
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
        DemoKit::AddFixed(panel, trackDropdown);

        auto tipDropdown = std::make_shared<UltraCanvasDropdown>("CpTip", 0, 0, controlWidth, controlHeight);
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
        DemoKit::AddFixed(panel, tipDropdown);

        addHeader("CpInnerHeader", "Centre Hole Size:");

        auto innerSlider = std::make_shared<UltraCanvasSlider>("CpInner", 0, 0, controlWidth, controlHeight);
        innerSlider->SetRange(0.1, 0.6);
        innerSlider->SetStep(0.01);
        innerSlider->SetValue(0.28);
        innerSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetInnerRadiusFraction(static_cast<float>(value));
        };
        DemoKit::AddFixed(panel, innerSlider);

        addHeader("CpSpacingHeader", "Ring Spacing (px):");

        auto spacingSlider = std::make_shared<UltraCanvasSlider>("CpSpacing", 0, 0, controlWidth, controlHeight);
        spacingSlider->SetRange(0.0, 16.0);
        spacingSlider->SetStep(0.5);
        spacingSlider->SetValue(4.0);
        spacingSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetRingSpacing(static_cast<float>(value));
        };
        DemoKit::AddFixed(panel, spacingSlider);

        auto clockwiseCheckbox = std::make_shared<UltraCanvasCheckbox>("CpClockwise", 0, 0, controlWidth, controlHeight);
        clockwiseCheckbox->SetText("Clockwise");
        clockwiseCheckbox->SetChecked(true);
        clockwiseCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetClockwise(newState == CheckedState::Checked);
        };
        DemoKit::AddFixed(panel, clockwiseCheckbox);

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("CpHover", 0, 0, controlWidth, controlHeight);
        hoverCheckbox->SetText("Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        DemoKit::AddFixed(panel, hoverCheckbox);
    }

// ===== MAIN CIRCULAR PROGRESS CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCircularProgressChartExamples() {
        auto page = DemoKit::CreatePage("CircularProgressContainer",
            "Circular Progress Chart Examples - Angle-Encoded Progress Rings",
            "One value per ring, encoded as arc sweep. Sub-styles: concentric rings, single progress ring and "
            "progress pie. Features tracks (auto-tint / explicit / hidden), round caps, arc-tip callouts, stacked "
            "start labels, centre disc with text, numbered legend chips, tooltips, hover and click callbacks.");

        // A card is at least as wide as its chart (plus padding and border).
        const float cardChrome = 2 * DemoKit::kCardPadding + 2;

        // The control panel on the left, the four examples beside it. The
        // examples stay beside the panel while one card fits there (their
        // cards then wrap under each other) and move under it after that.
        auto mainRow = DemoKit::AddRow(page, "CpMainRow");
        auto panel = DemoKit::CreateCard("CpControlPanel");
        DemoKit::Add(mainRow, panel);
        auto examples = DemoKit::CreateColumn("CpExamples", DemoKit::kPageGap);
        DemoKit::Add(mainRow, examples, 1);
        examples->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(460 + cardChrome));

        // ===== EXAMPLE 1: INFOGRAPHIC RINGS (the reference image) =====
        auto topRow = DemoKit::AddRow(examples, "CpTopRow");
        auto infoCard = DemoKit::AddCard(topRow, "CpInfoCard",
            "Circular Infographic (Numbered Legend, Centre Disc, Tip Callouts)", "", 460 + cardChrome);

        auto infoChart = BuildInfographicRings("InfographicRings", 0, 0, 460, 320);
        DemoKit::AddFixed(infoCard, infoChart);

        // ===== EXAMPLE 2: STACKED START LABELS =====
        auto stackedCard = DemoKit::AddCard(topRow, "CpStackedCard",
            "Radial Bar Card (Stacked Start Labels, No Tracks)", "", 460 + cardChrome);

        auto stackedChart = BuildStackedLabelRings("StackedLabelRings", 0, 0, 460, 320);
        DemoKit::AddFixed(stackedCard, stackedChart);

        // ===== EXAMPLE 3: SINGLE COMPLETION RING + PROGRESS PIE =====
        auto bottomRow = DemoKit::AddRow(examples, "CpBottomRow");
        auto singleCard = DemoKit::AddCard(bottomRow, "CpSingleCard",
            "Single Completion Ring and Progress Pie", "", 460 + cardChrome);
        auto singlePair = DemoKit::CreateRow("CpSinglePair", 0, false);
        DemoKit::Add(singleCard, singlePair);

        auto completionRing = BuildCompletionRing("CompletionRing", 0, 0, 230, 280);
        DemoKit::AddFixed(singlePair, completionRing);

        auto progressPie = CreateProgressPieChart("ProgressPie", 0, 0, 230, 280,
                                                  "Storage used", 65.0,
                                                  Color(40, 140, 230, 255));
        progressPie->SetTrackColor(Color(236, 240, 244, 255));
        progressPie->SetTrackMode(RingTrackMode::Explicit);
        progressPie->SetTooltipsEnabled(true);
        DemoKit::AddFixed(singlePair, progressPie);

        // ===== EXAMPLE 4: DONUT KPI PIE (new pie element features) =====
        auto donutCard = DemoKit::AddCard(bottomRow, "CpDonutCard",
            "Donut KPI (Pie Element: Start Angle, Centre KPI, Click Callback)", "", 460 + cardChrome);

        auto revenueData = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> revenue = {
            ChartDataPoint(1, 520, 0, "Subscriptions", 520),
            ChartDataPoint(2, 310, 0, "Services", 310),
            ChartDataPoint(3, 205, 0, "Licenses", 205),
            ChartDataPoint(4, 165, 0, "Other", 165)
        };
        revenueData->LoadFromArray(revenue);
        auto donut = CreatePieChartElement("KpiDonut", 0, 0, 460, 280);
        donut->SetDataSource(revenueData);
        donut->SetDonutMode(true);
        donut->SetInnerRadius(0.62f);
        donut->SetStartAngle(-90.0f);
        donut->SetCenterKPI("$1.2M", "Total revenue");
        donut->SetLabelPosition(LabelPosition::Outside);
        donut->SetLabelContent(LabelContent::NamePercentage);
        donut->onSliceClick = [donut = donut.get()](size_t index) {
            // Pop the clicked slice out and reset the others.
            donut->ClearAllSliceExplosions();
            donut->SetSliceExplosion(index, 0.08f);
        };
        donut->SetTooltipsEnabled(true);
        DemoKit::AddFixed(donutCard, donut);

        // === CONTROL PANEL (left column) ===
        CreateCircularProgressControlPanel(panel, {infoChart, stackedChart});

        return page;
    }

} // namespace UltraCanvas
