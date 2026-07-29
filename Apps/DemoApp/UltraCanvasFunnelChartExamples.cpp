// Apps/DemoApp/UltraCanvasFunnelChartExamples.cpp
// Funnel chart showcase: the segmented e-commerce funnel with conversion necks,
// a solid marketing cone with leader-line callouts, the rounded card layout, a
// dark presentation funnel with badges and annotations, a horizontal sales
// funnel with a terminal callout, and a playground exposing every shape, scale
// and colour mode live.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasFunnelChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"

#include <memory>
#include <vector>
#include <string>
#include <cstdio>
#include <random>

namespace UltraCanvas {

namespace {

constexpr int kTabContentW = 1240;
constexpr int kTabContentH = 700;
// The demo shell shows roughly this much of a tab without scrolling
constexpr int kInfographicW = 1030;

// =============================================================================
// TAB 1: SEGMENTED E-COMMERCE FUNNEL
// =============================================================================
// Detached bars whose breadth tracks the value, joined by tapering necks that
// carry the stage-to-stage conversion. The classic analytics funnel.

std::shared_ptr<UltraCanvasUIElement> BuildSegmentedTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("FunnelSegmentedTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("SegTitle", 30, 12, 700, 26);
    title->SetText("E-commerce campaign funnel");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("SegSubtitle", 30, 40, 800, 20);
    subtitle->SetText("Recipients reaching each step, with the conversion into the next step "
                      "drawn on the neck between the bars");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasFunnelChart>("SegmentedFunnel", 20, 70, 660, 570);
    chart->SetShapeMode(FunnelShapeMode::SegmentedBars);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::MinWidthTip);
    chart->SetColorMode(FunnelColorMode::SequentialRamp);
    chart->SetColorRamp(Color(158, 202, 232, 255), Color(21, 74, 120, 255));
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetShowConversionOnConnector(true);
    chart->SetAbbreviateValues(true);
    chart->SetStageGap(26.0);
    chart->SetStageLabelColumnWidth(90.0);
    chart->SetPercentColumnWidth(72.0);

    chart->AddStage("Sent", 5680);
    chart->AddStage("Viewed", 3875);
    chart->AddStage("Clicked", 1669);
    chart->AddStage("Add to Cart", 610);
    chart->AddStage("Purchased", 565);
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("SegNotes", 700, 90, 330, 480);
    notes->SetText(
        "Reading the chart\n\n"
        "Each bar's width is its share of the top\n"
        "stage, so the taper is the funnel.\n\n"
        "The pale neck between two bars carries\n"
        "the conversion from the upper stage into\n"
        "the lower one - 68.22% of everyone who\n"
        "was sent the mail opened it.\n\n"
        "The right-hand column is a different\n"
        "figure: the share of the very first stage,\n"
        "so it always falls monotonically.\n\n"
        "Where the funnel leaks\n\n"
        "  - The worst step is Clicked to Add to\n"
        "    Cart at 36.57%.\n"
        "  - Once a basket exists the funnel barely\n"
        "    leaks at all: 92.62% check out.\n"
        "  - End to end, 9.95% of recipients buy.\n\n"
        "Hover any bar for the raw count, both\n"
        "percentages and the number lost at that\n"
        "step.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    return tab;
}

// =============================================================================
// TAB 2: CONTINUOUS MARKETING CONE
// =============================================================================
// One solid funnel: the outline tapers evenly to a point while each slab's
// thickness tracks its value, and the stages name themselves on leader lines.

std::shared_ptr<UltraCanvasUIElement> BuildContinuousTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("FunnelContinuousTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("ConTitle", 30, 12, 700, 26);
    title->SetText("Marketing funnel");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("ConSubtitle", 30, 40, 800, 20);
    subtitle->SetText("Slab thickness tracks the value, so the area of each band is the "
                      "population it holds");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasFunnelChart>("ContinuousFunnel", 20, 70, 700, 590);
    chart->SetShapeMode(FunnelShapeMode::ContinuousFunnel);
    chart->SetScaleMode(FunnelScaleMode::HeightProportional);
    chart->SetTipMode(FunnelTipMode::PointTip);
    chart->SetColorMode(FunnelColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(58, 160, 165, 255), Color(233, 126, 43, 255), Color(18, 71, 82, 255),
        Color(150, 145, 24, 255), Color(120, 130, 30, 255), Color(211, 62, 54, 255)
    });
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::LeaderLineLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::OutsideEndValues);
    chart->SetPercentPlacement(FunnelPercentPlacement::HidePercentColumn);
    chart->SetShowConversionOnConnector(false);
    chart->SetStageLabelColumnWidth(210.0);
    chart->SetValueFormat("%.0f");

    chart->AddStage("Impressions", 1163);
    chart->AddStage("Clicks", 742);
    chart->AddStage("Site Visits", 121);
    chart->AddStage("White Paper Download", 72);
    chart->AddStage("Free Sign Up", 56);
    chart->AddStage("Upgrade", 42);
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("ConNotes", 730, 90, 300, 480);
    notes->SetText(
        "HeightProportional scaling\n\n"
        "In this mode the funnel's outline tapers\n"
        "evenly from the full width down to a point,\n"
        "and it is the thickness of each band that\n"
        "carries the value.\n\n"
        "That is why Impressions occupies most of\n"
        "the picture while the four smallest stages\n"
        "are compressed into the tip.\n\n"
        "LeaderLineLabels\n\n"
        "Each stage is named outside the shape and\n"
        "joined to it by a leader line. Because the\n"
        "value labels are also placed in the end\n"
        "column, the two merge into one string:\n"
        "'Clicks (742)'.\n\n"
        "Watch the tip\n\n"
        "PointTip closes the funnel completely past\n"
        "the last stage. Use FlatNeck with\n"
        "SetNeckRatio() when the final step should\n"
        "keep a visible width.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    return tab;
}

// =============================================================================
// TAB 3: CARD ROWS
// =============================================================================
// Every stage gets a rounded row card; the wedge runs through the cards and the
// gaps between them, with the name on the left and the percentage on the right.

std::shared_ptr<UltraCanvasUIElement> BuildCardRowsTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("FunnelCardsTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("CardTitle", 30, 12, 700, 26);
    title->SetText("Card row funnel");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("CardSubtitle", 30, 40, 800, 20);
    subtitle->SetText("The same data laid out as rounded rows - the layout dashboards reach "
                      "for when the funnel sits beside other cards");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasFunnelChart>("CardFunnel", 20, 70, 700, 580);
    chart->SetShapeMode(FunnelShapeMode::CardRows);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::PointTip);
    chart->SetColorMode(FunnelColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(47, 62, 156, 255), Color(58, 88, 214, 255), Color(88, 128, 246, 255),
        Color(252, 200, 90, 255), Color(238, 122, 44, 255)
    });
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetShowConversionOnConnector(false);
    chart->SetShowLegend(true);
    chart->SetLegendPosition(FunnelLegendPosition::LegendTopCenter);
    chart->SetStageGap(10.0);
    chart->SetStageLabelColumnWidth(110.0);
    chart->SetPercentColumnWidth(70.0);
    chart->SetCardStyle(Color(255, 255, 255, 255), Color(214, 222, 233, 255), 20.0);

    chart->AddStage("Sent", 5680);
    chart->AddStage("Viewed", 3875);
    chart->AddStage("Clicked", 1669);
    chart->AddStage("Add to Cart", 610);
    chart->AddStage("Purchased", 565);
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("CardNotes", 730, 100, 300, 460);
    notes->SetText(
        "CardRows\n\n"
        "Each stage is drawn inside a rounded card\n"
        "that spans the whole element, and the\n"
        "wedge passes through the card and the gap\n"
        "below it without breaking.\n\n"
        "The name column and the percentage column\n"
        "are the same ones the other shapes use;\n"
        "here they simply land inside the card.\n\n"
        "Hovering a row tints its border with the\n"
        "stage colour, which is why the cards make\n"
        "good click targets for drill-down.\n\n"
        "The legend is switched on for this tab.\n"
        "It lists every stage with its swatch and\n"
        "can be pinned to any of six positions.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    return tab;
}

// =============================================================================
// TAB 4: PRESENTATION INFOGRAPHIC
// =============================================================================
// Sheared slabs on a dark ground, numbered badges down the left and annotated
// callouts on the right - the deck-slide funnel.

std::shared_ptr<UltraCanvasUIElement> BuildInfographicTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("FunnelInfographicTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    // The chart fills the tab so its own dark background is the slide background
    auto chart = std::make_shared<UltraCanvasFunnelChart>("InfographicFunnel", 0, 0,
                                                          kInfographicW, kTabContentH - 10);
    chart->SetChartTitle("PRODUCT ADOPTION PIPELINE");
    chart->SetBackgroundColor(Color(46, 43, 74, 255));
    chart->SetShapeMode(FunnelShapeMode::SkewedInfographic);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::PointTip);
    chart->SetColorMode(FunnelColorMode::PerStageOverride);
    chart->SetSlabSkew(18.0);
    chart->SetStageGap(14.0);
    chart->SetUseGradientFill(true, 0.28);
    chart->SetShowTipTriangle(true);

    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::InsideStageLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::HideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::HidePercentColumn);
    chart->SetShowConversionOnConnector(false);
    chart->SetShowBadges(true, 26.0);
    chart->SetBadgeColor(Color(190, 185, 225, 255));
    chart->SetDescriptionColumnWidth(280.0);
    chart->SetDescriptionStyle(11.0, Color(205, 200, 235, 255));
    chart->SetLeaderLineColor(Color(120, 115, 160, 255));
    chart->SetInsideLabelColor(Color(255, 255, 255, 255));
    chart->SetAutoContrastInsideLabels(false);
    chart->SetStageLabelColor(Color(225, 222, 245, 255));

    struct InfoStage {
        const char* label;
        double value;
        const char* description;
        Color color;
    };
    const InfoStage stages[] = {
        {"Awareness", 100, "Reached by the launch campaign", Color(157, 92, 214, 255)},
        {"Interest",   72, "Opened the product tour",        Color(232, 74, 190, 255)},
        {"Evaluation", 44, "Started a trial workspace",      Color(56, 199, 224, 255)},
        {"Adoption",   21, "Converted to a paid seat",       Color(84, 124, 226, 255)},
    };

    for (const auto& entry : stages) {
        FunnelStage stage(entry.label, entry.value);
        stage.description = entry.description;
        stage.stageColor = entry.color;
        chart->AddStage(stage);
    }
    tab->AddChild(chart);

    return tab;
}

// =============================================================================
// TAB 5: HORIZONTAL SALES FUNNEL
// =============================================================================
// The same element rotated: the funnel flows left to right, every stage labels
// itself inside its own slab and the outcome is circled past the tip.

std::shared_ptr<UltraCanvasUIElement> BuildHorizontalTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("FunnelHorizontalTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("HorTitle", 30, 12, 700, 26);
    title->SetText("Sales funnel - conversion rate tracking");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("HorSubtitle", 30, 40, 800, 20);
    subtitle->SetText("HorizontalFunnel flows left to right; the circled figure past the tip "
                      "is the outcome the pipeline is being judged on");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasFunnelChart>("HorizontalFunnel", 20, 80, 990, 330);
    chart->SetOrientation(FunnelOrientation::HorizontalFunnel);
    chart->SetShapeMode(FunnelShapeMode::ContinuousFunnel);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::MinWidthTip);
    chart->SetColorMode(FunnelColorMode::PerStageOverride);
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::InsideStageLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::InsideStagePercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetShowConversionOnConnector(false);
    chart->SetShowTerminalCallout(true, 26.0);
    chart->SetPercentFormat("%.0f%%");
    chart->SetAutoContrastInsideLabels(true);

    struct SalesStage { const char* label; double value; Color color; };
    const SalesStage stages[] = {
        {"Inquiries",   389, Color(150, 150, 150, 255)},
        {"Lead",        252, Color(0, 174, 239, 255)},
        {"Opportunity", 121, Color(252, 191, 45, 255)},
        {"Sales",        40, Color(122, 193, 66, 255)},
    };
    for (const auto& entry : stages) {
        FunnelStage stage(entry.label, entry.value);
        stage.stageColor = entry.color;
        chart->AddStage(stage);
    }
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("HorNotes", 30, 430, 990, 240);
    notes->SetText(
        "Orientation\n\n"
        "SetOrientation(FunnelOrientation::HorizontalFunnel) swaps the flow axis and the cross axis. "
        "Everything else - the shape modes, the label columns, the connector necks, the hit testing - "
        "works exactly as it does vertically, so a funnel can be rotated without touching any other setting.\n\n"
        "Inside labels\n\n"
        "This tab puts the stage name, its value and its share of the first stage inside the slab. "
        "The chart measures the text against the slab before drawing it and simply omits any line that will "
        "not fit, which is why the narrow Sales band drops to fewer lines than Inquiries. "
        "SetAutoContrastInsideLabels(true) picks white or near-black text per stage from the fill's brightness, "
        "so the grey, blue, amber and green bands all stay readable.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    notes->SetWrap(TextWrap::WrapWord);
    tab->AddChild(notes);

    return tab;
}

// =============================================================================
// TAB 6: PLAYGROUND - EVERY MODE, LIVE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildPlaygroundTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("FunnelPlaygroundTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("PlayTitle", 30, 12, 700, 26);
    title->SetText("Playground: shape, scale and colour modes");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("PlaySubtitle", 30, 40, 800, 20);
    subtitle->SetText("A recruitment pipeline broken down by source - every switch below "
                      "restyles the same data");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasFunnelChart>("PlaygroundFunnel", 20, 70, 640, 590);
    chart->SetShapeMode(FunnelShapeMode::SegmentedBars);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetColorMode(FunnelColorMode::SequentialRamp);
    chart->SetColorRamp(Color(150, 205, 190, 255), Color(20, 90, 100, 255));
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetStageLabelColumnWidth(120.0);
    chart->SetPercentColumnWidth(80.0);
    chart->SetPercentFormat("%.1f%%");

    // Sub-segments turn each stage into a stack: the funnel still narrows, but
    // every band also shows where its population came from.
    struct PipelineStage {
        const char* label;
        double referral;
        double jobBoard;
        double outbound;
    };
    const PipelineStage pipeline[] = {
        {"Applied",    420, 980, 600},
        {"Screened",   310, 420, 210},
        {"Interviewed",190, 150,  85},
        {"Offered",     72,  38,  18},
        {"Hired",       55,  24,  11},
    };

    const Color kReferral(64, 145, 160, 255);
    const Color kJobBoard(226, 152, 60, 255);
    const Color kOutbound(120, 110, 190, 255);

    for (const auto& row : pipeline) {
        FunnelStage stage(row.label, row.referral + row.jobBoard + row.outbound);
        stage.segments.push_back(FunnelSubSegment("Referral", row.referral, kReferral));
        stage.segments.push_back(FunnelSubSegment("Job board", row.jobBoard, kJobBoard));
        stage.segments.push_back(FunnelSubSegment("Outbound", row.outbound, kOutbound));
        stage.targetValue = stage.stageValue * 1.15;   // The plan for this stage
        chart->AddStage(stage);
    }
    tab->AddChild(chart);

    // ===== CONTROL PANEL =====
    int panelX = 680;
    int y = 78;

    auto controlsTitle = std::make_shared<UltraCanvasLabel>("PlayControls", panelX, y, 340, 22);
    controlsTitle->SetText("Chart controls");
    controlsTitle->SetFontSize(13);
    controlsTitle->SetFontWeight(FontWeight::Bold);
    tab->AddChild(controlsTitle);
    y += 28;

    auto shapeLabel = std::make_shared<UltraCanvasLabel>("PlayShapeLabel", panelX, y, 340, 18);
    shapeLabel->SetText("Shape mode");
    shapeLabel->SetFontSize(11);
    tab->AddChild(shapeLabel);
    y += 20;

    auto shapeDrop = std::make_shared<UltraCanvasDropdown>("PlayShape", panelX, y, 330, 26);
    shapeDrop->AddItem("Segmented bars + necks");
    shapeDrop->AddItem("Continuous funnel");
    shapeDrop->AddItem("Card rows");
    shapeDrop->AddItem("Skewed infographic");
    shapeDrop->AddItem("Bar style (diminishing bars)");
    shapeDrop->SetSelectedIndex(0, false);
    shapeDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetShapeMode(FunnelShapeMode::ContinuousFunnel); break;
            case 2: chart->SetShapeMode(FunnelShapeMode::CardRows); break;
            case 3: chart->SetShapeMode(FunnelShapeMode::SkewedInfographic); break;
            case 4: chart->SetShapeMode(FunnelShapeMode::BarStyleFunnel); break;
            default: chart->SetShapeMode(FunnelShapeMode::SegmentedBars); break;
        }
    };
    tab->AddChild(shapeDrop);
    y += 36;

    auto scaleLabel = std::make_shared<UltraCanvasLabel>("PlayScaleLabel", panelX, y, 340, 18);
    scaleLabel->SetText("Scale mode");
    scaleLabel->SetFontSize(11);
    tab->AddChild(scaleLabel);
    y += 20;

    auto scaleDrop = std::make_shared<UltraCanvasDropdown>("PlayScale", panelX, y, 330, 26);
    scaleDrop->AddItem("Width proportional");
    scaleDrop->AddItem("Height proportional");
    scaleDrop->AddItem("Area proportional");
    scaleDrop->AddItem("Equal stages (pipeline)");
    scaleDrop->SetSelectedIndex(0, false);
    scaleDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetScaleMode(FunnelScaleMode::HeightProportional); break;
            case 2: chart->SetScaleMode(FunnelScaleMode::AreaProportional); break;
            case 3: chart->SetScaleMode(FunnelScaleMode::EqualStages); break;
            default: chart->SetScaleMode(FunnelScaleMode::WidthProportional); break;
        }
    };
    tab->AddChild(scaleDrop);
    y += 36;

    auto colorLabel = std::make_shared<UltraCanvasLabel>("PlayColorLabel", panelX, y, 340, 18);
    colorLabel->SetText("Colour mode (stages here carry their own segment colours)");
    colorLabel->SetFontSize(11);
    tab->AddChild(colorLabel);
    y += 20;

    auto colorDrop = std::make_shared<UltraCanvasDropdown>("PlayColor", panelX, y, 330, 26);
    colorDrop->AddItem("Sequential ramp");
    colorDrop->AddItem("Categorical palette");
    colorDrop->AddItem("Single hue");
    colorDrop->AddItem("Colour by conversion");
    colorDrop->SetSelectedIndex(0, false);
    colorDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetColorMode(FunnelColorMode::CategoricalPalette); break;
            case 2: chart->SetColorMode(FunnelColorMode::SingleHue); break;
            case 3: chart->SetColorMode(FunnelColorMode::ThresholdColor); break;
            default: chart->SetColorMode(FunnelColorMode::SequentialRamp); break;
        }
    };
    tab->AddChild(colorDrop);
    y += 36;

    auto tipLabel = std::make_shared<UltraCanvasLabel>("PlayTipLabel", panelX, y, 340, 18);
    tipLabel->SetText("Tip mode");
    tipLabel->SetFontSize(11);
    tab->AddChild(tipLabel);
    y += 20;

    auto tipDrop = std::make_shared<UltraCanvasDropdown>("PlayTip", panelX, y, 330, 26);
    tipDrop->AddItem("Last stage keeps its width");
    tipDrop->AddItem("Close to a point");
    tipDrop->AddItem("Close to a flat neck");
    tipDrop->SetSelectedIndex(0, false);
    tipDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetTipMode(FunnelTipMode::PointTip); break;
            case 2: chart->SetTipMode(FunnelTipMode::FlatNeck); break;
            default: chart->SetTipMode(FunnelTipMode::MinWidthTip); break;
        }
    };
    tab->AddChild(tipDrop);
    y += 38;

    auto gapLabel = std::make_shared<UltraCanvasLabel>("PlayGapLabel", panelX, y, 340, 18);
    gapLabel->SetText("Gap between stages");
    gapLabel->SetFontSize(11);
    tab->AddChild(gapLabel);
    y += 20;

    auto gapSlider = std::make_shared<UltraCanvasSlider>("PlayGap", panelX, y, 330, 24);
    gapSlider->SetRange(0.0f, 60.0f);
    gapSlider->SetStep(1.0f);
    gapSlider->SetValue(22.0f);
    gapSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    gapSlider->onValueChanged = [chart](float value) { chart->SetStageGap(value); };
    tab->AddChild(gapSlider);
    y += 34;

    auto horizontalCheck = std::make_shared<UltraCanvasCheckbox>("PlayHorizontal", panelX, y,
                                                                 330, 24, "Horizontal orientation");
    horizontalCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetOrientation(newState == CheckedState::Checked
                                      ? FunnelOrientation::HorizontalFunnel
                                      : FunnelOrientation::VerticalFunnel);
    };
    tab->AddChild(horizontalCheck);
    y += 26;

    auto gradientCheck = std::make_shared<UltraCanvasCheckbox>("PlayGradient", panelX, y,
                                                               330, 24, "Gradient fills");
    gradientCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetUseGradientFill(newState == CheckedState::Checked, 0.25);
    };
    tab->AddChild(gradientCheck);
    y += 26;

    auto targetCheck = std::make_shared<UltraCanvasCheckbox>("PlayTarget", panelX, y,
                                                             330, 24, "Target overlay (plan + 15%)");
    targetCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowTargetOverlay(newState == CheckedState::Checked);
    };
    tab->AddChild(targetCheck);
    y += 26;

    auto bottleneckCheck = std::make_shared<UltraCanvasCheckbox>("PlayBottleneck", panelX, y,
                                                                 330, 24, "Highlight the bottleneck");
    bottleneckCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetHighlightBottleneck(newState == CheckedState::Checked);
    };
    tab->AddChild(bottleneckCheck);
    y += 26;

    auto dimCheck = std::make_shared<UltraCanvasCheckbox>("PlayDim", panelX, y,
                                                          330, 24, "Dim stages you are not hovering");
    dimCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetDimUnhoveredStages(newState == CheckedState::Checked);
    };
    tab->AddChild(dimCheck);
    y += 26;

    auto dropOffCheck = std::make_shared<UltraCanvasCheckbox>("PlayDropOff", panelX, y,
                                                              330, 24, "Show the loss at each step");
    dropOffCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowDropOffLabels(newState == CheckedState::Checked);
    };
    tab->AddChild(dropOffCheck);
    y += 32;

    auto selectionTitle = std::make_shared<UltraCanvasLabel>("PlaySelTitle", panelX, y, 340, 20);
    selectionTitle->SetText("Selected stage");
    selectionTitle->SetFontSize(12);
    selectionTitle->SetFontWeight(FontWeight::Bold);
    tab->AddChild(selectionTitle);
    y += 22;

    auto selection = std::make_shared<UltraCanvasLabel>("PlaySelection", panelX, y, 340, 120);
    selection->SetText("Click a stage to read its figures here.\nClick a neck to report the "
                       "conversion through it.");
    selection->SetFontSize(11);
    selection->SetTextColor(Color(70, 70, 70, 255));
    selection->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(selection);

    // The callbacks report indices into the data source, so sorting cannot shift them
    auto source = chart->GetFunnelDataSource();
    chart->onStageClick = [selection, source, chart](size_t stageIndex) {
        if (!source || stageIndex >= source->GetPointCount()) return;
        const auto& stage = source->GetStage(stageIndex);
        FunnelStageMetrics metrics = chart->GetStageMetrics(stageIndex);

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
                      "%s\n\nReached: %.0f\nOf first stage: %.1f%%\nFrom previous: %.1f%%\n"
                      "Lost here: %.0f\n\nReferral %.0f / Job board %.0f / Outbound %.0f",
                      stage.stageLabel.c_str(), metrics.value,
                      metrics.percentOfFirst, metrics.percentOfPrevious, metrics.dropOff,
                      stage.segments.size() > 0 ? stage.segments[0].value : 0.0,
                      stage.segments.size() > 1 ? stage.segments[1].value : 0.0,
                      stage.segments.size() > 2 ? stage.segments[2].value : 0.0);
        selection->SetText(buffer);
    };

    chart->onConnectorClick = [selection, source, chart](size_t upperStageIndex) {
        if (!source || upperStageIndex + 1 >= source->GetPointCount()) return;
        const auto& upper = source->GetStage(upperStageIndex);
        const auto& lower = source->GetStage(upperStageIndex + 1);
        FunnelStageMetrics metrics = chart->GetStageMetrics(upperStageIndex + 1);

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
                      "%s -> %s\n\nConversion: %.1f%%\nLost: %.0f candidates",
                      upper.stageLabel.c_str(), lower.stageLabel.c_str(),
                      metrics.percentOfPrevious, metrics.dropOff);
        selection->SetText(buffer);
    };

    return tab;
}

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFunnelChartExamples() {
    auto container = std::make_shared<UltraCanvasContainer>(
        "FunnelChartExamples", 0, 0, kTabContentW + 20, kTabContentH + 60);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "FunnelChartTabs", 0, 10, kTabContentW + 10, kTabContentH + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab("Segmented Funnel", BuildSegmentedTab());
    tabs->AddTab("Continuous Cone", BuildContinuousTab());
    tabs->AddTab("Card Rows", BuildCardRowsTab());
    tabs->AddTab("Presentation Infographic", BuildInfographicTab());
    tabs->AddTab("Horizontal Funnel", BuildHorizontalTab());
    tabs->AddTab("Playground", BuildPlaygroundTab());
    tabs->SetActiveTab(0);

    container->AddChild(tabs);
    return container;
}

} // namespace UltraCanvas
